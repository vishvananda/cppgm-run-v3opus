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
  one door a dependent type comes back through. `record_template` is also
  14.5.4p1's tier: a head over a friend elaborated-type-specifier declares a
  class template of the enclosing namespace and grants to it.
- `sema_class.cpp` — 12's special members, which 14.5.2p1 lets a head stand
  over; and 11.3's `granting_class`/`friend_target`/`accessible`, where 11.2p5's
  naming class and 11.3p1's grant meet.
- `sema_function.cpp` with `Scope::hidden`/`hidden_index` — 11.3p6's chain of
  declarations one region holds and binds nothing of, indexed by the declaration
  it was made with so 7.3.1.2p3's reveal costs one probe. `declare_function` is
  also where 11.3p6 and 3.4.1p10 part company: a friend declaration is *made* in
  the namespace and *written* inside the class, and what 14.7.1p1 reads a
  pattern against is the second of those.
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

The F audit landed **249 / 326** — F's own 246 of 323 plus the 3 `course/pa22`
fixtures this audit wrote, with the failing set unchanged. The 77 that fail are
the set checkpoint M's audit left minus the 12 F fixed, grouped by the compiler
behaviour that owns them, from the diagnostic each one now reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 19 | compiles but the exit status or the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 3 of them and 3 more are `-bad` cases wrongly accepted |
| 12 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 9 | a template-id before `::` this walk does not settle | `resolve_prefix` | `X is written after a name that is not a namespace, class or enumeration` |
| 3 | 14.7.3p1's member of a specialization redeclared | declaration merge | `X is defined twice` |
| 3 | a `static_assert` whose fold comes out false | mixed | `a static_assert condition is false` |
| 3 | a head 14.1p2 declares that this milestone still refuses to instantiate | `TemplateHead` | `X is a template whose parameters PA20 does not instantiate` |
| 2 | 11.3p2's `friend C;` naming a *dependent* class | `grant_class_friendship` | `a friend declaration with no declarator names no class` |
| 2 | a default template argument the arity check counts | `TemplateHead` | `a template-argument-list gives X more arguments than it has parameters` |
| 2 | a cast written as a template argument | `sema_constant.cpp` | `names a type that is not integral` |
| 22 | constant-expression, sizeof-in-argument, arity and call-resolution one-offs | mixed | various |

14.5.5.2's ordering by pack *prefix* length and the six access failures through
member class templates, which checkpoint M's audit recorded as groups of their
own, are gone: F's 11.2p5 naming class and 14.5.4p1 grant answered them.

Known gaps probed and deliberately left:

- 11.3p11 leaves the name a friend elaborated-type-specifier first declares
  unbound until a matching declaration is written in the namespace, and both
  spellings bind it: `class host { friend class late; };` and `class host {
  template<class U> friend class late; };` each leave `late` findable, so `late*
  p = 0;` and `late<int>* p = 0;` are two programs `pa22/cppgm++-ref` and `g++`
  both refuse and this build accepts. The non-template spelling predates PA22
  and the template one is the tier 14.5.4p1 added beside it, so the fix is one
  hidden *type* chain read by the two declaration sites - `class_declaration`'s
  elaborated arm and `record_template`'s class tier - rather than anything
  14.5.4p1 owns.
- 14.5.4p1's grant is recorded in the lowering dialect alone, because PA11 and
  PA12 model a class template's class inside the head's own region and a friend
  declaration's class in the namespace, so the two can never be one entity
  there: `pa12/cppgm++-ref --emit-semantics` accepts a `late<T>` reaching a
  private member of the class that befriended `late` and this build refuses it.
  The PA11/PA12 dumps themselves agree byte for byte.
- 9.2p1 is enforced nowhere: `struct A { int f(); int f(); };` is accepted, and
  so are two equivalent member-template declarations. The template case is the
  shadow of the general one, so half-fixing it in the template path alone would
  answer one clause at two sites. `g++ -pedantic-errors` refuses all three.
- A conversion function template is keyed by the *spelling* of its
  conversion-type-id, so `operator U()` and `operator V()` over one head are two
  members and an out-of-class definition that renames its place matches none.
  14.8.2.3 at the *named* exit rests on the same fact: `a.operator int()`
  reaches no declaration here or in `pa22/cppgm++-ref`.
- An **empty** out-of-class destructor of a class template is elided by 12.4p8
  where `pa22/cppgm++-ref` and `g++` both write the definition.
- `template<class U> friend class W;` inside a class that is itself a *private*
  nested class, and then `W<T>` naming that class: we refuse where `g++` accepts
  and the non-template spelling of the same program is refused by `g++` too. The
  refusal is the one the non-template path already gives, so g++ is the outlier.

## Active Checkpoint

**The F audit landed this turn — see the ledger.** The next one is **D**,
14.6.2's dependent names an instantiation has to find (`no declaration of … is
in scope`, 12 fixtures) together with the 9 that stop at `resolve_prefix`: both
are one owner, the walk that turns a written prefix into a region once the
arguments are in hand, and the 9 are the same failure one component earlier.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22g`,
against `pa22/cppgm++-ref`. Six traps are recorded rather than re-measured:
`timeout`/`date` spawned per run invents a ~0.1 s floor that reads as 33 s over
the corpus, `cppgm++` run by hand needs `-o` or it compiles nothing, a relative
binary path measures 0.00 s and 1 MB from a shell whose directory moved, the
whole corpus handed to one process is one ill-formed unit and times as 0.00 s,
`/usr/bin/time` writes to stderr so a child whose stderr is discarded loses
every measurement, and `g++ file.t` treats a `.t` as a *linker input* and exits
0 with a warning — a verdict sweep without `-x c++` reads every ill-formed
program as accepted. Every generated input is checked for exit 0 before it is
timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| n specializations of a class template that declares a friend class template | 100 → 800 | 0.02 → 0.20 s, 12 → 51 MB | 2.00 s at 800 |
| n distinct friend class templates in one class, each used | 100 → 800 | 0.01 → 0.14 s, 10 → 41 MB | 1.24 s at 800 |
| n hidden friend function templates, each called through ADL | 100 → 800 | 0.02 → 0.18 s, 11 → 50 MB | 1.60 s at 800 |
| n class templates each *defining* a friend function template, each instantiated | 100 → 800 | 0.03 → 0.28 s, 13 → 67 MB | 2.06 s at 800 |
| n friend function templates of one name in one hidden chain, each reached by ADL | 100 → 800 | 0.02 → 0.22 s, 12 → 59 MB | 7.24 s at 800 |
| n friend declarations of one name, each revealed by a namespace declaration | 800 → 3200 | 0.19 → 0.79 s, 53 → 197 MB | 22.40 s at 800 |
| a protected member reached through a friend across a derivation chain | depth 4 → 256 | 0.00 → 0.02 s, 6 → 11 MB | 1.13 s at 256 |
| a friend template declared in a class nested d deep, and one named d deep | depth 2 → 24 | 0.00 s, 6 MB | — |
| the whole 326-file corpus, one process per file | — | 1.36 s | — |

Every dimension is linear in what it swept and flat in depth. They are linear
because the grant is one entry per *pair of templates* rather than per argument
list — `befriended` asks the pair as the use spelled it and then with each side
replaced by its `primary`, which is four hash probes and no walk — because
11.2p5's new naming class only reaches `befriends_between`, whose walk is the
single base path 10.1p3 already refuses a second of, and because
`Scope::hidden_index` keys a hidden chain by the declaration it was *made* with,
so 7.3.1.2p3's reveal drops one entry instead of re-keying everything left. That
last one is the audit's own fix: keyed by the chain's head it was 0.82 / 3.23 /
**15.64 s** at n = 800 / 1600 / 3200 and it is 0.19 / 0.38 / **0.79 s** now.

The one quadratic left on this surface is 13.3's own: n overloads of one name
and n calls of it is n candidate sets of n, which is what the program wrote
rather than what a reader repeats — `perf` puts 17 % of that run in
`converting_constructor` and none of it in the friend readers, and the same
shape with distinct names is linear. The reference is 57 s at n = 200 there.

`valgrind -q --error-exitcode=9` is clean over 36 inputs, 0 errors: the four
largest scaling ones, the two deepest nests and every probe program this audit
wrote. Run evidence: a unit holding a hidden friend function template, a friend
class template, a friend function template *defined* inside a class template
whose declarator writes the owner's parameter, and one declared there and
defined at namespace scope compiles through `lowir2cy86` + `cy86` and exits
**18**, the value `g++ -std=c++11` gives it; a two-unit build of the friend
class-template grant links and runs. Third oracle: all nine `object=` names that
unit writes — `_Z4grabIiET_3boxIiES0_` and `_Z4grabIiET_3boxIcES0_` among them —
agree with `g++` byte for byte.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|------------|-------------|-----------|
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
| **M** 14.5.2's member template, and the two heads its definition writes | A head over a constructor or a conversion function inside a class body declares a member *template* of that class, which reaches neither `function_definition` nor `declare_function` - so the class body walk sent it to `special_member_definition`, where an unqualified constructor name reads as an out-of-class definition of nothing and 12.1p1 refuses it. `special_member` now declares into `declaring_region`'s class with the head over the declarator, writes `template_parameters` and `record_function_template`, and takes 14.7.1p1's specialization rather than declaring a second member. 14.5.2p3's out-of-class definition writes one head per enclosing class template and then the member's own. 14.5.6.1p5 gained the value place. | 212 / 308 |
| **M2** the four exits a member template's definition can be written at | `template<class U> A::A(U) {}` and `template<class T> template<class U> A<T>::A(U) {}` read their parameter clause against the class alone and found no `U`; 3.4.1p8 puts the head *inside* the region its declarator-id names. `specialize` copied `object_member` and `access` but not which special member it declares, so 12.6.2's mem-initializers never ran. The ABI writes a function template's result type, which 12.1p1, 12.4p1 and 12.3.2p1 leave writing none. Beside it, 14.5.2p1's "a destructor shall not be a template" and 14.5.6.1p5's second declaration of one constructor template. | 219 / 308 |
| **M audit** the two facts 12 writes about a special member, and the class 12.1's entry points are owed by | 14.7.1p1's specialization is the declaration the template declares, so 12.3.1p2's `explicit` and 8.4.3's `= delete` travel onto it. 12.3.2p1's conversion function template reached *no* use at all: `Deduction::from_conversion` is 14.8.2.3's one P/A pair, asked at the one of `gather_conversions`' four readers that has a destination, and 13.3.3p1's last two tie-breaks came with it. The ABI named such a specialization from the *substituted* type. And `writes_base_entry` asked whether the *function* is an instantiation where 12.1's second entry point is a question about the class. Beside them, 8.4.2p1's `= default` under a head. | 229 / 318 |
| **F** 14.5.4's friend templates, and the class 11.2p5 names a member in | A friend declaration under a head reached none of what 11.3 already knew. `SemaModel::befriended` now asks the pair as the use spelled it and then with each side replaced by its `primary`, which is what 14.5.4p1 means: the grant a class template's own definition wrote is between two *templates* and every access check asks it of two specializations. `record_template` gained 14.5.4p1's tier - a head over a friend elaborated-type-specifier declares a class *template* in 11.3p11's enclosing namespace and grants to it - and `template_declaration` asks it under `templating()` rather than `lowering()`, because 14.6p8's PA11-dialect reading of a pattern was declaring a plain class of that spelling and refusing the program's own `template<class T> class W`. 14.7.1p1's instantiation reads the definition again in a region that binds arguments under no class at all, so `function_definition` asks 11.3p1 of the *template* rather than of the regions it stands in, and the head a friend declaration is written under is what says it declares a template - not the namespace 11.3p6 moved the declaration into. `equivalent_template` is asked of 11.3p6's hidden chain, where a later declaration of one friend function template has nowhere else to find it, and `reveal_friend` indexes the declaration that leaves by its *own* parameter type list. 11.3p1's granting class is taken at entry, because `StandingIn` moves the head a qualified declarator-id stands under inside the region that name reaches. 11.3p10's qualified friend now has to name a declaration that region made. And 11.2p5's naming class is the class a *qualified name* was looked up in as much as the one an object expression writes, which is what lets a member reach a protected member of a base through a class between them that befriends it. | **246 / 323** |
| **F audit** the two readings one friend definition gets, and the region 11.3p6 moved the declaration out of | A friend declaration written in a class *template* is read twice - 14.6p8's reading of the pattern and 14.7.1p1's for each specialization - and 11.3p6 puts both in one namespace, so the pattern reading's `defined` made one instantiation of a class defining a friend function template `unwrap is defined twice`. It declares and does not define; the instantiation defines, which is also what makes a second one 14.5.4p1's redefinition. `record_function_template` recorded the *namespace* as the region 14.7.1p1 reads the pattern against, where 3.4.1p10 reads a friend definition where it was written - the only region binding what the class's own instantiation bound - so `friend T mixed(box<T>, U)` was `T does not name a type` at the call that deduced it. 11.3p6's other half, that a friend declaration defines only under an unqualified declarator-id, was unwritten. And `Scope::hidden_index` keys a hidden chain by the declaration it was made with, so 7.3.1.2p3's reveal drops one entry rather than re-keying the whole chain: 15.64 s to 0.79 s at n = 3200. | **249 / 326** |
