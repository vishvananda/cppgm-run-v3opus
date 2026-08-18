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

## Current Checkpoint Review

Checkpoint 7 - the second reading a substitution makes, 14.6.4.2p1's bound on
what such a reading may name, and 3.9.1p8's floating scalar at 5.2.2p4's
boundary - was reconstructed from its commit, from `dev/src` and from the
README: which readings a pattern leaves behind, what each of them is read
under, and what the boundary fact costs. Three defects were found and fixed,
three gaps were probed as programs and recorded, and the rest is what the
review confirmed.

### Findings

**1. The bound was written for the three readings a pattern *interns* and set
to none for the two that hold every other name a template writes.** 14.6.4.2p1
says 3.4.1's half of a dependent call's candidate set is found from the template
definition context. The checkpoint's `ReadingBound` says so at
`dependent_expression_type`'s decltype-specifier and at both tiers of 14.1p9's
default - and `instantiate_body` and `complete_specialization` each wrote
`ReadingBound(model_, 0)`, which is *no bound at all*, for the function body and
the class body 14.7.1p1 reads again. Those two are where a template writes
nearly everything it writes.

```cpp
int pick(long) { return 1; }                                  // ran to 2 here
template<class T> int f(T t) { return pick(t); }              // g++ runs it to 1
int pick(int) { return 2; }
int main() { return f(1) == 1 ? 0 : 1; }
```

The accept/refuse half of it is as plain: `template<class T> int f(T t) { return
late(t); }` written above `int late(int);` is a program the reference binary and
`g++ -pedantic-errors` both refuse - `int` has no associated namespace, so
3.4.2 reaches nothing and 3.4.1 from the definition context reaches nothing
either - and this build translated. A member body of a class template, a
definition written outside its class, a partial specialization's body and an
alias template's type-id were the same in every direction.

The fix is that the bound is a fact of *each definition* and not of the
template. `TemplateInfo::visible` is the pattern's, `Partial::visible` is the
pattern a partial specialization wrote, `Member::visible` is an out-of-class
member definition's, `WrittenBody` is the pair `explicit_classes` and
`explicit_functions` now hold - a body `template<>` wrote out stands *below* the
pattern and reaches more, not less - and `PendingDefinition::visible` carries it
for the body a reading put aside, because the walk that drains the queue at the
end of the unit is flat and stands under no bound of its own. Each of the five
readings sets its own at entry, so a reading standing inside a reading takes the
inner one: without that, the bound of a class body being instantiated leaked
into every alias template and every trait its members named, which is the one
regression the first cut of this fix had.

Two facts about *where* the number is taken decide the whole thing.
`SemaModel::written_bound` is the bound a construct being recorded will be read
under - the one standing where a second reading is what is writing it, and the
whole unit where the program's own walk is - so a member template of a class an
argument list made does not inherit the unit. And the pattern's own number is
taken *after* its first reading rather than at the class-head, because the body
declares things its own second reading has to find: 11.3p1's friend is declared
into the namespace around the class by nothing but the body, and a body that
names the template it defines is what recursion is. Both were regressions the
sweep caught before the suite did.

**2. `ConstexprReading::selected` asked 3.4.2's question of no candidate, so a
fold inside a second reading still reached a later overload.** The plan recorded
this as a gap no fixture reaches. It reaches one now:

```cpp
constexpr int pick(long) { return 1; }
template<class T> struct W { static const int v = pick(T(0)); };   // ran to 2
constexpr int pick(int) { return 2; }                              // both oracles: 1
int main() { return W<int>::v == 1 ? 0 : 1; }
```

The expression tier draws 3.4.1's line against 3.4.2's by recording how many
entries the argument-dependent search appended; the fold's own
`callee_candidates` gathered the same two halves and told `select_overload`
nothing, so it passed `kAssociatedUnknown` and every candidate was exempt. It is
the same two lines - the size the set had before `ArgumentLookup` ran, and the
subtraction after it - and the operator door beside it takes the count
`OperatorCall::candidates` was already made to hand back. A member's own set has
no 3.4.2 half at all, so the two member doors pass zero.

**3. A name 1.4p8 reserves was numbered among the program's own declarations,
so the use that declared it put it after every pattern above it.** A reserved
function is declared by its first use. `bind` gave it a `declared_serial` there,
and `__builtin_expect` written inside a class template's `constexpr` member was
then a declaration that template's own second reading could not reach -
`pa21/tests/general/300-constexpr-template-aggregate-subscript-member.t` is the
program, and it is the only `through-pa22` test the bound broke. Such a name
stands before the unit rather than at whichever use named it, which is what
`declared_serial` zero already says for every declaration no namespace region
binds.

### What the review confirmed rather than found

- **The floating boundary is right, and the sweep that said so was vacuous.**
  The checkpoint's 20 recorded ABI shapes each returned a scalar from a function
  declared to return the class (`S round(S s) { return take(make()); }`), so
  both binaries refused all 20 and `compare_results.pl` reported a pass. Rebuilt
  as programs, 28 shapes - float, double, long double, two doubles, float
  beside int, an array of two and of four floats, an array of a class holding
  one, a base holding one, an empty base beside one, a union member, a nested
  class, a `const` member, a class of three doubles, a user destructor, a user
  copy constructor, a pointer to float, a `bool` beside a float, and the
  no-floating controls - are identical to the reference through the real
  comparator and run to `g++`'s own value on all 28.
- **14.1p9's default over a list an argument has yet to settle is right.** Ten
  shapes: a default that is not a constant expression, one naming a member the
  class does not have, one gating a partial specialization, a default reading
  the default before it, one in a member template, one whose *place* an earlier
  argument types, and two namings of one list being one specialization. All ten
  agree with the reference; nine agree with `g++` too, the tenth being 8.5.4p3's
  narrowing at a value place, which is the plan's own `static_assert` group.
- **The `catch (const NotConstant&)` in `substituted`'s evaluated arm stands for
  no success.** It leaves the argument the reading it was, which is what an
  outer list that has yet to settle needs; where the list *is* settled and the
  tree is not a constant, the program is refused a tier down - both a
  non-constant default and one naming nothing are refused here and by both
  oracles.
- **The cross-product of five readings against four kinds of later declaration
  agrees with the reference on 23 of 23.** Six refusals - a function body, a
  member body, a partial specialization's body, an out-of-class definition, an
  alias template's type-id, a trailing return type - and twelve acceptances
  including 3.4.2's later namespace function, a later hidden friend, an explicit
  specialization written below everything, a recursive template and a friend the
  body itself declared. `g++` agrees on 21; the two it does not are C++14's
  variable template and the static-member gap recorded below.
- **Nothing is gated and no phase is skipped.** The whole diff of the checkpoint
  and of this audit holds no `getenv`, no fixture name, no timeout, no
  environment read, no dialect switch keyed on anything but a dialect, and no
  caught exception standing for a success.
- **`valgrind -q --error-exitcode=9` is clean over 119 inputs**, and no file of
  the 417-file corpus exits above 1.

### Recorded rather than fixed

- **8.3.6p9's default *function* argument is read at the call and not where it
  was declared.** `int g(int n = late());` above `int late();` is a program both
  oracles refuse and this build translates - with no template anywhere in it, so
  it is the declarator layer's clause and not a reading of a template's. The
  same holds for a member's default argument and for one written in a function
  template's head.
- **A static data member's *dependent* initializer is read past the class
  body.** `static const int v = late(T());` over a `late` declared below the
  template is refused by `g++` at the point of instantiation and accepted by the
  reference binary, which is the milestone's oracle here; a non-dependent one is
  read in the class body and is bounded, and refusing both would be stricter
  than the `.ref` files the suite compares against.
- **8.5.4p3's narrowing at a value place.** `template<class T, char C =
  t<T>::w>` over a `w` of 300 is accepted here and by the reference, and refused
  by `g++`. The plan already carries it as one of the twelve `static_assert`
  clauses.

### Changes

| Where | What |
|-------|------|
| `sema_template.h` | `WrittenBody`, and `visible` on `TemplateInfo`, on `Partial` and on `Member`: each definition and the bound it was written under. |
| `sema_declaration.h` | `PendingDefinition::visible`, for the body a reading put aside and the end of the unit reads. |
| `sema_scope.h` | `SemaModel::written_bound`, the bound a construct being *recorded* will be read again under. |
| `sema_template.cpp` | `instantiate_body` and `complete_specialization` run under the body's own bound rather than under none; `record_template` takes the class's after its first reading; `queue_definition` stamps the entry. |
| `sema_function.cpp` | the function pattern's bound, taken once `check_template_definition` has read the body. |
| `sema_analyzer.cpp` | `write_definition` puts the queued body's bound back where the body is finally read. |
| `sema_pattern.cpp` | 14.5.1.3p1's out-of-class member definition carries and runs under its own. |
| `sema_specialize.cpp` | the partial specializations' and the alias template's, and 14.5.7p1's type-id read under the alias's own. |
| `sema_explicit.cpp` | the bodies `template<>` wrote out, which stand below the pattern. |
| `sema_constexpr.{h,cpp}`, `sema_constexpr_object.cpp` | `callee_candidates` counts what 3.4.2 appended and `selected` hands it to `select_overload`, so a fold asks 14.6.4.2p1 of 3.4.1's half of its set. |
| `sema_expression.cpp` | 1.4p8's reserved name stands before the unit rather than at the use that declared it. |

### Performance Evidence

Measured on the audited binary against a worktree of `30c7c2b5` built the same
way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| instantiation multiplicity | n specializations of one template, each body read under its own bound | 0.00 s @32, 0.02 @128, 0.10 @512, 0.23 @1024 - and 0.00 / 0.02 / 0.11 / 0.22 on the pre-audit binary |
| template multiplicity | n function templates, one body apiece | 0.00 @32, 0.01 @128, 0.04 @512, 0.10 @1024 - against 0.00 / 0.01 / 0.05 / 0.09 |
| later-declaration multiplicity | one detector, n later declarations of the name it reads | 0.00 @32, 0.00 @128, 0.02 @512, 0.05 @1024 - against 0.00 / 0.01 / 0.02 / 0.05 |
| fold multiplicity | n folded conversions beside one bounded candidate set | 0.00 @32, 0.01 @128, 0.07 @512, 0.14 @1024 - against 0.00 / 0.01 / 0.07 / 0.15 |
| body nesting | d class templates, each member body calling the one below | 0.00 s flat from d = 4 to d = 32, on both |
| whole PA23 corpus | 417 files, one process each | **2.06 s** against the pre-audit binary's 2.03 s; no `rc > 1`, valgrind clean over 119 inputs |

A bound is one `std::uint32_t` written where a definition is recorded and put
back by a two-assignment scope where it is read, so a body costs one of each
however many times it is read. `find` asks one comparison per namespace lookup
and only where a bound is set at all; the candidate walk asks one per entry on
the 3.4.1 side of a set it already knows the size of. `written_bound` is a test
and a load. Nothing here walks anything it did not walk before.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **343 / 417**, against
  341 / 416 at the turn's start, with no test that passed then failing now: the
  handout set goes 330 → 331 / 405 and the twelfth course fixture is this
  audit's.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- All 417 corpus files compiled one at a time: **0 crashes**.
- 119 inputs under `valgrind -q --error-exitcode=9`: 0 errors.
- 61 probe programs compared against `g++ -std=c++11 -pedantic-errors` and
  against `pa23/cppgm++-ref`, and every accepted one run through `lowir2cy86` +
  `cy86` to the value `g++` runs it to.
