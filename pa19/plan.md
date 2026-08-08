# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **194 / 293** (65 spec + 228 general), from a turn-start baseline
of 27, with pa1-pa18 at **1777 / 1777** and the file audit passing with the five
header-weight warnings it inherited. The whole pa1-pa19 report runs in 15.3 s.

The milestone gives the PA16-PA18 object model its first template tier: a
template-declaration records a pattern instead of declaring anything, and
14.7.1p1's instantiation is that same pattern read once more against a region
that binds each parameter to its argument. Nothing is substituted into syntax
and no text is replayed, so the ordinary PA11-PA18 machinery settles a
specialization exactly as it settles a class the program wrote out.

Two facts about the harness shape what has to be right, both read out of
`scripts/compare_results_common.pl` and carried forward from PA18:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape. So an internal LowIR name like `@Box_int___get` is a
  presentation tie-breaker, but the ABI name a specialization is given is part
  of the pairing, and every vtable / RTTI / typeinfo-name spelling is compared
  literally.
- **Top-level entries are sorted**, so emission order never matters;
  instruction order, global item order and vtable slot order do.

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
- **The class template's name binds in the region the template stands in.**
  Before this stage `class_declaration` bound it in the template-parameter
  region, where no use outside could reach it.
- **Instantiation is a second reading of the pattern, not a copy of it.**
  `instantiate_class` makes the declaration, and `complete_specialization`
  reads the body against a `ScopeKind::TemplateParameters` region whose
  bindings are typedef-names of the argument types, handing the pattern to the
  ordinary `class_declaration` with two extra facts: the declaration already
  made, and the name the template-id gave it. Every name the body writes is
  then looked up with the arguments already in hand, so a dependent member
  type, a dependent base and a member function body all settle through the
  PA16-PA18 path with no template-aware code in it.
- **The declaration and the completion are apart, because their points are.**
  14.7.1p1 lets a specialization be named before its template is defined:
  the declaration is an incomplete class, which is all a pointer, a reference
  or a typedef of it needs, and the definition completes every specialization
  already made where it arrives.
- **A specialization is held before its body is read**, so a template whose
  body names its own specialization finds the declaration rather than starting
  a second reading of it.
- **A dependent argument list makes a declaration and no body.** 14.6.2p1's
  `A<T>` written inside the template over its own parameters names no class
  yet: `TypeTable::is_dependent` is what says so, and it is what keeps a
  pattern's own signature from instantiating nonsense.
- **A specialization is bound to no name.** It is reached from the template-id
  that wrote its arguments, so ordinary lookup keeps finding the template.
  `template_id_entity` is the one place a spelling becomes one, and it is asked
  only where the ordinary lookup found nothing - in `resolve` for both
  spellings of a name, and at every component of `resolve_prefix`.
  14.6.1p1's injected-class-name is the specialization, and a
  template-argument-list after it names the template it was made of.
- **Substitution belongs to the walk, not to the type table.** Every category
  a type is only made of types is rebuilt by `TypeTable::substitute`; a
  specialization is the one that is not, because `A<T>` with `T` bound to `int`
  is a class only an instantiation can make. `SemaAnalyzer::substituted` walks
  the type and delegates the rest.
- **Function templates take the same three steps.** The pattern is recorded on
  the declaration the ordinary path makes; `specialize`/`deduce_specialization`
  make the declaration; `instantiate` reads the body against the bindings, with
  `declare_function` handed the declaration rather than making another.
  14.8.2.1p2's reference parameter deduces from the type it refers to, and
  14.8.2.5p4 matches `A<T>` against `A<int>` on the two facts a specialization
  records.
- **What a specialization is named by is two facts, not one spelling.**
  `TypeTable` records the template's own qualified name and the argument
  `TypeId`s, and `SemaEntity::template_arguments` records the interned list a
  function-template specialization was made from - because the ABI writes them
  apart, `_ZN3BoxIiE3getEv` and `_Z5call0IK4fobjEiT_`, and neither `Box<int>`
  nor the function's type can be split back into them. 8.3.5p5 drops a
  top-level cv-qualifier from a parameter, so two specializations of one
  function template can share a function type and only the arguments tell them
  apart - which is why the ABI writes the template's signature, where a
  parameter stands for itself as `T_`.
  `lowir_abi.cpp` reads those facts at the three places a name reaches a class:
  `abi_type` for the type, `build_function_name` for a member or a function
  specialization, and `build_data_name` for a static data member - each asking
  the *class* rather than the spelling, by walking the declaration's own region.
- **A type-id's spelling is read as a declarator, not as a word list.**
  `split_type_id` keeps a name whole - a qualified name, a nested
  template-argument-list, a decltype-specifier's parentheses belong to the
  component around them - and `type_id_words`/`abstract_declarator_words`/
  `suffix_words` read 8.1p1's type-specifier-seq and 8.3p1's
  abstract-declarator from what is left, so `void(int)`, `int(*)(char)`,
  `std::uint32_t[4]` and `V<V<int>*>` are arguments this milestone reads.
- **A default template-argument is read in a region that already binds the
  parameters before it** (14.1p9), and the whole list one list of explicit
  arguments makes is kept.
- **12.1p1's constructor name is the injected one**: a specialization's
  constructor is what its pattern spelled with the template-name.

## Current Failure Map

99 of 293 fail. Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| ~35 | exit 0, LowIR differs | mostly the point of instantiation: a member body instantiated eagerly that the reference leaves out, and the vague linkage a specialization's static data member binds with |
| ~25 | dependent names | `typename base<T>::type`, a name behind a dependent base at definition time, `sizeof` of a specialization still incomplete |
| ~20 | overload resolution over templates | template-backed operator overloads, an explicit template-id in an overload set, a call whose candidates mix templates and non-templates |
| 6 | not a translation unit | PA10 does not parse `template struct A<int>;` (14.7.2's explicit instantiation) nor a template-id whose name a declaration already made a *value* - `choose<int>(0)` |
| ~13 | the long tail | ADL at the point of instantiation, a using-declaration of a dependent base, a local class in a function template |

## Active Checkpoint

**C3 — the point of instantiation**: 14.7.1p1 instantiates a member function of
a class template only where a use asks, and 14.6.4.1's point is where that use
stands.

- **owner**: `sema_template.cpp` records each member's pattern on the
  specialization instead of reading it with the class; `name_function` is
  already the one place a use of a function is noted, and is what asks.
- **data flow**: `complete_specialization` reads the member *declarations* ->
  a use reaches the member -> its body is read against the same bindings
  region -> the ordinary pending-definition path writes it.
- **expected complexity**: one reading per member actually used, against one
  per member declared today; the bindings region is rebuilt from the arguments
  the specialization records, so nothing is held between the two.
- **validation**: the LowIR-differs group above, then the pa19 report and
  pa1-pa18.

## Performance Model

The dominant operation is one reading of one pattern per specialization, which
is linear in the pattern. What is superlinear is superlinear in the *program*.

Measured on the six shapes the tier makes scaling-sensitive, each timed twice,
`cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n distinct specializations of one class template, each with a member function | 0.04 s | 0.04 s | 0.06 s | 0.12 s | 0.31 s |
| one specialization named n times | 0.02 s | 0.01 s | 0.03 s | 0.04 s | 0.08 s |
| an n-member class template, four specializations, `sizeof` of each | 0.01 s | 0.01 s | 0.01 s | 0.02 s | 0.02 s |
| an n-deep nest of template-ids as arguments | 0.01 s | 0.02 s | 0.04 s | 0.07 s | 0.19 s |
| n deductions of one function template over n distinct classes | 0.03 s | 0.03 s | 0.08 s | 0.13 s | 0.17 s |
| n calls deducing one specialization from a `W<int>&` parameter | 0.01 s | 0.01 s | 0.01 s | 0.01 s | 0.02 s |

All six are linear in the source. The second and the sixth are what say the
memos work: 512 namings of `Box<int>` and 512 deductions of one specialization
each cost what one does plus the declarations they make. Valgrind is clean over
the instantiating path, and the whole pa1-pa19 report is 15.3 s.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern a template-declaration parameterises, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading of that pattern against a bindings region, 14.1p9's defaults read where they may name the parameters before them, 14.6.1p1's injected-class-name, the ABI's `<template-args>` on a specialization and on its members, 12.1p1's constructor named by the template-name, and 8.1p1's type-id read as a declarator so a function type, an array and a nested template-id are arguments this milestone reads; `sema_value.h` and `sema_template.h` split out of `sema_analyzer.h` | 27 -> **114 / 293**; pa1-pa18 1777 / 1777; file audit passes; four scaling shapes to 512 |
| C2 | the function tier and what a name cannot spell: a function template's pattern recorded on the declaration the ordinary path makes and read again for the specialization that named it, with `declare_function` handed that declaration rather than making another; the ABI's `<template-args>`, result type and `T_` signature, which is what tells `call0<fobj>` from `call0<fobj const>`; 14.5.1.3p1's out-of-class member definitions recorded on the template and read once the specialization's body is complete, including for one already made; a static data member of a specialization named by handing PA14's encoder the components a data name's one spelling cannot be split into | 114 -> **172 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 completion | the two points a specialization has, and what a dependent argument list names: the declaration made where the template-id stands and the body read where the definition is - so a specialization named before its template was defined is an incomplete class the definition then completes; 14.6.2p1's dependent argument list making a declaration and no body; `SemaAnalyzer::substituted` owning what `TypeTable::substitute` cannot rebuild; 14.8.2.1p2's reference parameter deducing from the type it refers to and 14.8.2.5p4 matching `A<T>` against `A<int>` | 172 -> **194 / 293**; pa1-pa18 1777 / 1777; file audit passes; six scaling shapes to 512, valgrind clean, pa1-pa19 report 15.3 s |
