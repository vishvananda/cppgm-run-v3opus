# PA18 Plan — `cppgm++ --emit-lowir` scoped polymorphism

PA18 stands at **9 / 32** of its fixtures, with pa1-pa17 at **1732 / 1732** and
the file audit passing with the three recorded header-weight warnings it
inherited. The milestone gives the PA16/PA17 object model a vpointer, a vtable
and dynamic dispatch, for the single-inheritance slice the README's Assignment
Boundary names.

`pa18/cppgm++-ref` is a wrapper and the README calls the checked-in `.ref` files
the oracle, but `reference-binaries/cppgm++` is a full PA18 compiler and is the
oracle for everything the fixtures do not reach: any program that creates no
polymorphic object emits no vtable, so its whole LowIR can be diffed against
ours byte for byte today. Two facts about the relaxed comparison shape
everything below, both read out of `scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape. Every other `@name` - a vtable, an RTTI record, a
  typeinfo name string - is compared **literally**, so those spellings are part
  of the contract even though `object=`, `binding=` and `storage=readonly` are
  stripped.
- **Top-level entries are sorted**, so emission order of globals and functions
  never matters; instruction order, global item order and vtable slot order do.

`g++` and `clang++` are the third and fourth oracles for what the fixtures do
not pin. Both are *lenient* about 10.4p3 - they accept `void g(A);` for an
abstract `A` and complain only at the call - so where they part company with
the standard the checked `100-abstract-class-by-value-argument-bad` wins and
the refusal stands at the declaration.

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
    dispatches has neither a trivial default constructor nor a trivial transfer
    member.
- **What a declaration may say about dispatch is one reading.**
  `require_virtual_placement` asks it of the `virtual` keyword and of the
  virt-specifiers together, in front of every form that can carry either: the
  ordinary declarator, the function definition, and the constructor,
  destructor and conversion-function definitions written outside their class.
  9.2p8 allows both only on the declaration a class body makes; what the
  *member* may then carry - 9.4.1p2's static member function, 12.1p4's
  constructor, a `final` or a pure-specifier on one that is not virtual - is
  asked once the class is complete, because 10.3p2 makes a declaration virtual
  that never wrote the keyword.
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
  **after** the vpointer, which is what the checked ABI reads.
- **A vpointer is in the storage but is no subobject of it**, so every reading
  of 12.8p12 asks the class and not its parts: 5.2.2p4 carries an object of a
  polymorphic class by address and returns it indirectly, no transfer member of
  one is trivial, and the definition 12.8p15 gives it carries its members from
  where the members begin rather than from byte 0.
- **4.10p3 is asked of the pointer value the base step moves.** Where the base
  subobject does not begin where the object does, a conversion of a pointer the
  program could have written a null into is a test -
  `basecast_null` / `basecast_adjust` - because moving a null on by eight bytes
  is a pointer into storage no object stands in. 9.3.2p1's `this` and
  5.3.1p3's `&x` are addresses of objects, so a step off either is the address
  and never a branch; the fact travels as `Value::nonnull` and lands on the node
  as `SemaFact::null_preserving`. 5.2.5p1's two spellings differ here:
  `E1->E2` steps on the pointer `E1` holds, `E1.E2` on the object `E1` names.
- **Override matching is one hash lookup per member**, keyed by the name and
  the `member_signature` 13.1 already tells two declarations of one name apart
  by - which is why `f() const` does not override `f()`, why `f() &` does not
  override `f() &&`, and why each overload of one name gets a slot of its own.
  The map is built once per class from the inherited slots, and is what
  9.4.1p2's static member function is asked against too.

## Current Failure Map

23 of 32 fixtures fail, and every one of them is waiting on emission rather
than on analysis. Grouped by the compiler behaviour they need:

| group | count | what is missing |
| --- | --- | --- |
| G1 vtable and RTTI globals | 22 | `@X__vtable`, `@__rtti_*`, `@__typeinfo_name__*`, the `declare global` type-info vtables, `__cxa_pure_virtual` |
| G2 vpointer writes | 22 | constructor and destructor stores of `addr @X__vtable + 16` |
| G3 destructor entry triple | 9 | D1 complete / D2 base / D0 deleting, with the deleting entry's `operator_delete` cleanup |
| G4 virtual dispatch | 20 | `load ptr` of the vpointer, slot `index`, indirect `call ... as (...)` |
| G5 base-qualified and explicit calls | 3 | 10.3p13's static call through `B::f`, and an explicit destructor call |
| G6 `delete` over a polymorphic type | 2 | 5.3.5's deleting entry and class-specific deallocation |

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
  or where the class's key function - its first non-inline virtual member
  function - is defined here) -> the constructor and destructor prologues store
  `addr @X__vtable + 16` into offset 0.
- Complexity: O(slots) per emitted vtable, O(1) per constructor/destructor. The
  RTTI records are one per class reached, memoised on the class.
- Validation: G1 and G2 close; the `.ref` files pin the global spellings
  literally, so `400-virtual-declaration-order-vtable` and
  `400-virtual-overload-distinct-vtable-slots` are the slot-order oracles and
  `400-std-rtti-name-substitution` the naming one. The audit's own probe set is
  the second oracle: `derived::f` defined out of class, and a polymorphic class
  constructed or copied, are the shapes whose only remaining difference from
  the reference binary is these globals.

## Performance Model

Measured over two shapes of single-inheritance chain and a conversion sweep,
each timed twice:

| shape | 32 | 64 | 128 | 256 |
| --- | --- | --- | --- | --- |
| 16 *new* virtuals per level (total slots quadratic) | 0.01 s | 0.02 s | 0.06 s | - |
| 64 virtuals *overridden* per level (total slots linear) | 0.02 s | 0.04 s | 0.08 s | 0.17 s |
| n conversions to the root of an n-deep chain | 0.00 s | 0.00 s | 0.01 s | 0.03 s |

The cost is the vtable copy and one hash lookup per member function. It is
linear in the vtable entries the ABI makes the milestone emit - class *k* of
the first shape genuinely has 16*k* slots - and linear in the source for the
other two. No use of a virtual function re-walks a hierarchy: overload
resolution already chose the declaration, and the slot index is a field on it.
`note_polymorphism` stops at the first `virtual` and is skipped outright for a
class whose base already dispatches, and `base_subobject_offset` walks the same
chain the lookup that found the base already walked. Valgrind is clean over all
32 fixtures and over the scaling cases.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2 virtual/override matching, p4 `final`, p5 `override`, p7 covariant return, 10.4p2/p3 abstractness, the ABI slot order, the vpointer's place in the layout with 4.10p3's null-preserving base cast, and 8.5.1p1/12.1p5/12.4p5/12.8p12 read against it; `sema_layout.cpp` split out of `sema_class.cpp` | 2 -> 6 / 29; pa1-pa17 1732 / 1732 |
| C1 audit | the exits a question about dispatch has at the forms C1 never reached, and the readers of a fact it widened: 9.2p8's virt-specifier placement over every form of declaration, 9.4.1p2's static member function, 12.4p9's pure virtual destructor (which had no form in the parse at all), 10.3p7's accessible base, 12.8p12's other two readers - a polymorphic class carried by its bytes at 5.2.2p4's boundary and its own assignment writing `copyobj` over the vpointer - and 4.10p3 asked of the operand's type rather than of the pointer value the step moves | 7 / 7; 6 / 29 -> **9 / 32**, three of them regression tests; pa1-pa17 1732 / 1732; 99 accept/reject probes and 40 lowering probes against clang, g++ and the reference binary |
