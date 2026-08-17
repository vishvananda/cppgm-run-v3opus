# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The stage is carried by the owners PA19–PA21 already left standing, extended
rather than replaced:

- `sema_template_head.cpp` — 14.1p2's head and 14.3p1's argument list. One
  place per parameter, one reading per written argument, and what a place *is*
  settled once in 14.6.1p1's own region.
- `sema_template.cpp` — the template entity graph: `TemplateInfo` per template,
  `instantiate_class`/`specialize` per argument list, `substituted` as the one
  door a dependent type comes back through.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`, because every
  fact keyed by one (the specialization, the object-file name, the memo) reads
  it as types. A type argument is the type; a value argument is `TypeKind::Value`;
  a run is `TypeKind::Pack`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

Turn-start baseline: **132 / 308** (98 spec + 210 general), 176 failures.

Grouped by the compiler behaviour that owns them (one test can want two):

| # | Group | Owner | Signature |
|---|---|---|---|
| 46 | **template-template parameters and 14.3.3 argument matching** | head + argument list | 34 × `X is a template whose parameters PA20 does not instantiate`, plus dependent `F<T>` lookups and one parse failure |
| ~30 | class partial specialization patterns the head refuses (function types, cv/ref-qualified, unbounded array) | `sema_template.cpp` partial ordering | `does not instantiate`, `no declaration of X is in scope` |
| 15 | parse failures | `ast_parser_*` | `... is not a translation unit` |
| 12 | out-of-class member-template definitions | `member_definition_owner` | `X is defined where no class declares it, which 9.3p1 gives no meaning` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition` |
| 6 | access checking through member class templates / alias paths | `sema_access.cpp` | `X is named where the access its class gave it does not reach`, plus 4 `-bad` cases wrongly accepted |
| 11 | LowIR mismatch (explicit instantiation ownership, extern template) | `lowir_*` | `generated LowIR does not match reference` |
| rest | dependent-name and instantiation timing | mixed | `no declaration of ... is in scope` |

## Active Checkpoint

**T — 14.1p2's template place and 14.3.3's template argument.**

*Owner.* `sema_template_head.cpp` owns the place and the argument; `type_model`
owns the entry a named template is one of; `sema_template.cpp` owns the
dependent `F<...>` and its substitution; `ast_parser_class.cpp` owns 14.1p2's
default argument at a template place.

*Data flow.* `read_template_head` marks a `type-parameter` written `template<…>
class` a **template place** and reads its own parameter-clause into a head of its
own, memoised by the clause node. `open_parameter_region` gives the place a
`TypeKind::TemplateParameter` type marked `parameter_template`, so a definition
read against it finds a *template* under that name. `bound_argument` at a
template place resolves the written spelling to a template entity, checks
14.3.3p1 against the place's own head, and interns
`TypeKind::TemplateName` — one entry per template entity. `bind_argument` binds
the place's name straight to that entity (a second name for one entity, as
7.3.3's using-declaration does), so `F<int>` inside the pattern is the ordinary
template-id path with no second implementation. Inside the pattern, where `F` is
still the place, `template_id_entity` answers 14.6.2p1's dependent type — a
stand-in interned by the place and the argument list — and `substituted`'s
default arm settles the place first and instantiates the real template.

*Expected complexity.* One head read per written clause (memoised by node), one
`resolve` per written template argument, one interned type per template entity.
No new scan is added to any argument list: the template place is decided by a
bool on the place the head already holds.

*Validation.* `make -C pa22 test` pass count above 132 with earlier PAs clean;
`make test-report-through-pa21`; `perl scripts/cppgm_file_audit.pl --stage pa22
--paths dev/src`. Scaling risk is the memoised nested head and the interned
`TemplateName`; both are keyed, so a sweep over a head named n times is O(n)
lookups and no re-read.

## Performance Model

| Path | Shape | Cost |
|---|---|---|
| (to be measured this turn) | | |

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|---|---|---|
| — | turn-start baseline | 132 / 308 |
