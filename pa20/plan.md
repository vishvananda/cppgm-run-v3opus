# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **133 / 172** - 125 of the 164 checked-in fixtures and the 8
under `cppgm.tests/course/pa20` - from a turn-start baseline of **127 / 172**,
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

**A specialization is a P/A pair of its own shape.**  `A<T>` against `A<int>`
is one argument list against another (`match_arguments`), so a trailing `P...`
in the pattern is the same run deduction a trailing parameter is - and
14.8.2.1p3's A may be a class *derived* from what P names, which is a walk of
10p1's base chain and is allowed only at the top of a pair the use wrote.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it, a
value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value`, and a run is a `Pack`-typed binding of the pack's name.

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

39 failing, grouped by what would fix them:

| group | n | owner |
| --- | --- | --- |
| 14.5.3p4 in the lists the call's argument list already answers for: 8.5.1's braced-init-list and array bound, 5.3.4's new-expression, 12.6.2's mem-initializer, and a `decltype` over one (`an expression is outside the PA12 subset`) | 5 | `sema_init_list.cpp`, `sema_allocation.cpp`, `sema_lifetime.cpp` |
| 14.5.5's partial specialization, which `support.h`'s `is_same<A,A>` and `enable_if<true,T>` are (`a static_assert condition is false`), and the variable templates beside them | 7 | `sema_template.cpp`, `sema_template_head.cpp` |
| 14.6.2p1's dependent *value* lookups: a member named before a later declaration shadows it, and a qualified value inside an argument spelling | 5 | `sema_declarator.cpp`, `sema_value_expression.cpp` |
| `decltype` inside a template-argument *spelling*, where the operand is a call, an object of class type or a delete-expression | 4 | `sema_value_expression.cpp`, `sema_type_id.cpp` |
| two packs in one function template head, which needs the flat argument list to record where each run begins | 2 | `sema_deduce.cpp`, `type_model.h` |
| 10p1 over a base pack of more than one element | 2 | `sema_class.cpp`, `sema_layout.cpp` |
| 2.14.8's user-defined literals and their overload sets | 3 | `sema_overload.cpp`, `literal_scan.cpp` |
| three LowIR mismatches: PA19's static-member demand, an enum argument's vtable, a constexpr member call in an initializer | 3 | `sema_template.cpp`, `sema_lifetime.cpp` |
| 5.19 outside the integral subset and six single-test shapes | 8 | mixed |

## Active Checkpoint

This turn landed **C4**.  The next one is:

**C5 - 14.5.3p4 in the lists 14.8.2's did not reach.**  Selected because it is
now the largest group, it is one written form - a clause standing for a run -
answered in four places that each walk a list of AST clauses, and three of the
five tests need nothing else.

- **Owner.**  `sema_init_list.cpp` for `InitializerClauses`, which is the cursor
  all four walks share, with `sema_allocation.cpp` and `sema_lifetime.cpp` for
  the new-expression and mem-initializer lists that build their own.
- **Data flow.**  A list holding a `PackExpansionExpression` is read once into a
  run of (clause, element region) pairs - the same `run_of_node` /
  `element_region` reading a call's argument list already asks - and the cursor
  answers `next()` off that run where it has one.  8.5.1p4's deduced array bound
  and 13.3's candidate arguments then count the run rather than the syntax.
- **Expected complexity.**  One reading of the list per initialization, n
  readings of one pattern for a run of n, and nothing at all for a list that
  wrote no expansion - which is every list a program without a pack writes.
- **Validation.**  The five tests of the group, `make test-report-through-pa19`,
  a multiplicity sweep at 0, 1, 2 and 512 clauses, and a valgrind run of each
  new shape.

## Performance Model

Best of three or five, `-O0`, **re-measured this turn**: the process floor is
now **0.00 s** rather than the 0.11 s an earlier turn carried forward, so a row
below is the shape's own cost.

| shape | measured |
| --- | --- |
| a pack of 0 / 1 / 64 / 512 / 4096 elements: bound, expanded into a base, counted | **0.02 s at 4096** |
| one template-id of 4096 arguments | **0.01 s** |
| a call forwarding a parameter pack of 0 / 2 / 128 / 384 / 1024 places | **floor** |
| 2080 expansions over 64 nested `pack_of<id<T>::type...>` | **floor** |
| 512 / 4096 distinct value arguments over two templates | **floor / 0.62 s** |
| `fac<200>` / `fac<800>` metafunction chain | **floor** |
| a 2000-deep chain instantiated but not evaluated | **floor** |
| 256- / 1024-deep `s< s< ... <int> > >` spelling | **floor** |
| 14.8.2.1p3 through a 200-deep base chain | **0.01 s** |
| 400 function templates returning `typename A<T>::type` | **0.04 s** |
| 300 calls deducing a pack from a class argument | **0.01 s** |

An expansion is linear in the run and a run is interned by its elements, so
`sizeof...` is a vector length.  14.8.2's match walks one P and one A and never
rescans the candidate set: the arguments of a specialization are already a list
on its type, `packs_in` asks each type of a pattern once, and 14.8.2.1p3's base
search is one walk of 10p1's chain, which this milestone lays out one deep per
class.  14.6.2p1's member lookup is one probe per distinct stand-in per
substitution, memoised with the rest of that substitution.

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
