# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C7, reviewed at `ba854b1d`.** The architecture is the right one and the
checkpoint's own sentence names it: a class the ABI cannot carry as bytes is
`ptr [pass=by_address]`, the caller passes the address of the object it built,
and `describe_parameter` is the one writer the declaration, the definition and a
call through a pointer all read. What the review found is that **the fact that
boundary reads was three-quarters of a fact, and every sibling of it read a
different quarter.**

12.8p12 says a copy of a class is the copy of its bytes. That is not enough to
say the bytes *stand for the object*: an object something runs at the end of is
one the program can watch, so a second object made out of its bytes is a second
end to run. C7 saw that and put the destructor beside the copy at the boundary -
but read 12.4p5's triviality where this translation's own answer to "does ending
a lifetime come to anything" is 12.4p8's `vacuous_destruction`, read the copy
half off the base class subobject alone, and left every other reader of the same
question asking 12.8p12 by itself. Six defects follow from that one seam, and
five of them are wrong output on shapes no fixture covers: a class with an
empty-bodied destructor had the wrong ABI in both directions, a class with a
real destructor was copied with `copyobj` where the reference calls the copy
constructor 12.8p15 defines, and a derived class with a member whose copy is a
call was passed as raw bytes.

The seventh and eighth are the checkpoint's other half: **5.2.2p4's parameter is
ended at one exit and the function has three**, and **5.2.9p4's cast is an
initialization only where the target class differs from the operand's**.

**1. The ABI asked 12.4p5 where the translation asks 12.4p8.**
`passes_indirectly` and `returns_indirectly` both read
`UserType::trivial_destruction`, which is "the program wrote no destructor
anywhere below". `struct A { int a; ~A(){} };` writes one and it runs nothing -
12.4p8's clause is exactly that a destructor whose body writes no statement
comes to what its subobjects come to - so an object of A is carried by its bytes
and handed back in registers. We passed it `by_address` and returned it through
a destination the caller named; the reference does neither, and neither do we
anywhere else, because `vacuous_destruction` is the answer eleven fixtures and
every other end of a lifetime already read. The fact is now
`UserType::vacuous_destruction`, written once from that same walk where the
class completes.

**2. The boundary's copy half read the base subobject and forgot the members.**
The checked-in `.ref` files do pin the base reading -
`200-trivial-move-does-not-fall-through-to-copy` and
`300-direct-object-parameter-passthrough-base-copy` both pass a derived class
that declares its own copy constructor as `obj<>` bytes - but "what the storage
it is laid out over is carried by" is the base **and** the members, and C7 read
only the base. `struct A {}; struct M { M(const M&); }; struct B : A { M m; };`
was passed as four raw bytes with a user-provided copy constructor never called.
`UserType::subobject_bytes` is that walk with the class's own declaration left
out of it, and `carried_by_bytes` is what 5.2.2p4 reads: the class's own answer
where it derives from nothing, the subobjects' where it derives from something.
Both fixtures still pass and the reference agrees on every shape but the one it
already disagreed with the files about.

**3. Everything else that copies a class asked 12.8p12 alone.**
`TypeTable::bytes_stand_for_object` is 12.8p12 and 12.4p8 as the one question,
and `copy_class_object`, `constructor_call`, 13.3's argument copy, 5.16p3's arm
and 12.8p31's elision all ask it now. For `struct A { int a; ~A(){ n++; } };`,
`A y = x;` was a `copyobj` and the reference calls the copy constructor - and
never wrote the definition of that constructor at all, because nothing had
asked for one. `constructor_call` asks it beside the *chosen* member's own
triviality rather than instead of it, which is what keeps
`spec/100-defaulted-move-nontrivial-subobject`'s trivial move a byte copy while
its non-trivial base step stays a call. 12.8p15's memberwise definition reads it
too: a member whose end comes to something ends the leading storage run and
takes its own call, and 9p6's member that holds nothing ends the run rather than
joining it, which is what the reference writes.

**4. 15.4p14's exception-specification was never computed, and finding 3 made
that visible.** Once a copy is a call, 15.2p2 wants a handler around it wherever
objects stand - and the reference writes none, because an implicitly declared
special member throws what the members its definition invokes throw, and 12.4p3
gives a destructor written with no exception-specification the same one. Both
are settled where the class completes: the destructor from one walk of the base
and the members, and the four transfer members inside the walk `settle_transfers`
already makes, so neither costs a second pass. Without it the fix in 3 would
have traded one wrong line for three wrong blocks.

**5. A parameter the boundary handed the function was ended at one exit of
three.** `open_parameter_lifetimes` was wired into `function_definition` only,
so every definition read through `write_definition` - a constructor, a member
function defined in its class body, a member the standard defined - never
destroyed its by-address parameter at all. `struct S { S(T t) {} };` leaked the
object the caller built, which is most real code. 15.2p2 was the third exit: the
parameter stood nowhere, so a handler covering the body owed nothing for it,
where the reference destroys it. It is one object with one end, written on the
`Parameter` node as `Fact::destruction` and read by `begin_object_lifetime` the
way a declaration's is.

**6. The caller owed the same end a second time.** A constructor call whose
argument is a by-address parameter object put that object in the *caller's*
handler, naming the ABI's base-object destructor for a complete object. With 5
fixed that is a double destruction. 12.2p3's end was already released for such an
argument; the end written on the node that began the lifetime is released with
it now, because 5.2.2p4 gives the whole of it to the function called.

**7. 5.2.9p4's cast to the operand's own class was refused outright.**
`cast_conversion`'s new branch is gated on the target class differing from the
operand's, so `(W)a`, `static_cast<W>(a)` and `(const W)a` fell through to a
byte copy that `copy_class_object` then refused for every class that writes a
copy constructor - "12.8p1 makes a call of the copy constructor its program
wrote and this milestone does not write". 5.2.9p4 is `T t(e);` whatever `e`'s
class is; the one operand that needs no initialization is a prvalue of the
target's own class, which 12.8p31 makes the same object. That is the gate now.

**8. The two readers of `creates_its_object` disagreed about cv.** The analysis
compared the cast's own type with the temporary's for equality and the lowering
compared them stripped, so `const W b = (const W)a;` built a temporary and copied
it where `W b = (W)a;` elided. 3.10p9 leaves the cv-qualification on the prvalue
and off the object under it; the question takes the `TypeTable` now and both
readers strip.

Beside them, C7 left `signed_decimal` defined and unused in
`lowir_lower_expression.cpp`, which was the one warning the build had.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, best
of three, against the pre-audit binary built from `ba854b1d` on the same shapes.

| axis | sizes | before | after | output |
| --- | --- | --- | --- | --- |
| n classes with an empty-bodied destructor, each passed by value and copied | 250/500/1000/2000 | 0.07/0.13/0.34/0.55 s | 0.06/0.13/0.26/0.55 s | 12 n lines |
| members nested n deep under a non-vacuous destructor, passed and copied | 50/100/200/400 | 0.01/0.02/0.03/0.07 s | 0.01/0.02/0.05/0.09 s | 38 n lines |
| n functions with two by-address parameters and two returns, each called | 250/500/1000/2000 | 0.09/0.14/0.26/0.52 s | 0.06/0.13/0.23/0.46 s | 35 n lines |
| n constructors each taking a by-address parameter | 250/500/1000/2000 | 0.06/0.14/0.25/0.53 s | 0.07/0.13/0.27/0.51 s | 27 n lines |
| an inheritance chain n deep, copied and assigned (15.4p14's walk) | 50/100/200/400 | 0.01/0.03/0.06/0.11 s | 0.02/0.04/0.08/0.15 s | 63 n lines |
| one function with three by-address parameters and n returns | 100/200/400 | 0.01/0.01/0.03 s | 0.01/0.02/0.03 s | 16 n lines |
| one function with n by-address parameters | 50/100/200/400 | - | 0.00/0.01/0.01/0.02 s | 5 n lines |
| n nested calls each passing a class by address | 25/50/100/200 | - | under 0.01 s | 3 n lines |
| n standing objects, each with a by-address argument under it | 250/500/1000/2000 | - | 0.03/0.06/0.13/0.27 s | 21 n lines |

Every row is linear in the source's own size and at parity with the pre-audit
binary. The two questions the review added are each one memoized walk of the
subobject tree per class - `vacuous_destruction` was already held per type, and
15.4p14's is folded into the walk `settle_transfers` already makes over the base
and the members, which is what rows two and five hold: linear in depth, not
quadratic in it.

`valgrind` is clean over all 108 probes of this review and over the depth-100
and 250-object ends of the shapes above.

The reference binary was the differential oracle over those 108 probes, run
through the harness's own relaxed comparison, with the checked-in `.ref` files
and `g++` as the third oracle wherever it and we disagreed - which is how
finding 2 was settled against the reference and finding 1 with it. 74 of the 108
agree exactly; the 30 that differ and the 4 both refuse are each named under Open
Gaps.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at 204 / 228 with the failure set
unchanged.

## Open Gaps

**The reference binary and the checked-in `.ref` files disagree about 12.4p8's
empty destructor, and we follow the files.** For `struct T { ~T() {} };` the
binary writes a call of `~T` at every end of a lifetime and the checked-in
outputs of the eleven fixtures that declare one write none, which is
`vacuous_destruction`'s own rule - a destructor whose body writes no statement
comes to what its subobjects come to. `g++` writes the call. This is the one
place where the binary is not the oracle the fixtures are, and a differential
sweep run against the binary alone reads it as a defect in every program that
declares such a destructor. The binary reads the *same* destructor as vacuous
for 5.2.2p4's boundary and for 12.8p12's copy, which is what settled the ABI
above; it is only the call at the end of a lifetime that it writes and the files
do not.

**5.2.2p4's boundary for a derived class that declares its own copy
constructor: the fixtures say bytes and the binary says address.**
`200-trivial-move-does-not-fall-through-to-copy` and
`300-direct-object-parameter-passthrough-base-copy` both pass such a class as
`obj<>`, and `pa17/cppgm++-ref` passes it `by_address`. We follow the files, so
`carried_by_bytes` reads the storage a derived class is laid out over rather
than its own copy constructor. A class that derives from nothing reads its own
either way, and both oracles agree there.

**The reference calls an empty member's assignment operator where its class
declares a destructor at all.** For `struct E { ~E(){} }; struct A { int a; E e; };`
the synthesized `A::operator=` copies four bytes for `a` and then calls
`E::operator=`, which does nothing; we write nothing for `e`, because 12.4p8
makes `~E(){}` vacuous and 9p6 leaves the member no bytes. It is the same
12.4p5-against-12.4p8 disagreement as the first gap, read inside 12.8p28's
definition instead of at an end of a lifetime.

**The reference computes an address twice where we compute it once.** A
synthesized copy constructor whose class has an empty base writes the base
subobject's address and then the member's; we write the member's alone. The
same shape as the discarded id-expression below.

**The reference writes no handler around the copy a `return` makes of a
by-address parameter.** For `T g(T t) { return t; }` the copy into the caller's
destination can throw and the parameter stands, so we destroy it on that edge
and `g++` agrees; `pa17/cppgm++-ref` writes no region there, though it writes
one around a local's construction in the same function.

**8.5p8's zero of a class every subobject of which holds nothing, where one of
them is a base.** `struct E {}; struct C : E { E b; };` costs the reference a
two-byte store for `C c{}` and costs us nothing; `struct C { E a; E b; };` costs
neither of us anything. `has_zeroed_storage` reads the base and the members the
same way, which is what makes the two shapes one answer here and two there.

**8.5.3p5's name for the storage a cast to a reference asks for.** A cast to a
class reference names its slot `refcall`, which is what
`300-indirect-call-result-rvalue-reference-materialization` pins for a
mem-initializer and what the reference writes for a call argument. The reference
names the same storage `tmpobj` where a declaration or a `return` binds the
reference and `arg` where a member access reads it; we write `refcall` in all
four. Moving the name to whoever asked cost that fixture, so the name stays at
the cast.

**5.2.9p4's cast of a glvalue to a class the bytes stand for.** `(W)a` costs the
reference a call of the copy constructor even where 12.8p25 makes that
constructor trivial; we write the `copyobj` the bytes are. The same cast of a
*prvalue* of the target's own class costs the reference a second object and
costs us none, which is 12.8p31 read the other way. A cast to a **base** class
by value - `(B)d` - is the reference's call again where we write the bytes.

**12.8p31 through a cast into a new-expression's storage.** `new W((W)2)` builds
the object in the allocation for the reference and builds a temporary and copies
it for us. It is the placement group's own gap, read at the one destination
`place_class_object` is not threaded through.

**The reference leaves a temporary an arm created undestroyed.** For
`c ? use(T(1)) : use(T(2))` `pa17/cppgm++-ref` writes no destructor call at all
and declares no destructor; `g++` and the checked-in `.ref` files of
`400-conditional-return-branch-temporary-lifetime` and
`400-conditional-prvalue-member-temporary-lifetime` end the temporary at the end
of the arm, which is what we now write.

**The reference leaves a 12.2p5-extended temporary out of the handler's list.**
`const T& r = T(1);` puts the temporary in the block's objects for the normal
path and in no handler, so an exception while the reference bound to it stands
leaks it. We destroy it, which `g++` agrees with, and it is the whole of the
difference on the three probes that bind two references or bind one in an inner
block.

**The reference writes a handler that owes nothing, and one around a call that
throws nothing.** `use(c ? T(1) : T(2))` gets an `eh_try`/`resume` pair around
the conditional's setup with no destruction in it, and a call of a constructor
declared `throw()` gets a region where 15.4p1 says no exception leaves. We write
neither. `f(T(1))` where `T` has a destructor is the same shape: the argument is
the parameter object, so nothing on this side owes an end for it, and the
reference still writes the empty pair. It is the whole of the difference on
twelve of the review's parameter probes.

**The reference's mem-initializer regions nest where ours stand in sequence.**
For `W::W() : m(1), b(use(T(2))) {}` the reference opens the step's region and
then a second one inside it for the temporary; we close the first and open the
second. Both destroy the same objects on the same paths; the block numbering
differs.

**The reference never destroys the elements of a local array of class type.**
`T a[3] = { T(1), T(2), T(3) };` costs it no destructor call at the end of the
block. `g++` destroys them and so do we. It predates this milestone.

**3.6.2p2's namespace-scope initializer is a full-expression this milestone does
not mark.** `int g = use(T(1));` is refused with the message 12.2p3's unmarked
points carry. The five places a full-expression is marked are all inside a
function body; the dynamic initialization of a namespace-scope object is the
sixth and it is written into an initialization function the lowering builds
elsewhere.

**6.4p4's condition-declaration of a class that answers no conversion is not
refused.** `if (T c = make())` where `T` declares no conversion to `bool` is
accepted; `condition_of_declaration` says the caller refuses it and the caller
refuses only the expression form. It predates this milestone.

**A discarded id-expression of class type writes one `addr` where the reference
writes two.** `(s, 2)` costs the reference one `addr $s` for the operand it
discards and one for the object; we write one.

**5.3.4p19's `::`-qualified cleanup: the reference looks the deallocation
function up in the class.** For `::new C[3]` where `C` declares its own
`operator delete[]`, 5.3.4p19 says the name is looked up in the *global* scope
because the new-expression begins with `::`; `g++` emits `_ZdaPv` and we do too,
and `pa17/cppgm++-ref` emits `_ZN1CdaEPv`. This is the reference being wrong and
is left that way. It is visible only on the 15.2p2 cleanup edge, which no
checked-in fixture reaches.

**A constructor defined outside a class that declares two of them is read as
coming to something.** 12.1p5's question of whether a definition does nothing
is taken from the unit's syntax under the constructor's unqualified name, which
every constructor of the class shares, so it is asked only where one of them is
still waiting for a definition. `struct S { S(); S(int); }; ... S::S(){}` writes
the loop where the reference elides it. 8.3.5p4's parameter-type-list is what
would tell the definitions apart, and matching on it belongs with the checkpoint
that next reads an out-of-class definition out of the syntax.

**5.3.4p18/p20's matching placement deallocation function is not chosen.** For
`new (buf) T[n]` the cleanup calls the *usual* `operator delete[]` where p20
says a placement allocation function is matched only by a placement deallocation
function of the same parameter types, and that where none matches, none is
called. The reference does the same, so we follow it.

**The reference refuses a redeclaration whose exception-specification differs;
`g++` and we accept it.** `void operator delete(void*) {}` and
`void* operator new(unsigned long) noexcept;` both mismatch the
exception-specification 3.7.4.1p2/3.7.4.2p2 gave the implicit declaration.
`pa17/cppgm++-ref` refuses them under 15.4p4; `g++` accepts both with a warning,
and so do we. 15.4p4's compatibility rule is not implemented for any function.

**15.4p8's `std::unexpected` filter is not written.** The reference wraps the
body of a function declared `throw()` in an `eh_filter` dispatch that calls
`__cxa_call_unexpected`, and writes `unwind=no` for `noexcept`; we write
`unwind=no` for both spellings and no filter for either.

**15.4p14's `trivial_lifecycle` attribute is not written.** The reference writes
`unwind=no, trivial_lifecycle=yes` over an implicit trivial constructor or
destructor. 15.4p14's own exception-specification is computed now - an
implicitly declared special member, one `= default` declared it, and a
destructor written with none take what the members they invoke allow, so
`unwind=no` is written where the reference writes it - but the second attribute
is not, and the comparison strips both.

**The reference unrolls without bound where we cap.** `kArrayLoopLimit` makes
8.5.1p7's elements a loop past sixteen of them and `kZeroSpanLimit` makes 8.5p6's
zero one `zeroinit` past sixty-four bytes; the reference writes n calls and n/2
stores at every size (`new Triv[100]()` costs it fifty `store i64` and costs us
one `zeroinit 400x4`). Both caps predate this milestone and the checked-in
fixtures ask for the unrolled form only below them.

**The floating zero an object is value-initialized with is spelled twice.** The
analysis spells it `0.0F` for `f32`, as the reference does; the lowering
re-spells it `0.0f` through `spell_floating`, so `float d{}` and `new float()`
both differ. The global image is the other half of the same seam: we write the
program's own digits (`1.0f`) where the reference re-renders the value (`1f`).
Both predate this milestone and no fixture covers either.

**4.4's qualification conversion to `volatile` is refused.** `int* q;
volatile int* p = q;` is "an expression has no conversion to the type it
initialises". `const` is accepted. It predates this milestone.

**5.3.4p6's non-integral bound and 5.3.5p2's non-object operand are refusals the
reference does not make.** `new int[1.5]`, `delete` of a pointer to function and
`delete` of an object whose destructor is deleted are each refused by `g++` and
by us and accepted by the reference.

**The reference ignores 8.3.5p1 on the assignment path**, accepts a
ref-qualifier on a constructor, a destructor and a friend, and accepts an
out-of-class definition whose ref-qualifier matches none. `g++` refuses all of
them with us.

**A ref-qualifier written twice, or written before the cv-qualifier-seq, is
accepted.** `void f() & &&;` and `void f() & const;` are refused by `g++` and
accepted by the reference and by us; the parser takes the suffixes in any order
and any number, as it already did for the cv-qualifier-seq. Both readers of the
qualifier take the first one written, so they cannot disagree.

**13.5.6's overloaded `operator->` is not implemented.** `object_region` refuses
a `->` whose operand is of class type. No fixture covers it.

**10.3's virtual dispatch is outside this milestone**, which the README says
outright, so an override is called non-virtually and no vtable is written.

**Class templates are outside this milestone**: `S<int>` names no declaration.

**The reference writes a `function-declaration` line for every member function a
class declares** and we write none; no pa12 fixture covers it. **PA11's
reference refuses a conversion function outright** where we describe it, which is
that milestone's own scope limit.

**The reference drops an observable conversion of an empty class.** For
`struct T {}; struct U { operator T() { ++calls; return T(); } };`, `T t = u;`
makes `pa17/cppgm++-ref` write no call at all; `g++` calls it, and 1.9p12 says
it must. We write the call.

**Pointer to member is outside the PA15 lowering subset**, so a conversion
function to one is refused at its use rather than at its declaration.

**`S::~S() = default;` written outside the class is not parsed at all**, which is
what `spec/200-out-of-class-defaulted-special-members` waits on.

**12.8p31 at a member or a base subobject.** `construct_object` refuses the
elision wherever the object is a subobject - the `!member` gate - so
`B::B() : m(f()) {}` writes a temporary and a copy where the reference builds
the returned object in the member. Giving the resolved tree a shape for a
subobject initialized by a bare initializer is the fix, and it belongs with the
checkpoint that next touches 12.6.2's mem-initializer.

**12.8p15's array member of a class whose transfer needs a call** is still
refused; `general/300-synthesized-array-member-special-members` is the fixture
and the form is the loop 12.6p1 and 12.4p8 already use.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1 | 12.8's four value-transfer special members | `c2894e79` | 6 / 6: one class's `operator=` bound into another's and its definition never written (also O(n²) in inheritance depth), 11.4p1's protected base member read as inaccessible, 12.8p11's deleted copy bypassed at every by-value boundary, a trivial transfer lowered as nothing, the site form disagreeing with the reference, and 12.8p15 having no form for an array member | 86 -> **88 / 228**; pa1-pa16 1494 / 1494; file audit passes |
| C2 | 6.6.3p2's returned object and 12.8p31's result object | `be9d930d` | 7 / 7: 5.16p3's result object never initialized from a glvalue operand (which refused two fixtures), 12.8p31's elision and the lowering's placement as two answers to one question, 3.6.2p2's namespace-scope initialization read as an array of clauses (a refusal one way and silently dropped initialization the other), 12.1p5's "nothing to do" answered a second time and wrong for a trivial copy, a discarded conditional as one object per arm and its storage named by the lowering rather than by what asked, a trivial transfer giving a returned value storage one copy too late, and 9p6's empty class with no byte to hand back | 112 -> **117 / 228**; pa1-pa16 1494 / 1494; file audit passes |
| C3 | 12.3.2's conversion functions, end to end | `8c59f91a` | 6 / 6: 13.3.3.1.2p1's one-user-defined-conversion flag set for the conversion function's direction and not the converting constructor's (which refused every `B b(s);`), 8.5.3p5's conversion-to-an-lvalue hook standing below the refusal of a temporary (which refused every non-const lvalue reference bound through one), 12.4p8's `empty_body` read before the out-of-class definition that writes it and the wrong answer then memoized for the unit, 13.3.1.5's candidates ordered by the object argument ahead of where the conversion gets to (a base's exact-match conversion losing to a nearer base's, and the result truncated), a cast of a class operand no conversion answers reading the object's bytes instead of being refused, and 13.6p3/p5's `++E` gated out on a rule that is not true | **149 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
| C4 | 8.3.5p1's ref-qualifiers, end to end | `9f693145` | 4 / 4: a using-declaration rebuilding the brought-in member's type without the ref-qualifier, so 13.3.1p4 made an `&&`-qualified base member viable on an lvalue and 7.3.3p14 could not see the derived class's own declaration of the same spelling as hiding it; 13.1p2's refusal probed for the one cv-qualification the declaration wrote instead of all four, so every `f() const` beside `f() &&` was accepted; the two qualifiers written after the parameter-clause dropped from PA11's `--emit-types` and spelled a second time on the form 9.3.1p3 had already lowered in PA12's `--emit-semantics`; and 5.2.5p4's first clause missed, so a reference member of a non-lvalue object was an xvalue and reached an `&&`-qualified member | **163 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
| C5 | 5.3.4 and 5.3.5, end to end | `6c785249` | 7 / 7 + 1 refusal: `Fact::elements == 0` read as "the bound is not a constant", so `new T[0]` recomputed a count the source wrote from the bytes the call was asked for; 8.5p7's zero over an extent the translation knows written as a byte loop, and over `bytes` rather than `bytes - 8` past a class array's count; 8.5p7's value-initialization of an array given no vacuity exit, so `new Triv[n]()` wrote n calls of a trivial constructor with 15.2p2's handler around them; `vacuous_construction` asking whether the class holds nothing at all instead of walking the subobject tree 12.4p8's counterpart walks, and reading neither a mem-initializer-list nor 3.4.1p8's out-of-class definition; 5.3.4p15's test gated on 15.4 alone rather than on 18.6.1.1p3's `std::nothrow_t`, so it stood around placement forms the reference leaves alone and around no array form at all; 15.2p2's cleanup naming the ABI's base-object destructor for elements that are complete objects; and C5's own `nonthrowing` fact taking `unwind=no` off every reserved builtin. The refusal: 5.3.5p5 leaves `delete` of an incomplete class - and `void*` with it - undefined and not ill-formed | **186 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
| C6 | 12.2p3's full-expression boundary and 15.2p2's handler in an ordinary body | `67babaa8` | 8 / 8: 15.2p2's region closed after 3.8p1's ends of the objects a block declares rather than in front of them, so every `return` and every block end wrote a destructor call under a handler that destroys the same object; `close_unwind_region` putting a handler back in the cache keyed on a count after the objects that made it good had been destroyed, so `use(T(1)); use(T(2));` named the first temporary's handler for the second and an if/else arm named the other arm's; 5.14p1 and 5.16p1's conditionally-evaluated operands having no frame, so a temporary an arm or a short-circuited right operand created was destroyed on the paths that never created it; the standing list copied once per region a lifetime ended inside, which was quadratic (1.13 s at 2000) under output linear at 34 n lines; 8.5.3p5's name for the storage of a discarded prvalue; 12.2p3's two cleanup edges out of a condition numbered before the region they leave was closed; 6.5.3p1's for-init-statement lowered after the loop's own blocks were numbered; and `lowir_lower_object.cpp` past the audit's 3000-line limit with `kUnwindSuffixLimit` defined twice, split at 15.2p2's own seam into `lowir_lower_unwind.cpp` | **194 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
| C7 | 5.2.2p4's class argument at the boundary, and the placement facts swept beside it | `ba854b1d` | 8 / 8: 12.4p5's triviality read where this translation's own end of a lifetime is 12.4p8's vacuity, so a class with an empty-bodied destructor was passed by address and returned indirectly where the reference does neither; 5.2.2p4's copy half read off the base class subobject alone, so a derived class with a member whose copy is a call was passed as raw bytes; 12.8p12 asked without 12.4p8 by every other reader, so a class whose destructor comes to something was copied with `copyobj` and the copy constructor 12.8p15 defines for it was never written at all; 15.4p14's exception-specification never computed, which the calls that fix exposed would have wrapped in handlers the reference does not write; 5.2.2p4's parameter ended at one exit of three, so a constructor and every member function defined in its class body leaked the object the caller built and 15.2p2's handler owed nothing for it; the caller owing that same end a second time, naming the ABI's base-object destructor for a complete object; 5.2.9p4's cast of a glvalue to its own class refused outright wherever the class wrote a copy constructor; and `creates_its_object` compared the cast's cv-qualified type where the lowering stripped it, so `(const W)a` copied where `(W)a` elided | **204 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
