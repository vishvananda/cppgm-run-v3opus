# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C5, reviewed at `277a48bb`** — the class a template makes of its own
parameters: 14.6.1p1's current instantiation as the class a class template's own
definition declares, read once against a kept region binding each parameter to a
type standing for itself; 14.5.1.3p1's out-of-class member definition read
against the same two, with 14.1p2's own head names; 14.6.2p1's dependent
qualified name, 14.6.2p3's dependent base off the lookup chain, and what a
dependent type is worth stood in for; 9.2p2's complete-class context as a
held-body list; 9.4.2p1's qualified class-head defining into the region its name
reaches; 9.3.1p3's object parameter read for a pattern; 15.4p1 asked wherever one
declaration redeclares another.

The increment's shape is right, and it is the one the milestone needed: making
the pattern's own reading declare a *class* - the specialization over parameters
standing for themselves - is what lets `S<T>` written inside `S` and `S<T>`
written anywhere else in the template be one declaration found the way every
other one is, and it is what gives 14.5.1.3p1's out-of-class definitions a class
to declare into long after the body closed. The `checking_` gates it adds are
each a question an argument list is what answers - a layout, a conversion, a
base this milestone does not lay out - and none of them is a gate on a *source*.
`DialectReading` and `FunctionReading` carry the reading, so the pa1-pa18
baseline is untouched by all of it and 1777 / 1777 stands.

**The blockers are what the reading kept, and what it did not keep.** The names
one out-of-class member definition's head wrote were bound in the region every
other reading of that class looks names up through, so they outlived the
definition and the next one collided with them - and the collision fell back to
reading that definition against the class's own spelling, which is **wrong code
on a program both oracles compile**. 9.2p2's held bodies were walked once rather
than drained, so a body held while held bodies were being read was read by
nobody. And 14.6.2p1's dependent name kept its spelling and not the two facts
the object file writes it from.

### Findings

**1. The names one member definition's head wrote outlived it, and the next
definition was read against the class's own spelling.** 14.1p2 leaves each
declaration of one template free to spell its parameters as it likes, and what
two heads share is the *places* the argument list is in the order of.
`bind_member_parameters` bound each head's names into the one region the class
was completed against - `TemplateInfo::parameter_region` for the pattern, the
specialization's bindings for an instantiation - because that is the region the
body reaches through the class. It refused, returning null, where a name already
stood there for another place. Three things followed, and they are one fault:

- A head that spells the class's own parameters in another order was refused and
  fell back to `open_template_bindings`, which binds the *class-head's* names.
  So `template<class T, class U> struct A { void f(); };` with
  `template<class U, class T> void A<U,T>::f() { U a; T b; }` over `A<int,char>`
  emitted `slot $a : i8` and `slot $b : i32` where
  `reference-binaries/cppgm++` and g++ both emit `i32` and `i8` - and the same
  swap in a return type, where the declarator resolved `A<U,T>` to the
  specialization the *other* argument order makes.
- Two definitions whose heads permute one another's names left the second
  refused outright - `V does not name a type` - wherever the first was read
  first, on a program both oracles compile; and 14.6p8's reading of it was
  skipped in silence by the same null.
- A name only the definition *before* it wrote was still standing, so
  `template<class U> void A<U>::f() {}` followed by
  `template<class T> void A<T>::g() { U x; }` compiled, which g++ refuses.

`open_member_parameters` now opens a region of the definition's own, inside the
one the class was completed against, and `EnclosedBy` stands it between the
class and that one for as long as the definition is read. A name the head wrote
reaches the argument its own place took whatever the class-head called that
place, nothing it binds is standing when the next definition is read, and the
class keeps the region every other reading of its members looks through.

**2. A body held while held bodies were being read was read by nobody.**
`read_held_pattern_bodies` took the entries above its mark, put the list back to
the mark and walked the copy - so a class declared *in* a body being read wrote
member function bodies that 9.2p2 held above the mark again, and nothing ever
read them; they stayed on the list for the rest of the unit.
`template<class T> void settle(T) { struct local { int reach() { return
nowhere_at_all(); } }; }` was accepted where g++ refuses it. Beside it,
`check_template_definition` - the reading a *function* template's own definition
gets - never drained the list at all, so every body a class in it declared was
held into a dump the call dropped. The list is drained back to the mark rather
than walked once, and the function template's reading owns the ones its body
held.

**3. A name written through a dependent prefix was named in the object file as
the parameter itself.** `dependent_member_name` made one type per prefix and
whole spelling, over a template-parameter type carrying no record of what it was
a member of - and `template_index` defaults to zero, so
`template<class T> typename T::car_type take(T *)` was
`_Z4takeI1HET_PS1_`, naming its return type `T_`. `reference-binaries/cppgm++`
and g++ **both** write `_Z4takeI1HENT_8car_typeEPS1_`. The type now carries the
prefix and the name as the two facts the ABI writes apart, `abi_type` writes
them as the member of an unresolved type they are, and every component after the
prefix is a member of the one before it - so `typename T::mid::leaf` is
`NT_3mid4leafE`, byte-identical to both oracles. This is a name no fixture can
fail on: `canonicalize_lowir_for_compare` masks a function's symbol before the
comparison.

**4. Beside them, on the naming path the tier already owned: a specialization's
argument list was spelled without the space a program writes it with.** A
function's symbol is masked and a global's is not, so what a static data member
of a two-argument specialization is named *is* a fact the suite can see:
`template<class T, class S> struct A { static T v; };` emitted
`@A_int_char___v` where the reference emits `@A_int__char___v`. The spelling
writes `A<int, char>` now. No fixture declares one, which is why it survived the
tier audit's own sweep.

### Changes

| what | where |
| --- | --- |
| 14.5.1.3p1's region of one member definition's own head names, standing between the class and the one it was completed against | `sema_template.cpp`, `sema_analyzer.h` |
| 9.2p2's held bodies drained rather than walked once, and a function template's reading owning the ones its body held | `sema_template.cpp` |
| 14.6.2p1's dependent member name kept as the prefix and the name the ABI writes apart, one type per component | `sema_declarator.cpp`, `sema_analyzer.h`, `type_model.h`, `type_model.cpp`, `lowir_abi.cpp` |
| 14.7.1p1's specialization spelled the way a program writes an argument list | `sema_template.cpp` |

Two regression tests:
`300-out-of-class-member-head-spells-its-own-parameters`,
`300-dependent-member-name-in-a-declared-return-type`. The other two findings
leave shapes `reference-binaries/cppgm++` *accepts* - a member definition
reaching the head of the one before it, and a local class's body read inside a
template definition - so a committed `-bad` fixture for either would contradict
the checked-in oracle, and they are recorded below instead.

### Performance Evidence

Fourteen shapes, each timed twice, `--emit-lowir -O0`, n = 32 to 512, with the
ones this checkpoint owns timed against the pre-C5 binary built from `9f5679c6`
in a worktree with `make build`. The region this audit opens per member
definition is one per reading, which is the count the tier had before those
names were bound into the class's own, so every shape is where the checkpoint
left it: n class templates each with a body 0.00 -> 0.03 s; n out-of-class
member definitions of one template 0.00 -> 0.03 s; n member function bodies in
one class template 0.00 -> 0.02 s; n qualified dependent names 0.00 s
throughout; n class templates each deriving from the previous one's current
instantiation 0.00 -> 0.04 s against `reference-binaries/cppgm++`'s 3.88 s at
n = 512; n distinct specializations each with a member function 0.00 -> 0.10 s
against the pre-C5 0.02 -> 0.09 s and the reference's 0.89 s. 32 nested class
definitions in one pattern and 32 nested blocks in a pattern body are 0.00 s.
The plan's table carries all fourteen.

**One shape the checkpoint made reachable is quadratic, and it is the tier's
model rather than C5's.** n out-of-class member definitions of a template with n
specializations reads every definition for every specialization - 14.5.1.3p1 as
this milestone reads it, where 14.7.1p1 instantiates the declarations a class
needs and leaves each definition to the use requiring it - so n = 32, 64 and 128
are 0.04 s, 0.18 s and 0.73 s, with one function emitted for the 16384 readings
of the last. It is the same before and after this audit's fix, and
`reference-binaries/cppgm++` is **1.00 s** on the same input, so it is recorded
rather than re-architected.

The exponential spelling shape is unchanged and is still the milestone's:
0.01 s, 0.16 s, 0.67 s and 2.75 s at n = 12, 16, 18 and 20, against the pre-C5
0.01 s, 0.20 s, 0.89 s and 3.48 s.

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **254 / 308 -> 256 / 310**,
  the two new tests being the two regressions these findings leave. The failing
  54 are the same 54.
- **File audit passes** for pa19 over `dev/src`, with the five header-weight
  warnings the shared headers have carried since PA18 - and no suppression.
  `sema_analyzer.h` is 2389 lines against the audit's 2400.
- **Every checked `.ref` regenerates byte-identically** from
  `reference-binaries/cppgm++` through `make ref-test`; no committed fixture
  moved.
- **The differential probe both oracles answer.** 73 synthesized programs over
  the paths this checkpoint owns - every order two out-of-class member
  definitions can spell one head's parameters in, a class declared in a body and
  in a class body of a pattern, a dependent base and a dependent member name, a
  dependent bit-field and a dependent `sizeof`, an exception-specification on
  each of the three declarations that can write one, and the orders a
  specialization, its template's definition and its member definitions can be
  written in - compiled by `dev/cppgm++`, by `reference-binaries/cppgm++` and by
  g++, with 56 of them compared as emitted LowIR.
- **Every LowIR comparison against the reference agrees** but the two the
  exception-specification gap below owns. The three findings' own programs are
  byte-identical to the reference, including the swapped head in a body and in
  a return type. Of the nineteen exit-status divergences that remain, nine are
  where this compiler and g++ refuse a body 14.6p8 reads and the reference does
  not read at all; the rest are recorded below or are shapes PA19's Out Of Scope
  list names.
- **The object-file names both other compilers write.** The dependent-member
  finding's names are byte-identical to `reference-binaries/cppgm++` *and* to
  `g++ -std=c++11` at two and at three components.
- **Multi-unit.** Two units each defining one class template's members with
  differently-spelled heads and each naming a different specialization are
  byte-identical to the reference and canonically identical in both unit orders.
- **The metadata the comparison strips, swept again.** `object=` over every
  fixture both compile differs from the reference on **7** tests and `binding=`
  on **10**, where C4 left 7 and 10 - and every one of them is a test that
  already fails, except the recorded unnamed-namespace divergence. The one
  passing test that hid an `object=` difference before this audit -
  `300-reference-member-same-template-name` - is the dependent-member finding.
- **Valgrind clean** with `--error-exitcode` over all 308 fixtures under
  `pa19/tests`, which is every one the suite compiles but the two course ones.

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
