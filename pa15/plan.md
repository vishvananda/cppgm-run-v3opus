# PA15 `cppgm++ --emit-lowir` Plan

Status: **in progress** — `pa15/tests/general` 87/108, `make test-report-through-pa14`
1065/1065, file audit clean.

## Stage Design

PA15 lowers the resolved PA12 procedural program into PA13 LowIR. The one
architectural decision is where the resolved program lives: **the PA12 dump tree
becomes the resolved procedural tree**, and each of its nodes carries typed
facts.

| Owner | Responsibility |
| --- | --- |
| `dev/src/sema_facts.h` | `FactKind` / `SemaFact`: what a resolved node is, typed |
| `dev/src/sema_*.cpp` | records those facts while PA11/PA12 already walk the tree |
| `dev/src/lowir_lower.{h,cpp}` | program layer: symbols, globals, function shells |
| `dev/src/lowir_lower_body.cpp` | function layer: blocks, statements, expressions |
| `dev/src/lowir_write.cpp` | the only place LowIR text is spelled |
| `dev/src/lowir_emit.{h,cpp}` | the `--emit-lowir` driver mode |

`SemaDialect::Lowering` is the PA12 walk with `record()` enabled. `record()` runs
from `respell()` and from the one dispatch point in `expression()`, so a line's
text and its facts are one description written once. Nothing downstream parses
dump text or re-reads syntax: the lowering sees `FactKind`, `TypeId`,
`ValueCategory`, `SemaEntity*`, the operator token, and the folded constant.

Two facts were added to the semantic layer because the lowering needs them and
only the analysis can know them:

* `SemaFact::operands` — 5p9/5.9p2, the one type a built-in binary operator
  brings both operands to. A comparison's own type (`bool`) does not say it.
* `SemaEntity::object_definition` — 3.1p2, whether a namespace-scope declaration
  defines the object or only declares it, which decides `global` vs
  `declare global`.

### Reference lowering conventions reproduced

Deduced from the checked-in `.ref` files and held to deliberately:

* an lvalue's `LowValue` operand *is* its storage (`$slot`, `@global`, or a
  `ptr` temporary), so reading is a `load` and `&` is at most an `addr`;
* an assignment and a prefix `++` hand back the object they wrote *and* the
  value they wrote (`LowValue::held`), so reading the result costs no reload;
* a built-in binary operator reads **both** operands to values before either is
  converted (`as_value` then `converted`), which is what orders
  `const i64 4` before `convert sext i64 i32 2`;
* a simple assignment evaluates its right operand first; a compound assignment
  reads its left operand first;
* a call evaluates its arguments before its callee;
* branch context tests an integer directly and compares a pointer or a floating
  value with zero; the value context of `&&`/`||` materializes truth at `i64`;
* an integral immediate is respelled at its destination type when the
  destination is signed or the same width, and goes through `convert`
  otherwise; a same-width signedness change or an integer/pointer reading of
  the same bits is `copy`;
* block labels are reserved where their construct starts, and a switch reserves
  every label its substatement writes before reading it, so nesting numbers
  outside-in;
* generated slots (`cond__`, `condaddr__`, `land__`, `lor__`, `refarg__`) share
  one counter that keeps rising past any source name, and a shadowed local slot
  gets a `__shadowN` suffix.

## Current Failure Map

21 of 108 remain. Grouped by what they need:

| Group | Tests | What is missing |
| --- | --- | --- |
| dynamic initialization (`__cppgm_init`, `role=init`) | 5 | 3.6.2 non-constant namespace-scope initializers |
| string literals and their backing globals | 3 | `__strlit__N` globals, array-of-char init |
| default arguments | 3 | PA12 does not resolve a call that omits them |
| `goto` / labelled statements | 2 | PA10/PA12 statement subset |
| braced-init-list as an expression (`e = {E::v}`, `return {}`) | 2 | PA12 expression subset |
| unscoped-enum promotion to `unsigned int` | 2 | 4.5p3 wide-range enums |
| function-type typedef reference / `static_cast` on one | 2 | parser + lowering |
| `void` return of a `void` call, excess-array diagnostic | 2 | small semantic gaps |

## Active Checkpoint

**Checkpoint 1 (this turn): the source-to-LowIR spine.** Owner: the four new
`lowir_*` modules plus the fact recording in the PA11/PA12 walk.

Data flow: source → PA10 AST → PA11/PA12 analysis (unchanged rules) → resolved
tree with `SemaFact` → `LowirProgramBuilder` → `lowir_model::Program` →
`serialize_lowir_program`. Complexity: one pass over the resolved nodes per
function, `O(nodes)`; symbol and slot lookups are hashed; the only repeated walk
is the per-switch label reservation, bounded by that switch's substatement.

Validation: `pa15/tests/general` 87/108 (from 0), `make test-report-through-pa14`
1065/1065, file audit clean.

## Performance Model

Nothing here rescans or rebuilds. Per function: one walk of its resolved nodes,
one `unordered_map` probe per named entity, one `unordered_set` probe per slot
name. Per program: one `unordered_set` probe per symbol declared or defined, and
one linear scan of `program_.functions`/`globals` per definition to reject a
repeat — bounded by the number of top-level entities, which is why it has not
been indexed yet.

The one super-linear shape is `LowirFunctionLowering::holds_label`, which is
consulted only when a statement follows a terminator; it is `O(subtree)` and is
asked at most once per unreachable statement, so a body of n nodes costs `O(n)`
reachable and `O(n * depth)` in the worst dead-code case. PA10's parse-depth
guard bounds that depth, so it cannot run away.

Measured (this machine, one process each):

| Case | Shape | Lowered | Wall |
| --- | --- | --- | --- |
| whole suite | 108 translation units | — | 0.33s total |
| multiplicity | 4000 locals + 4000 assignments | 20005 lines | 0.11s |
| depth | `x + x + ...` 20000 deep | 4008 lines | 0.05s |
| switch multiplicity | 4000 switches | 64008 lines | 0.14s |
| aggregate multiplicity | 3000-element array initializer | 6010 lines | 0.01s |
| dead code | `return` before 200-deep nested blocks | — | 0.00s |

Every curve is linear in emitted lines. Re-measure when dynamic initialization
and string-literal globals add per-object work.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Source-to-LowIR spine: typed facts on the resolved tree, LowIR builder, text writer, `--emit-lowir` driver; functions, globals, all statements incl. `switch`, and the scalar/pointer/array/reference expression subset | pa15 0 → 87/108, through-pa14 1065/1065 |
