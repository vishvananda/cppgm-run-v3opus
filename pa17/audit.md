# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C6, reviewed at `67babaa8`.** The architecture holds and is the right one.
12.2p3's boundary belongs to the analysis and 15.2p2's handler to the lowering,
and they meet at one fact of a node: `Fact::object` names the object a prvalue
of class type *is*, `Fact::destruction` names what ending it comes to, and every
reader of that prvalue - a member access, an argument bound to a reference, the
end of the full-expression, the handler - reaches the one object through the one
address. A region over the code between two changes of the standing set is the
right shape for the handler too, and 12.6.2p10's chain is what keeps n standing
objects at n destructions rather than n(n+1)/2.

What the review found is eight defects, and they are three patterns. Three are
the first: **a handler owed a set of objects that was not the set standing where
it was named** - the region stood around 3.8p1's own ends, a handler block was
named again for objects it no longer described, and a temporary was destroyed on
a path that never created it. Two are the second: **an order the oracle fixes
was decided by whoever asked first** - the blocks a condition's edges are
numbered against, and the for-init-statement's. Two are the third: **a walk the
checkpoint's own model calls constant is not** - the standing list was copied
once per region, and the file the machinery grew went past the audit's limit
with one of its constants defined twice. The first pattern is the one that
matters: each of the three writes a destructor call for an object that is not
there, which no fixture failed on and which `valgrind` cannot see because it is
the *emitted program* that would run it.

**1. 15.2p2's handler stood around 3.8p1's ends of the objects a block
declared.** A `return` writes 12.2p3's ends of its operand's temporaries and
then 6.6p2's ends of every block it leaves, and the lowering wrote both inside
the open region and closed it afterwards - so the call that destroys `b` stood
under a handler that destroys `b` and `a`, and a destructor that throws would
have destroyed them twice. The reference closes the region in front of those
ends, and the whole rest of the block - down to the block numbering - already
matched. The two ends are not one question: 12.2p3's belongs to the
full-expression the handler covers and 3.8p1's is reached on the one path that
leaves, so `SemaFact::full_expression_end` is what tells them apart, written by
`close_full_expression` and read by `leave_blocks` and by `statement`. A
temporary 12.2p5 moved into a block is 3.8p1's end and gets the same answer.

**2. A handler block was named again for a set of objects it no longer
described.** `dispatch_cache_` holds the block already written for each number
of standing objects, and `end_object_lifetime` drops the entries past the one
that ended - but `close_unwind_region` then put the closing region's entry back
under its *own* count, after that end. Two statements were enough:
`use(T(1)); use(T(2));` named the first temporary's handler for the second, so
an exception out of `use(T(2))` destroyed the object the program had already
destroyed and left the one standing. The same block came back across an
if/else, where it destroyed the other arm's temporary. A handler is a good name
for a later step only while the objects it owes still stand, so a region a
lifetime ended inside is no longer cached at all - which is `region.ended` asked
one more time, and costs the reuse nothing where nothing ended.

**3. A temporary an operand created was destroyed on the paths that never
created it.** `sema_analyzer.h` says of the frames that "an operand that may not
run is one whose temporary may not exist, so what it created is ended where it
ends rather than where the whole expression does" - and no operand had a frame.
`if (use(T(1)) && use(T(2)))` destroyed the second temporary on the
short-circuit edge, where its constructor had never run and the temporary
naming it was not even defined; `c ? use(T(1)) : use(T(2))` destroyed both at
the end of the conditional. 5.14p1 and 5.16p1's operands now each take a frame,
`take_full_expression` hands it back, and what it holds is either ended where
that operand ends or - where 13.5.7 made the operator a call, and a call
evaluates every argument - handed back to the enclosing full-expression. An arm
writes its ends under a node of its own, so the lowering has one place to write
them and it is the arm's own block. This is what the failure map called the
group's first half: both fixtures now agree with their reference everywhere
except 8.5p8's zero.

**4. The region kept a copy of the whole standing list, once per region.** The
performance model says the list "is kept once, by the first end of a lifetime
that falls inside a region" - and that is once *per region*, so a body with n
objects standing while n temporaries are made and ended copied n entries n
times. n locals followed by n temporary statements was 0.03/0.08/0.27/1.13 s at
250/500/1000/2000 with output linear in n at 34 n lines, which is the shape a
suite cannot see. What a close needs is the objects that stood at the open, so
the one entry each end takes out of the list is kept with the place it stood at
and the list is rebuilt only in the branch that is about to write one
destruction per object anyway. The same sizes are now 0.03/0.05/0.10/0.19 s and
the output is byte-identical.

**5. 8.5.3p5's name for the storage of a discarded prvalue.** `T(1);` as a
statement named its slot `tmpobj` where the reference names it `discard`, which
is what the same storage is named when a *call* hands the object back. What
asked for the object is the statement throwing its value away either way, so
`register_discarded_object` writes the name where it registers the lifetime.

**6. 12.2p3's edges out of a condition were numbered before the region closed.**
The two blocks that destroy a condition's temporaries were reserved before the
branch, and the block the region goes on in was reserved inside it - so the
three came out in the reverse of the reference's order and every `if`, `while`
and `do` whose condition holds a temporary differed by nothing but its block
numbers. The handler comes off where the value is taken, which is before the
edges out of it are named at all.

**7. 6.5.3p1's for-init-statement was lowered after the loop's own blocks were
numbered.** The four loop labels were reserved first, so an init-statement that
opened a block of its own - which after C6 is any that holds a temporary -
numbered it after the loop it runs before.

**8. The file the handler machinery grew went past the audit's size limit, and
one of its constants had two definitions.** `lowir_lower_object.cpp` reached
3029 lines. 15.2p2's handler in a body is its own question - what an exception
thrown while objects stand has to end - and it is now `lowir_lower_unwind.cpp`,
which leaves the object model at 2581 lines; `kUnwindSuffixLimit` was defined in
both files and is now one declaration in `lowir_lower.h`, read by the
destructor's own suffix and by the handler chain, which are the same chain.
Beside them, C6's constructor initialized `ended_lifetimes_` out of declaration
order, which is the one `-Wreorder` the tree had.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host,
best of three, at the end of the review.

| axis | sizes | time | output |
| --- | --- | --- | --- |
| n statements each creating and ending a temporary | 250/500/1000/2000 | 0.02/0.03/0.05/0.10 s | 19 n lines |
| n temporaries in one full-expression | 250/500/1000/2000 | 0.02/0.03/0.05/0.10 s | 18 n lines |
| n standing objects, each with a call after it | 250/500/1000/2000 | 0.02/0.03/0.07/0.14 s | 22 n lines |
| n reference-bound temporaries, each used | 250/500/1000/2000 | 0.02/0.04/0.07/0.14 s | 24 n lines |
| **n standing objects, then n temporaries made and ended under them** | 250/500/1000/2000 | **0.02/0.03/0.05/0.09 s** | 34 n lines |
| the same, before finding 4 | 250/500/1000/2000 | 0.03/0.08/0.27/**1.13 s** | 34 n lines |
| n conditionals each holding a temporary in each arm | 250/500/1000/2000 | 0.03/0.06/0.12/0.23 s | 53 n lines |
| n `if` conditions each holding a temporary | 250/500/1000/2000 | 0.02/0.04/0.08/0.14 s | 35 n lines |
| conditional arms nested n deep, each with a temporary | 50/100/200/400 | 0.01/0.01/0.02/0.03 s | 33 n lines |
| `&&` right operands nested n deep, each with a temporary | 50/100/200/400 | 0.01/0.01/0.02/0.03 s | 33 n lines |
| blocks nested n deep, each declaring an object | 50/100/200/400 | 0.01/0.01/0.02/0.04 s | 23 n lines |
| calls nested n deep, each with a temporary argument | 50/100/200 | 0.01/0.01/0.01 s | 9 n lines |

Row five against row six is finding 4, and it is the only quadratic the review
found: the output is the same 34 n lines at every size either way, so nothing in
the suite or in the emitted program could have said so. Rows nine to eleven are
the depth sweep - 12.2p3's frames, 15.2p2's regions and 5.16p1's arms are all
linear in nesting depth and not exponential in it. Past 340 nested calls the
parser refuses the unit; `pa17/cppgm++-ref` segfaults there.

`valgrind` is clean over all 110 lowering probes, over the depth-100 nestings of
each of the three shapes above, and over 250 standing objects with 250
temporaries made under them.

The reference was used as a differential oracle over those 110 probes, run
through the harness's own relaxed comparison, and `g++` and the checked-in
`.ref` files were the third oracle on every verdict the two disagreed about. The
attributes the comparison strips - `object=`, `binding=`, `pass=`, `unwind=`,
`role=`, `effects=` - were diffed separately over the same corpus, and the four
probes that differ there are all named under Open Gaps or belong to C7's
placement group. Every probe that fails now is one Open Gaps names.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at 194 / 228 with the failure set
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
declares such a destructor.

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
neither.

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
