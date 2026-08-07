# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C1, reviewed at `c2894e79`.** The architecture holds and is the right one.
12.8's four members are four facts of the class, settled where 9.2p2 completes
it and read through `SemaEntity::transfers` rather than searched for; the
definition is one walk of the same subobject list 12.6.2p10 and 12.4p8 already
walk; the lowering reads `SemaEntity::transfer` and a typed `storage-transfer`
node rather than recognising a shape. Nothing in the checkpoint is a second
pipeline, no scan is repeated, and no source text is re-read. Against a class
with n trivially copied members the output is 29 lines at every n up to 2000,
which is the point of the leading prefix.

What the review found is six defects, all in one place each owner answered a
question that belonged to another. Two produced output no linker could take or
no standard allows, one refused a program the milestone supports, one turned a
transfer into nothing at all, one was quadratic in the depth of an inheritance
chain, and one differed from the reference at every site.

**1. A lookup for `operator=` read another class's declaration as this one's.**
`note_transfers` and `declare_transfer_member` asked 3.4 for the name, and 10.2
answers it from a base while 3.4.1 answers it from an enclosing class. A class
that declared none of its own therefore found one that another class owns, and
did two things to it. It cleared that declaration's `transfer`, which is what
`demand_transfer_definition` reads - so `struct B { int x; }; struct D : B {};`
emitted `call ptr @B__operator_` against a `declare function` with no definition
anywhere in the program, and it did so for `B` alone as soon as any class in the
unit derived from it. And it chained this class's implicit `operator=` onto the
found class's overload list, which put one class's member in another class's
name binding and made a chain of n classes O(n²): a bare 2000-deep inheritance
chain took 0.91 s against the 0.05 s it took before the checkpoint. 12.8 asks
about the declarations of one class, so what answers it is that class's own
region, which is what `own_assignments` now is.

**2. 11.4p1's protected member of a base was one no derived class may name.**
The access test walked the enclosing scopes and stopped, so a base that made its
copy or move member protected deleted the corresponding member of every class
deriving from it: `struct B { protected: B(const B&) = default; }; struct D : B
{}; D t(s);` was refused where g++ accepts it. 12.8p11 asks the question of the
base subobject of the class asking, so the derivation the class already holds is
the rest of the answer.

**3. 12.8p11's deleted copy was bypassed wherever an object crossed a boundary.**
"Is a copy of an object of this class the copy of its bytes" had two answers: the
layout's, built from the declarations the class holds, and `settle_transfers`'.
The layout's did not know about deletion - `declares_copy_constructor` skips a
declaration that is `= delete` - so a class the program said may not be copied
was passed by value, returned by value and copy-initialized as a raw `copyobj`
with `EXIT_SUCCESS`. Five shapes were checked and g++ refuses all five; this unit
refused only the one that reached `construct_object`. Both halves of 12.8p11 and
p12 are now settled once, where the class's copy constructor is settled, and the
layout, 5.2.2p4's argument and the lowering's copy of an object all read there.

**4. A trivial transfer could come to nothing at all.** 12.1p5's "there is
nothing for a call of this constructor to do" was answered for a copy or move
constructor too. It is true of the default constructor, whose definition writes
nothing; a value transfer with an object to read from carries that object's
bytes, which is work. Wherever the direct `copyobj` form did not apply, the
transfer was dropped and the object was left holding what its storage held.
`transfers_value` is that distinction, taken once and read by both exits.

**5. The site form of a transfer did not match the reference.** The direct
`copyobj` at a site was written whenever the chosen constructor is trivial. The
reference writes it only where an object of the class may be copied at all: a
class whose copy constructor is deleted is one the program said is carried by
the member it declared, and the initialization is the call of that member however
little its definition comes to. One step *inside* such a definition is not that
initialization - 12.8p15 chose the constructor there - and the reference writes
the bytes for it, for a base subobject and for a member alike. Confirmed against
`pa17/cppgm++-ref` on ten shapes; `subobject_step` is what tells the two apart.

**6. 12.8p15 had no form for an array member.** A member of array of class type
was handed the whole source array where the element's own transfer member takes
one element, so the definition was refused with "no declaration of M accepts the
arguments of a call". The leading storage prefix hid this for the common case
and only for it: a base subobject in front of the member, or any member needing
a call before it, was enough to reach it with an ordinary trivially copied
element type. Where the element's class carries one element by its bytes the
whole array is those bytes, which is the one form the array has and the one that
does not count a bound the source wrote as a number.

Smaller things the same sweeps turned up: a `copyobj` for a run beginning part
way into an object claimed the whole class's alignment, which is now cut down to
the one the offset allows.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | before | after |
| --- | --- | --- | --- |
| bare inheritance chain, depth n | 250/500/1000/2000 | 0.01/0.04/0.19/0.91 s | 0.01/0.03/0.08/0.14 s |
| the same chain, each class copied and assigned | 250/500/1000/2000 | 0.03/0.07/0.28/1.64 s, 39 lines | 0.03/0.06/0.12/0.27 s, 21 n lines |
| n classes, each copied and assigned | 250/500/1000/2000 | 0.09/0.16/0.32/0.61 s, 29 n lines | unchanged |
| n trivially copied members, one class | 250/500/1000/2000 | 0.00/0.00/0.01/0.02 s, **29 lines at every n** | unchanged |
| n members each needing a call | 250/500/1000/2000 | 0.02/0.03/0.07/0.14 s, 10 n lines | unchanged |
| base subobject and n trivial members | 250/500/1000/2000 | 0.01/0.03/0.07/0.10 s, 5 n lines | unchanged |
| n one-bit bit-fields | 250/500/1000/2000 | 0.00/0.00/0.01/0.01 s, n/32 units | unchanged |

The chain's line count rising from 39 to 21 n is the first defect being fixed:
the definitions those 39 lines called were the ones no unit wrote.

`valgrind` is clean on every shape above and on each probe below. The file audit
passes with the three recorded header-weight warnings it already had, and
pa1-pa16 stand at 1494 / 1494.

`pa17/cppgm++-ref` produces PA17 output, so it was used as a differential oracle
over 43 probes of the checkpoint's subject. 34 agree exactly after the harness's
canonicalization. The nine that do not are: four where an array of a class whose
transfer needs a call is refused here and lowered element-wise there (the gap
below), three where such an array carried by its bytes is one `copyobj` here and
one operation per element there - which is the form the reference itself writes
when the same array leads the object - one `return local;` of a class with a
deleted copy constructor, which g++ refuses with us, and one `pass=by_address`,
which is the next checkpoint.

## Open Gap

12.8p15's array member of a class whose transfer needs a call is still refused.
`general/300-synthesized-array-member-special-members` is the fixture for it and
the reference lowers it element by element, both sides indexed by the same
element. It is one step per element, which PA16's `kArrayLoopLimit` already says
is not what a large bound may be written as, so the form is the loop 12.6p1 and
12.4p8 already use - the same index reading both objects. It belongs with C2.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1 | 12.8's four value-transfer special members | `c2894e79` | 6 / 6: one class's `operator=` bound into another's and its definition never written (also O(n²) in inheritance depth), 11.4p1's protected base member read as inaccessible, 12.8p11's deleted copy bypassed at every by-value boundary, a trivial transfer lowered as nothing, the site form disagreeing with the reference, and 12.8p15 having no form for an array member | 86 -> **88 / 228**; pa1-pa16 1494 / 1494; file audit passes |
