# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `sema_template_head.*` — 14.1p2's places, named and unnamed alike.
- `sema_deduce.{h,cpp}` — 14.8.2. `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, and `Instantiated` is the error that escapes it.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and the dependent stand-ins a pattern leaves behind
  (`dependent_written_`) for both a decltype-specifier and a value argument.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5, which says
  when two template-declarations declare one template.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused.

## Current Failure Map

400 tests, 292 passing (turn-start baseline 288, checkpoint start 246). The 108
left, grouped by the compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a call no declaration accepts | 21 | the candidate a deduction or a substitution should have kept is dropped |
| a value place outside the read subset | 15 | pointer/reference/enum non-type arguments, `typename` written as one, "outside the PA12/PA20 subset" |
| a name a substitution should have reached | 13 | `no declaration of X is in scope` for a member the arguments do settle |
| a wrong overload survives to a false `static_assert` | 10 | the refusal a substitution should have made is not made at all |
| parse gap | 6 | `is not a translation unit` (`operator new` in a NTTP, c-style casts at a value place) |
| an initialization with no conversion | 4 | 13.3.1.4/13.3.1.5 over a specialization the deduction did make |
| a class the reading left incomplete | 2 | a base or a `sizeof` naming one 14.7.1p1 has not settled |
| LowIR text mismatch | 19 | the program compiles; the emitted LowIR differs |
| the rest | 18 | one-off clauses, one test each |

Known gaps diagnosed but not landed, with their evidence:

- **14.6p2 at a reading a dependent context defers.** The clause is asked where
  a type-specifier is read while its prefix is still a place, which is the
  declaration exits: `T::type v = 0;` in a template no use instantiates is
  refused, and `static_cast<T::type>(0)` beside it is not read until the
  arguments have settled `T`, by which time the prefix is a class. Five shapes
  `g++` refuses translate here — a template argument, a `static_cast`, a
  `sizeof`, a c-style cast and a default template argument.
- **14.6p2 beyond a bare place.** `require_written_type` refuses only a name
  whose prefix is a place its own head declared. `pa20/course/pa20/100-a-
  decltype-an-argument-list-wrote-is-a-tree.t` writes `box<decltype(make())>
  ::type` with no `typename` and its `.ref` expects EXIT_SUCCESS, so widening
  the clause to every dependent prefix costs that fixture.
- **`__builtin_invoke`.** Two tests naming a door no reading has.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain: n declarations of one name is 0.80 s at
  n = 3200. The walk is PA22's; keying the index by the canonical form is also a
  change to what 7.3.3p14's hiding key is built from.

## Active Checkpoint

None open. The turn's checkpoint is complete and in the ledger below.

## Next Substantial Checkpoint

**The candidate a deduction drops and should not**, which is the largest group
left and the one the stage owns outright: 21 calls end in `no declaration of X
accepts the arguments of a call` where both oracles name one. The two doors to
walk are `Deduction::match_argument`'s pairs — 14.8.2.1p2's reference and
array/function decay corners, 14.8.2.5's non-deduced contexts, and the braced
argument 14.8.2.1p1 leaves undeduced — and `arguments_of`, where a place no pair
reached takes 14.1p9's default. The 13 `no declaration of X is in scope` cases
sit under the same walk, one tier down.

## Performance Model

Measured on this turn's binary, warm cache, `/usr/bin/time` on the binary
itself (a harness that spawns `date`/`timeout` per run invents a ~0.1 s floor).

| sweep | shape | result |
| --- | --- | --- |
| SFINAE multiplicity | n classes × 4 candidates, 3 of which fail substitution | 0.01 s @32, 0.04 @128, 0.18 @512, 0.34 @1024 — linear |
| substitution nesting | d nested `enable_if` chains under 3 candidates | 0.01 s flat from d = 8 to d = 48 |
| dependent value nesting | d nested calls each deducing `X<A + 1>` | 0.00 s @12, 0.01 s @48 |
| declarations of one name | n function templates, one parameter list, distinct result types | 0.04 @400, 0.09 @800, 0.24 @1600, **0.80 s @3200 — quadratic** |
| whole PA23 corpus | 400 files, one process each | 1.94 s warm, no crash |

Why it stays linear: 14.8.2p8's attempt is a `try` block per candidate, which
costs nothing until a candidate actually fails; `specialize` and
`instantiate_class` are still interned per argument list, so a failed attempt
re-reads only what it built itself; and a dependent value argument is interned
per (reading, spelling) so one spelling under one head is one type however many
times a declarator writes it. 14.7.1p1's two cycle marks — `TemplateInfo::
choosing` and `SemaAnalyzer::specializing_` — are one hash insert and one erase
apiece beside a list that is interned anyway. The one quadratic is PA22's
`TemplateSignature::equivalent` chain walk, named under the known gaps above.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.{h,cpp}`, `sema_specialize.cpp`, `sema_analyzer.{h,cpp}`, `sema_function.cpp`, `sema_declarator.cpp`, `sema_scope.{h,cpp}`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal that came from reading an instantiated class body, which is outside the immediate context and still refuses the program; 14.1p3's unnamed *type* place is declared into the head's region, so `template<typename, typename>` is a head an argument list fits; a dependent value argument keeps its spelling, its place and its region, and 14.7.1p1 reads 5.19 over them again, so `X<A + 1>` in a function template's declarator substitutes; `substituted_region` binds a value place to its constant; `dependent_member_type` rebuilds the stand-in over the class *this* substitution made and refuses a member a concrete class does not declare; 14.5.6.1p5 rather than 13.1's parameter-list index is what pairs two declarations written under heads; 14.8.2.1p4's qualification conversion on the argument of a pair; 14.6p2's `typename` over a prefix that is a bare place. The audit added 14.7.1p1's bound on the attempt asking for itself (`TemplateInfo::choosing`, `SemaAnalyzer::specializing_`), which is what three stack-overflow crashes were; 14.8.2p8's lexical order, so a leading result type is substituted before the parameter list (`SemaEntity::trailing_result`); and 14.1p12 over the pair 14.5.6.1p5 now settles | 246 → 292 / 400; through-pa22 2948 / 2948; 0 corpus crashes against 3; sweeps above |
