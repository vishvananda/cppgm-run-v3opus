# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C2 completion, reviewed at `aa6fb90f`** — the two points a specialization
has, 14.6.2p1's dependent argument list, `SemaAnalyzer::substituted`, and
14.8.2.1p2/14.8.2.5p4's deduction. The increment itself holds: the split
between the declaration a template-id makes and the body a definition
completes is 14.7.1p1's own, `TypeTable::is_dependent` is asked of the
arguments the type records rather than of the class's members, and the
substitution walk owns exactly the one category the type table cannot rebuild.
`instantiate_class` holds the specialization before reading its body, so a
pattern that names its own specialization finds the declaration; the pa1-pa18
baseline is untouched by all of it.

**The blockers were not in what the increment computes but in what it names.**
The plan claims the ABI asks "the *class* rather than the spelling, by walking
the declaration's own region". It did not: three of the four name paths still
split `abi_qualified_name` on `::`, and the fourth handed the encoder a class's
whole qualified spelling. A template-argument-list is written *inside* one
component, so every one of them mis-split as soon as an argument spelled a
qualified name or a class stood under a specialization - and the suite could
not see it, because `canonicalize_lowir_for_compare` strips `object=`,
`binding=` and every `alias object` line before comparing and pairs functions
by masked body shape. **Eleven fixtures emitted symbols that are not ABI names
at all - `_ZN3api14pair<const api14text<char>,...`, `_ZN10outer<int>5innerdeEv`,
`_ZN3BoxIiE4callEN8Box<int>3TagE` - and nine of the eleven passed.** One did not:
it drove the harness's own validator into `Can't use an undefined value as an
ARRAY reference`, which is the only reason any of this was visible at all.

### Findings

**1. A name was split out of a spelling that cannot be split.**
`name_components` cut `abi_qualified_name` at every `::`, so
`api::pair<const api::text<char>,api::tag>::pair` was **five** components and
not three, and the map from a component to the class it names ran off the end.
The splitter is now bracket-aware, but that is the smaller half: the components
themselves now come from the declaration's own regions - the classes from
`owning_classes`, the namespaces from the outermost class's own qualified
spelling, which is the one part `::` does split because 7.3.1p1 gives a
namespace one identifier for a name. `name_regions` is the single reading, and
`build_function_name` and `build_data_name` both consume it.

**2. `owning_classes` walked the region a definition was written in, not the
regions the declaration is named through.** 9.7p3's out-of-class definition of
a nested class opens its class scope under whatever the definition stands in -
for a specialization, the bindings region - so `outer<int>::inner` was **one**
owning class and not two, and the walk missed the template entirely. That was
invisible for a class the program wrote, because the spelling then agreed with
the walk by accident. Each class is now asked where its own declaration stands.

**3. `abi_type` handed the encoder the spelling of a class named through a
specialization.** `Box<int>::Tag` as a parameter type, `family<long int>::argument`,
`ctype<char>::mask` - every one of them went out as an `ABI_TYPE_NAMED` whose
`name` held `<` and `>`. A class or enumeration a specialization declares is now
an `ABI_TYPE_MEMBER` over its enclosing class's own encoding, which composes to
any depth. The type carries `TypeTable::declaration` for it, settled by
`SemaAnalyzer::own_type` beside `SemaModel::own_type` so no site records one and
not the other.

**4. The ABI's compression makes a `<template-param>` a substitution candidate,
and we never made it one.** `T` written twice in one signature came out `T_ T_`
where g++ and the reference write `T_ S0_`, so **every** function-template
specialization's symbol differed from both oracles - 20 fixtures' worth, and
none of them failed. The fact was already in PA14's model
(`AbiType::substitutable`); nothing set it.

**5. 14.7.1p1's instantiated definition was one this unit owned.** `binding=` was
`entity.inline_function` alone, so a member defined *in* its class came out weak
by accident and an out-of-class member definition, a static data member and a
function-template specialization all came out **strong**: two translation units
each naming `Box<int>` would each define `_ZN3BoxIiE5twiceEv` as the program's
one definition, which is a duplicate symbol at link. 19 symbols across the suite,
and `binding=` is stripped before comparison, so not one of them failed.

**6. The same fact had two more readers, and both kept asking the old question.**
`writes_base_entry` owes both of 12.1's entry points for "a definition no other
unit may hold", and the deleting-entry gate owes `D0` on the same reading. Both
asked `inline_function`, so an instantiated constructor emitted a `C2` entry
point the reference does not, and a virtual destructor instantiated from a
template owed a `D0` for a class no unit owns. `shared_definition` is the one
question now, and 3.2p3's wait-for-a-use is the third reader of it.

**And one avoidable walk.** `TypeTable::is_dependent` recursed with no memo, and
`substituted` asked it *before* its own memo. A specialization's arguments are a
graph and not a tree - `P<t,t>` reaches `t` twice - so a nest that doubles at
every level was walked 2^n times. The answer is a fact of the type and is now
kept; the memo is invalidated where `set_template_arguments` changes it.

### Changes

| what | where |
| --- | --- |
| the components of an encoded name read from the declaration's regions | `lowir_abi.cpp` |
| each owning class asked where its own declaration stands | `lowir_abi.cpp` |
| a class or enumeration named through a specialization as a member type | `lowir_abi.cpp`, `type_model.h/.cpp`, `sema_analyzer.h`, `sema_template.cpp`, `sema_analyzer.cpp` |
| the ABI's `<template-param>` as a substitution candidate | `lowir_abi.cpp` |
| 14.7.1p1's shared definition, and its three readers | `lowir_abi.h/.cpp`, `lowir_lower.h/.cpp`, `lowir_vtable.cpp` |
| 14.6.2p1's answer kept per type | `type_model.h/.cpp`, `sema_template.cpp` |

Two regression tests: `300-specialization-nested-name-encoding`,
`300-instantiated-definition-vague-linkage`.

### Performance Evidence

- **Seven scaling shapes to 512**, each timed twice, `--emit-lowir -O0`, all
  linear in the source: n distinct specializations 0.01 → 0.10 s; one
  specialization named n times 0.00 → 0.02 s; an n-member template over four
  specializations 0.00 → 0.01 s; an n-deep nest of template-ids 0.00 → 0.05 s;
  n deductions over n classes 0.01 → 0.07 s; n calls deducing one
  specialization 0.00 → 0.01 s; and the shape the naming fix added - n classes
  a specialization declares, each a parameter type of a member - 0.01 → 0.10 s.
  A class nested 64 deep inside one specialization costs 0.01 s, so the
  `owning_classes` walk per name is bounded by the source's own nesting.
- **The one superlinear shape is the spelling, and both implementations have
  it.** `typedef P<t,t>` repeated n times names a class whose written-out
  spelling doubles at every level, and a specialization is named by that
  spelling: 0.04 s, 0.31 s, 1.26 s, 4.94 s at n = 12, 16, 18, 20 in **25 lines
  of source**. `reference-binaries/cppgm++` is 0.04 s, 0.28 s, 1.14 s, **5.97 s**
  on the same inputs, so this is the milestone's shape and not the checkpoint's;
  g++ does n = 20 in 0.02 s because it never materialises the spelling.
- **The whole pa1-pa19 report is 15.6 s.**

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **194 → 200 / 295**.
- **File audit passes** for pa19 over `dev/src`, with the five header-weight
  warnings the shared headers have carried since PA18 - and no suppression.
- **Every checked `.ref` and `.ref.witness` in the repository regenerates
  byte-identically** from `reference-binaries/cppgm++` through `make ref-test`
  and `make ref-test-strict`, so the PA19 fixtures are the reference's output
  and not ours - the README's "no external reference binary for PA19" means no
  ref-test *target* of its own, not no oracle.
- **The symbol sweep the comparison cannot do.** `object=` over all 295 fixtures
  against the reference: 54 tests differed before the fix, **9 after**, and every
  one of the nine is now an extra or missing *definition* rather than a
  misspelled name. A 13-name differential probe through g++ - a nested class of
  a specialization as a parameter, a nested class defined out of class, a
  qualified name as a template argument, a static data member through both -
  is **byte-identical to g++** on every name.
- **Multi-unit.** A three-unit program in which two units each instantiate the
  same class template, its out-of-class members, its static data member and a
  function template validates through the harness's own `validate_lowir_text`,
  holds one weak definition of each specialization, and is **canonically
  identical in all four permutations** of the unit order.
- **Valgrind clean** with `--error-exitcode` over all 295 fixtures, the
  multi-unit programs and the scaling shapes.

## Open Gaps

**Recorded, not defects.** A specialization of a template declared in
7.3.1.1p1's unnamed namespace binds `internal` here and `weak` in the
reference; 3.5p4 gives every name in that region internal linkage and **g++
emits it local**, so the reference stands alone. An instantiated constructor
emits both of 12.1's entry points where the reference emits only the
complete-object one; **g++ emits both**, and the reference's own non-template
out-of-class constructor gets both - so this is the reference's rule for
instantiated definitions, and matching it is what turned three fixtures green.

**Out of scope and still named.** A variable template's partial specialization
is written into the object file as `_Z6v<T,T>`, which is not an ABI name.
14.5.5 partial specialization and variable templates are both in PA19's Out Of
Scope list, so the input's behaviour is undefined for this milestone; the name
is left where the feature is.

**The spelling a specialization is named by** is exponential in the depth of a
nest whose arguments double, as measured above. It is the reference's shape too
and no fixture reaches it, so it is recorded rather than re-architected: fixing
it means not storing a specialization's written-out name at all.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1, C2, C2 completion | the whole tier as landed, reviewed at its completion: `TemplateInfo` as the pattern a template-declaration parameterises, 14.7.1p1's instantiation as a second reading of it, the function tier and 14.5.1.3p1's out-of-class members, the two points a specialization has, 14.6.2p1's dependent argument list, `SemaAnalyzer::substituted`, and 14.8.2's deduction | `aa6fb90f` | 6 / 6 + 1 performance, in one family - **the object file's name for a specialization, which this suite cannot see**: `canonicalize_lowir_for_compare` strips `object=`, `binding=` and `alias object` and pairs functions by masked body shape, so eleven fixtures emitted symbols containing `<`, `>`, `,` and spaces and **nine of them passed**. A name was split out of a spelling at every `::`, so a template-argument-list that spells a qualified name made `api::pair<const api::text<char>,api::tag>::pair` five components and not three; `owning_classes` walked the region a definition was *written* in, so 9.7p3's out-of-class nested class lost the template above it; `abi_type` handed the encoder `Box<int>::Tag` as one spelling; the ABI's `<template-param>` was never made a substitution candidate, so **every** function-template specialization's symbol differed from g++ and the reference alike; 14.7.1p1's instantiated definition was bound `strong`, so two units naming `Box<int>` would each claim to own `_ZN3BoxIiE5twiceEv` - a duplicate symbol at link, over 19 symbols, none of which failed; and the same fact's two other readers kept asking `inline_function`, so an instantiated constructor owed a `C2` entry the reference does not and an instantiated virtual destructor owed a `D0` for a class no unit owns. Beside them, 14.6.2p1's own cost: `is_dependent` recursed with no memo over what is a graph and not a tree, and `substituted` asked it in front of its own memo | 194 → **200 / 295**, two of them the regression tests these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 → 9 tests and every survivor a definition rather than a name; 13 names byte-identical to g++; a three-unit program order-free in all four permutations; seven scaling shapes to 512 and the one that is not, measured against the reference; valgrind clean; the pa1-pa19 report 15.6 s |
