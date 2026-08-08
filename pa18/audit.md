# PA18 Audit — `cppgm++ --emit-lowir` scoped polymorphism

A review of each landed checkpoint, in the order a fact travels: declare,
settle, lay out, lower.

## Current Checkpoint Review

**C1 reviewed at `37a34dea`, the commit that gave the class its table and the
object its vpointer.** The architecture is the one the README's Design Notes
ask for and it is the PA16/PA17 object model widened rather than a second model
beside it: `sema_virtual.cpp` owns 10.3 and 10.4 as one settlement per class
against the direct base's already-settled answer, `sema_layout.cpp` owns
9.2p13 with the vpointer as a third thing to place, the slot index is a field
on the function so no call site walks a hierarchy, and the lowering reads
`SemaFact::null_preserving` and never the syntax. The slot order the settlement
builds was checked against the four `.ref` files that pin it - declaration
order, one slot per overload of a name, the destructor's consecutive
complete/deleting pair replaced in place by the derived class, and
`__cxa_pure_virtual` for a pure final overrider - and it agrees with every one.

**What the review looked for is the readings a checkpoint's own exits leave at
their siblings, and the readers of a fact it widened.** Seven blockers, in two
families.

The first family is **which declaration may say something about dispatch**. C1
wrote one exit per question and each has siblings:

**1. 9.2p8's virt-specifier stands only on the declaration of a virtual member
function, and `final` had no exit.** `require_dispatches` refused `override`
and refused a pure-specifier on a member that is not virtual, and let `final`
through - so `struct B { void f() final; };` was **accepted** where g++, clang
and the reference binary all refuse it.

**2. The same reading was missing wherever the declaration is not the one a
class body makes.** `require_virtual_placement` guarded the `virtual` keyword
and not the virt-specifiers written after the parameter-clause, so
`void f() final;` at namespace scope and `void B::f() override {}` were both
accepted; and the constructor/destructor and conversion-function definitions
written outside their class asked neither half, so `virtual B::~B() {}` and
`B::~B() override {}` were accepted too. One reading now stands in front of
every form that can say something about dispatch.

**3. 9.4.1p2's static member function shall not be virtual, and 10.3p2 is what
makes one.** A declaration of a derived class with the name and the signature
of an inherited virtual function overrides it and is virtual whether or not it
says so, so `struct D : B { static void f(); };` over `virtual void f()` is ill
formed. We **accepted** it and left the base's declaration standing in the
slot, so a call through a `D` object dispatched to `B::f`. The map the
settlement already builds answers it in the one probe every other member pays.

**4. 12.4p9's pure virtual destructor had no form at all.** `virtual ~B() = 0;`
fell out of the special-member production - which took `= default` and
`= delete` and not 9.2's pure-specifier beside them - and was read as a
simple-declaration whose decl-specifier-seq names no type, so a valid program
squarely inside the Assignment Boundary was **refused outright**.

**5. 10.3p7's covariant return needs an *accessible* base.** The clause asks
that the class in the overridden return type be an unambiguous and accessible
base of the one the override wrote, judged in the class the override is
declared in; `covariant_return` asked only whether it derives. The standard's
own example - `class D : private B { friend struct Derived; }` with `D* vf4()`
in a stranger and `D* vf5()` in the friend - now comes out the way the clause
and both external compilers read it, off the one walk `require_base_access`
already does.

The second family is **the readers of a fact C1 widened**:

**6. A polymorphic class's storage holds a vpointer, and 12.8p12's other two
readers counted only its subobjects.** C1 wrote "a class with a virtual
function has no trivial copy" into `trivially_copied` and left the readings
PA17 had written back when the base and the members were the whole of the
storage. `subobject_bytes` is what 5.2.2p4's boundary reads for a class that
derives from something, so a polymorphic derived class was **passed by value
and returned as bytes** where the reference passes `pass=by_address`;
`settle_transfers` computed a transfer member's triviality from the subobjects
alone, so a polymorphic class with *no* base was carried the same way; and the
definition 12.8p15 gives that class carried its members in one leading run from
byte 0 - which is the vpointer - so `a = b` over one wrote `copyobj 12x8` and
**overwrote the target's vpointer with the source's**, an assignment that
changes an object's dynamic type.

**7. 4.10p3's test is asked of the pointer value the step moves, and both
spellings of a member access handed `base_value` the wrong one.** It read the
operand's *type*, so 5.2.5p1's two forms came out reversed against the
reference: `p->v` converted the object and wrote no test where the reference
writes one, and `r.f()` converted the address `&r` and wrote a test where the
reference writes none. The fact belongs to the value: 5.3.1p3's `&x` is the
address of an object exactly as 9.3.2p1's `this` is, and `E1->E2` steps on the
pointer `E1` holds where `E1.E2` names an object that is there. Beside it,
`base_conversion` asked its operand for the object it names rather than for the
value, so 5.16p4's `c ? p : q` - an lvalue of pointer type - named the slot the
conditional chose and the whole conditional came out in its address form.

`Value::nonnull`, `require_virtual_placement` over every form of declaration,
`base_accessible` and the leading run 12.8p15 carries are what came out of it.

## Evidence

- **Accept/reject, 99 probes** over the virt-specifier, override-matching,
  covariant-return, static-member and abstract-class cross product, with
  `clang++`, `g++` and `reference-binaries/cppgm++` as oracles. Five
  divergences stand, all recorded under Open Gaps.
- **Lowering, 40 probes** over the base-conversion, member-access, by-value
  boundary and value-transfer cross product, diffed byte for byte against the
  reference binary. Every one now matches but for the vtable, RTTI and vpointer
  globals C2 owes; the `basecast_null`/`basecast_adjust`/`basecast_end` shape
  C1 emits is the reference's own, instruction for instruction.
- **Slot order** checked against the `.ref` files that pin it:
  `400-virtual-declaration-order-vtable` (declaration order),
  `400-virtual-overload-distinct-vtable-slots` (one slot per overload, pure
  entries as `__cxa_pure_virtual`), `300-virtual-destructor-override` (the
  consecutive complete/deleting pair, replaced in place) and
  `200-pure-virtual-override-member` (a pure-specifier on a declaration that
  never wrote `virtual`).
- **Layout** against `g++`: `sizeof` and every member offset over a three-deep
  chain whose middle class introduces the vpointer agree exactly (base at 8,
  members at 12 and 16, sizes 4 / 16 / 24).
- **Scaling**, two shapes and a conversion sweep, each timed twice:

  | shape | 32 | 64 | 128 | 256 |
  | --- | --- | --- | --- | --- |
  | 16 *new* virtuals per level (slots quadratic) | 0.01 s | 0.02 s | 0.06 s | - |
  | 64 virtuals *overridden* per level (slots linear) | 0.02 s | 0.04 s | 0.08 s | 0.17 s |
  | n conversions to the root of an n-deep chain | 0.00 s | 0.00 s | 0.01 s | 0.03 s |

  The first is superlinear because the tables themselves are - class *k* has
  16*k* slots and the ABI emits every one - so the settlement is linear in the
  vtable bytes the milestone has to write. The other two are linear in the
  source.
- **Valgrind** clean, with `--error-exitcode`, over all 32 fixtures and over
  the three scaling cases.

## Open Gaps

**Recorded, not defects.** `void g(B);` and `B h();` over an abstract `B` are
refused at the declaration. g++, clang and the reference binary all accept them
and complain only at a call, and 10.4p3 says the parameter type and the return
type are where the program is ill formed - the checked
`100-abstract-class-by-value-argument-bad` pins the refusal, so the standard
and the fixture win.

**`extern B e;` and a static data member declared of an abstract class are
accepted**, which is where g++ and the reference binary stand and clang does
not. 10.4p2 has no object created by either declaration; the definition of the
static member is a definition like any other and is refused.

**A static member function whose cv-qualification differs from the inherited
virtual's is accepted** - `struct B { virtual void f() const; };` beside
`struct D : B { static void f(); };`. 10.3p2 matches on the cv-qualification
too, so it overrides nothing and 9.4.1p2 has nothing to refuse; g++ and clang
are both stricter than N3485's own text here, and the reference binary agrees
with us.

**Outside this milestone**, unchanged and owed to C2 and after: the vtable,
RTTI and typeinfo-name globals, the constructor and destructor vpointer stores,
the destructor entry triple, virtual dispatch itself, 10.3p13's base-qualified
call and 5.3.5's deleting entry. The pure virtual destructor C1's fix admits is
covered by a compile-pass fixture that emits no vtable at all; the shape that
*does* emit one is C2's to validate.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2's overriding and the ABI slot order, 10.3p4/p5/p7, 10.4p2/p3, the vpointer's place in 9.2p13's layout, and 4.10p3's null-preserving base cast | `37a34dea` | 7 / 7, in two families - the exits a question about dispatch has at the forms the checkpoint never reached, and the readers of a fact it widened: 9.2p8's `final` on a member that is not virtual **accepted** where g++, clang and the reference all refuse; the same virt-specifier accepted at namespace scope, on a member function defined outside its class, and on a constructor, destructor or conversion function defined outside one, which asked neither half; 9.4.1p2's static member function with an inherited virtual's signature **accepted**, leaving the base's declaration standing in the slot; 12.4p9's `virtual ~B() = 0;` with no form in the special-member production at all, so a valid program inside the Assignment Boundary was **refused outright**; 10.3p7's covariant return asking only that the return type's class derive and not that the base be accessible from the class the override wrote; 12.8p12 read at one of its three readers, so a polymorphic class was **passed by value and returned as bytes** at 5.2.2p4's boundary and its own assignment wrote `copyobj` over the vpointer - **an assignment that changes an object's dynamic type**; and 4.10p3's test asked of the operand's type rather than of the pointer value the step moves, so `p->v` wrote no test and `r.f()` wrote one, each the reverse of the reference, with 5.16p4's `c ? p : q` dragged into its address form beside them | 6 / 29 -> **9 / 32**, three of them the regression tests these leave; pa1-pa17 1732 / 1732; file audit passes; 99 accept/reject probes against clang, g++ and the reference, and 40 lowering probes byte-identical to the reference but for the globals C2 owes |
