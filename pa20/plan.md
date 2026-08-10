# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **85 / 164** (31 spec + 133 general), from a turn-start baseline
of **39 / 164**, with pa1-pa19 at **2169 / 2169** and the file audit passing
with the five header-weight warnings it inherited.

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
evaluation is redone against each region.  The arithmetic is the one
`sema_constant.cpp` already does over a tree; what differs is where the operands
came from.  5.14p1, 5.15p1 and 5.16p1's unevaluated operand is a `live` flag
rather than a second pass, so `N == 0 ? 0 : 100 / N` reads and does not divide.

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
stand-in is made, and the declaration compares it across the evaluation.

**10p1's base is complete where the definition stands.**  14.6p8's reading asks
for no definition, because a name written in a template definition is no use of
anything - but a base class an argument list has already settled is one the
definition is laid out over.  `require_settled_type` puts the reading aside for
that one demand, so `template<class T> struct D : B<int> {}` lays out `B<int>`
as an instantiation would.

## Current Failure Map

79 failing, grouped by what would fix them:

| group | n | owner |
| --- | --- | --- |
| 14.5.3's parameter packs and pack expansions (`... does not instantiate`, `args names nothing`, six parse failures) | 30 | `sema_template.cpp`, `ast_parser_class.cpp` |
| 14.7.3's explicit specialization (`template<>` parse, stale-primary refresh, LowIR mismatches) | 13 | `ast_parser_declarator.cpp`, `sema_template.cpp` |
| deduction and overload resolution over the new places (`no declaration of f accepts the arguments of a call`) | 11 | `sema_overload.cpp` |
| 14.6.2p1's dependent qualified value/type lookups (`is written after a name that is not a namespace...`) | 6 | `sema_template.cpp` |
| 14.6.4.1's instantiation points and stale answers (`static_assert condition is false`, `sizeof names an incomplete type`) | 8 | `sema_template.cpp` |
| the rest: user-defined literals, `decltype` operands, single-test shapes | 11 | mixed |

## Active Checkpoint

**C2 - 14.7.3's explicit specialization.**  Selected because it is the largest
group whose owner is settled by C1's work and because it is what makes an
ordinary metafunction *terminate*: `fac<N>` recurses without it, and the
recursion is currently a stack overflow rather than a diagnostic.

- **Owner.**  `ast_parser_declarator.cpp` for what `template<>` declares - a
  class and not a template, which is why `box<int> b;` after it does not parse -
  and `sema_template.cpp` for the specialization table an instantiation asks
  before it reads the primary.
- **Data flow.**  `record_template` with an empty head records a
  *specialization* against the primary's `TemplateInfo`, keyed by the same
  interned argument list `instantiate_class` already keys by;
  `instantiate_class` asks that table first and reads the primary's pattern only
  where it misses.  14.7.3p6's late specialization refreshes a stale primary
  instantiation through the list the template already keeps.
- **Expected complexity.**  One hash lookup per naming, on the list id that is
  already computed - no new walk, and no scan of the specializations.
- **Validation.**  `300-*` and `400-*` in both suites, the `fac<N>` depth probe
  below, and `make test-report-through-pa19`.

## Performance Model

| shape | measured |
| --- | --- |
| 512 distinct value arguments over two templates (1024 specializations) | **0.05 s** |
| 4096 distinct value arguments over two templates (8192 specializations) | **0.45 s** |
| the same argument spelling written 4096 times | one split, 4096 lookups |

The split of an argument spelling is memoised per spelling (`value_words_`), so
one template-id written n times costs one tokenisation; the specialization
itself is already interned by argument list, so the second naming is a hash
lookup.  `open_parameter_region` is once per template, not once per naming, so
`template<class T, T v>` reads its own head's syntax once however many argument
lists bind it.

Open risk carried into C2: a recursive metafunction with no terminating
specialization overflows the machine stack instead of being diagnosed
(`fac<200>` at 0.24 s, SIGSEGV).  C2 both makes the ordinary case terminate and
owes a depth guard.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 14.1p4's non-type parameter and 14.3.2's integral argument: `TypeKind::Value` as an interned converted constant, `TemplateInfo::Parameter` as a place, 14.6.1p1's region opened once and settling every place's type, 5.19 read out of the argument spelling with 5.14/5.15/5.16's unevaluated operand, `SemaKind::TemplateValue` bound as a constant, the ABI's `<expr-primary>`, 5.8p1's shift over its own operand types, 7.1.5p9's constexpr object given 3.6.2p2's initialization, 7p4 deferred behind a counted stand-in, and 10p1's settled base completed inside 14.6p8's reading | 39 -> **85 / 164**; pa1-pa19 2169 / 2169 |
