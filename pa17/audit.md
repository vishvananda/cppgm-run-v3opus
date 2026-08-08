# PA17 Audit — `cppgm++ --emit-lowir` value semantics

A review of each landed checkpoint, in the order a fact travels: declare, settle,
define, lower.  The last section of it is the stage's own review, run over the
whole implementation rather than over the checkpoint that landed last.

## Final Stage Review

**PA17 reviewed at `5ba05933`, the commit the whole suite passing left.** The
architecture is the one the README asks for and it is the PA16 object model
given value semantics rather than a second pipeline beside it: every fact this
milestone adds is hung on the owner that already answers the question it belongs
to - `sema_class.cpp` for what a class is, `type_model` for what an object of it
is carried by, `sema_lifetime.cpp` for what beginning and ending one comes to,
`sema_overload.cpp` for 13.3, `sema_allocation.cpp` for 5.3.4/5.3.5's storage,
`sema_cast.cpp` for 5.2.9/5.2.11/5.4, and `lowir_lower*.cpp` reading only the
resolved tree.  Twelve checkpoints and eleven checkpoint audits each closed a
seam; what this review looked for is the seams no *one* checkpoint owns.

C12 is the one checkpoint with no review of its own, so its four facts were
swept first: `Fact::boundary_object` at every place a temporary is built - a
declaration, an argument, a returned object, 5.2.9p4's cast, a reference
binding, 5.3.4p12's storage and a subobject step, which agree with the reference
at every one; `direct_initialized_` at a direct-initialization, a
copy-initialization, a two-argument call, a default argument's own
initialization and a constructor of another class, which agree with `g++` at
every one; `surrogate_calls` beside 13.5's other operators, over a viable
`operator()`, an `explicit` conversion, a derived conversion hiding a base's, a
class argument and a call through a pointer; and `place_declarers` at a lookup
written in a class, in a block, through a base and through a chain of
directives.  Finding 1 below is what that last one turned up, and it is C12's
own; the other six are older seams the review reached.

**What it found is that a rule landed at one reader is still a rule with
siblings, and that the sibling is usually a spelling of the same question the
checkpoint never had to ask.**  Seven blockers, every one of them a second
implementation of a rule the stage had already settled once - two of them
silently wrong code, three refusals of valid programs, one an ill-formed program
accepted and lowered into a `branch` on a class object, and one a quadratic.

**1. 7.3.4p2's directive was recorded at the level its names appear at, so it
reached every lookup in the namespace around it.** C12 landed the level reading
in `SemaModel::lookup` - a directive's names appear at the nearest namespace
enclosing both it and the namespace it named - and `using_directive` went on
answering that same half a second time by *storing* the directive on that
enclosing namespace.  What the second recording loses is 7.3.4p2's other half:
the names can be used **in the scope the directive appears in**.  So
`int g() { using namespace R; } int h() { return q; }` found `R::q` in `h`,
which g++ and the reference both refuse; and two functions each nominating a
different namespace that declares one name made every use of that name
ambiguous - `int g() { using namespace L; return v; }` beside
`int h() { using namespace R; return v; }` was **refused outright**, and so was
every one of 250 sibling blocks each nominating one namespace.  The directive is
recorded on the scope that wrote it now, and `lookup` walks the chain outward
asking both halves at once: a level that wrote a directive reaching a declaring
region takes it up, and the first level from there that *encloses* it is where
its declarations appear.  That also replaced `place_declarers`' up-front walk of
the whole chain, so a lookup answered where it stands pays one level.

**2. 6.4p4's condition-declaration was never asked what the statement branches
on.** The expression spelling of a condition ran the contextual conversion and
refused where none answered; the declaration spelling ran the same conversion,
recorded that none had - "the caller's check is what refuses the condition" -
and then `continue`d past the caller's check.  So `if (S s = make())` over a
class with no conversion to bool was **accepted**, and the lowering wrote
`branch` on a `load obj<4x4>` of the object's own storage: a class object handed
to the machine as a jump condition.  `require_condition_type` is one reading for
both spellings now.

**3. 5.4p4's two readings of a cast to a reference had no fact to tell them
apart, so the lowering took the operand's address for both.**
`take((const int&)d)` over a `double` passed eight bytes of an `f64` through a
`const int&`; g++ truncates into a slot of its own and passes its address.  The
two readings - 8.5.3p5's initialization of a temporary holding the conversion,
and 5.2.10p11's reinterpretation of the storage the operand named - spell exactly
the same two types, so nothing below the analysis can tell them apart.
`Fact::binds_temporary` is that fact, written where the analysis already knows
and read where the lowering already materializes an argument bound to a
reference parameter.

**4. 5.2.9p4's cast read the object standing under it only where that object was
a `temporary-object`.** A call returning an object of this class creates it
exactly as a written temporary does, so `V v = (V)h();`, `return (V)h();`,
`sink((V)h())` and `const V& r = (V)h();` were each **refused** on a valid
program - "an object of the class type struct V is copied".  `creates_its_object`
and the lowering's own cast ask the same question of whatever node stands under
the cast now, and `h` writes its returned object straight into `v`.

**5. A constructor's by-value class parameter was not 5.2.2p4's boundary.** An
ordinary call names the storage the parameter stands in before the argument runs,
so the argument builds its object there; `constructor_call`'s own argument loop
read the argument as a value and copied it in, which **refused** every prvalue
the callee could have been left to build in place - `W w(from(T()))` where `W`
takes a `T` by value.  The two loops are one rule and read `class_argument`
alike now.

**6. 7.3.4p2's level asked the more expensive of the two questions.** It asked of
each region that declares the name whether this level reaches it, where the
reading before C12 knew to ask whichever set is smaller.  n blocks each
nominating one of n namespaces that all declare one name was n probes per lookup
for one answer - **0.170 s at 2000 against 0.086 s** for the same program with n
distinct names.  The walk of the reachable set runs on a budget of the declaring
list's size again, and the shape is 0.012/0.021/0.040/**0.080** s.

**7. 5.4p4 orders more readings than the fact above had cases for.** A
conversion the cast cannot write is the reinterpretation and not a refusal:
`(const E&)i` over an enumeration and `(const W&)i` over a class each spell a
`T t(e);` that is ill formed, and both g++ and the reference write the
reinterpretation where we refused.  And 4.4p4's qualification conversion leaves
no temporary to hold - it is between two pointers to the same type, so
`(const char* const&)p` over a `char*` names `p` where a `void* const&` over the
same operand materializes, which is the exception `match_reference` already
makes for an argument.

`lowir_lower_object.cpp` went past the audit's 3000-line limit with finding 5,
and the seam is the one `sema_allocation.cpp` already owns on the analysis side:
`lowir_lower_allocation.cpp` owns 5.3.4's storage and 5.3.5's return of it, with
every object it builds or ends going through the same `constructor_call` and
`destruction_step` a declaration reaches.

## Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, best
of three, at 250/500/1000/2000 unless said otherwise.

| finding | before | after |
| --- | --- | --- |
| 1. a block directive reaching the namespace around it | `int g(){using namespace R;} int h(){return q;}` **accepted**; two functions nominating two namespaces that declare one name **refused**; 250 sibling blocks each with one directive **refused** | each answers with g++ and the reference |
| 2. a condition-declaration of class type with no conversion | `if (S s = make())` **accepted**, lowered as `branch` on a `load obj<4x4>` | refused, as `if (s)` already was |
| 3. `(const int&)d` over a `double` | `addr $d` - the `f64`'s own storage through a `const int&` | `convert fptosi`, `store`, `addr $refcast__1`, which is what g++ writes |
| 4. `(V)h()` in a declaration, a return, an argument and a reference binding | **refused** at every one of the four | one object: `h` writes its returned object into `v` |
| 5. `W w(from(T()))` over a constructor taking `T` by value | **refused** | `from` builds into the parameter's storage; byte-identical to the reference |
| 6. n blocks nominating n namespaces that declare one name | 0.012/0.021/0.040/**0.170** s | 0.012/0.021/0.040/**0.080** s, 1263/2513/5013/10013 lines |
| 7. `(const E&)i`, `(const W&)i` | **refused** | the reinterpretation g++ and the reference both write |
| 7. `(const char* const&)p` over a `char*` | a temporary holding the value `p` already held | `addr $p`, which is what g++ writes; `(void* const&)p` still materializes |

The performance model below every one of these sits under is unchanged, and the
paths the review touched are linear in the source's own size at every axis
measured:

| axis | sizes | time / output |
| --- | --- | --- |
| n using-directives with n uses of n distinct names | 250/500/1000/2000 | 0.013/0.023/0.050/0.102 s, 5 n + 13 lines |
| n using-directives with n uses of **one** name | 250/500/1000/2000 | 0.012/0.021/0.040/0.080 s, 5 n + 13 lines |
| n uses of a name two regions declare, each hidden by a local | 250/500/1000/2000 | 0.010/0.017/0.035/0.070 s, 6 n + 15 lines |
| n casts to a reference of another type | 250/500/1000/2000 | 0.009/0.015/0.027/0.053 s, 6 n + 13 lines |
| n `reinterpret_cast`s to a reference | 250/500/1000/2000 | 0.007/0.012/0.021/0.039 s, 2 n + 13 lines |
| n casts of a call to its own class | 250/500/1000/2000 | 0.013/0.023/0.042/0.084 s, 15 n + 242 lines |
| n such casts nested n deep | 50/100/200/400 | 0.004/0.005/0.006/0.010 s, 3 n + 18 lines |
| n constructor calls with a by-value class argument | 250/500/1000/2000 | 0.013/0.023/0.044/0.086 s, 9 n + 13 lines |
| one constructor call with n by-value class arguments | 50/100/200/400 | 0.004/0.005/0.006/0.010 s, 3 n + 15 lines |
| n class-valued condition-declarations | 250/500/1000/2000 | 0.019/0.035/0.069/0.164 s, 32 n + 14 lines |
| n reference casts under n standing objects | 250/500/1000/2000 | 0.018/0.033/0.062/0.136 s, 23 n + 253 lines |
| n array members of two elements, all four transfers | 250/500/1000/2000 | 0.063/0.118/0.229/0.462 s, 108 n lines |
| a union of n class members, copied and destroyed | 250/500/1000/2000 | 0.012/0.020/0.039/0.080 s, **56 lines at every size** |
| n standing objects across n calls | 250/500/1000/2000 | 0.014/0.024/0.045/0.092 s, 18 n lines |
| n temporaries in one function | 250/500/1000/2000 | 0.008/0.012/0.021/0.041 s, 4 n lines |

The one superlinear axis measured is the source's own and predates this
milestone: **a use written n blocks deep from the declaration it names costs n
levels of the region chain**, so n such uses at depth n are 0.009/0.019/0.050/
0.157 s where the same n statements naming only their own block's declarations
are 0.02/0.04/0.09 s at 500/1000/2000 and n *empty* blocks nested n deep are
0.00 s at every size.  The binary built at `07baea74`, before C12, is the same
0.15 s on the same file to the millisecond.  It is 6.6p2's n² read the other way
round: the k x m the program wrote.

`valgrind` is clean over all 63 probes of this review, over the sixteen
fundamental types under a reference cast, and over every generated unit of the
performance sweep.

The reference binary reproduces **all 214 comparable fixtures exactly** -
`pa17/scripts/ref_vs_fixtures.pl` reports 214 same, 0 different, 0 exit-status
different - which is what makes a probe of it an oracle.  `g++` was the third
oracle everywhere the binary and this translation could disagree, and it settled
findings 3, 4 and 7, where the binary is wrong in a direction of its own: it
passes the converted *value* where a pointer is wanted for a reference cast, and
it emits a body that calls `h` not at all for `V v = (V)h();`.

Four regression tests came out of the review.
`400-constructor-by-value-argument-object` pins finding 5 and the reference
reproduces it exactly; `100-condition-declaration-no-conversion-bad`,
`100-block-using-directive-scope` and
`100-block-using-directive-out-of-scope-bad` pin findings 2 and 1, and the
reference agrees with each of them on the exit status and on the LowIR.
Findings 3, 4 and 7 have no fixture, because the reference's answer for each of
those shapes is one it does not agree with us on - adding a `.ref` for them would
break the property the whole stage leans on.

## Open Gaps

**13.3.1.4's copy-initialization of a class from a class measures the
constructors with no bound on the conversion its argument takes.** 13.3.3.1p4
allows only a standard conversion sequence to the first parameter of a
constructor that is a candidate by 13.3.1.4, and `construct_object` resolves the
class's constructors directly rather than through `conversion_match`, so nothing
sets that bound: `T t = c;` over a `Cv` with `operator int()` and a `T(int)` is
**accepted** where `g++` refuses two user-defined conversions, and `T t = c;`
over a `Cv` declaring both `operator int()` and `operator T()` is **refused** as
ambiguous where `g++` chooses `operator T()`. The reference binary answers both
exactly as we do - it reports the same ambiguity for the second - and
`sink(c)`, which asks the same question through `match_by_value`, answers both
correctly. What it wants is 13.3.1.4's candidate set made one: the conversion
functions of the argument's class beside the class's own converting
constructors, rather than reached *through* a constructor's parameter. That is a
checkpoint's work on the one central path 234 fixtures run through, and it costs
one temporary and one call and no wrong output.

**3.4.1p8's point of declaration is not modelled for a deferred member function
body.** A member function body is analysed after the unit has been read, so a
namespace-scope declaration written *after* the class is still in scope for it:
`struct S { int f() { using namespace C; return i; } }; int i = 1;` reads `::i`
beside `C::i` and is refused as ambiguous, where `g++` and the reference accept
it - 3.4.1p8 bounds a member body to what was declared before the class. The
same reading accepts `struct S { int f() { return i; } }; int i;`, which `g++`
refuses. An ordinary function body already gets this right, because it is read
where it stands. It wants a watermark on the declaration order carried into the
deferred body, which is a change to every lookup in the compiler and predates
this milestone entirely.

**5.2.11's own rule for what a `const_cast` may write is not modelled**, so
`const_cast<const E&>(i)` over an enumeration is accepted here and refused by
`g++` and the reference. Every cast to a reference the operand is
reference-related to nothing of falls to 5.4p4's reinterpretation unless a named
`static_cast` refuses it, and const_cast has 5.2.11p4's similar-pointer reading
that `pa15`'s `200-const-cast-reference-similar-pointer` pins - telling the two
apart wants 5.2.11's similarity, which nothing in this stage asks for.

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

**A `try` block in an ordinary body is refused** - "a statement outside the PA12
subset" - so `try { return h(); } catch (V e) { return e; }` does not parse here
and 12.8p32's exclusion of a catch-clause parameter has nothing to be asked of.
The README lists no handler syntax among this milestone's own. It predates C10.

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
| PA17 | the stage's own review: the readings a rule landed at one reader left at its siblings | `5ba05933` | 7 / 7, every one of them a second implementation of a rule the stage had already settled once: 7.3.4p2's directive recorded at the *level* its names appear at rather than where it was written, so a block directive reached every lookup in the namespace around it - `int g(){using namespace R;} int h(){return q;}` was **accepted** and two functions each nominating a namespace that declares one name **refused every use of it**; 6.4p4's condition-declaration never asked what the statement branches on, so `if (S s = make())` over a class with no conversion to bool was **accepted** and lowered as `branch` on a `load obj<4x4>`; 5.4p4's two readings of a cast to a reference had no fact to tell them apart, so `take((const int&)d)` over a `double` passed the `f64`'s own address through a `const int&`; 5.2.9p4's cast read the object standing under it only where that object was a `temporary-object`, so `V v = (V)h();`, `return (V)h();`, `sink((V)h())` and `const V& r = (V)h();` were each **refused** on a valid program; a constructor's by-value class parameter was not 5.2.2p4's boundary, so `W w(from(T()))` was **refused** where the same argument to an ordinary function is placed in the parameter's storage; 7.3.4p2's level asked the more expensive of the two questions, which was **0.170 s at 2000** against 0.086 s for the same program with distinct names; and 5.4p4's fall to the reinterpretation was missing at both ends - `(const E&)i` and `(const W&)i` were refused where g++ and the reference reinterpret, and 4.4p4's qualification conversion materialized a temporary where the operand's own storage already holds the value. `Fact::binds_temporary`, `require_condition_type`, `take_pending` and `lowir_lower_allocation.cpp` at 5.3.4's own seam are what came out of it | **1732 / 1732**, four of them regression tests; pa1-pa16 unchanged; file audit passes; the reference reproduces all 214 comparable fixtures |
