# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_` - so `template<template<class> class C> int use(); use<pair2>()` and `use<box>()` at a place written `template<template<class> class> class` were both **accepted** where both oracles refuse, and `template<template<class> class C = box> int use()` was **refused** as `no declaration of C<int> is in scope`, because `arguments_of` reads every default that is not a value place's as 8.1p1's type-id.  14.6.1p1's injected-class-name arm - a lookup that lands on a specialization is retargeted to the template it was made of - is right for `box` written inside `box<T>`'s own body and swallowed `holder<box<int> >` and `= box<int>` with it, two programs both oracles refuse; which of the two was written is a fact of the *spelling*, and `QualifiedName::names_a_template_id` is the reading this file already had.  14.3.3p1's pack matched every place the other head had left and none where it had none, so `template<class, class...> class C` refused `template<class T> struct box`; and inside the loop the pack compared only the *kind* of each place where the fixed arm compares the kind, the value signature and the nested head, so `template<int...>` stood at a place written `template<unsigned...>`.  Under all of it `match_template_id` read `L<A…>` against a class an argument list had already made and against nothing else - so 14.5.5.2p1's ordering, which hands it `L<A…>` against `M<B…>`, ordered no two patterns written over a template place, and it carried neither of 14.8.2.1's two allowances, so `take(const C<T>&)` called with `box<int>` deduced nothing where both oracles deduce |

## Current Checkpoint Review

Checkpoint T is where 14.1p2's *third* kind of place arrived. A head declared a
type or a value; it now declares a template as well, and the whole of the
checkpoint follows from what that place is worth. `TemplateHead` is the reader:
a head is settled once in 14.6.1p1's own region, `parameter_head` reads a
template place's own clause once per clause node, `argument_matches` is
14.3.3p1, and `TypeKind::TemplateName` is the type-table entry standing for the
template a written argument named — interned per declaration, which is what
makes `holder<box>` written twice one specialization and what T3 had to teach
`operand_of` before two templates stopped being one symbol. Beside it
`dependent_template_id` is 14.6.2p1's `C<A…>` while `C` is still a place, and
`match_template_id` is 14.8.2.5p4's pair over one.

All of it is the right shape. The head is read once per clause and a nest of
them is flat to depth 12; the interned entry gives `_ZN6holderIN1N3boxEE3getEv`
and `_ZN6holderIN1Q3boxEE3getEv` byte for byte with `g++ -std=c++11`; the
dependent naming is one declaration per place and interned list, so a pattern
that writes `C<T>` two hundred times substitutes once; and 64 probe programs
lower to LowIR the assignment's own comparator finds identical to the reference.

What the review found is the shape this stage keeps finding. Each of the four
new facts was written at one exit of its own family: 14.3.3p1 at the class tier
and not at the function tier, the template-name-or-template-id question at the
lookup and not at the spelling, the pack's own match beside the fixed one rather
than through it, and 14.8.2.5p4's pair over a class alone where the ordering,
the qualifiers and a base each hand it something else.

### Findings

**1. 14.3.3p1 is asked at the class tier and at neither exit of the function
tier.** `explicit_argument` asks `place_head(places[index]->type)` and
`match_template_id` asks it again, and nothing at the function tier ever wrote
that entry: `SemaAnalyzer::template_parameter` makes the place's type with
`is_template` set and stops, while the class tier's `open_region` records the
head, gives it a region and settles its places. So

```cpp
template<class A, class B> struct pair2 { };
template<template<class> class C> int use() { return 0; }
int main() { return use<pair2>(); }              // accepted
```

and the same with a place one level deeper (`template<template<class> class>
class W` given `box`) were programs `pa22/cppgm++-ref` and g++ both refuse and
this build **translated**. The same missing entry is why 14.1p9's default was
read as a type-id: `arguments_of` asks `parameter_value_type`, which is
`kNoType` for a type place *and* for a template place, so

```cpp
template<class T> struct box { int n; };
template<template<class> class C = box> int use() { C<int> c; return c.n; }
int main() { return use<>(); }                   // no declaration of C<int> is in scope
```

was **refused** where both oracles accept it. `TemplateHead::record_place` is
now the one recording — the class tier's own three steps, named — and
`TemplateHead::place_default` is the one reading of a default at a template
place, which `bind_arguments` and `arguments_of` both call.

**2. A template-id written at a template place was taken for the template it
specializes.** 14.6.1p1 lets the injected-class-name of a specialization be used
as a template-name, so a lookup that lands on a specialization is retargeted to
`named->primary` — and 3.4.3 answers `box` written inside `box<T>`'s body and
`box<int>` written at a template place with the same kind of entity. So
`holder<box<int> >` and `template<template<class> class C = box<int> >` were
both **accepted** as `holder<box>`, where both oracles refuse. Which of the two
was written is a fact of the spelling and nothing the lookup can recover;
`QualifiedName::names_a_template_id` — 7.3.3p5's own reading, aware that four of
13.5's operators are spelled with `<` — is asked before the lookup. `box`
written inside `box<T>` and `outer<char>::inner` written at a place are
unaffected, because neither writes a list at its last component.

**3. 14.3.3p1's pack was two readings, and neither matched a run of none.**
`argument_matches` walks the two heads to the shorter one and then refuses
outright wherever P declared more, so a trailing pack in P matched every length
but zero:

```cpp
template<class T> struct box { int n; };
template<template<class, class...> class C> struct holder { C<int> c; };
int main() { holder<box> h; return h.c.n; }      // refused; both oracles translate
```

And the pack arm compared `value` and `templated` of each remaining place where
the fixed arm compares those, 14.3.3p1's value signature and the nested head one
level down — so `template<int... Ns>` stood at a place written
`template<unsigned...>`, which both oracles refuse. `places_match` is now the one
pair reading and the pack asks it of each place the other head has left, which
is what the clause says a pack does.

**4. 14.5.5.2p1's ordering could not read a pair of two patterns.**
`match_template_id` deduces the place from *which template* the argument was
made of, and it read that fact off a class an argument list had already made —
`kind(bare) != Class` was the door. 14.5.5.2p1 rewrites two partial
specializations as function templates and deduces one from the other, so what it
hands the arm is `L<A…>` against `M<B…>`, a naming over a place at both ends.
Every two patterns written over a template place were therefore unordered, and
the diagnostic is `matches two partial specializations and neither is more
specialized than the other` — the plan's own failure group. What the argument
was applied to is a template the program declared where a list already made a
class of it and a place standing for itself where it did not; both are then
asked the same two questions of the same two heads. Two of the suite's tests
pass on this alone, and finding 3 would have cost a third without it.

**5. The pair carried neither of 14.8.2.1's two allowances.** Every other pair
of this walk takes `relaxed` and `derived` — the class arm's own cv test is
`relaxed ? (cv(A) & ~cv(P)) != 0 : cv(P) != cv(A)` — and the new arm compared
the qualifiers strictly and looked only at the argument itself. So

```cpp
template<template<class> class C, class T> int take(const C<T>& b);
int main() { box<int> b; return take(b); }       // refused; both oracles deduce
```

and a class *derived* from a specialization deduced nothing where g++ deduces.
The base is the second half: `named_below` looks for a base made of one *named*
template, which a place names none of — what a place asks of a base is
14.3.3p1's question, the same one the pair itself asks, so
`specialization_below` asks it. 14.8.2.1p3's pointer form is the same sentence
one indirection out, and its `simple` test spelled a simple-template-id as
`kind(inner) == Class && is_specialization(inner)`, which is the form written
over a named template and not the one written over a place.

**6. The substitution minted a second type for a naming the pattern had already
made.** `template_id_entity` answers `C<A…>` out of `dependent_templates_`,
keyed by the place and the interned list, so one spelling written n times is one
declaration. `substituted`'s arm for the same construct — reached wherever the
bindings leave `C` a place still — called `dependent_template_id` with a fresh
`type_entity_id()` every time it ran. Instrumenting the arm shows one file of
the suite minting `bound=22 args0=27` twice, which is two type-table entries for
one type: the mirror of the collision T3 found in `operand_of`, where two
templates were one entry. It is bounded — the count is flat in the number of
instantiations, one per written naming — so nothing measures slower; what it
costs is that two readings of one naming compare unequal. Both exits now ask
`dependent_template_name`.

### What the review confirmed rather than found

- **The head is read once per clause.** `parameter_heads_` is keyed by the
  clause node and `place_heads_` by the type the place stands for; nesting depth
  2 → 12 is flat, and nesting × namings is linear in the product to 11 × 3200
  (0.77 s, 189 MB) at the pre-audit build's numbers to within measurement.
- **The interned `TemplateName` is one entry per declaration.** `holder<box>`
  written twice is one specialization, two `box` templates in two namespaces are
  two symbols, and both agree with `g++ -std=c++11` byte for byte.
- **The dependent naming is one declaration per place and list.** 400 namings of
  `C<T>` in one pattern cost 0.01 s, and 160 instantiations over one pattern mint
  the substitution's entry once.
- **The LowIR is the reference's.** 64 probe programs — every shape below where
  this build and `pa22/cppgm++-ref` agree on the verdict — pass through
  `pa22/scripts/compare_results.pl` itself, single-unit and across two units.
- **No gate and no skipped work.** The checkpoint's diff holds no `getenv`, no
  fixture name, no dialect switch and no environment read; `record_place` and the
  new pair arms are reached in all three dialects alike.
- **valgrind is clean** over all 77 probe programs and over the three largest
  scaling inputs.

### Recorded, not landed

- **`--emit-semantics` and `--emit-types` refuse `box<int>` at all** in this
  build, where `pa12/cppgm++-ref --emit-semantics` answers it. An ordinary class
  template with no template place written shows it, so it is neither this
  checkpoint's nor a gate this checkpoint added.
- **14.5.5.2's ordering by pack prefix length** leaves `list<A0, Rest...>` and
  `list<A0, A1, Rest...>` unordered where both oracles select the second — with
  no template place written anywhere, so it is the plan's failure group and not
  this checkpoint's.
- **`trait<const box<int> >` selects the primary here** and the partial in the
  reference; g++ agrees with this build, and 14.5.5.1p1's pair writes no
  qualifier the argument did not.
- **`template<class T, unsigned N>` at a place written `template<class, int>`**
  and **`template<class T, class... Rest>` at a place written
  `template<class>`** are refused here and in the reference and accepted by g++,
  which is P0522's relaxation and not C++11's clause.
- **`template<class T, class U = int>` at a place written `template<class>`** is
  accepted here and in the reference and refused by g++, which is the same
  clause read the other way; the checkpoint's own 14.1p9 arm wrote it.
- **A qualified template-id prefix in a default at a template place** —
  `= outer<char>::inner` — is accepted here and by g++ and refused by the
  reference.

## Changes

- **`sema_template_head.cpp/.h` — `record_place`**, called from `open_region`
  and from `sema_analyzer.cpp`'s `template_parameter`: 14.1p2's head recorded
  against the type a place stands for, whichever tier declared the place.
- **`sema_template_head.cpp/.h` — `place_default`**, called from
  `bind_arguments` and from `sema_deduce.cpp`'s `arguments_of`: 14.1p9's default
  at a template place read as a template-name and not as 8.1p1's type-id.
- **`sema_template_head.cpp` — `template_argument`**: 14.3.3p1's room for a
  template-name asked of the spelling, through `QualifiedName`, before 3.4.3
  answers it.
- **`sema_template_head.cpp/.h` — `places_match`**, with `argument_matches`
  rewritten around it: one pair reading, asked of each place a pack has left, and
  a pack P declared past everything A wrote matching the run of none.
- **`sema_deduce.cpp/.h` — `match_template_id`**, with `derived_template_id` and
  `specialization_below`: the pair read over a place as over a named template,
  carrying 14.8.2.1p3 and p4's allowances, with the base a place asks 14.3.3p1
  of; and `match`'s pointer arm reading a simple-template-id written either way.
- **`sema_template.cpp` — `substituted`**: 14.6.2p1's naming over an unsettled
  place answered by `dependent_template_name`, which is where the other exit
  already asks.

## Performance Evidence

Best of three with `/usr/bin/time`, against a `make build` of `598d0d4a` in a
worktree so every number has a baseline rather than a memory, and against
`pa22/cppgm++-ref`.

| shape | this build | pre-audit build | `pa22/cppgm++-ref` |
| --- | --- | --- | --- |
| nested template places in one head, depth 2 / 12 | 0.00 / 0.00 s at 6 MB | 0.00 / 0.00 s | 0.53 s |
| function-tier template place, 400 / 800 / 1600 / 3200 namings | **0.06 / 0.13 / 0.28 / 0.58 s** at 22 / 39 / 72 / 138 MB | 0.06 / 0.13 / 0.27 / 0.57 s | 0.97 / 1.60 / 3.49 / 10.64 s |
| nesting depth × namings, 3×200 → 11×3200 (cross product) | **0.03 / 0.06 / 0.15 / 0.36 / 0.77 s** at 14 → 189 MB | 0.02 / 0.06 / 0.15 / 0.34 / 0.76 s | 0.70 → 7.60 s |
| one template named at a place 400 times | 0.01 s at 8 MB | 0.01 s | 0.61 s |
| 400 distinct templates each named once | 0.04 s at 17 MB | 0.04 s | 0.81 s |
| `C<T>` written 400 times in one pattern | 0.01 s at 11 MB | 0.01 s | 0.75 s |
| one `C<T>` pattern over 400 argument lists | 0.07 s at 23 MB | 0.07 s | 1.10 s |
| out-of-class member definition, 400 specializations | 0.05 s at 18 MB | 0.04 s | 0.83 s |
| one partial-specialization pattern, 160 instantiations | 0.01 s at 10 MB | 0.01 s | 0.58 s |
| the whole 169-file PA22 corpus | **1.31 s** | 1.30 s | — |

Every dimension is linear and every one matches its baseline to within
measurement: the audit added one recording per function-tier place, one spelling
scan per written template argument, and one memo lookup per substituted naming,
and none of the three is a term of the input. The reference is 10× to 18× slower
on the two cross-product sweeps. The multiplicity sweeps were run to 3200 and
the nesting sweep to depth 12; neither carries a 2^depth term, because the head
is read once per clause node and the naming is interned per place and list.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` — **156 / 308**, from the
  turn's 154 baseline, with the failing set a strict subset: the two partial
  ordering tests `400-defaulted-nested-cv-template-template-partial-specialization`
  and `400-repeated-pack-partial-specialization-ordering` now pass and nothing
  regressed.
- `make test-report-through-pa21` — **pass**, 2568 / 2568, 21 / 21 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa22 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- 77 systematic probe programs swept against `pa22/cppgm++-ref`, `g++
  -std=c++11 -pedantic-errors` and, where the verdicts agree, the assignment's
  own LowIR comparator: 7 over what may stand at a template place, 9 over
  14.3.3p1's pack, 7 over the function tier, 7 over 14.1p9's default, 8 over
  `C<A…>` written over an unsettled place, 8 over 14.8.2.5p4's pair, 5 over
  14.5.5's ordering, 4 over the object-file name and 3.4.2p2's regions, and the
  rest over the cross-product. Every disagreement judged against the standard
  and the third oracle rather than copied.
- Nesting-depth sweep to 12 levels and multiplicity sweep to 3200 namings on
  each new reading, with the cross product of the two; all linear.
- `valgrind -q --error-exitcode=9` over all 77 probes and the three largest
  scaling inputs: **clean**, 0 errors.
- No `.ref` regenerated: every producer this audit changed either refuses a
  program the fixtures do not write or answers one they already pin, which is
  what the 64-probe comparator run and the unchanged failing set together show.
