# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C6, reviewed at `955dce9f`** — what a definition takes from the other
declarations of the same entity, and what it owes the object file: 9.4.2p3's
in-class brace-or-equal-initializer read as the value the definition's storage
holds and, through 3.2p3, as what a read of the member *is*; 8.3.5p10's
parameter name made a fact of the function, held beside the default-argument
each declaration already records, first-namer winning and the definition beating
both, and asked of 12.1p1's constructor too; 8.3.5p5's array and function
parameters made the pointer objects they are, with the cv-qualifiers the clause
drops kept on the object; and 8.5p8's "holds nothing" read of the whole object.

The increment's shape is right, and the fact it identifies is the one 8.3.5p10
actually describes. A parameter's name is no part of the function's type, so it
is not a fact of any one declaration - and the object file has to write *one*
spelling for a place several declarations may each have spelled differently or
not at all. Making that a fact of the function, held at the place the function
type gives the parameter, is the only structure that answers it, and it is the
structure the default-argument was already held in. Merging the two into one
`ParameterRecord` beside `HeldInitializer` (`sema_declaration.h`) is what the
data was; the old `Default` was two facts sharing a name.

**The blocker is who counts as a declaration of the function.** C6 read
8.3.5p10's "any declaration" as any declaration at all, including one written
*below* the definition, and made it rename the object the definition had already
made. For a function the program declares that is right and the reference agrees.
For a specialization it is wrong, and it was **a regression C6 shipped**: a
declaration of a *template* written below the pattern's definition declares the
template, not the specialization, and 14.7.1p1 leaves the specialization a
declaration nothing wrote.

### Findings

**1. A declaration below a template's definition renamed the specialization's
parameter object.** 14.7.1p1 makes a specialization a declaration nothing wrote,
so the declarations that could have said anything about its parameters are the
template's - but only the ones that had been read when the pattern's definition
was. C6 held the name on the function's record and let any later declaration be
the first namer, retroactively spelling every object already made for the place.
So

```
template<class T> T zero_of(T) { return T(); }
template<class T> T zero_of(T sample);
int main() { return zero_of(41); }
```

emitted `function @zero_of(%sample : i32)` with `slot $sample` where
`reference-binaries/cppgm++` emits `%__param0`, and the two-specialization form
of the same program emitted it twice. This is **a difference the suite compares**
- parameter names are not masked the way `object=` is - and no fixture had the
declaration below the definition, so C6's own sweep, which is where the rule was
established, never wrote one. The pattern's spelling is now frozen where the
definition that gives it a body is read (`check_template_definition`), a
specialization takes that rather than re-asking the record, and only a
program's own declaration puts an object on the waiting list.

**2. Beside it, the two readers that kept asking the old question.** C6 moved
the record's key from `function.id` to `wrote_defaults(function).id` at the
writer and at two of the four readers. `required_parameters` and
`has_default_argument` (`sema_class.cpp`) still ask `function.id`, so 12.9's
inherited-constructor candidate set and 12.8p2's "still a copy constructor"
would both read an empty record for any function the two keys differ on. They
are **unobservable today** - both are asked only of constructors and assignment
operators, which 12.8p12 leaves out of the template tier, so `primary` and
`shadowed` are null wherever they are called, and probes through the pre-audit
and post-audit binaries emit identical LowIR. Recorded rather than changed: a
key that is right at three call sites and vestigial at two is the shape the next
checkpoint should not build on, and 14.7.1p1's inherited constructors are C7's.

### Changes

| what | where |
| --- | --- |
| 14.7.1p1's pattern spelling frozen where the definition that gives it a body is read | `sema_template.cpp` |
| a specialization taking the pattern's spelling rather than re-asking the function's record | `sema_analyzer.cpp`, `sema_declaration.h` |
| the waiting list restricted to the objects a program's own declaration made | `sema_function.cpp` |

Two regression tests:
`300-declaration-below-a-template-definition-names-no-specialization`,
`300-two-specializations-below-a-template-definition-take-no-name`. Finding 2
leaves no program either oracle answers differently, so it is recorded in Open
Gaps rather than pinned.

### Performance Evidence

Three shapes over the surface this audit owns, `--emit-lowir -O0`, n = 32, 128
and 512, each against the pre-audit binary built from `955dce9f` in a worktree
with `make build`: n functions each defined with an unnamed parameter and named
by a declaration below, 0.00 / 0.00 / 0.02 s before and after; one function with
n unnamed parameter places named by one declaration below, 0.00 / 0.00 / 0.01 s
before and after; n specializations of one template with an unnamed place,
0.00 / 0.01 / 0.05 s before and after. `reference-binaries/cppgm++` is 0.10 s on
each of the first two at every n and 0.50 s on the last at n = 512.

What the audit adds is bounded by what it replaces. The waiting list holds one
pointer per object a definition leaves unnamed and is cleared by the first
namer, so it is O(places) and not O(readings) - and the guard this audit adds is
what makes that true, because a specialization used to add one pointer per
instantiation to the template's list and never drain it. The freeze is one pass
over a record whose length is the parameter count, once per template definition.
Peak RSS at n = 512 is 11264 -> 11524 KB, 8104 -> 8336 KB and 19204 -> 19432 KB,
which is the one extra `std::string` per parameter place and about 2%.

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **262 / 311 -> 264 / 313**
  with the two new tests, the failing 49 the same 49 by name.
- **File audit passes** for pa19 over `dev/src` with the five header-weight
  warnings the shared headers have carried since PA18, and no suppression.
  `sema_analyzer.h` is 2381 lines against the audit's 2400.
- **Every `.ref` regenerates byte-identically** over all 313 fixtures through
  `make ref-test-pa19`; no committed fixture moved.
- **The differential probe.** 30 synthesized programs over the surface
  8.3.5p10 owns - a naming declaration above and below the definition, two and
  three of them naming differently, one place and three, the middle place named
  alone, a member and a constructor and an out-of-class member definition, a
  class template's member and its static member, a function template and an
  explicit specialization of one, an array parameter and a reference one, a
  reopened namespace, an operator, a default argument on the naming declaration,
  and a name colliding with a local, with a global and with a template
  parameter - through the pre-audit binary, `reference-binaries/cppgm++` and
  g++. All 30 compile in all three, and g++ accepts the one C6 had been
  refusing. Against the pre-audit binary, 15 of the 30 moved onto the reference
  and none off it; C6 alone had moved 2 off, which finding 1 is.
- **What is left disagreeing is the reference, twice.** It binds a body's name
  to a parameter place 3.3.4 ends at another declarator - `int chosen = 7; int
  pick(int) { return chosen - 2; } int pick(int chosen);` loads `$chosen`, the
  parameter, where this compiler and g++ both load the global, and the
  reference's program returns the wrong value. And it writes `binding=strong`
  for an explicit specialization this compiler writes `weak` for. Neither is
  C6's and neither can be a committed fixture. The third and last divergence in
  the 30 is an out-of-class constructor the reference writes as one function
  plus an `alias object` line and this compiler writes as two entries - which
  the comparison strips, and which is C7's C1/C2 question rather than this
  path's.
- **Valgrind clean** with `--error-exitcode` over all 313 fixtures under
  `pa19/tests`.
- **The sibling readers, probed rather than assumed.** The two `function.id`
  readers of finding 2 were driven through 12.9's inherited constructors from a
  class template base and 12.8p2's copy constructor with a defaulted second
  parameter; the pre-audit and post-audit binaries emit identical LowIR on both,
  which is what makes the vestigial key unobservable rather than latent.

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
`function.id` to `wrote_defaults(function).id` at the writer and at `accepts_arity`
and `write_default_argument`; `required_parameters` and `has_default_argument`
(`sema_class.cpp`) still ask `function.id`. Nothing observes it: both are asked
only of constructors and assignment operators, which 12.8p12 keeps out of the
template tier, so `primary` and `shadowed` are null wherever they are called and
the pre-audit and post-audit binaries emit identical LowIR through 12.9's
inherited constructors from a class template base and 12.8p2's copy constructor
with a defaulted second parameter. It is left as one key per reader rather than
unified because C7 owns 14.7.1p1's instantiated declarations, which is what would
make the two keys differ.

**Two places the reference is alone, found sweeping 8.3.5p10.** It binds a body's
name to a parameter place 3.3.4 ends at another declarator: in
`int chosen = 7; int pick(int) { return chosen - 2; } int pick(int chosen);` the
reference loads the parameter slot where this compiler and `g++ -std=c++11` both
load the global, and the reference's program returns the wrong value. And it
writes `binding=strong` for an explicit specialization of a function template
where this compiler writes `weak`, which the comparison strips. Neither can be a
committed fixture and neither is C6's.

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
