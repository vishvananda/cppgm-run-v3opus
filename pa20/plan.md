# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **136 / 175** - 125 of the 164 checked-in fixtures and the 11
under `cppgm.tests/course/pa20` - from a turn-start baseline of **133 / 172**,
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
them there.  `pack_place` is what tells a written argument which place it fills;
a function template's head is read by the ordinary declaration path instead, so
`function_pack_place` asks the same question of its declarations.  14.1p3's
unnamed place is declared there too, because 14.1p9's default is the *place's*
fact and not the name's.

**5.19 is read out of the spelling, like 8.1p1's type-id is.**
`sema_value_expression.cpp` is to a value argument what `sema_type_id.cpp` is to
a type argument: 14.2 writes the argument list inside a name, so it arrives as
text.  The terminals are *recovered* rather than re-lexed, the split is kept per
spelling, and what a `<` in such a spelling *is* - 14.2's list or 5.9's
operator - is one question with one answer (`sema_name.cpp`).

**An expansion is one reading per element.**  `sema_pack.h` owns it: the
pattern is read again for each argument of the run, in a region binding the
packs it names to that element, and nothing rewrites the pattern's syntax.  The
same reading answers from the three shapes a list is written in - a spelling
(`expand`), a type a declarator or a substitution built (`expand_type`), and
the tree a call's argument list holds (`run_of_node`) - so a run of n elements
costs n readings of one pattern wherever it stands.

**A function parameter pack is places, not a type.**  8.3.5p3's `f(int...)` and
14.5.3p4's `f(Args... args)` are told apart by whether the declarator's type
names a pack; the second declares one place per element, and 8.3.5p10 names the
first of them after the pack itself.  A run of *no* elements has no first place
and is still a declaration, so the clause declares the run itself.

**The object file writes the run the flat list cannot be split back into.**
14.4p1 keys the tier by one flat argument list, and 14.5.3's run is one
`<template-arg>` - `J...E` - with a place written `P...` encoded `Dp` of its
pattern, so the place the run begins at is recorded beside the arguments.

**14.8.2 is a match, and `sema_deduce.h` owns it.**  A use of a function
template names it without its arguments, so what makes a specialization is a
walk of a parameter type P beside the type A of what the use put there.  Two
uses write those pairs - 14.8.2.1's call and 14.8.2.2's target type - and both
end at 14.8.2p5, which asks whether the pairs, 14.5.3p4's run and 14.1p9's
defaults between them left every place with an argument.  The match knows
nothing about calls, so it is a reading of its own rather than part of 14.7.1's
instantiation.

**A list of entries is one rule.**  14.2's template-argument-list and 8.3.5p1's
parameter list are both entries paired one for one with a trailing `P...`
standing for every entry the ones before it did not take, so `match_arguments`
is what reads either - which is what lets 14.8.2.2's target type, whose A is one
whole function type, deduce a run.

**A specialization is a P/A pair of its own shape.**  `A<T>` against `A<int>`
is one argument list against another (`match_arguments`), so a trailing `P...`
in the pattern is the same run deduction a trailing parameter is - and
14.8.2.1p3's A may be a class *derived* from what P names, which is a walk of
10p1's base chain and is allowed only at the top of a pair the use wrote.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it, a
value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value`, and a run is a `Pack`-typed binding of the pack's name.  So
14.1p9's default is read in a region that binds the places before it at *both*
tiers: a type-id could be substituted afterwards, but 5.19's constant expression
is evaluated where it stands and needs `A` to be a constant there.

**14.1p4 reaches the object file too.**  An argument at a value place is an
expression, so one no substitution has settled is written `X <expression> E` -
`XT_E` for the place and `XspT_E` for 14.5.3p4's expansion of one, where a type
place writes `T_` and `DpT_`.  The `<template-param>` inside is a substitution
candidate and the `X ... E` around it is not.

**14.5.6.1p5 tells a pack place from a single place.**  The signature two heads
are compared by stands each parameter for its position, and a place that binds a
*run* is a position of its own - otherwise `f(T)` and `f(Ts...)` are one
declaration.  Two templates then need ordering, and 14.8.2.4p9 leaves the head
that wrote a place ahead of the head that wrote a run.

**14.6.2p1's dependent member is settled by the substitution.**  A name written
after a prefix the definition could not settle is a stand-in carrying the two
facts the ABI writes apart - the prefix and the name.  So `substituted` settles
the prefix and looks the name up in the class it became, exactly as it re-reads
a `decltype` the definition left standing; and 14.8.2.5p5 makes the same
stand-in a *non-deduced* context, because a nested-name-specifier says nothing
about what its prefix names.

**14.6p8's reading is put aside whole.**  `stood_in_` is a count of the values a
reading stood in for; `checking_` is the depth of the reading itself and
`unit_dialect_` is what the unit is read in.  10p1's base class is the one thing
a pattern's reading demands in earnest, so `require_settled_type` puts *both*
aside - a specialization completed in the checking dialect is left with none of
12.1's members it is owed.

## Current Failure Map

39 failing, grouped by what would fix them.  Every row was re-derived from the
build's own diagnostics this turn:

| group | n | owner |
| --- | --- | --- |
| 14.5.5's partial specialization and the variable templates beside it: `support.h`'s `is_same<A,A>` and `enable_if<true,T>`, and `same_v<char,char>` / `datasizeof_v<T>` / `flag<less<>>` naming no declaration | 7 | `sema_template.cpp`, `sema_template_head.cpp` |
| 5.19 outside the integral subset and six single-test shapes: `B{}`, `I + sizeof...(I)`, a wide string literal, a multicharacter constant, `u"x"` initialising a `const char16_t` array, `&C::f` as a template argument, `sizeof` of a qualified static array member, a conversion through a specialized base | 8 | mixed |
| 14.5.3p4 in the lists the call's argument list already answers for: 8.5.1's braced-init-list and array bound, 5.3.4's new-expression, 12.6.2's mem-initializer, and a `decltype` over one (`an expression is outside the PA12 subset`) | 5 | `sema_init_list.cpp`, `sema_allocation.cpp`, `sema_lifetime.cpp` |
| 14.6.2p1's dependent *value* lookups: a member named before a later declaration shadows it, and a qualified value inside an argument spelling (`v is written after a name that is not a namespace, class or enumeration`) | 5 | `sema_declarator.cpp`, `sema_value_expression.cpp` |
| `decltype` inside a template-argument *spelling*, where the operand is a call, an object of class type or a delete-expression | 4 | `sema_value_expression.cpp`, `sema_type_id.cpp` |
| 2.14.8's user-defined literals and their overload sets | 3 | `sema_overload.cpp`, `literal_scan.cpp` |
| three LowIR mismatches: PA19's static-member demand, an enum argument's vtable, a constexpr member call in an initializer | 3 | `sema_template.cpp`, `sema_lifetime.cpp` |
| two packs in one function template head, which needs the flat argument list to record where each run begins | 2 | `sema_deduce.cpp`, `type_model.h` |
| 10p1 over a base pack of more than one element | 2 | `sema_class.cpp`, `sema_layout.cpp` |

## Active Checkpoint

This turn landed **C4** and its audit.  The next one is:

**C5 - 14.5.5's partial specialization, and the variable templates beside it.**
Selected because it is the largest group with a single owner, and because
`support.h`'s `is_same` and `enable_if` are what every later fixture that writes
a trait reaches through - so the seven tests are a floor rather than the whole
of it.  The 14.5.3p4-in-lists group is tied with the dependent-value one now
and carries no shared header with it.

- **Owner.**  `sema_template_head.cpp` for the head that declares fewer places
  than the primary takes arguments, and `sema_template.cpp` for the choice
  between the partial specializations of one template.
- **Data flow.**  A `template<...> struct s<pattern>` head declares its own
  places and an argument *pattern* over them, which is the P of a match
  `sema_deduce.h` already reads - `match_arguments` against the written list.
  `instantiate_class` then asks which patterns the argument list matches and
  14.5.5.2 orders them by the same `at_least_as_specialized` a function
  template's are ordered by.  A variable template is the same head over an
  object rather than a class, so its specialization is a declaration the region
  holds keyed by the argument list.
- **Expected complexity.**  One match per partial specialization of the named
  template per distinct argument list, memoised with the specialization; nothing
  at all for a template no head partially specialized, which is every template a
  program without a trait writes.
- **Validation.**  The seven tests of the group, `make test-report-through-pa19`,
  a multiplicity sweep at 1, 2 and 64 partial specializations of one template, a
  differential sweep of the ordering against g++ and `reference-binaries/cppgm++`,
  and a valgrind run of each new shape.

## Performance Model

Best of seven, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row below is the shape's own cost.  A
harness that spawns processes of its own reads this machine's floor as 0.11 s;
it is not one.

| shape | measured |
| --- | --- |
| a pack of 512 / 4096 elements: bound, expanded into a base, counted | 0.006 / **0.021 s** |
| a call forwarding a parameter pack of 1024 places | **0.024 s** |
| a target type deducing a run of 256 / 1024 / 4096 places | 0.006 / 0.015 / **0.048 s** |
| 200 / 800 calls ordering a pack head against a non-pack one | 0.022 / **0.084 s** |
| 400 / 1600 / 3200 calls reading a value default that names an earlier place | 0.033 / 0.133 / **0.276 s** |
| 300 calls deducing a run from a specialization argument | **0.017 s** |
| 512 / 4096 distinct value arguments over two templates | 0.048 / **0.510 s** |
| one template-id of 4096 arguments | **0.011 s** |
| 2080 expansions over 64 nested `pack_of<id<T>::type...>` | **0.025 s** |
| `fac<800>` metafunction chain | **0.033 s** |
| a 2000-deep chain instantiated but not evaluated | **0.079 s** |
| 14.8.2.1p3 through a 200-deep base chain | **0.010 s** |

Every row was re-measured against the `fe28ba9d` build this turn and none moved
by more than run-to-run noise; the value-default row has no baseline at all,
because that shape did not compile before it.  Each row is linear in what it
walks: an expansion is linear in the run and a run is interned by its elements,
so `sizeof...` is a vector length.  14.8.2's
match walks one P and one A and never rescans the candidate set: the arguments
of a specialization are already a list on its type, `packs_in` asks each type of
a pattern once, and 14.8.2.1p3's base search is one walk of 10p1's chain.
14.5.6.2's ordering is one fact of a pair, memoised.  A default is read once per
deduction that reaches one, in a region opened there.  The reference binary is
20x slower on the three shapes this turn added: 1.01 s, 0.71 s and 1.02 s where
the rows above are 0.048 s, 0.084 s and 0.276 s.

The one shape that is not linear is a type whose arguments *double* -
`typedef p<t22,t22> t23;` costs about a second - which is the exponential
spelling PA19 recorded.  A metafunction with no terminating specialization still
overflows the machine stack rather than being diagnosed; a depth guard is owed
whenever a checkpoint touches `instantiate_class` again.

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
| C4 | 14.8.2 given its own owner (`sema_deduce.h`), and the four things a use it could not match needed: a specialization P matched as an argument *list* so a trailing `P...` deduces a run, 14.8.2.1p3's A that is a class derived from what P names, 14.8.2.5p5's non-deduced context, 14.1p9's default at a value place - unnamed places included - and 14.6.2p1's dependent member settled by the substitution.  10p1's base is now completed in the unit's own dialect, an explicit list that stopped at the pack place still deduces, and `user_types_` is a deque because every reader of it holds a reference while a class is completed | 127 -> **133 / 172**; pa1-pa19 2169 / 2169; floor re-measured at 0.00 s |
| C4 audit | the list a use is chosen from and the one the object file writes: 8.3.5p1's parameter list matched by the same rule 14.2's list is, so 14.8.2.2's target type deduces a run; 14.1p9's value default read in a region binding the places before it, as the class tier already read it; 14.5.6.1p5 telling a pack place from a single one, and 14.8.2.4p9 ordering the two heads that makes; and 14.1p4's `X <expression> E` for every non-type argument no substitution has settled | 133 / 172 -> **136 / 175** with three fixtures added; every one of 71 swept shapes agrees with g++ |
