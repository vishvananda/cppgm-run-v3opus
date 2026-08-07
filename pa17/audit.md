# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C2, reviewed at `be9d930d`.** The architecture holds and is the right one.
6.6.3p2's boundary is one fact of the type with one writer, so a declaration, a
definition, a call through a pointer and a member call cannot disagree: the ten
signature shapes probed against `pa17/cppgm++-ref` - a member function, a
`const` member, a variadic function, a call through a function pointer, a class
of exactly two words and one a byte wider, an empty class, a definition written
after its use and two objects initialized from one call - are byte-identical.
12.8p31's result object is one operand threaded down the initializer, never a
node read twice and never an instruction rewritten after it is written; the
return-slot local is one walk of the body, done once and only for a function
that returns indirectly. Nothing in the checkpoint is a second pipeline.

What the review found is seven defects, five of them in one shape: the
checkpoint declared that an initialization, a return, an argument, an arm and a
discarded value all reach one hand-off, and four of those five reached a
different one. Two produced a program refused that the milestone supports, one
produced silently wrong code, three wrote an object and a copy more than the
reference writes.

**1. 5.16p3's result object had no initialization from a glvalue operand.** A
conditional whose result is a prvalue of class type is an object of its own that
each operand copy-initializes, and the analysis wrote nothing for an operand
that named an object. The lowering then reached `place_class_object` with an
initializer that creates nothing and no transfer named, so its one remaining
exit was the copy of the bytes - which 12.8p1 refuses for a class whose copy is
a call the program wrote. `return use_local ? local : Value(33);` was refused
outright. The transfer is 13.3's, so it is now chosen where the operand stands,
through the same `build_temporary` a call's argument and a returned object
already use, and the lowering places what the analysis chose.

**2. 12.8p31's elision and the lowering's placement were two answers to one
question.** The analysis elided any prvalue of the object's own class; the
lowering could place a temporary, a call and a conditional. Where they disagreed
the program was lost either way: `A x = (c, f());` was elided into a form
nothing could write and refused, and a conditional was elided where the
reference writes the copy. `creates_its_object` is now that one question, asked
of the resolved node by both layers - an initializer that *creates* the object it
is worth is elided into, and one that only selects among objects is worth an
object that already stands somewhere, so 12.8p15's copy of it is a call the
program can watch run. 12.8p12's is not, and where the class carries an object
by its bytes the two are still one object, which is what keeps a POD conditional
elided the way the references write it.

**3. 3.6.2p2's own initialization reached none of the hand-off.** An object of
class type at namespace scope whose initializer 12.8p31 left standing was read
as a list of clauses over the elements of an array, which a class has none of.
`A g = f();` was refused with a bound no array wrote; `P g = q;` was worse - the
clause count of a name is zero, which the image was taken to hold, so the unit
emitted an empty `__cppgm_init` and left the object holding zero. Ten
namespace-scope shapes now agree with the reference exactly.

**4. `add_initialization` answered 12.1p5 a second time.** It skipped the whole
construction wherever the constructor is trivial - which is right for the
default constructor and wrong for a copy, exactly the defect C1's audit found in
the local path. This was its other exit. The question belongs to the
constructor call, which already asks it; what is left here is only to drop the
address named for a call that wrote nothing.

**5. A discarded conditional of class type was two objects.** It was read as
two alternatives each discarding a value, so it opened one object per arm where
5.16p3 gives it one. And the storage such an object is given was named by
whichever place in the lowering opened it - `condobj`, `tmpobj`, `argobj` -
while 8.5.3p5's name is what asked for the object, which the analysis already
writes for a temporary. `stands_in_no_storage` is now the one question, and the
name the analysis wrote is what the storage takes wherever the object came from.

**6. 12.8p12's trivial transfer gave its operand storage one copy too late.** It
read the operand as an object, so a call handing one back was given a slot and
then copied out of it: two copies where 12.8p15 asks for one, and a slot no
reference writes. `class_copy_source` is the same question `place_class_object`
already answers once - where a call handed the object back holding no storage of
its own, it *is* that object.

**7. 9p6's empty class had no byte to hand back.** Every copy of an object of a
class that holds nothing is written nothing, which is right between two objects
and wrong for the value a call returned: the ABI hands back one byte, and
putting it where the object stands is a store rather than bytes read out of
another object. `holds_class_value` tells the two apart.

Splitting the elision test out of `construct_object` took it past the file
audit's function-size limit; `read_initializer` is 8.5p15/p16's question -
which of 8.5's forms the program wrote - lifted out whole.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | time | output |
| --- | --- | --- | --- |
| n functions returning indirectly, two returns each | 250/500/1000/2000 | 0.03/0.06/0.11/0.22 s | 27 n lines |
| one function with n returns of one local | 250/500/1000/2000 | 0.02/0.02/0.04/0.07 s | 11 n lines |
| n nested calls, each returning and taking a class by value | 250/500 | 0.01/0.02 s | 3 n + 14 lines |
| conditionals of class type nested n deep | 250/500 | 0.02/0.02 s | 11 n + 19 lines |
| n conditionals, each with a glvalue arm 5.16p3 copies | 250/500/1000/2000 | 0.03/0.05/0.09/0.18 s | 24 n lines |
| one class, n class members each initialized from a call | 250/500/1000/2000 | 0.02/0.03/0.05/0.09 s | 12 n lines |

Every axis is linear in time and in output, and the two nesting axes are linear
in *depth* rather than exponential in it - the destination is one operand handed
down, so a conditional 500 deep costs 500 arms and not 2^500 of them. Above 500
the two nested axes are refused by the parse-depth budget PA10 already had, in
0.02 s and without recursing to a fault. `valgrind` is clean on every shape
above and on each probe below.

The file audit passes with the three recorded header-weight warnings it already
had, and pa1-pa16 stand at 1494 / 1494.

`pa17/cppgm++-ref` produces PA17 output, so it was used as a differential oracle
over 52 probes of the checkpoint's subject: every hand-off the placement rule
names, every signature shape, the empty class, the size boundary, source order,
multiplicity and ten namespace-scope initializations. 30 agree exactly, 8 more
agree after the harness's function-name and metadata canonicalization, and the
14 that do not are the failure-map groups below - `pass=by_address`, the
empty-destructor elision, the new-expression forms, and the two named under Open
Gaps. `g++` was the third oracle where the reference's shape is its own choice
rather than the standard's, which is how the conditional in
`300-conditional-class-cv-glvalue-copy` was settled: `decltype(c ? local :
fallback)` is `const Value&` there and not a prvalue, so our value category is
right and the reference's per-arm copy is a lowering the standard does not ask
for.

## Open Gaps

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
