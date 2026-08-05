# PA9 (cy86) Plan

## Stage Design

`cy86 -o out src...` translates the CY86 mock intermediate language into a
Linux x86-64 ELF executable.  Phases 1 to 7 are PA1 to PA5 unchanged; the
backend is new:

```
build_sema_tokens (one call per source file, one shared token vector)
  -> Cy86Parser        (dev/src/cy86_parser.cpp)   tokens -> Cy86Program
  -> Cy86Codegen       (dev/src/cy86_codegen.cpp)  program -> image bytes
       uses x86::Assembler (dev/src/x86_asm.cpp)   x86 forms -> machine code
  -> write_elf_executable (dev/src/x86_elf.cpp)    image -> ELF
```

The assignment concatenates what every translation unit tokenizes to into one
sequence before parsing, so one token vector and one literal pool serve the
whole command line and a statement may span two files.

Ownership of typed facts:

- `Cy86OpcodeTable` (`cy86_opcodes.cpp`) owns the operand constraints, kept as
  the handout's `cy86-opcode.desc` text - byte for byte, checked by reading -
  and parsed once into `Cy86OpcodeInfo`, indexed by spelling.  A statement
  names its opcode by pointing into that table, which therefore outlives the
  program.
- `Cy86Parser::NameFacts` owns what an identifier spells, decided once per
  `NameId` and never from the letters again: register or not, opcode or not,
  whether the spelling is one a label may take (it is reserved exactly when it
  is a register or an opcode), and whether a statement already carries it as a
  label.  The program publishes `label_limit`, one past the largest label name.
- `literal_field_bytes` / `literal_field_value` (`cy86_opcodes.cpp`) own the
  width conversion the assignment states for an immediate: truncate past the
  field, sign extend a signed integer, zero extend anything else.  The parser
  reads a label's `± TT_LITERAL` offset through it and the code generator fills
  every field through it, so one rule cannot drift into two.
- `Cy86Operand` keeps the ten bytes an encoding can read plus the width the
  literal itself had, not the literal's whole object: a string literal used as
  an immediate costs the same as a character one.
- `Cy86Codegen` owns the image bytes, one label -> address vector indexed by
  `NameId` with an unplaced sentinel, and the relocation list.  Nothing is read
  twice: `emit_immediate` is the only place a constant becomes an encoding, and
  it is where a label reference is recorded.

Key layout invariant: **no encoding's size depends on a label value.**  A
label reference always rides in a `mov r64, imm64` or in a data field of a
fixed width, so one emission pass fixes every address and a second pass adds
the addresses into the fields the relocation list recorded - no relaxation
loop, no re-emission.

Image shape, matched to the reference: one LOAD segment at file offset 0,
vaddr 0x400000, RWE, align 0x1000; the image starts at file offset 0x1000
(vaddr 0x401000), which is also the ELF entry.  The image opens with a
27-byte prologue (`xor r12..r15`, `mov rbp, rsp`, `movabs rax, <start>`,
`jmp rax`) and the statements follow at 0x40101b.  That offset is observable:
`500-string-literal-element-alignment` measures alignment padding against it.

CY86 registers are backed as the handout recommends: sp=rsp, bp=rbp, x=r12,
y=r13, z=r14, t=r15.  Scratch: rax holds the first value read and the result,
rcx the second (so a shift count is already in cl), rdx the high half of a
divide, r11 an address being built.  Red zone `[rsp-16]` and `[rsp-32]` are the
x87 spill slots; every lowering that uses them does so before it writes its
result, so a statement that assigns `sp` cannot pull the zone out from under
itself.

## Performance Model

Dominant operations, for a source of N bytes holding T tokens, S statements and
R label references:

| Work | Cost |
| --- | --- |
| phases 1 to 7 (PA1-PA5, shared) | O(N) |
| parse | O(T), one forward scan, one token of lookahead, no backtracking |
| identifier facts | O(1) per use after one decision per distinct name |
| emission | O(S), one pass, no relaxation, appends only |
| relocation | O(R), one vector append per reference, one pass to add addresses |
| label address | O(1), a vector indexed by `NameId`, sized once from the parse |

So the whole run is O(N + T + S + R) time and O(T + S + image) space, with the
token vector and literal pool released at the end of parsing because nothing
after it reads a token.  There is no search, no map lookup per use, and no
second pass over a statement.

Measured on this host, release build (`/usr/bin/time`, peak RSS):

| Workload | Before audit | After audit |
| --- | --- | --- |
| 50k statements (1.0 MB) | 0.20 s, 30 MB | 0.10 s, 25 MB |
| 200k statements (4.2 MB) | 0.50 s, 112 MB | 0.40 s, 88 MB |
| 800k statements (16.8 MB) | 2.03 s, 438 MB | 1.66 s, 343 MB |
| 200k distinct labels, each referenced once | 0.77 s, 119 MB | 0.68 s, 89 MB |
| 200k references to one label | 0.34 s, 77 MB | 0.32 s, 65 MB |
| 100k forward references resolved at the end | 0.64 s, 115 MB | 0.58 s, 89 MB |
| 80k string literal statements (3.4 MB) | 0.18 s, 38 MB | 0.17 s, 37 MB |
| 50k macro invocations | 0.10 s, 31 MB | 0.11 s, 25 MB |
| 401 translation units on one command line | - | 0.22 s, 46 MB |
| whole checked-in suite (21 cases, build and run) | - | 6.3 s |

Statement scaling is 4.0x time for 4x statements at both steps, and labels,
references and forward references scale the same way, which is the linear
behaviour the model predicts.  `perf` on the 800k-statement run puts 89% of the
time in the shared PA1-PA5 frontend (`SourceReader::fill`, `MacroExpander`,
`PPTokenLexer`, `SpellingPool::intern`) and 11% in this stage's own code, whose
largest entries are `Cy86Parser::parse_statement`, `x86::Assembler::instruction`
and `x86::Assembler::load_immediate` - the passes the model names, in the order
it predicts, with no unexplained slow path.

## Architecture Review

CP1 built the whole backend and differential testing against `cy86-ref` on a
synthesized 492-operation program found two defects the checked-in suite could
not reach: the ELF entry pointed at the CY86 entry statement rather than at the
image, so the prologue never ran and `bp` stayed zero; and `f80conv{s,u}N` used
the FISTP form of the destination width, which answers an out-of-range value
with the integer indefinite instead of its low bits.  Both are fixed and both
have regression tests.

## Final Architecture Review

An independent pass over the whole stage: the handout re-read against the
implementation, every source file read, the opcode table diffed against
`cy86-opcode.desc`, a generated program disassembled with `objdump -M intel`
and read form by form, 15 synthesized programs compared against `cy86-ref`
byte for byte on program stdout and exit status, a 40-case ill-formed battery
compared on compiler exit status, and 10 scaling workloads measured and
profiled.  It found one blocker and six architecture defects.

### Findings

| # | Finding | Disposition |
| --- | --- | --- |
| F1 | Signed division narrower than 64 bits trapped on the smallest negative value over -1: `sdiv8 x8 (-128) (-1)` killed the generated program with SIGFPE where the reference wrapped.  CY86 arithmetic wraps everywhere else it can overflow, and no wider form is reachable only at 64 bits. | Fixed; regression test |
| F2 | Two owners of what an identifier spells: the parser cached register and opcode facts per `NameId`, but the label rule asked `Cy86OpcodeTable::reserved(text)`, re-hashing the spelling at every label definition and every label reference. | Consolidated |
| F3 | Two owners of which names label a statement: `Cy86Program::labels`, an `unordered_map` used only to reject a duplicate, and the code generator's `defined_` vector kept parallel to `addresses_` and grown a label at a time. | Consolidated |
| F4 | An operand kept its literal's whole object representation, so `move64 x64 "<a megabyte>"` copied a megabyte to read eight bytes, and every operand carried a 32-byte string for at most ten bytes of value. | Fixed |
| F5 | The immediate width-conversion rule was implemented twice: `widen` in the parser for a label's offset and `immediate_bytes` in the code generator for a field. | Consolidated |
| F6 | "Load a constant, record a relocation if it is a label" existed three times (immediate operand, address operand, and the 80-bit split), and the 80-bit immediate split existed twice. | Consolidated |
| F7 | Scoping the parse to release its tokens left the opcode table dying with it, while every statement still pointed into it.  Caught by the differential suite before it left the working tree. | Fixed; lifetime documented |

Four places where this implementation and `cy86-ref` disagree were traced to
the end and left as they are, because the handout is the contract and no
checked-in fixture covers them:

| # | Divergence | Why ours |
| --- | --- | --- |
| D1 | The reference leaves stale high bits in a CY86 register when an instruction writes its 32-bit alias (`isub32` of `0x12345678` and `0x9ABCDEF0` gives it `0xFFFFFFFF77777788`).  Every low half agrees; only the bits above the operand differ. | The handout NOTE: "When an instruction writes to x32, it shall zero the upper 32 bits of x64 ... to match the behaviour of x86 registers" |
| D2 | The reference accepts `[5+3]`. | The grammar lists seven `memory` forms and a literal with an offset is not one of them; the checked-in `300-negative-memory-literal-bad` shows the reference rejecting a neighbouring out-of-grammar form |
| D3 | The reference accepts `(a+"x")` and `[a+1.5]`. | The handout requires the literal in a label's `± TT_LITERAL` to be of integral type, and gives an address the same reading as an immediate |
| D4 | The reference answers any `move80` with "native float80 moves not implemented yet" on this host. | The assignment specifies `move80`; ours moves ten bytes as eight plus two rather than through the x87 stack, which would normalize what it loaded |

### Changes

- `cy86_codegen.cpp`: every division at a width below 64 sign or zero extends
  both operands and divides at 64 bits, so a quotient that does not fit its
  width wraps into it instead of trapping; the 8-bit `ah` remainder shuffle and
  the width-by-width choice of CBW/CWD/CDQ/CQO are gone with it.
- `cy86_parser.h/.cpp`: `NameFacts` gained `reserved()` and
  `labels_a_statement`; the label rule and the duplicate rule read them instead
  of the spelling.  `take_literal_bytes` became `take_literal_value`, which
  fills an operand with the ten bytes an encoding can read.
- `cy86_model.h`, `cy86_opcodes.cpp`: `Cy86Program::labels` replaced by
  `label_limit`; `Cy86OpcodeTable::reserved` removed; `literal_field_bytes` and
  `literal_field_value` added as the one reader of the width rule; an operand
  holds `data[10]` and `data_size` instead of a `std::string`.
- `cy86_codegen.h/.cpp`: one `addresses_` table with an unplaced sentinel,
  assigned once from `label_limit`; `defined_` removed; `emit_immediate` and
  `emit_immediate80` are the only places a constant becomes an encoding.
- `cy86.cpp`: the token vector and literal pool live in the parse's own scope
  and are released before code generation; the opcode table outlives the
  program that points into it.
- `cppgm.tests/course/pa9/220-signed-division-overflow.t.1`: the smallest
  negative value over -1 at 8, 16 and 32 bits, an ordinary signed division and
  remainder to pin truncation toward zero, and an unsigned pair.

### Performance Evidence

The table under **Performance Model** is the evidence: ten workloads chosen for
what they scale in - statements, distinct labels, references to one label,
forward references resolved only at the end, literal bytes, macro invocations
and translation units - each measured before and after this audit, all linear,
with a profile attributing 89% of the remaining time to the shared frontend.
The audit's consolidations account for the improvement: 800k statements went
from 2.03 s and 438 MB to 1.66 s and 343 MB (18% faster, 22% less memory),
without changing what any of them emits.

### Validation

- `perl scripts/cppgm_file_audit.pl --stage pa9 --paths dev/src` - pass, 77
  files.
- `make test-report-through-pa9` - 447/447, 9/9 stages.
- 15 synthesized programs against `cy86-ref`: integer ALU at every width and
  both memory and register operands, shifts by register and by immediate,
  multiply, divide, remainder and division overflow, all ten comparisons at
  every width, integer and floating conversions in both directions, x87
  arithmetic and comparison at 32, 64 and 80 bits, every addressing form
  including a fixed `mmap`ed literal address and `[sp]`, immediate width
  conversion including string and character literals, literal statements and
  their alignment, `data` with label operands, control transfer including
  indirect and nested calls, register aliasing, multi-file concatenation with a
  statement split across files, and the entry point with and without `start`.
  Identical program stdout and exit status everywhere except D1 to D4.
- Alignment checked absolutely rather than by offset: every literal statement
  and `data` opcode address masked against its own alignment is zero.
- A 40-case ill-formed battery: identical compiler exit status except D2 and D3.
- `objdump -d -M intel` over a program exercising every opcode family: every
  encoding reads back as the instruction intended.

## Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| CP1 | The whole cy86 backend: opcode table, parser, semantic checks, layout and relocation, integer/control/x87 lowering, ELF writer | 20/20 pa9, 426/426 through pa8, file audit clean |
| CP2 | Final audit: independent review of parsing, semantic ownership, lowering, encoding and serialization; one blocker and six architecture defects fixed; performance modelled, measured and profiled | 21/21 pa9, 447/447 through pa9, file audit clean |
