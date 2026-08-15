# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **162 / 184** - 142 of the 164 checked-in fixtures and the 20
under `cppgm.tests/course/pa20` - from a turn-start baseline of **158 / 184**,
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
operator - is one question with one answer (`sema_name.cpp`).  Each is a reader
of its own that borrows the analyzer - `SpelledTypeId` and
`TemplateArgumentReader` - because where in the words the reading stands is
state no walk of a tree has.

**But an operand a spelling cannot answer for is asked of the parse.**
7.1.6.2p1's decltype-specifier holds an *expression*, and 5.1.1p8's
id-expression is only one of them: a call through 13.5.4's operator, a
delete-expression and 5.2.3p1's explicit type conversion each say nothing a
lookup of their text could reach.  So the parse keeps the tree it read for every
such operand (`AstArena::keep_spelled`), under the spelling it flattened it
into - the nodes are the parse's and not the tree's, which is exactly what a
template-argument-list drops - and the analyzer borrows that table
(`set_expressions`).  A specifier met as text is then answered by
`decltype_type`, the one reading that answers every other one.

**An expansion is one reading per element.**  `sema_pack.h` owns it: the
pattern is read again for each argument of the run, in a region binding the
packs it names to that element, and nothing rewrites the pattern's syntax.  The
same reading answers from the three shapes a list is written in - a spelling
(`expand`), a type a declarator or a substitution built (`expand_type`), and
the tree a call's argument list holds (`run_of_node`) - so a run of n elements
costs n readings of one pattern wherever it stands.

**A list the program wrote is what that reading is asked of.**  `WrittenList`
(`sema_pack.h`) is one written list and what its entries come to: the children
as written until an entry is `pattern...`, and from there each entry paired with
the region it is read in.  So 8.5.1's clauses, 5.2.2's arguments, 5.2.3's
conversion, 5.3.4's placement and initializer, 8.3.4p3's bound and 13.3.3.1.5's
length are one question with one answer, and a list holding no expansion - every
list PA15-PA19 lower - pays one node-kind test per entry and allocates nothing.
`InitializerClauses` is that list plus 8.5.1p11's cursor, so the walk that lets
a subaggregate take clauses out of the enclosing list reads each of them where
14.5.3p4 put it (`Clauses::in`).  A run no argument list has settled stands as
the one entry it was written as, which is what leaves 8.3.4p3's bound unsettled
inside 14.6p8's reading rather than wrong.

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

**14.5.5's pattern is that same match, and `sema_specialize.h` owns it.**  A
partial specialization declares places of its own and writes an argument
*pattern* over them, so it is a template beside the primary rather than a second
declaration of it: what it adds is a second body an argument list may be read
from.  14.5.5.1p1 is which - `match_arguments(pattern, arguments)` - and
14.5.5.2p1's ordering is the same match run between two patterns, because the
general one is the one that takes the specialized one as its arguments.  The
answer is a fact of the *template*, so it is memoised on `TemplateInfo` under the
interned list a naming already holds, and dropped whole where a later
declaration adds a pattern that list never saw.  Two heads spell one pattern
over places of their own, so what tells a redeclaration from a second pattern is
14.5.6.1p5's signature: the pattern with each place standing for its position.

**14.5.1p1's variable template is the third tier.**  A head over an object is
the same three steps - record the pattern, bind an argument list, read the
pattern once per list - and what differs is what one list makes of it.  A
specialization of one is reached where 5.19 asks for a constant, so the reading
leaves the constant its initializer evaluated to and declares no object: it is a
`SemaKind::TemplateValue` binding, exactly what a non-type place is bound to, so
every reader that already folds one folds this.  14.5.5's pattern and 14.7.3's
`template<>` answer for one argument list here as they do for a class.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it, a
value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value`, and a run is a `Pack`-typed binding of the pack's name.  A
place *standing for itself* - which 14.6.1p1's current instantiation puts at
every argument, and which 14.5.1.3p1's out-of-class definition is read against -
says which it is on its own type, so the two readings of one head bind it the
same way.  So
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

**A prefix an argument list has already settled is asked for its definition.**
3.4.3p1 looks a name up *in* the region its prefix named, which for a class
template specialization is 3.9p5's context requiring a complete type - so the
demand is `require_settled_type` and not `require_complete_type`.  14.6p8's
reading of a template definition asks for nothing, but a prefix no argument list
can still change is one the definition itself is read against, so the reading is
put aside for that demand exactly as 10p1's base class puts it aside.  Two walks
make that demand - `resolve_prefix` for a prefix the spelling reaches and
`qualified_in_type` for one that is a *type*, which is 7.1.6.2p1's
decltype-specifier - and both answer alike.  A prefix that is still dependent is
untouched by either: every component behind it is a member of the one before it,
which is 14.6.2p1's stand-in the substitution settles.

**What this milestone cannot read is what it cannot instantiate.**  14p1 lets a
program declare a template it never names, so a head or a pattern outside the
slice is recorded rather than refused where it stands - but 14.5.5p1's pattern is
a *second body* an argument list may be read from, so one that could not be read
is not a declaration that may be left out: every list would then be read from the
primary's body, which is a different program.  So a template one of whose second
bodies is unknown answers no argument list at all, at 14.3p1's gate that every
naming already passes, and 14.5.5p8.3's undeducible place is refused at the list
that matched it.

**One reading per argument list, and the tiers hold it differently.**  A class
specialization is held before its body is read, so a naming inside that body
finds the declaration already made and the reading terminates on its own.
14.5.1p1's specialization *is* the constant its initializer evaluates to, so
there is nothing to hold until the reading is over - `TemplateInfo::reading` is
therefore what a variable template holds instead, and a naming of a list already
being read is 5.19p2's circle rather than a second reading of it.

**14.6p8's reading is put aside whole.**  `stood_in_` is a count of the values a
reading stood in for; `checking_` is the depth of the reading itself and
`unit_dialect_` is what the unit is read in.  10p1's base class and 3.4.3p1's
settled prefix are what a pattern's reading demands in earnest, so
`require_settled_type` puts *both* aside - a specialization completed in the
checking dialect is left with none of 12.1's members it is owed.

## Current Failure Map

22 failing, grouped by what would fix them.

| group | n | owner |
| --- | --- | --- |
| 5.19 outside the integral subset: `B{}`, `I + sizeof...(I)`, a wide string literal, a multicharacter constant, `sizeof` of a qualified static array member | 5 | `sema_constant.cpp`, `sema_value_expression.cpp`, `literal_scan.cpp` |
| seven singletons: an aggregate whose array member no by-value parameter carries; an alias rewrite whose `value_type` does not name a type; a value place naming an incomplete current instantiation; `&C::f` as an argument; 5.9's `<` inside an argument spelling read as 14.2's list; a qualified function-template call's conversion; a conversion through a specialized base | 7 | mixed |
| 2.14.8's user-defined literals with their overload sets, and the LowIR one cooked call writes | 3 | `literal_scan.cpp`, `sema_overload.cpp` |
| three LowIR mismatches: PA19's static-member demand, an enum argument's vtable, a constexpr member call in an initializer | 3 | `sema_template.cpp`, `sema_lifetime.cpp`, `lowir_vtable.cpp` |
| 14.1p11's *second* pack place in one head: a flat argument list cannot say where one run ends and the next begins, so `<class... U, class... T>` deduces a list neither place can be bound from | 2 | `sema_deduce.cpp`, `sema_pack.cpp`, `type_model.h` |
| 10p1 over a base pack of more than one element | 2 | `sema_class.cpp`, `sema_layout.cpp` |

Outside the fixtures, the sweeps leave six shapes this milestone refuses where
both oracles accept: a specialization's body cannot name its own class -
`typedef s self;` inside `struct s<T*>` finds the primary and `s<T*>` written
there is read as 12.1p1's constructor name and does not parse, which has been
true of `template<>` specializations since C2; a partial specialization has no
out-of-class member definitions; a dependent array bound is unreadable in an
argument *spelling* (`s<T[N]>`, where `s<Arr>` over a typedef reads); a template
template parameter is outside every head; `typename c<true>::type` written in a
template nobody instantiates, where `c` is only declared, is refused where it
stands - which g++ refuses too ([temp.res]p8) and the reference accepts; and a
decltype-specifier is outside the base-specifier grammar
(`struct outer : decltype(mk())::inner`), which the reference refuses too.  A
seventh is new: a constructor template written in a class body
(`template<class... A> D(A... a) : B(a...) {}`) is refused where it stands,
which is 12.1p1's name read without a head over it and not a list question.
Four more stand: 14.5.5p1 does not refuse a partial
specialization of a *function* template; 14.7.3's explicit specialization of a
function template is emitted `binding=weak` where the reference writes
`binding=strong`; an unnamed enumeration reached through a `decltype` is mangled
`N1S17__anonymous_enum1E` where the reference writes the ABI's `N1SUt_E`; and a
static data member of a specialization of a template with a *non-type* parameter
is a `load` here and in g++ where the reference folds the initializer.  The first
three are metadata the comparison strips; the fourth is the family
`100-nontype-template-argument-static-member-no-storage` already owns.

## Active Checkpoint

**C8 - 5.19 outside the integral subset.**  Selected because it is the largest
group whose members share one owner and one rule: each of the five is a constant
expression the milestone reads for a template argument, a `static_assert` or an
array bound, and each is refused by the same walk for a different operand -
8.5.4's `B{}`, 5.3.3p5's `sizeof...` inside an arithmetic expression, 2.14.5's
wide string literal, 2.14.3's multicharacter constant, and `sizeof` of a
qualified static array member whose class the demand never completed.

- **Owner.**  `sema_constant.cpp` for 5.19's evaluation, `literal_scan.cpp` for
  the two literals whose bits phase 7 already knows, `sema_value_expression.cpp`
  for the operands an argument *spelling* has to recover.
- **Data flow.**  A written operand arrives either as the tree PA10 kept or as
  the text 14.2 wrote it inside a name; both end at one evaluation, so the
  increment is what that evaluation knows about a value - a class prvalue whose
  members are constants, the `std::size_t` a `sizeof...` is, and the code units
  a wide or multicharacter literal holds - rather than a second reader.
- **Expected complexity.**  One evaluation per written operand, memoised where
  a specialization already holds its constant; nothing for a program that
  writes none.
- **Validation.**  The five tests, `make test-report-through-pa19`, a
  differential sweep of every operand shape 5.19 admits against g++ and
  `pa20/cppgm++-ref`, and a valgrind run of each new shape.

## Performance Model

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row below is the shape's own cost.  A
harness that spawns a process of its own per run reads this machine's floor as
0.11 s; it is not one, and the `pa20/cppgm++-ref` wrapper adds a further ~0.5 s
of its own before the reference binary starts.

Rows marked \* were regenerated and re-measured against this turn's build; the
rest were measured against an earlier build and are carried forward, because
this turn touched what a written list comes to, which the marked rows bracket at
1 / 64 / 512 / 2048 entries.

| shape | here | `pa20/cppgm++-ref` |
| --- | --- | --- |
| \* 512 / 2048 / 8192 distinct `decltype` spellings in argument lists | 0.018 / 0.069 / **0.300 s** | 0.148 / 0.691 / 9.7 s |
| \* 8192 namings of *one* such spelling | **0.180 s** | 0.949 s |
| \* a `decltype` spelling nested 24 deep | **0.004 s** | 0.013 s |
| \* 256 / 1024 / 4096 `decltype` prefixes in one template definition | 0.021 / 0.082 / **0.410 s** | 23.9 s at 4096 |
| \* 64 names behind one dependent `decltype` prefix | **0.006 s** | 0.022 s |
| \* 256 / 1024 / 4096 out-of-class definitions over a value place | 0.014 / 0.046 / **0.188 s** | 2.084 s at 4096 |
| \* 256 / 2048 settled prefixes named in one definition | 0.015 / **0.119 s** | 0.250 s at 2048 |
| \* a pack of 4096 elements bound and counted | **0.017 s** | 0.159 s |
| \* `fac<800>` metafunction chain | **0.032 s** | 0.167 s |
| \* 256 patterns against 2048 distinct lists | **0.063 s** | 11.1 s |
| \* 14.5.3p4's recursion over a pack of 1024 | **1.556 s** | 9.3 s |
| \* an expansion of 1 / 64 / 512 / 2048 in an array's clause list | 0.005 / 0.007 / 0.015 / **0.046 s** | 1.30 s at 2048 |
| \* the same in an aggregate's clause list | 0.005 / 0.008 / 0.020 / **0.065 s** | 1.40 s at 2048 |
| \* the same as a constructor's argument list | 0.006 / 0.008 / 0.026 / **0.085 s** | 1.69 s at 2048 |
| \* a braced list nested 24 deep with an expansion at the leaf | **0.093 s** | - |
| \* an aggregate of 2^15 / 2^17 subobjects, with / without an expansion | 1.483 / 1.499 and 6.713 / **6.734 s** | - |
| a call forwarding a parameter pack of 1024 places | 0.025 s | - |
| a target type deducing a run of 4096 places | 0.041 s | - |
| 800 calls ordering a pack head against a non-pack one | 0.022 s | - |
| 3200 calls reading a value default that names an earlier place | 0.192 s | - |
| 4096 distinct value arguments over two templates | 0.550 s | - |
| a 2000-deep chain instantiated but not evaluated | 0.085 s | - |
| 14.8.2.1p3 through a 200-deep base chain | 0.011 s | - |
| 64 nested-pointer patterns all matching one list, ordered pairwise | 0.008 s | - |
| 512 / 2048 distinct variable-template specializations | 0.015 / 0.051 s | - |
| a variable-template chain 800 / 3000 / 6000 deep | 0.010 / 0.031 / 0.070 s | - |
| the doubling spelling at 2^20 leaves | 0.912 s | 2.277 s |

The decltype table is one entry per *distinct* operand spelling and one hash
lookup per naming, so the 512 / 2048 / 8192 row is linear in the spellings a
program writes and the 8192-namings row is cheaper than the 8192-spellings one -
which is what says the table is keyed by the text and not by the naming.
Nesting is flat because a nested spelling is one more entry, not one more scan.
`require_settled_type` at a prefix adds one integer test per component of a
nested-name-specifier and one instantiation per settled specialization a
definition names, which is why the pattern and metafunction rows did not move; a
prefix an argument list has yet to settle costs one stand-in per prefix and
component, memoised, which is what the 64-names row measures.  A place bound
under 14.6.1p1's current instantiation costs one test of its own type, which is
why the pack and recursion rows are the same to within the noise as the build
that did not make it.

A written list costs one node-kind test per entry until an expansion is found
and nothing at all after that for a list holding none, which is why the three
2048-entry rows are linear in the entries and 3.8x their own 512 rows.  The
deepest nesting is flat for the same reason: a nested list is one more list and
not one more scan.  The 2^17-subobject aggregate is exponential in its *type*
and not in the reading - the row with an expansion and the row without it are
the same to within the noise - so it is the same shape's-own-cost the doubling
spelling below is.

14.5.5.1p1's choice is one match per pattern per *distinct* argument list and
nothing else.  14.5.5.2p1's ordering is quadratic in the patterns that *match*,
which the 64-deep row measures at 4096 comparisons; a use matches one or two in
every shape a program writes.

Two shapes are not linear in what they walk, and both are the shape's own cost
rather than a reading's: a type whose arguments *double* is exponential in the
spelling, and 14.5.3p4's recursion over a pack walks argument lists whose lengths
sum to n^2/2 - g++ is 0.210 s at 1024 where this compiler is 1.556 s and the
reference 9.3 s.  A *class* metafunction with no terminating specialization
still overflows the machine stack rather than being diagnosed, here and in the
reference alike; a depth guard is owed whenever a checkpoint touches
`instantiate_class` again.  `sema_analyzer.h` is at 2380 of the audit's 2400
header lines.

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
| C5 | 14.5.5's pattern and 14.5.1p1's variable template given one owner (`sema_specialize.h`): a partial specialization as a head, an argument pattern and a body held beside the primary; 14.5.5.1p1's choice as `match_arguments` over the interned list, memoised per template and dropped where a later pattern arrives; 14.5.5.2p1's ordering as that same match between two patterns; 14.5.6.1p5's signature telling a redeclaration of one pattern from a second; and a variable template's specialization as the constant one init-declarator evaluates to - with 14.7.3's `template<>` and 9.4.2p1's qualified declarator-id answering for both tiers | 136 / 175 -> **145 / 178** with three fixtures added; pa1-pa19 2169 / 2169; every one of 48 swept shapes agrees with g++; ref 20x slower on the pattern row |
| C5 audit | what a pattern this milestone could not read leaves behind: three exits dropped a partial specialization and let the *primary* answer for every list it would have taken, so a template one of whose second bodies is unknown now answers no argument list at all, and 14.5.5p8.3's undeducible place is refused at the list that matched; and 14.5.1p1's specialization is the constant its initializer evaluates to, so one that names itself ran until the machine stack ran out where both oracles diagnose it | 145 / 178 -> **147 / 180** with two fixtures added; pa1-pa19 2169 / 2169; 68 swept shapes |
| C6 | a decltype an argument list wrote, and the prefix it stands before: the parse now keeps the tree it read for every decltype operand it flattened into a name (`AstArena::keep_spelled`), so a specifier met as text is answered by `decltype_type` and a call, a delete-expression or 5.2.3p1's conversion reads where only 5.1.1p8's id-expression did; the same carried tree is asked by 5.19's `id_constant` and by `resolve_prefix`, which every other reader of an id-expression already asked; and 3.4.3p1's prefix that an argument list has settled now demands its definition through `require_settled_type`, which is what lets `typename c<lower>::type` be written in a template definition at all.  `sema_type_id.cpp` became its own owner, `SpelledTypeId`, freeing 19 header lines | 147 / 180 -> **156 / 182** with two fixtures added; pa1-pa19 2169 / 2169; 37 decltype-operand shapes and 12 prefix shapes swept against both oracles, every accepted pair writing the reference's LowIR but one unnamed enum's mangled name; linear at 8192 spellings |
| C7 | 14.5.3p4 in every list a program writes one into, given one owner: `WrittenList` is a written list and what its entries come to, so 8.5.1's clauses, 5.2.2's arguments, 5.2.3's conversion, 5.3.4's placement and initializer, 8.3.4p3's deduced bound and 13.3.3.1.5's length all ask it once; `InitializerClauses` carries it with 8.5.1p11's cursor so an elided subaggregate reads each clause where the expansion put it; an unsettled run stands as the one entry it was written as, which leaves 14.6p8's reading a bound rather than a wrong one; 8.5.1p2's array walk became one implementation (`array_from_clauses`) driven by that cursor; and 5.2.3p2's temporary now demands 3.9p5's complete type, which is what lets `pr<int>{1,2}` stand as an argument at all.  8.5.1's aggregate-through-a-constructor half moved to its own owner beside 8.5.1's clause walk | 158 -> **162 / 184**; pa1-pa19 2169 / 2169; 20 list shapes swept against g++ and the reference with no new divergence, linear at 1 / 64 / 512 / 2048 entries, valgrind clean |
| C6 audit | the demand a prefix makes, at all three walks that make it and in all three modes that read one: `qualified_in_type` asks `require_settled_type` for a settled prefix and leaves 14.6.2p1's stand-in for a dependent one, and `resolve_prefix`'s decltype branch reports a dependent prefix rather than looking its spelling up; `id_constant` asks `decltype_qualified_name` instead of a second copy of it; the arena travels through `emit_translation_units`, so the two dump modes read such a name the way the lowering one does; and 14.6.1p1's current instantiation binds a value place as a value, which is what lets an out-of-class member definition of a class template with a non-type parameter name its own head | 156 / 182 -> **158 / 184** with two fixtures added; pa1-pa19 2169 / 2169; 134 swept shapes, every accepted pair writing the reference's LowIR |
