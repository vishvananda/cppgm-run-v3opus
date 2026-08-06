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

Turn-start baseline: 33 / 243.

| group | count | what is missing |
| --- | --- | --- |
| special member declarations (`X::X`, `~X`) | ~48 | 12.1/12.4 user ctors and dtors |
| base classes | ~28 | 10.1 single inheritance, layout and lookup |
| member function calls | ~22 | 13.3.1 implicit object argument |
| LowIR shape only (compiles, output differs) | ~11 | class objects in LowIR |
| bit-fields | ~9 | 9.6 layout and access |
| lowering subset rejects | ~15 | `constructor-action`, `member-expression` |
| operators / misc | rest | later checkpoints |

## Active Checkpoint

**C1 — the member function call, and the class object in LowIR.**

- Owner
  - `SemaEntity::offset` — 9.2p13 placement of a non-static data member, set
    once by `SemaAnalyzer::lay_out_class`.
  - `SemaEntity::inline_function` / `SemaEntity::trivial` — 7.1.2 and 12.1p5
    facts about a declaration, set where it is declared.
  - `SemaAnalyzer::member_call_expression` — 5.2.5 + 13.3.1 resolution.
  - `LowirUnitLowering::demanded_` — the definitions a use asked for.
- Data flow: layout writes offsets at class completion -> `member-expression`
  and `constructor-action` facts carry the member/constructor entity ->
  `LowirFunctionLowering` reads the offset and emits
  `index i8 [projection=field]` -> a call to an inline or implicit definition
  pushes its node onto the worklist -> the worklist is drained between
  top-level declarations, never while a `Function&` is live.
- Expected complexity: layout O(members) once per class; member access O(1) per
  resolved node; overload selection one row per candidate as before; each
  definition emitted at most once.
- Validation: `make test-report-through-pa15` clean; `make -C pa16 test` above
  33.

## Performance Model

- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, so a function used n times is lowered once. The worklist is drained at
  the top level, so `program_.functions` never reallocates under a live
  `Function&` reference.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
