# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**The C6 audit, reviewed at `d6700f4a`** — 8.3.5p10's parameter name made a
fact of the function rather than of any one of its declarations: the record each
declaration already leaves about a parameter (`ParameterRecord`, split out of
the old `Default` beside `HeldInitializer` in `sema_declaration.h`) carries the
name, a definition's own declarator beats it, the objects a definition left
unnamed wait on the record for the first declaration to name them, and 14.7.1p1
narrows which of the template's declarations a *specialization* is spelled from.

The shape is right and the split is the one the data wanted. The name a place is
spelled with is not a fact of a declaration - no two declarations of one
function need agree about it and one may write none - so the only structure that
answers what the object file writes is a fact of the function held at the place
its type gives. The waiting list is the same fact read the other way round: a
declaration that first names a place may stand below the definition that already
made the object, and the object is the only thing left to carry the spelling to.

**Two blockers, both of them the same fact asked at fewer places than have it.**
14.7.1p1's narrowing landed at one of the two events that could freeze a
specialization's spelling, and the widened fact reached the declarator path and
not the path a definition the standard writes takes.

### Findings

**1. A specialization is spelled from the template's *first* declaration, not
from the first one that named the place.** The C6 audit froze the pattern's
spelling where the definition giving it a body is read. Swept over all 120
orderings of an unnamed declaration `U`, two differently named ones `A`/`B`, an
unnamed definition `D` and a call `C`, this compiler disagrees with
`reference-binaries/cppgm++` on the **16 whose first declaration is `U`** -
every one where a name arrives after an unnamed declaration and before or after
the definition alike. `template<class T> T zero(T); template<class T> T
zero(T alpha); template<class T> T zero(T) { return T(); }` emitted `%alpha`
where the reference emits `%__param0`, and the rule is per *place*:
`two(T, T b)` followed by `two(T a, T)` is `__param0, b` in the reference and
was `a, b` here. The **same 120 orderings over an ordinary function agree in
every one**, so first-namer-wins is right for a function the program declares
and wrong for a specialization, which is a declaration nothing wrote. The freeze
is now where the template is first declared, and `check_template_definition`
owns none.

**2. A definition the standard writes was handed one declaration's names and
asked nothing else.** 12.8p28's and 12.9p8's definitions have no declarator of
their own, so `write_definition` builds their objects from the parameter list
some declaration wrote - and a place *that* declaration left unnamed came out
`__param1` where the reference spells it with the name another declaration of
the same function gave. Three shapes, the reference on the other side of each:
`box::box(const box&) = default;` below a class body that named the place;
an out-of-class `= default` naming a place the class did not, where the
definition's own name is what wins; and 12.9p8's inherited constructor whose
base constructor is *defined below* the using-declaration that inherited it, so
the snapshot `inherit_constructor` takes predates the name. `= default` written
outside the class also recorded nothing, being the one declaration form that
returns before `record_declared_parameters`.

The fix asks the record once, in `write_definition`, which runs after the whole
unit has been read - and asks the *base's* record too where the constructor is
an inherited one, so the answer no longer depends on where the using-declaration
stands. Which of the two spellings the record holds is this function's is one
question with two readers now, so `spelled_for` answers it for the declarator
path and for this one alike.

### Changes

| what | where |
| --- | --- |
| 14.7.1p1's pattern spelling frozen where the template is first declared | `sema_analyzer.cpp`, `sema_declaration.h` |
| the freeze `check_template_definition` no longer owns | `sema_template.cpp` |
| 8.3.5p10 asked where a definition the standard writes makes its objects | `sema_analyzer.cpp`, `sema_analyzer.h` |
| `= default` outside the class recording what its declarator spelled | `sema_class.cpp` |

Six regression tests, one per shape:
`300-a-templates-first-declaration-spells-its-places`,
`300-defaulted-member-definition-takes-the-declared-name`,
`300-defaulted-definition-names-the-place-the-class-did-not`,
`300-defaulted-definition-outspells-the-class-declaration`,
`300-defaulted-assignment-definition-takes-the-declared-name`,
`300-an-inherited-constructor-takes-the-bases-later-name`.  All six fail against
the pre-audit binary built from `d6700f4a`.

### Performance Evidence

Four shapes over the surface this audit owns, `--emit-lowir -O0`, n = 32, 128
and 512, each timed twice against the pre-audit binary built from `d6700f4a` in
a worktree with `make build`: n functions each defined with an unnamed place and
named by a declaration below, 0.00 / 0.00 / 0.02 s before and after; n
declarations of one function template then a definition and a call,
0.00 / 0.00 / 0.01 s before and after; n classes each with an out-of-class
defaulted copy constructor, all copied, 0.01 / 0.03 / 0.13 s before and after; n
derived classes each inheriting one base constructor and building one object,
0.00 / 0.02 / 0.08 s before and after.  Peak RSS is within 1% of the pre-audit
build at every point, and `reference-binaries/cppgm++` is 0.60-1.30 s on all
twelve.

Nothing here is asked more than once per definition. The freeze became one
assignment at the first declaration of a place, where it had been a pass over
the whole record at every template definition; `name_recorded_parameters` is one
walk of the parameters of a definition that is about to be written, and it is
the *only* walk - `inherit_constructor` keeps taking its snapshot and no longer
tries to name it.

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **266 / 315 -> 272 / 321**
  with the six new tests, the failing 49 the same 49 by name.
- **File audit passes** for pa19 over `dev/src` with the five header-weight
  warnings the shared headers have carried since PA18, and no suppression.
  `sema_analyzer.h` is 2388 lines against the audit's 2400.
- **Every `.ref` regenerates byte-identically** over all 321 fixtures through
  `make ref-test-pa19`; no committed fixture moved.
- **The order cross-product, run twice.** All 120 orderings of `U`, `A`, `B`,
  `D` and `C` over a function template and all 120 over an ordinary function,
  through this compiler and `reference-binaries/cppgm++`: 16 template orderings
  disagreed and 0 ordinary ones; all 240 agree now. Beside them 40 shapes over
  members, constructors, operators, static members, array and reference and
  function parameters, namespaces, default arguments, class templates and
  two-place templates, and 15 over the special members and inherited
  constructors, each through both compilers.
- **What is left disagreeing is not the name.** `s07`/`s08`'s difference is
  emission order and an `alias object` line, both of which the comparison
  strips; `s29` is the reference binding a body's name to a parameter place
  3.3.4 ends at another declarator, which g++ and this compiler both read as the
  global and which the C6 audit already recorded. 12.9p1's inherited-constructor
  arity and the entry points an instantiated constructor owes are unchanged and
  still C7's.
- **Valgrind clean** with `--error-exitcode` over all 321 fixtures under
  `pa19/tests`.
- **The build prints nothing.** Two `-Wall` warnings C4 left - a reference bound
  to `declare_type_alias`'s result and read nowhere, and four members
  initialized out of declaration order - are gone, so the next one that appears
  is about the change that made it.

## Open Gaps

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
key per reader rather than unified because C7 owns 14.7.1p1's instantiated
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
oracles against one makes it a defect rather than a reading, and it is C7's
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
