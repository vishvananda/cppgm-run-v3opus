# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **114 / 293** (65 spec + 228 general), from a turn-start baseline
of 27, with pa1-pa18 at **1777 / 1777** and the file audit passing with the five
header-weight warnings it inherited.

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
  region it was written in, and the parameters its head declared, with 14.1p9's
  defaults beside them. It hangs off the declaration
  (`SemaEntity::templated`), because the declaration is what a use finds.
  `record_template` writes it under `SemaDialect::Lowering` only: PA11 and PA12
  describe what a template-declaration *says* and instantiate nothing, so their
  walk is untouched.
- **The class template's name binds in the region the template stands in.**
  Before this checkpoint `class_declaration` bound it in the
  template-parameter region, where no use outside could reach it, which is why
  every `Box<int>` failed as "no declaration of Box<int> is in scope".
- **Instantiation is a second reading of the pattern, not a copy of it.**
  `instantiate_class` opens one `ScopeKind::TemplateParameters` region whose
  bindings are typedef-names of the argument types, and hands the pattern to
  the ordinary `class_declaration` with two extra facts: the declaration it
  already made for the specialization, and the name the template-id gave it.
  Every name the body writes is then looked up with the arguments already in
  hand, so a dependent member type, a dependent base and a member function body
  all settle through the PA16-PA18 path with no template-aware code in it.
- **The specialization is held before its body is read.** A class template
  whose body names its own specialization finds the declaration already made
  rather than starting a second reading of it.
- **A specialization is bound to no name.** It is reached from the template-id
  that wrote its arguments, so ordinary lookup keeps finding the template and
  never a declaration the program did not write. `template_id_entity` is the
  one place a spelling becomes one, and it is asked only where the ordinary
  lookup found nothing - in `resolve` for both spellings of a name, and at
  every component of `resolve_prefix`.
- **14.6.1p1's injected-class-name is the specialization**, and a
  template-argument-list written after it names the template it was made of.
- **What a specialization is named by is two facts, not one spelling.**
  `TypeTable` records the template's own qualified name and the argument
  `TypeId`s (`set_template_arguments`), because the ABI writes them apart -
  `_ZN3BoxIiE3getEv` - and the one spelling `Box<int>` cannot be split back
  into them. `lowir_abi.cpp` reads them at both places a name reaches a class:
  `abi_type` for the type itself, and `build_function_name` for a member,
  which asks the *class* whether it is a specialization rather than the
  spelling, by walking the member's own region.
- **A type-id's spelling is read as a declarator, not as a word list.**
  PA10 hands a template-argument on as one spelling, so `split_type_id` keeps a
  name whole - a qualified name, a nested template-argument-list, a
  decltype-specifier's parentheses all belong to the component around them -
  and `type_id_words`/`abstract_declarator_words`/`suffix_words` read 8.1p1's
  type-specifier-seq and 8.3p1's abstract-declarator from what is left. That is
  what makes `void(int)`, `int(*)(char)`, `std::uint32_t[4]` and `V<V<int>*>`
  arguments this milestone reads.
- **A default template-argument is read in a region that already binds the
  parameters before it** (14.1p9), and the whole list one list of explicit
  arguments makes is kept, so naming the same specialization again reads no
  default a second time.
- **12.1p1's constructor name is the injected one.** A specialization's
  constructor is what its pattern spelled with the template-name, whatever the
  template-id calls the class.

## Current Failure Map

179 of 293 fail. Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| 49 | a function template with a definition | 14.7.1p1's instantiation of a *function* template body, and the ABI's `<template-args>` on the specialization's own name (`_Z5call0I4fobjEiT_`) |
| 42 | exit 0, LowIR differs | not yet grouped; the first tier now runs far enough for the comparison to have an opinion |
| ~30 | dependent names | `typename base<T>`, `T::value_type` behind a dependent base, `sizeof` of an incomplete specialization |
| ~20 | overload resolution over templates | a call whose candidates include function templates, template-backed operator overloads, explicit template-ids in an overload set |
| 6 | not a translation unit | explicit instantiation (`template struct A<int>;`) and an explicit function-template-id in a declaration, which PA10 parses but this milestone does not read |
| ~30 | the long tail | out-of-class member definitions of a class template, `using` of a dependent base, ADL at the point of instantiation |

## Active Checkpoint

**C2 — the function tier**: 14.7.1p1's instantiation of a function template's
definition, with the specialization's own object-file name.

- **owner**: `sema_template.cpp` records the pattern the same way the class
  tier does; `instantiate` reads it instead of refusing it; `lowir_abi.cpp`
  writes the specialization's `<template-args>` and 5.1.3's `T_` for a
  parameter type the template wrote.
- **data flow**: `template_declaration` -> `TemplateInfo` on the function
  entity -> `specialize`/`deduce_specialization` make the declaration ->
  `instantiate` reads the body against a bindings region -> the ordinary
  pending-definition path writes it.
- **expected complexity**: one reading per specialization, linear in the body;
  the memo in `SemaModel::specialization_of` is what makes naming one twice a
  probe.
- **validation**: the 49 refusals, then the pa19 report and pa1-pa18.

## Performance Model

The dominant operation is one reading of one pattern per specialization, which
is linear in the pattern. What is superlinear is superlinear in the *program*.

Measured on the four shapes the tier makes scaling-sensitive, each timed twice,
`cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n distinct specializations of one template, each with a member function | 0.01 s | 0.01 s | 0.03 s | 0.05 s | 0.10 s |
| one specialization named n times | 0.00 s | 0.01 s | 0.01 s | 0.01 s | 0.02 s |
| an n-member class template, four specializations, `sizeof` of each | 0.00 s | 0.00 s | 0.01 s | 0.01 s | 0.01 s |
| an n-deep nest of template-ids as arguments | 0.00 s | 0.01 s | 0.01 s | 0.02 s | 0.06 s |

All four are linear in the source. The second is what says the memo works: 512
namings of `Box<int>` cost what 32 do plus the 512 declarations they make.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern a template-declaration parameterises, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading of that pattern against a bindings region, 14.1p9's defaults read where they may name the parameters before them, 14.6.1p1's injected-class-name, the ABI's `<template-args>` on a specialization and on its members, 12.1p1's constructor named by the template-name, and 8.1p1's type-id read as a declarator so a function type, an array and a nested template-id are arguments this milestone reads; `sema_value.h` and `sema_template.h` split out of `sema_analyzer.h` | 27 -> **114 / 293**; pa1-pa18 1777 / 1777; file audit passes; four scaling shapes to 512 |
