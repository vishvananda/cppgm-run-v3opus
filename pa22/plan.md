# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The owners PA19–PA21 left standing carry the stage, extended rather than
replaced:

- `ast_parser_*.cpp` with `ast_names.h` — the syntax boundary, and the one fact
  the parse has about a name no scope it models declares: what some declaration
  of the unit made the spelling. 14.2's `<` is settled from it.
- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. One place per parameter, one reading per written argument, and
  what a place *is* settled once in 14.6.1p1's own region.
- `sema_template.cpp` — the template entity graph: `TemplateInfo` per template,
  `instantiate_class`/`specialize` per argument list, `substituted` as the one
  door a dependent type comes back through.
- `sema_definition_names.cpp` — 14.6p8's first of the two readings: the names a
  template definition writes, looked up where the definition stands.
- `sema_specialize.h/.cpp` — the three heads whose declaration the primary's own
  three steps cannot answer for: 14.5.5's partial specialization, 14.5.1p1's
  variable template and 14.5.7p1's alias template.
- `sema_name.h/.cpp` — the one place a written spelling is turned back into what
  the program wrote: components, template-argument lists, and 14.2p4's keyword.
- `sema_deduce.cpp` — one P/A walk, shared by 14.8.2 and 14.5.5.1.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

Checkpoint P landed **200 / 308** and its audit held that count with the failing
set byte-identical. The 14 `… is not a translation unit` failures are gone —
every PA22 test now parses. Grouped by the compiler behaviour that owns them,
from the diagnostic each one now reaches:

| # | Group | Owner | Signature |
|---|---|---|---|
| 14 | **14.5.2's constructor template** — `struct A { template<class U> A(U); };` is refused on its own, before any call | `special_member`, `record_template` | `A is defined where no class declares it, which 12.1p1 gives no meaning` |
| 18 | compiles but the exit status or the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 3 of them and 3 more are `-bad` cases wrongly accepted |
| 8 | a template-id before `::` this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition`, `… with no declarator names no class` |
| 5 | access through member class templates and nested type paths | `sema_access.cpp` | `named where the access its class gave it does not reach` |
| 5 | a template-id that names no type at a dependent prefix | `sema_type_id.cpp` | `X does not name a type` |
| 5 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 4 | 14.5.5.2 ordering by pack *prefix length* | `most_specialized` | `matches two partial specializations` |
| 3 | a head 14.1p2 declares that this milestone still refuses to instantiate | `TemplateHead` | `X is a template whose parameters PA20 does not instantiate` |
| 3 | two declarations of one name overloaded by *arity* | declaration merge | `X is defined twice` |
| 3 | a `static_assert` whose fold comes out false | mixed | `a static_assert condition is false` |
| 25 | constant-expression, sizeof-in-argument, redeclaration-shape and call-resolution one-offs | mixed | various |

## Active Checkpoint

**P's audit landed this turn — see the ledger. The next one is C, 14.5.2's
constructor template, which owns the single largest group.**

*Owner.* `sema_class.cpp`'s `special_member` (the in-class declaration) and
`sema_template.cpp`'s `record_template` (the head it stands under), with
`special_member_definition` for the out-of-class `template<class U> A::A(U) {}`.

*Where it stops now.* `struct A { template<class U> A(U); };` alone is refused,
with no call written. The member is a `TemplateDeclaration` node, so the class
body walk sends it to `declaration` rather than to `special_member`;
`record_template` answers nothing for a `SpecialMemberDeclaration` whose name
carries no nested-name-specifier, and the generic walk then reaches
`special_member_definition`, which reads an unqualified constructor name as an
out-of-class definition of nothing. That one refusal is the whole of the 14-test
group: 12.1p1's diagnostic is reached before any of the deduction, ownership or
lowering behaviour the tests are about.

*Data flow.* 14.5.2p1 makes the member a *function template* whose name is the
class's own. `sema_function.cpp` already has the pair of facts that makes one -
`entity.template_parameters = head_region` and `record_function_template` - and
`special_member` already builds the constructor entity and chains it on
`owner.constructor`. What C has to add is the route from a `TemplateDeclaration`
wrapping a special member to `special_member` with `ctx.template_head` in force,
those two facts on the entity it makes, and 13.3's deduction over the
constructor chain.

*Expected complexity.* One extra branch per class member read, and one
deduction per constructor candidate that is a template - which is the cost the
ordinary member function template already pays.

*Validation.* `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` above 200 with
`make test-report-through-pa21` clean; the 14 `12.1p1 gives no meaning` tests
are the direct count and `struct A { template<class U> A(U); };` is the
narrowest of them.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22a`,
against a `make build` of the pre-audit tree in a worktree, and against
`pa22/cppgm++-ref`. Three traps are recorded rather than re-measured:
`timeout`/`date` spawned per run invents a ~0.1 s floor that reads as 33 s over
the 308-file corpus, `cppgm++` run by hand needs `-o` or it compiles nothing,
and 308 files handed to one process is one ill-formed unit and times as 0.00 s.

| Path | Sweep | This build | Baseline | `pa22/cppgm++-ref` |
|---|---|---|---|---|
| `h.get<int>(i)` with no keyword, n times | 400 → 3200 | 0.01 → 0.10 s, 8 → 28 MB | matches | 1.04 s at 3200 |
| `h.get<Ai>(a)` over n *distinct* argument lists | 400 → 3200 | 0.07 → 0.61 s, 23 → 141 MB | matches | 35.37 s at 3200 |
| `h.get < a` where `get` is *also* a template of the unit | 400 → 3200 | 0.02 → 0.20 s, 12 → 59 MB | — | 1.18 s at 3200 |
| `extern template struct box<Ti>;`, n distinct | 400 → 3200 | 0.02 → 0.18 s, 11 → 52 MB | 0.17 s, 47 MB | 0.76 s at 3200 |
| `extern template box<Ti>::box();`, n distinct | 400 → 3200 | 0.02 → 0.17 s, 11 → 51 MB | matches | 0.74 s at 3200 |
| `template struct box<Ti>;`, n distinct | 400 → 3200 | 0.03 → 0.33 s, 15 → 85 MB | matches | 3.75 s at 3200 |
| n partial specializations each naming their own head | 400 → 3200 | 0.02 → 0.24 s, 14 → 66 MB | — | 1.08 s at 3200 |
| `.get<H>(h)` chained | depth 4 → 256 | 0.00 → 0.01 s, 6.5 → 9 MB | — | — |
| compound statements nested in a specialization's body | depth 4 → 256 | 0.00 → 0.01 s, 6.5 → 8 MB | matches | — |
| the whole 308-file corpus, one process per file | — | 1.31 s | 1.28 s | — |

Every dimension is linear. The one that could not have been is the third: a
member name that is *also* a template spelling tries a template-id reading at
every `<`, and a reading that fails is remembered by `skip_simple_template_id`'s
`(position, qualified)` memo, so a backtracking caller pays it once. 5.2.2p1's
`(` is what bounds the guess, and 14.2p4's definition-time check is one
`find('<')` on the member spelling before any `QualifiedName` split is made.
13.5p6 on a new specialization is one `compare(0, 8, "operator")`. 14.7.2p2 is
one reading per explicit instantiation, now taken for both of p8's and p9's
forms - which costs p9's form the specialization entity p8's always made, 5 MB
over 3200 declarations and no term of the input.

`valgrind -q --error-exitcode=9` is clean over the six largest inputs above.
Third oracle: `_ZN1H3getIiEET_S1_` for `h.get<int>(4)` written with no keyword
agrees with `g++ -std=c++11` byte for byte, and all 25 of the audit's probe
programs agree with `g++ -std=c++11 -pedantic-errors` on the verdict.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|---|---|---|
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration*. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. | 142 / 308 |
| **T2** the place's own default, and a pattern's qualifiers | 14.1p2's default at a template place is an id-expression naming a template; 14.2p4's `X::template f` keyword is no part of the name a lookup asks for; 14.8.2.5p4 leaves a pair's qualifiers where they were written. | 146 / 308 |
| **T3** the object-file name | `TypeKind::TemplateName` had no `operand_of` arm, so two templates interned as one type and two specializations became one symbol. Beside it: `<template-arg>` writes such an argument as `ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY`. | 147 / 308 |
| **T4** 14.1p11 is about a primary head | A pack was refused anywhere but last in *every* head. 14.1p11 is written about a head an argument list is read against; 14.5.5p1's head writes no such list, so a pack stands anywhere in one. | 152 / 308 |
| **T5** the region an argument associates | 3.4.2p2 gives an argument at a template place the namespace or class that declares the template it named, and no more. 14.6.2p1 answers the other end: `U::template fn` behind an unsettled prefix stands as written. | 154 / 308 |
| **T audit** the exits the four new facts were written at | 14.3.3p1 was asked at the class tier and at neither exit of the function tier. `QualifiedName::names_a_template_id` tells 14.6.1p1's injected-class-name from a template-*id* written at a place. `places_match` is one pair reading, asked of each place a pack has left. | 156 / 308 |
| **A** 14.5.7p1's alias template, and the name a template-id is looked up by | `template<…> using X = T;` records a head and a *pattern that is a type-id*, so 7.1.3p2 makes `X<A…>` another name for the type the arguments substitute into it. The declaration is a `Typedef` carrying a `TemplateInfo`, and 11p1's access travels from the template onto the typedef-name. Beside it, 14.2p4's keyword is dropped inside `QualifiedName::part`, where every reader already asks. | 193 / 308 |
| **A audit** the three regions a template-id is looked up in | `resolve` answered a template-id at both its exits where 5.2.5p1's member lookup answered it at neither. `QualifiedName::prefix` is read off the split rather than by `last().size()`. 11p1's access is written by every tier that makes a declaration from an argument list. 14.7.2p2 is asked of what the template-id answered. | 193 / 308 |
| **P** the three places a template-argument-list is read, and the head that names a specialization | 14.2p4 makes the keyword optional wherever the object expression is not type-dependent, so `h.get<int>(4)` is a template-id the parse has to recognise with no keyword to lean on: `DeclaredNames::names_a_template` answers it from the unit-wide record 6.8p1 is already settled by, and 5.2.2p1 - a member function template can only be called - bounds the guess to a list a `(` follows, which keeps `a.b < c > d` two comparisons. 14.6p8's own reading is where the *dependent* case is refused, since 14.2p4 there reads the name as a non-template. 14.2p1's other two template-ids - `operator+<int>` and `operator+<>` - are read by one `skip_template_arguments` shared with the simple-template-id. 14.7.2p1 and 14.7.3p1 let a declaration name a specialization rather than declare anything, so `extern template box<int>::box();` and `template<> S<int>::S();` end at a `;` no out-of-class constructor otherwise may. `extern template` now reads its target for the same p2 requirement p8's form is read for, and 13.5p6 is asked where a function template specialization is *made*, which is the one place every argument list reaches. And 14.5.5p1: a class-head written on a template-id declared the whole flattened spelling as a template-name, so `MapBase<K,int>` written anywhere after it stopped being a type. | **200 / 308** |
| **P audit** the two forms 14.7.2 writes one requirement in | `explicit_instantiation` returned on `!owed` before reading its target, so p2 was asked of `template struct X<int>;` and of nothing `extern template` wrote - an alias, an ordinary class, a name no template declares and a prefix naming no type were four programs `g++` refuses and this build accepted. `instantiated_class` is p2's one reading and both forms reach it, with the `owed` gate moved onto p8's demand for the definitions. Beside it: the `SpecialMemberDeclaration` the same commit let the parse accept reached no sema arm, so 12.1p1's own declaration now asks p2 of its *prefix*; 14.7.2p1's member class of a specialization is looked up in the region the prefix resolved to; and the parser's `names_specialization_` is put down for the body a declaration holds, so a statement inside a specialization reads as a statement. | **200 / 308** |
