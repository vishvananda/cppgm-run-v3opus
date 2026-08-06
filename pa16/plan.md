# PA16 Plan — `cppgm++ --emit-lowir` object model

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs:

- `sema_scope.*` owns declarations, regions and lookup. Where a member sits in
  its object, which special member function a declaration declares, and which
  class a class derives from are facts about the declaration, so they live on
  its own `SemaEntity`; the region it declares carries the one edge 10.2's
  lookup follows.
- `sema_class.cpp` owns the object model: 10p1's base-clause, 9.2p13 layout, 11
  access, 12.1/12.4/12.6.2 special members and subobject order, and 3.7.1/3.8p1
  lifetime. Each is settled from the same one fact - what the class's own region
  declares, in the order it declares it - and a question about a subobject is
  asked in the same words whether it is a base, a member or an object of its own.
- `sema_analyzer.cpp` walks the syntax, resolves names and types, and hands each
  class to that owner once, where 9.2p2 makes it complete.
- `sema_overload.cpp` owns 13.3, including 4.10p3/8.5.3p4's derived-to-base
  sequences and 13.3.3.2p4's ordering of them.
- `lowir_lower*.cpp` reads only the resolved tree. It never re-resolves a name
  and never reads syntax.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, an
implicit-object argument in 13.3, `constructor-action` / `destructor-action` /
`member-initialization` / `base-conversion` nodes, and a demand-driven definition
worklist in the unit lowering.

## Current Failure Map

After C1-C4: 125 / 243. The 118 that remain:

| group | count | what is missing |
| --- | --- | --- |
| operator overloading | 33 | 13.5 over the object model, ADL, hidden friends |
| LowIR shape diffs, the program otherwise accepted | 31 | see below |
| bit-fields | 10 | 9.6 layout and access |
| friends | 6 | 11.3, and the access a friend declaration gives |
| class using-declarations, inheriting constructors | 7 | 7.3.3p1 into a class, 12.9 |
| other refusals | 31 | scattered, mostly downstream of the above |

Of the 31 shape diffs, the named ones are: 8.5p8's zero-initialization of a
value-initialized class with no user-provided constructor; the exception cleanup
regions the references write around partially constructed and partially
destroyed subobjects (`eh_cleanup` / `eh_try` / `resume`); arrays of class type
constructed and destroyed element by element; 5.16p3's conditional whose two
glvalue operands are a class and a base of it; and a `declare global` written
for an `extern` object nothing uses.

15.4p14's `unwind=no` is separate and needs no test result: the relaxed
comparison strips the field, and emitting nothing is silence rather than a false
claim. The direct `noexcept` on a declarator is its other half and needs a fact
carried out of the shared PA11 declarator reader.

Two refusals are not object-model work and were reached only through a base:
`alignof` is outside the constant subset, and a nested class declared in its
class and defined outside it is never marked defined.

## Active Checkpoint

Done: **C4 — single inheritance** (10.1, 10.2, 11.2, 11.4, 12.6.2p5, 12.4p8,
4.10p3, 8.5.3p4, 13.3.3.2p4, 5.9p2), which took 24 of the 43 base-class tests
and 4 beyond them.

Next: **C5 — 13.5 operator overloading over the object model**, the largest
remaining group at 33 tests: an operator written on a class or enumeration
operand is a call, whose candidates are the member operator functions 13.3.1.2p3
gathers from the class and the non-member ones ordinary lookup and 3.4.2's
associated namespaces reach, with 13.6's built-in candidates alongside them.

- owner: `sema_expression.cpp` for the operand types that turn an operator into
  a call, `sema_overload.cpp` for 13.3.1.2's candidate set and 13.6's built-ins,
  `sema_scope.cpp` for 3.4.2's associated namespaces and 11.3's friend
  declarations, which are what a hidden friend is found through.
- data flow: an operator expression whose operand has class or enumeration type
  builds one candidate set, resolves it exactly as a call does, and writes the
  same `call-expression` node the call path already lowers - so nothing new
  reaches the lowering.
- expected complexity: one candidate gathering per operator expression, over the
  declarations of the operand's class and of its associated namespaces, which is
  what a call of a named function already costs.
- what C4 leaves ready: a member operator is found through a base by the same
  10.2 walk, and its implicit object argument converts through the same
  `base-conversion` node every other derived-to-base conversion writes.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`, valgrind over the corpus, and a sweep of one
  chained operator expression at growing length.

## Performance Model

- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
  A base contributes its own cached size and alignment, so a chain of n derived
  classes is laid out in n passes and not n^2.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- 10.2's base chain is a pointer on the region, walked only where a class has
  one: a lookup in a program with no inheritance pays one null test per
  enclosing region, and one with inheritance pays the depth of the hierarchy
  rather than its size.
- 4.10p3 asks whether one class derives from another by walking that same chain,
  so a conversion costs the depth and no search.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, so a function used n times is lowered once. The worklist is drained at
  the top level, so `program_.functions` never reallocates under a live
  `Function&` reference.
- 12.6.2's mem-initializers are indexed by member name once per constructor, so
  a class with n members each named in the ctor-initializer costs n lookups
  rather than n^2 comparisons. The base is asked for by its own name first, so
  only a ctor-initializer that spelled it through an alias costs one lookup per
  mem-initializer it wrote.
- 12.6.2p10's order is `Scope::declarations` with the base before it, so one pass
  writes every subobject initialization, and 12.4p8 walks the same list backwards
  with the base after it.
- 12.1p5: a subobject whose default-initialization does nothing gets no node at
  all, so an empty base or member costs nothing in the tree or in the output.
- A slot named after an identifier another slot already took starts from the
  suffix that identifier last used, so n blocks declaring one name cost n steps.
- A jump out of a set of blocks writes one destructor action per object those
  blocks hold, which is what 3.8p1 asks for. Whether any object is alive at all
  is a carried count, not a walk of the open blocks.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in under 0.01 s.
- Measured for this checkpoint, each doubling 2.0x-2.5x: a chain of 100/200/400/
  800/1600 derived classes, each with its own member and constructor, an object
  of the last one and a member of the first named on it, 0.00/0.01/0.02/0.05/
  0.11 s; 500/1000/2000/4000 classes each deriving from one base, constructed
  and called, 0.06/0.13/0.25/0.54 s; 1000/2000/4000/8000 accesses through a
  four-deep chain in one body, 0.04/0.08/0.17/0.36 s.
- Measured earlier and unchanged, each doubling about 2.1x-2.3x: 4000 default
  member initializers in one class, 0.06 s; 4000 mem-initializers in one
  constructor, 0.07 s; 4000 locals with destructors in one block, 0.09 s; 4000
  namespace-scope objects constructed and destroyed, 0.09 s; 4000 loops each with
  a `break` leaving one object, 0.33 s; 2000 constructor overloads chosen between
  for one call, 0.06 s; a single `break` unwinding 800 nested blocks, 0.04 s;
  8000 members laid out and initialized twice, 0.14 s; 4000 member accesses and
  calls in one body, 0.20 s; nested aggregate depth 800, 0.80 s.
- 3.8p1 makes a return destroy every object of every block it leaves, so n nested
  blocks each holding an object and a return emit n^2/2 calls: measured 400 deep
  at 0.36 s for 166425 lines. That is what the source asks for, not a re-walk.
- Nested block scopes cost more than linearly in their depth, and did before the
  object model: 4000 nested blocks holding one scalar each take 0.23 s. The cost
  is the lookup walking enclosing regions; it belongs to the scope layer.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
| audit of C1-C2 | 9.4.2p2's definition told from its declaration by the declarator-id; 12.6.2p8 refused rather than dropped; 7.6.2p1's type-id form; 5.2.5p1's object expression kept or refused, never dropped; 13.3.3.2p3 ordering two sequences that differ only in qualifiers; 11p6's naming context; 9.3p2 read from where the definition is written; O(n^2) slot naming | 65 -> 70 / 243; pa1-pa15 1173/1173; valgrind clean over 249 inputs; every axis linear; stripped metadata diffed against the refs; findings and evidence in `audit.md` |
| C3 | 12.1/12.4 user-declared constructors and destructors in a class body, chained on the class; 13.3.1.3/13.3.1.4/8.5.4p3 constructor selection over 8.5's four initializer forms with `explicit`; 12.6.2 member initializations in declaration order, 12.6.2p8 default member initializers, 12.4p8 member destructions; 3.8p1 lifetime at block exit, at `return` and in 3.6.3p1's `@__cppgm_fini`; 8.4.2/8.4.3 `= default` and `= delete`; 12.8p31 copy elision from a value of the object's own type; 5.2.4 explicit destructor calls; C1/C2 and D1/D2 ABI names with the `alias object` line | 70 -> 102 / 243; pa1-pa15 1173/1173; valgrind clean on the new paths; every new axis linear |
| audit of C3 | six ways out of a region that ended no lifetime - `break`, `continue`, `goto`, the for-init-statement's own region, a static data member at shutdown, an aggregate initialized from braces - with the block-scope `static` written as an automatic object; 9.3.2p1's `this` in a destructor separated from 12.4p12's object parameter; a deleted destructor refused where the object is declared; `= T(...)` no longer refused as copy-list-initialization; a mem-initializer that names nothing refused and one written twice refused; 9.3p2's inline read from where the definition is written, not from the declaration; one `_` per character an identifier cannot hold, so two names never flatten to one; the goto check made a carried count | 102 / 243 held, none newly failing; pa1-pa15 1173/1173; valgrind clean over 273 inputs; every axis linear at 2.1-2.3x; stripped metadata clean against the refs but for `unwind=no`; findings and evidence in `audit.md` |
| C4 | 10p1's base-clause read before the members and recorded on the class and its region; 9.2p13 layout with the base subobject at offset 0 and no storage for an empty one; 9p2's injected-class-name; 10.2p2/p6 lookup through the base chain, qualified and unqualified, with the derived class hiding the base; 11.2p2's default base access, 11.2p4 on every derived-to-base conversion, 11.4p1's protected member from a derived class; 12.6.2p5's base initialization first, by its own name or an alias of it, and 12.4p8's base destruction last; 12.1p5/12.4p3 triviality through the base, and no node at all for a subobject whose initialization does nothing; 4.10p3, 8.5.3p4 and 5.2.9p11's conversions as one `base-conversion` node, 13.3.3.1.4p1's rank and 13.3.3.2p4's ordering of them; 5.9p2's composite pointer type; 5.2.9p4's discarded class operand; the object model split out into `sema_class.cpp` | 102 -> 125 / 243; pa1-pa15 1173/1173; valgrind clean over 243 inputs and the sweeps; depth, multiplicity and access axes all 2.0x-2.5x per doubling |
