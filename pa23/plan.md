# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `ast_parser_class.cpp` — 14.1p3's abstract declarator and 10p1's
  `class-or-decltype`, whose operand tree the arena keeps beside its spelling.
- `sema_template_head.*` — 14.1p2's places, 14.1p9's defaults, the bound each
  was written under, and 14.3.3p1 of a whole settled argument list.
- `sema_deduce.{h,cpp}` — 14.8.2. `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, and `Instantiated` is the error that escapes it.
- `sema_specialize.cpp` — 14.5.5.1p1's match, 14.5.5.2p1's ordering, 14.5.1p1's
  variable template and 14.5.7p1's alias.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and `DependentReadings`: the constructs a pattern left
  standing, each with the region and the bound 14.7.1p1 reads it again under.
- `sema_scope.{h,cpp}` — 3.4's lookups, and 14.6.4.2p1's bound on them:
  `declared_serial` per namespace-bound declaration, `ReadingBound` per
  reading, `written_bound` per construct being recorded. Every definition
  14.7.1p1 reads again carries the bound it was written under - the pattern,
  each partial specialization's body, each out-of-class member definition, each
  body `template<>` wrote out, and each body a reading put aside.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5 and
  14.5.6.2's ordering of two function templates.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused,
  gathers 13.3.1.4p1's converting constructors, and asks 14.6.4.2p1 of 3.4.1's
  half of a set and not of 3.4.2's. `sema_constexpr.cpp` draws the same line
  where a fold gathers a set of its own.
- `sema_layout.cpp` / `type_model.cpp` — 5.2.2p4's boundary, which reads
  12.8p12's copy, 12.4p8's destruction, 3.9.1p8's floating storage and 10.4p2's
  abstract class; and 8.3.2p5 / 8.3.4p1's *door*, which is the entry a
  declarator derives a type through as against the entry that interns one.
- `sema_expression.cpp` / `sema_lifetime.cpp` / `sema_init_list.cpp` — the
  refusals a substitution failure is made of: 5.7p1 and 5.2.6p1's completely
  defined pointee, and 8.5.4p7's narrowing of a clause through 13.3.3.1p4.
- `lowir_abi.cpp` — 14.2's encoding of an argument list.

## Current Failure Map

430 tests (400 handout + 30 course), 377 passing (handout-only 347 / 400). The
53 left, all of them handout, by the compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a call or name no declaration answers | 10 | the candidate a deduction or a substitution should have kept is dropped; two are `__builtin_invoke` |
| LowIR text mismatch | 16 | the program compiles; the emitted LowIR differs |
| a wrong answer survives to a false `static_assert` | 5 | the refusal a substitution should make is not made at all |
| a value place still outside the read subset | 2 | `a template argument is written outside the PA12 subset` |
| 13.3 ranks a set and finds no best | 1 | `compat::swap`, which is 14.5.7p1's equivalence below and not an ordering gap |
| the rest | 19 | one-off clauses, one test each |

The `has no best declaration` group is down to that one: checkpoint 15 landed
14.8.2.4p3's three contexts and the remaining pair is two spellings of one
dependent value read as two readings. The `invoke_result_impl<void,Args...>::type`
pair is *not* a deduction gap - both tests write `__builtin_invoke`, which no
layer implements at all.

Known gaps diagnosed but not landed:

- **13.5.2p1's arity of an operator function has no reader at any tier.**
  `template<class T> int operator-(tag, T, int = 0)` is a declaration `g++`
  refuses - a non-member `operator-` shall have one or two parameters - and the
  reference and this build accept. Checkpoint 15's ordering is what un-hid it:
  the *call* over such a pair used to be refused for having no best declaration,
  so a broad refusal stood where the precise one is missing. The reference
  implements the clause at no arity, so no fixture can pin it, and 13.5 has a
  different answer for every operator it names.
- **14.8.2.4p3's first bullet at a call written through an object is `g++`'s
  answer against the reference's.** `s.f(x)` over two static member templates,
  over a static and a non-static one of the same class, and a namespace-scope
  pair reached from a member body are three programs `g++` accepts and the
  reference refuses - it orders such a pair over p3's first bullet at no shape at
  all - so no course fixture can hold them.
- **`__builtin_invoke` is not implemented**, which is the whole of both
  `invoke_result_impl` tests and nothing to do with 14.8.2.
- **14.8.2p8 at a default template argument the reference does not fire on.**
  `500-tcc-member-constructible-pack-sfinae.t` writes `_TCC<...>::template
  __is_implicitly_constructible<_Args...>()` over a primary that returns
  `false`, so `enable_if<false, int>::type` fails and the candidate drops -
  which is what `g++` does and what we do, running to 4. The checked-in `.ref`
  runs to 11, so the fixture pins a reference behavior both other oracles
  disagree with.
- **A `static const` member whose initializer calls a member template that is
  not `constexpr`** is refused by `g++` and translated by the reference and by
  us. The pattern reading used to refuse it for the wrong reason - it threw on
  every such fold - and checkpoint 13 removed that refusal, so the missing one
  at the *instantiation* is now visible. `template<> int f<int>()` written
  without `constexpr` beside a `constexpr` primary is the same shape one tier
  up, and that one *is* refused: a call written where 5.19 reads one asks the
  declaration chain, and a static data member's initializer does not. No fixture
  pins the remaining half either way.
- **A global with an initializer is written per element by the reference and as
  one `zero n` run by us.** The collapse is PA13's and 152 earlier `.ref` lines
  hold `zero 1`.
- **14.6p2 at a reading a dependent context defers.** Widening the clause to
  every dependent prefix costs
  `pa20/course/pa20/100-a-decltype-an-argument-list-wrote-is-a-tree.t`. The
  reference implements the clause at no shape at all - it translates
  `T::plain x;` and `T::template box<int> x;` alike - so neither direction of it
  can be pinned by a course fixture, and `g++` is the only oracle for it.
- **A template-id component written after a decltype prefix.**
  `decltype(A::make())::template box<int>` is `no declaration of box<int> is in
  scope` here and a type in both oracles, with no template in the program at
  all; `T::type::template box<int>` through a typedef comes out right, so the
  gap is the decltype-qualified name reader and not 14.2p4's list.
- **A pack pattern in a partial specialization's own argument list.**
  `D<void_t<typename T::m...>, T...>` leaves the primary unsupported.
- **A naming that discarded an argument is rebuilt structurally by
  `TypeTable::substitute`**, which is the per-element path of an expansion
  whose pattern reaches a *settled* run: the type its type-id named is
  substituted rather than read again, so a reading the type-id left standing
  inside it is not remade. `SemaAnalyzer::substituted` is the path every other
  substitution takes and does read it again. Both shapes reachable through it -
  a detector under a nested class template - come out right, because what the
  elements hold is settled by the time it runs.
- **14.5.7p1's equivalence where the argument a naming threw away is a
  *reading*.** `300-equivalent-alias-return-template-redeclaration.t` is two
  spellings of one dependent value read as two readings here.
- **8.3.1p4 and 8.3.3p3 at a deduction, where the oracles disagree with each
  other.** `probe(T *)` over `T = int &` drops the candidate in `g++` and not in
  the reference or here; a member pointer to a reference or to `void` drops it
  here and in `g++` and not in the reference; `T C::*` over a non-class `C` is a
  hard refusal here where both oracles drop the candidate.
- **Two partial specializations reached through a non-deduced array bound are
  not ordered against each other.**
- **14.8.2.3p4's second sentence at a reference the *reference binary* does not
  read.** `operator T()` reaching a required result of `const holder &` deduces
  `T = holder` here and in `g++` - the object files agree byte for byte on
  `_ZNK5makercvT_I6holderEEv` - and `T = const holder &` in the reference, which
  is `_ZNK5makercvT_IRK6holderEEv` and a direct binding with no temporary. No
  handout fixture pins either reading, and a course fixture cannot hold the
  shape at all because its `.ref` would come from the reference.
- **An `f32` item whose value came from an integer literal is written `0f`
  here.** The reference writes `f32 0` for a member the program initialized with
  `0` and `f32 0.0F` for one value-initialization made, which is two spellings of
  one value; `spell_floating` normalizes the suffix to the item's width for both.
  It is the only diff left in
  `spec/300-conversion-function-template-object-result-copy-init.t`.
- **`pa24/tests/spec/100-function-template-nontype-function-pointer-specialization-call.t`
  segfaults**, on this build and on every earlier one swept. It is a later PA's
  input and no pa23 or earlier file crashes.
- **8.3.4p1's bound over a place is read in a class template's member and in an
  alias and not in a function template's parameter.**
- **8.3.5p8's function returning an array or a function is not refused**, so
  `T()` over `T = int[3]` derives a type `g++` refuses. The reference accepts
  it and no fixture writes it, so the refusal is not worth its risk yet.
- **14.1p4's fourth bullet has no layer below it.** A pointer-to-member place
  is refused because `int S::*p = &S::m;` is.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain: n declarations of one name is 0.80 s at
  n = 3200. The walk is PA22's.
- **8.3.6p9's default *function* argument is read at the call and not where it
  was declared.** `int g(int n = late());` above `int late();` is refused by
  both oracles and translated here with no template in the program at all, so
  the clause belongs to the declarator layer and not to 14.7.1p1's readings.
- **A static data member's *dependent* initializer is read past the class
  body**, so `static const int v = late(T())` reaches a `late` declared below
  the template. `g++` refuses it at the point of instantiation and the
  reference binary accepts it; a non-dependent initializer is read in the class
  body and is bounded.
- **`void *p; p[0];` is refused here and by `g++` and accepted by the
  reference**, which implements 5.2.1p1's completely-defined pointee for an
  incomplete class and not for `void` though it refuses `void *p; p + 1;`. A
  subscript detector cannot be pinned by a fixture at all: the reference
  refuses `decltype(((T *)0)[0])` outright rather than dropping the candidate.

## Active Checkpoint

**Audit: the number a second binding stands at, and the count a call hands the
ordering.** Complete; ledger row 16 is its record.

- *Owner.* `sema_scope.cpp` owns 14.6.4.2p1's number, which is a fact of where a
  *name* came into scope in a region and so of which slot of the binding a
  declaration overwrites. `sema_template_signature.cpp` owns 14.8.2.4p3, which is
  now two questions: which types the pair is ordered over (`ordering_parameters`)
  and how many places of them the context asks (`ordering_places` under
  `ordering_limit`). `sema_overload.cpp` owns the one fact only the call has -
  how many of the conversions it ranked are 13.3.1p3's implicit object argument.
- *Data flow.* Checkpoint 15 gave a second binding the number the first one
  stood at, which is right for `extern int anchor;` above a pattern and `int
  anchor = 4;` below it and wrong for every second binding of a spelling to
  another *kind* of entity: `struct probe;` above a pattern and `int probe(int);`
  below it are 3.3p4's two declarations, and the pattern that finds the class may
  not name the function. The number is taken from the slot this declaration
  overwrites now - a tag's from the tag, everything else's from the ordinary
  name - and only where that slot holds a declaration of the same kind, so `find`
  goes on filtering the function out and answering with the tag beneath it. And
  the limit it handed the ordering was the count 13.3.1p3 ranked, which holds a
  place for the object argument whether or not a candidate wrote a parameter for
  it: `s.f(&v)` over two static member templates was ordered over two places
  where the call wrote one and left unordered by a trailing default, while
  `S::f(&v)` over the same pair came out right. `ordering_limit` restates that
  count in the places the two lists hold - the object place kept where either
  declaration wrote one, dropped where 9.4p1's static member of the same class is
  the other, and never added for 13.3.1.2p4's first operand, which a non-member
  operator wrote as a parameter of its own.
- *Expected complexity.* One `kind` comparison per namespace binding, reading a
  slot `bind` already holds; and two pointer comparisons and three field reads
  per ordering, on a path that already walks both parameter lists and memoises
  its answer under the limit it resolved. Neither moves work into a loop.
- *Validation.* 135 generated ordering shapes across five candidate kinds crossed
  with three first-parameter shapes, five trailing-default shapes and one or two
  written arguments, plus 40 probes, against `g++`, the reference binary, the
  checkpoint binary and the pre-checkpoint binary - agreeing with `g++` on all
  135 and running the 124 accepted ones through `lowir2cy86` + `cy86` to its
  value, 111 of them programs the pre-checkpoint binary refuses. Every course
  `.ref` regenerated from the reference binary and unchanged, plus one new
  fixture the reference refuses as this build does. `through-pa22` at
  2948 / 2948; corpus 2.08 s against the pre-audit binary's 2.12 s; no `rc > 1`
  in pa23's 430 files or in the 2284 of pa10 through pa22; valgrind clean over
  298 inputs.

## Next Substantial Checkpoint

**The five false `static_assert`s and the eight `no declaration ... accepts the
arguments of a call`**, which are 14.7.1p1's timing rather than 13.3's ranking:
a body read where 14.7.1p1 asks for none - `300-decltype-conditional-no-body-instantiation.t`
instantiates `declval`'s body for an operand 5.16 only reads the type of - and a
candidate a deduction dropped where the standard keeps it. The two groups meet
at `300-member-overload-set-survives-class-completion.t`, whose detector reads a
trailing return type over an overload set the class had not finished declaring.

## Performance Model

Measured on this turn's binary against a `/tmp` worktree of `adf23e13` built the
same way, warm cache, `/usr/bin/time` on the binary itself. A loop that spawns
`timeout` per run reads the same corpus as 45.9 s against 2.6 s, which is the
wrapper's process floor and not the compiler's; a corpus pass run while a second
build saturates the machine reads 5.8 s against 1.9 s, which is that build's.
Carried-forward absolute times are the load of the turn that took them: the
corpus reads 2.16 s on one turn and 2.00 s on the next from the same binary, so
only the two figures of a pair say anything.

| sweep | shape | result |
| --- | --- | --- |
| address-argument multiplicity | n objects, one specialization per `&obj` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.12 @1024 - linear |
| function-argument multiplicity | n functions, one specialization per `&f` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| SFINAE multiplicity | n classes x 2 candidates, one failing substitution | 0.01 s @32, 0.03 @128, 0.15 @512, 0.30 @1024 - linear |
| detector multiplicity | n classes x 3 partial specializations | 0.01 s @32, 0.03 @128, 0.14 @512, 0.30 @1024 - linear |
| converting-constructor multiplicity | n calls through 3 constructor templates | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| variable-template multiplicity | n classes x 2 calls gated by `pointed<T>` | 0.00 s @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear |
| defaulted-argument multiplicity | n function templates, each naming `S<A>` whose second place takes 14.1p9's default over the deduced one | 0.01 s @32, 0.04 @128, 0.19 @512, 0.39 @1024 - linear |
| instantiation multiplicity | n specializations of one template, each body read under its own bound | 0.00 s @32, 0.02 @128, 0.10 @512, 0.23 @1024 - and 0.00 / 0.02 / 0.11 / 0.22 on the pre-audit binary |
| template multiplicity | n function templates, one body apiece | 0.00 @32, 0.01 @128, 0.04 @512, 0.10 @1024 - against 0.00 / 0.01 / 0.05 / 0.09 |
| later-declaration multiplicity | one detector, n later declarations of the name it reads | 0.00 @32, 0.00 @128, 0.02 @512, 0.05 @1024 - against 0.00 / 0.01 / 0.02 / 0.05 |
| fold multiplicity | n folded conversions beside one bounded candidate set | 0.00 @32, 0.01 @128, 0.07 @512, 0.14 @1024 - against 0.00 / 0.01 / 0.07 / 0.15 |
| template-place multiplicity | n specializations of one template with a template place, each asking 14.3.3p1 | 0.00 @32, 0.01 @128, 0.03 @512, 0.07 @1024 - and the same on the pre-checkpoint binary |
| narrowed-clause multiplicity | n braced constructions whose clause is a non-constant sum | 0.00 @32, 0.01 @128, 0.03 @512, 0.07 @1024 - against 0.00 / 0.01 / 0.03 / 0.07; ungated, the fold made it 0.09 @1024 |
| derived-array multiplicity | n class templates, each a member array the door derives | 0.00 @32, 0.01 @128, 0.06 @512, 0.13 @1024 - against 0.00 / 0.01 / 0.06 / 0.12 |
| substitution nesting | d nested trait layers under one `enable_if` | 0.00 s flat from d = 8 to d = 48 |
| detector nesting | d nested `W<...>` wrappers under one detector | 0.00 s flat from d = 4 to d = 32 |
| defaulted-argument nesting | d class templates, each defaulting its second place over the one below | 0.00 s flat from d = 4 to d = 32 |
| body nesting | d class templates, each member body calling the one below | 0.00 s flat from d = 4 to d = 32, on both binaries |
| narrowed-clause nesting | d nested braced constructions, each clause arithmetic over the one below | 0.00 s flat from d = 4 to d = 32, on both binaries |
| declarations of one name | n function templates, one parameter list | 0.04 @400, 0.24 @1600, **0.80 s @3200 - quadratic** |
| narrowed-clause gate multiplicity | n braced constructions whose clause is a constant reaching a *wider* unsigned destination - the pair the corrected gate folds for | 0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same on the pre-audit binary, and the same again with a 64-deep `constexpr` recursion for a clause |
| qualification multiplicity | n arguments through 4.4's walk, which every candidate asks | 0.00 @32, 0.00 @128, 0.01 @512, 0.03 @1024 - and the same |
| array-qualification multiplicity | n arguments adding `const` at an array's element | 0.00 @32, 0.00 @128, 0.01 @512, 0.03 @1024 - the pre-audit binary refuses the program |
| subscript multiplicity | n subscripts, one `is_incomplete` apiece | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| array-dimension nesting | d dimensions under one qualification conversion | 0.00 s flat from d = 4 to d = 32 |
| discarded-argument multiplicity | n classes x 2 detectors over `discard<typename T::pointer>`, one `mentions` walk and one re-read apiece | 0.01 @32, 0.02 @128, 0.12 @512, 0.25 @1024 - linear.  The pre-checkpoint binary refuses the program at the first assert, so its 0.04 s is no measurement |
| discarded-run multiplicity | n calls through `first_of<true_tag, enable_if_t<Bn::ok>...>`, one expansion built per call for its refusals alone | 0.00 @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear; the pre-checkpoint binary refuses it |
| pack-reading multiplicity | n function templates, each a value argument whose places `note_places` records | 0.00 @32, 0.02 @128, 0.10 @512, 0.20 @1024 - linear; the pre-checkpoint binary refuses it |
| discarded-alias nesting | d nested discarding aliases, each naming the one below beside the member being detected | 0.00 s flat from d = 4 to d = 48 |
| collapsing-alias multiplicity | n function templates, each declared through `discard<T>` and defined through `void` - the pair 14.5.7p1's scope restores | 0.00 @32, 0.01 @128, 0.03 @512, 0.06 @1024 - and the same on the pre-checkpoint binary, which accepts this program |
| member template-id multiplicity | n typedefs of one `Ops<P>::template box<U>` in one pattern - the interned stand-in read once per written component | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same on the pre-checkpoint binary |
| member template-id distinctness | n typedefs of `Ops<P>::template box<An>`, a different argument apiece, so no two share an entry | 0.00 @32, 0.01 @128, 0.06 @512, 0.12 @1024 - against 0.00 / 0.01 / 0.05 / 0.12 |
| member value-argument multiplicity | n `Ops<P>::template at<N>::value` operands, each argument read as a type first and then as an expression | 0.00 @32, 0.00 @128, 0.00 @512, 0.01 @1024 - and the same |
| member value-argument nesting | d nested `box<...>::value` under one such argument, which is where a type-then-value retry could double | 0.00 s flat from d = 4 to d = 32 |
| unsettled-specialization multiplicity | n function templates, each a default argument folding `ok<U>()` over its own place | 0.00 @32, 0.01 @128, 0.07 @512, 0.14 @1024 - linear; the pre-checkpoint binary refuses the program |
| member-naming cross product | n call sites x one member template-id substituted to a distinct argument apiece | 0.00 @32, 0.00 @128, 0.02 @512, 0.05 @1024 - and the same on the pre-audit binary |
| plain-member multiplicity | n `typename T::type` typedefs in one pattern - the path `stood_in_for` was added to | 0.00 @32, 0.00 @128, 0.01 @512, 0.01 @1024, 0.03 @2048 - and the same |
| constexpr-member multiplicity | n static data members of one pattern, each folding a member template `constexpr` stands on | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| member-naming nesting | d nested `T::template at<...>::value`, every level dependent | 0.00 s flat from d = 2 to d = 20, on both binaries |
| member type-argument nesting | d nested `A::box<...>::type` written as one member's argument | 0.00 s flat from d = 4 to d = 32, on both binaries |
| ordering multiplicity | n calls of one pair the limit is what orders - defaults on one side, deduced parameters on the other | 0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - linear, and the same on the pre-audit binary |
| ordering distinctness | n distinct pairs, one call each, so no two calls share a memo entry | 0.00 @32, 0.02 @128, 0.08 @512, 0.17 @1024 - against 0.00 / 0.02 / 0.08 / 0.18 |
| overload-set width | one call over n candidate templates that all tie on conversions, which is where the pairwise ordering walk lives | 0.01 @128, 0.02 @256, 0.04 @512 - and the same |
| conversion-ordering multiplicity | n classes x two conversion templates, 14.8.2.4p3's second bullet run once per initialization | 0.01 @32, 0.03 @128, 0.13 @512, 0.27 @1024 - and the same |
| ordering nesting | d nested `W<...>` under the one place two templates are ordered at | 0.00 s flat from d = 4 to d = 32, on both binaries |
| conversion-ordering nesting | d pointer levels in the two conversion-type-ids being ordered | 0.00 s flat from d = 4 to d = 32, on both binaries |
| **member-call ordering multiplicity** | n calls through an object over two non-static member templates - the path the object-argument count was added to | **0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same on the pre-audit binary** |
| **static-member ordering multiplicity** | n calls through an object over two static member templates - the shape whose limit this audit corrected | **0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - linear; the pre-audit binary refuses the program at the first call** |
| **ordering-limit distinctness** | one pair asked under five call arities, n times over - the run the memo keeps per pair | **0.00 @8, 0.00 @16, 0.01 @32, 0.01 @64 - and the same** |
| **binding multiplicity** | n namespace-scope declarations of distinct names | **0.00 @32, 0.00 @128, 0.01 @512, 0.01 @1024 - and the same** |
| **rebinding multiplicity** | n names each declared twice, which is the slot the kind test reads | **0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same** |
| **tag-then-function multiplicity** | n spellings bound as a tag and then as a function - the pair the kind test tells apart | **0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same** |
| whole PA23 corpus | 400 handout and 30 course files, one process each | **2.08 s warm over three alternating passes, against the pre-audit binary's 2.12 s**; no `rc > 1` here or in the 2284 files of pa10 through pa22; valgrind clean over 298 inputs |

Why 14.6.4.2p1's bound stays flat: it is one `std::uint32_t` written where a
definition is recorded and put back by a two-assignment scope where it is read,
so a body costs one of each however many times it is read; `find` asks one
comparison per namespace lookup and only where a bound is set at all, and the
candidate walk asks one per entry on the 3.4.1 side of a set whose size it
already has. 14.1p9's stand-in is one entity per (spelling, region), memoized by
`default_arguments_`, so a template named n times the same way reads its default
once. 3.9.1p8's floating fact is settled by the walk that already reads every
base and member. The one quadratic is PA22's `TemplateSignature::equivalent`
chain walk, named under the known gaps.

Why the refusals and the conversion walks stay flat: they are a fixed number of
type comparisons on a path that already interns a node or already walks a
pointee, so they cost per *use* and never per program. 14.3.3p1's is one
comparison per template place, and only where a specialization is being made -
a second naming of the same argument list is the held entry the caller already
found. 8.5.4p7's is the one that could have scaled, because its exception needs
the clause folded: the fold is asked only where the bullet could fire at all,
so an integral clause reaching an integral destination that holds every value
of its type - which is nearly every clause a program writes - pays nothing.
`object_cv` is one `kind` test more than `cv`, and for an array it walks the
dimensions over the scratch `qualified` already uses.

Why the readings a substitution makes again stay flat: each is memoised where
the thing it reads is interned, so a pattern that writes one spelling n times
reads it once and the (n - 1) later namings are a map lookup. `mentions` is the
graph `substituted` walks, asked once with the nodes already reached kept, and
only of a naming that has a dependent argument at all; `alias_arguments` is
memoised by `specialization_of` and the entry a discarding naming stands as is
interned by the same three facts; `note_places` runs once per (spelling,
region); a member written as a template-id costs one argument-list reading per
*written component* and leaves a stand-in interned by (prefix, name, list), so
the substitution runs once for it however many namings arrive. `rebuilt` is a
fixed number of field reads per argument and runs *before* the `mentions` walk
it gates, so a naming whose arguments are places pays less than it did. The one
thing that is per *use* is the expansion an argument list discards, which is
read once per element because that is what 14.5.3p4 asks for.

Why the two retries do not double: `member_component`'s type-then-value reading
is made only where the first ran out, and a spelling that is no type-id runs out
on its first word rather than inside its arguments - which the two argument
nesting sweeps are what say, one with the failure at the innermost level and one
with it at the outermost. `Substitution::unsettled` is one walk of an argument
list already in hand and returns on the first dependent entry.

Why this audit's two readers cost nothing: `stood_in_for` is one `TemplateId`
construction and one string comparison, asked last of a chain whose four earlier
tests are what say the name is a qualified one behind a place - so a name
written behind any settled prefix never reaches it, and asking it any earlier
reads a record the type has not got. `constexpr_declared` is three field reads
per fold that reaches a callee with no body, against one assignment per
specialization that no longer happens.

Why 14.8.2.4p3's limit costs nothing: it is two `min`s, one `is_pack_expansion`
and `ordering_limit`'s three field reads on a path that already walks both
parameter lists, and it carries no new question to the call - the count it takes
is the one 13.3 ranked the conversions over, restated in the places the two lists
hold. The memo is keyed by the pair *and* the resolved limit and holds a short
run rather than a nested map, because a pair is asked under the arities its calls
wrote and, at most, under the whole type - which is what the limit-distinctness
sweep says, one pair under five arities costing what one arity does.
`kResultPlace` builds one one-element vector for a pair of conversion templates,
which is the only shape that reaches it at all. The ordering itself is what it
was: a `Deduction::match` per place, memoised per (pair, limit), so an overload
set of n candidates asks 2n of them however many times the ranking compares -
which is what the width sweep says, 0.04 s at 512 candidates on this binary and
on the one before it.

Why 14.6.4.2p1's number stays flat under the kind test: `bind` already holds the
binding it is about to overwrite, so the test is one `kind` comparison and one
pointer read per namespace binding and reads nothing the function did not have -
n declarations of distinct names, n declared twice, and n bound first as a tag
and then as a function all cost what they did before it.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.{h,cpp}`, `sema_specialize.cpp`, `sema_analyzer.{h,cpp}`, `sema_function.cpp`, `sema_declarator.cpp`, `sema_scope.{h,cpp}`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal from an instantiated class body; 14.1p3's unnamed *type* place; a dependent value argument keeps its spelling, place and region; `substituted_region`; `dependent_member_type` rebuilt over the class *this* substitution made; 14.5.6.1p5 rather than 13.1's index pairs two declarations; 14.8.2.1p4's qualification conversion; 14.6p2's `typename` over a bare place | 246 → 292 / 400; through-pa22 2948 / 2948 |
| 2 | 14.3.2p1: an address argument is which object it designates | `sema_template_head.{h,cpp}`, `sema_value_expression.cpp`, `sema_expression.cpp`, `sema_constexpr.cpp`, `ast_parser_class.cpp` | 14.1p4's second and third bullets and 14.1p8's adjustment; 14.3.2p5 over 5.19p2's `ConstantAddress`; 14.3.2p1's linkage and 14.3.2p3's subobject refusals; `&` and `nullptr` in the spelling reader; 4.3p1's function name; 14.1p3's *abstract* declarator; 3.2p3's demand for the definition an argument names | 292 → 299 / 400, plus 5 course fixtures; 30-shape sweep agrees with the reference on all |
| 3 | 14.8.2p8 at 14.5.5.1p1's match, and 10p1's `class-or-decltype` | `sema_specialize.{h,cpp}`, `ast_parser_class.cpp`, `sema_derivation.cpp` | `Specialization::matches` runs `match_arguments` and 14.8.2.5p5's read-back inside one `Substitution` - the whole detector idiom; 14.5.5p8.3's unbound place moved out of that scope; 10p1's base written as a decltype-specifier has its operand read and kept | 304 → 314 / 405; 11-shape base sweep agrees with `g++` |
| 4 | 13.3.1.4p1's converting constructor is a template too | `sema_overload.cpp` | a constructor template is a candidate through the specialization the one argument deduces, with 12.3.1p2's `explicit` and 8.4.3p1's deleted asked of the template; 13.3.3p1's last tie-break, so the non-specialization wins a tie | 314 → 319 / 405; 11-shape sweep agrees with `g++` |
| 5 | 14.6.2p2's variable template, and a prefix the arguments settled | `sema_specialize.cpp`, `sema_template.cpp`, `sema_type_id.cpp`, `sema_deduce.cpp` | a naming of a variable template under an outer head gives back a declaration typed by a stand-in, held against the interned list; a settled non-class prefix refuses where 14.8.2.5p5's read-back took it for agreement; 8.3.4p1's bound written over a place is 14.8.2.5p5's non-deduced context | 319 → 324 / 405, plus 4 course fixtures; 15-shape and 6-shape sweeps agree with both oracles |
| 6 | audit: 14.3.2p1's address argument has a fourth reader | `type_model.{h,cpp}`, `lowir_abi.cpp`, `abi_mangle.cpp`, `sema_template_head.{h,cpp}`, `sema_value_expression.cpp` | the encoding names the declaration an address argument designates rather than this unit's table entry, so two units writing `at<&left>` write one definition; `reaches_place` is 14.3.2p5's own conversion list, asked before 8.5's; 14.1p4's fifth place; 13.4p1's target at a function place | 328 / 409 → 332 / 413; 11-shape mangling cross-product identical to the reference |
| 7 | the second reading a substitution makes: the tree, the region and the bound | `sema_declaration.h`, `sema_template.{h,cpp}`, `sema_template_head.cpp`, `sema_deduce.cpp`, `sema_scope.{h,cpp}`, `sema_overload.cpp`, `sema_operator.{h,cpp}`, `sema_argument_lookup.cpp`, `sema_analyzer.{h,cpp}`, `sema_layout.cpp`, `type_model.{h,cpp}` | 14.1p9 at a value place over a list an argument has yet to settle was *evaluated* against a dependent prefix and refused the program - `dependent_default` keeps the tree and the region and 14.7.1p1 evaluates it where the arguments arrive, which is the third form of `DependentDecltype`. 14.6.4.2p1: every reading a pattern left standing was made again against the *whole* unit, so an object declared later under the name of a hidden friend suppressed 3.4.2 and a later overload joined a set - `declared_serial` and `ReadingBound` put 3.4.1's half of such a reading back at the definition context, while 3.4.2's searches and every instantiated body run under none. The sibling sweep found the same gap at both tiers of 14.1p9's own default, which is read at the naming and not through a stand-in, so both carry the bound too. 5.2.2p4: a class of two words or less holding a floating scalar crosses by address in the reference and as its bytes here, which is 3.9.1p8 over the storage the layout walks and had no reader at all. `DependentReadings` and 14.5.6.2's ordering moved to their owners to make the room | 332 / 413 → 338 / 413, plus 3 course fixtures (341 / 416); through-pa22 2948 / 2948; 45 + 30 + 32 sweep shapes against `g++` and the reference; corpus 1.94 s against the baseline's 1.95 s |
| 8 | audit: what a body read again may name | `sema_template.{h,cpp}`, `sema_declaration.h`, `sema_scope.h`, `sema_function.cpp`, `sema_analyzer.cpp`, `sema_pattern.cpp`, `sema_specialize.cpp`, `sema_explicit.cpp`, `sema_constexpr.{h,cpp}`, `sema_constexpr_object.cpp`, `sema_expression.cpp` | 14.6.4.2p1's bound had landed for the three readings a pattern interns and was written as *no bound at all* at `instantiate_body` and `complete_specialization` - so a template body reached the whole unit, `pick(long)` above a pattern and `pick(int)` below it ran to `pick(int)` where `g++` runs to `pick(long)`, and `f(T t) { return late(t); }` above `int late(int);` was a program both oracles refuse and this build translated. The bound is a fact of each definition now: `TemplateInfo::visible`, `Partial::visible`, `Member::visible`, `WrittenBody` for the bodies `template<>` wrote out, and `PendingDefinition::visible` for the body a reading put aside - each reading setting its own at entry, taken after the definition's first reading so 11.3p1's friend and the template's own name stay reachable, and recorded through `written_bound` so a nested definition does not inherit the unit. `ConstexprReading::selected` draws 3.4.1's line against 3.4.2's, which is the gap the plan had recorded as reaching no fixture and which ran a fold to 200 against both oracles' 100. 1.4p8's reserved name stands before the unit rather than at the use that declared it | 341 / 416 -> 343 / 417; through-pa22 2948 / 2948; 23 + 28 + 10 sweep shapes against `g++` and the reference; corpus 2.06 s against the pre-audit binary's 2.03 s |
| 9 | the refusals SFINAE has nothing to fire on | `type_model.{h,cpp}`, `sema_declarator.cpp`, `sema_type_id.cpp`, `sema_template.cpp`, `sema_template_head.{h,cpp}`, `sema_virtual.cpp`, `sema_expression.cpp`, `sema_lifetime.cpp`, `sema_init_list.cpp`, `sema_overload.cpp`, `sema_analyzer.h` | four things the standard refuses and the layers below accepted, so 14.8.2p8 had no failure to drop a candidate on. 8.3.2p5's reference to void and 8.3.4p1's array of a reference, of void, of a function and of an abstract class now go through one *door* - `derived_reference` / `derived_array` / `derived_substituted_array` - that all three readers that derive a type call, while the interning entries stay open for 13.1's key and 14.8.2.1p3's stand-in, which is what the first landing of this rule broke in 24 tests. 10.4p2 became a fact of the type, settled where the vtable pass already answers it, so a substitution reaches it without a scope. 5.7p1 and 5.2.6p1 read the pointee as *completely defined*, which is `void *` and a class the unit only declared. 8.5.4p7 measures the clause a list-initialization hands a constructor, and its fourth bullet's round trip now asks whether the destination *represents* the value rather than whether the bits came back - `-1` reached `unsigned` and came back as `-1`. 14.3.3p1 is asked at `instantiate_class`, the one place every settled argument list meets its head, because `helper<T::template member>` is an argument no spelling looked up and no deduction bound. 4.10p2's `void *` reads 3.9.3p5's qualification, which an array carries on its element | 343 / 417 -> 354 / 420 (handout 331 -> 339 / 405); through-pa22 2948 / 2948; 90 + 25 + 48 + 14 sweep shapes against `g++` and the reference; corpus 1.91 s against the pre-checkpoint binary's 1.90 s; valgrind clean over 65 inputs |
| 10 | audit: the qualifiers an array carries, and the pointee a subscript moves over | `sema_expression.cpp`, `sema_init_list.cpp`, `sema_overload.cpp`, `sema_deduce.cpp`, `type_model.{h,cpp}` | each of checkpoint 9's refusals had one exit more than it was written at, and 3.9.3p5's fact had one reader of four. 5.2.1p1's completely-defined pointee is 5.7p1's, and `subscript_expression` restated the clause above code that never asked it - `Inc *p; p[0];` is a program both oracles refuse and this build translated, and the detector over it read back the wrong answer. 8.5.4p7's fourth bullet was width and equal-width signedness, which is not "cannot represent all the values": a signed source reaching a wider unsigned has negative values at every width and `bool` holds two of them however wide its storage, and the same statement gated the fold so the constant exception was never reached for either. 3.9.3p5 landed at `pointer_convertible`'s 4.10p2 arm alone, so 4.4's own walk, 5.9p2's composite pointer type and 14.8.2.1p2's conversion each read an array node's `cv`, which is zero - and that walk asked 4.4p4's second condition of the level it had just compared, which refused `volatile int *q = p;` with no array in it at all. `object_unqualified` is the fact's other half | 354 / 420 -> 356 / 421 (handout 339 -> 340 / 405); through-pa22 2948 / 2948; 225 + 675 narrowing shapes and 20 qualification shapes through eight sites, agreeing with `g++` on all 920 and running the accepted ones to its value; every course `.ref` regenerated from the reference binary and unchanged; corpus 1.87 s against the pre-audit binary's 1.88 s; valgrind clean over 135 inputs |
| 11 | the arguments an alias template's type-id threw away | `type_model.{h,cpp}`, `sema_specialize.{h,cpp}`, `sema_template.cpp`, `sema_deduce.cpp`, `sema_pack.{h,cpp}`, `sema_template_head.cpp` | 7.1.3p2 makes a template-id over an alias *be* the type its type-id named, so a naming holds no argument once it has been read - which is right until an argument is one no list has settled, because then it is built where 14.7.1p1 arrives and building it is what 14.8.2p8 drops a candidate for. `discard<typename T::pointer>` names `void` however it is written, and collapsing it where it stood left every `T` agreeing with the detector's partial specialization, so `detected_or_t` answered the same for a class with the member and one without. A naming whose type-id does not mention a dependent argument keeps an entry holding the alias, that type and the list, interned by the three so 14.5.7p2 leaves two namings of one list one type and a substitution can rebuild one with no declaration to name it after; `substituted` builds the arguments first and reads the type-id again, `match` unwraps both sides because 14.5.5.2p1's ordering writes one pattern as the other's argument, and `mentions` is `substituted`'s own graph read backwards. 14.3.3p1's place takes an alias as readily as a class template and `Op<Args...>` over one was instantiated as a class. Two readings the packs could not be found through: 14.5.3p5 asked of a reading read *again* rather than rebuilt - a decltype-specifier, a value argument, a head's own default are interned by their text, so `enable_if_t<bool(Bn::value)>...` named no pack at all - which `note_places` records on the entry, a name standing for a reading of its own handing on that reading's; and 14.5.3p4 where the pattern is not a place, `wrap<Args&&...>` having bound the place's name to `Args&&`, which is no pack | 356 / 421 -> 363 / 422 (handout 340 -> 346 / 405); through-pa22 2948 / 2948; 25 + 11 sweep shapes agreeing with `g++` on all 36; every course `.ref` regenerated from the reference binary and unchanged; corpus 1.93 s against the pre-checkpoint binary's 1.94 s; no `rc > 1`; valgrind clean over 102 inputs |
| 12 | audit: the type a naming is, and the arguments worth keeping | `sema_specialize.{h,cpp}` | 14.5.7p1 is the other half of the sentence 7.1.3p2 opens: a template-id over an alias template *is* the associated type, so the entry checkpoint 11 kept beside that type was a second type for one the program can also write out longhand - and `f(discard<T> *)` declared beside `f(void *)` defined stopped being two declarations of one template, at `TemplateSignature::of`'s canonical form, at 13.1's index of a parameter-type-list and at the return-type comparison a found pair is checked by, which is a function template, its out-of-class definition, a member of a class template, a member template of a class that is not one and a naming under another argument list. `rebuilt` is where the two halves meet: an argument that is a *place* is one 14.7.1p1 looks up, so nothing can refuse and the naming collapses to what its type-id named; an argument it *builds* - a member of a prefix, a specialization over a template place, a naming that discarded one of its own, an expression left standing, or a type derived over any of them - is what the entry is kept for, which is every argument 14.8.2p8 has something to fire on | 363 / 422 -> 364 / 423 (handout 346 / 405 unmoved); through-pa22 2948 / 2948; 40 probe programs against `g++`, the reference and the pre-checkpoint binary, each accepted one outside the recorded divergences running to `g++`'s value; every course `.ref` regenerated from the reference binary and unchanged; corpus 1.96 s against the pre-checkpoint binary's 1.92 s; no `rc > 1`; valgrind clean over 145 inputs |
| 13 | a dependent member written as a template-id, and the specialization no list has settled | `type_model.{h,cpp}`, `sema_specialize.{h,cpp}`, `sema_deduce.{h,cpp}`, `sema_declarator.cpp`, `sema_template.cpp`, `sema_constexpr.cpp`, `sema_pack.cpp`, `lowir_abi.cpp`, `sema_analyzer.h` | 14.2p4 makes the list a member wrote part of what its name stands for, and `dependent_member_name` had kept only the prefix and the raw spelling - so `typename Ops<P>::template difference_type<In>` was looked up as a plain type name in the class the arguments made and every candidate that asked for it was dropped.  The list is read where the reading stands, each entry a type-id where the spelling is one and 5.19's expression where it is not because 14.1p4 has no head to ask, and interned beside the prefix and the name; `Substitution::member` builds it again where the arguments arrive and 14.3.3p1's two exits finish it - a member class template through `instantiate_class`, a member alias template through `alias_arguments` - while a class with no such member template is 14.8.2p8's failure.  The ABI writes the third form of `<unresolved-name>`, and `mentions`, `collect_packs` and the `dependent_` interning key each took the new edge.  The value half is the same sentence about a function template: `Substitution::unsettled` says a specialization whose template has no pattern recorded - which 14.6p8's reading of a class template's body is what leaves - or whose own list still names a place is one 14.7.1p1 instantiates nothing for, so `instantiate` leaves it and `ConstexprReading::call` stands a value in, where the old path read a body over types no argument gave and refused programs both oracles accept.  7.1.5p2 travels with the declaration, so `specialize` copies `constexpr_function` for the one reading that writes no definition to read it off | 364 / 423 -> 369 / 425 (handout 346 -> 349 / 405); through-pa22 2948 / 2948; 16 + 12 probe programs against `g++`, the reference and the pre-checkpoint binary, every accepted one linked and run to `g++`'s value with one recorded divergence; every course `.ref` regenerated from the reference binary and unchanged; corpus 2.16 s against the pre-checkpoint binary's 2.16 s; no `rc > 1`; valgrind clean over 123 inputs |
| 14 | audit: the list a member wrote is a second fact of its name | `sema_template.cpp`, `sema_constexpr.cpp`, `sema_declarator.cpp` | checkpoint 13's rules are right - 24 cross-product shapes of the idiom agree with `g++` on all, the object file's third form of `<unresolved-name>` is byte-identical to the reference and to `g++` for the plain, the pack and the value form, and two units naming one such signature write one weak definition under one name.  What none of it carried is that splitting `dependent_member` into a name and a list left the two readers that ask one question of it wrong.  14.6p2's compared `dependent_member` to the whole written component - `box` against `box<int>` - so `T::template box<int> b;` with no `typename` stopped being refused while `T::plain` one line away still was; `stood_in_for` puts the name and the list back together, and is asked last of the chain because it reads a record only a type a dependent prefix made has.  And 7.1.5p2 was written as a field `specialize` copies onto every specialization, which is 14.7.3p1's too: `template<> int f<int>()` declared without `constexpr` beside a `constexpr` primary folded, and `char a[f<int>()]` was a program both oracles refuse and the checkpoint translated.  The copy is gone and `constexpr_declared` asks the declaration chain at the one reading that writes no definition to read the flag off, which is also what says whether the refusal is 5.19's about the program or the edge of this build | 369 / 425 -> 370 / 426 (handout 344 / 400 unmoved, the failing 56 the same file for file); through-pa22 2948 / 2948; 24 generated cross-product shapes and 34 probes against `g++`, the reference, the checkpoint binary and the pre-checkpoint binary, every accepted one linked and run to `g++`'s value; every course `.ref` regenerated from the reference binary and unchanged; corpus 1.99 s against the pre-audit binary's 2.01 s; no `rc > 1` in pa23's 400 files or the 2083 of pa10 through pa22; valgrind clean over 139 inputs |
| 15 | the types a deduction between two declarations is asked over | `sema_template_signature.cpp`, `sema_analyzer.h`, `sema_template.h`, `sema_overload.cpp`, `sema_deduce.cpp`, `sema_scope.cpp` | 14.8.2.4p3 has three answers to which types a partial ordering is done over and this build had only implemented the third: a call was ordered over the *whole* parameter list of each declaration, so `build(G &, int, int, R &, bool = true, bool = false)` beside `build(G &, int, int, R &, V, E, bool = false)` was two lists of different lengths and the deduction failed both ways with the call ranked by nothing.  `limit` is now p3's own answer - `kEveryPlace`, the count the call wrote arguments for, or `kResultPlace` - and `ordering_parameters` and `ordering_places` are where it is read.  p3's footnote is why a default argument orders nothing, and 14.8.2.4p12 is why a *trailing run* is a place of its own past the limit: cut off, p8's first sentence has no A a pack was transformed from to fail on, and `dispatch(C &, S, T &&)` stops being more specialized than `dispatch(C &, S, T &&, R &&...)`.  p3's second bullet had no implementation at all - two conversion templates were ordered over the object parameter, which tells them apart by nothing - and `operator T**()` now beats `operator T*()` for an `int **` exactly as it does in `g++`.  14.8.2.3p2 and p4 are the same sentence one tier down: the reference and the top-level qualification come off P and A wherever the *other* one came from, so `operator U()` deduces `U` from `const A &`, 13.3.1.4's copy constructor and move constructor reach one conversion function rather than two, and 13.3.3.2p3 has a pair it can order.  The sibling probe found 14.6.4.2p1's number written on the wrong declaration: a namespace binding made over one the region already made was numbered where *it* stood, so `extern int anchor;` above a pattern and `int anchor = 4;` below it put a name the pattern could see out of its own reach - the number is the first binding's now, and 13.1's chain walk still reads each declaration's own | 370 / 426 -> 376 / 429 (handout 344 -> 347 / 400); through-pa22 2948 / 2948; 42 generated shapes against `g++`, the reference and the pre-checkpoint binary, agreeing with `g++` on all 42 and every accepted one linked and run to its value, with two the reference alone refuses; `_ZNK5makercvT_I6holderEEv` byte-identical to `g++`'s; every course `.ref` regenerated from the reference binary and unchanged, plus three new fixtures; corpus 2.06 s against the pre-checkpoint binary's 2.07 s; no `rc > 1` in pa23's 429 files or the 2572 of pa10 through pa22; valgrind clean over 151 inputs |
| 16 | audit: the number a second binding stands at, and the count a call hands the ordering | `sema_scope.cpp`, `sema_template_signature.cpp`, `sema_overload.cpp`, `sema_analyzer.h` | checkpoint 15's two new facts are each read one step wide of where they were settled.  14.6.4.2p1's number was given to *every* second binding of a spelling, which is right for the object `extern int anchor;` and `int anchor = 4;` declare and wrong for 3.3p4's two declarations of one name: `struct probe;` above a pattern and `int probe(int);` below it bind one spelling to two entities, and numbering the function where the class stood put it in the pattern's reach - a program both oracles refuse and the pre-checkpoint build refused, translated here to 8.  The number is taken from the slot the declaration overwrites now, a tag's from the tag and everything else's from the ordinary name, and only where that slot holds a declaration of the same kind, so `find` goes on filtering the function out and answering with the tag beneath it.  And 14.8.2.4p3's limit was the count 13.3.1p3 ranked the conversions over, which holds a place for the implicit object argument whether or not a candidate wrote a parameter for it: `s.f(&v)` over two static member templates was ordered over two places where the call wrote one, so a trailing default was asked and answered neither way and the pair was left unordered - while `S::f(&v)`, the same pair over the same argument, came out right.  `ordering_limit` restates the count in the places the two lists hold: the object place kept where either declaration wrote one, dropped where 9.4p1's static member of the same class is what the other is ordered against, and never added for 13.3.1.2p4's first operand, which a non-member operator wrote as a parameter of its own.  Recorded rather than fixed: 13.5.2p1's arity of an operator function, which has no reader at any tier and which the checkpoint's ordering un-hid by removing the broad refusal that stood over it | 376 / 429 -> 377 / 430 (handout 347 / 400 unmoved, the failing 53 the same file for file); through-pa22 2948 / 2948; 135 generated cross-product shapes and 40 probes against `g++`, the reference, the checkpoint binary and the pre-checkpoint binary, agreeing with `g++` on all 135 and running the 124 accepted ones to its value, 111 of them programs the pre-checkpoint binary refuses; every course `.ref` regenerated from the reference binary and unchanged, plus one new fixture the reference refuses as this build does; corpus 2.08 s against the pre-audit binary's 2.12 s; no `rc > 1` in pa23's 430 files or the 2284 of pa10 through pa22; valgrind clean over 298 inputs |
