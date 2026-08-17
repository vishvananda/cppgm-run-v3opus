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

Turn-start baseline **132 / 308**; now **154 / 308**, 154 failures.

Grouped by the compiler behaviour that owns them (one test can want two):

| # | Group | Owner | Signature |
|---|---|---|---|
| 39 | **alias templates** (`template<…> using X = …`) — 40 such tests, 39 fail | `sema_analyzer.cpp` alias path + `template_id_entity` | `no declaration of X is in scope` |
| 14 | parse failures | `ast_parser_*` | `… is not a translation unit` |
| 12 | out-of-class member-template definitions | `member_definition_owner` | `X is defined where no class declares it, which 9.3p1 gives no meaning` |
| 10 | LowIR mismatch (explicit-instantiation ownership, `extern template`) | `lowir_*` | `does not match reference` |
| 8 | a template-id written before `::` that this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition`, `… with no declarator names no class` |
| 6 | 14.5.5.2 ordering leaves two patterns unordered | `most_specialized` | `matches two partial specializations` |
| 5 | dependent array bound `T[N]` in a partial-specialization pattern | `sema_type_id.cpp`, `TypeTable::array_of` | `X is a template whose parameters PA20 does not instantiate` |
| 4 | access through member class templates and nested type paths | `sema_access.cpp` | `named where the access its class gave it does not reach`, plus `-bad` cases wrongly accepted |
| rest | dependent-name and instantiation timing | mixed | `no declaration of … is in scope` |

## Active Checkpoint

**Landed this turn — see the ledger. The next one is A (alias templates).**

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

*Validation.* `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` above 154 with
`make test-report-through-pa21` clean; the four
`400-member-alias-template-template-*` tests are the cross-check that the alias
entity reaches a template place.

## Performance Model

Measured with `dev/cppgm++ --emit-lowir -O0 -o <out>` on generated inputs under
`/tmp/perf`. The paths at risk this turn are the head read (once per clause), the
14.3.3p1 match (recursive, one level per nested template place), the interned
`TemplateName` entry, and the dependent-`C<A…>` memo.

| Path | Sweep | Result |
|---|---|---|
| nested template places in one head | depth 2 → 10 | 0.004s → 0.005s — flat; the head is read once per clause node and the match recurses one level per nesting |
| distinct templates at a template place | 10 → 200 namings, each a different template | 0.006 → 0.044s — linear, one interned entry per template |
| one template named at a place n times | 10 → 200 | 0.006 → 0.031s — linear; the entry is interned, so the n namings share it |
| partial-specialization patterns over a template place × candidate count | 25×4 → 100×8 (cross product, not one axis) | 0.006 → 0.008s — flat in both |
| nested heads each named | depth 4 → 16 | 0.005 → 0.007s — flat |

`valgrind -q --error-exitcode=9` is clean on a two-argument template-place case
and on `200-adl-template-template-argument-namespace.t`; the memo keyed by
`const AstNode*` holds `TemplateInfo*` into `template_patterns_`, a deque, so
nothing it hands out moves.

Correctness cross-check against the third oracle: `_ZN6holderIN1N3boxEE3getEv`
and `_ZN6holderIN1Q2fnEE3getEv` agree with `g++ -std=c++11` byte for byte.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|---|---|---|
| — | turn-start baseline | 132 / 308 |
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration* so `C<A…>` in the pattern is the ordinary template-id path. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. While `C` is still the place, `C<A…>` is 14.6.2p1's dependent type, interned by place and list, and 14.8.2.5p4 deduces both halves at once. `sema_template_head.cpp` became `TemplateHead` with a header of its own, which freed the room in `sema_analyzer.h`. | 142 / 308 |
| **T2** the place's own default, and a pattern's qualifiers | 14.1p2's default at a template place is an id-expression naming a template, so the parse is told a template-name stands there; 14.2p4's `X::template f` keyword is no part of the name a lookup asks for; 14.8.2.5p4 leaves a pair's qualifiers where they were written, so `L<T…>` matches no class an argument list wrote `const` on. | 146 / 308 |
| **T3** the object-file name | `TypeKind::TemplateName` had no `operand_of` arm, so two entries standing for two templates interned as one type and two specializations became one symbol. Beside it: `<template-arg>` writes such an argument as `ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY`, and 3.4.3 names the template from outside every region around it. | 147 / 308 |
| **T4** 14.1p11 is about a primary head | A pack was refused anywhere but last in *every* head. 14.1p11 is written about a head an argument list is read against; 14.5.5p1's head writes no such list, so a pack stands anywhere in one — and what the deduction leaves for it is one entry per place rather than one flat list. | 152 / 308 |
| **T5** the region an argument associates | 3.4.2p2 gives an argument at a template place the namespace or class that declares the template it named, and no more — a template is no type. 14.6.2p1 answers the other end: `U::template fn` behind an unsettled prefix stands as written. | 154 / 308 |
