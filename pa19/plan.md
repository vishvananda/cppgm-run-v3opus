# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **227 / 299** (65 spec + 234 general), from a turn-start baseline
of 223 / 295, with pa1-pa18 at **1777 / 1777** and the file audit passing with
the five header-weight warnings it inherited. The whole pa1-pa19 report runs in
10.2 s.

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

72 of 299 fail. Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| 22 | exit 0, LowIR differs | the point of instantiation - a member body instantiated where the reference leaves it out, and one left out where it writes it; a static data member's in-class constant read as storage; 12.8p12 over a reference member; one that fails the harness's own validator, where a variable template's partial specialization is written `@v_T_T_` |
| 14 | dependent names and the current instantiation | `typename base<T>::type`, a member alias of the current specialization, a nested type named through an out-of-class definition, a decltype over a qualified argument |
| 11 | a template body checked where it stands | 14.6p8: the `-bad` fixtures that redeclare an active template parameter, name a type where a value is wanted, or name nothing at all, and are **accepted** because nothing reads the body until an instantiation asks |
| 6 | syntax PA10 does not parse | 14.7.2's `template struct A<int>;`, a function template-id in an expression an ordinary `<` could open, a member array-reference return |
| 5 | the declaration context a qualified template declarator is parsed in | a namespace-qualified definition, a later redeclaration's default argument, a local declaration that a member call must beat |
| 14 | the long tail | 14.8.1p2's partial explicit argument list, a using-declaration of a dependent base, `alignas` over an instantiation, a move-only by-value argument, an unused conversion function's body |

## Next Substantial Checkpoint

**C4 - the reading a template body gets where it stands**: 14.6p8 makes a
template definition ill-formed where no valid specialization could be generated
and no diagnostic is required only for the parts that depend on a parameter, so
the body is read once at its own point and again for each specialization. It is
the largest single group the failure map has that is one behaviour rather than a
tail, and 11 fixtures are **accepted** today for want of it.

- **owner**: `sema_template.cpp` reads the pattern under the template-parameter
  region as it does today, with a dialect that declares nothing and asks the
  ordinary PA11-PA12 questions of every name that does not depend on a
  parameter; `TypeTable::is_dependent` is already the fact that says which.
  `sema_analyzer.cpp:function_definition` is where the reading is skipped today
  (`target.scope->kind == ScopeKind::TemplateParameters && specializing == 0`).
- **data flow**: `record_template` holds the pattern -> the definition is read
  once against the parameters themselves -> a name that reaches a dependent
  base or a dependent type is left alone -> `complete_specialization` reads it
  again against the arguments, unchanged.
- **expected complexity**: one extra reading per template *definition* against
  one per specialization today, so the cost is linear in the source and
  independent of how many specializations are made. The reading is over the
  pattern the declaration already holds, so nothing is reparsed.
- **validation**: the 11 accepted `-bad` fixtures, then 14.6.2p3's unqualified
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

Valgrind is clean over all 299 fixtures, over the multi-unit programs and over
the scaling shapes, and the whole pa1-pa19 report is 10.2 s.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern a template-declaration parameterises, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading of that pattern against a bindings region, 14.1p9's defaults, 14.6.1p1's injected-class-name, the ABI's `<template-args>` on a specialization and on its members, 12.1p1's constructor named by the template-name, and 8.1p1's type-id read as a declarator; `sema_value.h` and `sema_template.h` split out of `sema_analyzer.h` | 27 -> **114 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 | the function tier and what a name cannot spell: a function template's pattern recorded on the declaration the ordinary path makes and read again for the specialization that named it; the ABI's `<template-args>`, result type and `T_` signature; 14.5.1.3p1's out-of-class member definitions read once the specialization's body is complete; a static data member of a specialization named by handing PA14's encoder the components a data name's one spelling cannot be split into | 114 -> **172 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 completion | the two points a specialization has, and what a dependent argument list names: the declaration made where the template-id stands and the body read where the definition is; 14.6.2p1's dependent argument list making a declaration and no body; `SemaAnalyzer::substituted` owning what `TypeTable::substitute` cannot rebuild; 14.8.2.1p2's reference parameter and 14.8.2.5p4's `A<T>` against `A<int>` | 172 -> **194 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| tier audit | the object file's name for a specialization, which this suite cannot see: the components of an encoded name walked out of the declaration's own regions rather than split out of a spelling, each owning class asked where its own declaration stands, a class named through a specialization written as a member type, the ABI's `<template-param>` made a substitution candidate, and 14.7.1p1's shared definition answered once for the three readers that follow from it; 14.6.2p1's answer kept per type | 194 -> **200 / 295**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 -> 9 tests; 13 names byte-identical to g++; valgrind clean |
| C3 | the call a template joins: 14.8.2.5p3's parameter written over no template parameter, 14.8.2.1p4's reference top, 8.3.6p1's unwritten trailing arguments read against the arguments the specialization was made from, 13.3.1.2p4's first operand and 13.4p1's overloaded name left standing where an operator gathers its candidates, 14.8.2.2's target type, 14.8.2.1p6's overload set, 3.4.2p2's template arguments, 13.5p6 asked of the specialization rather than the template, 14.5.6.2's ordering of two templates whose conversions tie, 14.5.6.1p5's equivalent declarations, and 8.5.3p5's temporary for a literal a reference binds; 8.5.1's aggregate walk and 8.5.4's list-initialization split into `sema_init_list.cpp` | 200 -> **223 / 295**; pa1-pa18 1777 / 1777; file audit passes; valgrind clean over the newly reached paths and the four new scaling shapes |
| C3 audit | the declaration a target type chooses, the clauses an ordering strips, and the definition a name written above it still gets: 14.5.6.2p4's ordering as one question with 13.3.3p1's tie and 13.4p1's target as its two readers, 14.5.6.2p9 and p10 beneath the p5 and p7 that strip what they order by, 14.8.2.1p3's lvalue reference collapsed by 8.3.2p6, 5.3.3p2 and 5.3.6p3 over a reference type-id, 14.6.4.1p1's point of instantiation at the end of the unit for a specialization named above its template's definition, and 14.5.6.1p5's equivalence as a signature each declaration has on its own | 223 -> **227 / 299**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 60 run-and-compare programs agreeing with `reference-binaries/cppgm++` and g++; declaring 512 overloads of one template name 0.36 s -> 0.04 s and 44.8 MB -> 15.8 MB; valgrind clean |
