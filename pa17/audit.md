# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C9, reviewed at `39fa3bf7`.** The architecture is the right one and the
checkpoint's own sentence names it: what a ctor-initializer means is a reading
of the *list* and not of one entry, so 12.6.2p6's delegation is answered before
the base and the members are walked at all, and 9.5p1's one storage is what a
union's constructor says which member stands in. Both are the same move - the
class the list is read against, rather than the entry, is what says what the
entry means - and `Placement::Delegate`, `delegates_to` and the one colouring of
the constructors that delegate are the right three pieces for it.

What the review found is that **the checkpoint answered each of its three rules
at one reader and left the siblings of that reader asking the old question.**
9.5p1's one storage reached construction and neither destruction nor the
transfer; 8.4.2p2's out-of-class `= default` reached the parse path constructors
and destructors take and not the one the assignment operators take; and
12.6.2p6's own reading of a name compared the characters of its last component
where it had to ask what the name denotes. Every one of the four below is wrong
output or a wrong refusal on a shape no fixture covers, and the reference binary
and `g++` agree against us on all four.

**1. 12.6.2p6 read the last component of a name and not the class it denotes.**
`read_mem_initializers` keys its index on `QualifiedName(id).last()`, so a base
class whose own last component is the derived class's name - `struct S : N::S`,
writing `S() : N::S(3) {}` for its base - answered `named.find("S")` and was
read as a delegation to `S`. The program was refused outright with "no
declaration of S accepts the arguments of a call", and a `S(int)` that happened
to exist would have been called on an object with no base built. A qualified
spelling is now asked what it denotes - `names_the_class`, which is the one
question 12.6.2p2's base and p6's own class both ask - before it is read as a
delegation; an unqualified one is not, because 12.6.2p2 looks it up in the scope
of the constructor's class first and this class's own injected-class-name is
what stands there. So the probe stays one probe for every ctor-initializer that
writes no nested-name-specifier, which is nearly all of them.

**2. 12.4p8's word is *non-variant*, and a union's destructor destroyed every
member it declared.** `union U { int a; D d; U() {} ~U() {} };` wrote a call of
`D::~D` on the one storage - at the end of every lifetime of a `U`, inside every
15.2p2 handler that covered one, under `delete`, and per element of a
`new U[n]`. That is a destructor call on storage no lifetime began in, and it is
the destruction half of exactly the reading C9 wrote for construction: a variant
member no constructor designated holds no object, so there is no end of one to
run. `one_storage` is now the one owner of 9.5p1 and its three destruction
readers agree - `write_member_destructions` writes no member, `vacuous_destruction`
answers what the body alone comes to, and `destruction_nonthrowing` has nothing
for 15.4p14 to allow, so the calls that fix removes take their handlers with
them. The reference's own `~U` body is empty and `g++` writes no call either.

**3. 12.8p15's transfer walked the members of a union too, and twice over the
same bytes.** The member the standard defines for `union U { int a; D d; }`
wrote the leading `copyobj` of the trivial prefix *and then* a copy call per
variant member into the same four bytes - reading bytes no lifetime wrote and
writing over the bytes the step before it had just carried. p15 and p28 give a
union one copy of its object representation and no memberwise walk at all, which
is now what `write_transfer_steps` writes before it asks for a base or a field.
This was the milestone's one output that grew with a *union's* member count: a
union with n class members was 33 n lines and is **39 lines at every size**.

**4. 8.4.2p2 reached the constructor and the destructor and not the assignment
operators.** `X& X::operator=(const X&) = default;` is the same rule written on
the other of the two paths a special member is parsed by - the ordinary
init-declarator, where a constructor and a destructor never go - and it got none
of the three answers C9 gave the first path. 7.1.2p2's non-inline definition:
we left it inline, so the symbol was `binding=weak` where the reference and
`g++` both write a strong one. 3.2p4's "this unit holds it whether or not
anything here names it": we demanded nothing, so **a unit whose only definition
was that line emitted no `_ZN1XaSERKS_` at all** and every unit that linked
against it was left with an undefined symbol. And 3.2p1's one definition: a
second `= default` for the same operator was accepted where the reference and
`g++` refuse it and where `special_member_definition` already refused the
constructor form. All three are now keyed on 3.4.3p3's qualified declarator-id,
which is the same thing "written outside the class" means on the other path.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, best
of three, against the pre-audit binary built from `39fa3bf7` on the same shapes.

| axis | sizes | before | after | output |
| --- | --- | --- | --- | --- |
| a union with n class members, copied and destroyed | 250/500/1000/2000 | 0.01/0.03/0.06/0.12 s, 33 n + 412 lines | 0.00/0.00/0.00/0.01 s | **39 lines at every size** |
| a union with n scalar members, copied, assigned and destroyed | 250/2000 | 0.00/0.01 s, 42 lines | 0.00/0.01 s | 42 lines at every size |
| n classes each defaulting ctor, dtor, copy ctor and `operator=` outside the class | 250/500/1000/2000 | 0.05/0.10/0.21/0.42 s, 52 n + 4 lines | 0.06/0.13/0.25/0.52 s | 65 n + 4 lines |
| n classes each with one delegating constructor and one use | 250/500/1000/2000 | 0.04/0.08/0.17/0.36 s | 0.04/0.08/0.17/0.37 s | 33 n + 8 lines |

The first row is the one that mattered: a union's transfer and its destruction
were each one step per *declared* member, so the output grew with a count that
says nothing about how many objects there are - at most one - and every one of
those steps but the first wrote over the bytes the step before it carried. It is
now one `copyobj` and no destruction at all, constant in the member count.
The third row is 13 n lines *more* than before and that is the defect being
fixed: the assignment-operator definition this unit owes and did not write. The
fourth says the delegation paths are untouched by any of this.

`valgrind` is clean over the 200-member unions of both shapes, the 100-class
out-of-class defaults, the 100-class delegations, and every probe of this review.

The reference binary was the differential oracle over the 49 probes of this
review, run through the harness's own relaxed comparison, with the checked-in
`.ref` files first and `g++` as the third oracle - which is how all four findings
were settled, all four the same way by both. 40 of the 49 now agree exactly - 35
byte for byte after canonicalization and 5 refusing the same program; the nine
that remain are named under Open Gaps, and five of those nine are the one
12.4p8 reading below.

The file audit passes with the three recorded header-weight warnings it already
had, pa1-pa16 stand at 1494 / 1494, and pa17 at **217 / 228**.

## Open Gaps

**The reference binary and the checked-in `.ref` files disagree about 12.4p8's
empty destructor, and we follow the files.** For `struct T { ~T() {} };` the
binary writes a call of `~T` at every end of a lifetime and the checked-in
outputs of the eleven fixtures that declare one write none, which is
`vacuous_destruction`'s own rule. `g++` writes the call. This is the one place
where the binary is not the oracle the fixtures are, and a differential sweep
run against the binary alone reads it as a defect in every program that declares
such a destructor. It is durable: every later sweep has to discount it.

C9's review widened it by one class of shape. **A union whose destructor writes
no statement is vacuous for us and not for the reference**, because 12.4p8's
`non-variant` means its class-typed variant members are not what it comes to -
so `union U { int a; D d; U() {} ~U() {} };` is passed and returned by value
here, and needs no call at any end, where the binary passes it by address and
calls a `~U` whose body it has itself written empty. The same reading reaches a
class holding such a union as a *named* member, whose own empty-bodied
destructor then comes to nothing too. The binary is inconsistent with itself
here and we are not; `g++` cannot settle it, because its rule is 12.4p5's
triviality - it calls the empty `~T` above as well - which is the rule the
fixtures already say this translation does not use.

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

**8.5p7's zero stands in front of a default constructor the standard defines,
and the reference writes none.** `Z y{}` over `struct Z { int a = 5; long long
b; }` - and equally over a union with a brace-or-equal-initializer - zeroes the
storage and then calls the constructor, because the class's default constructor
is neither user-provided nor deleted and 8.5p7 says both steps. `g++` writes the
zero too. It predates C9 and is not a union question: the same shape over a
non-union class writes the same extra store.

**An `inline` `= default` written outside the class is emitted only where
something names it.** `inline X::X() = default;` is 7.1.2's inline definition,
so 3.2p4 asks for one only in a unit that odr-uses it, and 12.1p5 leaves nothing
to odr-use; the reference and `g++` both write the weak definition anyway. The
non-inline spelling C9 landed is not affected - that one this unit owes outright
and writes. It predates C9's audit and is `owe_internal_definition`'s policy for
every definition, not a fact of 8.4.2p2.

**A constructor takes a member's address for an anonymous union member it then
writes nothing into.** `struct H { int t; union { int a; D d; }; H() {...} };`
leaves one dead `index` in `H::H`. Anonymous unions are already outside this
milestone - 12.6.2p2's mem-initializer that designates a member of one is the
recorded hole - and this is the same seam seen from the lowering; it predates
C9.

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
| C9 | 12.6.2p6's delegating constructors, 9.5's unions and 8.4.2p2's out-of-class `= default` | `39fa3bf7` | 4 / 4, every one of them a sibling of the reader the checkpoint answered at: 12.6.2p6's delegation keyed on the last component of a mem-initializer-id rather than on the class it denotes, so `struct S : N::S` writing `N::S(3)` for its base was read as a delegation and the program refused; 12.4p8's *non-variant* missed, so a union's destructor called the destructor of every class-typed member it declared - on storage no lifetime began in, at every end of a lifetime, in every 15.2p2 handler, under `delete` and per element of a `new U[n]` - and `vacuous_destruction` and `destruction_nonthrowing` answered off the same walk; 12.8p15/p28's transfer walked those members too, writing the trivial prefix's `copyobj` and then a copy call per variant member over the same bytes, which was 33 n lines in a union's member count and is now 39 at every size; and 8.4.2p2 reached only the parse path a constructor and a destructor take, so `X& X::operator=(const X&) = default;` was left inline, was never demanded - a unit holding only that line emitted no definition at all - and a second one was accepted | **217 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
