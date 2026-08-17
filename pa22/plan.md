# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The owners PA19–PA22 left standing carry the stage, extended rather than
replaced:

- `ast_parser_*.cpp` with `ast_names.h` — the syntax boundary, and the one fact
  the parse has about a name no scope it models declares: what some declaration
  of the unit made the spelling. 14.2's `<` is settled from it.
- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. One place per parameter, one reading per written argument, and
  what a place *is* settled once in 14.6.1p1's own region.
- `sema_template.h/.cpp` — the template entity graph: `TemplateInfo` per
  template, `TemplateSignatures` for 14.5.6.1p5's comparison of two heads,
  `instantiate_class`/`specialize` per argument list, and `substituted` as the
  one door a dependent type comes back through.
- `sema_class.cpp` — 12's special members, which 14.5.2p1 lets a head stand
  over: a constructor template and a conversion function template reach neither
  `function_definition` nor `declare_function`, so this file writes for them
  what those write for every other member template.
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

Checkpoint M landed **224 / 313** — 219 of the 308 the turn began with, from
200, plus the 5 new `course/pa22` fixtures. The 15 tests that stopped at
12.1p1's refusal and the 8 that stopped at "matches no declaration" are gone.
Grouped by the compiler behaviour that owns them, from the diagnostic each one
now reaches:

| # | Group | Owner | Signature |
|---|---|---|---|
| 19 | compiles but the exit status or the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 3 of them and 3 more are `-bad` cases wrongly accepted |
| 10 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 9 | a template-id before `::` this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 7 | friend templates | `sema_class.cpp` | `a friend declaration is written outside a class definition`, `… with no declarator names no class` |
| 6 | access through member class templates and nested type paths | `sema_access.cpp` | `named where the access its class gave it does not reach` |
| 4 | 14.5.5.2 ordering by pack *prefix length* | `most_specialized` | `matches two partial specializations` |
| 3 | 14.7.3p1's member of a specialization redeclared | declaration merge | `X is defined twice` |
| 3 | a `static_assert` whose fold comes out false | mixed | `a static_assert condition is false` |
| 3 | a head 14.1p2 declares that this milestone still refuses to instantiate | `TemplateHead` | `X is a template whose parameters PA20 does not instantiate` |
| 2 | a cast written as a template argument | `sema_constant.cpp` | `names a type that is not integral` |
| 23 | constant-expression, sizeof-in-argument, arity and call-resolution one-offs | mixed | various |

Known gaps this checkpoint probed and deliberately left:

- 9.2p1 is enforced nowhere: `struct A { int f(); int f(); };` is accepted, and
  so are two equivalent member-template declarations. The template case is the
  shadow of the general one, so half-fixing it in the template path alone would
  answer one clause at two sites. `g++ -pedantic-errors` refuses all three.
- 14.8.2.3: `a.operator int()` naming a specialization of a conversion function
  template reaches no declaration. `pa22/cppgm++-ref` refuses it too.

## Active Checkpoint

**M landed this turn — see the ledger. The next one is F, 14.5.4 and 11.3's
friend templates, which owns the largest group with a single owner.**

*Owner.* `sema_class.cpp`'s friend path with `sema_function.cpp`'s
`friend_target`, reached from the class-body walk.

*Where it stops now.* A `friend` declaration written under a template head
inside a class body reaches the namespace-scope arm and is refused with `a
friend declaration is written outside a class definition`; a `friend class T;`
whose declarator is absent reaches `… with no declarator names no class`. Both
are the same missing fact: the class the declaration was *written in*, which
the head's own region now stands between.

*Data flow.* 11.3p1 makes the friend a declaration of the region around the
class with the class's access; `declaring_region` already answers which class a
head was written in — the same fact checkpoint M gave `special_member` — so
`friend_target` is asked of it rather than of `ctx.scope`.

*Expected complexity.* One region walk per friend declaration, which is the
walk `declares_member_template` already makes.

*Validation.* `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` above 219 with
`make test-report-through-pa21` clean; the 7 friend diagnostics are the direct
count.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22b`,
against a `make build` of `3d611d98` in a worktree, and against `g++
-std=c++11`. Three traps are recorded rather than re-measured: `timeout`/`date`
spawned per run invents a ~0.1 s floor that reads as 33 s over the 308-file
corpus, `cppgm++` run by hand needs `-o` or it compiles nothing, and 308 files
handed to one process is one ill-formed unit and times as 0.00 s. A fourth was
hit this turn: `/usr/bin/time` writes to stderr, so redirecting the child's
stderr to `/dev/null` silently discards every measurement.

| Path | Sweep | This build | `g++ -std=c++11` |
|---|---|---|---|
| n constructor templates in one class, 1 call | 100 → 800 | 0.01 → 0.07 s, 9 → 26 MB | — |
| 1 constructor template, n calls | 100 → 800 | 0.01 → 0.04 s, 7 → 17 MB | — |
| 1 constructor template, n *distinct* argument types | 100 → 800 | 0.02 → 0.18 s, 12 → 49 MB | — |
| n out-of-class member template definitions, distinct names | 100 → 800 | 0.02 → 0.16 s, 11 → 47 MB | — |
| n constructor templates × n calls | 100 → 800 | 0.03 → 1.04 s, 12 → 57 MB | 0.28 → 2.61 s |
| n member-template *overloads* of one name × n calls | 100 → 800 | 0.04 → 1.27 s, 15 → 72 MB | 0.28 → 2.72 s |
| the same with a value place in each head | 100 → 800 | 0.06 → 2.08 s, 16 → 92 MB | 0.34 → 3.96 s |
| constructor templates chained through nested members | depth 4 → 256 | 0.00 → 0.04 s, 7 → 17 MB | — |
| the whole 308-file corpus, one process per file | — | 1.29 s | baseline 1.28 s |

Every term is linear on its own. The three quadratic rows are 13.3's own cross
product — n calls each comparing n candidates — and `g++` grows the same way at
about twice the wall clock, so none of them is this checkpoint's. The one that
could have been worse is the value-place row: 14.5.6.1p5's signature is built
once per declaration (`TemplateSignatures::built`) and each value place's own
type is canonicalized with the bindings the walk has already made, which are
exactly the places before it — so one memo serves every substitution in a head
and the whole type after it, and no place is read twice.

`valgrind -q --error-exitcode=9` is clean over the six largest inputs. Run
evidence: the six shapes above compile through `lowir2cy86` + `cy86` and exit
the value `g++ -std=c++11` gives them; a parameter of *class* type is garbage in
that scaffold for a non-template constructor too, so the sweeps pass scalars and
pointers. Third oracle: `_ZN1AC1IiEET_` for a constructor template agrees with
`g++` byte for byte, and 13 probe programs over 14.5.2's ill-formed shapes agree
with `g++ -std=c++11 -pedantic-errors` on the verdict but for the two 9.2p1
cases recorded above. Differential: the five new `course/pa22` fixtures match
`pa22/cppgm++-ref`'s LowIR through the real comparator.

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
| **P** the three places a template-argument-list is read, and the head that names a specialization | 14.2p4 makes the keyword optional wherever the object expression is not type-dependent, so `h.get<int>(4)` is a template-id the parse has to recognise with no keyword to lean on: `DeclaredNames::names_a_template` answers it from the unit-wide record 6.8p1 is already settled by, and 5.2.2p1 bounds the guess to a list a `(` follows, which keeps `a.b < c > d` two comparisons. 14.6p8's own reading is where the *dependent* case is refused. 14.2p1's other two template-ids are read by one `skip_template_arguments`. 14.7.2p1 and 14.7.3p1 let a declaration name a specialization rather than declare anything. And 14.5.5p1: a class-head written on a template-id declared the whole flattened spelling as a template-name. | 200 / 308 |
| **P audit** the two forms 14.7.2 writes one requirement in | `explicit_instantiation` returned on `!owed` before reading its target, so p2 was asked of nothing `extern template` wrote - four programs `g++` refuses and this build accepted. `instantiated_class` is p2's one reading and both forms reach it. Beside it: 12.1p1's own declaration asks p2 of its *prefix*, 14.7.2p1's member class is looked up in the region the prefix resolved to, and `names_specialization_` is put down for the body a declaration holds. | 200 / 308 |
| **M** 14.5.2's member template, and the two heads its definition writes | A head over a constructor or a conversion function inside a class body declares a member *template* of that class, which reaches neither `function_definition` nor `declare_function` - so the class body walk sent it to `special_member_definition`, where an unqualified constructor name reads as an out-of-class definition of nothing and 12.1p1 refuses it. `special_member` now declares into `declaring_region`'s class with the head over the declarator, writes `template_parameters` and `record_function_template`, and takes 14.7.1p1's specialization rather than declaring a second member when a reading for one argument list reached it; `demand_constructor_definition` is where building the object asks the template for the body, since a constructor is reached by no name; and 14.6p8 reads such a body where it stands, which keeps a worse conversion function template uninstantiated where a non-template one wins. 14.5.2p3's out-of-class definition writes one head per enclosing class template and then the member's own, so `record_template` looks through the nest to ask `member_definition_owner`. 14.5.6.1p5 gained the value place: the stand-in carries the type it binds a value of, over the places before it. | **212 / 308** |
| **M2** the four exits a member template's definition can be written at | `template<class U> A::A(U) {}` and `template<class T> template<class U> A<T>::A(U) {}` read their parameter clause against the class alone and found no `U`; 3.4.1p8 puts the head *inside* the region its declarator-id names, which is what `StandingIn` already does for `function_definition`. `specialize` copied `object_member` and `access` from the template but not which special member it declares, so a constructor template's specialization was an ordinary function - 12.6.2's mem-initializers never ran and the object file spelled it as a source name; and 12.6.2p2's members were taken from wherever the reading stood rather than from the class. The ABI writes a function template's result type, which 12.1p1, 12.4p1 and 12.3.2p1 leave those three writing none of. Beside it, 14.5.2p1's "a destructor shall not be a template" and 14.5.6.1p5's second declaration of one constructor template, both found by a `g++ -pedantic-errors` verdict sweep. | **219 / 308** |
