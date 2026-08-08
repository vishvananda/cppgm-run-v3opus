# PA18 Plan — `cppgm++ --emit-lowir` scoped polymorphism

PA18 **passes** and is complete, at **45 / 45** - all 32 checked-in fixtures and
13 regression tests - with pa1-pa17 at **1732 / 1732** and the file audit
passing with the four header-weight warnings it inherited. The milestone gives
the PA16/PA17 object model a vpointer, a vtable and dynamic dispatch, for the
single-inheritance slice the README's Assignment Boundary names.

The pa1-pa18 report takes **9.3 s** and no test in it is near its limit;
`pa9/300-binary-calculator`, the one that was, runs in 1.15 s against 10 s.

`pa18/cppgm++-ref` is a wrapper and the README calls the checked-in `.ref` files
the oracle, but `reference-binaries/cppgm++` is a full PA18 compiler and is the
oracle for everything the fixtures do not reach; g++ is the third, and where the
two agree against us it is a defect rather than a judgment call. **Every checked
`.ref` file in the repository regenerates byte-identically from the reference
binaries**, so no fixture holds our own output.

Two facts about the relaxed comparison shape everything below, both read out of
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
- **Override matching reads the class that introduced the slot.** A slot's index
  is fixed where the name first took one and every class below copies the table
  with that index, so `introduced_slots_` records only what each class
  *introduces*, keyed by the interned name and 13.1's `member_signature` in one
  word, and `inherited_slot` walks the derivation. The records are linear in the
  declarations rather than quadratic in the derivation.
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
  nothing** - `vacuous_construction`, `vacuous_destruction`,
  `construction_writes_nothing` and 3.6.2p2's image are the four, and a
  polymorphic class answers "something" at every one of them.
- **An object whose whole storage is the vpointer holds it in the image.**
  9p6 leaves a class declaring no data member holding nothing but what 10.3p1
  gave it, so `vpointer_image` folds 12.1p11's store into the static
  initializer of a namespace-scope object and the program runs nothing before
  it. A byte the vpointer does not cover, an array, or a constructor the program
  itself wrote each keeps 3.6.2p2's action.
- **4.10p3 is asked of the pointer value the base step moves**, not of its type:
  `basecast_null`/`basecast_adjust` only where the base subobject does not begin
  where the object does and the pointer could hold a null.
- **`lowir_vtable.cpp` owns the emitted data.** A table, a type-information
  record and the string that names the type are the three globals a polymorphic
  class owns; each is emitted **on demand and memoised**, so a program that
  creates no polymorphic object holds none of them. The demand has exactly two
  sources: a constructor or destructor body writing the vpointer, and this unit
  holding the definition of the class's key function.
- **The deleting entry is a fact of the definition, not of the table.** A table
  this unit writes asks for it, and so does a definition of a virtual destructor
  no other unit may hold - because 10.4p2's pure slots name the runtime's own
  function and ask for nothing, and a class whose key function stands elsewhere
  has its table in that unit. `owe_deleting_entry` is the one place both ask.
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
- **The region says which function a declaration is local to.** `Scope`
  carries 9.8p1's enclosing function, the occurrence number of the class
  between it and the region and whether that class has a spelling at all, each
  inherited when the region is opened, so `declare_in` settles all three on the
  declaration with one read and no walk outwards. `TypeTable` carries the same
  triple for the class or enumeration, because `abi_type` is handed a `TypeId`
  and 3.5p8 leaves nothing else to tell two functions' `struct L`s apart.
  `named_from_namespace_scope` is now that one read.
- **A type the body left unnamed is named by its place, not by a spelling.**
  Nothing binds such a declaration in a region, so `settle_unnamed_local_name`
  settles it where the declaration is read, and PA14's encoder writes the ABI's
  `<unnamed-type-name>` - `Ut_`, `Ut0_`, ... - where a source name would stand.
  A number this unit counted for itself would be another unit's other class.
- **Which entry points a special member owes is a question about its
  definition, and the two are asked apart.** The base-object entry is owed
  where a base subobject named it or where the definition is one no other unit
  may hold - `writes_base_entry`'s reading of 9.3p2. The complete-object entry
  is owed where a complete object named it, where the definition is that same
  one this unit alone holds, or where this unit's own source wrote a
  user-provided definition for one of the classes **below the vpointer** - the
  class that introduces it and the non-polymorphic classes under it, which is
  what `settle_shared_entry_points` walks. A class whose base already dispatches
  adds none: every unit that can create a complete object of it can define its
  members for itself.
- **Dispatch is a fact of the callee node.** `SemaFact::dispatches` is set where
  overload resolution names the member, from 5.2.2p1's three conditions: the
  function is virtual, an object expression named it, and the id was not
  qualified. Codegen never rediscovers polymorphism from syntax.
- **A value a call hands back in registers needs no second place to stand when
  it is an object.** 12.2p1 already gives it a temporary and the copy into it
  stands in the step the call belongs to, and `load`/`store` have no lowered
  type that spells an object.

## Current Failure Map

No fixture fails. Seven shapes no fixture reaches are known divergences from
`reference-binaries/cppgm++`; g++ is the third oracle wherever it has an
opinion, and all seven are left as they are.

| shape | what differs | why it is left |
| --- | --- | --- |
| a class nested inside a local class | the reference drops the enclosing local class - `_ZTSZ1fvE5inner` where we and g++ write `_ZTSZ1fvEN1L5innerE` | 5.1.6's `<local-name>` is a `<name>`, which a `<nested-name>` is one of, and g++ agrees |
| a local class of an `extern "C"` function | the reference writes `Z6ext_fnvE`, we and g++ write `Z6ext_fnE` | 3.5p9 names the function by its own spelling, so there is no bare-function-type to write; g++ agrees |
| an unnamed enumeration declared before an unnamed class in one function | the reference numbers the class `Ut_`, we and g++ number it `Ut0_` | the ABI counts a region's unnamed types in one sequence whatever their class-key; g++ agrees |
| a local class of a constructor or destructor body | the reference emits the class's table, record and members **twice**, once per entry point, with a discriminator on the second | one class is one entity; the second copy is the reference lowering the body twice |
| a user-provided destructor an included file defines | `f2` keeps a destructor call the identical program written in one file (`f1`) elides, so the reference reads 12.4p8's empty body only for its own source | the elision is right in both, and following the reference would mean *not* reading a definition this unit holds |
| an `inline` destructor defined outside its class and never used | the reference emits an unused `D1`; we and g++ emit nothing | 3.2p3 puts an inline definition in the program where a use asks |
| a class-typed value a call hands back, read through a member access | the reference closes the step after the copy and re-opens one for the field read; we leave the read in the step the call stands in | neither `index` nor `load` can throw, so the two are the same program written two ways |

One more difference is not a divergence but a **harness limitation**, and it
strikes the reference identically: `lowir_destructor_entry_from_object_symbol`
reads any `D0`/`D1`/`D2` in an object symbol as a destructor entry, so a class
local to a *destructor* body - whose members are named `_ZZN6HolderD1EvEN1L1fEv`
- makes `validate_lowir_vtable_destructor_slot_order` refuse both compilers'
output. No fixture writes that shape.

Four shapes are refused outright and belong to later milestones by their own
READMEs: a block-scope static (pa15 puts "function-local static objects and
guard variables" out of scope and pa21 owns them), `try`/`catch`/`throw` (pa22),
a pointer to member function, and multiple inheritance (PA18 Out Of Scope).
The reference accepts all four; it also **accepts two programs the standard
refuses** - an override of a `final` function and an object of an abstract
class - which is its error recovery and not an oracle.

## Performance Model

The dominant operations are the settlement of one class's table, which is a copy
of the base's plus this class's own declarations, and the emission of one table,
which is one ABI name per slot. Both are linear in what they name; what is
superlinear is superlinear in the *program*.

Measured on the eleven shapes the milestone makes scaling-sensitive, each timed
twice, `cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| 16 *new* virtuals per level | 0.03 s | 0.07 s | 0.19 s | 0.59 s | 2.23 s |
| 64 virtuals *overridden* per level | 0.07 s | 0.14 s | 0.29 s | 0.60 s | 1.31 s |
| n objects of an n-deep polymorphic chain | 0.02 s | 0.03 s | 0.05 s | 0.11 s | 0.24 s |
| n `new`/`delete` pairs over that chain | 0.02 s | 0.03 s | 0.05 s | 0.10 s | 0.21 s |
| n polymorphic classes over one n-deep non-polymorphic chain | 0.01 s | 0.02 s | 0.05 s | 0.11 s | 0.29 s |
| n array-`new`/`delete[]` pairs of a polymorphic element | 0.02 s | 0.03 s | 0.06 s | 0.12 s | 0.25 s |
| n functions each declaring a *named* local polymorphic class | 0.02 s | 0.03 s | 0.05 s | 0.11 s | 0.22 s |
| n functions each declaring an *unnamed* one | 0.02 s | 0.03 s | 0.05 s | 0.11 s | 0.21 s |
| n unnamed local classes in **one** function | 0.02 s | 0.03 s | 0.05 s | 0.10 s | 0.20 s |
| one n-parameter function holding an n-member local class | 0.01 s | 0.01 s | 0.03 s | 0.12 s | 0.47 s |
| n namespace-scope polymorphic objects | 0.01 s | 0.02 s | 0.04 s | 0.08 s | 0.16 s |

and on the twelfth, the *unit count*, which is what
`LowirProgramBuilder::finish`'s settlement scales in and what the eleven
single-unit shapes never reach:

| units | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `--emit-lowir` over all of them | 0.01 s | 0.01 s | 0.02 s | 0.04 s | 0.07 s | 0.14 s | 0.28 s |
| the LowIR that emits, in lines | 1221 | 2355 | 4627 | 9171 | 18287 | 36591 | 73199 |

Nine of the eleven are linear in the source. The two that are not are quadratic
in the *program*, measured rather than argued:

- **16 new virtuals per level** emits 16 K, 48 K, 162 K, 587 K and **2.2 M**
  lines at the five sizes, because 12.1p11 makes every base constructor write
  its own vpointer and creating one object of the deepest class therefore
  demands all 512 tables - 16*n(n+1)/2 slots in all. The reference emits the
  same program: 162 648 lines against our 162 520 at n=128, in **13.88 s against
  our 0.19 s**, and it does not finish at all past that. The *settlement* alone,
  with no object created, is 0.44 s at n=512.
- **one n-parameter function holding an n-member local class** is 9.8p1's name
  repeating the whole encoding of the function whose body declared the class, so
  n members of an n-parameter function is n^2 bytes of object-file name - 408 KB
  of LowIR at n=512, against the reference's 435 KB in 0.26 s. What was
  avoidable was asking for one of those names more than once: `object_symbols_`
  holds it per declaration and entry point.

What the audit removed from the settlement was the 40 % it spent building keys
it read once: 10.3p2's match had rebuilt a `std::unordered_map<std::string,
unsigned>` over *every* inherited slot for every class - 2.1 million string
constructions at n=512 - to answer sixteen questions. The slot a name has never
moves, so the record now belongs to the class that introduced the name and a
class below reads it: 1.12 s -> **0.44 s** on the settlement, with the records
linear in the declarations rather than quadratic in the derivation.

`settle_shared_entry_points` is the other walk that could have been quadratic in
a polymorphic chain and is not: it runs only for the class that introduces the
vpointer, over the non-polymorphic classes under it, so each class in a
derivation is asked once for the program.

Depth is linear and bounded: a 512-deep expression nesting and a 64-deep nest of
class definitions each cost the same 114 ms the empty program does, and
`parse_depth.h`'s guard refuses 1024 rather than recursing into the stack. The
reference does not finish a 32-deep expression nesting.

`cy86`'s image layout is the one place a *generated* program's speed was the
risk rather than the compiler's. A `data` statement beside code put the
program's variables in a line it also fetched instructions from, which x86
answers with a machine clear: 43.9 s against 0.885 s on a loop whose counter
shares a line with its body, and 28.1 s against 0.33 s on the same shape written
the other way round. Code that follows data is now emitted on the next 64-byte
line behind a `jmp rel32` that leaves the label byte-exact.
`pa9/300-binary-calculator` runs in 1.15 s against its 10 s limit.

## Architecture Review

The stage was reconstructed from the source rather than from the checkpoints,
and traced end to end on the three facts the milestone adds.

**What a class dispatches** is settled in one place and read by name. The
parse writes `virtual`, the virt-specifiers and the pure-specifier onto the
declaration; `require_virtual_placement` is the single reading of where they may
stand; `settle_virtual_members` is the single settlement, run once per class
where 9.2p2 completes it; and everything after it - the layout, the lowering,
5.2.2p1's dispatch decision, 10.4p2's refusals - reads a field. There is no
second path and no re-derivation from syntax.

**The vpointer** is one fact with four readers, and the audit added the fourth:
`vacuous_construction`, `vacuous_destruction`, `construction_writes_nothing` and
3.6.2p2's image all ask the class rather than its parts, and all four now answer
"something" for a polymorphic class.

**The name the object file gives an entity** is one path: `abi_symbol_of` over
records PA14's encoder reads, with `object_symbols_` memoising the answer per
declaration and entry point. 9.8p1's local name and the ABI's unnamed-type name
both enter it as facts of the *region*, settled where the declaration is read.

**What the program names** is settled once, in `LowirProgramBuilder::finish`,
over the finished program: two linear passes, order-free over every permutation
of two and three units.

**Ownership is not duplicated.** The deleting entry had two demands and now has
one place that records them. The complete-object entry had one walk repeated by
every derived class and now has one walk run by the class that introduces the
vpointer. Override matching had one index rebuilt per class and now has one
record per introduction.

## Final Architecture Review

The whole stage passes the audit's own gates:

- **No fallback path, source-specific gate, fixture name, environment variable
  or file-audit bypass** is anywhere in `dev/src`; there is no `#if 0`, no
  `TODO`, no `FIXME`. The file audit passes with the four header-weight warnings
  the shared headers have carried since C3.
- **No skipped phase, no embedded output, no interpreter substitute.** The
  lowering writes LowIR text from the resolved tree; `lowir2cy86` remains the
  optional execution scaffold the README names.
- **No timeout workaround.** The whole pa1-pa18 report is 9.3 s and the slowest
  generated program runs in 1.15 s against a 10 s limit.
- **No weakened check.** The two programs the reference accepts and we refuse -
  an override of a `final` function and an object of an abstract class - are
  ill-formed, and g++ and clang refuse them too.
- Every checked `.ref` file in the repository regenerates byte-identically from
  the reference binaries, so no fixture holds our own output.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2 virtual/override matching, p4 `final`, p5 `override`, p7 covariant return, 10.4p2/p3 abstractness, the ABI slot order, the vpointer's place in the layout with 4.10p3's null-preserving base cast, and 8.5.1p1/12.1p5/12.4p5/12.8p12 read against it; `sema_layout.cpp` split out of `sema_class.cpp` | 2 -> 6 / 29; pa1-pa17 1732 / 1732 |
| C1 audit | the exits a question about dispatch has at the forms C1 never reached, and the readers of a fact it widened: 9.2p8's virt-specifier placement over every form of declaration, 9.4.1p2's static member function, 12.4p9's pure virtual destructor, 10.3p7's accessible base, 12.8p12's other two readers, and 4.10p3 asked of the pointer value rather than of the operand's type | 7 / 7; 6 / 29 -> **9 / 32**, three of them regression tests; pa1-pa17 1732 / 1732; 99 accept/reject probes and 40 lowering probes |
| C2 | the emitted polymorphic object model: `lowir_vtable.cpp`'s tables, type-information records and name strings with the ABI's three record kinds and `__cxa_pure_virtual`; the key function deciding which unit owes the table and `__external_vtable__` where another does; 12.1p11/12.4p11's vpointer stores; 12.4's D1/D2/D0 triple with 5.3.5p3's deallocation as the last step of 12.4p8's suffix; 10.3p12's dispatch on `SemaFact::dispatches` with 5.2.2p1's qualified-id suppressing it; 5.3.5p3's `delete` through the deleting slot; 12.4p11 read into `vacuous_destruction` and 12.1p11 into `construction_writes_nothing`; 5.4p4's cast of a null pointer constant folded where 4.10p1 and not 5.2.10p5 is the conversion | 9 / 32 -> **30 / 32**, plus 4 new regression tests all passing (34 / 36); pa1-pa17 1732 / 1732; 20 emission probes against the reference binary |
| C2 audit | the boundary a fact of the *program* was settled at, the third reader of a fact C2 widened, and the sibling spellings of a rule it landed at one: the table's name and `__cxa_pure_virtual` and a use's declaration settled in `LowirProgramBuilder::finish` rather than per unit, 12.1p11 read into `vacuous_construction`, 15.2p2's handler opened only where the element constructor can throw, 12.6.2p6's delegating constructor left without a vpointer store, and 5.2.10p5's `reinterpret_cast` left out of 5.4p4's fold | 7 / 7; 34 / 36 -> **37 / 39**, three of them regression tests; pa1-pa17 1732 / 1732; all 36 checked `.ref` files regenerated from the reference binary, 44 lowering probes, four multi-unit shapes, seven scaling shapes, valgrind clean |
| C2 audit review | the increment re-derived rather than taken on its commit message, and the blocker sitting under the whole report: all seven of C2's audit fixes hold and are order-free over two and three units, and `pa9/300-binary-calculator`'s 6.84 s against the reference's 0.35 s was a cache line shared between a store and an instruction fetch, not load - fixed in `cy86_codegen.cpp` the way the reference does it, with the label held byte-exact by a `jmp rel32` | 1 / 1; pa18 holds **37 / 39**; pa1-pa17 1732 / 1732 in 10.3 s where pa9 alone took 15.3 s; nine multi-unit programs valid, a unit-count sweep to 512, valgrind clean |
| C3 | 9.8p1's `<local-name>`: the function whose body declares a class settled on the declaration and on the type where the region is read, the ABI's discriminator counted per function and name, the encoder's `<local-name>` context carrying the records that describe its function so a const member function and a variadic one are spellable, `Z <source-name> E` for a function the object file names by its own spelling, and the `N`/`E` of a nested local name put inside the context where g++ and 5.1.6 put it | 37 / 39 -> **39 / 40**, one of them a regression test; pa1-pa17 1732 / 1732; 14 naming probes against `reference-binaries/cppgm++` and g++, which agree on all but a class nested inside a local class |
| C4 | 2.2p1's file a definition was read from: `Preprocessor::source_depth` recorded by position in a run-length `IncludeTable` beside `PackTable`, read where a special member's body is read into `SemaEntity::own_source_definition`, and `settle_vtable_ownership` owing the complete-object entry for a base's user-provided constructor or destructor only where this unit's own source wrote it; the object-file name of a declaration memoised per entry point | 39 / 40 -> **41 / 41**, one of them a regression test, PA18 complete; pa1-pa17 1732 / 1732; 14 entry-point probes against `reference-binaries/cppgm++`, of which 12 agree and the two that do not are in the Failure Map; three scaling shapes, valgrind clean |
| final audit | the whole stage re-derived from the source, and the six blockers that survived every checkpoint: the ABI's `<unnamed-type-name>` for a class a body left unnamed (a **cross-unit miscompile**), `load obj<NxM>` as **invalid LowIR** for a class-typed call result under a handler, 15.4p14 unasked of the implicit default constructor, the deleting entry owed by the table rather than by the definition, the complete-object entry owed by every derived class rather than by the vpointer's own, 12.1p11 unread by 3.6.2p2's image, and 10.3p2's override index rebuilt per class | 6 / 6; 41 / 41 -> **45 / 45**, four of them regression tests; pa1-pa17 1732 / 1732; **1777 / 1777** through pa18 in 9.3 s; file audit passes; every checked `.ref` in the repository regenerates byte-identically; 102 differential probes, 16 multi-unit programs in every order, twelve scaling shapes, a depth sweep to 512 and valgrind clean over all of them |
