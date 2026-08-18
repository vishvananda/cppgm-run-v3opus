# PA23 Audit — deduction, substitution and SFINAE

A review of each landed checkpoint, in the order one use of a function template
travels: what a deduction settles, what building the declaration it settled
means, and what a failure of that building says about the candidate that asked.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| 1 | `188e92bc` | 3 / 3 + 3 recorded | **the scope 14.8.2p8 makes of one attempt, landed with no bound on the attempt asking for itself - and the order the same sentence says that attempt is made in.**  Checkpoint 1 made a substitution failure candidate state: `Substitution` wraps the three deduction entry points and the written-argument-list `specialize`, `Instantiated` is the class body 14.7.1p1 read walking past it, 14.1p3's unnamed place is declared, a dependent value argument keeps its spelling and is read again, `dependent_member_type` builds over the class *this* substitution made, and 14.5.6.1p5 rather than 13.1's index pairs two templates.  Those rules are right and swept clean - 8 unnamed-place shapes, 10 SFINAE and qualification-conversion shapes including the multi-level pointer 4.4 refuses, 6 shapes of the `Instantiated` boundary, and the discarded attempt leaves nothing a later naming reads, because the failure happens before `hold_specialization` and the class instantiations it made are 14.7.1p1's and permanent.  What none of it carried is that the same clause bounds the attempt: `Specialization::chosen` and `SemaAnalyzer::specialize` each hold their answer only once it is built, so a request arriving while that same one is being served recursed without bound - and the two new readings closed the loop, a dependent value argument read again at substitution and a stand-in rebuilt over the class the arguments named.  `300-recursive-streamable-sfinae-guard.t` and `300-dependent-adl-hidden-friend-before-later-value.t` are two programs the pre-checkpoint build translated to exit 0 and this one **exhausted the stack on**, reported by the harness as ordinary `EXIT_FAILURE` mismatches, with `300-recursive-trailing-return-sfinae-cache.t` the same crash one tier down and older than the checkpoint.  `TemplateInfo::choosing` and `SemaAnalyzer::specializing_` are the mark each tier takes, and a re-entrant request is refused - which 14.8.2p8 turns into the candidate discarded, the answer `g++` gives all three.  Beside them: 14.8.2p8's *lexical order*, which `substituted` read backwards for every declarator - the parameter list was built first however the return type was written, so a leading result type whose substitution is a hard error was never reached when a parameter was what failed, and `SemaEntity::trailing_result` is 8.3.5p2's fact that says which came first; and 14.1p12, which has no door at the pair 14.5.6.1p5 had just made findable, so two heads of one template each giving one place a default was a program both oracles refuse and this build translated.  Recorded rather than fixed: 14.6p2 is asked where the *pattern* reading reads a type-specifier, so five shapes whose reading a dependent context defers reach the clause with a prefix an argument list has already settled; and n declarations of one template name is quadratic in PA22's `TemplateSignature::equivalent` walk |

## Current Checkpoint Review

Checkpoint 1 was reconstructed from its commit, from `dev/src` and from the
README: what a substitution attempt is a scope over, what it may leave behind,
which refusals cross its boundary, and what each dimension it opened costs.
Three defects were found and fixed, three gaps were probed as programs and
recorded, and the rest is what the review confirmed.

### Findings

**1. The attempt had no bound on asking for itself, so two programs the
pre-checkpoint build translated ended in a stack overflow — and the suite called
them ordinary failures.** 14.7.1p1 makes one specialization per template and
argument list, and both tiers that build one hold their answer only once it is
*built*: `Specialization::chosen` writes `info.chosen` after every pattern has
been matched, and `SemaAnalyzer::specialize` calls `hold_specialization` after
the substitution has returned. So a request for a list already being served is
not a second naming — it is that same service asking for itself, and neither
tier could tell the two apart:

```
#0  SemaModel::create              <- stack exhausted
...
#34 Specialization::chosen         <- is_streamable<Sink&, Value&>
#33 Specialization::matches
#32 Specialization::substitution_agrees
#27 SemaAnalyzer::substituted      <- void_t<decltype(declval<S>() >> declval<T>())>
#26 SemaAnalyzer::decltype_type
#20 SemaAnalyzer::select_overload
#19 Deduction::from_call           <- operator>>(Stream&&, T&&)
#17 SemaAnalyzer::specialize
#13 SemaAnalyzer::substituted      <- enable_if_t<is_streamable<Stream, T>::value, int>
...                                <- and round again
```

The checkpoint is what closed the loop: a dependent value argument now keeps its
spelling and is read again at substitution, and `dependent_member_type` now
rebuilds a stand-in over the class the substitution made, so a default template
argument written over `is_streamable<Stream, T>::value` reaches the choice that
is being made for it. `tests/general/300-recursive-streamable-sfinae-guard.t`
and `tests/general/300-dependent-adl-hidden-friend-before-later-value.t` are
**exit 0 on `188e92bc~1` and SIGSEGV on `188e92bc`**;
`tests/spec/300-recursive-trailing-return-sfinae-cache.t` is the same cycle at
the function tier alone and crashed before the checkpoint too. The harness
reports all three as `tool exit status mismatch (expected EXIT_SUCCESS, got
EXIT_FAILURE)`, which is why a green-looking 288 held three crashes.

`TemplateInfo::choosing` and `SemaAnalyzer::specializing_` are the mark each
tier takes for as long as it is serving one list, and both come off however the
service ends — a substitution that failed is 14.8.2p8's candidate discarded and
says nothing about the template, so a later naming of the same list is entitled
to build it again. A request that finds the mark standing is refused, and that
refusal is what the reading around it wanted: 14.8.2p8 discards the candidate
that asked, the enclosing overload resolution picks the non-template
`operator>>`, the `decltype` settles, the partial specialization matches, and
`is_streamable<Sink&, Value&>::value` is `true` — the answer `g++` gives. All
three fixtures translate now and the whole 400-file corpus has no crash left.

**2. 14.8.2p8's substitution proceeds in lexical order, and the result type was
built last however the declarator wrote it.** `substituted`'s `TypeKind::Function`
arm builds the parameter list and only then asks for the result type, so a
result type written *before* the parameter-clause was reached only when every
parameter had already substituted. The clause says the substitution stops at the
first failure, so which came first is what decides whether a hard error in one of
them is reached at all:

```cpp
template<class T> struct hard { static_assert(sizeof(T) == 0, "hard"); typedef int type; };
template<class T> using result = typename hard<T>::type;
template<class T> struct gate {};
template<class T> result<T> choose(T, typename gate<T>::type = {});
int choose(...);
int main() { choose(0, 0); }        // g++ refuses; this build translated
```

The order is the declarator's and no fact of the function type the two came to,
so `SemaEntity::trailing_result` is 7.1.6.4p1's `auto` carried onto the
declaration, and `specialize` asks for the result type first wherever it was
written first — the walk below reads it last either way and the memo hands the
same answer on, so nothing is substituted twice. `tests/spec/300-leading-return-
alias-substitution-order-bad.t` is the fixture.

**3. 14.1p12 had no door at the pair 14.5.6.1p5 had just made findable.** The
checkpoint stopped 13.1's index from pairing two templates and made
`TemplateSignature::equivalent` the answer — which is the first time two heads
the program wrote for one template are known to be one declaration. Nothing then
asked what the two heads say about one place:

```cpp
template<class T = int> int inspect(T value = 7) noexcept(sizeof(T) != 0);
template<class U = int> int inspect(U value) noexcept(sizeof(U) != 0) { return (int)value; }
// g++ refuses the repeated default; this build translated
```

`require_one_default_per_place` walks the two heads position by position, which
is the same pairing the comparison was made by, and refuses where one position
has a default from both. 14.6p8's reading of a pattern and 14.7.1p1's reading for
each specialization declare the friend function template a class body wrote with
the head it wrote *each time*, so the question is asked of what the source
declared at its own level and a re-reading is no second declaration — six
sibling shapes (a default on one declaration only, defaults split across two
positions, a member template defined out of class, a friend template in a class
instantiated twice, a value place, a class template beside it) all still
translate and agree with `g++`. `tests/general/300-bad-noexcept-alpha-
redeclaration-default.t` is the fixture.

### What the review confirmed rather than found

- **The attempt leaves nothing behind that a later naming reads.** A function
  template's substitution throws before `model_.hold_specialization`, so no
  half-built declaration is interned; `Substitution::discards` puts `stood_in_`
  back where the attempt found it; and the class specializations a failed
  attempt instantiated are 14.7.1p1's own and permanent, which is what makes a
  re-attempt a probe rather than a second substitution. The nesting sweep is
  flat because of it: d nested `enable_if` chains under three candidates is
  0.01 s from d = 8 to d = 48, where a fallback that re-read would be 2^d.
- **14.1p3's unnamed place is right at both tiers.** Eight shapes — a partial
  specialization over `template<typename, typename>`, a two-place function
  template, a detector's `class = void`, a leading value place, an unnamed pack,
  a template-template place, one place alone, and an unnamed place before a
  named one — translate and run the value `g++` gives them. The PA10 dump still
  writes no line for a place no identifier named.
- **14.8.2.1p4's qualification conversion admits no pair 4.4 refuses.** Ten
  shapes agree with `g++`, including `probe(const T**)` against `int**`, which
  the deduction takes and the call then refuses exactly as `g++` does, and
  `probe(T* const*)`, `probe(const volatile T*)` and a derived-class argument.
- **The `Instantiated` boundary is where the clause draws it.** Six shapes: a
  `static_assert` in a class a substitution instantiated refuses the program
  from a return type, a parameter, an array bound and a member's type alike,
  while `enable_if<false, T>::type` and a missing `typename T::missing` discard
  the candidate. The one shape that parts from `g++` is a *dynamic*
  exception-specification's type-id, which no reading here reaches at all — the
  gap PA22's own audit recorded.
- **Nothing is gated and no phase is skipped.** The checkpoint's whole diff and
  this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read, no dialect switch keyed on anything but a dialect, and no caught
  exception standing for a success: the two new `catch` blocks each end in a
  `throw` or in a candidate the clause says is not one.
- **`valgrind -q --error-exitcode=9` is clean over 84 inputs**: the 78 audit
  probes, the three cycle fixtures and the three scaling inputs.

### Recorded rather than fixed

- **14.6p2 is asked at the reading that reads a type-specifier while the prefix
  is still a place, which is the declaration exits alone.** `T::type v = 0;`
  written in a template no use instantiates is refused; `static_cast<T::type>(0)`
  in the same template is not read until the arguments have settled `T`, and by
  then the prefix is a class and the clause no longer applies. So
  `holder<T::type>`, `static_cast<T::type>(0)`, `sizeof(T::type)`, `(T::type)0`
  and `template<class T, class U = T::type>` are five programs `g++` refuses and
  this build translates. Answering them means reading the written form of a
  deferred type-id where it stands, which is a reading this stage does not have.
- **The clause is still narrower than the standard's, for the reason the plan
  gives.** `require_written_type` refuses only a prefix that is a bare place of
  its own head. `pa20/course/pa20/100-a-decltype-an-argument-list-wrote-is-a-
  tree.t` writes `box<decltype(make())>::type` inside a template and its `.ref`
  pins `EXIT_SUCCESS`, so a dependent *template-id* prefix cannot be refused
  here.
- **n declarations of one function template name is quadratic.** 13.1's index
  cannot key a declaration written under a head by what 14.5.6.1p5 asks, so
  `declare_function` walks the chain: 0.09 s at 800, 0.24 s at 1600 and 0.80 s
  at 3200. The walk is PA22's — the pre-checkpoint build is 0.64 s at 3200 for
  the shape that already reached it — and the checkpoint made the
  same-parameter-list shape reach it too, which it used to refuse outright with
  `over is defined twice`. Keying the index by the canonical form would answer
  it in one probe and is a change to what 7.3.3p14's hiding key is built from.

### Changes

| Where | What |
|-------|------|
| `sema_template.h`, `sema_specialize.cpp` | `TemplateInfo::choosing` and `Choosing`: 14.7.1p1's cycle at 14.5.5.1p1's choice, refused rather than recursed. |
| `sema_analyzer.h`, `sema_template.cpp` | `SemaAnalyzer::specializing_` and `Building`: the same cycle at the function tier, where one declaration is held only once it is built. |
| `sema_scope.h`, `sema_scope.cpp`, `sema_function.cpp` | `SemaEntity::trailing_result`, 8.3.5p2's fact about which of the result type and the parameter list the declarator wrote first. |
| `sema_template.cpp` | 14.8.2p8's lexical order: `specialize` asks for a leading result type before the parameter list. |
| `sema_function.cpp` | `require_one_default_per_place`, 14.1p12 over the pair 14.5.6.1p5 settles. |

### Performance Evidence

Measured on the audited binary, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| SFINAE multiplicity | n classes × 4 candidates, 3 failing substitution | 0.01 s @32, 0.04 @128, 0.18 @512, 0.34 @1024 — linear |
| substitution nesting | d nested `enable_if` chains under 3 candidates | 0.01 s flat from d = 8 to d = 48 |
| dependent value nesting | d nested calls each deducing `X<A + 1>` | 0.00 s @12, 0.01 s @48 |
| declarations of one name | n function templates, one parameter list, distinct result types | 0.04 @400, 0.09 @800, 0.24 @1600, **0.80 s @3200 — quadratic**, PA22's chain walk (pre-checkpoint 0.64 s at 3200 for the shape that already reached it) |
| whole PA23 corpus | 400 files, one process each | **1.94 s** warm, no crash |

The cycle marks cost one hash insert and one erase per choice and per
specialization built, both of which already intern a list; nothing is re-read
and no dimension moved.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` — **292 / 400**, against 288
  at the turn's start, with no test that passed then failing now.
- `make test-report-through-pa22` — **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` — **pass**,
  with the five `bad-division` warnings it already had.
- All 400 corpus files compiled one at a time: **0 crashes**, against 3 before.
- 84 inputs under `valgrind -q --error-exitcode=9`: 0 errors.
- Every probe in this audit compared against `g++ -std=c++11 -pedantic-errors`,
  and every accepted one run through `lowir2cy86` + `cy86` to the value `g++`
  runs it to.
