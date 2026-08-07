# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C3, reviewed at `8c59f91a`.** The architecture holds and is the right one.
12.3.2p1's "the name is a type" is carried as `CarriedTypeId` beside the syntax
and resolved once, so the region binds the type's own spelling and a declaration,
an out-of-class definition, `a.operator T()` and the ABI's `cv` terminal cannot
disagree - `operator myint` and `operator int` are one function, and every
mangling probed (`_ZN1ScviEv`, `_ZNK1ScviEv`, `_ZNV1ScvlEv`, `_ZN1ScvPKcEv`,
`_ZN1ScvRiEv`, `_ZN1Ucv1TEv`, `_ZN1Vcv1EEv`, `_ZN1ScvyEv`) is the reference's.
The conversions a class declares stay on that class and `conversions_above`
chains only the classes that declare any, so a hierarchy n deep costs one step
per class rather than a walk per question. `vacuous_destruction` is one question
with one memo. Nothing in the checkpoint is a second pipeline.

What the review found is six defects. Two are 13.3 answering half a question -
the flag that stops a second user-defined conversion was set in one direction
and not the other, and the reference-binding hook stood below the refusal it had
to precede. One is 12.4p8's new fact being read before it was written. One is a
ranking that put the object argument ahead of where the conversion gets to. One
is a cast that reached no conversion and read the object's bytes instead of
refusing. One is a 13.6 gate whose comment states a rule that is not true.

**1. 13.3.3.1.2p1's "one user-defined conversion" ran in one direction only.**
`conversion_match` measures its second, standard sequence under
`standard_only_`; `converting_constructor` guarded *itself* with that flag but
did not set it while measuring its own parameter. Before C3 nothing could
reach a constructor's built-in parameter from a class argument, so the omission
had no way to show; with 12.3.2's conversions it does. `struct B { B(int); }`
became a converting constructor *from* any class with `operator int`, which made
`B(const B&)` and `B(B&&)` viable beside `B(int)` on one argument and left
`B b(s);` and `B b = s;` refused as ambiguous - a program the reference and
`g++` both accept. The same flag now stands around both sequences, which is
also what the plan already claimed of it.

**2. 8.5.3p5's conversion to an lvalue stood below the refusal of a
temporary.** `match_reference` reached `conversion_match` only after the guard
that a non-const lvalue reference binds no temporary - but the lvalue a
conversion function hands back is an object of its own, not a temporary, and a
non-const lvalue reference binds one. `int& r = s;`, `T& r = s;` and `f(s)` for
`void f(int&)` on a class with `operator int&` were all refused where the
reference and `g++` bind. The hook now stands before that guard, and the guard
below it still refuses what it was written for: a conversion that hands back a
prvalue reaches a non-const lvalue reference through nothing.

**3. 12.4p8's `empty_body` was read before it was written.** A member defined
outside its class stands wherever the program put it, and 3.4.1p8 has every
namespace-scope function body read where *it* is written - so
`struct S { ~S(); }; void f(){ S a; } S::~S() {}` asked whether destroying an
`S` comes to anything before the definition that answers had been read, wrote
the call, and then held that answer in the per-type memo for the whole unit,
so the function *after* the definition wrote the call too. Definition-first was
right and definition-last was wrong, which is the worst shape a fact can have.
The unit's syntax is complete before the first declaration of it is read, so
`collect_unit_definitions` takes the out-of-class definitions out of it once,
keyed by the unqualified name each defines, and `note_definition_body` resolves
3.4.1p8's prefix against the class itself to say which of them defines this
destructor. `writes_no_statement` is the one reading of a body both that and
`open_special_member_body` do, so the two cannot answer differently. 250
classes whose destructors are defined after their uses now emit exactly what the
same program with the definitions written first emits, and both are the
reference's.

**4. 13.3.1.5's candidates were ordered by the object argument first.**
`conversion_match` compared how the operand reached each candidate's object
parameter and only broke ties on where the conversion got to. For
`struct B1 { operator int(); }; struct B2 : B1 { operator long(); };
struct B3 : B2 {};`, `int b = x;` therefore chose `B2::operator long` - the
nearer base - and truncated its result, where the reference and `g++` both call
`B1::operator int`. It is a wrong value, not a refusal. The second standard
conversion sequence to the destination now orders these candidates and the
object argument tells apart only the ones that get equally far, which is still
what picks `operator T()` over `operator T() const` on an object that is not
const.

**5. A cast of a class operand that reached no conversion read the object's
bytes.** C3 gave 5.2.9p4/5.4p4 to `explicit_conversion` but neither cast site
looked at whether it answered, so `(char)t`, `int(t)` and
`static_cast<char>(t)` on a class with no conversion - or with only an
`explicit operator int`, which 13.3.1.5p1 keeps out of a cast to `char` - fell
through to `load obj<1x1> $t` and `copy i8`, which is a value of the target type
by accident of layout and not at all for a class of another size. The reference
and `g++` refuse all four. `cast_conversion` is that question with the refusal
in it; 5.2.9p4's cast to `void`, a cast to a class and a cast to a reference are
each the initialization answered elsewhere and stay where they were.

**6. 13.6's `++E` and `--E` were gated out with a rule that is not true.**
`builtin_operands` returned false for `OP_INC`/`OP_DEC` on the ground that an
operand taken by reference is one no conversion function can fill. 13.6p3 and
p5 write those candidates over `VQ T&`, and a conversion function that hands
back an lvalue reaches one: `++s` on a class with `operator int&` is accepted by
the reference and by `g++` and was refused here. `builtin_conversion_type` now
answers that question too, and `increment_expression` reads back the operand the
conversion made - the one read-back of the five 13.6 sites that C3 wrote and
this one had not. `s = 5`, `s += 5` and `&s` stay refused, which is where the
reference stands; the by-value `operator int()` still reaches no `++`, as
`g++` also has it.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | time | output |
| --- | --- | --- | --- |
| n classes, destructor defined out of class **after** its use | 250/500/1000/2000 | 0.02/0.04/0.10/0.22 s | 21 n lines |
| the same n classes with the definitions written **first** | 250/500/1000/2000 | 0.02/0.05/0.10/0.22 s | 21 n lines, byte-identical |
| hierarchy n deep, every class declaring a conversion | 250/500/1000/2000 | 0.01/0.03/0.07/0.17 s | 17 lines |
| hierarchy n deep, only the root declaring one | 250/500/1000/2000 | 0.00/0.01/0.04/0.11 s | 18 lines |
| one class declaring n conversions, one use | 250/500/1000/2000 | 0.01/0.03/0.06/0.14 s | 30 lines |
| n uses of one conversion | 250/500/1000/2000 | 0.00/0.01/0.02/0.04 s | 4 n lines |
| n direct-initializations of a class from a class through 13.3.3.1.2 | 250/500/1000/2000 | 0.01/0.02/0.05/0.11 s | 11 n lines |
| n nested casts through one conversion | 250/500 | 0.00/0.00 s | 16 lines |

Every axis is linear in time and in output, and the two that grow the *class*
rather than the uses write a constant number of lines - the candidate set is one
walk of the classes that declare a conversion and never one per base. The
syntax walk `collect_unit_definitions` added costs nothing measurable: the two
destructor axes above are the same program in two orders and their times are the
same. 520 nested casts with an out-of-class destructor definition behind them
parse and lower without a fault; 900 nested parentheses are refused by the
parse-depth budget PA10 already had, where `pa17/cppgm++-ref` faults.
`valgrind` is clean on every shape above and on 19 probes of the checkpoint's
subject, refusals included.

`pa17/cppgm++-ref` produces PA17 output, so it was used as a differential oracle
over 61 probes: the conversion mangling of ten declared types and three
cv-qualifications, the hiding and depth of conversions across a hierarchy,
`a.operator T()` written with a typedef, the out-of-class definition, every
contextual conversion 4p3 and 6.4.2p2 reach, thirteen built-in operator shapes,
seven cast and initialization shapes, source order, and the multiplicity of both
conversions and uses. 48 agree after the harness's function-name and metadata
canonicalization, 8 more agree on the verdict where both refuse, and the 5 that
do not are named under Open Gaps. `g++` was the third oracle wherever the
reference's reading is its own rather than the standard's, and it settled
findings 1, 2, 4, 5 and 6 - in each of those the reference and `g++` agreed
against us.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at 149 / 228 with the failure set
unchanged.

## Open Gaps

**The reference drops an observable conversion of an empty class.** For
`struct T {}; struct U { operator T() { ++calls; return T(); } };`, `T t = u;`
makes `pa17/cppgm++-ref` write no call at all, so `calls` stays zero; `g++`
calls it, and 1.9p12 says it must. We write the call. This is the reference
being wrong and is left that way; a class of non-zero size agrees exactly.

**A discarded id-expression of class type writes no `addr`.** `(s, 2)` costs the
reference one `addr $s` for the operand it discards and costs us none. It is not
C3's subject - a class with no conversion function at all shows the same - and
it belongs with 12.2p3's full-expression group.

**Pointer to member is outside the PA15 lowering subset**, so a conversion
function to one is refused at its use rather than at its declaration.

**`S::~S() = default;` written outside the class is not parsed at all**, which
is what `spec/200-out-of-class-defaulted-special-members` waits on. 8.4.2's
out-of-class defaulted definition is a declaration form, not a conversion
question, so it is named in the failure map rather than fixed here.

**12.8p31 at a member or a base subobject.** `construct_object` refuses the
elision wherever the object is a subobject - the `!member` gate - so
`B::B() : m(f()) {}` writes a temporary and a copy where the reference builds
the returned object in the member. 12.8p31 does not condition on placement; the
gate is there because the resolved tree has no shape for a subobject
initialized by a bare initializer, only for one initialized by a
constructor-action. Giving it one is the fix, and it belongs with the
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
