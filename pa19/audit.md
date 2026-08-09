# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C10, reviewed at `0b3f72b8`** — 9.4.2p1's and 3.4.1p8's qualified
declarator-id recording its pattern on the declaration the region that name
reaches already has, with 14.1p1's own head standing inside that region
(`StandingIn`); 3.4.3p1's prefix walked component by component; 14.5.2's member
template given 9.3.1p3's object parameter; 14.8.1p2's partly written argument
list made a declaration of its own; and 8.2p7's `T (X)` read back as the
declarator 3.4 says it is.

**Two of the five rules are right and swept clean, and a third is right at the
two readers it was given.**  `StandingIn` holds over every form the qualified
declarator-id takes - a function template defined and declared out of
namespace, a nested namespace, a leading `::`, a class-head-name that defines
the template a namespace declared, an out-of-class member and a static data
member of one, and a head whose parameter name a typedef in the target region
also spells - eight shapes agreeing with `reference-binaries/cppgm++` and
**g++** and identical to the reference as emitted LowIR in every one.  8.2p7 is
swept over twelve: a plain place, an array, two places, a type-name that makes
the parameter a function, a decltype over the place beside it, a member
function, a template, a name nothing declares, a declaration matched by a
definition, a default argument, a redundant pointer and a place shadowing a
global - agreeing with **g++** in all twelve and beating the reference on three
it refuses.  14.8.1p2 is swept over eleven, including the memo, a target type,
too many arguments, a deduction that reaches nothing, an address with no
target, a substitution failure and a class template, which reaches this path in
neither compiler.

**Seven blockers, and four of them are one shape - 14.5.2's object parameter has
readers C10 did not give it.**  A member template now carries 9.3.1p3's object
parameter, and every question that used to be answered from a function type had
to be asked again: 9.4.1p2's "which declaration does this definition redeclare",
14.7.1p1's "what is a specialization called on", and 14.5.6.2p2's "which places
are these two ordered by", which two different pairs reach.  Three of the four
left a program both oracles compile refused - one of them a **regression C10
shipped** - and the fourth emitted an object parameter for a member 9.4p1 calls
on no object.  The other three blockers are 14.8.1p2's unlanded second arm, its
third reader, and two components 3.4.3p1's new prefix walk does not reach.

### Findings

**1. A static member template defined outside its class was refused.**
`declares_static_member` asks 13.1's index of the class's chain, which is keyed
by the parameter list *as written* - and 14.1p2 gives each head parameters of its
own, so the definition's `int(T)` is not the declaration's.  The probe missed, so
9.4.1p2's `static` was taken as unwritten rather than as said elsewhere, the
definition got an object parameter its declaration has not, and
`template<class T> int counter::raised(T)` over a `static` member was
**`a definition of raised matches no declaration of it`** where both oracles
compile it.  14.5.6.1p5's signature is the question that answers it - each head's
parameters standing for the places they were declared in - which is the same
`equivalent_template` `declare_function` asks two lines later.

**2. And 14.7.1p1's second reading of that definition could not ask either.**
The region an instantiation reads a pattern under binds *arguments*, so its
declarations are typedef-names and not template parameters: neither 13.1's index
nor 14.5.6.1p5's signature can find the declaration a substituted type came
from.  A specialization is a declaration nothing wrote, so what it is called on
is the template's own declaration's fact, and `template int counter::raised<int>
(int)` emitted `function @counter__raised(%this : ptr, %step : i32)` for a
member 9.4p1 calls on no object.

**3. 14.5.6.2 could not order a static member template against a non-static one
- a regression C10 shipped.**  `more_specialized` refused any pair whose
`object_member` differed, which before this checkpoint was never two member
templates.  `struct c { template<class T> static int r(T); template<class T> int
r(T *); };` called as `o.r(&two)` is **`a call of r has no best declaration`**
here, and `reference-binaries/cppgm++`, **g++** and the pre-C10 `e7eb1c1a` build
all choose `r(T *)`.  13.3.1p4 is why: a static member has an implicit object
parameter that matches any object, so it tells the two apart by nothing and the
places their declarators wrote are what is left to order them by.

**4. And the same guard left 13.5p6's member operator template unordered against
the non-member beside it** - which this checkpoint's object parameter is what
put in the candidate set at all (`e7eb1c1a` does not read a member operator
template as an operator).  14.5.6.2p2 is the clause: where only one of two
function templates is a non-static member, that one is considered to have a
first parameter of "reference to cv A", which is exactly the place the
non-member wrote its own first operand in.  9.3.1p3 wrote it as a *pointer*, so
`ordering_parameters` is the one place the two lists are lined up - a reference
against a non-member that wrote the operand, dropped against 9.4p1's static
member of the same class, and left as written for every other pair, so a member
against a non-member that wrote no operand is still ordered by nothing.

**5. 14.1p9's default template argument never reached a function template's
deduction.**  14.8.1p2's sentence has two arms - a trailing argument may be
omitted where it can be **deduced or obtained from a default template-argument**
- and the checkpoint landed the first.  `record_function_template` records
`nullptr` for every default, so `template<class T, class U = long> int mix(T);`
was `no declaration of mix accepts the arguments of a call` with `mix<int>(2)`
and with a bare `mix(2)` alike.  The default is a fact of the *parameter* -
14.1p2 lets each declaration spell its places as it likes - so it is kept beside
the declaration `template_parameter` makes, read in the region the head declared
its parameters in because 14.1p9 lets it name the places before it, and
substituted with what the deduction has settled so far, so `class U = T` takes
what `T` deduced.

**6. 3.4.3p1's prefix walk did not reach two of the components it walks.**
`member_definition_owner` looked each component up in `named->scope`, which a
typedef-name naming a class has none of, and began at `ctx.scope` for a name
written `::v::S<T>::f`.  Both are what `resolve_prefix` - the one place 3.4.3p1
is answered - already does with `region_of` and the empty first component, and
without the owner the definition's head is never stood against the class, so
`template<class T> int ::vessel::real::held<T>::twice()` and the same through a
typedef were `twice is written after a name that is not a namespace, class or
enumeration` on programs both oracles compile.

**7. 14.7.2p1's explicit instantiation is the third reader 14.8.1p2 was not
given.**  `instantiation_named` matches the type the declaration wrote against
`found[index]->type`, and a partly written list stands for a declaration whose
type is still the template's own - so `template int mix<char>(double);` named no
specialization where the arm beside it, a name that wrote no list at all,
already deduces from that type with `deduce_target`.  **g++ accepts it** and
`reference-binaries/cppgm++` refuses it, so it is landed on g++'s side and
recorded with the other places the reference stands alone; it can be no
committed fixture, because the reference is what writes one.

### Changes

| what | where |
| --- | --- |
| 14.5.6.1p5's signature answering 9.4.1p2 for a member template's definition | `sema_class.cpp`, `sema_analyzer.h` |
| 14.7.1p1's specialization saying what it is called on | `sema_function.cpp` |
| 14.5.6.2p2's places two templates are ordered by (`ordering_parameters`) | `sema_overload.cpp` |
| 14.1p9's default kept beside the parameter and read where a deduction finds the place empty | `sema_analyzer.cpp`, `sema_analyzer.h`, `sema_template.cpp` |
| 3.4.3p1's leading `::` and typedef-name component in a member definition's prefix | `sema_template.cpp` |
| 14.8.1p2's partial list deduced from at 14.7.2p1's explicit instantiation | `sema_template.cpp` |

Five regression tests under `cppgm.tests/course/pa19`, one per finding but the
two that are one clause: `100-static-member-template-out-of-class`,
`100-static-and-non-static-member-template-order`,
`100-member-operator-template-order-against-non-member`,
`100-function-template-default-template-argument`,
`100-member-definition-prefix-through-alias`.  All five fail against the
pre-audit binary built from `0b3f72b8` - two on a refused definition, two on
`no best declaration` and one on a call no declaration accepts - and the
ordering one *passes* against the pre-C10 `e7eb1c1a` build, which is what makes
it the regression rather than a gap.  Finding 7 has no fixture: the reference is
what writes a `.ref` and it refuses the program.

### Performance Evidence

Nine shapes over the surface this audit owns, `--emit-lowir -O0`, n = 32, 128
and 512, each timed twice against the pre-audit binary built from `0b3f72b8` in
a worktree with `make build`:

| shape | pre-audit | now |
| --- | --- | --- |
| n function templates overloading one name, each called | 0.00 / 0.03 / 0.37 s | 0.00 / 0.03 / 0.35 s |
| n target types each choosing among n function templates | 0.00 / 0.02 / 0.15 s | 0.00 / 0.02 / 0.15 s |
| n calls of one template with a default template argument | 0.00 / 0.00 / 0.00 s | 0.00 / 0.00 / 0.01 s |
| n out-of-class static member template definitions in one class | 0.00 / 0.00 / 0.01 s | 0.00 / 0.00 / 0.02 s |
| n member templates of one class, each called | 0.00 / 0.01 / 0.05 s | 0.00 / 0.01 / 0.05 s |
| n distinct specializations of one class template | 0.00 / 0.01 / 0.05 s | 0.00 / 0.01 / 0.05 s |
| n templates each named by a partial explicit list | 0.00 / 0.01 / 0.04 s | 0.00 / 0.01 / 0.05 s |
| n non-member operator template pairs, each called twice | 0.01 / 0.07 / 1.17 s | 0.01 / 0.07 / 1.17 s |
| **n member operator templates ordered against a non-member, each called twice** | - | 0.01 / 0.05 / 0.63 s |

The two questions the audit adds are asked per *declaration* and not per use:
14.5.6.1p5's signature is computed once per declaration and already memoised, so
n static member template definitions in one class stay linear, and 14.1p9's
default is one type-id read per place a deduction leaves empty, which is why n
calls of such a template are 0.01 s at n = 512.  Every shape the ordering path
already carried is where the checkpoint left it, to the hundredth and within 1%
of its memory (n overloads of one name 29.3 MB in both binaries, n member
templates 20.5 -> 20.6 MB): `ordering_parameters` returns the written list
itself wherever 14.5.6.2p2 has nothing to change, so the pair that costs an
adjusted list is the mixed one alone.

The last shape is the one this audit makes compilable - the pre-audit binary
**refuses it**, so its 0.06 s is an early exit and not a measurement.  Its cost
is 13.3p1's own quadratic over the ADL-reachable `operator+` declarations, and
the honest baseline is the row above it: the *same* call count written with two
non-member operator templates per class, which both binaries compile in 1.17 s
at n = 512 against this shape's 0.63 s.  `reference-binaries/cppgm++` is 0.18 s
at n = 32 and **21.01 s** at n = 128 on it, against our 0.01 s and 0.05 s.

### Validation

- **322 / 346 -> 327 / 351**, the five new tests being the five shapes these
  leave, and the failing 24 the same 24 by name; pa1-pa18 **1778 / 1778**.
- **File audit passes** for pa19 over `dev/src` with the five header-weight
  warnings the shared headers have carried since PA18, and no suppression.  The
  build prints nothing.  `sema_analyzer.h` is **2391 against the limit of
  2400**: this audit's one new member is a map and its two other questions are a
  parameter on a declaration that already existed and a free function of the
  file that asks it, and the next thing that header grows by owes a split.
- **Every `.ref` regenerates byte-identically** over all 319 fixtures under
  `pa19/tests` and all 27 checked-in under `cppgm.tests/course/pa19`; no
  committed fixture moved.  The five new `.ref` files are the reference's own
  output.
- **Ninety-six synthesized shapes through three binaries** - this compiler, the
  `0b3f72b8` pre-audit and `e7eb1c1a` pre-C10 builds - with
  `reference-binaries/cppgm++` and **g++** beside them: the object parameter a
  member template carries over static, friend, cv-qualified, operator,
  class-template and out-of-class forms; `StandingIn` over eight qualified
  declarator-id forms; 3.4.3p1's prefix through a namespace, a nested namespace,
  an alias, a typedef, a leading `::` and a class; 8.2p7 over twelve; 14.8.1p2
  over eleven; 14.1p9's default over seven; and 14.5.6.2's ordering over fifteen.
  **No shape in the sweep is accepted by both oracles and refused here.**  Six
  the reference alone refuses this compiler and g++ both compile - three 8.2p7
  places and the three forms of finding 7 - and three this compiler and g++ both
  refuse the reference accepts, which is its error recovery: a qualified
  class-head naming no template, one naming a typedef, and a static member
  template with a ref-qualifier 8.3.5p6 does not allow.  The one shape left
  where g++ alone compiles is a member operator template against a *fully
  generic* non-member one, which g++ itself diagnoses as "ISO C++ says that
  these are ambiguous".
- **Eighty of them compared as emitted LowIR** against the reference after the
  harness's own canonicalization and pairing, identical in all 80.
- **Run evidence with scalars.**  One unit writing all six rules - a static
  member template defined out of class through `::`, a member of a class
  template through a typedef prefix, a defaulted template argument, a partial
  explicit list at an explicit instantiation, and both orderings - lowered here
  and run through `lowir2cy86` and `cy86` returns **42**, which is what g++
  builds it to return.  The same unit without the explicit instantiation, which
  is the one line the reference refuses, is byte-identical to the reference's
  LowIR.
- **Two units, both orders.**  Two units each defining one static member
  template through a qualified declarator-id and one template with a defaulted
  argument keep one weak copy, emit the same LowIR in either order, and agree
  with the reference symbol for symbol and `object=` for `object=`.
- **Valgrind clean** with `--error-exitcode=99` over all 351 fixtures.

## Open Gaps

**14.8.2's substitution failure ends the translation unit.**  A deduction that
substitutes the arguments into a decltype-specifier reads the expression again,
and where that reading has no answer the error is thrown rather than the
candidate dropped: `pick(T a) -> decltype(a + a)` beside `pick(T * a) ->
decltype(*a + *a)` called on an `int *` is
`an operand of + or - is neither arithmetic nor a pointer` here, and both oracles
choose the second.  It is the shape a decltype return type exists for, and
**substitution-failure candidate dropping and SFINAE are on PA19's Out Of Scope
list**, so the reading is left where the milestone puts it; what it owes when the
subset reaches it is 14.8.2p8's immediate context - the reading a candidate's own
substitution does, and no reading below it, failing that candidate alone.

**Two declarations that name their places differently, where only g++ agrees.**
`template<class T> auto pick(T a, T b) -> decltype(a + b);` followed by
`template<class T> auto pick(T x, T y) -> decltype(x + y) { }` is
`a call of pick has no best declaration` here and in
`reference-binaries/cppgm++`, and **g++ compiles it**: 14.4p1 makes two
expressions equivalent when they contain the same sequence of tokens with the
names looked up to the same entities, and 8.3.5p10 does not put a place's name in
the function's type - so whether a place is "the same entity" across two
declarations is the question the clause leaves open, and this compiler and the
reference both answer it by the spelling.  The head's own names are canonicalized
by position, which is the half 14.4p1 states outright; the places' are not, and no
fixture through pa24 writes a pair.

**A decltype-specifier over a non-static data member at class scope wants an
object.** `struct box { int v; decltype(v + v) doubled(); };` is
``this` is written outside a member function` here and both oracles accept it:
5.1.1p13 lets an id-expression naming a non-static data member stand in an
unevaluated operand, and the expression layer adds 9.3.1p3's object to every
member name it reads.  The same declaration with no template anywhere is refused
identically, so it is the PA12 expression layer's rather than this tier's - what
it owes is a reading of a member name that stops at the declaration where no
object is being named.  `decltype(v)` alone, which is 7.1.6.2p4's id-expression
arm, is answered without one and works.

**One region is rebuilt per decltype-specifier rather than per region.**
14.7.1p1's second reading builds a fresh `Scope` for each of 14.1p1's and
3.3.7p1's regions standing over the specifier, so a clause of n places each of
whose type is a decltype-specifier over the first costs n regions of up to n
declarations - 0.19 s and 99.9 MB at n = 512, against 0.05 s and 30.7 MB at
n = 256.  The narrower rule is one rebuild per region and bindings, memoised on
the substitution, which needs a key over the bindings map that the per-call memo
does not have.  A 512-place clause is not a translation unit and every shape a
program writes is linear, so it is recorded rather than landed.

**A trailing-return-type in a function declarator is dumped without its
spelling.**  Found by putting all 59 shapes the C7 audit swept through
`--emit-ast` against the pa10 reference: `auto f(int a) -> int` is
`trailing-return-type` here and `trailing-return-type int` there, `-> int*` is
`trailing-return-type int*` there, and `-> C` agrees because
`parse_trailing_return_type` spells the node only where the type-id is one
`TypeName`.  A *lambda*'s `-> int` is textless in both, so the reference spells
the declarator's and not the lambda's.  It is PA10's rather than this
checkpoint's - the code is `15d8a001`'s, the milestone that wrote the AST - and
the one pa10 fixture with a function declarator's trailing-return-type writes
`-> C`, which is why 1778 / 1778 stands over it.  The other 58 shapes dump
byte-identically.

**A member function template's explicit argument list does not parse.**  C10
landed the declaration, 9.3.1p3's object parameter and the out-of-class
definition, and this audit landed 9.4p1's static arm of it and 14.5.6.2p2's
ordering - so `template<class E> int graph::search(E const &) const { }` written
outside the class compiles and matches the reference's LowIR.  What is left is
`s.f<int>(0)`, which is not a translation unit at all: `parse_member_id` accepts
a bare identifier after `.` and never tries 14.2's argument list, so 14.8.1p2's
partial list is unreachable through a member access.  Both oracles compile it,
it is why `pa22`'s `300-explicit-instantiation-deduced-member-function-template`
is the one explicit-instantiation fixture in the later assignments this compiler
cannot read, and it is what the plan's C11 owns along with the two-clause head
and 12.3.2p1's conversion function template.

**14.7.2p5's second explicit instantiation is unasked.**  `template int f<int>
(int);` written twice is ill-formed and **g++ refuses it**; this compiler and
`reference-binaries/cppgm++` both accept it and emit one rooted definition.  It
is a rule of its own rather than a reader of the one the C7 audit landed, and no
fixture through pa24 writes it.

**Six more places the reference is alone, found sweeping 8.2p7 and 14.8.1p2.**
It refuses `int g(int (x[10]))`, `int g(int (x), decltype(x) y)` and
`template<class T> int g(T (x))` - three spellings of 8.2p7's parenthesized
place that this compiler and **g++** both read as a declarator - and it refuses
all three forms of a partly written argument list at an explicit instantiation:
`template int mix<char>(double);`, the same over a static member template, and
`extern template`.  14.8.1p2 lets a trailing argument be omitted wherever a
template-id names a function template, 14.7.2p2 asks only that the declaration
name a specialization, and **g++ accepts all three**, so the audit lands them on
g++'s side.  None can be a committed fixture, because the reference is what
writes a `.ref`.

**Three places the reference is alone, found sweeping 8.3p1 and 14.7.2.**  It
cannot find a parameter name in the body of a function whose declarator-id
stands under a nested clause - `int (*f(int a))(int)`, `int (*f(int a))[2]`,
`int (&f(int a))[2]`, `int (*(f(int a)))(int)`, the same with an
exception-specification, as an out-of-class member definition and under a
template head, eight shapes in all - where 8.3p1, its own `_Z1fl` and **g++**
say the place is the function's own.  It refuses `template int t<int>::v;`,
which 14.7.2p1 lists outright and **g++ accepts**.  And it roots no definition
for a member *class*'s members at an explicit instantiation, where **g++** emits
all 40 of a 40-deep chain as this compiler does.  None can be a committed
fixture and each is decided by g++ and the standard.

**The class's own parameter names are still reachable from a definition written
outside it.** `template<class T> struct A { void f(); };` with
`template<class U> void A<U>::f() { T x; }` compiles here and in
`reference-binaries/cppgm++`; g++ refuses it, because 14.1p2 gives the
definition a head of its own and the class's is not in scope for it. The seam is
structural rather than a rule left unasked: the class scope is what a member
definition's body is read inside, and the region enclosing that class is the one
the class body itself was read against - the one binding the class-head's names.
Refusing here means the class scope having two enclosing regions, one for its
own body and one for a definition written outside it, which is a change to what
a class *is* in this model rather than to what a rule asks. The names the C5
audit settled are the ones the definition's own head wrote, which is what decides
which argument a name reaches.

**15.4p1 over two declarations of one function template.**
`template<class T> void f(T) throw();` followed by
`template<class U> void f(U) { }` is refused here and accepted by
`reference-binaries/cppgm++`, which refuses the same mismatch between two
declarations of an ordinary function; g++ warns on both and compiles both. The
standard makes all of them ill-formed, so the checkpoint's rule is asked of the
declaration pair the reference does not ask it of. No fixture writes one.

**An exception-specification writes none of the EH the reference writes.**
`void f() throw(); void f() throw() { }` emits, in the reference, an
`eh_try`/`eh_filter` dispatch calling `__cxa_call_unexpected`, and this compiler
emits the body alone - for an ordinary function and for an instantiated member
alike. It predates this tier (the pre-C5 binary emits the same), no fixture
reaches it, and 15.4p8's `unexpected` handler is what the work owes.

**Recorded, not defects.** A specialization of a template declared in
7.3.1.1p1's unnamed namespace binds `internal` here and `weak` in the
reference; 3.5p4 gives every name in that region internal linkage and **g++
emits it local**, so the reference stands alone. An instantiated constructor
emits both of 12.1's entry points where the reference emits only the
complete-object one; **g++ emits both**, and the reference's own non-template
out-of-class constructor gets both - so this is the reference's rule for
instantiated definitions, and matching it is what turned three fixtures green.

**14.5.1.3p1 read for every specialization rather than for every use.** A class
template's out-of-class member definitions are read once per specialization,
wherever that specialization is completed, so n definitions and n
specializations cost n^2 readings even where no member of any of them is called
- 0.73 s at n = 128, against `reference-binaries/cppgm++`'s 1.00 s. C8 landed
14.7.1p1's half of this - a member defined *in* its class waits on
`held_definitions_` for the use that names it - and `instantiate_member`'s is
what is left: an out-of-class definition is read where it stands, for every
specialization already made, so it is the milestone's one quadratic in the
*tier* rather than in the program.

**Out of scope and still named.** A variable template's partial specialization
is written into the object file as `_Z6v<T,T>`, which is not an ABI name.
14.5.5 partial specialization and variable templates are both in PA19's Out Of
Scope list, so the input's behaviour is undefined for this milestone; the name
is left where the feature is.

**The spelling a specialization is named by** is exponential in the depth of a
nest whose arguments double, as measured above. It is the reference's shape too
- and by a factor of 18 at n = 20 - and no fixture reaches it, so it is recorded
rather than re-architected: fixing it means not storing a specialization's
written-out name at all.

**14.6.1p6 where only g++ agrees.** The rule is now asked wherever a
declaration binds a name, and the reference accepts six of those where the
standard and g++ refuse them: `typedef int T`, `int T`, an enumerator, a
parameter of a definition, a local function declaration, and the template's own
declared name.  It also accepts a class, an enumeration or a namespace-alias
written in a *nested* block of the body while refusing the same declaration at
the body's own level - what it is answering there is a collision with the
bindings it puts in that one region, not 14.6.1p6.  This compiler refuses all of
them at every depth.  The one shape the seam does not reach is a parameter of a
declaration that writes no body: 3.3.4's function prototype scope is not a
region this model opens, so the name is bound nowhere to be asked about, and
both oracles accept it.

**6.6.4p1 is asked where one of the three body readings ends.**
`require_labelled_gotos` runs where a body written outside its class is read and
where an instantiation reads one; `write_definition`, which reads a member
function defined *in* its class at the end of the unit, does not ask it, so
`struct S { void f() { goto miss; } };` is accepted. It was accepted before this
checkpoint too, and `reference-binaries/cppgm++` accepts it while g++ refuses
it, so it is recorded rather than changed.

**A handler's name is where the reading meets a construct the milestone does
not model.** `catch (int caught) { return caught; }` in an uninstantiated
function template is refused here - the definition-time reading looks `caught`
up and the walk that reads a handler declares nothing for it - and both oracles
accept it. The cause is not the reading: the same try-block in an *ordinary*
function is `a statement is outside the PA12 subset`, so an instantiated one is
refused whatever the reading says, and declaring the exception-declaration for
the reading alone would make an uninstantiated template compile where the
instantiated one cannot. It is recorded with the try-block rather than fixed
half-way, and 15.3p2's region is what the fix owes when the subset reaches it.

**7.1.3p3's other half - that a redeclaration names the same type - is unasked.**
`typedef int A; typedef long A;` at namespace and at block scope, and `struct A
{}; typedef int A;` in either, each declare one name for two types and are
accepted here. g++ refuses all of them; the reference accepts every one but
`struct A {}; typedef int A;` at namespace scope, where it stops with an
internal-looking `missing class info` rather than a diagnosis and accepts the
same shape written with an enumeration or inside a block. What is unasked is
7.1.3p3's agreement of *type* between two declarations rather than 9.2p1's
declared-twice, which is a rule of its own and not the half this checkpoint
landed, so it is left where a reading of 7.1.3p3 would put it.

**A member function template's address is not a target 13.4p1 chooses through.**
`int (S::*p)(int *) = &S::m;` over two member templates finds no declaration
here; `resolve_target`'s pointer-to-member arm asks each declaration for the
pointer type it *has*, which a template has none of until a deduction makes one.
`reference-binaries/cppgm++` does not compile it either, and no fixture writes
it, so it is recorded where the rest of 14.8.2.5's pointer-to-member pairs are.

**The parameter record is keyed two ways.** C6 moved `defaults_` from
`function.id` to `wrote_defaults(function).id` at the writer and at
`accepts_arity` and `write_default_argument`; `required_parameters` and
`has_default_argument` (`sema_class.cpp`) still ask `function.id`. The two keys
differ only where `primary` or `shadowed` is set, and **a member of a class
template specialization has neither** - `primary` is written at exactly two
places, both of them a specialization of a template *itself*, so an instantiated
constructor's record is its own and the two readers find it. What is left is a
function-template specialization and a using-declaration's copy, and 12.8p12
keeps both out of what those two readers are asked about; 12.9's inherited
constructors from a class template base and 12.8p2's copy constructor with a
defaulted second parameter emit identical LowIR either way. C8 has now landed
14.7.1p1's instantiated declarations and the two keys still agree - a member of
a specialization with a default argument emits the same parameter list as the
reference - so it stays one key per reader.

**A `= default` copy constructor is trivial in the reference and user-provided
here.** 8.4.2p5 leaves a function explicitly defaulted on a declaration that is
not its first one *user-provided*, so `struct box { box(const box&); ... };`
with `box::box(const box&) = default;` has a non-trivial copy constructor and a
copy of a `box` is a call of it. `reference-binaries/cppgm++` writes `copyobj`
at the copy and marks the definition `trivial_lifecycle=yes`. Beside it, an
*in-class* `= default` copy constructor of a trivially copyable class: the
reference emits the weak definition and this compiler emits none, so a call from
another unit would find nothing to link to. Both predate this tier - they are
PA16-PA18 lifecycle questions rather than 8.3.5p10's - and no fixture reaches
either, so they are recorded where the rest of 12.8's triviality is. A fixture
over a defaulted definition has to make the class non-trivially copyable for
another reason, which the six the C6-audit review added do.

**Two places the reference is alone, found sweeping 8.3.5p10.** It binds a body's
name to a parameter place 3.3.4 ends at another declarator: in
`int chosen = 7; int pick(int) { return chosen - 2; } int pick(int chosen);` the
reference loads the parameter slot where this compiler and `g++ -std=c++11` both
load the global, and the reference's program returns the wrong value. And it
writes `binding=strong` for an explicit specialization of a function template
where this compiler writes `weak`, which the comparison strips. Neither can be a
committed fixture and neither is C6's.  The first survives the 240-ordering
cross-product the C6 audit ran, where it is the only shape of an ordinary
function's parameter names the two compilers disagree about.

**12.9p1's inherited constructor keeps its arity in both other compilers.** For
`base_of<int>(int, int = 0)` inherited through `using base_of<int>::base_of;`,
`reference-binaries/cppgm++` and `g++ -std=c++11` both emit a two-parameter
`derived::derived` and apply the default at the call, where this compiler emits
the one-parameter candidate 12.9p1 forms by omitting the trailing defaulted
parameter and 12.9p3 gives no default argument to - a *signature* difference the
comparison does not strip, so a fixture over it would fail. Two oracles against
one makes it a defect rather than a reading. It is **not C8's**, which is what
the C7-audit ledger row guessed: the identical difference stands over
`struct base_of { base_of(int, int = 0); };` with no template anywhere in the
program, so what forms the candidate set is 12.9p1's own reading and not
14.7.1p1's instantiated declarations. It is a PA16-PA18 lifecycle question and
no fixture through pa24 writes one.

**A member class's table is asked for where the specialization is completed and
not where an object of it is built.** 10.3p10 leaves it unspecified whether a
virtual member of a class template is instantiated where nothing else would
instantiate it, and the walk this audit lands takes the broad reading: every
class the instantiation made is asked for its virtual members, whether or not
this unit ever builds an object of it and so whether or not it ever emits that
class's table. The cost is one grant per virtual member, which is flat in the
count - 512 sibling member classes are 0.09 s -> 0.10 s - and quadratic in the
lexical *depth* of the nest the members stand in, because the body granted at
depth n looks its names up through an n-deep chain: a 512-deep class nest with a
virtual member at every level is 0.40 s and a 1024-deep one 1.51 s, against
0.06 s for the same nest with no virtual member. Both numbers are the pre-C8
binary's to the hundredth, so the reading is the milestone's rather than this
audit's - what the `43aa2aa0` build saved on that shape it saved by writing an
unresolved symbol. `reference-binaries/cppgm++` is 0.03 s and 0.06 s, which is
the narrow reading: the table is emitted where the vpointer is stored, so the
demand a class's slots make is the demand its *constructor* gets. Making that
the rule means walking `entity.vtable`'s final overriders from
`demand_constructor_definition` rather than the declarations from
`complete_specialization`, and a 512-deep class nest is not a translation unit,
so it is recorded rather than re-architected.

**Two units defining one specialization's member keep one copy, and the root
goes with the copy that is dropped.** `template int S<int>::m();` in one unit
and `s.m()` in another emit three `function` entries in the reference - one per
unit plus `main` - and two here, because the program builder keeps the first
unit's definition of a symbol and drops the second. With the explicit
instantiation first that is harmless; with it second, the copy kept is the one
with no `object_root=yes` and the program loses a root it owes. The reference is
order-dependent here too for a function template's own explicit instantiation -
both compilers write no root when the instantiation is the later unit - so what
this is, is one merge dropping a fact rather than choosing between two: the
`object_root` of a dropped duplicate should join the copy that stays. It is the
program builder's rather than this tier's and no fixture is multi-unit, so it is
recorded with the rest of what only a link would see.

**Run evidence needs scalars.** The `pa13` LowIR -> CY86 path is the only way to
run what this milestone emits, and it hands a by-value class parameter garbage -
from our LowIR and from the reference's alike. A differential probe that passes
a class by value is measuring the scaffold; every disagreement one reports has
to be reproduced with scalars and pointers before it is a finding.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1, C2, C2 completion | the whole tier as landed, reviewed at its completion: `TemplateInfo` as the pattern a template-declaration parameterises, 14.7.1p1's instantiation as a second reading of it, the function tier and 14.5.1.3p1's out-of-class members, the two points a specialization has, 14.6.2p1's dependent argument list, `SemaAnalyzer::substituted`, and 14.8.2's deduction | `aa6fb90f` | 6 / 6 + 1 performance, in one family - **the object file's name for a specialization, which this suite cannot see**: `canonicalize_lowir_for_compare` strips `object=`, `binding=` and `alias object` and pairs functions by masked body shape, so eleven fixtures emitted symbols containing `<`, `>`, `,` and spaces and **nine of them passed**. A name was split out of a spelling at every `::`, so a template-argument-list that spells a qualified name made `api::pair<const api::text<char>,api::tag>::pair` five components and not three; `owning_classes` walked the region a definition was *written* in, so 9.7p3's out-of-class nested class lost the template above it; `abi_type` handed the encoder `Box<int>::Tag` as one spelling; the ABI's `<template-param>` was never made a substitution candidate, so **every** function-template specialization's symbol differed from g++ and the reference alike; 14.7.1p1's instantiated definition was bound `strong`, so two units naming `Box<int>` would each claim to own `_ZN3BoxIiE5twiceEv` - a duplicate symbol at link, over 19 symbols, none of which failed; and the same fact's two other readers kept asking `inline_function`, so an instantiated constructor owed a `C2` entry the reference does not and an instantiated virtual destructor owed a `D0` for a class no unit owns. Beside them, 14.6.2p1's own cost: `is_dependent` recursed with no memo over what is a graph and not a tree, and `substituted` asked it in front of its own memo | 194 → **200 / 295**, two of them the regression tests these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 → 9 tests and every survivor a definition rather than a name; 13 names byte-identical to g++; a three-unit program order-free in all four permutations; seven scaling shapes to 512 and the one that is not, measured against the reference; valgrind clean; the pa1-pa19 report 15.6 s |
| C3 | the call a template joins, and what the ordering it reaches leaves out: 14.8.2.5p3's parameter written over no template parameter, 14.8.2.1p2/p4's reference, 8.3.6p1's unwritten trailing arguments, 13.3.1.2p4's first operand, 14.8.2.2's target type, 14.8.2.1p6's overload set, 14.5.6.2's ordering, 14.5.6.1p5's equivalent declarations, and 8.5.1/8.5.4 split into `sema_init_list.cpp` | `e67acde3` | 5 / 5 + 1 performance, all of them on paths the increment did not join up - **the readers a landed rule was not given, and the clauses beside the ones it landed**: 14.5.6.2's ordering reached 13.3.3p1's tie and not 13.4p1's target, so `int (*p)(int *) = pick;` over `pick(T)` and `pick(T *)` took whichever was declared first, in all four contexts `resolve_target` answers; the ordering itself stopped at p7, so `f(T &)` against `f(const T &)` was **ill-formed** where p9 - the clause p5 and p7 exist to leave - chooses the second; 14.8.2.1 landed p2 and p4 and not p3, so a forwarding reference deduced a parameter no lvalue can bind and dropped out of the candidate set; that same deduction makes `T` a reference type, and 5.3.3p2 measured `sizeof(int &)` as 8 where both oracles say 4; and 14.5.6.1p5's new answer that two declarations declare one template made a specialization named *above* that definition reachable, which emitted a `declare function` and no definition - **a program that does not link**, over a call, a target type and a member alike, and a suite that compares LowIR and never links could not see it. Beside them, 14.5.6.1p5's own cost: the question was asked of every pair of declarations of one name, each pair substituting one head's parameters for the other's and, over a class template, instantiating a specialization to do it - quadratic in time and in memory, where the answer is a fact of one declaration | 223 → **227 / 299**, the four new tests being the four regressions these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 60 synthesized programs run through `dev/cppgm++`, the reference and g++ with every exit status now agreeing; declaring 512 overloads of one template name 0.36 s → 0.04 s and 44.8 MB → 15.8 MB; fifteen scaling shapes and the two that are 13.3p1's own quadratic; valgrind clean over all 299 fixtures; the pa1-pa19 report 10.2 s |
| C4 | the reading a template definition gets where it stands: 14.6p8's body read once at its own point in the PA11 dialect and again for each specialization, 14.7.1p1's naming made a declaration rather than a use, 14.6.1p6's redeclared template parameter, 9.2p1's member type declared twice, `FunctionReading` and `DialectReading`, and `sema_declaration.h`/`sema_function.cpp` split out | `fa07d078` | 5 / 5, all of them the reading's own edges - **the reading left something behind, and the two questions it added landed at some of the places a declaration binds a name and not the rest**: a template-id written in a definition being read makes a specialization, and it went onto the list that is not an inventory but what a *later* declaration is read for - 14.5.1.3p1's out-of-class member definition and the definition the template itself gets - so a specialization no instantiation asked for was completed and given a member, and where the member definition arrived after the reading it was completed from inside `instantiate_member` and the member was declared twice: **`m is defined twice` on a program both oracles compile**; 14.6p8's walk reached an expression written as a statement and stopped at every declaration's initializer, so `int y = nowhere;`, `for (int i = nowhere; ...)` and `S s = { nowhere };` were accepted; 3.4.2p2 was read as leaving *every* callee to the instantiation, where a fundamental type associates no namespace and `nowhere_at_all()` and `nowhere(1)` name what nothing declares; 14.6.1p6 reached a typedef, an alias, an object and a class-scope using-declaration and not a class, an enumeration, an elaborated declaration, a namespace-alias, an enumerator, a parameter, a function or a template's own name; and 9.2p1's question landed on the typedef-name side alone, so `typedef int A; struct A {};` and its enum and elaborated forms declared one name as two kinds of type in a class, a namespace and a block alike. Both oracles refuse the first three and the type-name over a typedef-name; g++ refuses the six 14.6.1p6 shapes the reference accepts, beside the two this checkpoint already refused | 235 / 301 → **242 / 308**, the seven new tests being the seven regressions these leave and the failing 66 the same 66; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 95 synthesized programs through `dev/cppgm++`, the reference and g++, with 19 of them compared as emitted LowIR and byte-identical to the reference; two units each naming a specialization no call instantiates emit only what they asked for, in either order; `object=` differences 9 → 7 tests and `binding=` 12 → 10, every survivor a test that already fails but the recorded unnamed-namespace one; every scaling shape where the checkpoint left it, including the two 13.3p1 quadratics and the exponential spelling; valgrind clean over all 308 fixtures; the pa1-pa19 report 10.4 s |
| C5 | the class a template makes of its own parameters: 14.6.1p1's current instantiation read once against a kept parameter region, 14.5.1.3p1's out-of-class member definitions read against the same two with 14.1p2's own head names, 14.6.2p1's dependent qualified name and 14.6.2p3's dependent base, 9.2p2's held bodies, 9.4.2p1's qualified class-head, 9.3.1p3's object parameter for a pattern, and 15.4p1 | `277a48bb` | 3 / 3 + 1 beside them, all of them **what the reading kept and what it did not keep**: 14.1p2 leaves each declaration of one template free to spell its parameters as it likes, and the names an out-of-class member definition's head wrote were bound in the one region the class was completed against - the region every reading of its members looks names up through - so they outlived the definition, and a head spelling those places in another order was refused there and read against the class's own spelling instead: `template<class U, class T> void A<U,T>::f() { U a; T b; }` over `A<int,char>` emitted `i8` and `i32` where the reference and g++ emit `i32` and `i8`, in a return type as much as in a body, and where two heads permute one another the second was `V does not name a type` on a program both oracles compile, and a name only the definition before it wrote was still standing; 9.2p2's held bodies were taken off the list and walked once, so a class declared *in* a body being read held bodies above the mark again that nobody ever read - `struct local { int reach() { return nowhere_at_all(); } }` in a function template was accepted - and `check_template_definition` never drained the list at all; and 14.6.2p1's dependent member name kept its spelling rather than the two facts the object file writes it from, so `typename T::car_type` was `T_` where **`reference-binaries/cppgm++` and g++ both write `NT_8car_typeE`**. Beside them, on the naming path the tier already owned: a specialization's argument list was spelled without the space a program writes, so a static data member of a two-argument one was `@A_int_char___v` against the reference's `@A_int__char___v` - a global name, which the comparison does not mask | 254 / 308 → **256 / 310**, the two new tests being the two regressions these leave and the failing 54 the same 54; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 73 synthesized programs through `dev/cppgm++`, the reference and g++, 56 of them compared as emitted LowIR with every comparison agreeing but the two the exception-specification gap owns; the dependent-member names byte-identical to the reference *and* to g++ at two and at three components; two units with differently-spelled member heads order-free; `object=` differences 7 and `binding=` 10, every survivor a test that already fails but the recorded unnamed-namespace one; eleven scaling shapes measured against a pre-C5 worktree build and each where the checkpoint left it, plus the 14.5.1.3p1 quadratic the checkpoint made reachable, on which the reference is slower; valgrind clean over all 308 fixtures under `pa19/tests` |
| C6 | what a definition takes from the other declarations of the same entity, and what it owes the object file: 9.4.2p3's in-class brace-or-equal-initializer as the value the definition's storage holds and as what a read of the member is, 8.3.5p10's parameter name made a fact of the function held beside the default-argument, 8.3.5p5's array and function parameters made the pointer objects they are, and 8.5p8's "holds nothing" read of the whole object | `955dce9f` | 1 found, 1 fixed: a declaration written below a *template's* definition renamed the parameter objects every specialization had already made, so `template<class T> T zero_of(T) {...}` followed by `template<class T> T zero_of(T sample);` emitted `%sample` where the reference emits `%__param0` - **a regression C6 shipped**, on a difference the suite compares, that no fixture had the shape for. 14.7.1p1 leaves a specialization a declaration nothing wrote, so the pattern's spelling is frozen where the definition giving it a body is read and only a program's own declaration puts an object on the waiting list.  Recorded, not fixed: `required_parameters` and `has_default_argument` still key the record by `function.id` where C6 moved the writer to `wrote_defaults`, which is vestigial today because 12.8p12 keeps special members out of the template tier | 262 / 311 -> **264 / 313**, the two new tests being the regression this fixes and the failing 49 the same 49 by name; pa1-pa18 1777 / 1777; file audit passes; every `.ref` regenerates byte-identically over all 313 fixtures; 30 synthesized programs through the pre-audit binary, `reference-binaries/cppgm++` and g++, of which C6 moved 15 onto the reference and 2 off it and all 17 now agree; the three left disagreeing are metadata the comparison strips and one the reference is alone on against g++; three scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build at about 2% more memory; valgrind clean over all 313 fixtures under `pa19/tests` |
| C6 audit | 8.3.5p10's parameter name made a fact of the function rather than of any one of its declarations: `ParameterRecord` split out of `Default` beside `HeldInitializer`, a definition's own declarator beating the record, the objects a definition left unnamed waiting for the first declaration to name them, and 14.7.1p1 narrowing which of the template's declarations a specialization is spelled from | `d6700f4a` | 2 / 2, both of them **the same fact asked at fewer places than have it**: 14.7.1p1's narrowing froze a specialization's spelling where the pattern's *definition* is read, where `reference-binaries/cppgm++` freezes it at the template's *first declaration* - over all 120 orderings of an unnamed declaration, two differently named ones, an unnamed definition and a call, this compiler disagreed on the **16 whose first declaration is the unnamed one**, per place as well as per function (`two(T, T b)` then `two(T a, T)` is `__param0, b` and not `a, b`), while **the same 120 orderings over an ordinary function agree in every one**; and the widened fact reached the declarator path and not the one 12.8p28's and 12.9p8's definitions take, which have no declarator and are handed one declaration's list - so `box::box(const box&) = default;` below a class that named the place, an out-of-class `= default` naming a place the class did not, and an inherited constructor whose base is defined below the using-declaration each wrote `__param1` where the reference writes the name, and `= default` outside the class recorded nothing at all, being the one declaration form that returns before `record_declared_parameters` | 266 / 315 -> **272 / 321**, the six new tests being the six shapes these leave and the failing 49 the same 49 by name; pa1-pa18 1777 / 1777; file audit passes; every `.ref` regenerates byte-identically over all 321 fixtures; 240 declaration orderings plus 55 shapes over members, constructors, operators, static members, array/reference/function parameters, default arguments, class templates, special members and inherited constructors, through this compiler, the pre-audit binary and `reference-binaries/cppgm++`, with all six regressions failing against the pre-audit binary; what is left disagreeing is emission order, an `alias object` line and the one place the reference binds a body's name to a parameter 3.3.4 ends elsewhere; four scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build within 1% of its memory; valgrind clean over all 321 fixtures; the build's two `-Wall` warnings gone |
| C7 | the type a declarator-id ends up under, and what lookup answers before a `<`: 8.3p1's constructor read from the level the id stands in, 14.2p3 asked of the overload set, 7.3.4p2's using-directive answered after every region the name is written inside, and 14.7.2's explicit instantiation with `object_root=yes` as the demand 3.2p3 has no use to point at | `c3f2411f` | 3 / 3, two of them **a rule landed at the question and not at the walk that answers it** and the third a fact carried as a terminal in a dump an earlier assignment is graded on: `declares_function` walks in to the declarator-id and asks which suffix it ends up under, and `declarator_type` - which is what *binds* a definition's places - still spent them on the outermost parameter-clause, so the two agree only where the level around the id wrote none.  `char (*getter(long key))(char value)` declared the right type (`_Z6getterl`) and emitted `function @getter(%value : i8)`: a body that names `key` was **refused on a program g++ compiles**, and one that names `value` - a place of the type `getter` *returns* - was accepted, over eight shapes counting the pointer, the array, the redundant parentheses, the exception-specification, the out-of-class member definition and the template head.  And the grammar's `explicit-instantiation-target` is a class-declaration *or a simple-declaration*, whose arm checked that a declarator existed and then `return`ed: `template int carried<int>(int);` emitted **nothing** where `reference-binaries/cppgm++` and g++ both emit the definition rooted - which four checked-in `.ref` files under pa22 and pa24 pin byte for byte - while `int slot; template int slot;` and `int plain(int); template int plain(int);` were **accepted** where both oracles refuse them.  And that form's node was the declaration's with `KW_TEMPLATE` hung on it, so the PA10 dump the shared AST feeds read `explicit-instantiation-declaration KW_TEMPLATE:template` where the reference writes `explicit-instantiation-definition` - and no pa10 fixture writes the form, so 1777 / 1777 stood over it.  What is right and swept clean: 14.2p3's overload set and 7.3.4p2's directive agree with both oracles over 15 shapes including the three that must not find the name, and 14.7.2p8's walk of the region a specialization opened agrees with **g++** at every depth, rooting all 40 definitions of a 40-deep member-class chain where the reference roots none | 283 / 326 -> **288 / 331**, the five new tests being five of the six shapes these leave and the failing 43 the same 43 by name; pa1-pa18 1777 -> **1778 / 1778** with the sixth, which is the pa10 dump; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 under `cppgm.tests/course/pa19`, diagnostics included; three cross-products through this compiler, the pre-audit binary and `reference-binaries/cppgm++` with g++ beside them - 20 declarator shapes now agreeing with g++ in every one and with the reference in twelve, 21 explicit-instantiation shapes agreeing with the reference symbol for symbol and root for root in twenty, and 15 lookup shapes agreeing everywhere; four later-PA `.ref` files that were missing `object_root=yes` now matching; a function returning a function pointer run through `lowir2cy86` to the value g++ builds it to return; two units in either order; seven scaling shapes at n = 32, 128 and 512 and the parenthesis nest measured to the 8000 the parser accepts; the PA10 dump byte-identical to the reference for both explicit instantiation forms; valgrind clean over all 332 fixtures |
| C8 | what a name in an instantiated body reaches, and which of a specialization's bodies an instantiation reads: 14.6.2p3's dependent base left off 3.4.1's chain for the specialization as well as for the definition, as a fact of the base-specifier the program wrote once; and 14.7.1p1's instantiation of the *declarations* of a class's members, each body held on `held_definitions_` for the use that names it | `43aa2aa0` | 2 / 2 + 1 beside them, and the two are **one shape - the deferral was landed at the three places a *use* stands and neither demand that has no expression behind it was given it**, over an output where a body nobody grants is a `declare function` and no definition, which is a program that does not link and a suite that compares LowIR and never links: 10.3p10's table was asked of the specialization's own declarations and not of the classes the same reading made, so `struct Inner { virtual int f(); }` nested in a class template emitted `@Outer_int___Inner__vtable` naming `@Outer_int___Inner__f` and **nothing defined it**, at one level and at three alike, where the `4ec3d164` binary and `reference-binaries/cppgm++` both do; and 14.7.2p1's explicit instantiation - the one declaration 3.2p3 has no use to point at - set `explicitly_instantiated` on a declaration whose body was still held, so `template int tester<int>::test();` over a member defined *in* its class emitted **nothing at all** where both of those binaries emit it with `object_root=yes`.  Beside them and older, the same clause asked of the innermost class alone: `template int tester<int>::probe::test();` was refused where the reference and **g++** both root it, because what says the declaration names a specialization was read off the class the prefix named rather than off the classes that class stands in.  What is right and swept clean: 14.6.2p3 over eight shapes - unqualified, `this->`, qualified, a non-dependent base under a template head, a nested class, an out-of-class definition, a non-template class deriving from a specialization and a class below one - picks the reference's callee in every one with g++ compiling all eight, and `deferred_conversion<incomplete>`, the clause the deferral is for, compiles here and is refused by `4ec3d164` | 294 / 331 -> **297 / 334**, the three new tests being the three shapes these leave and the failing 37 the same 37 by name; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; thirty use shapes - calls, `&`, conversion and operator functions, special members, uncalled and nested and inherited virtuals, all four 14.7.2 forms, subobject copy/assign/destroy, chained and cross-class bodies, target types, temporaries, arrays and two specializations - each swept for a symbol some entry names that no `function` line defines, all clean here and matching the reference's definition count where `43aa2aa0` leaves two unresolved; a rooted explicit instantiation run through `lowir2cy86` to the 42 g++ builds it to return; seven scaling shapes at n = 32, 128 and 512 against a `43aa2aa0` worktree build, unchanged but for the class nest 512 deep that returns to the pre-C8 0.40 s the pre-audit build bought with an unresolved symbol; valgrind clean over all 334 fixtures |
| C9 | the region a declarator's own places stand in, and the type an argument list reads: 3.3.7p1's function prototype scope with each place declared as its declarator-id is read, 5.1.1p3's `this` over a member declarator's trailing-return-type, 14.6.2.2p1's type-dependent decltype-specifier made a type of its own and answered by 14.7.1p1 reading the expression again, and 14.2's argument list read for a spelled decltype-specifier | `1b135271` | 3 / 3 + 1 beside them, and the three are **one shape - the type a decltype-specifier stands for was made a fact of the *reading* and not of the expression**: keyed by the AST node and the region's id, so one function written twice had two return types and `template<class T> auto added(T a, T b) -> decltype(a + b);` with the definition below a call emitted **`declare function @added` and no definition** - a program that does not link, over a suite that compares LowIR and never links - while 14.5.1.3p1's out-of-class member definition of the same shape was refused outright, both on programs the two oracles compile; and the second reading rebuilt every declaration the region had, so a place written *after* the specifier whose own type was a decltype over that region sent the substitution back into itself - `auto differ(T a, decltype(a - a) b) -> decltype(a - b)` was a **segmentation fault** at eight places as at two, where 3.3.7p1 begins a place's potential scope at its own declarator-id and the specifier could not have named it.  14.4p1 is the key both want: the specifier as written and the declarations the names it writes reach, with what each head *spelled* left out of both - 14.1p2 lets each declaration name its parameters as it likes and 14.4p1 makes a parameter equivalent to the one at the same position, which is what `-> decltype(T() + seed)` and `-> decltype(U() + seed)` need to be one specifier.  Beside them, 5.1.1p3 was asked of the region alone, so a **static** member's and a friend's declarator each got an object of the class - the decl-specifier-seq is what says, and both oracles refuse the static form.  And older, the other arm of the sentence C9's spelled form leans on: 7.1.6.2p4's unparenthesized class member access names an entity, so `-> decltype(box.slot)` emitted `-> ptr` where the reference and **g++** emit `-> i32`.  What is right and swept clean: 3.3.7p1's region over twelve shapes - a later type-id, a default argument, a trailing-return-type, an inner clause, an unnamed place, 3.3.2p6's class - agreeing with g++ in every one and never escaping the declarator | 304 / 334 -> **310 / 340**, the six new tests being the six shapes these leave and the failing 30 the same 30 by name; pa1-pa18 1778 / 1778; the file audit passes and did not before - `sema_template.cpp` reached **3029 lines against the limit of 3000**, so 8.1p1's reading of a type-id out of a spelling is `sema_type_id.cpp` - and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; 52 synthesized shapes through this compiler, the `1b135271` pre-audit and `e067d2e9` pre-C9 builds and `reference-binaries/cppgm++` with g++ beside them, 42 of them compared as emitted LowIR and identical to the reference in 41, the survivor a reference member's initializer the pre-audit binary differs on identically; a unit writing all three forms run through `lowir2cy86` to the 42 g++ builds it to return; two units defining one such specialization order-free; seven scaling shapes at n = 32, 128 and 512 against a `1b135271` worktree build, unchanged but for n declarations of one template 0.04 s -> 0.02 s, and the quadratic the fix opens measured to n = 512 where the pre-audit binary segfaults at 8; valgrind clean over all 340 fixtures |
| C10 | the region a qualified declarator-id declares into, the member a member template declares, and the arguments a template-id left out: 9.4.2p1's and 3.4.1p8's class-head-name and declarator-id recording their pattern where the name reaches with 14.1p1's own head standing inside it, 3.4.3p1's prefix walked component by component, 14.5.2's member template given 9.3.1p3's object parameter, 14.8.1p2's partly written argument list made a declaration of its own, and 8.2p7's `T (X)` | `0b3f72b8` | 7 / 7, and six of them are **one shape - the object parameter a member template now carries has readers the checkpoint did not give it, and 14.8.1p2 has a third**: `declares_static_member` asks 13.1's index of the class's chain, which is keyed by the parameter list *as written*, and 14.1p2 gives each head parameters of its own - so 9.4.1p2's `static` was read as unwritten and `template<class T> int counter::raised(T)` over a static member was **`a definition of raised matches no declaration of it`** on a program both oracles compile, while 14.7.1p1's second reading of the same definition could ask neither that index nor 14.5.6.1p5's signature - the region an instantiation stands in binds arguments and not parameters - and emitted `function @counter__raised(%this : ptr, ...)` for a member 9.4p1 calls on no object.  And 14.5.6.2's ordering refused every pair whose `object_member` differed, which before this checkpoint was never two member templates: a static and a non-static member template of one name called through an object is **`a call of r has no best declaration` where the pre-C10 `e7eb1c1a` build, the reference and g++ all choose `r(T *)`** - a regression C10 shipped - and the same guard left 13.5p6's member operator template, which this checkpoint is what put in the candidate set, unordered against the non-member beside it, where 14.5.6.2p2 gives the one non-static member a first parameter of "reference to cv A" and 13.3.1p4 gives the static one an implicit one that matches any object.  Beside them 14.8.1p2's other arm - a trailing argument omitted where it is **obtained from a default template-argument** rather than deduced - was never landed at all, `record_function_template` recording `nullptr` for every default; 3.4.3p1's new prefix walk looked each component up in `named->scope`, which a typedef-name naming a class has none of, and began at `ctx.scope` for a name written `::v::S<T>::f`, both of which `resolve_prefix` already answers; and 14.7.2p1's explicit instantiation matched the written type against the *template's* own, so `template int mix<char>(double);` named no specialization where the arm beside it already deduces with `deduce_target`.  What is right and swept clean: `StandingIn` over eight qualified declarator-id forms and 8.2p7 over twelve places, agreeing with g++ in every one and with the reference wherever it compiles the shape at all | 322 / 346 -> **327 / 351**, the five new tests being five of the shapes these leave and the failing 24 the same 24 by name; pa1-pa18 1778 / 1778; the file audit passes and the build prints nothing, with `sema_analyzer.h` at **2391 against the limit of 2400**; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 27 checked-in under `cppgm.tests/course/pa19`; 96 synthesized shapes through this compiler, the `0b3f72b8` pre-audit and `e7eb1c1a` pre-C10 builds and `reference-binaries/cppgm++` with g++ beside them, 80 compared as emitted LowIR and identical to the reference in all 80, with **no shape in the sweep accepted by both oracles and refused here** and six the reference alone refuses; one unit writing all six rules run through `lowir2cy86` to the 42 g++ builds it to return, and the same unit less the line the reference refuses byte-identical to its LowIR; two units order-free and `object=` for `object=`; nine scaling shapes at n = 32, 128 and 512 against a `0b3f72b8` worktree build, every one where the checkpoint left it within 1% of its memory, and the one the audit makes compilable at 0.63 s at n = 512 against 1.17 s for the same call count the pre-audit binary already compiled and **21.01 s at n = 128** in the reference; valgrind clean over all 351 fixtures |
