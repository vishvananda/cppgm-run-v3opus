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
  three kinds: a type, a value of a written type, or a template.
- `sema_template.cpp` — the template entity graph: `TemplateInfo` per template,
  `instantiate_class`/`specialize` per argument list, `substituted` as the one
  door a dependent type comes back through.
- `sema_specialize.h/.cpp` — the three heads whose declaration the primary's own
  three steps cannot answer for: 14.5.5's partial specialization (the middle step
  changes), 14.5.1p1's variable template and 14.5.7p1's alias template (the last
  step changes — what an argument list makes of it is a constant, or a type that
  already exists).
- `sema_name.h/.cpp` — the one place a written spelling is turned back into what
  the program wrote: components, template-argument lists, and 14.2p4's keyword.
- `sema_deduce.cpp` — one P/A walk, shared by 14.8.2 and 14.5.5.1.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

Checkpoint A landed **193 / 308** and its audit held it there, 115 failures,
with the failing set unchanged. Grouped by the compiler behaviour that owns
them, from the diagnostic each one now reaches:

| # | Group | Owner | Signature |
|---|---|---|---|
| 14 | **parse failures** — `a.f<int>()` for a member template, out-of-class member-template heads, `extern template` over a constructor or operator | `recognizer_*`, `ast_parser_*` | `… is not a translation unit` |
| 13 | out-of-class member-template definitions | `member_definition_owner` | `X is defined where no class declares it, which 12.1p1 gives no meaning` |
| 11 | compiles but the LowIR does not match | `lowir_*` | none; explicit-instantiation ownership and `extern template` |
| 10 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 8 | a definition whose head is written apart from the declaration | `sema_function.cpp` | `a definition of X matches no declaration of it` |
| 7 | a template-id before `::` this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition`, `… with no declarator names no class` |
| 7 | `-bad` cases wrongly accepted | mixed | none |
| 4 | 14.5.5.2 ordering by pack *prefix length* | `most_specialized` | `matches two partial specializations` |
| 4 | a template-id that names no type at a dependent prefix | `sema_type_id.cpp` | `X does not name a type` |
| 4 | access through member class templates and nested type paths | `sema_access.cpp` | `named where the access its class gave it does not reach` |
| 3 | a head 14.1p2 declares that this milestone still refuses to instantiate | `TemplateHead` | `X is a template whose parameters PA20 does not instantiate` |
| 3 | two declarations of one name overloaded by *arity* | declaration merge | `X is defined twice` |
| 20 | constant-expression, sizeof-in-argument and call-resolution one-offs | mixed | various |

## Active Checkpoint

**A landed and was audited this turn — see the ledger. The next one is P (the
member-template parse boundary), which owns the 14-test parse group and feeds
the 13-test out-of-class group behind it.**

*Owner.* `recognizer_expression.cpp` and `ast_parser_name.cpp` for `a.f<int>()`
and `X::f<int>()` where `f` is a member template written with no `template`
keyword; `recognizer.cpp`'s `parse_template_declaration` for the two-clause head
`template<class T> template<class U>`.

*Data flow.* 14.2p4 lets the keyword be omitted wherever the prefix is not
dependent, so the parse cannot lean on it: what says `<` opens an argument list
is `DeclaredNames`, which already answers that question for an unqualified name
(`NameKind::Template`, `NameKind::FunctionTemplate`) and does not answer it for a
name written after `.`, `->` or `::`. The member's own declaration is what
settles it, and the parser records member names already.

*The semantic half is done.* The A audit gave 5.2.5p1's lookup 14.2's exit, so
a member id the parser hands over as a template-id is answered:
`300-nondependent-member-template-id-call` translates verbatim once its
`h.get<int>(4)` is written `h.template get<int>(4)`. What P has to change is
where the argument list is recognised and nothing behind it.

*Expected complexity.* One `DeclaredNames` probe per member name written with a
following `<`, which is the probe the unqualified path already pays; no
backtracking beyond the one the recognizer already does at `<`.

*Validation.* `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` above 193 with
`make test-report-through-pa21` clean; the 14 `… is not a translation unit`
tests are the direct count and `300-nondependent-member-template-id-call` is the
narrowest of them.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf`,
against a `make build` of the pre-audit commit in a worktree, and against
`pa22/cppgm++-ref`. The paths at risk are the alias substitution (one per naming
unless memoised), a nest of aliases each naming the one below it twice (2^depth
unless the memo is keyed by the interned list), `QualifiedName::part`, which
every lookup in the compiler now runs 14.2p4's test inside, and 5.2.5p1's new
14.2 exit, which every member access that finds no member now reaches.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|---|---|---|---|
| one alias named n times | 400 → 3200 | 0.02 → 0.16 s, 11 → 48 MB | 3.62 s at 3200 |
| alias nest, each naming the one below **twice** | depth 4 → 24 | 0.00 s, flat at 6.5 MB | 0.56 s at 24 |
| n distinct argument lists through one alias | 400 → 3200 | 0.05 → 0.54 s, 22 → 135 MB | 7.11 s at 3200 |
| member alias template named n times | 400 → 3200 | 0.02 → 0.16 s, 12 → 48 MB | 2.25 s at 3200 |
| `S::template f<int>()` written n times | 400 → 3200 | 0.01 → 0.07 s, 8 → 24 MB | 1.10 s at 3200 |
| `s.template f<int>()` written n times | 400 → 3200 | 0.01 → 0.08 s, 9 → 27 MB | 0.93 s at 3200 |
| `s.template f<Ti>()` over n *distinct* lists | 400 → 3200 | 0.04 → 0.34 s, 17 → 92 MB | 11.06 s at 3200 |
| `.template g<int>()` chained | depth 4 → 64 | 0.00 s, flat at 6.4 → 7.2 MB | — |
| the whole 308-file PA22 corpus | — | 1.26 s | — |

Every dimension is linear in what it sweeps. The nest is the one that could have
been 2^depth: `Specialization::alias` memoises on `(template, interned argument
list)` through `SemaModel::hold_specialization`, so a type-id that writes
`f<T>` twice reads it once and depth 24 costs what depth 4 does. 14.2p4's test is
two integer comparisons and one `compare` on the component a split already
built — `part` makes no copy it did not make before, and `prefix` reads an offset
the split already recorded. The member-access exit is the probe the qualified
path already pays, and it runs only where the class declares no member of the
name.

Not this checkpoint's, but measured and recorded: a nest of aliases whose
*result* type doubles per level (`pair2<a<T>, a<T> >`) is inherently 2^depth — at
depth 20 it is 0.39 s / 178 MB and at depth 24 it is 6.80 s / 2.37 GB. The cost
is the known per-subobject layout cost, not the alias layer's.

`valgrind -q --error-exitcode=9` is clean over the five largest scaling inputs
and over 90 alias/keyword/member-template probes. Correctness cross-check
against the third oracle: `_Z3use3boxIiE` for a parameter written through an
alias agrees with `g++ -std=c++11` byte for byte across two translation units,
and 84 probe shapes pass the assignment's own comparator against the reference's
LowIR.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|---|---|---|
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration*. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. `C<A…>` over a place is 14.6.2p1's dependent type, interned by place and list. | 142 / 308 |
| **T2** the place's own default, and a pattern's qualifiers | 14.1p2's default at a template place is an id-expression naming a template; 14.2p4's `X::template f` keyword is no part of the name a lookup asks for; 14.8.2.5p4 leaves a pair's qualifiers where they were written. | 146 / 308 |
| **T3** the object-file name | `TypeKind::TemplateName` had no `operand_of` arm, so two templates interned as one type and two specializations became one symbol. Beside it: `<template-arg>` writes such an argument as `ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY`. | 147 / 308 |
| **T4** 14.1p11 is about a primary head | A pack was refused anywhere but last in *every* head. 14.1p11 is written about a head an argument list is read against; 14.5.5p1's head writes no such list, so a pack stands anywhere in one. | 152 / 308 |
| **T5** the region an argument associates | 3.4.2p2 gives an argument at a template place the namespace or class that declares the template it named, and no more. 14.6.2p1 answers the other end: `U::template fn` behind an unsettled prefix stands as written. | 154 / 308 |
| **T audit** the exits the four new facts were written at | 14.3.3p1 was asked at the class tier and at neither exit of the function tier. `QualifiedName::names_a_template_id` tells 14.6.1p1's injected-class-name from a template-*id* written at a place. `places_match` is one pair reading, asked of each place a pack has left. `substituted` asks `dependent_template_name` where it minted a second type for one naming. | 156 / 308 |
| **A** 14.5.7p1's alias template, and the name a template-id is looked up by | `template<…> using X = T;` records a head and a *pattern that is a type-id*, so `X<A…>` declares nothing: 7.1.3p2 makes it another name for the type the arguments substitute into that type-id, which is what leaves 14.5.7p2's two namings one type and the ABI name the aliased type's. The declaration is a `Typedef` carrying a `TemplateInfo`, so `TemplateHead::template_argument` — which already accepted one at a template place — reaches it, and 11p1's access travels from the template onto the typedef-name one argument list makes. Beside it, 14.2p4's keyword: it was dropped at one exit, the template-argument reader, and at none of the others, so `a.template f<A>()`, `X::template f<A>` and `typename A<T>::template B<int>` were four different refusals. `sema_name.h` — already the one place a spelling is turned back into components — now drops it inside `QualifiedName::part`, which is where every reader already asks. | **193 / 308** |
| **A audit** the three regions a template-id is looked up in | A name no declaration bound may still be a template-id, and `resolve` answers that at both its exits where 5.2.5p1's member lookup answered it at neither — so `a.template f<int>()`, the refusal the checkpoint was written to end, still stood. `template_specializations` now takes the region a member access looks in. `QualifiedName::prefix` is the nested-name-specifier read off the split rather than off the spelling by `last().size()`, which the keyword's move made shorter than the span it came from. 11p1's access is written by every tier that makes a declaration from an argument list and not only by the class tier and the alias. 14.7.2p2 is asked of what the template-id answered, so an alias template's typedef-name is refused where it was dereferenced. | 193 / 308 |
