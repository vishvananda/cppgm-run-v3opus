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

## Current Checkpoint Review

Checkpoint 9 - the refusals SFINAE had nothing to fire on: 8.3.2p5 and 8.3.4p1
through one door, 10.4p2 as a fact of the type, 5.7p1's completely defined
pointee, 8.5.4p7 at the constructor a list-initialization chooses, and 14.3.3p1
at `instantiate_class` - was reconstructed from its commit, from `dev/src` and
from the README: which readers derive a type, which expressions move a pointer,
which paths reach a constructor with clauses, and which readers ask what an
array is qualified by. Three defects were found and fixed, three gaps were
probed as programs and recorded, and the rest is what the review confirmed.

### Findings

**1. 5.2.1p1's pointee is 5.7p1's, and the subscript was the exit the clause
did not land at.** The checkpoint asked "is the pointee completely defined" of
`+`, `-`, `++` and `--`; `+=` and `-=` inherit it through `compound_operator`.
`subscript_expression` is the fifth, and its own comment restates the clause -
"one operand is a pointer to a completely-defined object type" - above code
that never asked it.

```cpp
struct incomplete_class;
int main() { incomplete_class *p = 0; p[0]; return 0; }   // translated here
```

The reference binary and `g++ -pedantic-errors` both refuse it. `void *p;
p[0];` was worse than accepted: it built an expression of type `void`, which
the tier above then reported as a comparison of unrelated types. And the
detector the checkpoint exists for read back the wrong answer - `decltype(((T
*)0)[0])` over an incomplete class chose the candidate `g++` drops.

**2. 8.5.4p7's fourth bullet was width and equal-width signedness, which is not
"cannot represent all the values of the original type".** Two pairs escape that
statement. A signed source reaching a *wider* unsigned destination has negative
values the destination has at no width at all; and `bool` holds two values
however wide its storage is, which the model spells as a signed type one byte
wide, so neither its range nor its signedness is readable off the width.

```cpp
char g();
int main() { unsigned x[] = { g() }; return (int)x[0]; }   // translated here
int main() { bool     x[] = { g() }; return x[0] ? 1 : 0; }  // and this
```

The same statement was the *gate* on the fold, so those pairs never reached the
bullet's exception either and `unsigned x[] = { (char)-1 }` was accepted as a
constant that fits. The fix states the bullet once - whether the destination
holds every value of the source type - and the gate is that same question, so
the fold is still asked only where the bullet could fire and `W w{ g(x) + g(y) }`
still pays nothing.

**3. 3.9.3p5 landed at one reader of four, and 4.4p4's walk asked its second
condition of the level it had just compared.** The checkpoint made "an array is
as qualified as its element" a fact and read it at `pointer_convertible`'s
4.10p2 arm. Three more readers ask the same question of a pointee that may be
an array: `qualification_convertible` (4.4 itself, which is that function's own
first arm and five other callers'), `composite_pointer` (5.9p2 and 5.16p6) and
`Deduction::qualification_converted` (14.8.2.1p2). Each read `cv` and `strip_cv`
of the array node, which are zero and the array itself, so

```cpp
int a[3] = {1,2,3};
const int (*p)[3] = &a;          // refused here, accepted by both oracles
```

was refused as an initializer, as an argument, as one operand of `?:` and `==`,
and as the argument of a `const T (*)[3]` parameter. Beside it the walk itself:
each descent compares the level it arrives at against an `above_all_const`
covering the levels strictly *above* it, and then the terminal return compared
that same level again, by which time its own const had been folded in - so
`volatile int (*)[3]` off an `int (*)[3]`, and `volatile int *q = p;` with no
array in it anywhere, were refused. `descended` says the pair standing at the
end has already been compared. `200-range-array-reference-mutable-begin.t` is
the handout test that turned.

### What the review confirmed rather than found

- **The three doors are the only three, and they refuse through every reader.**
  A reference to void and an array of a reference, of void, of a function and
  of an abstract class are refused whether the type is derived by the
  declarator walk, by the flattened-spelling reader, by 14.3p1's substitution
  or by a pack expansion of a pattern, and `template<class T> char probe(T
  (*)[1]);` drops the candidate for `T = void` and keeps it for `T = int`. The
  entries the table interns beside them - 13.1's ref-qualifier key and
  14.8.2.1p3's stand-in - stay open, which is what the first landing of the
  rule broke in 24 tests.
- **10.4p2 as a fact of the type is settled everywhere a class can be
  abstract.** A class abstract by an inherited pure virtual is one, an array of
  it is refused, `new De[2]` is refused, and a class template's specialization
  is complete before an array of it can be formed at all.
- **14.3.3p1 at `instantiate_class` answers for every naming.** A class
  template, an alias template, a variable template, a function template, a
  head whose place is a pack, and a naming that declares a pointer and never
  instantiates all refuse `A<W>` where `W` writes `template<int>` at a
  `template<class>` place - and the matching argument is accepted at all six.
- **8.5.4p7 reaches a constructor by seven paths and refuses on all seven** -
  a variable, a temporary, a function argument, a mem-initializer, a
  new-expression, a return statement and an NSDMI.
- **Every course `.ref` is the reference binary's.** All 21 were regenerated
  from `cppgm++-ref` through the harness and not one changed.
- **Nothing is gated and no phase is skipped.** The checkpoint's diff and this
  audit's hold no `getenv`, no fixture name, no timeout, no environment read,
  no dialect switch keyed on anything but a dialect, and no caught exception
  standing for a success.
- **`valgrind -q --error-exitcode=9` is clean over 135 inputs** - 70 of the
  corpus and 65 probes of the paths this audit touched - and no file of the
  400-file corpus exits above 1.

### Recorded rather than fixed

- **`void *p; p[0];` is refused here and by `g++` and accepted by the
  reference**, which implements 5.2.1p1's completely-defined pointee for an
  incomplete class and not for `void`. The clause reads the same for both and
  the reference refuses `void *p; p + 1;`, so the divergence is the
  reference's inconsistency and no fixture writes it.
- **A subscript detector cannot be pinned by a fixture.** The reference refuses
  `template<class T, class = decltype(((T *)0)[0])> char f(int);` outright
  rather than dropping the candidate, so the program both this build and `g++`
  accept has no agreeing oracle to generate a `.ref` from.
- **8.5.4p7's fourth bullet is stricter here than in the reference.** The
  reference implements no part of it that needs the source type's range, so
  every shape this audit fixed is one the reference accepts. `g++` agrees with
  this build on all 900 shapes swept; the plan already carries the same
  divergence for `unsigned u{-1}` and `short s{40000}`.

### Changes

| Where | What |
|-------|------|
| `sema_expression.cpp` | 5.2.1p1's completely-defined pointee at the subscript, the fifth exit of the clause the checkpoint landed at four; `composite_pointer` reads 3.9.3p5 past the dimensions on both operands. |
| `sema_init_list.cpp` | 8.5.4p7's fourth bullet asked once - whether the destination holds every value of the source *type* - with `bool`'s two values and the asymmetry of a sign change named, and the fold's gate is that same question. |
| `sema_overload.cpp` | 4.4's walk reads `object_cv` at every level and compares `object_unqualified` at the end, and `descended` keeps it from asking 4.4p4's second condition of the level it just compared. |
| `sema_deduce.cpp` | 14.8.2.1p2's qualification conversion reads what an array level asks for off its element. |
| `type_model.{h,cpp}` | `object_unqualified`, the other half of `object_cv`: the same type with the qualification an array carries on its element taken off, over the dimension scratch `qualified` already walks. |
| `course/pa23/100-the-qualifiers-an-array-carries-are-its-elements.t` | the four readers of 3.9.3p5 in one program, run to the value `g++` runs it to. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `07cb3fb3` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| narrowed-clause multiplicity | n braced constructions whose clause is a constant reaching a wider unsigned destination - the pair the corrected gate folds for | 0.00 s @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same on the pre-audit binary |
| deep-fold narrowing multiplicity | the same with a 64-deep `constexpr` recursion for a clause | 0.00 / 0.00 / 0.02 / 0.04 - and the same on the pre-audit binary |
| qualification multiplicity | n arguments through 4.4's walk, which every candidate asks | 0.00 / 0.00 / 0.01 / 0.03 - and the same |
| array-qualification multiplicity | n arguments adding const at an array's element | 0.00 / 0.00 / 0.01 / 0.03 - the pre-audit binary refuses the program |
| subscript multiplicity | n subscripts, one `is_incomplete` apiece | 0.00 / 0.00 / 0.01 / 0.02 - and the same |
| array-dimension nesting | d dimensions under one qualification conversion | 0.00 s flat from d = 4 to d = 32 |
| whole PA23 corpus | 400 handout files, one process each | **1.87 s** against the pre-audit binary's 1.88 s; no `rc > 1`; valgrind clean |

`object_unqualified` is `unqualified` for everything that is not an array, which
is what `strip_cv` already was, and `object_cv` is one `kind` test more than
`cv` - so 4.4's walk costs what it cost. For an array it walks the dimensions
over the scratch `qualified` already uses and interns nothing a program did not
already write. The narrowing gate asks the fold on two pairs more than it did,
both of which the bullet would refuse without one, so a program that keeps
compiling pays for folds whose answers it needed.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **356 / 421**, against
  354 / 420 at the turn's start, with no test that passed then failing now:
  `200-range-array-reference-mutable-begin.t` turned and the sixteenth course
  fixture is this audit's.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- 900 narrowing shapes against `g++ -std=c++11 -pedantic-errors`: 225 of the
  integral cross product with a non-constant source and 675 of five source
  types crossed with fifteen destinations and nine values. All 900 agree.
- 20 qualification shapes through eight syntactic sites against both oracles:
  20 of 20 agree with `g++`, and the 15 accepted ones run through
  `lowir2cy86` + `cy86` to the value `g++` runs them to.
- All 400 corpus files compiled one at a time: **0 crashes**; 135 inputs under
  `valgrind -q --error-exitcode=9`: 0 errors.
