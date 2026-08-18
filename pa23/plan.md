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

400 tests, 288 passing (turn-start baseline 246). The 112 left, grouped by the
compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a wrong overload survives to a false `static_assert` | ~10 | the refusal a substitution should have made is not made at all |
| an operand rule missing, so a probe that must fail succeeds | ~8 | `void*` arithmetic, post-increment of a const lvalue, narrowing in a list |
| pointer/reference/enum non-type arguments | ~6 | "outside the PA20 subset" — PA23 owns these |
| a dependent call still refused at instantiation | ~15 | "no declaration of X accepts the arguments of a call" outside any attempt |
| `is not a translation unit` | ~5 | parse gaps (`operator new` in a NTTP, c-style casts at a value place) |
| LowIR text mismatch | ~21 | the program compiles; the emitted LowIR differs |
| the rest | ~47 | one-off clauses, one test each |

Known gaps diagnosed but not landed this turn, with their evidence:

- **14.6p2 beyond a bare place.** `require_written_type` refuses only a name
  whose prefix is a place its own head declared. `pa20/course/pa20/100-a-
  decltype-an-argument-list-wrote-is-a-tree.t` writes `box<decltype(make())>
  ::type` with no `typename` and its `.ref` expects EXIT_SUCCESS, so widening
  the clause to every dependent prefix costs that fixture.
- **`typename` in a template argument, and `__builtin_invoke`.** Two and two
  tests, each naming a door no reading has.

## Active Checkpoint

None open. The turn's checkpoint is complete and in the ledger below.

## Performance Model

Measured on this turn's binary, warm cache, `/usr/bin/time` on the binary
itself (a harness that spawns `date`/`timeout` per run invents a ~0.1 s floor).

| sweep | shape | result |
| --- | --- | --- |
| SFINAE multiplicity | n classes × 4 overloads, 3 of which fail substitution | 0.01 s @32, 0.06 s @256, 0.12 s @512, 0.24 s @1024 — linear |
| deduction nesting | n nested calls each deducing `X<A + k>` | 0.00 s through depth 20 |
| substitution nesting | n nested `W<i><A+1>::type` aliases under one deduction | 0.00 s through depth 12 |
| whole PA23 corpus | slowest single test | 0.01 s |

Why it stays linear: 14.8.2p8's attempt is a `try` block per candidate, which
costs nothing until a candidate actually fails; `specialize` and
`instantiate_class` are still interned per argument list, so a failed attempt
re-reads only what it built itself; and a dependent value argument is interned
per (reading, spelling) so one spelling under one head is one type however many
times a declarator writes it.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.cpp`, `sema_analyzer.cpp`, `sema_function.cpp`, `sema_declarator.cpp`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal that came from reading an instantiated class body, which is outside the immediate context and still refuses the program; 14.1p3's unnamed *type* place is declared into the head's region, so `template<typename, typename>` is a head an argument list fits; a dependent value argument keeps its spelling, its place and its region, and 14.7.1p1 reads 5.19 over them again, so `X<A + 1>` in a function template's declarator substitutes; `substituted_region` binds a value place to its constant; `dependent_member_type` rebuilds the stand-in over the class *this* substitution made and refuses a member a concrete class does not declare; 14.5.6.1p5 rather than 13.1's parameter-list index is what pairs two declarations written under heads; 14.8.2.1p4's qualification conversion on the argument of a pair; 14.6p2's `typename` over a prefix that is a bare place | 246 → 288 / 400; through-pa22 2948 / 2948; sweeps above |
