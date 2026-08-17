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
  of the unit made the spelling. 14.2's `<` is settled from it. It is also where
  a tree an argument list flattened away is *kept*: `keep_spelled` holds the
  reading of a `decltype`, a `noexcept` and a parenthesized `sizeof` operand
  under the spelling it flattens to, because none of the three is a question the
  text can answer.
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
  later, against whichever of them its declarator-id named. `owner` and
  `nested_owner` are 14.5.2p3's two tiers of one question, and each is asked
  *before* the class tier that would read the same nested-name-specifier as a
  prefix it must resolve.
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
- `sema_value_expression.cpp` — `TemplateArgumentReader`, the same second
  implementation for the *other* kind of argument: 5.19's constant expression
  read out of one spelling. It owns the split into terminals - a name closes up
  with its argument lists, its `::`, 14.2p4's keyword and 5.3.3p1/5.3.6p1's
  parenthesized operand - and 5.19's own precedence walk, 5.18p1's comma inside
  5.1.1p6's parentheses, 5.2.9p4's discarded value, and 14.5.3p4's expansion
  inside 5.2.2p1's argument list. What no reading of words can answer it asks of
  the tree the parse kept: 5.3.7p3's operator and 5.3.3p1's expression operand.
  What 5.3.3 and 5.3.6 *come to* is not this reader's and not the type table's:
  `SemaAnalyzer::size_of` and `align_of` are the one answer apiece, because
  14.6p8's stand-in, p3's demand and p3's refusal are three things a lookup of a
  type's size cannot make and three readings write each operator. The demand is
  `require_settled_type` - asked of the type rather than of the mark
  `instantiate_class` leaves, so it reaches a specialization a reading that asked
  for nothing named, and so that no reader makes it by reading the same text
  twice.
- `sema_pack.cpp` — 14.5.3p4's expansion: which packs a pattern names
  (`collect_packs`, through a template-id whose template is a place), what an
  expansion comes to (`expand_type`), how a substitution reaches through one
  (`substitute_entry`), and `run_of`/`element_region`, the two primitives every
  by-element reading is built out of.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.

## Current Failure Map

The C audit stands at **318 / 348** — 278 of the 308 `tests/` fixtures plus all
40 `course/pa22` ones, the last of which it wrote. The 30 that fail group by the
compiler behaviour that owns them, from the diagnostic each one reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 8 | compiles and the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 2 of them |
| 6 | a `-bad` case wrongly accepted | mixed | none; 3 are 11p1's access, 2 are 14.5.6.1p5's shape |
| 5 | a dependent name an instantiation has to find | mixed | `no declaration of … is in scope`, `does not name a type`, `decltype names … which is not an object` |
| 4 | call resolution over a member template or a using-declaration | `sema_overload.cpp` | `has no best declaration`, `accepts the arguments of a call`, `is a deleted function`, `no function call operator` |
| 2 | 11.3p2's `friend C;` naming a *dependent* class, and 11.2's reach into one | `grant_class_friendship` | `a friend declaration with no declarator names no class` |
| 2 | 11p1's access at a template-id path | `accessible` | `the access its class gave it does not reach`, `names no class the member access can be looked up in` |
| 3 | one-offs | mixed | `abi-mangle: empty source name`, `an expression is outside the PA12 subset`, `PA20 does not instantiate` |

C's own group is gone whole: the five `sema_constant.cpp` diagnostics and the two
`sizeof`-over-a-stood-in-call failures all compile and match. What is left of the
constant-expression reader is nothing the fixtures reach, and the C audit's three
findings were nothing they reach either — the `course/pa22` fixture it added is
what pins them.

Known gaps probed and deliberately left:

- `A<sizeof x>` is 5.3.3p1's *unparenthesized* operand, the sixth exit of the
  reader C opened five: the split leaves `sizeof` a word of its own and the
  parse keeps no tree under a spelling whose end the enclosing expression is
  what says. Both oracles take it. Reaching it means the reader finding the span
  of one unary-expression before it can ask the parse for a key, which is the
  operand boundary 5.3.3 owns and not one the words carry.

- `Trait<int>::value && constraints<Traits...>::value` inside a partial
  specialization over a template-template pack is `no declaration of Trait<int>
  is in scope` here where `g++` and `pa22/cppgm++-ref` both run it. Either half
  alone is read: the recursion on its own and the bound place on its own are two
  programs this build takes. So a nested instantiation of the same template made
  *while* the outer body is being read is what loses the place - the same
  re-entrancy shape `StandingIn` has, and it belongs to the general instantiation
  path rather than to anything a value place owns.
- `pa22/cppgm++-ref` accepts an out-of-class definition of a member of a class
  nested **two** deep inside a class template and emits only a `declare
  function` for it — `_ZN5outerIcE3mid4deep1fEv` is a symbol its own LowIR never
  defines and `lowir2cy86` refuses to link — where `g++` and this build both
  write the definition and run its value. One level of nesting it defines, so
  the `course/pa22` fixture for 14.5.1.3p1's entry points pins that tier and
  stops short of the one the reference cannot materialize. Its inner-head twin
  is the same shape: a member class template whose out-of-class definition
  renames the *inner* head's places is `invalid sizeof type-id` there for a
  destructor, a constructor, a conversion function, an operator and an ordinary
  member function alike, all of which `g++` and this build run.
- 9.2p9 is enforced nowhere: `struct A { A a; };` is accepted, and so is a
  member whose type is the class an alias template in the same body names —
  `template<class U> using rebind = box<U>; rebind<int> other;` inside
  `box<T>` is `field 'other' has incomplete type` in `g++` and translates here.
  It is 9.2p1's shape one clause along, and like it belongs to the general class
  reading rather than to anything a template path owns.
- 5.19p2 at a static data member 9.4.2p2 defined outside its class: `code<char>::
  value` is folded in generated code where the unit writes no `template<>` for
  that member and read out of the object where it writes one, which is what
  `pa19/tests/general/300-class-template-static-member-out-of-class-definition`
  and `spec/300-explicit-specialization-static-data-member` pin between them and
  what `SemaEntity::member_specialized` carries into `storage_of`. The strict
  reading - 14.6.4.1p1 puts an instantiated definition's initialization at the
  end of the unit, so it precedes no use at all - is what `g++` gives and what
  the pa19 fixture refuses.
- 14.7.3p1's written definition of a static data member is still marked
  `instantiated_definition` by the pattern's own reading, so a `template<> const
  int code<int>::value = 7;` **no use names** is deferred and never written,
  where `pa22/cppgm++-ref` and `g++` both emit it. `supersede` is the function
  tier of the same rule and drops a *held* body; the object tier has to withdraw
  the definition line the pattern's reading already wrote into the dump.
  14.7.3p14 is the other half of it: an explicit specialization of a static data
  member with no initializer is a *declaration*, and `template<> int
  code<int>::value;` is read here as a definition of zero where both oracles
  leave the storage to another unit.
- `template<> int tag<int>::f();`, a declaration of an explicit specialization of
  a member function, is read here as leaving the pattern's out-of-class
  definition in place: `g++` and `pa22/cppgm++-ref` both write a declaration and
  fail to link. The reference contradicts itself one spelling along, so the shape
  is recorded rather than pinned.
- One explicit class specialization written twice - `template<> struct A<int>
  {}; template<> struct A<int> {};` - is accepted here and refused by both
  oracles; 14.7.3p6's refusal is written about a specialization the *pattern*
  was read for, which this is not. And a `template<>` over a member **class** -
  `template<> struct tag<int>::inner {…};` - is `a template declaration of inner
  redeclares a name that is not a class template` here, where `g++` accepts and
  runs the written body and the reference accepts and runs the pattern's.
- `template<> outer<int>::inner::id()`, a member of a class a class template's
  body declares, is accepted here and by `g++` and refused by
  `pa22/cppgm++-ref`; the three special-member exits of 14.7.3p1 are the other
  way round, accepted here and by `g++` and refused by the reference. So no
  `course/pa22` fixture pins either: the ref would write the refusal into the
  `.ref`.
- `pa22/cppgm++-ref` accepts a member of a partial specialization of a member
  class template defined outside its class and then emits a `declare function`
  for it: `_ZN6holderIiE4slotIPcE5widthEv` is a symbol its own LowIR leaves
  undefined. Its static-data-member twin is worse - it leaves *both* bodies'
  globals undeclared. This build writes the definitions and runs the value
  `g++` gives.
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
  both refuse and this build accepts. The fix is one hidden *type* chain read by
  the two declaration sites - `class_declaration`'s elaborated arm and
  `record_template`'s class tier - rather than anything 14.5.4p1 owns.
- 14.5.4p1's grant is recorded in the lowering dialect alone, because PA11 and
  PA12 model a class template's class inside the head's own region and a friend
  declaration's class in the namespace, so the two can never be one entity
  there. The PA11/PA12 dumps themselves agree byte for byte.
- 9.2p1 is enforced nowhere: `struct A { int f(); int f(); };` is accepted, and
  so are two equivalent member-template declarations. `g++ -pedantic-errors`
  refuses all three.
- A conversion function template is keyed by the *spelling* of its
  conversion-type-id, so `operator U()` and `operator V()` over one head are two
  members and an out-of-class definition that renames its place matches none.
  `pa22/cppgm++-ref` refuses that shape outright, so no fixture pins it.
  14.8.2.3 at the *named* exit rests on the same fact: `a.operator int()`
  reaches no declaration here or in `pa22/cppgm++-ref`.
- `pa22/cppgm++-ref` cannot read a member *class* template whose out-of-class
  definition renames the enclosing head's places; `g++` accepts all four
  spellings and this build runs the value it gives, so the `course/pa22` fixture
  for 14.5.1.3p1's rename pins the three tiers the reference agrees on.
- An **empty** out-of-class destructor of a class template is elided by 12.4p8
  where `pa22/cppgm++-ref` and `g++` both write the definition.
- `template<class U> friend class W;` inside a class that is itself a *private*
  nested class, and then `W<T>` naming that class: we refuse where `g++` accepts
  and the non-template spelling of the same program is refused by `g++` too.
- 8.3.4p1's array bound is an `unsigned long long` on the type, and a bound the
  reading has not settled stands in as **1** - so `template<class T, unsigned
  long N> struct r<T[N]>` writes the pattern `T[1]` and `read_pattern` refuses
  the head outright. Deducing `N` needs a bound that is a `TypeId`, which the
  ABI's `ABI_ARRAY_BOUND_RAW` and every reader of `types_.bound` would move
  with it. `spec/100-class-partial-specialization-array-size.t` is the fixture.
- 14.3.2p5's conversion of a value argument at a *dependent* place is made
  where the argument is read and not where the place settles, so the entry
  `value_type(place, bits)` holds carries the source constant's bits. What is
  left is the pattern side: `probe<ic<T, 300> >` does not take `ic<char, 44>`,
  which is the answer `g++` gives too.
- 14.3.2p2's narrowing conversion at a value place is refused by neither this
  build nor `pa22/cppgm++-ref`, and `g++ -pedantic-errors` refuses both. 8.3.5p6
  is the same shape - `h<int(char)(long)>` and `h<int(char)[3]>` are function
  types this reading and the reference both build.
- A non-type argument of *pointer* type - `template<class T, T* p>` named
  `&g`, and `template<int* p>` too - is refused before the place is ever asked
  about: `TemplateArgumentReader` has no address arm. Both oracles accept.
- A rooted nested-name-specifier written directly after a type-specifier is not
  recoverable from the spelling PA10 flattened: `int ::C::*` arrives as
  `int::C::*`, one word. The reference reads it; every other spelling of the
  form is read here.
- One expansion over *two* function parameter packs - `template<class... A,
  class... B> int f(pr<A, B>...)` - deduces nothing here and nothing in
  `pa22/cppgm++-ref` either, where the class-tier spelling of the same rule now
  deduces in both. `g++` takes it.
- `pa22/cppgm++-ref` answers `split<box<> >` and
  `split<box<pr<int, char>, pr<long, short> > >` from the primary where the
  pattern `box<pr<A, B>...>` takes both here and in `g++`; a run of *one* it
  reads. So the course fixture for 14.5.3p4's two packs pins the run of one.
- `pa22/cppgm++-ref` reads three declarations of one pattern over a pack as three
  partial specializations and calls the naming ambiguous. `g++ -pedantic-errors`
  accepts it and so does this build.
- Three shapes of C's own surface where `pa22/cppgm++-ref` parts from `g++` and
  from this build, so no `course/pa22` fixture pins them: a `sizeof` of the
  specialization whose own body writes it - `template<int N> struct A { char
  pad[N]; static const int v = sizeof(A<1>); };` - is accepted there and refused
  here and by `g++`; `sum(1 ...)`, an expansion whose pattern names no parameter
  pack, is accepted there and refused by both; and `sum(0, Ns...)` over a run of
  none is refused there and taken by both.

## Active Checkpoint

**C and its audit landed — see the ledger.** The next one is **B**, the six
`-bad` cases this build accepts and both oracles refuse. Three of them are 11p1's
access asked at a path the *arguments* built - `spec/300-private-leading-
template-id-path-rejected`, `spec/300-private-base-inherited-member-template-
argument-rejected` and `spec/200-private-intermediate-alias-member-rejected` -
and they share their owner with the two access failures in the compile-pass
column, `basic_string is named where the access its class gave it does not
reach` and `Matcher names no class the member access can be looked up in`. So
the owner is `accessible` with 11.2p5's naming class, the data flow is one
template-id path to one naming class per component, and the other three - 9.2p1's
redeclaration in `spec/200-template-template-redeclaration-shape-bad` and
`spec/300-explicit-specialized-ctor-template-header-bad`, and 5.19p2's
non-static member in `200-bad-dependent-nonstatic-member-constant` - are two
clauses no reading enforces at all, each one refusal written at a door the
readings already pass through.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22*`,
against `pa22/cppgm++-ref` and, where a row says so, against the turn-start
build in a worktree of the checkpoint before the row's own (`32ba715b` for C's).
A turn-start build that **refuses** the generated input times a refusal and not
the work, so such a row says so rather than carrying the number. Eleven traps are
recorded rather than re-measured: `timeout`/`date` spawned per run invents a
~0.1 s floor that reads as 33 s over the corpus, `cppgm++` run by hand needs
`-o` or it compiles nothing, a relative binary path measures 0.00 s from a shell
whose directory moved, the whole corpus handed to one process is one ill-formed
unit and times as 0.00 s, `/usr/bin/time` writes to stderr so a child whose
stderr is discarded loses every measurement, `g++ file.t` treats a `.t` as a
*linker input* and `-x c++` written *after* the input file has no effect, `bc` is
absent so a best-of-three written around it silently keeps the first run,
`date +%s.%N` interpolated into `python3 -c` loses the second reading, `make
ref-test` regenerates only `tests/` - a course fixture needs `TEST='course/pa22/
x.t'` spelled **relative** - a git worktree cannot be added under
`/home/vishvananda/work`, which is read-only, and the *first* pass over a corpus
measures the page cache and not the compiler: 2.62 s cold against 1.53 s warm.
Every generated input is checked for exit 0 before it is timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| n partial specializations of one template over a function type, each named | 100 → 800 | 0.01 → 0.23 s, 10 → 38 MB | 10.62 s at 800 |
| n namings of a pattern that writes 14.8.2.5p5's non-deduced context | 100 → 800 | 0.01 → 0.12 s, 10 → 39 MB | 0.91 s at 800 |
| n pointer-to-member type-ids written as template arguments | 100 → 800 | 0.01 → 0.09 s, 9 → 32 MB | 0.82 s at 800 |
| a function type of n parameters written as a template argument | 100 → 800 | 0.00 s, 6 → 7 MB | 0.54 s at 800 |
| one pack pattern matched against a run of n | 800 → 3200 | 0.00 → 0.01 s, 7 → 8 MB | 0.64 s at 3200 |
| *two* packs expanded together over a run of n | 800 → 3200 | 0.01 → 0.03 s, 8 → 13 MB | 1.21 s at 3200 |
| a function type reached through d nested member typedefs | depth 4 → 256 | 0.00 → 0.02 s, 6 → 13 MB | 0.56 s at 256 |
| **O** n patterns of one template, each with an out-of-class member definition, each named | 100 → 800 | 0.02 → 0.36 s, 13 → 59 MB | 12.13 s at 800 |
| **O** a member class template nested d deep, each level defined out of class | depth 4 → 40 | 0.00 → 0.09 s, 6 → 16 MB | 17.78 s and **7.99 GB** at 24; killed at 32 |
| **O audit** n specializations of one pattern that has 8 out-of-class members | 100 → 800 | 0.03 → 0.26 s, 13 → 64 MB | — |
| **X** n class specializations, each with one explicitly specialized member | 50 → 400 | 0.00 → 0.04 s, 8 → 18 MB | 0.68 s at 400 |
| **X audit** n `template<>` static members of one template, over 8 specializations | 100 → 800 | 0.04 → 0.34 s, 14 → 69 MB | 0.75 → **21.27 s** |
| **X audit** n `template<>` static members × n specializations, n² uses | 20 → 80 | 0.02 → 0.29 s, 10 → 60 MB | 0.62 → 1.92 s |
| **R** n out-of-class member function templates of one class template, each renaming the owner's places | 100 → 800 | 0.02 → 0.19 s, 12 → 50 MB | 1.04 → **28.48 s** and 3.66 GB |
| **R** a member class template nested d deep, every head renaming the places above it | depth 4 → 40 | 0.00 → 0.03 s, 7 → 11 MB | 23.81 s and **8.53 GB** at 24; killed at 40 |
| **R audit** n out-of-class members of one nested class, each head renaming | 100 → 800 | 0.01 → 0.11 s, 10 → 32 MB | 0.88 → **22.25 s**; the turn-start build **refuses** |
| **R audit** a class nested d deep in a class template, one out-of-class member | depth 4 → 64 | 0.00 s, 7 → 8 MB | 0.53 → 0.54 s; the turn-start build **refuses** |
| **C** n operands written in one call as a template argument | 100 → 800 | 0.00 s, 7 → 9 MB | 1.13 s at 800 |
| **C** one pattern expanded into a call's arguments over a run of n | 400 → 3200 | 0.00 → 0.03 s, 9 → 22 MB | 12.46 s at 3200 |
| **C** calls nested d deep inside one template argument | depth 4 → 128 | 0.00 → 0.01 s, 7 MB | **killed at 120 s** at depth 24 |
| **C** n `sizeof` type-ids in one argument spelling, each a specialization | 100 → 800 | 0.00 → 0.01 s, 6 → 7 MB | 1.73 s at 800; the turn-start build **refuses** |
| **C audit** n `alignof` type-ids in one argument spelling, each a specialization | 100 → 800 | 0.00 → 0.01 s, 6 → 7 MB | 0.70 s at 800 |
| **C audit** a `sizeof` of a specialization nested d deep in its own operand | depth 8 → 128 | 0.00 → 0.01 s, 6 → 7 MB | 0.60 s flat; the turn-start build is **2^d** — 15.42 s at 20, killed at 60 s at 24 |
| **C audit** n class templates, each named once under `alignof` at a declarator | 100 → 800 | 0.02 → 0.16 s, 9 → 29 MB | 1.21 → 1.44 s; the turn-start build is 0.08 s and **answers 1** |
| **C** n member class templates of one class template, each defined out of class, against the turn-start build | 50 → 400 | 0.03 → 0.25 s, 9 → 28 MB | 0.03 → 0.24 s there; ref 1.73 s |
| **C** n nested classes of member class templates, each defined out of class | 50 → 400 | 0.06 → 0.49 s, 11 → 40 MB | 2.03 s at 400; the turn-start build **refuses** |
| the whole 348-file corpus, one process per file | — | **1.65 s** | 1.61 s at the turn-start build; the loop's own floor is 1.36 s |
| *(carried from F)* n friend declarations of one name, each revealed | 800 → 3200 | 0.19 → 0.79 s | 22.40 s at 800 |

C's four costs are each paid once and nothing scans. `operand_end` is one
forward scan of the words the reading below it is about to read anyway, made
before the operand rather than after it because 14.5.3p4's `...` stands *after*
the pattern and a pattern that is a pack's own name runs out a word before the
reading reaches the ellipsis that says how to read it - a list nested d deep
scans its own contents once per level, which is 0.01 s and 7 MB at depth 128
where `pa22/cppgm++-ref` is killed at 120 s at depth 24. `expand_operand` is one
reading of the pattern per element, over the words already split, so a run of
3200 is 0.03 s against the reference's 12.46 s. 14.7.1p1's demand under
`sizeof` and `alignof` is one call of `require_settled_type` on the type the
probe already has in hand, made inside `size_of` and `align_of` so that every
reading of either operator makes it: the second `template_argument_type` the
checkpoint wrote in its place was a *reading* per level doubled at every level
below it - 15.42 s at depth 20 and killed at 60 s at 24, where the reference is
0.60 s flat and this build is 0.01 s at depth 128. What the demand costs is the
work it was owed: 800 class templates each named once under `alignof` at a
declarator is 0.16 s and 29 MB against the reference's 1.44 s, where the
turn-start build was 0.08 s because it laid none of them out and answered 1. And
asking
`nested_owner` before the class tier costs one components walk per second-head
declaration, which the tier below made anyway: n = 400 member class templates
defined out of class is 0.25 s against the turn-start build's 0.24 s.

The corpus row is re-measured after the C audit over 348 files: 3.01 s of wall
clock against the turn-start build's 2.97 s, over a 1.36 s floor the loop's own
fork-and-exec costs - so 1.65 s of compiler work against 1.61 s, about 5 ms per
fixture, and the 0.04 s is the classes a `sizeof` or an `alignof` now asks to be
laid out.

Every dimension is linear in what it sweeps and flat in depth except 14.5.5.1p1's
own: n patterns beside one template and n lists naming it is n candidate scans of
n, which is what the program wrote. `perf` on the n = 800 case puts 28 % of the
run in `Deduction::match` and `match_arguments` and **0.08 %** in
`SemaAnalyzer::substituted`. It is memoized by `TemplateInfo::chosen`, which keys
the whole choice by the interned argument list. The reference is exponential in
member-class-template nesting - 0.53 s at depth 8, 17.78 s and 7.99 GB at 24, the
OOM killer at 32 - where this build is 0.02 s and 9 MB at 24; reading each level
once against the class the level above declared is what keeps it flat. Earlier
checkpoints' per-element costs are unchanged: `match_run` copies a map of one or
two entries however long the run is, `SpelledTypeId::suffix` reads `expand_type`
once per written parameter, `enclosed_by_a_head` is one walk per queued body,
`TemplateInfo::reading_region` and `Member::carried` are pointers written where
a definition is recorded, and `instantiating_pattern_` is one unsigned compare.

`valgrind -q --error-exitcode=9` is clean over 69 inputs after the C audit, 0
errors: its 65 sibling-exit probes and its four largest scaling inputs. It was
clean over 58 after C, 62 after the R audit, 34 after R, 37 after X, 57 after the
O audit.

The C audit's run evidence: all 65 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref`, and agree with `g++` on every
one - 13 discarded-value shapes and 5 comma shapes, 12 `alignof` shapes over a
specialization, a reference type-id, an array bound, a static_assert and a class
template's own body, 10 expansion shapes including two packs together, an
expansion nested in a call inside another, a template-id pattern and a run of
none, 6 `::template` shapes, and 6 `sizeof`-off-the-kept-tree shapes including
one spelling written over two different overload sets in two namespaces. Every
probe that translates runs through `lowir2cy86` + `cy86` and returns the value
`g++` gives it, and the `course/pa22` fixture the audit added does too. Three of
the 65 are shapes `pa22/cppgm++-ref` answers differently from `g++` and from this
build, recorded above.

C's own run evidence: all 52 probes are judged against `g++ -std=c++11
-pedantic-errors` and agree with it on every one - 6 comma and void-cast shapes
and one refusal, 7 expansion-in-a-call shapes and one refusal, 13 `sizeof`
shapes including a reference type-id, a class with a reference member, two
overloads told apart by the argument, and a specialization nothing else in the
unit completes, 8 `::template` and splitter shapes, and 5 out-of-class
member-class-template shapes. The four `course/pa22` fixtures C wrote and the
seven fixtures it turned green all compile through `lowir2cy86` + `cy86` and
return the value `g++` gives them, over two translation units as well as one,
where the two `mark` symbols the two units owe are owed once each and weak.
Third oracle for C: those two mangled names -
`_ZNK7adaptorIiE5rangeILi7EE8iterator4markEv` and its `Li14E` twin - agree with
`g++` and with `pa22/cppgm++-ref` byte for byte, and the reference accepts all
four course fixtures. Earlier run evidence stands: the twenty-one out-of-class
shapes the R audit turned green, R's member class template nest and member
alias template shapes, X's twenty-one `template<>` shapes, O's twenty
out-of-class shapes and their six mangled names, and D's function-type and
pointer-to-member manglings, all agreeing with `g++` and, where it writes one,
with `pa22/cppgm++-ref`.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|------------|-------------|-----------|
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration*. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. | 142 / 308 |
| **T2–T5, T audit** the place's own default, its object-file name, 14.1p11 and the region an argument associates | 14.1p2's default at a template place is an id-expression naming a template; `TypeKind::TemplateName` gained an `operand_of` arm, so two templates no longer intern as one type; 14.1p11 is written about a head an argument list is read against, so a pack stands anywhere in a partial specialization's; 3.4.2p2 gives an argument the region that declares the template it named; and 14.3.3p1 was asked at the class tier and at neither exit of the function tier. | 156 / 308 |
| **A, A audit** 14.5.7p1's alias template, and the three regions a template-id is looked up in | `template<…> using X = T;` records a head and a *pattern that is a type-id*, so 7.1.3p2 makes `X<A…>` another name for the type the arguments substitute into it; the declaration is a `Typedef` carrying a `TemplateInfo`. `resolve` answered a template-id at both its exits where 5.2.5p1's member lookup answered it at neither, and `QualifiedName::prefix` is read off the split rather than by `last().size()`. | 193 / 308 |
| **P, P audit** the three places a template-argument-list is read, and the two forms 14.7.2 writes one requirement in | 14.2p4 makes the keyword optional wherever the object expression is not type-dependent, so `h.get<int>(4)` is a template-id the parse has to recognise with no keyword to lean on: `DeclaredNames::names_a_template` answers it from the unit-wide record, and 5.2.2p1 bounds the guess to a list a `(` follows. `explicit_instantiation` returned on `!owed` before reading its target, so p2 was asked of nothing `extern template` wrote. | 200 / 308 |
| **M, M2, M audit** 14.5.2's member template, its four definition exits, and the two facts 12 writes about a special member | A head over a constructor or a conversion function inside a class body declares a member *template* of that class, which reached neither `function_definition` nor `declare_function`; 3.4.1p8 puts the head *inside* the region its declarator-id names; `specialize` copied `object_member` and `access` but not which special member it declares, so 12.6.2's mem-initializers never ran; and 12.3.2p1's conversion function template reached *no* use at all - `Deduction::from_conversion` is 14.8.2.3's one P/A pair. | 229 / 318 |
| **F, F audit** 14.5.4's friend templates, and the two readings one friend definition gets | `SemaModel::befriended` asks the pair as the use spelled it and then with each side replaced by its `primary`, which is what 14.5.4p1 means. A friend declaration written in a class *template* is read twice and 11.3p6 puts both in one namespace, so the pattern reading's `defined` made one instantiation `unwrap is defined twice`; and `record_function_template` recorded the *namespace* as the region 14.7.1p1 reads the pattern against where 3.4.1p10 reads a friend definition where it was written. `Scope::hidden_index` keys a hidden chain by the declaration it was made with: 15.64 s to 0.79 s at n = 3200. | 249 / 326 |
| **D, D audit** 8.3.5's function type as a template argument, and the fact 8.3.5p7 made part of its identity | `SpelledTypeId` had learned neither 14.5.3p4 nor 8.3.5p7's trailing qualifiers nor 8.3.5p5's adjustment; four of 14.5.5's own rules came with it, each of which had left one list matching two patterns and neither more specialized. Then neither reader that turns such a type back into a name had ever written a ref-qualifier, so `holder<int(char) const>` and `holder<int(char) const &>` were one symbol; and 8.3.3p1's `nested-name-specifier *` was a ptr-operator `SpelledTypeId` had never had. | 267 / 332 |
| **O, O audit** 14.6.1p1's current specialization of a partial specialization, and which of a template's bodies a declarator-id names | This build had one current instantiation per template - the primary's - so an out-of-class definition of a member of a *pattern* bound its own head to the primary's places. `TemplateInfo::Partial` now gets what `info.current` already was, keyed in `TemplateInfo::patterns` by the interned list. 14.5.2p1's member class template: 14.6p8's reading recorded no template at all. Then `read_declaration` - 14.5.2p3's own tier - recorded against `kNoPartial` outright, so a member of any partial specialization of a member class template was read against the primary's places. | 285 / 336 |
| **X, X audit** 14.7.3p1's explicit specialization of a member, and the question 5.19p2 asks with the same words | A `template<>` definition of one member of one class specialization *is* that member's definition, so 14.7.1p1's reading of the pattern shall not write a second one: `SemaEntity::instantiated_definition` is written for a function too, under an `instantiating_pattern_` depth, and `Pending::from_pattern` lets the end of the unit drop the queued body. Then `note_object` answered two questions with one field - "a use reads the object" and 5.19p2's own answer about the declaration - so a `template<>` written for one list made every *other* list's member no constant expression at all. | 295 / 338 |
| **R, R audit** 14.1p2's names a definition written outside its class wrote, and the region its head bound | An out-of-class member definition stands under one head per class it is nested in, and 14.1p2 lets each spell the enclosing classes' places with names of its own - which this build bound nowhere 14.7.1p1 could reach. `TemplateInfo::reading_region` is the region the head above opened, taken before `StandingIn` moves the nest, and `Member::carried` is it one tier down. Then R bound those names where the *declaration* is made and 14.7.1p1 leaves the *body* to the use, so `enclosed_by_a_head` asks it once at the one door every queued body passes; and 14.5.3p4's count may not be asked at 14.6p8's reading, where the expansion stands for itself. | 306 / 343 |
| **C, C audit** 5.19 read out of one spelling, at the five operators the reader had no answer for, and the two sentences the last of them is written about | 14.2 writes an argument list inside a name, so `TemplateArgumentReader` is the second implementation of 5.19 exactly as `SpelledTypeId` is of 8.1p1 - and four of the clause's own operators had no exit there. 5.18p1's comma is read inside 5.1.1p6's parentheses alone, because outside them a comma separates one argument from the next; 5.2.9p4's cast to cv void is a *discarded* value, which is what `valued` refuses at every reader that takes an operand's worth and what makes `((void)B, true)` read as `true`; 14.5.3p4's expansion stands in 5.2.2p1's argument list, and whether an operand is a pattern is settled by `operand_end` *before* it is read, because the `...` stands after it and `sum(Ns...)` runs out on `Ns` a word early; and 14.2p4's keyword is written inside a component, so `X::template f<A>::v` is one word the split closes up rather than two. 5.3.3p1's other arm is the fifth: how large the type an *expression* has is, is 13.3's answer over a typed operand, so the parenthesized operand closes up with its operator in the split and the tree the parse kept under that spelling is what answers it - beside 5.3.3p2 and 5.3.6p3's reference, which `measured_type` now owns for the three readers that write the operator. Two sweeps came with it: 14.7.1p1's demand is made outside the probe that settles 5.4p2's ambiguity, so `A<sizeof(box<4>)>` lays `box<4>` out; and 14.5.2p3's `nested_owner` is asked *before* the class tier, which reads the same nested-name-specifier as a prefix it must resolve - so `adaptor<T>::range<M>::iterator` is a class nested in a member class template rather than `M is written as a template argument and names no constant`. Then that last operator's own clauses were answered out of a table: `TypeTable::object_align` gives an incomplete class an alignment of zero and a dependent one a number too, and two of the three readings that write `alignof` called it bare - so `S<alignof(wrap<int>)>` was **1 where both oracles give 8**, at a template argument, in an array bound and in a static_assert alike, and `alignof(never)` was a program both oracles refuse and this build ran. `SemaAnalyzer::align_of` is `size_of`'s twin and all three ask it. Under it, 14.7.1p1's demand reads a mark `instantiate_class` writes only where the naming was a use, and neither 14.6p8's reading nor `trait_value`'s own probe leaves one - so `sizeof(box<4>)` inside any class template's body was `sizeof names an incomplete type`; the demand is now `require_settled_type`, asked of the type rather than of the mark, once inside `size_of` and `align_of`. And the demand the checkpoint did make, it made by reading the operand's type-id a *second* time, which is one reading per level doubled at every level below it: a `sizeof` nested 24 deep in its own operand was killed at 60 s where the reference is 0.60 s flat and this build is now 0.01 s at depth 128. | **318 / 348** |
