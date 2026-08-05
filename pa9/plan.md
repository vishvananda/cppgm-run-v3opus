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

Ownership of typed facts:

- `Cy86OpcodeTable` (`cy86_opcodes.cpp`) owns the operand constraints, kept as
  the handout's `cy86-opcode.desc` text and parsed once into `Cy86OpcodeInfo`,
  indexed by spelling.  It also answers "is this spelling reserved" for the
  label rule.
- `Cy86Program` owns statements, their labels, and the label -> statement map.
  A label is a `NameId`, so nothing compares strings after parsing.
- `Cy86Codegen` owns the image bytes, the label -> address vector and the
  relocation list.  No statement is read twice.

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
multiply or divide, r11 an address being built.  Red zone `[rsp-16]` and
`[rsp-32]` are the x87 spill slots.

## Current Failure Map

Turn-start baseline 0/18; now 20/20 (18 course and assignment tests plus two
regression tests added this turn).  No failures remain.

## Active Checkpoint

None: the assignment passes.  The next turn is PA10.

## Performance Model

Measured on this host, release build:

| Case | Result |
| --- | --- |
| compile 500-to-float80 / 600-float-calculator | < 0.01 s each |
| synthetic 50k-statement program | 0.19 s, 42 MB |
| synthetic 200k-statement program | 0.88 s, 157 MB (4x statements, 4.6x time) |
| generated 500-to-float80 on 1,000,000 inputs | 0.29 s |
| generated 600-float-calculator on 34 MB of input | 0.82 s |

Parsing is a single forward scan with one token of lookahead and no
backtracking; identifier facts (register, opcode, label) are decided once per
`NameId` and cached in a vector, not once per use.  Emission is one pass,
relocations are appended rather than searched, and label addresses live in a
vector indexed by `NameId`, so resolving a reference is a load.  The
statement-count scaling above is the linear behaviour that predicts.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| CP1 | The whole cy86 backend: opcode table, parser, semantic checks, layout and relocation, integer/control/x87 lowering, ELF writer | 20/20 pa9, 426/426 through pa8, file audit clean |

### CP1 notes

Two defects found by differential testing against `cy86-ref` on a synthesized
492-operation program, neither reachable from the checked-in suite:

- The ELF entry pointed at the CY86 entry statement rather than at the image,
  so the prologue never ran and `bp` stayed zero.  Regression test
  `cppgm.tests/course/pa9/200-initial-register-state.t.1`, which segfaults on
  the defective build.
- `f80conv{s,u}N` used the FISTP form of the destination width, which answers
  an out-of-range value with the integer indefinite instead of its low bits.
  Every conversion now goes through the 64-bit form.

After both fixes the synthesized program's output is byte-identical to the
reference's across every width, signedness and float form.  `move80` is not
comparable: the reference reports it unimplemented on this host.
</content>
