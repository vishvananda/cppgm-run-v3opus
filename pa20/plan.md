# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **127 / 172** - 119 of the 164 checked-in fixtures and the 8
under `cppgm.tests/course/pa20` - from a turn-start baseline of **123 / 169**,
with pa1-pa19 at **2169 / 2169** and the file audit passing with the five
header-weight warnings it inherited.

The milestone gives PA19's template tier two things its argument list did not
have: 14.3.2p1's argument at a non-type place, which is a *value*, and
14.5.3p1's place that binds a *run* of arguments rather than one.  Both are
type-table entries, because 14.4p1 makes an argument list what tells two
specializations apart and every fact the tier keys - the specialization, the
substitution, the object-file name - reads that list as `std::vector<TypeId>`.

## Stage Design

**A value argument is a type-table entry.**  `TypeKind::Value`
(`type_model.h`) holds the type the argument was converted to and the bits it
holds, interned like a pointer or an array: `value_type(int, 3)` is one entry
however many times `f<3>` is written.  `is_dependent` asks only its type - the
bits are settled - and `substitute` rebuilds it for `template<class T, T v>`.

**A pack is the same kind of entry, twice.**  `TypeKind::Pack` is either a
*run* - interned by its elements, which is what an argument list bound to a
pack place - or 14.5.3p4's *expansion* `P...`, interned by its pattern, which
is what stands in a list until the run arrives.  `is_dependent` is true for
every expansion and for a run holding a dependent element, so a substitution
reaches both.  Nothing declares an object of either.

**14.1p4 and 14.1p11's places say what each takes.**
`TemplateInfo::Parameter` (`sema_template.h`) is a place rather than a name:
the name its head wrote, whether it binds a value, whether it binds a run, the
syntax that says what type that value has, and the type standing for the place.
`open_parameter_region` opens 14.6.1p1's region once per template and settles
them there - which is where `template<class T, T v>` reaches `T` and where a
pack place is marked on the type it declared, so every later reading finds the
fact without the head.  `pack_place` is what tells a written argument which
place it fills: the places before the pack one for one, and every argument
after them the pack's.  A function template's head is read by the ordinary
declaration path instead, so `function_pack_place` asks the same question of
its declarations.

**5.19 is read out of the spelling, like 8.1p1's type-id is.**
`sema_value_expression.cpp` is to a value argument what `sema_type_id.cpp` is to
a type argument: 14.2 writes the argument list inside a name, so it arrives as
text.  The terminals are *recovered* rather than re-lexed, the split is kept per
spelling, and what a `<` in such a spelling *is* - 14.2's list or 5.9's
operator - is one question with one answer (`sema_name.cpp`).  5.14/5.15/5.16's
unevaluated operand is a `live` flag rather than a second pass.

**An expansion is one reading per element.**  `sema_pack.h` owns it: the
pattern is read again for each argument of the run, in a region binding the
packs it names to that element, and nothing rewrites the pattern's syntax.  The
same reading answers from the three shapes a list is written in - a spelling
(`expand`), a type a declarator or a substitution built (`expand_type`), and
the tree a call's argument list holds (`run_of_node`) - so a run of n elements
costs n readings of one pattern wherever it stands.  All three open the element
region at `element_region`, because a pattern names *either* kind of settled
pack however it was written.  The element binding carries the pack it stands
for (`SemaEntity::pack_element_of`), so a pattern that names it as a pack again
- a nested expansion, `sizeof...` - is read over the run and not the element.

**A function parameter pack is places, not a type.**  8.3.5p3's `f(int...)` and
14.5.3p4's `f(Args... args)` are told apart by whether the declarator's type
names a pack; the second declares one place per element, and 8.3.5p10 names the
first of them after the pack itself.  So the pack's own name reaches the first
place, `SemaEntity::pack_run` on that declaration says how many the expansion
made, and `sizeof...(args)` and `h(args...)` both read the run off the
declaration the name already reaches.  A run of *no* elements has no first
place and is still a declaration, so the clause declares the run itself: one
entry of the parameter list that is no place of the function, which the walks
mapping entries onto places count apart.

**The object file writes the run the flat list cannot be split back into.**
14.4p1 keys the tier by one flat argument list, and 14.5.3's run is one
`<template-arg>` - `J...E` - with a place written `P...` encoded `Dp` of its
pattern.  So the place the run begins at is recorded beside the arguments for a
class and read off the head's declarations for a function template, and the
three sites of `lowir_abi.cpp` that write an argument list ask one helper.

**14.8.2.1p1 deduces a pack as a run.**  A trailing `P...` is one pattern
against every argument the fixed places did not take, and what the pack deduces
to is the run of what its own place took in each of them - so the P/A loop is
the one that was already there, run once per leftover argument into bindings of
its own.  `deduced_arguments` then splices that run into the one flattened
argument list `specialize` and the ABI already read.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it, a
value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value`, and a run is a `Pack`-typed binding of the pack's name.  An
argument that is 14.6.1p1's own expansion binds the *place* instead, because
that is what a definition read against the current instantiation names.

**14.6p8's stand-in is counted, because 7p4 has to know.**  `stood_in_` is a
count, incremented at each place a stand-in is made - including `sizeof...` over
a pack no argument list has settled - and put back by any discarded probe.

**What a reading puts aside is a header of its own.**  `sema_reading.h` holds
the records 6.6.1, 12.2p3, 14.6p8 and 11p6 each leave one reading holding while
it stands, because every one of those readings can stand inside another.

## Current Failure Map

45 failing, grouped by what would fix them:

| group | n | owner |
| --- | --- | --- |
| 14.8.2 where the callee or an argument is still dependent: deduction from a class-template argument - which four of the pack shapes need - a template-id callee, an alias or variable-template parameter (`no declaration of ... accepts the arguments of a call`, `... is in scope`) | 18 | `sema_overload.cpp`, `sema_template.cpp` |
| the rest: a dependent non-type place's own declarator, `sizeof...` inside an argument *spelling*, a multicharacter literal, four single-test shapes | 7 | mixed |
| 14.6.2p1's dependent qualified type and value lookups (`is written after a name that is not a namespace...`, `... does not name a type`) | 5 | `sema_template_head.cpp`, `sema_template.cpp` |
| 5.19 outside the integral subset, which the suite asserts on: `B{}`, a dependent trait's value, a wide literal, two `static_assert` conditions | 4 | `sema_constant.cpp` |
| three LowIR mismatches: PA19's static-member demand, an enum argument's vtable, a constexpr member call in an initializer | 3 | `sema_template.cpp`, `sema_lifetime.cpp` |
| 2.14.8's user-defined literals and their overload sets | 3 | `sema_overload.cpp`, `literal_scan.cpp` |
| 14.5.3p4 in the three lists this checkpoint left: 8.5.1's braced-init-list, 12.6.2's mem-initializer and 5.3.4's new-expression | 3 | `sema_init_list.cpp`, `sema_lifetime.cpp` |
| 10p1 over a base pack of more than one element, which this milestone lays out one of | 2 | `sema_class.cpp`, `sema_layout.cpp` |

## Active Checkpoint

This turn audited **C3** and landed the four fixes in `audit.md`.  The next one
is:

**C4 - 14.8.2 over the places the last two checkpoints opened.**  Selected
because it is now the largest group by a factor of two, it is one owner, and
every shape in it is a call whose candidate set this tier already builds.

- **Owner.**  `sema_overload.cpp` for the candidate set a call collects when the
  callee is a template-id or an object of class type, and `sema_template.cpp`
  for `deduce` over an argument that is a specialization - `f(S<char, short>)`
  against `S<U...>` is the one P/A pair a pack has to match through a class,
  and it is what `transform`, `expand`, `run` and `construct` all fail on.
- **Data flow.**  `deduce` gains a `Class` arm that pairs the pattern's
  `template_arguments` with the argument's, splicing a trailing expansion the
  way `deduce_specialization` already splices a trailing parameter; the run it
  binds is the same `pack_type` entry every other reader takes, so the ABI's
  pack place and `deduced_arguments`' flattening both keep answering.
- **Expected complexity.**  One walk per P/A pair, no rescan of the candidate
  set - the arguments of a specialization are already a list on its type, and
  `packs_in` asks each type of a pattern once.
- **Validation.**  The 18 tests of the group, `make test-report-through-pa19`,
  a multiplicity sweep at 0, 1, 2 and 64 elements through the new arm, and a
  differential run of each new shape through `reference-binaries/cppgm++` with
  its mangled name compared against g++'s.

## Performance Model

Best of five, `-O0`, re-measured this turn.  This machine has a **0.11 s process
floor** - an empty translation unit measures 0.11 s through this binary and
through `reference-binaries/cppgm++` alike - so a row at the floor is a shape
that costs nothing measurable.

| shape | measured |
| --- | --- |
| a pack of 0 / 1 / 64 / 512 / 4096 elements: bound, expanded into a base, counted | **floor** (ref 0.41 s at 4096) |
| a call forwarding a parameter pack of 0 / 2 / 128 / 384 / 1024 places | **floor** (ref 0.31 s at 1024) |
| 2080 expansions over 64 nested `pack_of<id<T>::type...>` | **floor** |
| 512 / 4096 distinct value arguments over two templates | **floor / 0.62 s** |
| `fac<200>` / `fac<800>` metafunction chain | **floor** |
| a 2000-deep chain instantiated but not evaluated | **floor** (ref SIGSEGV) |
| 256- / 1024-deep `s< s< ... <int> > >` spelling | **floor** (ref > 60 s at 256) |
| one template-id of 1024 arguments | **floor** |

An expansion is linear in the run: one reading of the pattern per element, one
region per element, and no rewriting of the pattern's syntax, so n elements cost
n readings and never n^2.  A run is interned by its elements, so two places
bound to one run read one entry and `sizeof...` is a vector length.  The split
of an argument spelling is memoised per spelling (`value_words_`),
`open_parameter_region` runs once per template rather than once per naming, and
`packs_in` asks each type of a pattern once rather than once per path through
it.

The one shape that is not linear is a type whose arguments *double* -
`typedef p<t22,t22> t23;` costs about a second - which is the exponential
spelling PA19 recorded rather than this milestone's.  A metafunction with no
terminating specialization still overflows the machine stack rather than being
diagnosed; a depth guard is owed whenever a checkpoint touches
`instantiate_class` again.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 14.1p4's non-type parameter and 14.3.2's integral argument: `TypeKind::Value` as an interned converted constant, `TemplateInfo::Parameter` as a place, 14.6.1p1's region opened once, 5.19 read out of the argument spelling, `SemaKind::TemplateValue` bound as a constant, 7.1.5p9's constexpr object, 7p4 deferred behind a counted stand-in, and 10p1's settled base completed inside 14.6p8's reading | 39 -> **85 / 164**; pa1-pa19 2169 / 2169 |
| C2 | 14.7.3's explicit specialization: a `template<>` head declaring the specialization and no template, the class body read in place of the pattern and the function body run in place of the pattern's, both keyed by the interned argument list | 85 -> **92 / 164**; `fac<200>` SIGSEGV -> **0.01 s** |
| C1, C2 audit | the spelling a value argument arrives as: 14.2's `<` told apart from 5.9's and 5.8's, 3.4.3p1's rooted name, 4.12p1's conversion, 5.2.3p1/p3's notation, 8.5p16 and 8.5.4p3's initializers, and 14.6p8's count put back by a discarded probe | 92 / 164 -> **103 / 169** with five fixtures added |
| C3a | 14.5.3's place and run at the class tier: `TypeKind::Pack` as both a run and an expansion, `pack_place` counting a written list, an expansion read once per element, 5.3.3p5's `sizeof...` parsed and answered, and 14.5.3p4's base-specifier pattern laid out where the run holds one base | 103 -> **108 / 169** |
| C3b | the function tier: 14.8.2.1p1 deducing a trailing `P...` as a run, `deduced_arguments` splicing it into one flattened list, 8.3.5p3's ellipsis told from 14.5.3p4's expansion by the declarator's type, one place declared per element under 8.3.5p10's names, and a substitution splicing an expansion inside a parameter list | 108 -> **118 / 169** |
| C3c | 14.5.3p4 in a call's argument list, read over the tree rather than a spelling, with a function parameter pack as one of the answers; explicit argument lists counted by the pack place; and 14.3.2p1 refusing a pack of types at a non-type place | 118 -> **123 / 169**; expansion linear at 1/64/512 elements |
| C3 audit | the two kinds of settled pack and the list the object file writes for either: one element region for a spelling and a tree alike, an element that carries the pack a nested expansion and `sizeof...` still name, a run of no elements declared where it declared no place, and 14.5.3's `J...E` and `Dp` in every mangled name | 123 / 169 -> **127 / 172** with three fixtures added; linear at 4096 elements |
