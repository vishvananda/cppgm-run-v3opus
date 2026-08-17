# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_`, so a template-name at a function-tier place was unchecked in both directions; 14.6.1p1's injected-class-name arm swallowed a template-*id* written at a place; 14.3.3p1's pack matched no run of none and compared only each place's kind; and `match_template_id` read `L<A…>` against a class alone, so 14.5.5.2p1 ordered no two patterns over a template place and 14.8.2.1's two allowances reached neither |
| A | `202f04ec` | 4 / 4 + 6 recorded | **the three things a template-id can be looked up in, with 14.2's own exit written at two of them - and the access 11p1 gives a member template, carried by one of the three tiers that make a declaration from an argument list.**  Checkpoint A landed 14.5.7p1's alias template and moved 14.2p4's keyword into `QualifiedName::part`.  The alias is right and swept clean: 7.1.3p2's transparency, 14.5.7p2's identity, the ABI name, the region a member alias's access travels from, the memo that keeps a nest naming the one below twice flat to depth 24 - 84 probes pass the assignment's own comparator against the reference.  What the keyword's move did not carry is the *reason* it was moved: the commit names `a.template f<A>()` as one of three refusals it ends, and `member_named` - 5.2.5p1's lookup, the third of the three - had no 14.2 exit at all, so every `a.template f<int>()`, `p->template f<int>()` and `this->template f<int>()` was still `no declaration of f<int> is in scope` where both oracles translate.  Beside it the move made `last()` shorter than the span it was taken from, and three readers took the nested-name-specifier off the spelling by that length: `d.B::template f<int>()` came out as `B::templat names no class`.  Under the new exit, 11p1's access is a fact `instantiate_class` and the alias arm both write onto the declaration one argument list makes and the two function-tier exits never did, so a *private* member template reached through `.` was accepted where both oracles refuse.  And `template_id_entity` may now answer with a typedef-name: 14.7.2p2's reader took `made.primary` without asking, so `template struct P<int>;` over an alias **segfaulted** where both oracles refuse |
| P | `0d1c75c9` | 4 / 4 + 6 recorded | **the two forms 14.7.2 writes one requirement in, with p2 asked at one of them - and the parse form the same commit opened, which reached no reading at all.**  Checkpoint P made every PA22 test parse: 14.2p4's keyword became optional at a member name, 14.2p1's operator template-id got one shared `skip_template_arguments`, and 14.7.2p1's declaration was let end at a `;` no out-of-class constructor otherwise may.  The member reading is right and is *narrower* than the reference's - `s.a < 1 > (0)` over two `int` members is two comparisons here and refused there - and 13.5p6 at the specialization agrees with `g++` on all five shapes probed.  What the commit's own comment claims and the code does not is that "the target is read either way": `explicit_instantiation` returned on `!owed` **before** reading anything, so `extern template struct P<int>;` over an alias, over a non-template, over a name no template declares and over a prefix naming no type were four programs `g++` refuses and this build accepted - the p9 twin of the segfault checkpoint A's audit had just fixed.  Beside it `is_explicit_instantiation_target` began accepting a `SpecialMemberDeclaration` that `explicit_instantiation` had no arm for: `extern template box<int>::box();` passed only because the early return got there first, and `template box<int>::box();` came out of the *class* arm as `an explicit instantiation defines a class`.  And the parser's `names_specialization_` was taken once and never put down, so every body under such a head read its statements as out-of-class special members: `template<> void f<int>(int) { A::A(); }` was refused where the same statement in an ordinary function is accepted |
| M | `d2e26a4d` | 6 / 6 + 6 recorded | **the two facts 12 writes about a special member, read off the template and never off the declaration one argument list makes - and the class 12.1's second entry point is owed by.**  Checkpoint M, M2 and the M audit gave a head over a constructor and over a conversion function the class's own declaration, at all four exits it can be written at, and 14.5.6.1p5 is what matches a definition to it.  The declaration side is right and swept clean: 30 shapes over those four exits - value places, packs, defaults, nests, `const`, a base's, a mem-initializer, an out-of-class definition that renames its place - translate and run the value `g++` gives them.  What none of it carried is what 12 says about the *declaration*: `specialize` copied `object_member`, `access` and M2's `special` and left 12.3.1p2's `explicit` and 8.4.3's `= delete` default-constructed, so `only_direct a = 3` and a call of a deleted constructor template were two programs both oracles refuse and this build accepted.  12.3.2p1's conversion function template was landed and **unreachable**: 13.3.1.5p1's candidate set held the template, whose result type is a place, so `int k = a;` was `an expression has no conversion to the type it initialises` where both oracles translate it - the ref binary's own refusal of `a.operator int()` had been recorded as the whole of the gap and it was the *named* exit alone.  The ABI wrote such a specialization's name from the substituted type, `_ZN1AcviIiEEv` against the `_ZN1AcvT_IiEEv` `g++` and the reference both write, at the one of `build_function_name`'s three readings `templated` had not been threaded to.  And a constructor template reached only through a base subobject wrote both of 12.1's entry points where the reference writes one: `writes_base_entry` asked whether the *function* is an instantiation, which for a special member is true on exactly this checkpoint's new surface and nowhere else |


## Current Checkpoint Review

Checkpoint M is where 14.5.2p1's member template became a member. A head
written over a constructor or a conversion function inside a class body reaches
neither `function_definition` nor `declare_function`, so the class-body walk had
been sending it to `special_member_definition`, where an unqualified constructor
name reads as an out-of-class definition of nothing. M sent it to
`special_member` with `declaring_region` standing between the head and the
class; M2 wrote the three sibling exits its definition can be written at, with
`StandingIn` putting the head inside the region the declarator-id names; and the
M audit found 14.5.2p1's destructor and 14.5.6.1p5's second declaration of one
constructor template by a `g++ -pedantic-errors` verdict sweep.

The declaration side is right, and it is right at every exit. Thirty shapes over
those four exits - a value place, a pack, a default argument, a
`const` object parameter, a nest two classes deep, a member of a class template,
a mem-initializer of a derived class, an out-of-class definition that renames
the place its declaration wrote - translate here, in `pa22/cppgm++-ref` and in
`g++`, match the reference's LowIR through the real comparator, and run the
value `g++` gives them through `lowir2cy86` + `cy86`.

What the review found is one thing said four ways: **a fact 12 writes about a
special member was read off the template, and never off the declaration one
argument list makes of it.**

### Findings

**1. 14.7.1p1's specialization carried neither of 12's own two facts.**
`specialize` copies `object_member`, `access`, the exception-specification and -
since M2 - `special` and `conversion_function`. 12.3.1p2's `explicit` and
8.4.3's `= delete` are facts the same declarator wrote and no argument list
changes, and 13.3 reads both off the declaration it chose, before any body of it
is instantiated. So

```cpp
struct A { int n; template<class U> explicit A(U u) : n((int)u) { } };
A a = 3;                                 // accepted; both oracles refuse

struct B { int n; B() : n(0) { } template<class U> B(U) = delete; };
B b(3);                                  // accepted; both oracles refuse
```

were two programs `pa22/cppgm++-ref` and `g++ -pedantic-errors` both refuse and
this build translated. They are copied where `special` is, which is the one
place the declaration is built from the template's type and never from that
declarator again.

**2. The conversion function template was landed and reachable by nothing.**
13.3.1.5p1's candidate set is the conversion functions the class holds, and a
*template* hands back a place - so `conversion_result` produced a value of type
`U`, `match_argument` reached nothing, and every use of the feature checkpoint M
had just landed was refused:

```cpp
struct A { int n; A(int x) : n(x) { } template<class U> operator U() { … } };
int k = a;                               // refused; both oracles translate it
```

The plan recorded 14.8.2.3 as a gap and recorded only its *named* exit -
`a.operator int()`, which the reference refuses too - so the exit where both
oracles disagree with this build had been read as covered. `Deduction`'s
`from_conversion` is 14.8.2.3's one P/A pair: 12.3.2p1 writes the
conversion-type-id where every other function writes its result, so P is that
result and A is the type 13.3.1.5p1 asked the class to reach, with p2's
reference and cv allowances where A is no reference. It is asked at
`conversion_match` and at neither of the other three readers of
`gather_conversions`: 6.4.2p2's selection, 13.6's built-in operand and
5.3.4's array bound each filter on the result's *kind*, and a place is none of
the kinds they take - so a template is no candidate there for the same reason
14.8.2.3 gives it none.

**3. 13.3.3p1's last two questions had never been asked of that set.** With
templates in it, `explicit operator bool` and an `operator T` that deduces
`bool` are two candidates that get equally far, which left a contextual
conversion ambiguous - and `300-contextual-logical-bool-prefers-nontemplate-`
`conversion.t`, a fixture written for exactly this, went from passing to
failing the moment the deduction landed. A function the class declared beats a
specialization that is no better, and two specializations are ordered by
14.5.6.2 - the same last pair of questions `better_candidate` asks of any two
candidates of a call.

**4. The ABI named such a specialization from the substituted type.**
`build_function_name` already reads a function template's parameter list and its
result record off `templated` - the *template's* type, where a place stands for
itself - and 12.3.2p1's conversion terminal was the one reading left on
`entity.type`. So this build wrote `_ZN1AcviIiEEv` where `g++` and
`pa22/cppgm++-ref` both write `_ZN1AcvT_IiEEv`: two oracles, one symbol, and a
unit that would have linked against neither.

**5. `writes_base_entry` asked the question of the function.** A constructor
template of a class the program itself wrote, reached only through a base
subobject, wrote both of 12.1's entry points where the reference writes one -
`abi_instantiated` is true for any specialization of a function template, and
what says a unit owes a constructor whole is that the *class* is what a
template-id named. `abi_instantiated_class` is that reading. It is provably this
checkpoint's surface and no other: the caller has already refused every
`kOrdinaryFunction`, and 14.5.2p1 leaves a destructor no head - so the two
readings differ on constructor templates alone. A class template's own
constructor still writes both, which is what the reference does there too.

**6. 8.4.2p1's `= default` under a head.** 12.1p5's and 12.8p2's implicit
declarations are written over no places, so a head leaves `= default` nothing
the standard would write. `g++` refuses all four spellings of it and this build
accepted all four. The refusal is `require_template_special_member`, beside
`declares_member_template` in the vocabulary both belong to, which also takes
over 14.5.2p1's destructor from the two places that had written it out - one
rule, one reader, the four exits a special member's declaration and its
definition can be written at.

### What the review confirmed rather than found

- **The four exits M and M2 wrote are the four exits.** `template<class U> A(U)`
  in the class body, `template<class U> A::A(U)` outside it, `template<class T>
  template<class U> A<T>::A(U)` and the conversion function's own two all
  declare one template, match by 14.5.6.1p5 when the heads name their places
  differently, and reach 12.6.2's mem-initializers with the class's members.
  `template<class T> A<T>::~A() {}` is untouched by the destructor refusal.
- **The deduction costs one walk and one memo.** `specialize` is keyed on the
  template and the interned argument list, so n uses of one destination deduce n
  times and specialize once; n distinct destinations are n specializations and
  no re-reading of any. The sweep is linear to 800 and to depth 256.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch, a timeout or an
  environment read; every new exit is reached in all three dialects alike.
- **valgrind is clean** over all 72 probe programs and the five largest scaling
  inputs, 0 errors.

### Recorded, not landed

- **A conversion function template is keyed by the spelling of its
  conversion-type-id.** `operator U()` and `operator V()` over one head are two
  members of the class here (`g++` refuses, the reference accepts), and
  `template<class V> A::operator V()` defining a class's `operator U()` matches
  no declaration here and in the reference where `g++` translates it. 14.8.2.3
  at the *named* exit rests on the same fact. A canonical name built from the
  head's own stand-ins answers all three, and it renames a member - which is
  PA11's and PA12's output as much as this stage's, so it is a checkpoint and
  not an audit's edit.
- **9.2p1 is still enforced nowhere**, which is why the first of those stays
  accepted rather than becoming this stage's fifth refusal.
- **The reference keeps one specialization of a conversion function template
  across two units**: `--emit-lowir a.cpp b.cpp` where each deduces a different
  destination writes only the first unit's. `g++` writes both and so does this
  build.
- **An empty out-of-class destructor of a class template** is elided by 12.4p8
  here where the reference and `g++` write the definition. A non-empty one
  agrees exactly, and the `d2e26a4d` build does the same - it is not the
  member-template path.
- **Nine shapes the reference accepts and `g++` refuses with this build**: the
  destructor template at both exits, one constructor template declared twice,
  one defined twice, a `virtual` member template, and the four `= default`
  spellings. Each is the reference's error recovery on synthesized input.
- **`virtual` on a member template** is refused as `f is declared \`virtual\`
  outside the body of a class`, which is the right verdict read off the head's
  region rather than the class's. 14.5.2p3 is the clause; the diagnostic is not.

## Changes

- **`sema_template.cpp` — `specialize`**: 12.3.1p2's `explicit` and 8.4.3's
  `= delete` are facts of the declaration the template declares, so one argument
  list makes a declaration of them too.
- **`sema_deduce.h/.cpp` — `Deduction::from_conversion`**: 14.8.2.3's one P/A
  pair, with p2's reference and cv-qualification allowances where A is no
  reference type.
- **`sema_overload.cpp` — `conversion_match`**: 13.3.1.5p1's candidate set
  reaches a conversion function template through the specialization the
  destination deduces, and 13.3.3p1's last two tie-breaks - a function the class
  declared over a specialization, and 14.5.6.2 between two - now that the set can
  hold one.
- **`lowir_abi.cpp` — `build_function_name`**: 12.3.2p1's conversion terminal
  read off `templated`, where the parameter list and the result record already
  are.
- **`lowir_lower.cpp` — `writes_base_entry`**: 12.1's second entry point is owed
  where the *class* is what a template-id named, which is `abi_instantiated_class`
  and not `abi_instantiated`.
- **`sema_scope.h/.cpp` — `require_template_special_member`**, called from
  `special_member`, `conversion_function` and `special_member_definition`:
  14.5.2p1's destructor and 8.4.2p1's `= default`, one reader for the four exits
  a head over a special member can be written at. It is also what freed
  `special_member` from the file audit's 240-line ceiling, which it stood 4 lines
  under.

## Performance Evidence

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22c`,
against a `make build` of `d2e26a4d` in a worktree so every number has a
baseline rather than a memory, and against `pa22/cppgm++-ref`.

| shape | this build | `d2e26a4d` | `pa22/cppgm++-ref` |
| --- | --- | --- | --- |
| a conversion function template, n *distinct* destinations, 100 → 800 | 0.01 / 0.02 / 0.05 / **0.10 s** at 10 → 35 MB | refused | 2.00 s at 800 |
| a conversion function template, n uses of one destination, 100 → 800 | 0.00 / 0.01 / 0.01 / **0.03 s** at 7 → 15 MB | refused | 0.66 s at 800 |
| n classes each with a non-template and a template conversion, 100 → 800 | 0.02 / 0.04 / 0.08 / **0.17 s** at 12 → 53 MB | 0.02 / 0.04 / 0.08 / 0.17 s | 1.29 s at 800 |
| n constructor templates × n calls, 100 → 800 | 0.03 / 0.07 / 0.27 / **0.99 s** at 12 → 52 MB | 0.03 / 0.07 / 0.28 / 1.06 s | 46.61 s at 800 |
| a conversion template reached through a derivation, depth 4 → 256 | 0.00 / 0.00 / 0.00 / **0.01 s** at 6 → 11 MB | refused | 1.13 s at 256 |
| the whole 313-file corpus, one process per file | **1.33 s** | 1.31 s | — |

Every dimension is linear in what it sweeps and every one that had a baseline
matched it. The three rows the baseline refuses are what 14.8.2.3 opened, and
they are linear because the deduction is one `match` walk of the
conversion-type-id against the destination and `specialize` is memoised on the
template and the interned argument list - n uses of one destination deduce n
times and specialize once, and n distinct destinations are n specializations and
no re-reading of any. The one quadratic row is 13.3's own cross product and it
came out 0.07 s *faster* than the baseline. Nesting sweeps were run to depth 256
and multiplicity sweeps to 800.

A trap worth the line it costs: `g++ file.t` treats a `.t` as a **linker input**
and exits 0 with a warning, so a verdict sweep run without `-x c++` reads every
ill-formed program as accepted. The first pass of this audit's sweep did exactly
that, and four refusals read as agreements until the flag went in.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` — **229 / 318**, which is the
  turn's 224 / 313 with the 5 fixtures this audit wrote, and the failing set
  byte-identical to the turn's.
- `make test-report-through-pa21` — **pass**, 2568 / 2568, 21 / 21 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa22 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
  `special_member` stood 4 lines under the 240-line ceiling before this audit
  wrote anything, so the refusals it owed went to a reader of their own in
  `sema_scope` rather than into it - which is where 14.5.2p1's vocabulary
  already lives.
- 72 systematic probe programs swept against `pa22/cppgm++-ref` and
  `g++ -std=c++11 -pedantic-errors -x c++`: 30 over the four exits a member
  template's declaration and definition can be written at, 10 over what one
  argument list carries onto the declaration, 10 over the conversion function
  template's uses, 9 controls with no member template in them, 5 over
  `= default` and the destructor, 3 over the entry points a base subobject asks
  for, 3 that return a computed value, and 2 over source order.
  Every disagreement judged against the standard and the third oracle rather
  than copied: this build now agrees with `g++` on **all 72**, and with the
  reference on 62.
- All 72 through `pa22/scripts/compare_results.pl` itself in a scratch directory
  under `pa22/`, with `KEEP_GOING=1` so the run does not stop at the first
  difference: **62 pass**, and the 10 that do not are the recorded
  disagreements above - 9 verdicts the reference recovers from and 1 empty
  destructor the reference writes and this build elides.
- Run evidence through `lowir2cy86` + `cy86`: the conversion and constructor
  shapes exit the value `g++ -std=c++11` gives them, including three that return
  a computed value rather than 0 - **25, 21 and 23** against g++'s 25, 21 and 23.
- Third oracle on the symbol: `_ZN1AcvT_IiEEv` and `_ZN1AC1IiEET_` agree with
  `g++` byte for byte, and `nm` on the two-unit probe shows `g++` writing both
  specializations of one conversion function template where the reference writes
  the first unit's alone.
- `valgrind -q --error-exitcode=9` over all 72 probes and the five largest
  scaling inputs: **clean**, 0 errors.
- Every `course/pa22` `.ref` regenerated from `pa22/cppgm++-ref` through the
  harness: the five checkpoint M wrote came back byte-identical, so none of them
  was holding this build's own output, and the five this audit added are the
  reference's.
