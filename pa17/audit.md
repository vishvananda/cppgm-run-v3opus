# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C5, reviewed at `6c785249`.** The architecture holds and is the right one.
5.3.4 and 5.3.5 are actions over storage the expression already names, not a
second object model: `sema_allocation.cpp` owns the two lookups and the two
sizes, the initialization is the one 8.5 would give an object of the type
declared with the same initializer, and the destruction is 12.4p3's own action.
3.7.4.1p2's four functions being ordinary declarations made in the global
namespace before the unit is read is the right shape too - a program that writes
one redeclares it, 13.3 chooses among them beside whatever the program declared,
and the object file names them what the implementation calls them. Nothing
walks for the lookup: a hierarchy 2000 deep whose root declares
`operator new`/`operator delete` with the allocation written at the leaf costs
the same 0.08 s and the same 30 lines as the identical hierarchy with no
allocation function in it.

What the review found is seven defects, and they are two patterns. Four are the
first: **the array form asked the running program for what the translation had
already settled** - the count, the extent of 8.5p7's zero, and whether building
one element comes to anything at all. Three are the second: **a fact was written
in one place and read in fewer places than own the question** - 5.3.4p15's test,
the ABI entry 15.2p2's cleanup names, and the boundary C5's own `nonthrowing`
took off the reserved builtins. The last two of those are attributes the relaxed
comparison strips before it compares, so no fixture could ever have failed on
them; they were found by diffing that metadata by hand.

**1. A count of zero was read as a count the translation does not know.**
`Fact::elements` carried both "how many elements" and, by being zero, "the bound
is not a constant" - so `new T[0]` took the run-time path: a slot for the bytes
the call was asked for, a `sub` and a `udiv` to get the count back out of them,
and all of that twice for a class array, to arrive at the zero the source wrote.
5.3.4p6's bound is the one array bound the standard does not require to be
constant, so whether it *is* one is a fact of its own; `Fact::counted` is that
fact and a bound of zero is now a bound like any other.

**2. 8.5p7's zero over an extent the translation knows was written as a loop.**
`new int[4]()` wrote a three-block byte-at-a-time loop over sixteen bytes where
the reference writes one `zeroinit 16x4`. The same code zeroed `bytes` rather
than `bytes - 8` for a class array with a run-time bound, so it ran eight bytes
past the last element - the count 5.3.4p1 writes in front of them is part of
what the call asked for and no part of what they stand in. The zero now asks
the count what the extent is: one instruction over an extent the
translation knows, the spans `zero_object` writes where the elements are objects
of class type, and the loop only where the bound is a value.

**3. 8.5p7's value-initialization of an array had no vacuity exit at all.**
`array_new_initialization` asked `vacuous_construction` only where *no*
initializer was written, so `new T[n]()` always wrote a per-element constructor
loop with 15.2p2's handler and 5.3.4p18's deallocation behind it - for
`struct Triv { int a; };`, n calls of a constructor 12.1p6 makes trivial, and a
cleanup for an exception none of them can throw. It is the sibling of the exit
the checkpoint did write. 8.5p7 gives the question two halves and both are now
asked in the one place: the storage is zeroed where the default constructor is
neither user-provided nor deleted, and the constructor is called only where
12.1p6 left it something to do.

**4. `vacuous_construction` was not the walk of the subobject tree its
counterpart is.** The plan lists it beside `vacuous_destruction` as one of the
questions each layer asks once, but where 12.4p8's walks the class's bases
and members, this one asked whether the class *holds nothing at all*
(`empty_class && base == nullptr`). So `struct Y { int a; Y(){} }` - a
constructor whose definition does nothing over a class with nothing to build -
was built element by element in a loop in which nothing happens, and so was a
class whose only member is one of those. It is now the same walk, held per type,
with the two things that make a definition come to something counted: 12.6.2p8's
brace-or-equal-initializer on a member, and a mem-initializer-list, which
`writes_no_statement` now reads beside the compound-statement. 3.4.1p8's
definition written outside the class is taken from the unit's syntax the way a
destructor's already was, so `Y::Y(){}` written after the body asking about it
gets the same answer as one written before - but only where one constructor of
the class is still waiting for a definition. A class's destructor has one
unqualified name and its constructors *share* one, so with two of them waiting
the syntax cannot say which definition defines which, and taking the first would
give the default constructor another's body: `struct S { int a; S(); S(int); };`
with `S::S(int){}` written before `S::S(){ a = 7; }` and both after the use had
the array built with no constructor call at all. Where two are waiting the
answer waits for the read to reach them.

**5. 5.3.4p15's test of the address was written for the wrong set of allocation
functions, and never for the array form.** The gate was "15.4 says it throws
nothing, and it is not 18.6.1.3p2's `(size_t, void*)`". That wrote a branch
around every `new (tag) T` whose placement function happened to be `noexcept`
and around a class's own `operator new(size_t) noexcept`, where the reference
writes none - and wrote none at all around `new (std::nothrow) T[n]`, where the
reference does. 18.6.1.1p3 is the half of the answer that was missing:
`std::nothrow_t` is the argument a program writes to ask for the form that
reports failure with a value rather than an exception, and a placement form that
merely promises not to throw obtains its storage from wherever the program said
and has no failure of its own to report. The question is settled once, in the
analysis, as `Fact::may_fail`, and both forms read it; the array form holds its
value in an object of its own, because the step past 5.3.4p1's count is itself
under the test.

**6. The array-new cleanup destroyed complete objects through the ABI's
base-object entry.** 15.2p2's handler destroys the elements already built, and
each of those is a complete object of the element's class - but nothing on that
path told the analysis so. The cleanup writes no destructor-action of its own
and the lowering derives the destructor from the constructor, so a class whose
destructor no *other* use reached was named `_ZN1CD2Ev` where the reference and
the ABI write `_ZN1CD1Ev`. The relaxed comparison strips `object=` and pairs
functions by it, so no fixture could ever have seen this. `array_new_initialization`
now notes the complete-object entry the way `delete_expression` does.

**7. C5's own `nonthrowing` fact took `unwind=no` off every reserved builtin.**
The line `describe_builtin` writes was `CUM_NO` outright before C5 and became
`entity.nonthrowing ? CUM_NO : CUM_DEFAULT` - and `reserved_function` sets no
such fact, so `__builtin_memcpy`, `__builtin_memmove`, `__builtin_strlen` and
`__builtin_unreachable` all silently lost the boundary 17.6.5.12 gives them.
`unwind=` is one of the attributes the comparison strips, so the suite stayed
green. 15.4p14 says of them what 15.4p1 says of a program's `noexcept`, so the
fact is now written on each declaration where it is made; and a definition the
program wrote with a non-throwing exception-specification carries the boundary
over its body, which it did not before. The reference writes it on a definition
and not on a declaration of a function the program wrote, and now so do we.

**Refusals the checkpoint made that neither oracle makes.** 5.3.5p5 leaves
`delete` of a pointer to an incomplete class - and 5.3.5p2's `void*` with it -
undefined rather than ill-formed: what the program loses is the destructor call
the definition would have named, and the storage still goes back. Both were
refused outright. The expression is now written for what the type does say,
which for an incomplete type is one deallocation and no test of the address,
because there is nothing under it to guard.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | time | output |
| --- | --- | --- | --- |
| n scalar `new`/`delete` pairs of a class with a constructor and a destructor | 250/500/1000/2000 | 0.02/0.03/0.06/0.11 s | 15 n + 27 lines |
| n array `new T[3]`/`delete[]` pairs | 250/500/1000/2000 | 0.04/0.07/0.14/0.28 s | 83 n + 27 lines |
| n array pairs whose bound is a call | 250/500/1000/2000 | 0.05/0.09/0.17/0.33 s | 91 n + 28 lines |
| n classes each declaring their own `operator new`/`operator delete`, each used | 250/500/1000/2000 | 0.06/0.11/0.22/0.44 s | 36 n + 6 lines |
| hierarchy n deep, root declaring `operator new`/`delete`, allocated at the leaf | 250/500/1000/2000 | 0.02/0.03/0.04/0.08 s | 30 lines |
| the same hierarchy with **no** allocation function in it | 250/500/1000/2000 | 0.02/0.03/0.04/0.08 s | 30 lines |
| `new T[N]`/`delete[]` for a class with a constructor and a destructor | 100/1000/10000/100000 | 0.01 s at every N | 110 lines |
| `new int[N]()` | 100/1000/10000/100000 | 0.01 s at every N | 15 lines |
| `new Triv[N]()` for a trivial class | 100/1000/10000/100000 | 0.01/0.01/0.01/0.03 s | 27 lines |
| conditionals nested n deep inside an array bound | 50/100/200/400 | 0.01/0.01/0.01/0.02 s | 14 n + 17 lines |
| members nested n deep under `new L[4]()` | 50/100/200/400 | 0.01/0.01/0.02/0.03 s | 26 lines |

Rows seven to nine are findings 1, 2 and 3 together, and they are the point of
all three: the count is a value the expression carries, so `new T[100000]` is
the same 110 lines as `new T[100]`; 8.5p7's zero over 400 000 bytes is one
instruction; and a trivial class's elements are zeroed rather than constructed
one at a time. Before the review the second of those was a byte loop and the
third was a per-element call at every size. Row ten is the analysis asking 5.19
for the bound before reading it as a value: the two readings are linear in the
depth of the operand and not exponential in it. Row eleven is finding 4's walk,
held per type, over a subobject tree that comes to nothing at every depth. Rows
five and six are the checkpoint's own invariant, re-measured: 5.3.4p9's lookup
costs the depth the source wrote and nothing more.

`valgrind` is clean on all 200 lowering probes, refusals included.

The reference was used as a differential oracle over those 200 probes, run
through the harness's own relaxed comparison, and `g++` was the third oracle on
every verdict the two disagreed about. Every probe on which the reference and we
both accept now produces LowIR the harness accepts as equal, save the two named
under Open Gaps where `g++` agrees with us against the reference. The attributes
the comparison strips - `object=`, `binding=`, `unwind=`, `role=`, `effects=` -
were diffed separately over the same corpus, which is what findings 6 and 7 were
found by, and the only differences left there are the ones Open Gaps names.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at 186 / 228 with the failure set
unchanged.

## Open Gaps

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

**15.4p14's exception-specification of an implicitly declared special member is
not computed.** The reference writes `unwind=no, trivial_lifecycle=yes` over an
implicit trivial constructor or destructor; we write neither attribute anywhere.

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

**A discarded id-expression of class type writes no `addr`.** `(s, 2)` costs the
reference one `addr $s` for the operand it discards and costs us none. It
belongs with 12.2p3's full-expression group.

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
