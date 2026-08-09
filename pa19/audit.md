# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C7, reviewed at `c3f2411f`** — 8.3p1's constructor read from the level the
declarator-id stands in, so 8.4p1's `T (&f(P))[2] {}` is a function definition;
14.2p3 asked of the overload set rather than of the last declaration of a
spelling; 7.3.4p2's using-directive answered after every region a name is
written inside; and 14.7.2's explicit instantiation, with `object_root=yes` as
the demand 3.2p3 has no use to point at.

Three of the four are right and swept clean.  20 declarator shapes, 15 lookup
shapes and 21 explicit-instantiation shapes through this compiler,
`reference-binaries/cppgm++` and g++ leave 14.2p3's overload set and 7.3.4p2's
directive agreeing with both oracles everywhere, and 14.7.2p8's walk of the
region a specialization opened agreeing with **g++** at every depth - 40 nested
member classes root 40 definitions here and 40 weak symbols in g++, where the
reference roots none.

**Three blockers.  Two are the same shape - the rule was landed at the question
and not at the walk that answers it - and the third is a fact carried as a
terminal in a dump an earlier assignment is graded on.**

### Findings

**1. 8.3p1's constructor was read at the level the id stands in for
`declares_function` and at the outermost level for the walk that binds the
places.** `declares_function` walks in to the declarator-id and asks which
suffix it ends up under; `declarator_type`, which is what actually *binds* a
definition's parameter objects, still spent `declared` on the outermost
parameter-clause and handed the nested declarator nothing.  The two agree only
where the level around the id wrote no clause - the `T (&f(P))[2]` C7 shipped -
and disagree wherever it wrote one.  `char (*getter(long key))(char value)` is
the function `(long key)` makes, and this compiler declared its type correctly
(`_Z6getterl`) while emitting `function @getter(%value : i8)`: the body could
name `value`, which is a place of the function type `getter` *returns*, and
could not name `key`, which is its own.  `int (*getter(int key))(int) { return
key > 0 ? picked : 0; }` was **refused on a program g++ compiles**, and so were
`int (*f(int a))[2]`, `int (*(f(int a)))(int)`, `int (**f(int a))(int)` and the
same four under a template head and as out-of-class member definitions.
`declarator_type` now asks the same question `declares_function` does -
`takes_enclosing_suffix` says whether a level hands its id up - so the clause
that spells the places is the first suffix at the id's own level, or the one the
level around it wrote where its own wrote none.  `T (f)(P)` still spells `P`.

**2. 14.7.2p1's other target was a `return`.**  The grammar's
`explicit-instantiation-target` is a class-declaration *or a
simple-declaration*, and the simple-declaration arm checked that a declarator
existed and then did nothing at all - so `template int carried<int>(int);` was
accepted and emitted nothing where `reference-binaries/cppgm++` **and g++** both
emit the definition rooted, and `int slot; template int slot;` and
`int plain(int); template int plain(int);` were **accepted** where both oracles
refuse them.  Four checked-in `.ref` files under pa22 and pa24 pin the first
half of that byte for byte.  The arm now reads the declaration for the type it
writes and looks the specialization up by it: 14.8.1's explicit argument list
where one is written, 14.8.2.2's deduction from the declared type where none is,
and the member a class template specialization already made where the prefix
names one.  9.3.1p3's object parameter is part of what a declaration says, so
both spellings of the type are built and each candidate is asked with the one
its own declaration carries.

**3. The definition form was spelled by hanging a keyword on the declaration
node.**  14.7.2p1's `template struct box<int>;` is the same target
`extern template` writes with one keyword fewer, and C7 carried that keyword as
the node's own terminal - so the PA10 dump read
`explicit-instantiation-declaration KW_TEMPLATE:template` where the reference
writes `explicit-instantiation-definition`, and `explicit_instantiation` asked
`node.token` to tell the two forms apart.  The shared AST is PA10's output and
no pa10 fixture writes the definition form, so 1777 / 1777 stood over a dump
that differs from the oracle's at the top of the declaration.  Whether this unit
owes the definitions is a fact about the declaration rather than a terminal
inside it, so each form is a node of its own; the dump is now byte-identical to
the reference for both, and the regression test is a pa10 course fixture,
because that is where the AST is graded.

### Changes

| what | where |
| --- | --- |
| 8.3p1's binding clause read at the level the declarator-id ends up at | `sema_declarator.cpp` |
| 14.7.2p1's simple-declaration target read and resolved | `sema_template.cpp`, `sema_analyzer.h` |
| 14.7.2p2's refusal where that declaration names no specialization | `sema_template.cpp` |
| 8.5p16's `WrittenInitializer` moved to the layer that owns it | `sema_declaration.h`, `sema_lifetime.cpp` |
| 14.7.2p1's definition form given a node of its own | `ast_model.h`, `ast_parser.cpp`, `ast_parser_declarator.cpp`, `sema_analyzer.cpp` |

Five regression tests, one per shape:
`100-explicit-instantiation-function-template`,
`100-explicit-instantiation-deduced-function-template`,
`100-explicit-instantiation-member-of-specialization`,
`100-explicit-instantiation-names-an-object-bad`,
`100-explicit-instantiation-non-template-function-bad`, and a sixth under
`cppgm.tests/course/pa10`, `200-explicit-instantiation-definition`, for the dump.
All six fail against the pre-audit binary built from `c3f2411f`.  The declarator finding has no
fixture and cannot have one: the reference refuses every shape that tells the
two readings apart, and the parameter name it would disagree on is one the
comparison does not strip.

### Performance Evidence

Seven shapes over the surface this audit owns, `--emit-lowir -O0`, n = 32, 128
and 512, each timed twice against the pre-audit binary built from `c3f2411f` in
a worktree with `make build`:

| shape | pre-audit | now |
| --- | --- | --- |
| n ordinary function definitions | 0.00 / 0.01 / 0.02 s | 0.00 / 0.01 / 0.02 s |
| n definitions of `int (*f(int))(int)` | refused | 0.00 / 0.01 / 0.03 s |
| one declarator under n redundant parentheses | 0.00 / 0.00 / 0.01 s | 0.00 / 0.00 / 0.01 s |
| the same over `int (&f(int))[2]` | 0.00 / 0.00 / 0.01 s | 0.00 / 0.00 / 0.01 s |
| n explicit instantiations of one template over n classes | 0.00 / 0.01 / 0.03 s | 0.01 / 0.02 / 0.07 s |
| n explicit instantiations of one specialization | 0.00 / 0.00 / 0.01 s | 0.00 / 0.00 / 0.01 s |
| n members of one specialization, each explicitly instantiated | 0.00 / 0.01 / 0.02 s | 0.01 / 0.01 / 0.04 s |

`takes_enclosing_suffix` walks inward from one declarator level, and only 8.4p1's
function definition has places to spell - so it is asked once per definition
rather than once per level, because the level that spells them takes the places
away from the ones inside it.  The shape that does ask it at every level is n
parentheses around a pointer-returning definition, and it is measured to the
depth the parser accepts: 0.03 / 0.11 / 0.42 / **1.72 s** at n = 1000, 2000,
4000 and 8000, against 0.41 and 1.76 s for the same nest with no pointer in it,
which asks it once.  The walk is inside the quadratic the AST walk above it
already is, and 16000 is not a translation unit.  The three explicit
instantiation shapes are larger than the pre-audit build's because it did
*nothing* for them; each is linear, and the reading is one per declaration.  The two shapes
the plan records as superlinear are where C7 left them: `typedef P<t,t>`
eighteen deep is 0.65 s before and after, and 128 out-of-class member
definitions over 128 specializations 0.46 s before and after.

### Validation

- **1777 / 1777 -> 1778 / 1778** through pa18, the new test being the pa10 dump
  fixture, and pa19 **283 / 326 -> 288 / 331** with the other five, the
  failing 43 the same 43 by name.
- **File audit passes** for pa19 over `dev/src` with the five header-weight
  warnings the shared headers have carried since PA18, and no suppression.  The
  build prints nothing.  `sema_analyzer.h` is 2383 lines against the audit's
  2400: this checkpoint added nine and moved sixteen out, as C5's split did.
- **Every `.ref` regenerates byte-identically** over all 319 fixtures under
  `pa19/tests` and all 12 under `cppgm.tests/course/pa19`, diagnostics included;
  no committed fixture moved.  The five new pa19 fixtures and the pa10 one are
  the reference's own output.
- **Three cross-products through three compilers.**  20 declarator shapes -
  parenthesized ids, pointers and references to functions and arrays, member and
  template and trailing-return forms - where **every one now agrees with g++**
  and eight disagree with the reference, each of them a program the reference
  cannot find the parameter name in.  21 explicit-instantiation shapes, of which
  **20 agree with the reference symbol for symbol and root for root**, including
  the four forms both refuse and 14.8.1's two spellings of the argument list.
  15 lookup shapes over 14.2p3's overload set and 7.3.4p2's directive, all three
  compilers agreeing on every one, including the three that must *not* find the
  name.
- **Four checked-in later-PA `.ref` files** - `300-explicit-instantiation-func`
  `tion`, `-free-function-emits-definition`, `-static-member-function` under
  pa22 and `100-explicit-instantiation-after-explicit-specialization-no-effect`
  under pa24 - go from missing `object_root=yes` to matching this compiler's
  emitted symbols exactly.
- **The PA10 dump is byte-identical to the reference** for both explicit
  instantiation forms and over 58 of the 59 shapes this audit swept, the
  exception being a trailing-return-type spelling PA10 has always left out.
- **Run evidence with scalars.** `int (*picker(int key))(int)` returning a
  function pointer, lowered here and run through `lowir2cy86` and `cy86`,
  returns 42, which is what `g++` builds it to return; it did not compile at all
  before.
- **Two units, both orders.** An explicit instantiation in one unit and a call
  in the other emit the same symbols and the same root as the reference in each
  order.
- **Valgrind clean** with `--error-exitcode` over all 331 pa19 fixtures and the
  pa10 dump fixture.

## Open Gaps

**A trailing-return-type in a function declarator is dumped without its
spelling.**  Found by putting all 59 shapes this audit swept through
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

**A member function template is half in this tier.**  Its declaration in a class
is read and 14.7.2p1 now names its specializations, but
`template<class E> int graph::search(E const&) const { }` written outside the
class is `no declaration of E is in scope`, and `s.f<int>(0)` is not a
translation unit at all - the explicit argument list after a member name does
not parse.  Both oracles compile both.  It predates this checkpoint (the
`c3f2411f` binary refuses the same two) and the seam is the member-template
head rather than 14.7.2's, which is why
`pa22`'s `300-explicit-instantiation-deduced-member-function-template` is the
one explicit-instantiation fixture in the later assignments this compiler still
cannot read.  Beside it, 9.3.1p3's object parameter is on an ordinary member
function's recorded type and not on a member function *template*'s, which is
what makes the two spellings `instantiation_named` asks with necessary.

**14.7.2p5's second explicit instantiation is unasked.**  `template int f<int>
(int);` written twice is ill-formed and **g++ refuses it**; this compiler and
`reference-binaries/cppgm++` both accept it and emit one rooted definition.  It
is a rule of its own rather than a reader of the one this audit landed, and no
fixture through pa24 writes it.

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
a class *is* in this model rather than to what a rule asks. The names this audit
does settle are the ones the definition's own head wrote, which is what decides
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
- 0.73 s at n = 128, against `reference-binaries/cppgm++`'s 1.00 s. 14.7.1p1
instantiates the *declarations* a class needs and leaves each definition to the
use that requires it, which is what would make the cost the program's uses
rather than its square; the declarations here come from the class body and the
definitions are what `instantiate_member` reads, so the change is which of the
two `complete_specialization` drives.

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
defaulted second parameter emit identical LowIR either way. It is left as one
key per reader rather than unified because C8 owns 14.7.1p1's instantiated
declarations, which is what would make a member's two keys differ.

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
another reason, which the six this audit adds do.

**Two places the reference is alone, found sweeping 8.3.5p10.** It binds a body's
name to a parameter place 3.3.4 ends at another declarator: in
`int chosen = 7; int pick(int) { return chosen - 2; } int pick(int chosen);` the
reference loads the parameter slot where this compiler and `g++ -std=c++11` both
load the global, and the reference's program returns the wrong value. And it
writes `binding=strong` for an explicit specialization of a function template
where this compiler writes `weak`, which the comparison strips. Neither can be a
committed fixture and neither is C6's.  The first survives the 240-ordering
cross-product this audit ran, where it is the only shape of an ordinary
function's parameter names the two compilers disagree about.

**12.9p1's inherited constructor keeps its arity in both other compilers.** For
`base_of<int>(int, int = 0)` inherited through `using base_of<int>::base_of;`,
`reference-binaries/cppgm++` and `g++ -std=c++11` both emit a two-parameter
`derived::derived` and apply the default at the call, where this compiler emits
the one-parameter candidate 12.9p1 forms by omitting the trailing defaulted
parameter and 12.9p3 gives no default argument to. The programs agree on their
result and the difference is in `object=`, which the comparison strips - but two
oracles against one makes it a defect rather than a reading, and it is C8's
because 14.7.1p1's instantiated member declarations are what the candidate set is
formed from. Pre-existing: the pre-audit binary emits the same.

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
