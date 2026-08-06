# PA13 `lowir2cy86` Plan

Status: **PA13 complete** — `tests/spec` 96/96, `make test-report-through-pa12`
848/848, file audit clean.

## Stage Design

PA13 is a backend adapter: read LowIR text, validate it structurally, and emit
PA9 CY86 source text. Ownership:

| Owner | Responsibility |
| --- | --- |
| `dev/src/lowir_model.h` | typed LowIR program model (shared with later PAs) |
| `dev/src/lowir_text.{h,cpp}` | LowIR lexer + parser + `TypeFacts`; the text boundary |
| `dev/src/lowir_validate.{h,cpp}` | structural well-formedness + runtime-role resolution |
| `dev/src/lowir_cy86.{h,cpp}` | typed program -> CY86 text adapter |
| `dev/lowir2cy86.cpp` | driver: `-o out in...`, `EXIT_FAILURE` on any error |

`lowir_model::Program` is the only interface between layers: the parser never
emits text, the emitter never re-reads source characters.

### CY86 target model (recovered from `tests/spec/*.ref`)

* Symbols: `@f` -> `fn__f`, `@g` -> `g__g`, `^b` in `@f` -> `fn__f__b`, plus a
  synthetic `fn__f__epilogue`. Functions print before globals; blank line between.
* Frame: `params (incl. synthetic indirect-result ptr) | slots | temps`, each cell
  `max(8, roundup8(size))` (`f80`/`obj<16x*>` = 16), addressed `[bp-off]`. A fixed
  64-byte / four-entry 16-byte scratch area is appended iff the function uses
  `convert` or any `f80`.
* Calling convention: args in `x64,y64,z64,t64` then `[sp]`, `[sp+8]`, ...;
  incoming stack args at `[bp+16]`. `f80`/`obj` params and returns pass by
  address, the return pointer being a synthetic leading parameter.
* Values live in memory: every instruction loads operands into `x/y/z/t`,
  computes, stores the destination. `cmp` always materialises canonical `i64` 0/1.
* Operand rules: a temporary yields its cell contents, a slot or global yields its
  address, and a memory-class expectation yields the operand's address.
* EH is a runtime handler stack in `g____cppgm_eh_top` / `g____cppgm_eh_value`
  with a `fn____cppgm_eh_unhandled` trampoline, synthesised on demand; internal
  `__eh_*` / `__atomic_cmpxchg_*` labels share one program-wide counter.

### Reference behaviours deliberately reproduced

The `.ref` CY86 text is the graded oracle, so these reference quirks are matched
rather than "fixed" (all verified by assembling the checked-in `.ref` itself):

* a 5th+ call argument is staged through `x64`, clobbering argument 0;
* `load` through a temporary whose LowIR type is `obj<...>` reads the object
  bytes as an address;
* `bswap*` is not a PA9 opcode and bare negative immediates (`move64 x64 -1`,
  `data64 -69...`) are outside the PA9 immediate grammar, so a few emitted
  programs cannot be re-assembled by `dev/cy86`.

## Current Failure Map

Turn-start baseline 0/96; final 96/96. Groups and where they are handled:

| Group | Cases | Owner |
| --- | --- | --- |
| G1 structural rejection | 29 | `lowir_text` lexer/parser + `lowir_validate` |
| G2 scalar core | 21 | frame layout, const/copy/load/store/addr/index, ALU, control flow, calls, globals |
| G3 object/ABI | 8 | `obj<BxA>`, `copyobj`, `zeroinit`, indirect result, stack args |
| G4 `f80` + conversions | 8 | 16-byte scratch, `move80`, `*conv*` bridging |
| G5 atomics | 6 | single-threaded lowering, cmpxchg label pairs |
| G6 EH | 3 | handler-stack lowering + synthetic runtime symbols |
| G7 metadata smoke | 21 | metadata parsed, validated, codegen-neutral |

## Active Checkpoint

None — CP1 closed the assignment. Next work on this tool would be driven by
PA14+ needing `serialize_lowir_program` (declared in `lowir_model.h`, not yet
implemented) for the source-to-LowIR direction.

## Performance Model

Measured on this host with generated LowIR (`binary add` chains):

| Input | Time | Peak RSS |
| --- | --- | --- |
| 30k instructions (1.2 MB) | 0.24 s | 76 MB |
| 120k instructions (4.7 MB) | 1.10 s | 222 MB |
| 10k top-level symbols | 0.10 s | 39 MB |

* 4x input -> 4.6x time: linear, no super-linear path. LowIR is flat, so there is
  no nesting-depth term to blow up.
* Single lexing pass into one reserved token vector; parsing uses one-token
  lookahead and no backtracking, so nothing is re-scanned or re-parsed.
* Parsed nodes are moved, not copied, into the program: `push_back(std::move(...))`
  for functions, blocks and instructions removed a full deep copy of every
  function body (388 MB -> 319 MB peak at 120k instructions).
* Shrinking `Token` to one string plus reserving the token vector removed the
  1.5x reallocation transient (319 MB -> 222 MB, 1.26 s -> 1.10 s).
* Frame layout is computed once per function into `unordered_map`, so every
  operand reference is an O(1) lookup rather than a scan of the param/slot/temp
  lists; program symbol tables are likewise hashed and built once.
* Output is appended to one `std::string`: linear in output size.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| CP1 | Whole `lowir2cy86` adapter: LowIR lexer/parser, structural validator, CY86 emitter (G1-G7) | 96/96 `tests/spec`; prior PAs 848/848; audit clean. Sweeps: linear scaling to 120k instructions; valgrind clean on 8 fixtures + 5 synthesized probes; multiplicity probes (multi-file program, 4-arm switch, 6-arg direct and indirect calls, repeated cmpxchg, cross-function EH labels, mixed-alignment structured data) execute correctly through `dev/cy86`. Probe-found and fixed: a sub-32-bit `load` through a pointer cleared the destination register that still held its own address. |
