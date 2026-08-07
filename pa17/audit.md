# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C4, reviewed at `9f693145`.** The architecture holds and is the right one.
8.3.5p1 says the ref-qualifier is part of the function type, and the checkpoint
puts it there: one field of the interned node, beside 8.3.5p7's cv-qualifier-seq
and in the same step of `shape_of`'s key, so `declarator_type` writes it once and
the declaration, the out-of-class definition, 13.1's signature, the Itanium name
and a pointer to member all read the one fact. All ten cv/ref manglings agree
with `g++` and with the reference byte for byte. `Value::object_category` is the
right shape too: 9.3.1p3 holds the implicit object parameter as a pointer, and
`object_match` is the one place the pointer's conversion and the reference's
binding are asked together, reached by a member access, a call with no object
expression, an operator's left operand and 13.3.1.5's own candidate set alike.
Nothing in the checkpoint is a second pipeline and nothing walks for the
qualifier: a hierarchy 2000 deep whose root declares `&`, `const &` and `&&` and
is called three ways costs the same 0.11 s and the same constant output as the
identical hierarchy with no ref-qualifier at all.

What the review found is four defects, and they are one pattern seen four times:
a fact hung on the function type has to be read by *every* layer that rebuilds
or spells that type, and three rebuilders were updated while a fourth was not,
and two dumps spell the type the analyzer has and were never asked what they
now print.

**1. A using-declaration rebuilt the shadow's type without the ref-qualifier.**
13.3.3.1p4 makes a member a using-declaration brought into a derived class a
member of *that* class where the implicit object parameter is concerned, so
`declare_using_member` rebuilds the base's function type with the derived
class's object pointer in front. It rebuilt it with `function_of` alone, which
drops 8.3.5p1's qualifier - the one rebuilder of a member function type that C4
did not carry it through, where `with_object_parameter`, `member_pointer_of` and
`substitute` all do. Both readers of the fact then read the wrong one.
13.3.1p4's viability: `struct B { int f() && ; }; struct D : B { using B::f; };`
made `D d; d.f();` call `B::f() &&` on an lvalue, which the reference and `g++`
both refuse. And 7.3.3p14's hiding, which is keyed on the same signature: a
derived class that declared `int f() &` of its own no longer hid the base's
`int f() &`, so `d.f()` was refused as ambiguous where both oracles call the
derived one. The shadow now carries the qualifier over with the rest of the
type, and the two questions read the one fact again.

**2. 13.1p2 was asked of one cv-qualification instead of all four.**
`require_uniform_ref_qualifiers` probed the chain's index for the signature the
other ref-spelling would have given *with the object parameter the declaration
wrote*. But 13.1p2 is keyed on the name and 8.3.5p4's parameter-type-list, and
8.3.5p7's cv-qualifier-seq is no part of that list - so `void f() const;` beside
`void f() &&;` is a set the rule refuses just as `void f();` beside
`void f() &&;` is. Every cv mismatch was accepted: `f() &`/`f() const`,
`f() volatile`/`f() const &`, `operator int() const`/`operator int() &&` and
`f(int) const`/`f(int) &&` are each refused by `g++` and were each accepted
here. The probe now asks for each of the four qualifications, which is four or
eight reads of a map per declaration and no walk: one class declaring 2000
member functions is 0.10 s, the same shape it was.

**3. The two qualifiers written after the parameter-clause were spelled in the
wrong assignment's dump - both ways.** `declarator_type` wrote them onto the
function type only under `semantics()`, so PA11's `--emit-types` described
`int f() &;` as `function of () returning int` and `typedef int F() const;` as
`function of () returning int`, where the reference writes `function of () &
returning int` and `function of () const returning int`. That hole was there for
the cv-qualifier-seq before C4 and no pa11 fixture covers it; C4's new field
joined it. In the other direction, C4 made PA12's `--emit-semantics` spell the
ref-qualifier on the form 9.3.1p3 has *already* lowered - `function-definition
S::f function of (pointer to struct S) & returning int` where the reference
writes no `&`, because the cv-qualifier-seq beside it has by then moved onto the
object parameter and the ref-qualifier is carried as a fact rather than spelled
twice. Both are one rule: the qualifiers are spelled on the type the declarator
wrote, and the lowered form spells the object parameter instead. The guard is
gone, so a typedef, a pointer to function and a pointer to member all hold and
spell both; `function_description` is the one reader that leaves the
ref-qualifier unspelled where the object parameter already stands for it, used
by the four lines that name a function's own type. PA11 and PA12 now agree with
the reference on every ref-qualified and cv-qualified shape probed.

**4. A reference member of a non-lvalue object was given the xvalue category.**
5.2.5p4 answers `E1.E2` in two steps, and the first is that a member declared to
have reference type makes the expression an lvalue whatever E1 was - what such a
member names is the object it is bound to and not a subobject of E1. Only after
that does the rule that a member of a non-lvalue object is an xvalue apply.
`member_value` already gave a reference member the *type* the first step gives
it and then took its *category* from the second, so
`struct S { M& m; }; S(g).m.f()` reached `M::f() &&`, which the reference and
`g++` both refuse. It is the README's "xvalue propagation through non-static
data-member access" read one clause short, and the clause it missed is the one
13.3.1p4 then binds by. The category now comes from the same step the type
does.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | time | output |
| --- | --- | --- | --- |
| n classes each declaring `f() &` and `f() &&`, both called | 250/500/1000/2000 | 0.04/0.09/0.19/0.40 s | 32 n + 7 lines |
| one class declaring n such pairs, all 2 n calls written | 250/500/1000/2000 | 0.02/0.05/0.10/0.22 s | 22 n + 17 lines |
| one class declaring n **unqualified** members, all called | 250/500/1000/2000 | 0.01/0.02/0.05/0.10 s | 10 n + 9 lines |
| hierarchy n deep, root declaring `&`, `const &`, `&&`, called three ways | 250/500/1000/2000 | 0.01/0.01/0.04/0.11 s | 54 lines |
| the same hierarchy with **no** ref-qualifier at all | 250/500/1000/2000 | 0.00/0.01/0.04/0.11 s | 47 lines |
| n members brought in by n using-declarations, all called | 250/500/1000/2000 | 0.01/0.03/0.06/0.13 s | 11 n + 9 lines |
| a using-declaration chained n classes deep, called both ways | 50/100/200/400 | 0.00/0.00/0.01/0.01 s | 41 lines |
| n chained calls of `f() &` returning `S&` | 50/100/200/400 | under 0.01 s | n + 19 lines |
| n nested calls of `g() &&` returning a class by value | 50/100/200/400 | under 0.01 s | 4 n + 26 lines |

The third row is what finding 2 costs: every one of those n declarations now
pays 13.1p2's eight probes instead of two, and the axis is the same time it was.
The sixth and seventh are finding 1's: the shadow carries one more field, the
chain of using-declarations writes a constant number of lines at every depth, and
neither the type nor the signature is rebuilt per use. Rows four and five are
the checkpoint's own invariant, re-measured: the ref-qualifier is carried and
never walked for, so declaring three spellings at the root of a 2000-deep
hierarchy costs the same time as declaring none.

`valgrind` is clean on all 75 lowering probes and on 20 `--emit-types` /
`--emit-semantics` probes, refusals included. The nesting sweeps above are
linear in depth, not exponential in it.

The reference was used as a differential oracle over 75 lowering probes and 20
declaration-dump probes, run through the harness's own relaxed comparison. Every
probe now agrees with `g++` on the verdict, and every probe on which the
reference and we both accept produces LowIR the harness accepts as equal, save
the one virtual-dispatch probe named under Open Gaps. Ten are refusals the
reference does not make - a ref-qualifier on a constructor, a destructor and a
friend, five declarations mixing a cv-qualification with a ref-qualifier, an
out-of-class definition whose ref-qualifier matches none, and an assignment on
an rvalue - and `g++` refuses all ten with us.
Findings 1 and 4 were settled by the reference and `g++` agreeing against us;
findings 2 and 3 by `g++` and by the standard's own wording where the reference
is lenient.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at 163 / 228 with the failure set
unchanged.

## Open Gaps

**The reference ignores 8.3.5p1 on the assignment path.** `S() = 5` for
`struct S { S& operator=(int) & ; }` and `a = b` for a class whose member's
`operator=` is `&&`-qualified are both accepted by `pa17/cppgm++-ref` and
refused by `g++`; we refuse. The reference also accepts a ref-qualifier on a
constructor, a destructor and a friend declaration, and an out-of-class
definition whose ref-qualifier matches no declaration. These are the reference
being lenient and are left that way.

**A ref-qualifier written twice, or written before the cv-qualifier-seq, is
accepted.** `void f() & &&;` and `void f() & const;` are refused by `g++` and
accepted by the reference and by us; the parser takes the suffixes in any order
and any number, which it already did for the cv-qualifier-seq before C4. Both
readers of the qualifier take the first one written, so they cannot disagree.

**13.5.6's overloaded `operator->` is not implemented.** `object_region` refuses
a `->` whose operand is of class type, so a ref-qualified `operator->` is
refused with every other one. No fixture covers it.

**10.3's virtual dispatch is outside this milestone**, which the README says
outright, so a ref-qualified override is called non-virtually and no vtable is
written. No fixture covers it.

**Class templates are outside this milestone**: `S<int>` names no declaration,
so a ref-qualified member of a class template is refused at the use.

**The reference writes a `function-declaration` line for every member function a
class declares** and we write none. It is unrelated to the ref-qualifier - a
class with no qualifier at all shows the same - and no pa12 fixture covers it.

**PA11's reference refuses a conversion function outright** ("unsupported
declaration kind special-member-declaration") where we describe it. That is the
reference's own scope limit at that milestone.

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
