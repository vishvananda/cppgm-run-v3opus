# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **235 / 301** (65 spec + 234 general + 2 course), from a
turn-start baseline of 227 / 299, with pa1-pa18 at **1777 / 1777** and the file
audit passing with the five header-weight warnings it inherited. The whole
pa1-pa19 report runs in 10.3 s.

The milestone gives the PA16-PA18 object model its first template tier: a
template-declaration records a pattern instead of declaring anything, and
14.7.1p1's instantiation is that same pattern read once more against a region
that binds each parameter to its argument. Nothing is substituted into syntax
and no text is replayed, so the ordinary PA11-PA18 machinery settles a
specialization exactly as it settles a class the program wrote out.

Three facts about the harness shape what has to be right, read out of
`scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape.
- **Top-level entries are sorted**, so emission order never matters;
  instruction order, global item order and vtable slot order do.
- **`object=`, `binding=`, `linkage=` and every `alias object` line are
  stripped before the comparison.** The object file's own name for a
  specialization, and whether two units may both define it, are therefore
  requirements no fixture can fail on - they are checked by regenerating every
  `.ref` from `reference-binaries/cppgm++` and by sweeping the emitted symbols
  against it and against g++.

## Stage Design

- **`sema_template.cpp` owns the whole tier**, in the three steps 14 splits it
  into: the template-argument list is read where a name is turned back into
  what was written, the bindings are a region, and the specialization is one
  declaration however many times it is named.
- **A template is a pattern, not a declaration.** `TemplateInfo`
  (`sema_template.h`) is the syntax the template-declaration parameterises, the
  region it was written in, the parameters its head declared with 14.1p9's
  defaults beside them, 14.5.1.3p1's out-of-class member definitions, and the
  specializations made so far. It hangs off the declaration
  (`SemaEntity::templated`), because the declaration is what a use finds.
  `record_template` writes it under `SemaDialect::Lowering` only: PA11 and PA12
  describe what a template-declaration *says* and instantiate nothing.
- **Instantiation is a second reading of the pattern, not a copy of it.**
  `instantiate_class` makes the declaration, and `complete_specialization`
  reads the body against a `ScopeKind::TemplateParameters` region whose
  bindings are typedef-names of the argument types. The declaration and the
  completion are apart because their points are: 14.7.1p1 lets a specialization
  be named before its template is defined, and a dependent argument list
  (`TypeTable::is_dependent`) makes a declaration and no body at all.
- **A specialization is bound to no name.** It is reached from the template-id
  that wrote its arguments, so ordinary lookup keeps finding the template.
  14.6.1p1's injected-class-name is the specialization, and a
  template-argument-list after it names the template it was made of.
- **Substitution belongs to the walk, not to the type table.** Every category a
  type is only made of types is rebuilt by `TypeTable::substitute`; a
  specialization is the one that is not, because `A<T>` with `T` bound to `int`
  is a class only an instantiation can make. `SemaAnalyzer::substituted` walks
  the type and delegates the rest.
- **Function templates take the same three steps.** The pattern is recorded on
  the declaration the ordinary path makes; `specialize`/`deduce_specialization`
  make the declaration; `instantiate` reads the body against the bindings.
  14.5.6.1p5's two declarations of one template write types that differ, so
  `equivalent_template` asks the question by putting one head's parameters in
  place of the other's - the chain's index of parameter type lists cannot.
- **Deduction is over the P/A pairs the *call* wrote, and nothing else.**
  14.8.2.5p3 leaves a parameter written over no template parameter deducing
  nothing, so whether the argument reaches it is 13.3's question about a
  conversion; 8.3.6p1's unwritten trailing arguments deduce nothing either;
  13.3.1.2p4's first operand is a non-member operator candidate's own first
  argument; and 14.8.2.1p6's overload set is tried one declaration at a time,
  with two that both deduce leaving a non-deduced context rather than a
  failure. 14.8.2.2's target type is the one pair a whole function type makes.
  14.8.2.1p3 is what makes a reference of the argument rather than of the
  parameter: an rvalue reference over an unqualified parameter takes an lvalue's
  type as an lvalue reference, and 8.3.2p6 collapses the two.
- **13.3.3.2p3 carries two different clauses in one field.** A reference
  binding is ordered by how qualified it made the object and every other
  sequence by what a qualification conversion made of a pointer, so a sequence
  of one kind is ordered against one of the other by neither - which is what
  leaves `f(T)` against `f(const T &)` to 14.5.6.2's ordering of the two
  templates, memoised per pair of declarations.
- **14.5.6.2's ordering is one question with two readers.** 13.3.3p1's tie
  between two specializations and 13.4p1's target type both ask which template
  is more specialized, so `more_specialized` answers it once: p5 and p7 strip
  the reference and the qualifiers, `at_least_as_specialized` deduces each
  list from the other and is memoised per pair, and p9 orders by what those two
  clauses took off - which is the only thing left to tell `f(T &)` from
  `f(const T &)` by.
- **Whether two declarations declare one template is a fact of each of them.**
  14.5.6.1p5's types differ because each head declared parameters of its own, so
  each declaration's *signature* - its type with every parameter standing for
  the place its head declared it in - is computed once and the chain is walked
  by comparing types. Asking it of a pair instead costs a substitution per pair,
  and over a class template a specialization per pair as well.
- **A definition is read where it stands as well as where it is used.**
  14.6p8 makes a template definition ill-formed - no diagnostic required - where
  no valid specialization could be generated from it, so
  `check_template_definition` reads the body once at its own point and
  `instantiate_body` reads it again for each specialization.  The first reading
  is the *PA11* one: a type that depends on a parameter has no layout, no
  conversion and no overload set until an argument arrives, so what the pattern
  can be asked is what its declarations say and 3.4p1's lookup of the names it
  writes.  14.6.2p1's member name, 14.2's template-id and 3.4.2p2's callee are
  the three the instantiation is left.
- **The reading leaves nothing behind.**  Its lines stand in a dump nothing
  reads, and 14.7.1p1 makes a template-id it names a declaration rather than a
  use requiring a definition - so `instantiate_class` makes the specialization
  and completes it where an instantiation asks.  `templating()` is what says the
  template layer answers during it, where `lowering()` used to.
- **One body's facts belong to that body.**  `FunctionReading` puts aside what
  the reading around it knew, because naming a specialization in the middle of a
  body is what asks for another body to be read; `DialectReading` does the same
  for which of the three dialects the walk is in.
- **A template parameter is redeclared by a fact of the regions.**  14.6.1p6
  refuses a declaration of a parameter's name anywhere in the template, so the
  question is asked where a name is bound and the template-parameter regions
  standing over a region are chained as they are opened - it costs the number of
  template heads above the declaration and not the block nesting it happens to
  be written at.
- **A class may not declare a member type twice.**  7.1.3p3 lets a namespace
  redeclare a typedef-name for the same type and 9.2p1 does not let a class
  declare a member twice, so `declare_type_alias` is where both are asked;
  7.1.3p6's redefinition of a *class*-name is the one a class does allow.
- **A specialization has a second point of instantiation at the end of the
  unit.** 14.6.4.1p1: the pending entry a name leaves is settled where the walk
  reaches it, so the definition the template has by the end of the unit is the
  one the specialization stands for however far above it the name stands.
- **An object-file name is walked, never split.** `lowir_abi.cpp` builds the
  components of every encoded name from the *declaration's own regions*, and
  what a specialization is named by is two facts - the template's own qualified
  name and the argument `TypeId`s - because the ABI writes them apart.
- **14.7.1p1's definition is nobody's.** `LowirUnitLowering::shared_definition`
  answers one question for the three that follow from it: what the object file
  binds the symbol as, which of 12.1's entry points the definition owes, and
  whether 3.2p3 waits for a use before writing it.
- **A type-id's spelling is read as a declarator, not as a word list.**
  `split_type_id` keeps a name whole, and
  `type_id_words`/`abstract_declarator_words`/`suffix_words` read 8.1p1's
  type-specifier-seq and 8.3p1's abstract-declarator from what is left.

## Current Failure Map

66 of 301 fail. Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| 19 | the class template's pattern read where it stands | 14.6.1p1's current instantiation: `typename base<T>::type`, a member alias of the current specialization, a nested type named through an out-of-class definition, a decltype over a qualified argument, and the five `-bad` fixtures whose declaration stands in a class body or an out-of-class member definition rather than in a function template's |
| 21 | exit 0, LowIR differs | the point of instantiation - a member body instantiated where the reference leaves it out, and one left out where it writes it; a static data member's in-class constant read as storage; 12.8p12 over a reference member; a variable template's partial specialization written `@v_T_T_` |
| 6 | syntax PA10 does not parse | 14.7.2's `template struct A<int>;`, a function template-id in an expression an ordinary `<` could open, a member array-reference return |
| 5 | the declaration context a qualified template declarator is parsed in | a namespace-qualified definition, a later redeclaration's default argument, a local declaration that a member call must beat |
| 15 | the long tail | 14.8.1p2's partial explicit argument list, a using-declaration of a dependent base, `alignas` over an instantiation, a move-only by-value argument, an unused conversion function's body |

The first group is one behaviour and is the largest: 14.6.1p1's current
instantiation is what a class template's body has to be read against, and the
five remaining `-bad` fixtures are accepted for want of it.

## Next Substantial Checkpoint

**C5 - the class a template makes of its own parameters**: 14.6.1p1 names the
current instantiation, and 14.6p8's reading of a class template's definition is
that class.  It is what the 19-fixture group above is all of: a member alias of
the current specialization, a nested type named through an out-of-class
definition, `typename base<T>::type`, and the declarations the five accepted
`-bad` fixtures write in a class body or an out-of-class member definition.

- **owner**: `sema_template.cpp`.  `open_pattern_parameters` opens the region
  binding each parameter to a type standing for itself - the same region an
  out-of-class member definition is read against - and the pattern class is the
  class `class_declaration` makes of the template's body in it.  Both hang off
  `TemplateInfo`, because 14.5.1.3p1's member definitions arrive after the class
  is read and are read against the same two.
- **data flow**: `record_template` records the pattern -> the pattern class is
  read once, in the PA11 dialect, with 9.2p2's member bodies held until the
  class is closed -> a name that reaches a dependent base or a dependent type
  is left alone -> `complete_specialization` reads the same syntax again against
  the arguments, unchanged.
- **expected complexity**: one extra reading per class template *definition*
  against one per specialization today, so the cost is linear in the source and
  independent of how many specializations are made.
- **known obstacle**: a first attempt at this cost 34 fixtures, in three
  groups - a member named in a body before the class declared it (9.2p2's
  complete-class context, which the PA11 walk reads inline), a dependent base's
  name (`split<T> does not name a type`), and a qualified name through a
  dependent type (`value_type is written after a name that is not a namespace`).
  The first is a deferral and the other two are 14.6.2p1, so the checkpoint has
  to carry the dependent-name half with it rather than after it.
- **validation**: the five accepted `-bad` fixtures, then 14.6.2p3's unqualified
  name that must skip a dependent base, then the pa19 report and pa1-pa18.

## Performance Model

The dominant operation is one reading of one pattern per specialization, which
is linear in the pattern. What is superlinear is superlinear in the *program*.

Measured on the shapes the tier makes scaling-sensitive, each timed twice,
`cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n distinct specializations of one class template, each with a member function | 0.01 s | 0.01 s | 0.02 s | 0.05 s | 0.10 s |
| one specialization named n times | 0.01 s | 0.01 s | 0.01 s | 0.01 s | 0.03 s |
| an n-member class template, four specializations, `sizeof` of each | 0.00 s | 0.00 s | 0.01 s | 0.01 s | 0.01 s |
| an n-deep nest of template-ids as arguments | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.06 s |
| n deductions of one function template over n distinct classes | 0.01 s | 0.01 s | 0.02 s | 0.05 s | 0.09 s |
| n calls deducing one specialization from a `W<int>&` parameter | 0.00 s | 0.01 s | 0.01 s | 0.01 s | 0.02 s |
| n classes a specialization declares, each a parameter type of a member | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.05 s |
| two templates that tie, ordered by 14.5.6.2 at n call sites | 0.01 s | 0.01 s | 0.01 s | 0.01 s | 0.02 s |
| an overload set of n declarations deduced against a template parameter | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.04 s |
| n declarations of one template name, none of them called | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.04 s |
| n specializations each named above the definition of its template | 0.01 s | 0.01 s | 0.02 s | 0.03 s | 0.06 s |
| an n-deep specialization as an ADL argument, named n times | 0.01 s | 0.01 s | 0.02 s | 0.05 s | - |
| **n function templates overloading one name, each called once** | 0.01 s | 0.01 s | 0.03 s | 0.06 s | 0.18 s |
| **n target types each choosing among n function templates** | 0.01 s | 0.01 s | 0.03 s | 0.06 s | 0.15 s |

The second and the sixth are what say the memos work; the eighth is the
ordering memo, kept per pair of declarations, so n call sites cost what one
does. The tenth is what the signature answers: whether two declarations declare
one template is a fact of each declaration, so declaring the nth overload of a
template name costs what declaring the nth ordinary overload does - it was 0.36 s
at n = 512 and 1.57 s at n = 1024 when the question was asked of every pair, and
each of those pairs instantiated a specialization where a parameter was written
over a class template (44.8 MB against 15.8 MB now).

The last two are quadratic and are 13.3p1's own shape: a call - and a target
type - gathers every declaration of the name, so n of them over an
n-declaration chain are n^2 candidates however they are ranked. The ADL nest is
linear in the source and the walk over a specialization's template arguments is
0.03 s of the 0.05 s at n = 256.

**One shape is exponential and it is the spelling.** `typedef P<t,t>` repeated
n times names a class whose written-out spelling doubles at every level, and a
specialization is named by that spelling: 0.01 s, 0.16 s, 0.62 s and 2.50 s at
n = 12, 16, 18 and 20 in 23 lines of source. `reference-binaries/cppgm++` is
0.19 s, 2.85 s, 12.28 s and **46.31 s** on the same inputs, so this is the
milestone's shape rather than the tier's; g++ does n = 20 in 0.06 s because it
never materialises the spelling. Fixing it means not storing a specialization's
written-out name at all.

14.6p8's reading of a definition is one reading per template *definition* and
not per specialization, so what it adds is linear in the source however many
specializations the unit makes:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n function templates, each an 8-statement body, none of them called | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.03 s |
| one function template of n statements, one specialization | 0.00 s | 0.01 s | 0.01 s | 0.01 s | 0.02 s |
| n nested blocks in a function template, each declaring a name | 0.00 s | 0.01 s | 0.01 s | 0.02 s | 0.04 s |
| a class template of n member typedefs, one specialization | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.01 s |
| n specializations of one function template over n classes | 0.01 s | 0.01 s | 0.02 s | 0.04 s | 0.09 s |

The third is what 14.6.1p6 is measured by: the question is asked of every
declaration in every program, so a walk outwards would cost the block nesting
the declaration is written at.  Chaining the template-parameter regions as they
are opened makes it cost the number of template heads instead - at 1024 nested
blocks 0.10 s -> **0.08 s**, which is what the same nest without a template head
over it costs, so the check is no longer measurable.

Valgrind is clean over all 299 fixtures, over the multi-unit programs and over
the scaling shapes, and the whole pa1-pa19 report is 10.3 s.  Two of the rules
this checkpoint added have no fixture of their own, so `cppgm.tests/course/pa19`
holds one each - 14.6.1p6 over an alias-declaration in an uninstantiated
function template body, and 9.2p1's member type declared twice beside the
namespace redeclaration 7.1.3p3 allows - regenerated through `make ref-test`.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern a template-declaration parameterises, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading of that pattern against a bindings region, 14.1p9's defaults, 14.6.1p1's injected-class-name, the ABI's `<template-args>` on a specialization and on its members, 12.1p1's constructor named by the template-name, and 8.1p1's type-id read as a declarator; `sema_value.h` and `sema_template.h` split out of `sema_analyzer.h` | 27 -> **114 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 | the function tier and what a name cannot spell: a function template's pattern recorded on the declaration the ordinary path makes and read again for the specialization that named it; the ABI's `<template-args>`, result type and `T_` signature; 14.5.1.3p1's out-of-class member definitions read once the specialization's body is complete; a static data member of a specialization named by handing PA14's encoder the components a data name's one spelling cannot be split into | 114 -> **172 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 completion | the two points a specialization has, and what a dependent argument list names: the declaration made where the template-id stands and the body read where the definition is; 14.6.2p1's dependent argument list making a declaration and no body; `SemaAnalyzer::substituted` owning what `TypeTable::substitute` cannot rebuild; 14.8.2.1p2's reference parameter and 14.8.2.5p4's `A<T>` against `A<int>` | 172 -> **194 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| tier audit | the object file's name for a specialization, which this suite cannot see: the components of an encoded name walked out of the declaration's own regions rather than split out of a spelling, each owning class asked where its own declaration stands, a class named through a specialization written as a member type, the ABI's `<template-param>` made a substitution candidate, and 14.7.1p1's shared definition answered once for the three readers that follow from it; 14.6.2p1's answer kept per type | 194 -> **200 / 295**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 -> 9 tests; 13 names byte-identical to g++; valgrind clean |
| C3 | the call a template joins: 14.8.2.5p3's parameter written over no template parameter, 14.8.2.1p4's reference top, 8.3.6p1's unwritten trailing arguments read against the arguments the specialization was made from, 13.3.1.2p4's first operand and 13.4p1's overloaded name left standing where an operator gathers its candidates, 14.8.2.2's target type, 14.8.2.1p6's overload set, 3.4.2p2's template arguments, 13.5p6 asked of the specialization rather than the template, 14.5.6.2's ordering of two templates whose conversions tie, 14.5.6.1p5's equivalent declarations, and 8.5.3p5's temporary for a literal a reference binds; 8.5.1's aggregate walk and 8.5.4's list-initialization split into `sema_init_list.cpp` | 200 -> **223 / 295**; pa1-pa18 1777 / 1777; file audit passes; valgrind clean over the newly reached paths and the four new scaling shapes |
| C3 audit | the declaration a target type chooses, the clauses an ordering strips, and the definition a name written above it still gets: 14.5.6.2p4's ordering as one question with 13.3.3p1's tie and 13.4p1's target as its two readers, 14.5.6.2p9 and p10 beneath the p5 and p7 that strip what they order by, 14.8.2.1p3's lvalue reference collapsed by 8.3.2p6, 5.3.3p2 and 5.3.6p3 over a reference type-id, 14.6.4.1p1's point of instantiation at the end of the unit for a specialization named above its template's definition, and 14.5.6.1p5's equivalence as a signature each declaration has on its own | 223 -> **227 / 299**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 60 run-and-compare programs agreeing with `reference-binaries/cppgm++` and g++; declaring 512 overloads of one template name 0.36 s -> 0.04 s and 44.8 MB -> 15.8 MB; valgrind clean |
| C4 | the reading a template definition gets where it stands: 14.6p8's body read once at its own point in the PA11 dialect and again for each specialization, with 14.6.2p1's member name, 14.2's template-id and 3.4.2p2's callee left to the instantiation; 14.7.1p1's naming made a declaration and not a use, so a specialization is completed where an instantiation asks; 14.6.1p6's redeclared template parameter as a fact of the chained template-parameter regions; 9.2p1's member type declared twice against 7.1.3p3's namespace redeclaration; `FunctionReading` and `DialectReading` over the three readings of one body; `sema_declaration.h` and `sema_function.cpp` split out | 227 / 299 -> **235 / 301**; pa1-pa18 1777 / 1777; file audit passes; `reference-binaries/cppgm++` and g++ agree on every new rejection except one the reference accepts and g++ refuses (14.6.1p6 over `typedef int T`); 1024 nested blocks under a template head 0.10 s -> 0.08 s; valgrind clean |
