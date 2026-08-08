# PA18 Plan — `cppgm++ --emit-lowir` scoped polymorphism

PA18 stands at **6 / 29** of its fixtures, with pa1-pa17 at **1732 / 1732** and
the file audit passing with the three recorded header-weight warnings it
inherited. The milestone gives the PA16/PA17 object model a vpointer, a vtable
and dynamic dispatch, for the single-inheritance slice the README's Assignment
Boundary names.

`pa18/cppgm++-ref` is a wrapper, but PA18 has no external reference binary: the
checked-in `.ref` files are the oracle. Two facts about the relaxed comparison
shape everything below, both read out of `scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape. Every other `@name` - a vtable, an RTTI record, a
  typeinfo name string - is compared **literally**, so those spellings are part
  of the contract even though `object=`, `binding=` and `storage=readonly` are
  stripped.
- **Top-level entries are sorted**, so emission order of globals and functions
  never matters; instruction order, global item order and vtable slot order do.

`g++` is the third oracle for what the fixtures do not pin. It is *lenient*
about 10.4p3 - it accepts `void g(A);` for an abstract `A` and only complains
at the call - so where the two part company the standard and the checked
`100-abstract-class-by-value-argument-bad` win, and the refusal stands at the
declaration.

## Stage Design

Polymorphism is not a second class model. Every fact the milestone adds is a
fact of the class the standard hangs it on, settled once where 9.2p2 completes
that class, and read by name afterwards.

- **`sema_virtual.cpp` owns what a class dispatches.** 10.3p2's virtual member
  functions and overriding, 10.3p4's `final`, 10.3p5's `override`, 10.3p7's
  covariant return and 10.4's abstractness are one settlement over the class's
  own declarations against the direct base's already-settled answer. It runs in
  two steps, because the two questions are needed at different points of class
  completion:
  - `note_polymorphism` runs **before** `lay_out_class`, because whether the
    object holds a vpointer is a layout question. It asks only what the
    declarations say - the base is polymorphic, or a member wrote `virtual` -
    and stops at the first answer.
  - `settle_virtual_members` runs inside `declare_special_members`, **after**
    12.1p5's and 12.4p3's implicit declarations exist and **before** 12.1p5's
    and 12.4p5's triviality is asked, because an implicitly declared destructor
    of a class with a polymorphic base is itself virtual, and a class that
    dispatches has neither a trivial default constructor nor - where the
    destructor is virtual - a trivial destructor.
- **`sema_layout.cpp` owns 9.2p13**, split out of `sema_class.cpp` when the
  vpointer gave the layout a third thing to place. One walk of the class's own
  declarations settles the vpointer's eight bytes, the base subobject's place,
  every member's offset, the bit-field storage units, the size, the alignment
  and the ABI's carry questions together.
- **The vtable is a fact of the class** (`SemaEntity::vtable`), a vector of
  `VirtualSlot` in ABI slot order: the derived class's is the base's with each
  overridden slot replaced in place, followed by the slots this class
  introduces, in the declaration order of the members that introduce them. A
  destructor takes two consecutive slots - 12.4's complete-object entry and the
  ABI's deleting entry - at the position its declaration stands in.
- **The slot index is a fact of the function** (`SemaEntity::vtable_index`),
  and single inheritance is what makes that sound: the index a base assigns is
  the index every class below it keeps, so a call site that resolved to
  `Base::f` reads the index off `Base::f` and needs no walk of the dynamic
  type's table.
- **The vpointer is a fact of the layout.** `introduces_vptr` is true for the
  polymorphic class whose base carries none, and it gets the first eight bytes;
  a class whose base is polymorphic inherits the pointer with the base
  subobject, and a class with a *non*-polymorphic base puts that base subobject
  **after** the vpointer, which is what the checked ABI reads. Where that
  offset is nonzero, 4.10p3 turns a *pointer* conversion into a test -
  `basecast_null` / `basecast_adjust` - because moving a null pointer on by
  eight bytes is a pointer into storage no object stands in. `this` is 9.3.2p1's
  address of an object, so a conversion of it is the address and never a branch;
  the fact travels as `Value::nonnull` and lands on the node as
  `SemaFact::null_preserving`.
- **Override matching is one hash lookup per member**, keyed by the name and
  the `member_signature` 13.1 already tells two declarations of one name apart
  by - which is why `f() const` does not override `f()`, why `f() &` does not
  override `f() &&`, and why each overload of one name gets a slot of its own.
  The map is built once per class from the inherited slots.

## Current Failure Map

23 of 29 fixtures fail, and every one of them is waiting on emission rather
than on analysis. Grouped by the compiler behaviour they need:

| group | count | what is missing |
| --- | --- | --- |
| G1 vtable and RTTI globals | 22 | `@X__vtable`, `@__rtti_*`, `@__typeinfo_name__*`, the `declare global` type-info vtables, `__cxa_pure_virtual` |
| G2 vpointer writes | 22 | constructor and destructor stores of `addr @X__vtable + 16` |
| G3 destructor entry triple | 9 | D1 complete / D2 base / D0 deleting, with the deleting entry's `operator_delete` cleanup |
| G4 virtual dispatch | 20 | `load ptr` of the vpointer, slot `index`, indirect `call ... as (...)` |
| G5 base-qualified and explicit calls | 3 | 10.3p13's static call through `B::f`, and an explicit destructor call |
| G6 `delete` over a polymorphic type | 2 | 5.3.5's deleting entry and class-specific deallocation |
| G7 refusals | 4 | 10.3p4/p5/p7 and 10.4p2/p3 - **closed by C1** |

`300-virtual-call-dereferenced-member-pointer` is the one pure G4 case: no
object of the class is created, so only the indirect call is missing.

## Active Checkpoint

**C2 - the vtable, the RTTI records and the vpointer the constructors write.**
Not started.

- Owner: a new emitter beside `lowir_emit.cpp` for the vtable and RTTI globals,
  and `lowir_lower_object.cpp` for the vpointer actions. Both read
  `SemaEntity::vtable`, `polymorphic` and `introduces_vptr` and never the
  syntax.
- Data flow: class metadata -> demand-driven global emission (a vtable is
  emitted where a constructor or destructor of the class writes its vpointer,
  or where the class's key function is defined here) -> the constructor and
  destructor prologues store `addr @X__vtable + 16` into offset 0.
- Complexity: O(slots) per emitted vtable, O(1) per constructor/destructor. The
  RTTI records are one per class reached, memoised on the class.
- Validation: G1 and G2 close; the `.ref` files pin the global spellings
  literally, so `400-virtual-declaration-order-vtable` and
  `400-virtual-overload-distinct-vtable-slots` are the slot-order oracles and
  `400-std-rtti-name-substitution` the naming one.

## Performance Model

Measured on `scale_128_64.cpp` - a 323 KB single-inheritance chain 128 classes
deep, each overriding 64 virtual functions and adding a member, so 8192 slots
are copied over the hierarchy:

| build | wall |
| --- | --- |
| `HEAD` before C1 | 0.17 / 0.17 / 0.18 s |
| C1 | 0.18 / 0.19 / 0.19 s |

The cost is the vtable copy and one hash lookup per member function, and it is
linear in the source: 80 KB / 0.04 s, 323 KB / 0.19 s over the same shape. No
use of a virtual function re-walks a hierarchy - overload resolution already
chose the declaration, and the slot index is a field on it. `note_polymorphism`
stops at the first `virtual` and is skipped outright for a class whose base
already dispatches. Valgrind is clean on the polymorphic fixtures and on the
scaling case.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2 virtual/override matching, p4 `final`, p5 `override`, p7 covariant return, 10.4p2/p3 abstractness, the ABI slot order, the vpointer's place in the layout with 4.10p3's null-preserving base cast, and 8.5.1p1/12.1p5/12.4p5/12.8p12 read against it; `sema_layout.cpp` split out of `sema_class.cpp` | 2 -> 6 / 29; pa1-pa17 1732/1732; 35 differential probes agree with g++ |
