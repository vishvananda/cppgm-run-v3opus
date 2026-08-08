# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C8, reviewed at `bc50cabb`.** The architecture is the right one and the
checkpoint's own sentence names it: 13.3.3.1.5p1's argument is not an
expression, so it travels as the list with the line it will be written on
holding its place, `match_list` answers what it converts to from the parameter
alone, and the clauses are read once - where 8.5.4 has a type to read them for.
That is one reading of one list, and it is what makes 2^20 leaves cost 27 lines.

What the review found is that **the list a class is built from had two owners
and they answered 8.5.1 differently.** `aggregate_from_list` writes the
subobjects where they stand and walks 8.5.1p11's elided braces; the constructor
`member_constructor` gives an aggregate takes one clause per member and walks
nothing. A declaration reaches the first and a prvalue reaches the second, and
C8 pointed every braced *argument* at the second. Five of the six defects below
are that seam or the parameter list on the far side of it, and every one of them
is wrong output or a wrong refusal on a shape no fixture covers.

**1. 8.5.1p6's capacity counted a subobject that holds nothing as no clause.**
`clause_capacity` summed each member's own capacity, so `struct Empty {}` added
zero and `struct Holder { Empty tag; int value; }` had capacity 1. 8.5.1p11 says
the braces around a subobject *may* be left out - which means they may equally
be written, and a written `{}` is one clause however little it holds. So
`take({{}, 7})` and `take({{}})` were refused where the reference binary and
`g++` both accept them. `clauses_a_subobject_takes` is now the one question a
member and an array element both ask, and it is the subobject's own capacity or
the one clause its braces are, never nothing.

**2. 8.5.1p11's elided braces reached the subobject walk and never the
parameter list.** `construct_from_members` gave `initialize` one clause per
parameter, so `Outer{Pair pair; int z;}` built from `{1, 2, 3}` handed `1` to a
parameter of type `Pair` and refused it. `Outer o = {1, 2, 3}` had been right
since C1, because that is the other owner. The fix is `construct_from_clauses`,
which takes the same `Clauses` cursor the subobject walk takes: a member whose
braces were left out is one object of its class, built where the parameter
carrying it stands by the constructor 8.5.1 gives *that* class, out of the run
of clauses its own subobjects take. That is the shape the reference writes -
`Pair::Pair(%t2, 1, 2)` into an `argobj` slot, then `Outer::Outer(%t1, $argobj,
3)` - and it recurses, so the two-deep nesting the reference refuses is one more
step and not a second mechanism.

**3. The elision question had no bound, so a subaggregate that holds nothing ate
a clause.** `aggregate_subobject` read "the clause does not initialize the whole
subaggregate" as "its braces were left out", which for `struct Empty {}` is a
subaggregate with no subobject for the clause to reach: `Holder h = {7}` over
`{Empty, int}` was accepted, with `7` walking past `tag` into `value`, where
`g++` says the initializer for `Empty` must be brace-enclosed and the reference
refuses the call. `elides_its_braces` is now the one place that question is
asked - by the walk that writes the subobject where it stands *and* by the walk
that passes it as a parameter - and it is bounded by `clause_capacity`: a
subaggregate that takes no clause has no braces to leave out.

**4. 8.5.1p2's constructor left out a reference member and a bit-field
member.** `member_constructor` returned null for a class holding either, which
left every prvalue of it - a `T{...}`, a braced argument, an element of an array
of it - with no constructor the clauses could reach and a refusal that said no
declaration accepted the arguments. Neither is a member 8.3.5p5 has anything to
say about: a reference parameter is the address it binds and a bit-field's
parameter is its underlying type, which is what the reference writes and what
`adjust_parameter` already gives. **This is what
`general/300-refmember-copy-constructor-binds-storage` was waiting on** - the
one fixture this review turns, 209 -> 210.

**5. 8.5.1p15's union got a parameter per member and wrote each of them into the
one storage a union is.** `union Held { In in; long long raw; };` was given
`Held(Held*, In, long long)`, whose body copied the `In` into the union and then
stored a zero over it. `clause_capacity` and `aggregate_members` both stop at a
union's first member; the constructor and the definition that writes it now stop
there too, and the definition stops on the parameter list rather than on a
second reading of the rule, so the two cannot drift apart again.

**6. 13.3.3.1.5p1's list was viable against an ellipsis.** An argument past the
last parameter was given `kEllipsis` and passed as it stood, so a
braced-init-list arrived at `require_complete_value` with no type and was
reported as an overloaded function name no target chose between. 13.3.3.1.5
gives a list a conversion sequence for a *parameter*, and the ellipsis is not
one; `g++` refuses the program outright. The ellipsis match is now unviable for
a list, so a candidate that does name a parameter for it is chosen instead, and
the refusal that is left says what it is.

The one shape this review leaves is the seventh, and it is recorded rather than
fixed: **an aggregate holding an array member has no by-value parameter list**,
because 8.3.5p5 adjusts an array parameter to the pointer it decays to and what
the member holds is the array. The reference declares that parameter as the
array and marks it `[pass=decay]`, which is a boundary convention this
translation writes nowhere else. `build_temporary` now says exactly that rather
than reporting that no constructor accepted the arguments.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, best
of three, against the pre-audit binary built from `bc50cabb` on the same shapes.

| axis | sizes | before | after | output |
| --- | --- | --- | --- | --- |
| n calls each passing a fully braced list to a nested aggregate | 250/500/1000/2000 | 0.01/0.02/0.05/0.10 s | 0.01/0.02/0.05/0.10 s | 10 n + 60 lines |
| n calls each passing a list whose inner braces are left out | 250/500/1000/2000 | refused | 0.01/0.03/0.05/0.10 s | 10 n + 60 lines |
| one elided list through n aggregates nested n deep | 25/50/100/200 | refused | 0.00/0.01/0.01/0.03 s | 24 n + 34 lines |
| the same, whose first clause is a 40-deep chain of calls | 25/50/100/200 | refused | 0.01/0.01/0.02/0.05 s | 24 n + 82 lines |
| `f({})` against a class whose members double at each of n levels | 8/12/16/20 | 27 lines, under 0.01 s | 27 lines, under 0.01 s | 27 lines at every n |

The elided walk is **linear in nesting depth and not exponential in it**, and
the row that says so is the fourth: the clause the descent probes is the same
clause at every level, so a first clause that is itself expensive is read once
per level and the total stays linear - 0.05 s at depth 200 with 40 calls under
the probe. `clause_capacity` is still one memoized walk per type, which is what
holds the fifth row at 27 lines for 2^20 leaves after the capacity of a
subobject changed. The first row is the shape both binaries already built, and
it is at parity.

`valgrind` is clean over the depth-200 elided nesting, the 2000-call
multiplicity, the union, the reference member and the 40-call probe.

The reference binary was the differential oracle over the 37 probes of this
review, run through the harness's own relaxed comparison, with the checked-in
`.ref` files first and `g++` as the third oracle wherever the binary and we
disagreed - which is how findings 1, 3 and 6 were settled. 25 of the 37 now
agree exactly; the rest are named under Open Gaps.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at **210 / 228**.

## Open Gaps

**The reference binary and the checked-in `.ref` files disagree about 12.4p8's
empty destructor, and we follow the files.** For `struct T { ~T() {} };` the
binary writes a call of `~T` at every end of a lifetime and the checked-in
outputs of the eleven fixtures that declare one write none, which is
`vacuous_destruction`'s own rule. `g++` writes the call. This is the one place
where the binary is not the oracle the fixtures are, and a differential sweep
run against the binary alone reads it as a defect in every program that declares
such a destructor. It is durable: every later sweep has to discount it.

**An aggregate holding an array member has no by-value parameter list**, so a
prvalue of one - `Holder{{1, 2}, 3}`, a braced argument of that type, an element
of an array of it - is refused. The reference declares the parameter as the
array and writes `ptr [pass=decay]` for it, with the member copied through the
address in the constructor's body; matching that means a function type holding
an unadjusted array parameter, a `describe_parameter` case for it, a `ptr` slot
for the name, and an array temporary at the call. It is the one shape of 8.5.4
this milestone leaves and it is a checkpoint's worth of work, not an audit's.

**8.3.5p5's array parameter is an array to the body and a pointer to the
boundary.** `int take(int cells[2])` gets `slot $cells : ptr` and then reads the
name as an array - `addr $cells`, `unary decay ptr` - where the reference loads
the pointer. The entity keeps the declared type and only the function type is
adjusted, so the two disagree. It predates this milestone and is the same
8.3.5p5 the gap above turns on.

**The reference passes a class holding a bit-field by address.** For
`struct Holder { int a : 3; int b; }` it writes `ptr [pass=by_address]` where we
write `obj<8x4>`; `g++` passes it in `%rdi` as a value, so `g++` and we agree
against the binary. This is 5.2.2p4's `carried_by_bytes` and not 8.5.4's.

**12.8p31 does not reach through a parenthesized single argument.** A
mem-initializer written `pair({1, 2})` or `pair(Pair{1, 2})` builds a temporary
and copies it into the member where the reference constructs the member in
place. `read_initializer` finds 12.8p31's elision only where the whole
initializer is `T(...)`; where the prvalue is one *argument* of a
direct-initialization, 13.3 chooses the copy constructor and nothing takes the
copy back out. The braced spelling is new with C8 and the parenthesized one is
older; both want the same fix, and it means choosing the constructor before the
`constructor-action` names the subobject.

**We are ahead of the reference on four shapes, and `g++` agrees with us**: a
braced list two aggregates deep with both sets of braces left out, `new Out{1,
2, 3}`, `Out({1, 2, 3})`, and the empty subaggregate member of finding 1. The
reference refuses each; `g++` accepts each.

**The reference builds an empty subaggregate member it has no clause for.** For
`Holder{Inner inner; int value;}` with `Inner{Empty tag;}` and `{{}, 7}`, the
reference builds an `Empty` temporary and passes it to `Inner`'s member
constructor; we value-initialize `Inner` with its default constructor and write
one call fewer. Both end with the same object.

**A braced-init-list passed to an ellipsis is refused.** `g++` refuses it too;
the reference builds an array temporary and decays it, which is `initializer_list`
machinery this milestone has no other trace of.

**Outside this milestone**, unchanged: 10.3's virtual dispatch, which the README
puts after it; class templates; 13.5.6's overloaded `operator->`; pointer to
member in the PA15 lowering subset; 9.2p1's refusal of a member declared twice
in one class; and 5.5's `.*`.

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
| C8 | 8.5.4's braced-init-list of a class, and 13.3.3.1.5's sequence for an argument that is one | `bc50cabb` | 6 / 6 + 1 recorded refusal: 8.5.1p6's capacity counted a subobject that holds nothing as no clause, so `f({{}, 7})` was refused; 8.5.1p11's elided braces reached the walk that writes a subobject where it stands and never the by-value parameter list a prvalue is built by, so `f({1, 2, 3})` into `{Pair, int}` was refused and so were `Outer{1, 2, 3}` and an element of an array of one; that elision question had no bound, so `Holder h = {7}` over `{Empty, int}` walked a clause past a subaggregate with nothing to take it, which `g++` and the reference both refuse; 8.5.1p2's synthesized constructor left out a reference member and a bit-field member, so every prvalue of such an aggregate was refused; 8.5.1p15's union got a parameter per member and its definition wrote each of them into the one storage a union is; and 13.3.3.1.5p1's list was a viable ellipsis argument that then reached `require_complete_value` with no type. The refusal: an aggregate holding an array member has no by-value parameter list, because 8.3.5p5 adjusts the parameter to a pointer and the member is the array | 209 -> **210 / 228**; pa1-pa16 1494 / 1494; file audit passes |
