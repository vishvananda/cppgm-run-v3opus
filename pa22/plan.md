# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The owners PA19–PA21 left standing carry the stage, extended rather than
replaced:

- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. One place per parameter, one reading per written argument, and
  what a place *is* settled once in 14.6.1p1's own region. A place is one of
  three kinds now: a type, a value of a written type, or a **template**.
- `sema_template.cpp` — the template entity graph: `TemplateInfo` per template,
  `instantiate_class`/`specialize` per argument list, `substituted` as the one
  door a dependent type comes back through.
- `sema_specialize.cpp` — 14.5.5's partial specialization: a head whose places
  are all deduced from an argument pattern, and 14.5.5.2's ordering.
- `sema_deduce.cpp` — one P/A walk, shared by 14.8.2 and 14.5.5.1.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`, because every
  fact keyed by one (the specialization, the object-file name, the memo) reads
  it as types: a type argument is the type, a value is `TypeKind::Value`, a run
  is `TypeKind::Pack`, a template is `TypeKind::TemplateName`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

Turn-start baseline **132 / 308**; checkpoint T landed **154 / 308**; the audit
of T leaves **156 / 308**, 152 failures.

Grouped by the compiler behaviour that owns them (one test can want two):

| # | Group | Owner | Signature |
|---|---|---|---|
| 39 | **alias templates** (`template<…> using X = …`) — 40 such tests, 39 fail | `sema_analyzer.cpp` alias path + `template_id_entity` | `no declaration of X is in scope`, `X does not name a type` |
| 17 | compiles but the LowIR or the expected refusal does not match | `lowir_*`, `sema_access.cpp` | no diagnostic; explicit-instantiation ownership, `extern template`, and `-bad` cases wrongly accepted |
| 14 | parse failures | `ast_parser_*` | `… is not a translation unit` |
| 12 | out-of-class member-template definitions | `member_definition_owner` | `X is defined where no class declares it, which 12.1p1 gives no meaning` |
| 7 | a template-id written before `::` that this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition`, `… with no declarator names no class` |
| 5 | dependent array bound `T[N]` in a partial-specialization pattern | `sema_type_id.cpp`, `TypeTable::array_of` | `X is a template whose parameters PA20 does not instantiate` |
| 4 | access through member class templates and nested type paths | `sema_access.cpp` | `named where the access its class gave it does not reach` |
| 4 | 14.5.5.2 ordering by pack *prefix length* — `list<A0, Rest...>` against `list<A0, A1, Rest...>`, with no template place written | `most_specialized` | `matches two partial specializations` |
| rest | dependent-name and instantiation timing | mixed | `no declaration of … is in scope` |

## Active Checkpoint

**T landed and was audited this turn — see the ledger and `audit.md`. The next
one is A (alias templates).**

*Owner.* `sema_analyzer.cpp`'s `alias_declaration` and `record_template` own the
declaration; `template_id_entity` owns the naming; the entity is a `Typedef`
carrying a `TemplateInfo`, which is already what `TemplateHead::template_argument`
accepts at a template place.

*Data flow.* `template<…> using X = T;` records a head and a *pattern* that is a
type-id rather than a class body, so `X<A…>` is not a declaration of its own:
7.1.3p2 makes it another name for the type the arguments substitute into the
pattern. So the naming binds the arguments, substitutes, and answers the
type — no `instantiate_class`, no specialization entity, and 14.5.7p2 leaves two
namings of one alias one type. A member alias template is the same reading with
the class's own head standing outside it.

*Expected complexity.* One substitution per (alias, interned argument list),
memoised on the alias's `TemplateInfo` the way `chosen` already is, so an alias
named n times costs one substitution.

*Validation.* `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` above 156 with
`make test-report-through-pa21` clean; the four
`400-member-alias-template-template-*` tests are the cross-check that the alias
entity reaches a template place, and `TemplateHead::template_argument` already
refuses a template-*id* there, so the alias must arrive as a template-name.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf`,
against a `make build` of the pre-audit checkpoint in a worktree and against
`pa22/cppgm++-ref`. The paths at risk are the head read (once per written
clause), the 14.3.3p1 match (one level per nested template place, and one pair
per place a pack has left), the interned `TemplateName` entry, and the
dependent-`C<A…>` memo — which both the naming and the substitution now ask.

| Path | Sweep | This build | Pre-audit | `pa22/cppgm++-ref` |
|---|---|---|---|---|
| nested template places in one head | depth 2 → 12 | 0.00 s, flat at 6 MB | same | 0.53 s |
| nesting × namings (cross product) | 3×200 → 11×3200 | 0.03 → 0.77 s, 14 → 189 MB | 0.02 → 0.76 s | 0.70 → 7.60 s |
| function-tier template place | 400 → 3200 namings | 0.06 → 0.58 s, 22 → 138 MB | 0.06 → 0.57 s | 0.97 → 10.64 s |
| distinct templates at a template place | 400 | 0.04 s, 17 MB | 0.04 s | 0.81 s |
| one template named at a place n times | 400 | 0.01 s, 8 MB | 0.01 s | 0.61 s |
| `C<T>` written n times in one pattern | 400 | 0.01 s, 11 MB | 0.01 s | 0.75 s |
| one `C<T>` pattern over n argument lists | 400 | 0.07 s, 23 MB | 0.07 s | 1.10 s |
| out-of-class member definition | 400 specializations | 0.05 s, 18 MB | 0.04 s | 0.83 s |
| the whole 169-file PA22 corpus | — | 1.31 s | 1.30 s | — |

Every dimension is linear in what it sweeps and none carries a 2^depth term: the
head is read once per clause node, the naming is interned per place and interned
list, and the substitution's own arm asks that same memo rather than minting a
type of its own.

`valgrind -q --error-exitcode=9` is clean over 77 probe programs and over the
three largest scaling inputs; the memo keyed by `const AstNode*` holds
`TemplateInfo*` into `template_patterns_`, a deque, so nothing it hands out
moves.

Correctness cross-check against the third oracle: `_ZN6holderIN1N3boxEE3getEv`
and `_ZN6holderIN1Q3boxEE3getEv` agree with `g++ -std=c++11` byte for byte, and
64 probe programs lower to LowIR the assignment's own comparator finds identical
to the reference.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|---|---|---|
| — | turn-start baseline | 132 / 308 |
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration* so `C<A…>` in the pattern is the ordinary template-id path. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. While `C` is still the place, `C<A…>` is 14.6.2p1's dependent type, interned by place and list, and 14.8.2.5p4 deduces both halves at once. `sema_template_head.cpp` became `TemplateHead` with a header of its own, which freed the room in `sema_analyzer.h`. | 142 / 308 |
| **T2** the place's own default, and a pattern's qualifiers | 14.1p2's default at a template place is an id-expression naming a template, so the parse is told a template-name stands there; 14.2p4's `X::template f` keyword is no part of the name a lookup asks for; 14.8.2.5p4 leaves a pair's qualifiers where they were written, so `L<T…>` matches no class an argument list wrote `const` on. | 146 / 308 |
| **T3** the object-file name | `TypeKind::TemplateName` had no `operand_of` arm, so two entries standing for two templates interned as one type and two specializations became one symbol. Beside it: `<template-arg>` writes such an argument as `ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY`, and 3.4.3 names the template from outside every region around it. | 147 / 308 |
| **T4** 14.1p11 is about a primary head | A pack was refused anywhere but last in *every* head. 14.1p11 is written about a head an argument list is read against; 14.5.5p1's head writes no such list, so a pack stands anywhere in one — and what the deduction leaves for it is one entry per place rather than one flat list. | 152 / 308 |
| **T5** the region an argument associates | 3.4.2p2 gives an argument at a template place the namespace or class that declares the template it named, and no more — a template is no type. 14.6.2p1 answers the other end: `U::template fn` behind an unsettled prefix stands as written. | 154 / 308 |
| **T audit** the exits the four new facts were written at | 14.3.3p1 was asked at the class tier and at neither exit of the function tier, whose places are declarations rather than entries of a head — so `use<pair2>()` was accepted and a template place's default was read as 8.1p1's type-id. `QualifiedName::names_a_template_id` now tells 14.6.1p1's injected-class-name from a template-*id* written at a place, which the lookup answers alike. `places_match` is one pair reading, asked of each place a pack has left, and a pack P declared matches the run of none. `match_template_id` reads a naming over a place as it reads one over a named template — which is what 14.5.5.2p1's ordering hands it — and carries 14.8.2.1p3 and p4's allowances. `substituted` asks `dependent_template_name` where it minted a second type for one naming. | 156 / 308 |
