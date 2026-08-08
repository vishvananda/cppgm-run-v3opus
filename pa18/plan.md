# PA18 Plan — `cppgm++ --emit-lowir` scoped polymorphism

PA18 stands at **34 / 36** of its fixtures (30 of the 32 checked-in ones, plus
the four regression tests C2 added), with pa1-pa17 at **1732 / 1732** and the
file audit passing with the three recorded header-weight warnings it inherited.
The milestone gives the PA16/PA17 object model a vpointer, a vtable and dynamic
dispatch, for the single-inheritance slice the README's Assignment Boundary
names.

`pa18/cppgm++-ref` is a wrapper and the README calls the checked-in `.ref` files
the oracle, but `reference-binaries/cppgm++` is a full PA18 compiler and is the
oracle for everything the fixtures do not reach. Two facts about the relaxed
comparison shape everything below, both read out of
`scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape. Every other `@name` - a vtable, an RTTI record, a
  typeinfo name string - is compared **literally**, so those spellings are part
  of the contract even though `object=`, `binding=` and `storage=readonly` are
  stripped. An extra or missing *function* is a diff; an extra `alias object`
  line is not, because aliases are stripped too.
- **Top-level entries are sorted**, so emission order of globals and functions
  never matters; instruction order, global item order and vtable slot order do.
  `validate_lowir_vtable_destructor_slots` additionally refuses a table whose
  destructor entries are not the complete/deleting pair in that order.

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
  12.8p12 asks the class and not its parts.
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
- **The vpointer store is an action of the lowered body, not of the tree.**
  `run` writes it after the base subobject's construction and before this
  class's own members (12.6.2p10), and at the head of what a destructor does
  (12.4p11) - so the shared AST that PA10-PA12 dump is untouched.
- **The deleting entry is a second body over one definition.** 12.4p8's suffix
  gains one step - 5.3.5p3's deallocation - and 15.2p2's handler for every step
  before it ends with the storage going back too, which is what the existing
  `destructor_epilogue` already writes for a destruction.
- **Dispatch is a fact of the callee node.** `SemaFact::dispatches` is set where
  overload resolution names the member, from 5.2.2p1's three conditions: the
  function is virtual, an object expression named it, and the id was not
  qualified. Codegen never rediscovers polymorphism from syntax.

## Current Failure Map

2 of the 32 checked-in fixtures fail, and both are naming/emission conventions
of the reference binary rather than missing compiler behaviour:

| group | count | what is missing |
| --- | --- | --- |
| G1 function-local class ABI names | 1 | `100-function-local-class-vtable-identity`: a class a *function* declares gets `abi_name` `local` here and `Z5firstvE5local` in the reference, so two `local`s in two functions collide in one unit and the table is spelled `__vtable_type_5local` instead of `__vtable_type_Z5firstvE5local`. The gap is PA17-era ABI naming for block-scope entities, not PA18 lowering: our `_ZN5localC1Ev` should be `_ZZ5firstvEN5localC1Ev`. Fixing it touches `Scope::abi_prefix` for a function/block region and `TypeTable::user_qualified_name` for a local class, and must leave the PA11/PA12 `dump_name` alone. |
| G2 the reference's primary-source-file rule | 1 | `400-header-out-of-class-virtual-vtable`: a *user-provided* constructor of a class taking part in a polymorphic object gets both of the ABI's entry points, **except** when the class definition came from an `#include`. Probed 20 shapes through `reference-binaries/cppgm++`: the same two classes written into one file emit `@HB__HB` and `@HB__HB__base_entry`, and split across a header emit only the base entry. Nothing in this compiler models which file a definition was read from, so we write both and the fixture sees one function too many. |

## Active Checkpoint

**C3 - the ABI name of an entity a function declares.**  Not started.

- Owner: `sema_scope.cpp`'s `name_in_region` and the `Scope` a function body
  opens, with `lowir_abi.cpp` reading the result. The dump name stays what
  PA11/PA12 already print; only `abi_name` and the class type's
  `user_qualified_name` gain the enclosing function's encoding.
- Data flow: the function whose body is open -> the block/function scope's
  `abi_prefix` -> `abi_name` of every entity declared under it -> PA14's
  `ABI_TYPE_LOCAL_TYPE` / `ABI_CONTEXT_FUNCTION` records, which the encoder
  already has.
- Complexity: one string per function body entered, O(1) per declaration; no
  walk outwards, exactly as `abi_prefix` already works for a namespace.
- Validation: `100-function-local-class-vtable-identity` closes; pa14's
  `abimangle` suite and the whole pa10-pa17 report are the guard that no dump
  and no earlier object name moved. The two local classes named `local` in
  `first()` and `second()` are the shape that proves the collision is gone.

## Performance Model

Measured on the four shapes the milestone makes scaling-sensitive, each timed
twice, `cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| 16 *new* virtuals per level (total slots quadratic) | 0.11 s | 0.23 s | 0.66 s | 1.90 s | - |
| 64 virtuals *overridden* per level (total slots linear) | 0.20 s | 0.42 s | 0.95 s | 1.70 s | - |
| n objects of an n-deep polymorphic chain | 0.05 s | 0.10 s | 0.24 s | 0.78 s | 2.54 s |
| n `new`/`delete` pairs over that chain | 0.04 s | 0.07 s | 0.14 s | 0.32 s | - |

The first shape is linear in the table entries the ABI makes the milestone
emit - class *k* genuinely has 16*k* slots - and the second and fourth are
linear in the source. The third grows quadratically, and the growth is **not
this milestone's**: the same chain with a non-virtual but non-vacuous destructor
costs 1.17 s and 8.9 MB of LowIR at n=512 against 2.54 s and 10.4 MB here, so
PA18 adds a 16% constant factor to a shape PA17 already had - 15.2p2 gives each
of n live objects a handler that ends the ones behind it, which is n(n+1)/2
calls in one block. What PA18 itself costs is linear: a table is emitted once
per class and memoised, a record walks the derivation once and memoises each
class on the way, the complete-object-entry marking stops at the first base that
already dispatches, and a dispatch site is one load, one index and one load.
Valgrind is clean over all 36 tests and over the scaling cases.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2 virtual/override matching, p4 `final`, p5 `override`, p7 covariant return, 10.4p2/p3 abstractness, the ABI slot order, the vpointer's place in the layout with 4.10p3's null-preserving base cast, and 8.5.1p1/12.1p5/12.4p5/12.8p12 read against it; `sema_layout.cpp` split out of `sema_class.cpp` | 2 -> 6 / 29; pa1-pa17 1732 / 1732 |
| C1 audit | the exits a question about dispatch has at the forms C1 never reached, and the readers of a fact it widened: 9.2p8's virt-specifier placement over every form of declaration, 9.4.1p2's static member function, 12.4p9's pure virtual destructor, 10.3p7's accessible base, 12.8p12's other two readers, and 4.10p3 asked of the pointer value rather than of the operand's type | 7 / 7; 6 / 29 -> **9 / 32**, three of them regression tests; pa1-pa17 1732 / 1732; 99 accept/reject probes and 40 lowering probes |
| C2 | the emitted polymorphic object model: `lowir_vtable.cpp`'s tables, type-information records and name strings with the ABI's three record kinds and `__cxa_pure_virtual`; the key function deciding which unit owes the table and `__external_vtable__` where another does; 12.1p11/12.4p11's vpointer stores; 12.4's D1/D2/D0 triple with 5.3.5p3's deallocation as the last step of 12.4p8's suffix; 10.3p12's dispatch on `SemaFact::dispatches` with 5.2.2p1's qualified-id suppressing it; 5.3.5p3's `delete` through the deleting slot; 12.4p11 read into `vacuous_destruction` and 12.1p11 into `construction_writes_nothing`; 5.4p4's cast of a null pointer constant folded where 4.10p1 and not 5.2.10p5 is the conversion | 9 / 32 -> **30 / 32**, plus 4 new regression tests all passing (34 / 36); pa1-pa17 1732 / 1732; 20 emission probes against the reference binary |
