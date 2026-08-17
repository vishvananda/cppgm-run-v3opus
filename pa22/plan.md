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
  which of those bodies an out-of-class member definition was written over, and
  14.7.3p1's *other* question lives here too: which of the two definitions of a
  member of a class specialization this unit holds - the pattern's, read again,
  or the one the program wrote out for exactly those arguments.
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

X landed **295 / 338** — the 293 of 336 it left, plus the 2 `course/pa22`
fixtures it wrote — and its audit holds the same 295 while ending a refusal no
fixture reaches. The 43 that fail group by the compiler behaviour that owns
them, from the diagnostic each one now reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 12 | compiles but the exit status or the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 3 of them and 6 more are `-bad` cases wrongly accepted |
| 6 | a constant expression written inside a template argument | `sema_constant.cpp` | `names a type that is not integral`, `does not close its arguments`, `names no constant` |
| 4 | dependent names an instantiation has to find | mixed | `no declaration of … is in scope` |
| 3 | 11.3p2's `friend C;` naming a *dependent* class, and 11.2's reach into one | `grant_class_friendship` | `a friend declaration with no declarator names no class` |
| 3 | `sizeof` over a stood-in call written as a template argument | `sema_constant.cpp` | `is written inside sizeof as a template argument and names no type` |
| 2 | a default template argument the arity check counts | `TemplateHead` | `a template-argument-list gives X more arguments than it has parameters` |
| 2 | an out-of-class member template whose owner renamed its places | `StandingIn` | `X does not name a type` |
| 2 | a member alias template rebound through its owner | `Specialization::alias` | `rebind_alloc<T> does not name a type` |
| 9 | call resolution, access, a dependent member typedef and lowering one-offs | mixed | various |

The whole `X is defined twice` group and the `a static_assert condition is false`
one are gone: they were 14.7.3p1's, and the two `StandingIn` renames plus
`spec/300-explicit-instantiation-out-of-class-nested-member` are what is left of
the out-of-class shapes, each stopping somewhere else now.

Known gaps probed and deliberately left:

- 5.19p2 at a static data member 9.4.2p2 defined outside its class: `code<char>::
  value` is folded in generated code where the unit writes no `template<>` for
  that member and read out of the object where it writes one, which is what
  `pa19/tests/general/300-class-template-static-member-out-of-class-definition`
  and `spec/300-explicit-specialization-static-data-member` pin between them and
  what `SemaEntity::member_specialized` carries into `storage_of`. It is the
  *lowering's* question alone: 5.19p2 still reads the value the pattern's
  initializer gave the specialization, so `int arr[code<char>::value];` folds
  here as it does in both oracles. The strict reading - 14.6.4.1p1 puts an
  instantiated definition's initialization at the end of the unit, so it precedes
  no use at all - is what `g++` gives and what the pa19 fixture refuses; the ref
  folds a use written *above* the `template<>` too only where the use is a
  namespace-scope initializer, which nothing pins.
- 14.7.3p1's written definition of a static data member is still marked
  `instantiated_definition` by the pattern's own reading, so a `template<> const
  int code<int>::value = 7;` **no use names** is deferred and never written,
  where `pa22/cppgm++-ref` and `g++` both emit it. `supersede` is the function
  tier of the same rule and drops a *held* body; the object tier has to withdraw
  the definition line the pattern's reading already wrote into the dump, because
  clearing the mark alone lets both lines through and `emitted_globals_` keeps
  the pattern's image (`= 3` where the program wrote 7). 14.7.3p14 is the other
  half of it: an explicit specialization of a static data member with no
  initializer is a *declaration*, and `template<> int code<int>::value;` is read
  here as a definition of zero where both oracles leave the storage to another
  unit.
- `template<> int tag<int>::f();`, a declaration of an explicit specialization of
  a member function, is read here as leaving the pattern's out-of-class
  definition in place: `g++` and `pa22/cppgm++-ref` both write a declaration and
  fail to link. The reference contradicts itself one spelling along - where the
  pattern's body is written *in* the class it defines the member, which `g++`
  still refuses - so the shape is recorded rather than pinned.
- One explicit class specialization written twice - `template<> struct A<int>
  {}; template<> struct A<int> {};` - is accepted here and refused by both
  oracles; 14.7.3p6's new refusal is written about a specialization the *pattern*
  was read for, which this is not. And a `template<>` over a member **class** -
  `template<> struct tag<int>::inner {…};` - is `a template declaration of inner
  redeclares a name that is not a class template` here, where `g++` accepts and
  runs the written body and the reference accepts and runs the pattern's.
- `template<> outer<int>::inner::id()`, a member of a class a class template's
  body declares, is accepted here and by `g++` and refused by
  `pa22/cppgm++-ref`; the three special-member exits of 14.7.3p1 -
  `template<> tag<int>::~tag()`, `template<> tag<int>::tag()` and
  `template<> tag<int>::operator int()` - are the other way round, accepted here
  and by `g++` and refused by the reference. So no `course/pa22` fixture pins
  either: the ref would write the refusal into the `.ref`.

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

**X landed with its audit — see the ledger.** The next one is **R**, the two
out-of-class member templates whose owner renamed its places:
`spec/300-out-of-class-member-template-owner-param-rename` and
`…-renamed-owner-parameters` stop at `Tp does not name a type` and `X does not
name a type`, which is 14.1p2 read at the wrong tier - `StandingIn` moves the
head a qualified declarator-id stands under inside the region that name reaches,
and the names *this* head wrote are what the arguments have to be bound to, not
the ones the class's own head spelled. `general/400-member-alias-template-owner-
rebind-cache` and `…/400-alias-rebind-partial-specialization-shadow` are the
alias tier of the same question.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22e`,
`/tmp/perf22f`, `/tmp/perf22g`, `/tmp/pa22audit` and `/tmp/perf22x`, against
`pa22/cppgm++-ref` and, where a row says so, against the turn-start build in a
worktree of the checkpoint before the row's own (`eac77160` for the O rows,
`08d582ad` for the X audit's). A turn-start build that **refuses** the generated
input times a refusal and not the work, so such a row says so rather than
carrying the number. The rows a checkpoint carried forward are that checkpoint's own
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
| **X** n members of one template, each explicitly specialized for one list | 100 → 800 | 0.00 → 0.04 s, 8 → 17 MB | 0.74 s at 800 |
| **X** n specializations read from the pattern, then one `template<>` member | 100 → 800 | 0.01 → 0.09 s, 9 → 30 MB | refuses |
| **X** n class specializations, each with one explicitly specialized member | 50 → 400 | 0.00 → 0.04 s, 8 → 18 MB | 0.68 s at 400 |
| **X** n out-of-class member definitions of one template, against the turn-start build | 100 → 800 | 0.00 → 0.04 s, 8 → 17 MB | 0.00 → 0.03 s there; ref 0.76 s at 800 |
| **X** a member class template nested d deep, each level defined out of class, against the turn-start build | depth 4 → 40 | 0.00 → 0.59 s, 7 → 34 MB | 0.00 → 0.60 s there |
| **X audit** n specializations of one template that has one `template<>` static member | 100 → 800 | 0.01 → 0.10 s, 10 → 30 MB | 0.57 → 0.90 s |
| **X audit** n `template<>` static members of one template, over 8 specializations | 100 → 800 | 0.04 → 0.34 s, 14 → 69 MB | 0.75 → **21.27 s** |
| **X audit** n `template<>` static members × n specializations, n² uses | 20 → 80 | 0.02 → 0.29 s, 10 → 60 MB | 0.62 → 1.92 s |
| **X audit** n uses of a member a `template<>` elsewhere superseded | 100 → 800 | 0.00 → 0.02 s, 7 → 10 MB | 0.54 → 0.65 s |
| **X audit** n class templates, each with a `template<>` destructor and one object | 100 → 800 | 0.01 → 0.14 s, 10 → 41 MB | 0.54 → 0.62 s; the turn-start build **refuses** all three |
| the whole 338-file corpus, one process per file | — | **1.43 s** | — |
| *(carried from F)* n friend declarations of one name, each revealed | 800 → 3200 | 0.19 → 0.79 s | 22.40 s at 800 |

The corpus row is re-measured after X: three runs give 1.43–1.45 s over the 338
files, unchanged from the 1.43 s O left over 336. 1.13 s of it is the process
floor - 3.4 ms per run, measured by running `int main(){return 0;}` 336 times -
so the compiler's own work is 0.9 ms per fixture.

X's four costs are each paid once per declaration and nothing scans.
`instantiating_pattern_` is one unsigned increment per class instantiation and
per out-of-class member reading, so `declare_function` and
`open_special_member_body` answer "is this definition one no unit wrote" with a
comparison against zero. `Pending::from_pattern` is one flag test per definition
written at the end of the unit, which is what lets a superseded body be dropped
without walking the queue; the entry still reachable by name is erased from
`held_definitions_` by `SemaEntity::id`. `Specialization::note_object`'s walk is
over `TemplateInfo::specializations` and runs once per `template<>` definition of
a static data member - a template no such declaration was written for pays one
hash probe of `explicit_members` per instantiated definition, which is the
100 → 800 row's 0.01 → 0.09 s. And `PatternReading::owner`'s new `settled` call
is the `template_id_entity` lookup `nested_owner` already paid, once per
out-of-class definition, into a class `instantiate_class` memoized: n = 800
definitions is 0.04 s against the turn-start build's 0.03 s and the reference's
0.76 s, and the d-deep nest is 0.59 s at depth 40 against the turn-start build's
0.60 s - the same superlinear d² component walk over d definitions the nest
already was, unchanged by the checkpoint.

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

`valgrind -q --error-exitcode=9` is clean over 37 inputs after X, 0 errors: its
21 sibling-exit probes, the nine fixtures it turned green, its five largest
scaling inputs and the two `course/pa22` fixtures it added; it was clean over 57
after the O audit, over 107 before it, and over 87 before O. X's own run
evidence: the nine fixtures it turned green and 20 of the 21 probes compile
through `lowir2cy86` + `cy86` and return the value
`g++ -std=c++11 -pedantic-errors` gives them - a destructor, a constructor, a
conversion function, two members of one specialization, a member of a partial
specialization, a member the pattern only declared, a static data member beside a
partial specialization's, a nested class's member, two class specializations each
with an out-of-class member template, a `template<>` declaration then its
definition, an explicit instantiation over an explicit member specialization, and
a member of a class the program wrote out with `template<>`. The 21st is 14.7.3p6's
own: an explicit specialization written after a call already instantiated the
member, which `g++` refuses to compile at all and both this build and
`pa22/cppgm++-ref` accept with no diagnostic required. Third oracle for X: the
same 21 probes run through `pa22/cppgm++-ref`, which agrees on 19 - it refuses
`template<> outer<int>::inner::id()`, a nested class's member, where `g++` and
this build take it. The audit's own run evidence: the twenty shapes - a static data member, a constructor, a
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
| **X** 14.7.3p1's explicit specialization of a member, and the definition it replaces | A `template<>` definition of one member of one class specialization *is* that member's definition, so 14.7.1p1's reading of the pattern shall not write a second one - `X is defined twice` was this build writing both. `explicit_functions` is keyed by the template and the argument list, which a member of a class specialization has neither of: the declaration is the class's member and which of its two definitions the unit holds is a fact of *that declaration*. So `SemaEntity::instantiated_definition` - already 9.4.2p2's for an object - is written for a function too, under a new `instantiating_pattern_` depth that tells 14.7.1p1's reading of a pattern from 14.7.3p1's own class body, which `complete_specialization` reads under one `instantiating_class_`; a written definition over a read one calls `Specialization::supersede` instead of throwing, and `Pending::from_pattern` lets the end of the unit drop the queued body whose mark is gone. 12's entry points are the same three exits - `require_replaceable` is the one rule read at the constructor's, the destructor's and 12.3.2p1's conversion function's, where `pa22/cppgm++-ref` refuses all three and `g++` accepts. Beside them: 14.7.3p1's *declaration*, a `template<>` over a simple-declaration, which says the list is not the pattern's to be read for and is what `has_written_definition` asks; `read_template_head` no longer lets an empty head parameterise what stands under it, so `template<> int tag<int>::id()` is 13.1's plain redeclaration of the class's own member and not 14.5.6.1p5's second template; `PatternReading::owner` gains `nested_owner`'s own test, so `box<void>::apply` is a member template of a class the program wrote out; `supersede` clears a member template's pattern, which is what makes an explicit definition replace the one the class pattern gave it; 14.7.3p6 refuses an explicit class specialization written after the pattern was read for that list; and `Specialization::note_object` is 5.19p2 at a static data member - the pattern's initializer is a value a use reads only while it is the one definition the unit has. | **295 / 338** |
| **X audit** which definition of one member this unit holds, and the question 5.19p2 asks with the same words | X answered two questions with one field: `note_object` said "a use of this member reads the object rather than the value" by clearing `SemaEntity::constant`, which is 5.19p2's own answer about the declaration - so a `template<>` written for one argument list made every *other* list's member no constant expression at all, and `int arr[code<char>::value];` and `box<code<char>::value>` were two programs the reference, `g++` and the pre-X build all accept and this one refused. The two part company at one site, `storage_of`, where 9.4.2p3's value is written beside the place a use reads - so the lowering's question is `SemaEntity::member_specialized` and 5.19p2 keeps the value it folded. Beside it, 12's three entry points queue their bodies through `open_special_member_body`, which set no `Pending::from_pattern`, so a body 10.3p10's table demanded before the `template<>` was read is one `supersede` can no longer reach. | **295 / 338** |
