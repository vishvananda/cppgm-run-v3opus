# PA23 Audit — deduction, substitution and SFINAE

A review of each landed checkpoint, in the order one use of a function template
travels: what a deduction settles, what building the declaration it settled
means, and what a failure of that building says about the candidate that asked.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| 1 | `188e92bc` | 3 / 3 + 3 recorded | **the scope 14.8.2p8 makes of one attempt, landed with no bound on the attempt asking for itself - and the order the same sentence says that attempt is made in.**  Checkpoint 1 made a substitution failure candidate state: `Substitution` wraps the three deduction entry points and the written-argument-list `specialize`, `Instantiated` is the class body 14.7.1p1 read walking past it, 14.1p3's unnamed place is declared, a dependent value argument keeps its spelling and is read again, `dependent_member_type` builds over the class *this* substitution made, and 14.5.6.1p5 rather than 13.1's index pairs two templates.  Those rules are right and swept clean - 8 unnamed-place shapes, 10 SFINAE and qualification-conversion shapes including the multi-level pointer 4.4 refuses, 6 shapes of the `Instantiated` boundary, and the discarded attempt leaves nothing a later naming reads, because the failure happens before `hold_specialization` and the class instantiations it made are 14.7.1p1's and permanent.  What none of it carried is that the same clause bounds the attempt: `Specialization::chosen` and `SemaAnalyzer::specialize` each hold their answer only once it is built, so a request arriving while that same one is being served recursed without bound - and the two new readings closed the loop, a dependent value argument read again at substitution and a stand-in rebuilt over the class the arguments named.  `300-recursive-streamable-sfinae-guard.t` and `300-dependent-adl-hidden-friend-before-later-value.t` are two programs the pre-checkpoint build translated to exit 0 and this one **exhausted the stack on**, reported by the harness as ordinary `EXIT_FAILURE` mismatches, with `300-recursive-trailing-return-sfinae-cache.t` the same crash one tier down and older than the checkpoint.  `TemplateInfo::choosing` and `SemaAnalyzer::specializing_` are the mark each tier takes, and a re-entrant request is refused - which 14.8.2p8 turns into the candidate discarded, the answer `g++` gives all three.  Beside them: 14.8.2p8's *lexical order*, which `substituted` read backwards for every declarator - the parameter list was built first however the return type was written, so a leading result type whose substitution is a hard error was never reached when a parameter was what failed, and `SemaEntity::trailing_result` is 8.3.5p2's fact that says which came first; and 14.1p12, which has no door at the pair 14.5.6.1p5 had just made findable, so two heads of one template each giving one place a default was a program both oracles refuse and this build translated.  Recorded rather than fixed: 14.6p2 is asked where the *pattern* reading reads a type-specifier, so five shapes whose reading a dependent context defers reach the clause with a prefix an argument list has already settled; and n declarations of one template name is quadratic in PA22's `TemplateSignature::equivalent` walk |
| 2 | `a63c183b` | 4 / 4 + 6 recorded | **the name the object file gives an address argument was this unit's entry number for it, and 14.3.2p5's conversions were 8.5's.**  Checkpoints 2-5 made 14.1p4's address places, 10p1's `class-or-decltype`, 13.3.1.4p1's constructor template, 14.6.2p2's variable template and 14.6.2p1's settled prefix.  Those rules are right and swept clean - 11 base shapes, 7 converting-constructor shapes, 8 array-bound shapes, a variable-template stand-in that leaves no global behind, and `Substitution` having no destructor, so `took_places` standing lexically inside the attempt is the walk outside its `try` the comment claims.  What none of it carried is that the *bits* of an address argument are an entry of the constant-address table, numbered in the order this unit reached each address: `at<&left>` was written `_ZN2atILPi1EE4readEv` where both oracles write `_ZN2atIXadL_Z4leftEEE4readEv`, and two units of one program that each name it write one weak definition under two names, which no link can merge.  The suite could not see it, because the comparator drops `object=` from every function header before comparing - the checkpoint's own `100-an-address-argument-is-which-object-it-designates` fixture disagreed with its `.ref` on all five names and passed.  Beside it: 14.3.2p5's conversions were `at_pointer_place`/`at_reference_place`, which are 8.5's readings of the same places and take the three the clause's own note leaves out - a zero-valued integral constant, 4.10p3's derived-to-base, and any pointee at all - so `at<&d>` at a `B2 *` place over a `struct D : B1, B2` was accepted and *ran to the wrong storage*; 14.1p4's fifth place was declared by `non_type_place` and refused by every argument reader, so `template<decltype(nullptr) N>` was a head no list could fill; and 13.4p1 had no door at a function place, so `H<f>` beside two declarations of `f` took whichever the chain led with.  Recorded rather than fixed: a pointer-to-member place, which the layer below has no pointer to member at all for; a `void *` place; `(int*)0`, whose cast the spelling reader reads only integral targets of; and `char (&a)[sizeof(T)]` as a function parameter, which the pre-checkpoint build refuses identically and is PA22's |
| 3 | `30c7c2b5` | 3 / 3 + 3 recorded | **14.6.4.2p1's bound was written for three narrow readings and set to *none* for the two largest ones, so an instantiated body reached the whole unit and chose a later overload.**  Checkpoint 7 made three things: 14.1p9's default over a list an argument has yet to settle travels as a tree and a region; 14.6.4.2p1 puts 3.4.1's half of a second reading back at the definition context, through `declared_serial` and `ReadingBound`; and 3.9.1p8's floating scalar crosses 5.2.2p4's boundary by address.  The first and the third are right and swept clean - 10 shapes of the default over a dependent prefix agreeing with the reference on all and with `g++` on 9, and 28 boundary shapes byte-identical to the reference through the real comparator and running to `g++`'s value on all 28 (the boundary sweep the checkpoint recorded was vacuous: every one of its 20 programs returned a scalar from a function declared to return the class, so both binaries refused all 20 and the comparator called it a pass).  The second landed only for the readings a *pattern* interned - the decltype-specifier, the two tiers of 14.1p9's default - and wrote `ReadingBound(model_, 0)` at `instantiate_body` and at `complete_specialization`, which is every function body and every class body 14.7.1p1 reads again.  So `template<class T> int f(T t) { return late(t); }` above `int late(int);` was a program both oracles refuse and this build translated, `pick(long)` above the template and `pick(int)` below it ran to `pick(int)` where `g++` runs to `pick(long)`, and the same held of a member body, an out-of-class definition, a partial specialization's body and an alias template's type-id.  The bound is a fact of *each* definition, so `TemplateInfo::visible`, `Partial::visible`, `Member::visible`, `WrittenBody` and `PendingDefinition::visible` carry it and each reading sets its own - taken after the first reading of the body, because 11.3p1's friend and the template's own name are declarations the body itself has to find.  `ConstexprReading::selected` passing `kAssociatedUnknown` - the gap the plan recorded as reaching no fixture - is the same clause at the fold, and `W<int>::v` over `fold(long)` above and `fold(int)` below ran to 200 here against 100 in *both* oracles.  Recorded rather than fixed: 8.3.6p9's default *function* argument, which the non-template `int g(int n = late());` refuses identically and is no reading of a template's; a static data member's dependent initializer, deferred past the class body, where the reference agrees with us against `g++`; and 8.5.4p3's narrowing at a value place, which is the plan's own `static_assert` group |

| 4 | `07cb3fb3` | 3 / 3 + 3 recorded | **the refusals landed at one exit each, and the fact 3.9.3p5 made had one reader of four.**  Checkpoint 9 gave 14.8.2p8 four things to fire on: 8.3.2p5 and 8.3.4p1 through one *door* the three type-deriving readers call, 10.4p2 as a fact of the type, 5.7p1's completely-defined pointee, 8.5.4p7 at the constructor a list-initialization chooses, and 14.3.3p1 at `instantiate_class`.  Those rules are right and swept clean - the door refuses through a declarator, a flattened spelling, a pack expansion and a substitution alike, an abstract class reached by inheritance is one, 14.3.3p1 answers the same at a class template, an alias, a variable template, a function template, a pack and a naming that never instantiates, and 8.5.4p7 refuses through all seven initialization paths a constructor is reached by.  What none of it carried is that each refusal has one exit more than the checkpoint wrote.  5.2.1p1's pointee is 5.7p1's - `Inc *p; p[0];` is a program the reference and `g++` both refuse and this build translated, and its own comment restated the clause the code never asked.  8.5.4p7's fourth bullet was written as width and equal-width signedness, which is not "cannot represent all the values": signed reaching a *wider* unsigned has negative values at every width, and `bool` holds two of them however wide its storage - so `unsigned x[] = { g() }` off a `char g()` and `bool x[] = { g() }` were translated, and the same statement gated the fold, so `unsigned x[] = { (char)-1 }` never reached the constant exception either.  And 3.9.3p5 landed at `pointer_convertible`'s 4.10p2 arm alone: 4.4's own walk, 5.9p2's composite pointer type and 14.8.2.1p2's conversion each read `cv` of an array node, which is zero, so `const int (*p)[3] = &a;` was refused four ways - beside a walk that asked 4.4p4's second condition of the level it had just compared, which refused `volatile int *q = p;` with no array in it at all.  225 + 675 narrowing shapes and 20 qualification shapes now agree with `g++` on all, `200-range-array-reference-mutable-begin.t` turned, and every course `.ref` regenerated from the reference binary is unchanged.  Recorded rather than fixed: `void *p; p[0]`, which the reference accepts and `g++` and this build refuse; a subscript detector, which the reference refuses whole so no fixture can pin it; and `typedef abstract_class a[2];`, which the plan already carries |
| 5 | `54fc10e2` | 1 / 1 + 3 recorded | **the naming the checkpoint made a second type of is the type its type-id named, so every declaration a program wrote twice stopped pairing.**  Checkpoint 11 gave 14.8.2p8 the one thing 7.1.3p2's collapse had taken from it: a naming of an alias template whose type-id threw an argument away keeps that argument, so `discard<typename T::pointer>` is built where 14.7.1p1 arrives and a `T` with no `pointer` is a candidate dropped.  That rule is right and swept clean - the detector answers the two classes apart at eight syntactic sites and runs to `g++`'s value at all of them, `held<Ts &...>` binds a pattern that is not a place through six shapes, `note_places` carries a value argument's packs through a reading read again, and 14.3.3p1's place takes an alias.  What none of it carried is 14.5.7p1, the other half of the same sentence: a template-id over an alias template *is* the associated type, so an entry standing beside that type is a second type for one the program may also write out longhand - and `template<class T> void f(discard<T> *)` declared beside `template<class T> void f(void *)` defined stopped being two declarations of one template.  Three readers ask it: `TemplateSignature::of`'s canonical form at 14.5.6.1p5's four tiers, 13.1's index of a parameter-type-list, and the return-type comparison two declarations of one function are paired by - so a function template, its out-of-class definition, a member of a class template, a member template of a class that is not one, and a naming under another argument list were five programs both oracles and the pre-checkpoint build accept and this one refused.  `rebuilt` is where the two halves meet: an argument that is a *place* is looked up by the substitution and can refuse nothing, so the naming collapses to what its type-id named exactly as 14.5.7p1 says, and an argument the substitution *builds* again - a member of a prefix, a specialization, a derived type, an expression read a second time - is what the entry is kept for.  Recorded rather than fixed: the same equivalence where the discarded argument is one of those readings, which needs the associated type as a canonical form rather than a scope; 8.3.1p4 and 8.3.3p3 at a deduction, where the two oracles disagree with each other; and `TypeTable::substitute`'s structural rebuild, which the plan already carries |
| 6 | `e4191b39` | 2 / 2 + 3 recorded | **the list a member wrote is a second fact of its name, and the two readers that had been asking the old one question got a wrong answer each.**  Checkpoint 13 made 14.2p4's argument list part of what a dependent member's name stands for: it is read where the reading stands, interned beside the prefix and the name, built again by `Substitution::member` and finished at 14.3.3p1's two exits, with a class that declares no such member template 14.8.2p8's failure.  That rule is right and swept clean - 24 shapes of the idiom across the sites a type-specifier stands at, the member kinds a class can declare and the argument kinds a list can hold, agreeing with `g++` on all 24 and running the 21 accepted ones to its value; the ABI writes the third form of `<unresolved-name>` byte-identical to the reference and to `g++` for the plain, the pack and the value form, and two units naming one such signature write one weak definition under one name.  What none of it carried is that widening `dependent_member` from the whole written component to the bare name left 14.6p2's reader comparing the two: `require_written_type` asked whether the stand-in's member equals `written.last()`, which for `T::template box<int>` is `box<int>` against `box` - so `T::template box<int> b;` written with no `typename` stopped being refused, a program `g++` refuses and the pre-checkpoint build refused.  And the value half copied 7.1.5p2 onto every specialization `specialize` makes, which is every specialization the *program* wrote out too: `template<> int f<int>()` declared without `constexpr` beside a `constexpr` primary folded, so `char a[f<int>()]` was a program both oracles refuse and this one translated.  The copy is gone and the clause is asked where it is needed - `constexpr_declared` reads the template's flag for a specialization that is not 14.7.3p1's, which is the one reading that writes no definition to read it off, and the same answer is what says whether the refusal is 5.19's about the program or the edge of this build.  Recorded rather than fixed: a template-id component after a *decltype* prefix, which the pre-checkpoint build refuses identically and both oracles accept; 14.6p2 itself, which the reference implements at no shape at all so no fixture can pin it; and `TypeTable::substitute`'s structural rebuild, which leaves a member stand-in standing as it leaves a discarding naming and which the plan already carries |

## Current Checkpoint Review

Checkpoint 13 - a dependent member written as a template-id, and the
specialization no list has settled: `dependent_arguments` and
`dependent_template_id` beside `dependent_owner` and `dependent_member`;
`Specialization::member_component` reading one written component's list where
the reading stands; `Substitution::member` building it again and
`SemaAnalyzer::dependent_member_type` finishing it at 14.3.3p1's two exits; and
`Substitution::unsettled` saying which specializations 14.7.1p1 instantiates
nothing for - was reconstructed from its commit, from `dev/src` and from the
README: which readers of a dependent member's name the new fact reaches, which
sites such a name can stand at, what the object file calls it, and which
declarations the value half stops making a definition of. Two defects were found
and fixed across the reader path each reaches, three gaps were probed as programs
and recorded, and the rest is what the review confirmed.

### Findings

**1. 7.1.5p2 was copied onto every specialization `specialize` makes, and one of
them is the specialization the program wrote out for itself.** The checkpoint
needed the flag for the one reading that makes *no* definition - 14.6p8's, which
stands a value in for a call it cannot fold and has no body to read `constexpr`
off - and wrote it as a field of every specialization. 14.7.3p1's is not one of
them: an explicit specialization is a declaration of its own, and its own
decl-specifiers say whether a call of it is one 5.19 reads.

```cpp
template<class T> constexpr int f() { return 1; }
template<> int f<int>() { return 2; }          // not constexpr, and says so
int main() { char a[f<int>()]; return (int)sizeof(a) - 2; }
```

Both oracles and the pre-checkpoint build refuse it; this one folded the call and
sized the array. The flag cannot be corrected at the declaration either, because
`declare_function` merges it with `||` - 7.1.5p2 says all declarations of one
function shall write `constexpr`, so a later one can only add it - and the copy
had already gone on before `template<>` was read.

The fix is at the reading rather than at the field. `constexpr_declared` is
7.1.5p2 asked of the declaration chain: the specialization's own flag, or the
template's where the specialization is not one 14.7.3p1 let the program write
out. It is what the stand-in is gated on and what says which of the two refusals
the failure is - 5.19's answer about the program, or the edge of what this build
has read - so a specialization made by a naming alone folds exactly as it did and
one the program declared answers for itself.

**2. 14.6p2's reader compares the member's name to the whole component that was
written, which the checkpoint made two facts of.** `dependent_member` held the
component as it was spelled until this checkpoint split the list off it, and
`require_written_type` asks whether the stand-in it is holding is the one this
reading made for the last component - by comparing that string to
`written.last()`. For `T::template box<int>` those are now `box` and `box<int>`:

```cpp
struct A { template<class U> struct box { U v; }; };
template<class T> struct S { T::template box<int> b; };   // no `typename`
int main() { S<A> s; return s.b.v; }
```

`g++` refuses it and so did the pre-checkpoint build; this one translated it,
while `T::plain` one line away was still refused. `stood_in_for` is the question
put back together - the name the spelling wrote before its `<`, against a
stand-in that says whether it holds a list at all, with an operator-function-id
spelled with `<` no template-id because `TemplateId` says so. It is asked last of
the chain, because it reads a record only a type a dependent prefix made has:
computing it ahead of `prefix != kNoType` reads `user_at` of a type that has no
user record, which is a segmentation fault on three `pa12` fixtures with no
template in them at all.

### What the review confirmed rather than found

- **The idiom answers at every site, member kind and argument kind crossed.**
  24 generated shapes - a parameter, a return type, a member typedef, a
  base-clause, a default template argument, a pointer, a local, a `sizeof`
  operand, a `decltype` operand, a nested naming, a member call, two detectors, a
  partial specialization's own argument, two lists after one name, one list
  written twice, a missing member, a member that is no template, and the
  member-kind and argument-kind axes across them - agree with `g++` on all 24,
  and the 21 it accepts run through `lowir2cy86` + `cy86` to the value `g++` runs
  them to.
- **The object file's name for one is the reference's and `g++`'s.**
  `_Z1fI1AEiPNT_3boxIiEE`, `_Z1fI1AJicEEiPNT_3boxIDpT0_EE` and
  `_Z1hI1BLi5EEiPNT_2atIXT0_EEE` - the plain, the pack and the value form - are
  byte-identical in all three, and two translation units that each name
  `take<A>` write one weak definition under one name that links and runs.
- **The interning is by the three facts the name is made of.** Two lists after
  one name are two members and one list written twice is one, at a member class
  template, a member alias template and a member value template alike, and a
  prefix that is a different place is a different member again.
- **The value half leaves nothing unemitted.** A member function template of a
  class template called from outside and from inside the pattern, a nested one, a
  template defined below its use, an explicit instantiation, a default argument
  folding over its own place and a `template`-qualified call through an object
  all link through `lowir2cy86` + `cy86` and run to `g++`'s value - so
  `instantiate`'s early return suppresses no definition the program owes.
- **A pattern that is a member template-id expands per element.** Three shapes of
  `typename T::template box<Ts>...` - as a class template's argument, with a
  member named through it, and as a function's parameter pack - give
  `sizeof...` the length the list wrote and run to `g++`'s value.
- **Every course `.ref` is the reference binary's.** All 25 were regenerated
  from `cppgm++-ref` through the harness and not one changed; the twenty-sixth
  is this audit's.
- **Nothing is gated and no phase is skipped.** The checkpoint's 742 added source
  lines and this audit's 40 hold no `getenv`, no fixture name, no timeout, no
  environment read, no dialect switch keyed on anything but a dialect, and no
  caught exception standing for a success - the one `catch` in
  `member_component` is 5.4p2's type-then-value order, and a spelling the access
  clause refuses reports that refusal rather than the retry's.
- **`valgrind -q --error-exitcode=9` is clean over 139 inputs** - 60 of the
  corpus, the 26 course fixtures and 53 probes of the paths this audit touched -
  and no input exits above 1: not one of pa23's 400, and not one of the 2083
  files pa10 through pa22 run through their own dialects.

### Recorded rather than fixed

- **A template-id component written after a *decltype* prefix.**
  `typename decltype(T::make())::template box<int>` is refused here as `no
  declaration of box<int> is in scope` and accepted by both oracles - and so is
  `decltype(A::make())::template box<int>` with no template in the program at
  all, which is where the gap actually is. The pre-checkpoint build refuses both
  identically, so this is the decltype-qualified name reader and not 14.2p4's
  list; `T::type::template box<int>` through a typedef comes out right.
- **14.6p2 has no agreeing oracle.** The reference translates
  `T::plain x;` and `T::template box<int> x;` alike, so neither half of finding 2
  can be pinned by a course fixture; `g++` refuses both and so does this build,
  and the plan already carries the clause's other direction - the readings a
  dependent context defers, which cost a `pa20` fixture to widen.
- **A member stand-in is left standing by `TypeTable::substitute`.** Its
  parameter-kind arm rebuilds an alias naming and looks a place up, and a member
  of a prefix - with or without the list this checkpoint gave it - matches
  neither, so the per-element path of an expansion over a settled run leaves the
  naming for `SemaAnalyzer::substituted` to build. Every shape reachable through
  it comes out right, and the plan already carries the alias half of the same
  gap.

### Changes

| Where | What |
|-------|------|
| `sema_template.cpp` | the `constexpr_function` copy is gone: 7.1.5p2 is a fact of what each declaration wrote, and a specialization that never wrote one asks the template for it where it is needed rather than carrying an answer no declarator of its own gave. |
| `sema_constexpr.cpp` | `constexpr_declared` - 7.1.5p2 read off the declaration chain, which is the specialization's own flag or the template's where 14.7.3p1 did not let the program write the specialization out. It gates 14.6p8's stand-in and says which of the two refusals a call with no body is. |
| `sema_declarator.cpp` | `stood_in_for` - whether a stand-in is the one this reading made for the last component written, with 14.2p4's list taken off the spelling first. Asked last of 14.6p2's chain, after the test that says the record is there to read. |
| `course/pa23/100-bad-an-explicit-specialization-says-whether-it-is-constexpr.t` | the two halves in one program: the specialization no declaration but the naming makes still folds, and the one `template<>` wrote out without `constexpr` does not. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `e4191b39` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| member-naming multiplicity | n distinct member template-ids in one pattern, no two sharing an entry | 0.00 s @32, 0.01 @128, 0.03 @512, 0.07 @1024 - and the same on the pre-audit binary |
| member-naming repetition | n typedefs of one `P::template box<int>`, the interned stand-in read once | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| member-naming cross product | n call sites x one member template-id substituted to a distinct argument apiece | 0.00 @32, 0.00 @128, 0.02 @512, 0.05 @1024 - and the same |
| plain-member multiplicity | n `typename T::type` typedefs in one pattern - the path `stood_in_for` was added to | 0.00 @32, 0.00 @128, 0.01 @512, 0.01 @1024, 0.03 @2048 - and the same |
| unsettled-specialization multiplicity | n function templates, each a default argument folding `ok<T>()` over its own place | 0.00 @32, 0.01 @128, 0.07 @512, 0.14 @1024 - and the same |
| constexpr-member multiplicity | n static data members of one pattern, each folding a member template `constexpr` stands on | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| member-naming nesting | d nested `T::template at<...>::value`, every level dependent | 0.00 s flat from d = 2 to d = 20, on both binaries |
| member value-argument nesting | d nested `A::at<...>::value` written as one member's argument - where the type-then-value retry could double | 0.00 s flat from d = 4 to d = 48, on both binaries |
| member type-argument nesting | d nested `A::box<...>::type` written as one member's argument | 0.00 s flat from d = 4 to d = 32, on both binaries |
| whole PA23 corpus | 400 handout and 26 course files, one process each | **1.99 s against the pre-audit binary's 2.01 s**, over three alternating passes; no `rc > 1`; valgrind clean |

`stood_in_for` is one `TemplateId` construction and one string comparison, made
only where the name is a qualified one whose prefix is a place the enclosing head
declared - which the four tests before it are what say - so a name written behind
any settled prefix pays nothing it did not pay. `constexpr_declared` is three
field reads per fold that reaches a callee with no body, and removing the copy
takes one assignment off every specialization made. The corpus is 0.02 s under
the pre-audit binary, which is that assignment and the walk of the type a
collapsing naming no longer makes.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **370 / 426** (handout
  344 / 400, course 26 / 26), against 369 / 425 at the turn's start: the failing
  set is the same 56 files name for name, and the twenty-sixth course fixture is
  this audit's.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages. The first
  build of finding 2's fix read `user_at` of a type with no user record and
  crashed three `pa12` fixtures; the harness reported all three as ordinary
  `EXIT_FAILURE` mismatches, which is what the whole-corpus `rc > 1` scan below
  is for.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- The failing set of the checkpoint binary was taken as a whole and compared:
  this audit turns one test - its own fixture - and regresses none.
- 24 generated cross-product shapes plus 34 hand-written probes through `g++
  -std=c++11 -pedantic-errors`, the reference binary, the checkpoint binary and
  the pre-checkpoint binary. Every one this build accepts that is not one of the
  divergences recorded above runs through `lowir2cy86` + `cy86` to the value
  `g++` runs it to.
- All 400 pa23 corpus files and all 2083 files pa10 through pa22 compiled one at
  a time under their own dialects: **0 exits above 1**; 139 inputs under
  `valgrind -q --error-exitcode=9`: 0 errors.
