# PA18 Plan — `cppgm++ --emit-lowir` scoped polymorphism

PA18 stands at **39 / 40** of its fixtures (31 of the 32 checked-in ones, plus
the four regression tests C2 added, the three the C2 audit added and the one C3
added), with pa1-pa17 at **1732 / 1732** and the file audit passing with the
three recorded header-weight warnings it inherited. The milestone gives the
PA16/PA17 object model a vpointer, a vtable and dynamic dispatch, for the
single-inheritance slice the README's Assignment Boundary names.

The pa1-pa17 report takes **10.3 s** and no test in it is near its limit. It was
not so before the C2 audit's review: `pa9/300-binary-calculator` ran 6.84 s
against a 10 s limit and timed out whenever the machine was loaded, because
CY86's one writable and executable segment let a `data` statement put the
program's variables in a cache line it also fetched instructions from. That is
fixed in `cy86_codegen.cpp` and the margin is now 9 s rather than 3 s, so a
failure of that check is a real regression again and not weather.

`pa18/cppgm++-ref` is a wrapper and the README calls the checked-in `.ref` files
the oracle, but `reference-binaries/cppgm++` is a full PA18 compiler and is the
oracle for everything the fixtures do not reach. All 36 checked-in `.ref` files
regenerate byte-identically from it, so no fixture holds our own output. Two
facts about the relaxed comparison shape everything below, both read out of
`scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape. Every other `@name` - a vtable, an RTTI record, a
  typeinfo name string - is compared **literally**, so those spellings are part
  of the contract even though `object=`, `binding=`, `unwind=`, `effects=` and
  `storage=readonly` are stripped. An extra or missing *function* is a diff; an
  extra `alias object` line is not, because aliases are stripped too.
- **Top-level entries are sorted**, so emission order of globals and functions
  never matters; instruction order, global item order and vtable slot order do.
  `validate_lowir_vtable_destructor_slots` additionally refuses a table whose
  destructor entries are not the complete/deleting pair in that order, and
  `validate_lowir_structure` refuses a program holding two top-level entries of
  one name - a declaration beside a definition included.

## Stage Design

Polymorphism is not a second class model. Every fact the milestone adds is a
fact of the class the standard hangs it on, settled once where 9.2p2 completes
that class, and read by name afterwards.

- **`sema_virtual.cpp` owns what a class dispatches.** 10.3p2's virtual member
  functions and overriding, 10.3p4's `final`, 10.3p5's `override`, 10.3p7's
  covariant return and 10.4's abstractness are one settlement over the class's
  own declarations against the direct base's already-settled answer, in two
  steps: `note_polymorphism` before `lay_out_class`, because the vpointer is a
  layout question, and `settle_virtual_members` inside `declare_special_members`
  after 12.1p5's and 12.4p3's implicit declarations exist and before their
  triviality is asked.
- **`settle_vtable_ownership` closes that settlement with the ABI's two
  questions about the *emitted* table**: which unit owes it - the key function,
  the first virtual member the class declares that is neither pure nor inline -
  and what 5.3.5p9 chose for the deleting entry to give the storage back to,
  which is a lookup the entry cannot make for itself because no
  delete-expression stands under it.
- **What a declaration may say about dispatch is one reading.**
  `require_virtual_placement` asks it of the `virtual` keyword and of the
  virt-specifiers together, in front of every form that can carry either.
- **`sema_layout.cpp` owns 9.2p13**, one walk that settles the vpointer's eight
  bytes, the base subobject's place, every member's offset, the size and the
  alignment together.
- **The vtable is a fact of the class** (`SemaEntity::vtable`), a vector of
  `VirtualSlot` in ABI slot order: the derived class's is the base's with each
  overridden slot replaced in place, followed by the slots this class
  introduces in declaration order. A destructor takes two consecutive slots.
- **The slot index is a fact of the function** (`SemaEntity::vtable_index`), and
  single inheritance is what makes that sound: a call site that resolved to
  `Base::f` reads the index off `Base::f` and needs no walk of the dynamic type.
- **The vpointer is a fact of the layout.** `introduces_vptr` is true for the
  polymorphic class whose base carries none; a class with a *non*-polymorphic
  base puts that base subobject **after** the vpointer.
- **A vpointer is in the storage but is no subobject of it**, so every reading of
  12.8p12 asks the class and not its parts; and **12.1p11 and 12.4p11 are read
  wherever a milestone asks whether building or ending a lifetime comes to
  nothing** - `vacuous_construction`, `vacuous_destruction` and
  `construction_writes_nothing` are the three, and a polymorphic class answers
  "something" at every one of them.
- **4.10p3 is asked of the pointer value the base step moves**, not of its type:
  `basecast_null`/`basecast_adjust` only where the base subobject does not begin
  where the object does and the pointer could hold a null.
- **Override matching is one hash lookup per member**, keyed by the name and
  13.1's `member_signature`.
- **`lowir_vtable.cpp` owns the emitted data.** A table, a type-information
  record and the string that names the type are the three globals a polymorphic
  class owns; each is emitted **on demand and memoised**, so a program that
  creates no polymorphic object holds none of them. The demand has exactly two
  sources: a constructor or destructor body writing the vpointer, and this unit
  holding the definition of the class's key function.
- **What a *program* names is settled by the program.** A unit is read before
  the ones after it, so no unit can answer which name the program gives a class's
  table, whether the runtime's `__cxa_pure_virtual` is already declared, or
  whether a name it only used is one a later unit defines. Each unit records
  what it did against the object-file name that identifies the entity, and
  `LowirProgramBuilder::finish` settles it: `settle_vtable_names` makes one name
  of the two a table can be written under, and `settle_external_declarations`
  drops a declaration the program went on to define.
- **The vpointer store is an action of the lowered body, not of the tree.**
  `run` writes it after the base subobject's construction and before this
  class's own members (12.6.2p10), and at the head of what a destructor does
  (12.4p11) - so the shared AST that PA10-PA12 dump is untouched. A *delegating*
  constructor writes none: 12.6.2p6 gives the whole initialization to the target.
- **The deleting entry is a second body over one definition.** 12.4p8's suffix
  gains one step - 5.3.5p3's deallocation - and 15.2p2's handler for every step
  before it ends with the storage going back too, which is what the existing
  `destructor_epilogue` already writes for a destruction.
- **The region says which function a declaration is local to.** `Scope`
  carries 9.8p1's enclosing function and the occurrence number of the class
  between it and the region, each inherited when the region is opened, so
  `declare_in` settles both on the declaration with one read and no walk
  outwards. `TypeTable` carries the same pair for the class or enumeration,
  because `abi_type` is handed a `TypeId` and 3.5p8 leaves nothing else to tell
  two functions' `struct L`s apart. `named_from_namespace_scope` is now that
  one read.
- **Dispatch is a fact of the callee node.** `SemaFact::dispatches` is set where
  overload resolution names the member, from 5.2.2p1's three conditions: the
  function is virtual, an object expression named it, and the id was not
  qualified. Codegen never rediscovers polymorphism from syntax.

## Current Failure Map

1 of the 32 checked-in fixtures fails, and it is an emission convention of the
reference binary rather than missing compiler behaviour:

| group | count | what is missing |
| --- | --- | --- |
| G2 the file a definition was read from | 1 | `400-header-out-of-class-virtual-vtable`: we emit one constructor entry too many. Twelve probes through `reference-binaries/cppgm++` pin the rule, and it splits the two entry points apart. **The base-object entry** is written where a base subobject asked for it or where the definition is not inline - which `writes_base_entry` already says, and every probe agrees with. **The complete-object entry** is written where a complete object asked for it, where the definition is not inline, *or where a base's user-provided definition was read from this unit's own source file*. An inline constructor in a header used only as a base gets the base entry alone (`b1`, `b7`, `c1`); the same constructor in the primary file gets both (`b2`, `b3`); an out-of-line constructor gets both wherever it stands (`b5`, `b8`); an implicit one gets only what was used (`e1`, `e2`). Nothing in this compiler models which file a definition came from, so `settle_vtable_ownership` marks every user-provided constructor of every base and we write both. The reference diverges further for definitions read from an included file - `f2` keeps a destructor call `f1` elides, and `n1` gives a *non*-polymorphic base's constructor both entries where we give one - and neither divergence is under a checked-in fixture, so neither is in this group. |

## Active Checkpoint

**C4 - the file a definition was read from.**  Not started.

- Owner: `AstTokenStream`, which already carries one phase-4 fact into phase 7
  the same way - `PackTable` records only the positions `#pragma pack` changes
  the answer at. A second run-length table records the positions the reading
  enters and leaves an included file, and `SemaAnalyzer` reads it where a
  function definition's body is read, so the fact lands on the declaration and
  no later question asks the preprocessor anything.
- Data flow: `Preprocessor::source_depth` -> `AstTokenStream::sources()` ->
  `SemaAnalyzer::set_sources` -> `SemaEntity::own_source_definition` ->
  `settle_vtable_ownership`'s complete-entry marking.
- Complexity: one comparison per token and one record per `#include` that
  changed the answer; the reader is one binary search per function definition,
  and a unit that includes nothing stores nothing and answers without a search.
- Validation: `400-header-out-of-class-virtual-vtable` closes; the twelve
  probes above are the shape sweep, and the whole pa1-pa17 report plus pa18's
  other 39 tests are the guard that a definition in the primary file - which is
  every fixture but this one - still owes both entries.

## Performance Model

Measured on the eight shapes the milestone makes scaling-sensitive, each timed
twice, `cppgm++ --emit-lowir -O0`. The eighth is the *unit count*, which is what
`LowirProgramBuilder::finish`'s settlement scales in and what the first seven -
all single-unit - never reach:

| shape | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| n units, the shared class's key function in the last | 0.00 s | 0.01 s | 0.01 s | 0.03 s | 0.06 s | 0.11 s | 0.22 s |
| the LowIR that emits, in lines | 1047 | 2046 | 4046 | 8046 | 16075 | 32203 | 64459 |

and on the seven single-unit shapes:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| 16 *new* virtuals per level | 0.02 s | 0.05 s | 0.13 s | 0.40 s | 1.54 s |
| 64 virtuals *overridden* per level | 0.03 s | 0.08 s | 0.14 s | 0.30 s | 0.61 s |
| n objects of an n-deep polymorphic chain | 0.02 s | 0.04 s | 0.07 s | 0.16 s | 0.37 s |
| the same with a throwing call after each | 0.02 s | 0.04 s | 0.08 s | 0.18 s | - |
| n `new`/`delete` pairs over that chain | 0.02 s | 0.04 s | 0.08 s | 0.18 s | 0.43 s |
| n polymorphic classes over one n-deep non-polymorphic chain | 0.02 s | 0.03 s | 0.06 s | 0.13 s | 0.28 s |
| n array-`new`/`delete[]` pairs of a polymorphic element | 0.01 s | 0.02 s | 0.03 s | 0.06 s | 0.15 s |

Six of the seven are linear in the source. The first is superlinear in the
*settlement* and not in the output: a derived class's table is the base's with
the overridden slots replaced, so n classes each introducing 16 virtuals cost
16*n(n+1)/2 slot copies - 2.1 million at n=512, in 1.5 s - while the emitted
LowIR stays linear, because only a class an object is created of has its table
written. That cost is what "the vtable is a fact of the class" buys: the call
site is one load, one index and one load, with no walk of the dynamic type. The
sixth shape is the one `settle_vtable_ownership`'s walk over a non-polymorphic
base chain could have made quadratic - it stops at the first base that already
dispatches, and only the class that introduces the vpointer pays for the
prefix - and it is linear. A table, a record and a name string are emitted once
per class and memoised; a record walks the derivation once and memoises each
class on the way.

The eighth shape is linear in both the units and the output, with the rename
path active throughout: `settle_vtable_names` returns before walking wherever
the units agree on a table's name, which is every single-unit program, and walks
the finished program once where they do not; `settle_external_declarations` is
two hash-set builds over the finished program and runs for every program.

`cy86`'s image layout is the one place a *generated* program's speed was the
risk rather than the compiler's. A `data` statement beside code put the
program's variables in a line it also fetched instructions from, which x86
answers with a machine clear: 43.9 s against 0.885 s on a loop whose counter
shares a line with its body, and 28.1 s against 0.33 s on the same shape written
the other way round. Code that follows data is now emitted on the next 64-byte
line behind a `jmp rel32` that leaves the label byte-exact, and separating by
4096 bytes instead measures the same, so no line is shared. Valgrind is clean
over all 39 fixtures, over all eight scaling shapes, over every multi-unit probe
and over `cy86` on the layout probes.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2 virtual/override matching, p4 `final`, p5 `override`, p7 covariant return, 10.4p2/p3 abstractness, the ABI slot order, the vpointer's place in the layout with 4.10p3's null-preserving base cast, and 8.5.1p1/12.1p5/12.4p5/12.8p12 read against it; `sema_layout.cpp` split out of `sema_class.cpp` | 2 -> 6 / 29; pa1-pa17 1732 / 1732 |
| C1 audit | the exits a question about dispatch has at the forms C1 never reached, and the readers of a fact it widened: 9.2p8's virt-specifier placement over every form of declaration, 9.4.1p2's static member function, 12.4p9's pure virtual destructor, 10.3p7's accessible base, 12.8p12's other two readers, and 4.10p3 asked of the pointer value rather than of the operand's type | 7 / 7; 6 / 29 -> **9 / 32**, three of them regression tests; pa1-pa17 1732 / 1732; 99 accept/reject probes and 40 lowering probes |
| C2 | the emitted polymorphic object model: `lowir_vtable.cpp`'s tables, type-information records and name strings with the ABI's three record kinds and `__cxa_pure_virtual`; the key function deciding which unit owes the table and `__external_vtable__` where another does; 12.1p11/12.4p11's vpointer stores; 12.4's D1/D2/D0 triple with 5.3.5p3's deallocation as the last step of 12.4p8's suffix; 10.3p12's dispatch on `SemaFact::dispatches` with 5.2.2p1's qualified-id suppressing it; 5.3.5p3's `delete` through the deleting slot; 12.4p11 read into `vacuous_destruction` and 12.1p11 into `construction_writes_nothing`; 5.4p4's cast of a null pointer constant folded where 4.10p1 and not 5.2.10p5 is the conversion | 9 / 32 -> **30 / 32**, plus 4 new regression tests all passing (34 / 36); pa1-pa17 1732 / 1732; 20 emission probes against the reference binary |
| C2 audit | the boundary a fact of the *program* was settled at, the third reader of a fact C2 widened, and the sibling spellings of a rule it landed at one: the table's name and `__cxa_pure_virtual` and a use's declaration settled in `LowirProgramBuilder::finish` rather than per unit, 12.1p11 read into `vacuous_construction`, 15.2p2's handler opened only where the element constructor can throw, 12.6.2p6's delegating constructor left without a vpointer store, and 5.2.10p5's `reinterpret_cast` left out of 5.4p4's fold | 7 / 7; 34 / 36 -> **37 / 39**, three of them regression tests; pa1-pa17 1732 / 1732; all 36 checked `.ref` files regenerated from the reference binary, 44 lowering probes, four multi-unit shapes, seven scaling shapes, valgrind clean |
| C2 audit review | the increment re-derived rather than taken on its commit message, and the blocker sitting under the whole report: all seven of C2's audit fixes hold and are order-free over two and three units, and `pa9/300-binary-calculator`'s 6.84 s against the reference's 0.35 s was a cache line shared between a store and an instruction fetch, not load - fixed in `cy86_codegen.cpp` the way the reference does it, with the label held byte-exact by a `jmp rel32` | 1 / 1; pa18 holds **37 / 39**; pa1-pa17 1732 / 1732 in 10.3 s where pa9 alone took 15.3 s; nine multi-unit programs valid, a unit-count sweep to 512, valgrind clean |
| C3 | 9.8p1's `<local-name>`: the function whose body declares a class settled on the declaration and on the type where the region is read, the ABI's discriminator counted per function and name, the encoder's `<local-name>` context carrying the records that describe its function so a const member function and a variadic one are spellable, `Z <source-name> E` for a function the object file names by its own spelling, and the `N`/`E` of a nested local name put inside the context where g++ and 5.1.6 put it | 37 / 39 -> **39 / 40**, one of them a regression test; pa1-pa17 1732 / 1732; 14 naming probes against `reference-binaries/cppgm++` and g++, which agree on all but a class nested inside a local class |
