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

## Current Checkpoint Review

Checkpoint 11 - the arguments an alias template's type-id threw away: a naming
whose type-id does not mention a dependent argument keeps an entry holding the
alias, that type and the list; `substituted` builds the arguments and reads the
type-id again; `match` unwraps both sides; 14.3.3p1's place takes an alias; and
14.5.3p5's places travel on the reading that named them while 14.5.3p4's pattern
may be no place at all - was reconstructed from its commit, from `dev/src` and
from the README: which readers ask whether two types are one type, which sites a
naming of an alias template can stand at, which arguments a substitution builds
rather than looks up, and which packs a reading read again can name. One defect
was found and fixed across the whole reader path it reaches, three gaps were
probed as programs and recorded, and the rest is what the review confirmed.

### Findings

**1. 14.5.7p1 is the other half of the sentence 7.1.3p2 opens, and the entry the
checkpoint made stands beside the type instead of being it.** A template-id
naming a specialization of an alias template *is* the associated type its
type-id named. The checkpoint needed the naming to survive as itself so that
14.7.1p1 could build the argument the type-id threw away - which is right - but
made it a second type for one the program can also write out longhand, and the
readers that ask whether two declarations declare one thing all compare types:

```cpp
template<class T> using dis = void;
template<class T> void f(dis<T> *p);
template<class T> void f(void *p) { }          // two templates here, one everywhere else
int main() { int i = 0; f<int>(&i); return 0; }
```

Both oracles and the pre-checkpoint build accept it; this one answered `a call
of f<int> has no best declaration`. Three readers ask the question and each has
its own diagnostic: `TemplateSignature::of`, whose canonical form is 14.5.6.1p5
at all four of its tiers; 13.1's index of a parameter-type-list, which is how a
definition finds the declaration it defines; and the return-type comparison
`declare_function` makes of a pair it has already found. So the same program
written five other ways - a return type, an alias naming another alias, an
out-of-class definition of a member of a class template, a member template of a
class that is not one, and a naming standing under another argument list - was
refused as `a definition of m matches no declaration of it` and `two
declarations of m differ only in their return type`.

The fix is where the two halves of the clause meet rather than at the three
readers. An argument that is a *place* is one 14.7.1p1 looks up: whatever list
arrives has already bound it to a built type, so rebuilding the naming around it
can come to nothing new and nothing can refuse - which leaves `dis<T>` and
`dis<Ts...>` the `void` 14.5.7p1 says they are, wherever they are written.
`rebuilt` is that question, and it is `substituted`'s own answer asked ahead of
it: the four arms it takes before its lookup - a member of a prefix no list has
settled, a specialization over a template place, a naming that discarded an
argument of its own, and an expression a definition left standing - are the
readings a substitution makes again, and every other dependent argument is a
type derived over one of them. So the entry is kept for exactly the arguments
14.8.2p8 has something to fire on, and `discard<pointer_of<A> >`,
`enable_if_t<Bn::ok>...` and `discard<T &>` all keep theirs.

### What the review confirmed rather than found

- **The detector idiom answers at every site a discarding naming stands at.**
  Eight of them - a function parameter, a return type, a default template
  argument, a base-clause, an argument of a class template, a pack expansion, a
  pointer to the naming and a member typedef - each written over a class that
  has the member and one that does not, all eight agreeing with `g++` and
  running through `lowir2cy86` + `cy86` to the value `g++` runs them to.
- **The refusals the entry exists for are still made, and the ones it never had
  are still not.** `discard<T &>` over `void` and `discard<T[2]>` over `void`
  drop the candidate; `discard<T>` over `void` does not, which is what
  collapsing a place means and what `g++` answers.
- **14.5.3p4's pattern that is not a place is one expansion and not two.**
  `held<Args &...>`, `held<const Args...>`, `held<Args *...>`, an alias
  forwarding to another, two patterns over one run, and a run written into a
  function's parameter list all give `sizeof...` the length the list wrote and
  run to `g++`'s value.
- **A reading read again names its packs and no others.** A value argument, a
  decltype-specifier and a head's own default carry the places their spelling
  named; a `sizeof...` operand inside one, a name bound to a pack of another
  length in an enclosing head, and a name that merely spells one do not turn
  into an expansion of it.
- **14.3.3p1's place takes an alias.** A template place bound to an alias
  template names its type-id's type, one bound to a class template instantiates,
  a member read through either is found, and a discarding alias passed through a
  place still collapses where its arguments settle.
- **Every course `.ref` is the reference binary's.** All 22 were regenerated
  from `cppgm++-ref` through the harness and not one changed; the twenty-third
  is this audit's.
- **Nothing is gated and no phase is skipped.** The checkpoint's 548 added lines
  and this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read, no dialect switch keyed on anything but a dialect, and no caught
  exception standing for a success.
- **`valgrind -q --error-exitcode=9` is clean over 145 inputs** - 67 of the
  corpus, the 23 course fixtures and 55 probes of the paths this audit touched -
  and no file of the 400-file corpus exits above 1.

### Recorded rather than fixed

- **14.5.7p1's equivalence where the argument thrown away is a reading.**
  `f(discard<typename T::x> *)` declared and `f(void *)` defined are one
  template in both oracles and two here, because the entry that keeps the
  argument is what 14.8.2p8 needs and what the comparison then tells apart. The
  two facts only meet in a *canonical* form - the associated type, computed
  through the classes an argument list names - which is a checkpoint of its own
  and not a scope. `300-equivalent-alias-return-template-redeclaration.t` is the
  same family one tier down and failed identically before this checkpoint: two
  spellings of one dependent value are two readings here.
- **8.3.1p4 and 8.3.3p3 at a deduction, where the oracles disagree with each
  other.** `template<class T> char probe(T *);` over `T = int &` drops the
  candidate in `g++` and does not in the reference or here; a member pointer to
  a reference or to `void` drops it here and in `g++` and not in the reference;
  and `T C::*` over a non-class `C` is a hard refusal here where both oracles
  drop the candidate. All three answer identically on the pre-checkpoint binary,
  so none is this checkpoint's - and the first has no agreeing oracle to pin.
- **A naming that discarded an argument is rebuilt structurally by
  `TypeTable::substitute`**, which the plan already carries. The path is the
  per-element expansion of a pattern that reaches a settled run, and the two
  shapes reachable through it - a detector under a nested class template - come
  out right, because what the elements hold is settled by then.

### Changes

| Where | What |
|-------|------|
| `sema_specialize.{h,cpp}` | `rebuilt`: whether 14.7.1p1 builds an argument or looks it up, which is `substituted`'s own four arms asked ahead of it. `discarded_arguments` keeps the entry only for an argument it answers yes to, so 14.5.7p1 leaves `dis<T>` and `dis<Ts...>` the type their type-id named and 14.8.2p8 keeps the readings it fires on. |
| `course/pa23/100-an-alias-naming-is-the-type-its-type-id-named.t` | the two halves in one program: five shapes of one template written twice, and the detector and a bare place answering the way each has to. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `08c821a8` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| collapsing-alias multiplicity | n function templates, each declared through `discard<T>` and defined through `void` - the pair the scope restores | 0.00 s @32, 0.01 @128, 0.03 @512, 0.06 @1024 - and the same on the pre-checkpoint binary, which accepts this program |
| discarded-argument multiplicity | n classes x 2 detectors over `discard<pointer_of<T> >`, one `mentions` walk and one re-read apiece | 0.01 @32, 0.02 @128, 0.12 @512, 0.25 @1024 - linear; the pre-checkpoint binary refuses the program |
| discarded-run multiplicity | n calls through `first_of<int, enable_if_t<Bn::ok>...>` | 0.00 @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear; the pre-checkpoint binary refuses it |
| discarded-alias nesting | d nested discarding aliases, each naming the one below beside the member being detected | 0.00 s flat from d = 4 to d = 48 |
| whole PA23 corpus | 400 handout files, one process each | 1.96 s against the pre-checkpoint binary's 1.92 s, over three alternating passes; no `rc > 1`; valgrind clean |

`rebuilt` is a fixed number of field reads per argument of a naming that has a
dependent one at all - the same walk `mentions` was already gated by - and it
runs before `mentions` rather than beside it, so a naming whose arguments are
places now pays *less* than it did: the walk of the type its type-id named is
never made. The corpus is 0.04 s over the pre-checkpoint binary on 400 files,
which is `note_places` looking one name up per identifier of each reading a
pattern leaves standing, and it is the checkpoint's cost rather than this
audit's.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **364 / 423**, against
  363 / 422 at the turn's start, with no test that passed then failing now: the
  eighteenth course fixture is this audit's.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- The failing set of the pre-checkpoint binary was taken as a whole and
  compared: the checkpoint turned 6 tests and regressed none, and this audit
  turns none and regresses none.
- 40 probe programs through `g++ -std=c++11 -pedantic-errors`, the reference
  binary and the pre-checkpoint binary: 7 shapes of one template written twice,
  8 sites of the detector idiom, 6 of 14.5.3p4's pattern, 6 of a reading's own
  packs and 14.3.3p1's place, 5 of the refusals a derived argument makes, 6 of
  8.3.1p4 and 8.3.3p3, and 2 of the structural rebuild. Every one this build
  accepts that is not one of the divergences recorded above runs through
  `lowir2cy86` + `cy86` to the value `g++` runs it to.
- All 400 corpus files compiled one at a time: **0 crashes**; 145 inputs under
  `valgrind -q --error-exitcode=9`: 0 errors.
