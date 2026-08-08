# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.

## Current Checkpoint Review

**C11, reviewed at `4eb9f73b`.** The architecture is the right one and it is one
reading of a walk this stage already had: the member 12.8p15 defines for an array
takes an *element*, so the transfer is written once and the array is walked
around it, with `element_walk_` the one cursor the destination and the source
both read. 12.8p28's assignment gets the same walk 12.6p1's construction had,
`FactKind::ArrayTransfer` is where it hangs, and the analysis stays O(1) in the
bound. 5.16p6's arm becoming a *reading* of the object it names is right and the
reference and `g++` agree; so is 8.5.3p4's unrelated cast to a class reference
becoming the initialization it is, where the reference binary is plainly wrong.

What the review found is that **a step written once and run more than once
answers the second run with what the first run built, and the walk of an array
is not the walk every reader of that array writes.** Two of the five write code
for an object nothing built, one crashes the compiler, one refuses a valid
program, and one leaves objects standing that an exception had to end. Three are
older holes the checkpoint's new answers reached; two are its own.

**1. 12.8p28's element walk runs a step written once, and the objects that step
created answered for the next element.** `array_transfer` and `array_lifecycle`
lower one subtree per element, and `placed_`/`slots_` are the function's - so
`creates_object` told the second element that the object its step creates
already stands. Where the element's own `operator=` takes its parameter **by
value**, `Own::operator=` over `E v[3]` was **refused** outright - "an object of
the class type struct E is copied" - at 2 through 16 elements and accepted at 1
and past 16, because past `kArrayLoopLimit` the step is lowered once. Where a
constructor has a **default argument** that creates an object, `T a[3]` built
that object once and passed it to all three elements, which 8.3.6p9 evaluates
per call; that half predates C11 in 12.6p1's own array and the checkpoint
widened it to 12.8p15's. `place_object` and `name_object` are the two writers of
where an object with no declaration to name it stands, and **one element's run
records what it placed and drops it at its end** - the same shape C10's
`probe_expression` gave a read that answers a question. Erasing what the run
added, rather than saving the maps, is what keeps n array members linear.

**2. 3.8p1's end of an array a declaration named was one call in 15.2p2's
handler and the whole walk everywhere else.** `T a[3]; use(a);` destroyed `a[0]`
alone if `use` threw and all three where the block ends, so two elements leaked
on every exceptional path; past `kArrayLoopLimit` it was worse, because
`construct_element_run` joined the standing list on a mark left over from an
earlier step - the array stood **twice**, and `a[0]` was destroyed once as the
whole array's first element and once as an object of its own. `begin_object_
lifetime` carries the elements and the stride now, so `replay_unwind` writes the
same walk `leave_blocks` does - written out below `kArrayLoopLimit`, one loop
past it - and 12.6p1's loop joins the list only where 12.6.2's initialization is
what is running. The reference destroys every element here and `g++` does too.

**3. `new T[n]`'s own cleanup owed the elements and 5.3.4p18's storage and not
the objects standing around it.** An exception out of an element's constructor
ran the built elements' destructors, gave the storage back and resumed - leaving
every object the enclosing block had declared standing. `construct_element_run`
already replayed the standing list for 12.6p1's array; `construct_array_new_run`
did not, and the two are one rule. With it the `new T[20]` probe goes from 44
lines of disagreement with the reference to 4.

**4. 8.5.3p4's cast to a class reference wrote an end of a lifetime for an
object nothing was asked to build, and that crashed the compiler.** Where the
conversion is 12.3.2p2's conversion function of the operand's own class, the
checkpoint applied it and then spelled the *call* as the lvalue the cast is - so
`stands_in_no_storage` no longer saw a prvalue of class type, nothing gave the
object storage, and 12.2p3's end of its lifetime named an object with none:
`use((const W&)s)` **segfaulted**, one function long. Which node stands for the
object is what says who names its storage: 13.3.3.1.2's converting constructor
built it *where the operand stood*, so the cast lifts to that node and the
temporary keeps the name `build_temporary` gave it, which a fixture pins; a
conversion function hands it back as the prvalue of a call, which goes on being
one under the cast's own line and is named `refcall` there. `g++` writes exactly
that call, that binding and that end.

**5. 8.5.3p5's bound was missed, so a non-const lvalue reference bound a
temporary.** The conversion the cast applies is 5.2.9p4's `T t(e);`, and only a
reference that may bind a temporary - an rvalue reference, or an lvalue
reference to a non-volatile const - is that initialization. `(W&)i` applied the
converting constructor and bound `W&` to the temporary it made, where `g++`
warns and reinterprets and the binary reinterprets; and `static_cast<W&>(i)`,
which `g++` and the binary both refuse, was accepted. Both are now the two
readings 5.4p4 orders: the initialization where the reference binds a temporary,
the reinterpretation of the operand's own storage where a C-style cast has one to
fall to, and a refusal where a named `static_cast` has none.

`sema_expression.cpp` went past the audit's 3000-line limit with this, and the
seam is its own: 5.2.9/5.2.11/5.4's cast is one question with two unrelated
answers - a direct-initialization of a temporary of the target type, and 8.5.3's
binding - so `sema_cast.cpp` owns it, the way C6 split 15.2p2's handler out of
`lowir_lower_object.cpp`.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, best
of three, against the binary built from `4eb9f73b` - the checkpoint as it landed
- on the same shapes.

| axis | sizes | before | after | output |
| --- | --- | --- | --- | --- |
| n array members of two elements whose element's `operator=` takes a by-value parameter, copied and assigned | 250/500/1000/2000 | **refused at every size** | 0.10/0.19/0.38/0.74 s | 170 n lines, one argument object per element |
| n local arrays of four elements, each standing across a call | 250/500/1000/2000 | 0.042/0.075/0.141/0.271 s, 55 n lines, **one of the four elements ended in the handler** | 0.052/0.089/0.163/0.321 s | 65 n lines, four of four |
| n local arrays of three elements whose constructor has a default argument | 250/500/1000/2000 | 0.042/0.074/0.137/0.267 s, 49 n lines, **the default argument evaluated once for three elements** | 0.050/0.086/0.170/0.314 s | 68 n lines, once per element |
| n casts `(const W&)s` through a conversion function | 250/500/1000/2000 | **segfault at every size** | 0.022/0.035/0.061/0.111 s | 19 n lines |
| n `new T[20]`/`delete[]` pairs with an object standing | 250/500/1000/2000 | 0.052/0.095/0.185/0.354 s, 94 n lines, **the standing object not ended when an element's constructor throws** | 0.053/0.096/0.189/0.361 s | 96 n lines |
| one array member of n elements, all four transfers | 250/500/1000/2000 | 0.011 s, 413 lines | unchanged | **413 lines at every size** |
| n array members of two elements, all four transfers | 250/500/1000/2000 | 0.14/0.27/0.53/1.00 s, 236 n lines | unchanged | linear in the member count |
| an array member of a class whose element has an array member, n levels deep | 4/6/8/10 | 168 n lines, 0.013/0.013/0.014/0.015 s | unchanged | linear in the depth, not exponential |

The first row is a valid program refused, and refused only between 2 and 16
elements - the one range where the step is written out rather than looped, which
is what said the collision is the re-lowering and not the walk. The three rows
under it are lines the output was *missing*: the calls 8.3.6p9 asks for, and the
ends of lifetime 15.2p2 owes. The last three rows are the checkpoint's own
invariants, and they are untouched: dropping what one element's run placed costs
that run's own objects and nothing else, so the walk stays O(1) in the bound and
linear in the member count and in the nesting.

`valgrind` is clean over all 63 probes of this review and over the 60-fold
multiplicity and depth-8 nesting shapes.

The reference binary was the differential oracle over those probes, with `g++`
as the third oracle and the checked-in `.ref` files above both. It is what
settled findings 2 and 3 (it ends every element, and it ends the standing
objects), and `g++` is what settled 4 and 5, where the binary is wrong in a
different direction from the code the checkpoint wrote. A worktree at `4eb9f73b`
and one at `da4e459b` are what told the checkpoint's own defects (4 and 5, and
the refusal in 1) from the older holes its new answers reached (2, 3, and 1's
default argument).

`300-synthesized-array-member-byvalue-element-assignment` is the regression test
finding 1 leaves: the reference binary reproduces it exactly, which is what makes
its `.ref` an oracle rather than a copy of our own output. Findings 2 through 5
have no fixture, because the reference's answer for each of those shapes is one
it does not agree with us on - adding a `.ref` for them would break the property
the whole stage leans on, that the binary reproduces every checked-in fixture.

The file audit passes with the three recorded header-weight warnings, pa1-pa16
stand at 1494 / 1494, and pa17 at **227 / 230**. The reference binary still
reproduces all 208 comparable fixtures exactly.

## Open Gaps

**A reference to a type that is not a class binds the operand's own storage
where it should bind a temporary holding the conversion of it.** `(const int&)d`
over a `double` passes the address of the `double`, so the callee reads four
bytes of another type's representation; `(const int&)wide` over a `long long`
does the same. 5.4p4 orders `static_cast` first and `const int& t(e);` is well
formed, so the reading is 8.5.3p5's temporary: `g++` truncates into a slot of its
own and passes its address, and an argument written `use((int)wide)` already gets
exactly that (`refarg`) from us. The binary is wrong in its own direction - it
converts the value and then passes the *value* where a pointer is wanted. C11
made this reading right for a class referenced type and recorded the non-class
one as left alone; what it needs is a fact of the node saying which of 5.4p4's
readings a cast is, because the lowering cannot tell an 8.5.3p5 binding from a
`reinterpret_cast` from the types alone. It is a checkpoint's work and not an
audit's, and it is silent wrong code until then.

**A transfer into 6.6.3p2's returned object opens no 15.2p2 region, so an
automatic object standing at a `return` is not ended if the copy throws.**
`V ret(V& r) { T t; return r; }` writes no handler for `t`. This is the rule
C11 landed and `400-conditional-prvalue-member-temporary-lifetime` pins it: the
binary writes no handler there either, and writes one only where a temporary's
own end already needed the region. `g++` writes the cleanup. It is the fixtures'
reading and not a judgment left open, so it is recorded rather than changed.

**The elements a local array has already built are no standing objects while the
rest of it is being built.** `T a[3]` writes no handler between the elements, so
an exception out of the second element's constructor leaves the first standing.
The reference writes none either, at every size; `g++` does. It is one step
further out than finding 2 of this review, which was about the array once it
stands.

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

C10's review widened it once more, at the ABI. **A destructor whose empty body
is written *outside* the class is vacuous for us and by-address for the
reference**: for `struct T { ~T(); }; T::~T() {}` the binary writes
`ptr [pass=by_address]` where the identical class writing `~T() {}` inside gets
`obj<4x4>` from it, and `K::~K() = default;` is the same shape. Neither of those
is 12.4p5's triviality, which would carry both by address - `g++` does, and is
where the two of us part - so the binary is inconsistent with itself again and
the rule the fixtures pin is the one we keep: 12.4p8 reads a body, and where the
body is written is not what it says.

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

**12.8p31's elision reaches every exit but the subobject ones.** C10's
`elide_transfer` is the machinery this gap wanted - the elision asked once,
after 13.3 has chosen - and it is asked for a declaration, an argument, a
returned object and 5.3.4p12's storage and not for a member or a base. So a
mem-initializer written `m(V(3))`, `pair({1, 2})` or `pair(Pair{1, 2})` builds a
temporary and copies it into the member where the reference constructs the
member in place, and `D() : V(V(3)) {}` does the same for a base. What stops it
is structural rather than a missed reader: `elide_transfer` puts the creating
node where the `constructor-action` stood, and for a subobject that action is
what names the member's address - so the elision has to choose the constructor
before the action names the subobject, which is a checkpoint's work and not an
audit's. It costs one temporary and one call per such mem-initializer and no
wrong output.

**We are ahead of the reference on four shapes, and `g++` agrees with us**: a
braced list two aggregates deep with both sets of braces left out, `new Out{1,
2, 3}`, `Out({1, 2, 3})`, and an empty subaggregate member. The reference
refuses each; `g++` accepts each.

**We elide through 5.2.9p4's cast and the reference does not.** `V v = (V)V(3)`,
`sink((V)V(3))`, `return (V)V(3)` and `const V& r = (V)V(3)` each build one
object here where the binary builds a temporary, copies it and destroys it -
`el7` is 26 lines against its 48. 12.8p31 names the cast's own temporary as the
object being initialized and C7 settled that reading against the fixtures, so
this is the elision working and not a shape left out. Finding 2 above is what
made it sound.

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

**Two shapes this review found that we refuse and the reference accepts.**
`V v = (V)h();`, a cast of a call's prvalue to its own class, reaches
"an object of the class type struct V is copied, which 12.8p1 makes a call of
the copy constructor its program wrote and this milestone does not write":
`creates_its_object` reads 5.2.9p4's cast only where a `temporary-object` stands
under it, and here a `call` does. And a `try` block in an ordinary body is
"a statement outside the PA12 subset", so `try { return h(); } catch (V e) {
return e; }` is refused; the README lists no handler syntax among this
milestone's own, and 12.8p32's exclusion of a catch-clause parameter has nothing
to be asked of until it parses. Both predate C10.

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
| C10 | 12.8p31's remaining placement, and which of two questions an end of a lifetime asks | `ae1e4567` | 5 / 5, every one of them an object the checkpoint gave an end or a place to that something else was already holding: a read that answers a question - 8.5.1p11's probe of a clause, 5.3.3p1's and 7.1.6.2p4's unevaluated operands - left 12.2p1's temporary standing in the program's full-expression, so `K k = {D(), 7}` over a `D` with a destructor wrote an end for an object the lowering was never asked to give storage to and **crashed the compiler**; 12.8p31's elision left the object it elided in 12.2p3's frame wherever 5.2.9p4's cast stood over the prvalue, so `V v = (V)V(3)` destroyed `v` before its first read, `return (V)V(3)` handed the caller a destroyed object, `new V((V)V(3))` returned a pointer to one, and `sink((V)V(3))` destroyed the argument object across 5.2.2p4's boundary the callee owes; 12.4p3's new end was re-derived by `destructor_call` against 12.4p5's triviality, so a temporary of a class with a declared-but-trivial destructor was destroyed only when something threw, and `register_temporary` never noted the entry, so an end only 15.2p2 reaches named the base-object destructor of a complete object and left the symbol undefined; 5.16p3's selection was asked below the hand-off as well as at it, so a cast between the destination and the conditional distributed the transfer over both arms; and 12.8p32 read a reference name as p31's automatic object, so `V g(V& r) { return r; }` moved where the reference and `g++` copy | **222 / 228** unchanged; pa1-pa16 1494 / 1494; file audit passes |
| C11 | 12.8p15/p28's array member, and the two readings a conditional arm makes | `4eb9f73b` | 5 / 5, three of them one rule - a walk of an array is the same walk for every reader of it, and a step written once is not one run twice: 12.8p28's element walk re-lowered one step per element while `placed_`/`slots_` were the function's, so `creates_object` handed the second element the object the first built - an element's `operator=` taking its parameter by value **refused** a valid program at 2 through 16 elements and was accepted at 1 and past 16, and a constructor's default argument was evaluated once for every element where 8.3.6p9 evaluates it per call; 3.8p1's end of an array a declaration named was one call in 15.2p2's handler where the block's end walks every element, so `T a[3]` leaked two of three on every exceptional path and past `kArrayLoopLimit` stood in the list twice and destroyed its first element twice; `new T[n]`'s own cleanup owed the elements and 5.3.4p18's storage and not the objects standing around it, so an exception out of an element's constructor unwound past a block without ending anything it had declared; 8.5.3p4's cast to a class reference spelled 12.3.2p2's conversion function as the lvalue the cast is, so nothing asked for the object's storage and 12.2p3's end named an object nothing built - `use((const W&)s)` **crashed the compiler**; and 8.5.3p5's bound was missed, so `(W&)i` bound a non-const lvalue reference to a temporary and `static_cast<W&>(i)` was accepted where `g++` and the binary both refuse it. `place_object`/`name_object` and one element's run, `array_entry` on the standing entry, and `sema_cast.cpp` at 5.4p4's own seam are what came out of it | 226 / 229 -> **227 / 230**, one of them the regression test finding 1 leaves; pa1-pa16 1494 / 1494; file audit passes |
