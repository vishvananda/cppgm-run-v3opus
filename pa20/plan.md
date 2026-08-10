# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **103 / 169** - 98 of the 164 checked-in fixtures (19 / 31 spec,
79 / 133 general) and the 5 this turn's audit added under
`cppgm.tests/course/pa20` - from a turn-start baseline of **92 / 164**, with
pa1-pa19 at **2169 / 2169** and the file audit passing with the five header-weight
warnings it inherited.

The milestone gives PA19's template tier a second kind of argument.  PA19's
argument list is a list of types; 14.3.2p1's argument at a non-type place is a
*value*, and 14.4p1 makes it as much a part of what tells two specializations
apart.  So the whole tier - the specialization table keyed by an argument list,
the substitution, the object-file name, the spelling a global is named by -
keeps reading an argument list as `std::vector<TypeId>`, and the type table
grows one entry that is a converted constant rather than a type.

## Stage Design

**A value argument is a type-table entry.**  `TypeKind::Value`
(`type_model.h`) holds the type the argument was converted to and the bits it
holds, interned like a pointer or an array: `value_type(int, 3)` is one entry
however many times `f<3>` is written, so `model_.specialization_of` pairs the
two namings without knowing that either is a value.  `is_dependent` asks only
its type - the bits are settled - and `substitute` rebuilds it for
`template<class T, T v>`, where the *place's* type still names a parameter.
Nothing declares an object of one, so every switch that reaches a `default`
refuses it where a real type belongs.

**14.1p4's place says which kind of argument it takes.**
`TemplateInfo::Parameter` (`sema_template.h`) is a place rather than a name:
the name its head wrote, whether it binds a value, the syntax that says what
type that value has, the type standing for the place, and that value type over
the places before it.  `open_parameter_region` opens 14.6.1p1's region once per
template and settles all five there - which is where `template<class T, T v>`
reaches `T` - and every argument list read afterwards substitutes its own
bindings into what it found rather than reading the syntax again.  A function
template's head is read by the ordinary declaration path instead, so its places
are `SemaKind::TemplateValue` declarations and carry the same two facts on the
parameter's own type (`TypeTable::parameter_value_type`).

**5.19 is read out of the spelling, like 8.1p1's type-id is.**
`sema_value_expression.cpp` is to a value argument what `sema_type_id.cpp` is to
a type argument: 14.2 writes the argument list inside a name, so it arrives as
text.  The terminals are *recovered* rather than re-lexed - phase 7 wrote
exactly the separators that keep two of them from munching together - and the
split is a fact of the text alone, so it is kept per spelling and only the
evaluation is redone against each region.  What a `<` written in such a spelling
*is* - 14.2's list or 5.9's operator - is one question with one answer
(`sema_name.cpp`), because the three scans that split a spelling all ask it.
The arithmetic is the one `sema_constant.cpp` already does over a tree; what
differs is where the operands came from.  5.14p1, 5.15p1 and 5.16p1's
unevaluated operand is a `live` flag rather than a second pass, so
`N == 0 ? 0 : 100 / N` reads and does not divide.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it,
and a value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value` - which is exactly what 7.2p10's enumerator and 9.4.2p3's
in-class static member already are, so `id_constant`, `named_value` and the
lowering read it without a case of their own.  In the *pattern* the same place
binds no constant and its type is 14.6.1p1's parameter, so 14.6.2p2's unknown
value is the stand-in the reading already had.

**14.6p8's stand-in is counted, because 7p4 has to know.**  A `static_assert`
whose condition named something an argument list has yet to settle asserts
nothing where the pattern stands - the instantiation evaluates it again.  What
tells the two apart is not the condition's shape but whether the reading stood a
value in: `stood_in_` is a count, incremented at each of the four places a
stand-in is made and put back by any probe whose reading is discarded, and the
declaration compares it across the evaluation.

**What a reading puts aside is a header of its own.**  `sema_reading.h` holds
the records 6.6.1, 12.2p3, 14.6p8 and 11p6 each leave one reading holding while
it stands - the function it is reading, the dialect it reads in, the class a
declaration names a member of - because every one of those readings can stand
inside another and an error thrown out of the inner one has to leave the outer
where it found it.

**10p1's base is complete where the definition stands.**  14.6p8's reading asks
for no definition, because a name written in a template definition is no use of
anything - but a base class an argument list has already settled is one the
definition is laid out over.  `require_settled_type` puts the reading aside for
that one demand, so `template<class T> struct D : B<int> {}` lays out `B<int>`
as an instantiation would.

## Current Failure Map

66 failing, grouped by what would fix them:

| group | n | owner |
| --- | --- | --- |
| 14.5.3's parameter packs and pack expansions - every remaining `200-*`, including the six that do not parse | 35 | `sema_template.cpp`, `ast_parser_class.cpp` |
| 14.8.2 over the new places: deduction and overload resolution where a name is a template-id (`no declaration of ... accepts the arguments of a call`, `... is in scope`) | 11 | `sema_overload.cpp` |
| the rest: a dependent non-type place's own declarator and default, multicharacter and wide literals, four single-test shapes | 9 | mixed |
| 14.6.2p1's dependent qualified value and type lookups (`is written after a name that is not a namespace...`, `... does not name a type`) | 4 | `sema_template_head.cpp`, `sema_template.cpp` |
| three LowIR mismatches: PA19's static-member demand (recorded in `audit.md`), an enum argument's vtable, a constexpr member call in an initializer | 3 | `sema_template.cpp`, `sema_lifetime.cpp` |
| 14.5.1's variable-template shapes the suite writes anyway | 3 | `sema_template.cpp` |
| 5.19 outside the integral subset: user-defined literals | 1 | `sema_constant.cpp` |

## Active Checkpoint

This turn completed **C1** and **C2** and audited both; all three are in the
ledger below.  The next one is:

**C3 - 14.5.3's template parameter packs and pack expansions.**  Selected because
it is now every remaining `200-*` failure and over half the PA: one feature, one
owner, and the six parse failures beside it are the same feature seen from the
grammar.

- **Owner.**  `sema_template_head.cpp` for the place a pack is - one head entry
  standing for however many arguments - and `sema_template.cpp` for what an
  expansion comes to at an instantiation.  `ast_parser_class.cpp` owns the
  declarator forms that do not parse yet.
- **Data flow.**  A pack place binds a *list* rather than one argument, which
  the type table already interns (`type_list`); `bind_template_arguments` gives
  the trailing arguments to it, and an expansion written in a declaration, a
  call or an initializer is read once per element of the list the pack is bound
  to.  `sizeof...` reads that list's length.
- **Expected complexity.**  One reading per element, no rewriting of the
  pattern's syntax - the same monotonic rule the tier already keeps, so n
  elements cost n readings and not n^2.
- **Validation.**  Every `200-*` in both suites, a multiplicity sweep at 1, 2
  and 64 elements, `make test-report-through-pa19`, and a differential run of
  each new shape through `reference-binaries/cppgm++`, which accepts the PA20
  slice and is this milestone's usable second oracle.

## Performance Model

Best of five, `-O0`:

| shape | measured |
| --- | --- |
| 512 distinct value arguments over two templates (1024 specializations) | **0.09 s** |
| 4096 distinct value arguments over two templates (8192 specializations) | **0.85 s** |
| `fac<200>` / `fac<800>` metafunction chain | **0.01 / 0.06 s** |
| a 2000-deep chain instantiated but not evaluated | **0.09 s** |
| 256- / 1024-deep `s<s<...<int> >` spelling | **0.02 / 0.21 s** |
| one template-id of 1024 arguments | **0.01 s** |
| 1024 value arguments each written `(i < n)` | **0.02 s** |
| one argument spelling of 512 `+` operands | **0.00 s** |

The split of an argument spelling is memoised per spelling (`value_words_`), so
one template-id written n times costs one tokenisation; the specialization
itself is already interned by argument list, so the second naming is a hash
lookup.  `open_parameter_region` is once per template, not once per naming, so
`template<class T, T v>` reads its own head's syntax once however many argument
lists bind it.  14.7.3p1's explicit definitions are keyed by the same interned
list, so asking for one is a hash lookup on a number the caller already has.

The one shape that is not linear is the *spelling* of a deep nest, which is
quadratic in the characters it writes and is PA19's recorded shape rather than
this milestone's: a name is scanned once for every name written inside it.  A
metafunction with no terminating specialization still overflows the machine
stack rather than being diagnosed; that is a program outside the supported
slice, and a depth guard is owed whenever a checkpoint touches
`instantiate_class` again.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 14.1p4's non-type parameter and 14.3.2's integral argument: `TypeKind::Value` as an interned converted constant, `TemplateInfo::Parameter` as a place, 14.6.1p1's region opened once and settling every place's type, 5.19 read out of the argument spelling with 5.14/5.15/5.16's unevaluated operand, `SemaKind::TemplateValue` bound as a constant, the ABI's `<expr-primary>`, 5.8p1's shift over its own operand types, 7.1.5p9's constexpr object given 3.6.2p2's initialization, 7p4 deferred behind a counted stand-in, and 10p1's settled base completed inside 14.6p8's reading | 39 -> **85 / 164**; pa1-pa19 2169 / 2169 |
| C2 | 14.7.3's explicit specialization: a `template<>` head declaring the specialization and no template - so the name it wrote is a class-name and a later `box<int> b;` parses - with the class body read in place of the pattern against no bindings, the function body run in place of the pattern's, and both keyed by the interned argument list the specialization is already found by | 85 -> **92 / 164**; `fac<200>` SIGSEGV -> **0.01 s** |
| C1, C2 audit | the spelling a value argument arrives as: 14.2's `<` told apart from 5.9's and 5.8's in the one scan all three readers now share, 3.4.3p1's rooted name read by that scan in both of them, 4.12p1 given to the conversion 14.3.2p5 makes an argument a converted constant by, 5.2.3p1/p3's functional and braced notation folded where `(T)x` already was, 8.5p16 and 8.5.4p3's initializers read for 5.19p3's constant, and 14.6p8's count put back by a discarded probe | 92 / 164 -> **103 / 169** with five fixtures added; pa1-pa19 2169 / 2169 |
