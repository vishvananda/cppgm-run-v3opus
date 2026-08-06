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

After C1 + C2 and the audit of them: 70 / 243. The 173 that remain:

| group | count | what is missing |
| --- | --- | --- |
| special member declarations (`X::X`, `~X`) | 64 | 12.1/12.4 user ctors and dtors |
| base classes | 29 | 10.1 single inheritance, layout and lookup |
| operator overloading | 23 | 13.5 over the object model |
| LowIR shape diffs, the program otherwise accepted | 17 | later checkpoints |
| bit-fields | 10 | 9.6 layout and access |
| default member initializers | 7 | 12.6.2p8, refused until a constructor has a body |
| ADL and hidden-friend lookup | 6 | 3.4.2 over the associated classes |
| other refusals | 17 | scattered, mostly downstream of the above |

The relaxed comparison strips `object=`, `binding=`, `linkage=`, `role=` and
`unwind=`, so a passing test says nothing about them. Diffed directly against the
references, the one gap left is that `noexcept` sets no `unwind=no`: 20 pa16
refs carry it, the AST keeps `function-qualifier noexcept` and `lowir_write`
already emits it, and what is missing between them is a fact on the declaration.
Three fixtures pass today while emitting a function boundary that does not say
what the source said.

## Active Checkpoint

Next: **C3 — user-declared constructors and destructors** (12.1, 12.4, 12.6.2),
which is the largest remaining group and which two paths already leave a place
for: `construct_object` writes the `constructor-action` and would choose the
constructor by 13.3 over the class's constructor chain, and a `ctor-initializer`
becomes member-initialization actions of the shape aggregate initialization
already produces. It also carries three things the audit left standing on it: a
constructor body, which is what lets 12.6.2p8's default member initializers stop
being refused; the C1/C2 constructor ABI names and their alias, which the refs
write as `_ZN1XC1Ev` and `alias object _ZN1XC2Ev`; and a destructor as an action
at block exit and in `@__cppgm_fini`.

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
- A slot named after an identifier another slot already took starts from the
  suffix that identifier last used, so n blocks declaring one name cost n steps.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in under 0.01 s to a
  29-line program with one `zeroinit 1048575x1` and one `zero 1048575`, so a
  bound the source wrote as one number costs one node rather than 2^20.
- Measured at the end of the audit, each doubling about 2.1x: 8000 members in one
  class laid out and initialized twice, 0.14 s; 4000 member accesses and calls in
  one body, 0.20 s; 2000 classes each with an object and a member call, 0.19 s;
  4000 blocks each declaring a class object, 0.23 s.
- Nested aggregate depth is quadratic in emitted lines and flat per line, because
  naming each subobject from the object again is the shape the refs write:
  depth 800 is 0.80 s for 323617 lines, the same 2.4 µs per line as depth 100.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
| audit of C1-C2 | 9.4.2p2's definition told from its declaration by the declarator-id; 12.6.2p8 refused rather than dropped; 7.6.2p1's type-id form; 5.2.5p1's object expression kept or refused, never dropped; 13.3.3.2p3 ordering two sequences that differ only in qualifiers; 11p6's naming context; 9.3p2 read from where the definition is written; O(n^2) slot naming | 65 -> 70 / 243; pa1-pa15 1173/1173; valgrind clean over 249 inputs; every axis linear; stripped metadata diffed against the refs; findings and evidence in `audit.md` |
