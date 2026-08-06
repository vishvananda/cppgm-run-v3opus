# PA16 Plan — `cppgm++ --emit-lowir` object model

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs:

- `sema_scope.*` owns declarations, regions and lookup. Where a member sits in
  its object, and which special member function a declaration declares, are
  facts about the declaration, so they live on its own `SemaEntity`. A class
  holds the chain of its constructors and its destructor, because neither has a
  name an ordinary lookup reaches.
- `sema_analyzer.*` owns 9.2 layout, 12.1/12.4/12.6.2 special members and 13.3
  overload resolution, and writes the resolved tree with typed `SemaFact`s. It
  also owns 3.7.1 storage duration: which region ends an object's lifetime is
  settled where the object is declared, and every way out of that region -
  falling through, `return`, `break`, `continue` - runs one walk over the frames
  it leaves.
- `lowir_lower*.cpp` reads only that resolved tree. It never re-resolves a name
  and never reads syntax.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as new typed facts at those owners rather than as a
second pipeline: field offsets on members, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` nodes, and
a demand-driven definition worklist in the unit lowering.

## Current Failure Map

After C1-C3 and its audit: 102 / 243. The 141 that remain:

| group | count | what is missing |
| --- | --- | --- |
| base classes | 43 | 10.1 single inheritance, layout, lookup, 12.6.2p5 |
| LowIR shape diffs, the program otherwise accepted | 27 | see below |
| operator overloading | 33 | 13.5 over the object model, ADL, hidden friends |
| bit-fields | 10 | 9.6 layout and access |
| other refusals | 28 | scattered, mostly downstream of the above |

Of the 27 shape diffs, the ones still standing are: 8.5p7's zero-initialization
before a non-trivial value-initializing constructor; the exception cleanup
regions the references write around partially constructed and partially
destroyed subobjects (`eh_cleanup` / `eh_try` / `resume`, 3 tests); arrays of
class type constructed and destroyed element by element; and a `declare global`
written for an `extern` object nothing uses.

`unwind=no` is a sixth, and the audit's metadata diff separated it into two
owners rather than one. 15.4p14 gives an implicitly declared or explicitly
defaulted special member an exception-specification of its own — the references
write `unwind=no` on the constructor of `struct X { int m = 1; };`, which spells
no `noexcept` at all — and that is the same 15.4 model the `eh_cleanup` regions
need, so one checkpoint owns both. The direct `noexcept` on a declarator is the
other half and needs a fact carried out of the shared PA11 declarator reader.
Neither changes a test result: the relaxed comparison strips the field, and
emitting nothing is silence rather than a false claim.

## Active Checkpoint

Done: **C3 — user-declared constructors and destructors** (12.1, 12.4, 12.6.2),
and its audit, which settled that a region's storage duration is what ends an
object's lifetime and that every way out of a region runs the same walk.

Next: **C4 — single inheritance** (10.1, 10.2, 11.2, 12.6.2p5), the largest
remaining group at 43 tests and the one every other group is behind: the base
subobject at offset 0 in `lay_out_class`, the base's members in `lookup_in`,
`protected` access, the base's constructor as the first member-initialization
and its destructor as the last member-destruction, and the derived-to-base
conversion in 13.3.3.1.

- owner: `sema_analyzer.cpp` for layout and the two initialization orders,
  `sema_scope.cpp` for the lookup that reaches through a base, `sema_overload.cpp`
  for 13.3.3.1p6's conversion rank.
- data flow: the `base-clause` is read where the class is, records the base on
  the class's `SemaEntity`, and every later question - layout, lookup, access,
  construction order, conversion rank - reads that one fact.
- expected complexity: layout and the two orders stay one pass over
  `Scope::declarations`; lookup walks the base chain, which single inheritance
  bounds by the depth of the hierarchy, not by its size.
- what the audit leaves ready: a mem-initializer-id that names no non-static data
  member is refused by name, so the base's own mem-initializer is the one shape
  C4 has to add to that check rather than a silent drop it has to find.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` over the 43
  base-class tests, `make test-report-through-pa15`, a valgrind run over the new
  paths, and a depth sweep of a chain of derived classes.

## Performance Model

- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, so a function used n times is lowered once. The worklist is drained at
  the top level, so `program_.functions` never reallocates under a live
  `Function&` reference.
- 12.6.2's mem-initializers are indexed by member name once per constructor, so
  a class with n members each named in the ctor-initializer costs n lookups
  rather than n^2 comparisons.
- 12.6.2p10's order is `Scope::declarations`, so one pass writes every member
  initialization, and 12.4p8 walks the same list backwards.
- A slot named after an identifier another slot already took starts from the
  suffix that identifier last used, so n blocks declaring one name cost n steps.
- A jump out of a set of blocks writes one destructor action per object those
  blocks hold, which is what 3.8p1 asks for. Whether any object is alive at all
  is a carried count, not a walk of the open blocks, so a jump that ends no
  lifetime costs nothing per enclosing block.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in under 0.01 s.
- Measured for this checkpoint and its audit, each doubling about 2.1x-2.3x:
  4000 default member initializers in one class, 0.06 s; 4000 mem-initializers in
  one constructor, 0.07 s; 4000 locals with destructors in one block, 0.09 s;
  4000 namespace-scope objects constructed and destroyed, 0.09 s; 4000
  namespace-scope aggregates whose member has a destructor, 0.09 s; 4000 loops
  each with a `break` leaving one object, 0.33 s; 2000 constructor overloads
  chosen between for one call, 0.06 s; a single `break` unwinding 800 nested
  blocks, 0.04 s for 800 destructor calls.
- 3.8p1 makes a return destroy every object of every block it leaves, so n
  nested blocks each holding an object and a return emit n^2/2 calls: measured
  400 deep at 0.36 s for 166425 lines, the same 2.2 us per line as 100 deep.
  That is what the source asks for, not a re-walk.
- Nested block scopes cost more than linearly in their depth, and did before this
  checkpoint: 4000 nested blocks holding one scalar each take 0.23 s and 4000
  nested `for` statements holding one class object each take 1.21 s, both within
  noise of the pre-audit binary. The cost is the lookup walking enclosing
  regions; it belongs to the scope layer, not to the object model.
- Measured earlier and unchanged: 8000 members laid out and initialized twice,
  0.14 s; 4000 member accesses and calls in one body, 0.20 s; 2000 classes each
  with an object and a member call, 0.19 s; nested aggregate depth 800, 0.80 s
  for 323617 lines.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
| audit of C1-C2 | 9.4.2p2's definition told from its declaration by the declarator-id; 12.6.2p8 refused rather than dropped; 7.6.2p1's type-id form; 5.2.5p1's object expression kept or refused, never dropped; 13.3.3.2p3 ordering two sequences that differ only in qualifiers; 11p6's naming context; 9.3p2 read from where the definition is written; O(n^2) slot naming | 65 -> 70 / 243; pa1-pa15 1173/1173; valgrind clean over 249 inputs; every axis linear; stripped metadata diffed against the refs; findings and evidence in `audit.md` |
| C3 | 12.1/12.4 user-declared constructors and destructors in a class body, chained on the class; 13.3.1.3/13.3.1.4/8.5.4p3 constructor selection over 8.5's four initializer forms with `explicit`; 12.6.2 member initializations in declaration order, 12.6.2p8 default member initializers, 12.4p8 member destructions; 3.8p1 lifetime at block exit, at `return` and in 3.6.3p1's `@__cppgm_fini`; 8.4.2/8.4.3 `= default` and `= delete`; 12.8p31 copy elision from a value of the object's own type; 5.2.4 explicit destructor calls; C1/C2 and D1/D2 ABI names with the `alias object` line | 70 -> 102 / 243; pa1-pa15 1173/1173; valgrind clean on the new paths; every new axis linear |
| audit of C3 | six ways out of a region that ended no lifetime - `break`, `continue`, `goto`, the for-init-statement's own region, a static data member at shutdown, an aggregate initialized from braces - with the block-scope `static` written as an automatic object; 9.3.2p1's `this` in a destructor separated from 12.4p12's object parameter; a deleted destructor refused where the object is declared; `= T(...)` no longer refused as copy-list-initialization; a mem-initializer that names nothing refused and one written twice refused; 9.3p2's inline read from where the definition is written, not from the declaration; one `_` per character an identifier cannot hold, so two names never flatten to one; the goto check made a carried count | 102 / 243 held, none newly failing; pa1-pa15 1173/1173; valgrind clean over 273 inputs; every axis linear at 2.1-2.3x; stripped metadata clean against the refs but for `unwind=no`; findings and evidence in `audit.md` |
