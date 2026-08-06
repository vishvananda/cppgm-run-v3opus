# PA16 Plan — `cppgm++ --emit-lowir` object model

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs:

- `sema_scope.*` owns declarations, regions and lookup. A class member's
  placement in its object is a fact about the declaration, so it lives on the
  member's own `SemaEntity`.
- `sema_analyzer.*` owns 9.2 layout, 12.1 special members and 13.3 overload
  resolution, and writes the resolved tree with typed `SemaFact`s.
- `lowir_lower*.cpp` reads only that resolved tree. It never re-resolves a name
  and never reads syntax.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The PA16 object model is added as new typed facts at those owners rather than
as a second pipeline: field offsets on members, an implicit-object argument in
13.3, a `constructor-action` fact on a declaration, and a demand-driven
definition worklist in the unit lowering.

## Current Failure Map

Turn-start baseline: 33 / 243. After C1 + C2: 65 / 243.

| group | count | what is missing |
| --- | --- | --- |
| special member declarations (`X::X`, `~X`) | ~48 | 12.1/12.4 user ctors and dtors |
| base classes | ~30 | 10.1 single inheritance, layout and lookup |
| bit-fields | ~11 | 9.6 layout and access |
| arrays of class type | ~6 | element-wise construction helpers |
| operator overloading | ~12 | 13.5 over the object model |
| remaining LowIR shape diffs | rest | later checkpoints |

## Active Checkpoint

Next: **C3 — user-declared constructors and destructors** (12.1, 12.4, 12.6.2),
which is the largest remaining group and which the aggregate path already
leaves a place for: `construct_object` chooses a constructor by 13.3 over the
class's constructor chain, a `ctor-initializer` becomes member-initialization
actions of the same shape aggregate initialization already produces, and a
destructor becomes an action at block exit and in `@__cppgm_fini`.

## Performance Model

- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, so a function used n times is lowered once. The worklist is drained at
  the top level, so `program_.functions` never reallocates under a live
  `Function&` reference.
- Access control is a scope-chain walk from the naming context, bounded by the
  nesting depth of the use, not by the size of the class.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in 0.004 s to a 33-line
  program with one `zeroinit 1048575x1` and one `zero 1048575`, so a bound the
  source wrote as one number costs one node rather than 2^20.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
