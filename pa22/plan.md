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
  variable template and 14.5.7p1's alias template. `member_pattern` is also
  which of those bodies an out-of-class member definition was written over.
- `sema_pattern.h/.cpp` — 14.6p8's reading of a template definition where it
  stands, and 14.6.1p1's class it is read as. One per body a naming may be read
  from - the primary's `TemplateInfo::current` and each `Partial::current` - and
  14.5.1.3p1's out-of-class member definition is that same reading arriving
  later, against whichever of them its declarator-id named.
- `sema_name.h/.cpp` — the one place a written spelling is turned back into what
  the program wrote: components, template-argument lists, and 14.2p4's keyword.
- `sema_deduce.cpp` — one P/A walk, shared by 14.8.2 and 14.5.5.1. What a type
  *is* is settled here as much as what it deduces: a function type's
  cv-qualifier-seq and ref-qualifier and a value argument's bits are compared,
  and a fixed place facing an entry that stands for a run takes nothing.
- `sema_type_id.cpp` — `SpelledTypeId`, the second implementation of 8.1p1's
  type-id and 8.3.5p1's parameter clause. 14.2 writes a template-argument-list
  inside a name, so an argument reaches the semantic layer as text; every rule
  the tree reading knows has to be written here too.
- `sema_pack.cpp` — 14.5.3p4's expansion: which packs a pattern names
  (`collect_packs`, through a template-id whose template is a place), what an
  expansion comes to (`expand_type`), and how a substitution reaches through one
  (`substitute_entry`).
- `type_model.h/.cpp` — every argument list is a list of `TypeId`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

The O audit landed **285 / 336** — the 283 of 334 O left, plus the 2
`course/pa22` fixtures the audit wrote. The 51 that fail are the same 51 O left
and group by the compiler behaviour that owns them, from the diagnostic each one
now reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 17 | compiles but the exit status or the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 3 of them and 7 more are `-bad` cases wrongly accepted |
| 6 | a constant expression written inside a template argument | `sema_constant.cpp` | `names a type that is not integral`, `does not close its arguments`, `names no constant` |
| 5 | 14.7.3p1's member of a specialization redeclared, or its definition matching none | declaration merge | `X is defined twice`, `a static_assert condition is false` |
| 4 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 3 | 11.3p2's `friend C;` naming a *dependent* class, and 11.2's reach into one | `grant_class_friendship` | `a friend declaration with no declarator names no class` |
| 3 | `sizeof` over a stood-in call written as a template argument | `sema_constant.cpp` | `is written inside sizeof as a template argument and names no type` |
| 2 | a default template argument the arity check counts | `TemplateHead` | `a template-argument-list gives X more arguments than it has parameters` |
| 2 | an out-of-class member template whose owner renamed its places | `StandingIn` | `X does not name a type` |
| 2 | a member alias template rebound through its owner | `Specialization::alias` | `rebind_alloc<T> does not name a type` |
| 7 | call resolution, access, a dependent member typedef and lowering one-offs | mixed | various |

The 5 that stopped at `resolve_prefix` and 9 of the dependent-name failures were
one owner and are gone: an out-of-class definition whose declarator-id names a
member of a *partial specialization* or of a member *class template*. What is
left of that shape is the two `StandingIn` renames and
`spec/300-explicit-instantiation-out-of-class-nested-member`, each of which now
stops somewhere else.

Known gaps probed and deliberately left:

- `pa22/cppgm++-ref` accepts a member of a partial specialization of a member
  class template defined outside its class and then emits a `declare function`
  for it: `_ZN6holderIiE4slotIPcE5widthEv` is a symbol its own LowIR leaves
  undefined and `lowir2cy86` refuses to link. Its static-data-member twin is
  worse - it leaves *both* bodies' globals undeclared. This build writes the
  definitions and runs the value `g++` gives, so the `course/pa22` fixture for
  14.5.5p1's member-template bodies pins the reading and stops short of the
  materialization the reference cannot write.
- 14.5.2p3's declarator-id says which head parameterises which class by how far
  the region has already bound, so a head written over a class the *enclosing*
  head has yet to name is read as the enclosing one's. Every spelling the
  fixtures write is read; what is not is a definition whose second head names a
  member of a class the first head's own arguments do not reach.
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
- 8.3.4p1's array bound is an `unsigned long long` on the type, and a bound the
  reading has not settled stands in as **1** - so `template<class T, unsigned
  long N> struct r<T[N]>` writes the pattern `T[1]` and `read_pattern` refuses
  the head outright. Deducing `N` needs a bound that is a `TypeId`, which the
  ABI's `ABI_ARRAY_BOUND_RAW` and every reader of `types_.bound` would move
  with it. `spec/100-class-partial-specialization-array-size.t` is the fixture.
- 14.3.2p5's conversion of a value argument at a *dependent* place is made
  where the argument is read and not where the place settles, so the entry
  `value_type(place, bits)` holds carries the source constant's bits. Probed
  through both spellings and through the emitted symbol the two still name one
  specialization even where the conversion is not width-preserving -
  `wrap<char>::type` for `ic<T, 300>`, `ic<char, 300>` and `ic<char, 44>` are one
  entity and one `_ZN2icIcLc44EE1fEv` where `pa22/cppgm++-ref` writes two - so
  what is left is the pattern side: `probe<ic<T, 300> >` does not take
  `ic<char, 44>`, which is the answer `g++` gives too.
- 14.3.2p2's narrowing conversion at a value place is refused by neither this
  build nor `pa22/cppgm++-ref`: `ic<char, 300>` and `ic<T, 300>` for `T = char`
  are accepted here and there, and `g++ -pedantic-errors` refuses both. 8.3.5p6
  is the same shape - `h<int(char)(long)>` and `h<int(char)[3]>` are function
  types this reading and the reference both build.
- A non-type argument of *pointer* type - `template<class T, T* p>` named
  `&g`, and `template<int* p>` too - is refused before the place is ever asked
  about: `TemplateArgumentReader` has no address arm, so `& is written as a
  template argument and names no constant`. Both oracles accept.
- A rooted nested-name-specifier written directly after a type-specifier is not
  recoverable from the spelling PA10 flattened: `int ::C::*` arrives as
  `int::C::*`, one word, and the ptr-operator reading takes `int::C` for the
  owner. The reference reads it. Every other spelling of the form - `int C::*`,
  `int A::B::*`, `int W<int>::*`, `int (C::*)(char) const` - is read here.
- One expansion over *two* function parameter packs - `template<class... A,
  class... B> int f(pr<A, B>...)` - deduces nothing here and nothing in
  `pa22/cppgm++-ref` either, where the class-tier spelling of the same rule now
  deduces in both. `g++` takes it. The two run through different pairings: a
  parameter clause is one P/A pair per parameter and a template-argument-list is
  one `match_arguments`.
- `pa22/cppgm++-ref` answers `split<box<> >` and
  `split<box<pr<int, char>, pr<long, short> > >` from the primary where the
  pattern `box<pr<A, B>...>` takes both here and in `g++`; a run of *one* it
  reads. So the course fixture for 14.5.3p4's two packs pins the run of one.
- `pa22/cppgm++-ref` reads three declarations of one pattern over a pack -
  `template<class H, class... T> struct twice<box<H, T...> >;` written thrice -
  as three partial specializations and calls the naming ambiguous.
  `g++ -pedantic-errors` accepts it and so does this build, which is why the
  course fixture for 14.5.6.1p5's signature stops short of that shape.

## Active Checkpoint

**O and its audit landed — see the ledger.** The next one is **X**, 14.7.3p1's
explicit specialization of a member: `spec/300-explicit-specialization-member-
function`, `…-out-of-class-member-overrides-primary`, `…-replaces-primary-
instantiation` and `…-static-data-member` are one owner - a `template<>`
definition of one member of one specialization is the definition that
specialization *has*, so 14.7.1p1's instantiation of the pattern's own member
shall not also write one and `X is defined twice` is this build writing both.
`explicit_functions` is keyed by the template and the argument list, which a
member of a class specialization has neither of: the declaration is the class's
member and what `template<>` wrote for it is a fact of that declaration.
`spec/300-explicit-specialization-after-instantiation` and
`…-function-explicit-specialization-declaration-before-primary-definition` are
the ordering half of the same clause.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22e`,
`/tmp/perf22f`, `/tmp/perf22g` and `/tmp/pa22audit`, against `pa22/cppgm++-ref`
and, where a row says so, against the turn-start build in a worktree of
`eac77160`. The rows a checkpoint carried forward are that checkpoint's own
generators; the audit's rows are its own. Ten traps are recorded rather than
re-measured: `timeout`/`date` spawned per run invents a
~0.1 s floor that reads as 33 s over the corpus, `cppgm++` run by hand needs
`-o` or it compiles nothing, a relative binary path measures 0.00 s and 1 MB
from a shell whose directory moved, the whole corpus handed to one process is
one ill-formed unit and times as 0.00 s, `/usr/bin/time` writes to stderr so a
child whose stderr is discarded loses every measurement, `g++ file.t` treats a
`.t` as a *linker input* and exits 0 with a warning and `-x c++` written
*after* the input file has no effect and does not fix it, `bc` is absent so a
best-of-three written around it silently keeps the first run, `date +%s.%N`
interpolated into `python3 -c` loses the second reading and prints a syntax
error rather than a time, and `make ref-test` regenerates only `tests/` - a
course fixture needs `TEST='course/pa22/x.t'` spelled **relative**, and it
writes through the `course` symlink into `cppgm.tests/`. Every generated input
is checked for exit 0 before it is timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| n partial specializations of one template over a function type, each named | 100 → 800 | 0.01 → 0.23 s, 10 → 38 MB | 10.62 s at 800 |
| n namings of a pattern that writes 14.8.2.5p5's non-deduced context | 100 → 800 | 0.01 → 0.12 s, 10 → 39 MB | 0.91 s at 800 |
| n pointer-to-member type-ids written as template arguments | 100 → 800 | 0.01 → 0.09 s, 9 → 32 MB | 0.82 s at 800 |
| a function type of n parameters written as a template argument | 100 → 800 | 0.00 s, 6 → 7 MB | 0.54 s at 800 |
| one pack pattern matched against a run of n | 800 → 3200 | 0.00 → 0.01 s, 7 → 8 MB | 0.64 s at 3200 |
| *two* packs expanded together over a run of n | 800 → 3200 | 0.01 → 0.03 s, 8 → 13 MB | 1.21 s at 3200 |
| n member-function-pointer arguments with 8.3.5p1's qualifiers | 100 → 400 | 0.01 → 0.05 s, 10 → 20 MB | — |
| a function type reached through d nested member typedefs | depth 4 → 256 | 0.00 → 0.02 s, 6 → 13 MB | 0.56 s at 256 |
| a function type nested d deep as one argument | depth 4 → 24 | 0.00 s, 6 MB | — |
| a d-deep dependent member chain in a non-deduced pattern | depth 4 → 24 | 0.00 s, 6 → 7 MB | — |
| n namings of two multi-pack patterns beside each other | 2 → 64 | 0.00 → 0.02 s, 6 → 12 MB | — |
| **O** n patterns of one template, each with an out-of-class member definition, each named | 100 → 800 | 0.02 → 0.36 s, 13 → 59 MB | 12.13 s at 800 |
| **O** n out-of-class member definitions of one pattern | 100 → 800 | 0.00 → 0.04 s, 8 → 19 MB | 0.81 s at 800 |
| **O** n member class templates of one class template, each defined out of class | 50 → 400 | 0.00 → 0.03 s, 8 → 16 MB | 0.56 s at 400 |
| **O** a member class template nested d deep, each level defined out of class | depth 4 → 40 | 0.00 → 0.09 s, 6 → 16 MB | 17.78 s and **7.99 GB** at 24; killed at 32 |
| **O** n member class templates defined *in* class, against the turn-start build | 50 → 400 | 0.00 → 0.03 s, 7 → 15 MB | 0.02 → 0.03 s there; ref 0.55 s at 400 |
| **O audit** n namings of a member class template's *pattern*, its member defined out of class | 50 → 400 | 0.01 → 0.19 s, 10 → 37 MB | 0.60 → 1.19 s |
| **O audit** n out-of-class member definitions of one member-template pattern | 50 → 400 | 0.01 → 0.14 s, 8 → 20 MB | 0.57 → 0.86 s |
| **O audit** n specializations of one pattern that has 8 out-of-class members | 100 → 800 | 0.03 → 0.26 s, 13 → 64 MB | — |
| the whole 336-file corpus, one process per file | — | **1.40 s** | — |
| *(carried from F)* n friend declarations of one name, each revealed | 800 → 3200 | 0.19 → 0.79 s | 22.40 s at 800 |

The corpus row is re-measured again and the 3.07 s carried here after O is not
reproducible either: six runs give 1.39–1.47 s over the 336 files, which is the
1.43 s that stood before O. 1.13 s of it is the process floor - 3.4 ms per run,
measured by running `int main(){return 0;}` 336 times - so the compiler's own
work is 0.8 ms per fixture. 290 of the 336 compile to the end; the other 46 stop
at a diagnostic.

Every dimension is linear in what it sweeps and flat in depth except the first,
which is 14.5.5.1p1's own: n patterns beside one template and n lists naming it
is n candidate scans of n, which is what the program wrote. `perf` on the n =
800 case puts 28 % of the run in `Deduction::match` and `match_arguments` and
**0.08 %** in `SemaAnalyzer::substituted` — so `substitution_agrees`, D's second
reading, is not what the quadratic is made of. It is memoized by
`TemplateInfo::chosen`, which keys the whole choice by the interned argument
list, so a list named twice pays for one scan. This build is faster than the
reference in every dimension measured against it.

O's own three costs are each paid once per definition and nothing scans:
`Specialization::member_pattern` reads one head and one argument list per
out-of-class member definition of a template that *has* patterns and one test of
an empty vector for every template that has none; `PatternReading::nested_owner`
walks the components of one declarator-id once per head, stopping at the first
one the region cannot settle; and `PatternReading::instantiate` asks
`TemplateInfo::chosen`, which is a hash lookup on a number the specialization
already carries. `TemplateInfo::patterns` keys the current instantiation of each
body by its interned argument list, so `complete_specialization` tells one from
the primary's without scanning `partials`. What the guard change costs is one
14.6p8 reading per member class template a pattern declares - 0.02 s to 0.03 s
and 14.2 MB to 15.4 MB at n = 400, linear, and 18x the reference's 0.55 s.

The audit's own tier costs the same: `read_declaration` now asks
`member_pattern` the question `record_template` already asked, which is one head
read per definition of a member template that *has* patterns and a test of an
empty vector for every one that has none - 0.19 s at 400 namings of such a
pattern against the reference's 1.19 s, and 0.14 s at 400 definitions of one
against its 0.86 s.

The reference is exponential in that nesting: 0.53 s at depth 8, 2.08 s and
546 MB at 20, 17.78 s and 7.99 GB at 24, and the OOM killer at 32, where this
build is 0.02 s and 9 MB at 24 and 0.09 s and 16 MB at 40. Reading each level
once against the class the level above declared is what keeps it flat;
re-reading the whole prefix per level is what does not. `nested_owner` walks the
prefix once per head and so is d² component lookups over the whole nest, each of
them a hash lookup into a class `instantiate_class` already memoized.

The per-element costs are flat: `match_run` copies the outer bindings into each
element's map, which is a map of one or two entries however long the run is, and
takes a run per pack place rather than one — two packs over a run of 3200 is
0.03 s and 13 MB; `SpelledTypeId::suffix` reads `expand_type` once per written
parameter rather than once per element; and the ptr-operator is one string
compare on the word before a `*`, so a type-id that writes none pays nothing.

`valgrind -q --error-exitcode=9` is clean over 57 inputs after the audit's fix,
0 errors: its six largest scaling inputs, the twenty out-of-class shapes it
swept, the four programs the fix turned green and the two `course/pa22` fixtures
it added; it was clean over 107 before it, and over 87 before O. The audit's own
run evidence: the twenty shapes - a static data member, a constructor, a
destructor, a nested class, a member function template, a conversion function,
an operator, a two-parameter pattern beside two more, a redeclaration that
renamed its places, a use written before the definition, a namespace-qualified
owner - and the four the fix turned green all compile through `lowir2cy86` +
`cy86` and run the value `g++ -std=c++11 -pedantic-errors` gives them, over two
translation units as well as one. Third oracle for O: the six mangled names of a
pattern's members - a function, a member function template, a static data
member, a nested class's member, and two of them over a second specialization -
agree with `g++` and with `pa22/cppgm++-ref` byte for byte. O's own run
evidence: the partial specialization's member definitions and the three-deep
member class template nest both compile through `lowir2cy86` + `cy86` and return
the 0 that `g++ -std=c++11 -pedantic-errors` gives them. Earlier run evidence:
`holder<int(char) const>::f()`
beside `holder<int(char) const &>::f()`, a pointer-to-member pattern beside a
member-function-pointer one, and 14.5.3p4's expansion over two packs all compile
through `lowir2cy86` + `cy86` and run the value `g++ -std=c++11` gives them.
Third oracle: the four mangled names of `int(char)`, `int(char...)`,
`int(char) const &` and `int(char) volatile &&`, and the three of
`int (C::*)() const &`, `int (C::*)() &&` and `int (C::* const)(char)`, agree
with `g++` and with `pa22/cppgm++-ref` byte for byte.

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
| **D** 8.3.5's function type as a template argument, and the ordering of the patterns written over it | 14.2 writes a template-argument-list inside a name, so a type-id reaches the semantic layer as text and `SpelledTypeId` is the second implementation of 8.3.5p1's parameter clause. It had learned neither 14.5.3p4 - `R(Args...)` read its `...` as 8.3.5p3's ellipsis, so `box<R(Args...)>` was a variadic function of one `Args` matching no `int(int, float)` at all - nor 8.3.5p7's trailing qualifiers, so `holder<R(A...) const &>` was a head this milestone refused to read, nor 8.3.5p5's adjustment, so `call<Fun(A0)>` deduced `A0` to a function rather than to a pointer. Four of 14.5.5's own rules came with it, each of which had left one list matching two patterns and neither more specialized: 8.3.5p7's ref-qualifier is part of a function type's identity in `Deduction::match`; a fixed place facing an entry that stands for a run takes nothing from it, which is what 14.5.5.2p1 needs when it asks the match of two *patterns*; 14.5.6.1p5's signature is built through `substitute_entry`, because `substituted` leaves an expansion standing and one pattern written twice kept each head's own pack place; and 14.8.2.5p5's non-deduced context is settled by `substitution_agrees`, the pattern read back with each place standing for what it was deduced to. Beside them: 14.3.2p1's value argument at a *dependent* place is the settled constant over that place and no longer an opaque stand-in named after its spelling - which 14.8.2.5p4 had been reading as a deducible place, so `all_true<ic<T, true>...>` matched an `ic<bool, false>` by deducing "true" from `false` - with `Deduction::match`'s own `Value` arm beside it; and `collect_packs` reaches through `S<E>`, a template-id whose template is a place, while `match_run` merges back what each element deduced for every place that is not the pack. | **264 / 329** |
| **D audit** the fact 8.3.5p7 made part of a function type's identity, and the second reading of 8.1p1's type-id | D made the ref-qualifier part of a function type's identity in `Deduction::match` and neither reader that turns such a type back into a name had ever written one: `abi_type`'s `<function-type>` wrote no `<ref-qualifier>` and `type_spelling` wrote neither it nor 8.3.5p1's cv-qualifier-seq after the clause, so `holder<int(char) const>` and `holder<int(char) const &>` were two specializations, one LowIR label and one symbol - a program defining and calling both returned 22 where both oracles return 12. Beside it 8.3.3p1's `nested-name-specifier *` was a ptr-operator `SpelledTypeId` had never had, so `int C::*` read as a type-specifier-seq spelling `int C::`; and `match_run` took one pack place where `expand_type` and `substitute_entry` each read an expansion over as many as its pattern names, so `box<pr<A, B>...>` matched no list at all. | **267 / 332** |
| **O** 14.6.1p1's current specialization of a partial specialization, and of a member class template | This build had one current instantiation per template - the primary's - so an out-of-class definition of a member of a *pattern* bound its own head to the primary's places and wrote a prefix naming a specialization nothing had read. `TemplateInfo::Partial` now gets what `info.current` already was: a class over its own head's places, read from its own body in its own region, keyed in `TemplateInfo::patterns` by the interned list the class already carries so `complete_specialization` routes one hash lookup. Which body a definition belongs to is 14.5.6.1p5's signature of the arguments it wrote; it joins that body's own `members`, and a specialization 14.5.5.1p1 read from another body holds none of them. Beside it, 14.6.1p1's injected-class-name is the *template's* name inside a body whose class-head wrote a template-id, which is what `typedef Iter<Vec, false> pointer;` inside `Vec<bool, A>` names. And 14.5.2p1's member class template: 14.6p8's reading recorded no template at all, so `outer<T>::inner` was undeclared in the current instantiation and `template<class T> template<class U> struct outer<T>::inner {…}` named nothing - the reading now records what a class-specifier declares, and 14.5.2p3's declarator-id says which head parameterises which class by how far the region has already bound, so the third head of `outer<T>::inner<U>::deeper` is `inner`'s and not `outer`'s. 14.6p8's reading and the class it is read as move to `sema_pattern.h`. | **283 / 334** |
| **O audit** which of a template's bodies a declarator-id names, at the second tier the same commit opened | O taught `record_template` to ask `Specialization::member_pattern` which body an out-of-class definition belongs to, and `PatternReading::read_declaration` - 14.5.2p3's own tier, where a *second* head's declarator-id is read against the class the head above it settled - recorded against `kNoPartial` outright. So a member of any partial specialization of a member class template defined outside its class was read against the primary member template's places and came out `f is written after a name that is not a namespace, class or enumeration`: `A<T>::in<U*>::f`, two patterns beside each other, one three heads deep and one under a partial specialization of the enclosing template are four programs `g++` and `pa22/cppgm++-ref` both accept. `nested_owner` now hands back the component it stopped walking at, exactly as `owner` already did, and the same `member_pattern` reads it - so 14.5.6.1p5's signature answers it at both tiers and `in<K*>` defines the body `in<U*>` declared. | **285 / 336** |
