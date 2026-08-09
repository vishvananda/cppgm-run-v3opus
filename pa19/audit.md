# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C4, reviewed at `fa07d078`** — the reading a template definition gets where it
stands: 14.6p8's body read once at its own point in the PA11 dialect and again
for each specialization, 14.7.1p1's naming made a declaration rather than a use,
14.6.1p6's redeclared template parameter, 9.2p1's member type declared twice,
`FunctionReading` and `DialectReading` over the three readings of one body, and
`sema_declaration.h` and `sema_function.cpp` split out.

The increment's shape is right and its split is code motion and nothing else:
of the eight functions that left `sema_analyzer.cpp`, seven arrived in
`sema_function.cpp` byte for byte, and the one that changed is
`function_definition`. `FunctionReading` holds a superset of what the two body
readings used to put aside by hand - it adds `lifetimes_`, which
`function_definition` never saved, and 6.6.4p1's labels and gotos, which it
cleared and never gave back - so a specialization named in the middle of a body
is now read as a body of its own by construction rather than by the two lists
happening to agree. `checking_` is a depth and `DialectReading` a scope guard,
so the reading unwinds correctly through the exception every `-bad` fixture
throws. The pa1-pa18 baseline is untouched by all of it.

**The blockers are the reading's own edges.** 14.6p8's walk reached the
statements of a body and stopped at the declarations in it; 3.4.2p2 was read as
leaving *every* callee to the instantiation; 14.6.1p6 and 9.2p1 each landed at
some of the places a declaration binds a name and not at the rest; and the
reading, which the plan called one that leaves nothing behind, left a
specialization on the list two later declarations are read for.

### Findings

**1. The reading left a specialization behind, and two later declarations were
read for it.** A template-id written in a definition being read makes a
specialization - it has to, because `A<int> *` needs a type - and
`instantiate_class` put it on `TemplateInfo::specializations`. That list is not
an inventory: it is what a declaration arriving *later* is read for, in both of
14's places - 14.5.1.3p1's out-of-class member definition is read for every
specialization already made, and so is the definition the template itself gets.
So a specialization no instantiation ever asked for was completed by the second
and given the member by the first. Where the member definition arrived after
the reading, `instantiate_member` resolved `A<T>::` against an incomplete
specialization, that resolution completed it, and the completion walked
`info.members` with the arriving pattern already on it - so the member was
declared twice and `template<class T> struct A { T v; int m(); };` followed by a
template body naming `A<int>` and then `template<class T> int A<T>::m()` was
**`m is defined twice`**, on a program both oracles compile. The list now holds
the specializations an instantiation asked for; `require_specialization` is
where one joins it and where it is completed, and 14.6p8's reading leaves the
declaration in the model and nothing on any list.

**2. The reading read the statements of a body and not the declarations in it.**
An initializer is an expression of the definition exactly as the operand of a
`return` is, and 3.3.2p1 puts its names after the declarator it belongs to - but
the walk reached expressions only where a statement was one, so `int y =
nowhere;`, `for (int i = nowhere; ...)` and `S s = { nowhere };` were each
accepted in a body no argument list could make valid. Both oracles refuse all
three.

**3. 3.4.2p2 left every callee to the instantiation, including the ones it
reaches nothing for.** The namespaces a call searches beyond ordinary lookup are
the ones its arguments' *types* are associated with, and 3.4.2p2 associates
none with a fundamental type - so `nowhere_at_all()` and `nowhere(1)` name
nothing any argument list could declare, and both oracles refuse them. A call
with no arguments, or with none but literals, is now looked up here; anything
else is left where 14.6.2p1 leaves it, and only 3.4p1's first question is asked,
because 5.2.3p1's explicit type conversion writes a type-name in the same place.

**4. 14.6.1p6 landed at three of the places a declaration binds a name.** The
rule reached a typedef, an alias-declaration, an object and a class-scope
using-declaration. It did not reach a class, an enumeration, an
elaborated-type-specifier, a namespace-alias, an enumerator, a parameter or a
local function declaration - each of which binds a name in a region nested in
the template head. `reference-binaries/cppgm++` and g++ **both** refuse the
class, the enumeration, the elaborated declaration and the namespace-alias; g++
refuses the other three where the reference accepts them, exactly as it already
accepts the `typedef int T` and the `int T` this checkpoint refuses. The
question is now asked wherever a declaration binds a name - and once where no
region can be asked, because a template's own declared name is bound before its
head is read and a class template's parameter region is never opened at all, so
`record_template` asks the head it just read. A using-declaration at namespace or
block scope is the one that is *not* asked: 7.3.3p1 makes it a declaration of
what another region declared rather than a name of its own, and both oracles
accept `using N::T` under a head that declared `T`.

**5. 9.2p1's question landed on one side of itself.** `declare_type_alias`
refused a typedef-name a class already declares; nothing refused a class-name
or an enum-name standing where a typedef-name of that spelling already does.
7.1.3p3's leniency is a typedef-name's alone - what 3.3.10p2 binds a class-name
as is the tag an elaborated-type-specifier reaches - so `typedef int A; struct A
{};`, `typedef int A; enum A { q };` and `typedef int A; struct A;` each
declared one name as two kinds of type and compiled, in a class, in a namespace
and in a block alike. Both oracles refuse all of them. `declare_type_name` is
the one place a class-name and an enum-name are asked this and 14.6.1p6
together, which is the same pairing `declare_type_alias` already had.

### Changes

| what | where |
| --- | --- |
| 14.7.1p1's list of the specializations an instantiation asked for, which a later declaration is read for | `sema_template.cpp`, `sema_analyzer.h` |
| 14.6p8 over the initializer of a declaration written in a body | `sema_analyzer.cpp` |
| 3.4.2p2's callee, looked up where the arguments associate nothing | `sema_template.cpp`, `sema_analyzer.h` |
| 14.6.1p6 at the type-name, enumerator, parameter, function, namespace-alias and template-name bindings | `sema_analyzer.cpp`, `sema_function.cpp`, `sema_template.cpp`, `sema_analyzer.h` |
| 3.3.10p2 and 9.2p1's type-name over a typedef-name, as `declare_type_name` | `sema_analyzer.cpp`, `sema_analyzer.h` |

Seven regression tests: `300-specialization-named-only-in-a-template-definition`,
`300-specialization-asked-for-after-its-member-definition`,
`300-template-definition-initializer-names-nothing-bad`,
`300-template-definition-callee-associates-nothing-bad`,
`300-local-class-redeclares-template-parameter-bad`,
`300-class-name-declared-over-typedef-name-bad`,
`300-member-enum-declared-over-member-typedef-bad`.

### Performance Evidence

Twelve shapes, each timed twice, `--emit-lowir -O0`, n = 32 to 512. Nothing
this audit added is more than a hash probe per declaration and a walk of the
syntax already being read, and every shape stayed where the checkpoint left it:
n distinct specializations 0.00 -> 0.07 s; one specialization named n times
0.00 -> 0.02 s; n specializations of one class template over n classes 0.01 ->
0.12 s; n function templates each an 8-statement body of initializers, none of
them called, 0.00 -> 0.05 s - which is what says the initializer reading is
linear in the source; n template bodies each calling a name through literal
arguments 0.00 -> 0.01 s; n nested blocks under a template head 0.00 -> 0.01 s;
a function template of n parameters 0.00 s throughout; n declarations of one
template name 0.00 -> 0.02 s; n classes declared in one region 0.00 -> 0.01 s.

The two quadratic shapes are 13.3p1's own and are unchanged: n function
templates overloading one name, each called once, 0.01 -> 0.24 s, and n target
types each choosing among n function templates, 0.00 -> 0.16 s. The exponential
one is unchanged too and is still the spelling: 0.01 s, 0.15 s, 0.62 s and
2.54 s at n = 12, 16, 18 and 20.

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **235 / 301 -> 242 / 308**,
  the seven new tests being the seven regressions these findings leave. The
  failing 66 are the same 66. The whole pa1-pa19 report is **10.4 s**.
- **File audit passes** for pa19 over `dev/src`, with the five header-weight
  warnings the shared headers have carried since PA18 - and no suppression.
- **Every checked `.ref` regenerates byte-identically** from
  `reference-binaries/cppgm++` through `make ref-test`; no committed fixture
  moved.
- **The differential probe both oracles answer.** 95 synthesized programs over
  the paths this checkpoint owns - every place a declaration binds a name under
  a template head, every place a typedef-name and a type-name can meet, every
  statement an undeclared name can hide in, ADL through a concrete and through a
  dependent argument, and the four orders a specialization, its template's
  definition and its out-of-class member can be written in - compiled by
  `dev/cppgm++`, by `reference-binaries/cppgm++` and by g++. Every disagreement
  above was found this way. What remains is where the *reference* stands alone:
  it refuses a callee ADL does find through a concrete class argument, which g++
  and this compiler accept, and it accepts the 14.6.1p6 redeclarations g++ and
  this compiler refuse.
- **The emitted LowIR is the reference's, byte for byte**, on all nineteen
  programs the specialization finding is about, including the four write orders
  and a class template whose own body names its specialization.
- **Multi-unit.** Two units each naming one specialization in a body no call
  instantiates and asking for the other emit exactly the one they asked for,
  are byte-identical to the reference, and are canonically identical in both
  unit orders.
- **The metadata the comparison strips, swept again.** `object=` over every
  fixture both compile differs from the reference on **7** tests and `binding=`
  on **10**, where the C3 audit left 9 and 12 - and every one of them is a test
  that already fails, except the one that is the recorded unnamed-namespace
  divergence. No passing test hides a name or a linkage this checkpoint changed.
- **Valgrind clean** with `--error-exitcode` over all 308 fixtures and over the
  95 probe programs.

## Open Gaps

**Recorded, not defects.** A specialization of a template declared in
7.3.1.1p1's unnamed namespace binds `internal` here and `weak` in the
reference; 3.5p4 gives every name in that region internal linkage and **g++
emits it local**, so the reference stands alone. An instantiated constructor
emits both of 12.1's entry points where the reference emits only the
complete-object one; **g++ emits both**, and the reference's own non-template
out-of-class constructor gets both - so this is the reference's rule for
instantiated definitions, and matching it is what turned three fixtures green.

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
