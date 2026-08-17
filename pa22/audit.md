# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_`, so a template-name at a function-tier place was unchecked in both directions; 14.6.1p1's injected-class-name arm swallowed a template-*id* written at a place; 14.3.3p1's pack matched no run of none and compared only each place's kind; and `match_template_id` read `L<A…>` against a class alone, so 14.5.5.2p1 ordered no two patterns over a template place and 14.8.2.1's two allowances reached neither |
| A | `202f04ec` | 4 / 4 + 6 recorded | **the three things a template-id can be looked up in, with 14.2's own exit written at two of them - and the access 11p1 gives a member template, carried by one of the three tiers that make a declaration from an argument list.**  Checkpoint A landed 14.5.7p1's alias template and moved 14.2p4's keyword into `QualifiedName::part`.  The alias is right and swept clean: 7.1.3p2's transparency, 14.5.7p2's identity, the ABI name, the region a member alias's access travels from, the memo that keeps a nest naming the one below twice flat to depth 24 - 84 probes pass the assignment's own comparator against the reference.  What the keyword's move did not carry is the *reason* it was moved: the commit names `a.template f<A>()` as one of three refusals it ends, and `member_named` - 5.2.5p1's lookup, the third of the three - had no 14.2 exit at all, so every `a.template f<int>()`, `p->template f<int>()` and `this->template f<int>()` was still `no declaration of f<int> is in scope` where both oracles translate.  Beside it the move made `last()` shorter than the span it was taken from, and three readers took the nested-name-specifier off the spelling by that length: `d.B::template f<int>()` came out as `B::templat names no class`.  Under the new exit, 11p1's access is a fact `instantiate_class` and the alias arm both write onto the declaration one argument list makes and the two function-tier exits never did, so a *private* member template reached through `.` was accepted where both oracles refuse.  And `template_id_entity` may now answer with a typedef-name: 14.7.2p2's reader took `made.primary` without asking, so `template struct P<int>;` over an alias **segfaulted** where both oracles refuse |
| P | `0d1c75c9` | 4 / 4 + 6 recorded | **the two forms 14.7.2 writes one requirement in, with p2 asked at one of them - and the parse form the same commit opened, which reached no reading at all.**  Checkpoint P made every PA22 test parse: 14.2p4's keyword became optional at a member name, 14.2p1's operator template-id got one shared `skip_template_arguments`, and 14.7.2p1's declaration was let end at a `;` no out-of-class constructor otherwise may.  The member reading is right and is *narrower* than the reference's - `s.a < 1 > (0)` over two `int` members is two comparisons here and refused there - and 13.5p6 at the specialization agrees with `g++` on all five shapes probed.  What the commit's own comment claims and the code does not is that "the target is read either way": `explicit_instantiation` returned on `!owed` **before** reading anything, so `extern template struct P<int>;` over an alias, over a non-template, over a name no template declares and over a prefix naming no type were four programs `g++` refuses and this build accepted - the p9 twin of the segfault checkpoint A's audit had just fixed.  Beside it `is_explicit_instantiation_target` began accepting a `SpecialMemberDeclaration` that `explicit_instantiation` had no arm for: `extern template box<int>::box();` passed only because the early return got there first, and `template box<int>::box();` came out of the *class* arm as `an explicit instantiation defines a class`.  And the parser's `names_specialization_` was taken once and never put down, so every body under such a head read its statements as out-of-class special members: `template<> void f<int>(int) { A::A(); }` was refused where the same statement in an ordinary function is accepted |
| F | `6a9c1f3f` | 4 / 4 + 2 recorded | **the two readings one friend definition gets, with 11.3p6's class carried onto only the first - and the region 11.3p6 moved the declaration out of, which the second one reads the pattern against.**  Checkpoint F gave a friend declaration written under a head everything 11.3 already knew: `befriended` asks the pair with each side replaced by its `primary`, `record_template` gained 14.5.4p1's class tier under `templating()`, 11.3p1's granting class is taken before `StandingIn` moves the head, and 11.2p5's naming class is the class a qualified name was looked up in.  Those rules are right and swept clean - 15 friend-class shapes, 11 qualified-access shapes and 7 reveal orderings agree with `g++`, the four scaling dimensions are linear to 800 and flat to depth 256, and `valgrind` is clean over 36 inputs.  What none of it carried is that a friend declaration written in a class *template* is read **twice** and read somewhere else: 14.6p8's reading of the pattern and 14.7.1p1's reading for a specialization both reach `declare_function` with one namespace `where`, so the pattern reading's `defined` made every instantiation of a class defining a friend function template `unwrap is defined twice` - the shape F's own fixture writes in a non-template class - while `g++` accepts one instantiation and refuses two.  Under it, `record_function_template` recorded the *namespace* as the region 14.7.1p1 reads the pattern against, so `template<class U> friend T mixed(box<T>, U)` was `T does not name a type` the moment a call deduced it: 11.3p6 makes the declaration in the namespace and 3.4.1p10 reads the definition where it was written, which is the only region that binds what the class's own instantiation bound.  Beside them 11.3p6's other half - a friend declaration defines a function only where its declarator-id is unqualified - was unwritten, so `template<class T> friend int n::f(T) {}` was a program both oracles refuse and this build translated.  And `reveal_friend` dropped and rebuilt the whole hidden chain's index on every reveal because that index is keyed by the declaration heading the chain, which is the one leaving: n friend declarations of one name cost n walks, **15.64 s at n = 3200 against 0.78 s** once the chain is keyed by what it was made with |
| M | `d2e26a4d` | 6 / 6 + 6 recorded | **the two facts 12 writes about a special member, read off the template and never off the declaration one argument list makes - and the class 12.1's second entry point is owed by.**  Checkpoint M, M2 and the M audit gave a head over a constructor and over a conversion function the class's own declaration, at all four exits it can be written at, and 14.5.6.1p5 is what matches a definition to it.  The declaration side is right and swept clean: 30 shapes over those four exits - value places, packs, defaults, nests, `const`, a base's, a mem-initializer, an out-of-class definition that renames its place - translate and run the value `g++` gives them.  What none of it carried is what 12 says about the *declaration*: `specialize` copied `object_member`, `access` and M2's `special` and left 12.3.1p2's `explicit` and 8.4.3's `= delete` default-constructed, so `only_direct a = 3` and a call of a deleted constructor template were two programs both oracles refuse and this build accepted.  12.3.2p1's conversion function template was landed and **unreachable**: 13.3.1.5p1's candidate set held the template, whose result type is a place, so `int k = a;` was `an expression has no conversion to the type it initialises` where both oracles translate it - the ref binary's own refusal of `a.operator int()` had been recorded as the whole of the gap and it was the *named* exit alone.  The ABI wrote such a specialization's name from the substituted type, `_ZN1AcviIiEEv` against the `_ZN1AcvT_IiEEv` `g++` and the reference both write, at the one of `build_function_name`'s three readings `templated` had not been threaded to.  And a constructor template reached only through a base subobject wrote both of 12.1's entry points where the reference writes one: `writes_base_entry` asked whether the *function* is an instantiation, which for a special member is true on exactly this checkpoint's new surface and nowhere else |
| D | `852d9e97` | 3 / 3 + 5 recorded | **the fact 8.3.5p7 made part of a function type's identity, asked by the match and by neither the name nor the object file - and the second implementation of 8.1p1's type-id, which is still missing one of 8.3's forms whole.**  Checkpoint D and D2 taught `SpelledTypeId` three of 8.3.5's rules, made 14.3.2p1's value at a dependent place a settled constant over the place, and settled four of 14.5.5's orderings.  Those rules are right and swept clean: 14 declarator forms, 10 function-type patterns, 7 value-place shapes and 6 pack shapes agree with `g++` and with `pa22/cppgm++-ref`, 14.8.2.5p5's non-deduced context is answered at the function tier too, and every dimension is linear or flat where the plan says it is.  What none of it carried is what the *other* readers of a function type do with the qualifier it had just made part of one: `abi_type`'s `<function-type>` wrote no `<ref-qualifier>` and `type_spelling` wrote neither it nor 8.3.5p1's cv-qualifier-seq after the clause, so `holder<int(char) const>` and `holder<int(char) const &>` were two specializations, one LowIR label and **one symbol** - `_ZN6holderIKFicEE1fEv` against the two the reference and `g++` both write - and a program that defines and calls both returned **22 where both oracles return 12**.  Beside it, 8.3.3p1's `nested-name-specifier *` was a ptr-operator this reading had never had, so `int C::*` read as a type-specifier-seq spelling `int C::` and five pointer-to-member spellings were type-ids this build refused and both oracles accept; and `match_run` took one pack place where `expand_type` and `substitute_entry` - the checkpoint's own other two readings of 14.5.3p4 - each read an expansion over as many as its pattern names, so `box<pr<A, B>...>` matched no list at all |


## Current Checkpoint Review

Checkpoint D and D2 are where a *function type* became something a template
argument may be written over. 14.2 writes a template-argument-list inside a
name, so a type-id reaches the semantic layer as the text PA10 flattened, and
`SpelledTypeId` — the second implementation of 8.1p1 — had learned neither
14.5.3p4's expansion in a parameter clause, nor 8.3.5p7's trailing qualifiers,
nor 8.3.5p5's adjustment. Four of 14.5.5's own orderings came with them,
14.3.2p1's value at a dependent place stopped being an opaque stand-in, and
`collect_packs` and `match_run` learned the two halves of 14.5.3p4 they had
stopped short of.

Those rules are right and each was written at the exits it has. Fourteen
declarator forms written as a template argument — a pointer, a reference, an
array of arrays, a function returning a pointer to a function, a pointer to an
array, `unsigned long long` — read the type both oracles read. Ten
function-type patterns, seven value-place shapes and six pack shapes agree with
`g++ -pedantic-errors` and with `pa22/cppgm++-ref`; 14.8.2.5p5's non-deduced
context is answered at the *function* tier too, at all four shapes probed; and
every dimension is linear in what it sweeps and flat in depth.

What none of it carried is what the *other* readers of a function type do with
the qualifier this checkpoint had just made part of one: **8.3.5p7 became part
of a function type's identity in the match, and neither the name a
specialization is built from nor the symbol the object file names it by had
ever written one.**

### Findings

**1. Two function types that differ only in 8.3.5p7's ref-qualifier were one
symbol.** `Deduction::match` now reads the ref-qualifier as part of a function
type's identity, which is what leaves `holder<R(A...)>` and `holder<R(A...) &>`
two patterns. The two readers that turn such a type back into a name never
learned it: `abi_type`'s `<function-type>` wrote `F <bare-function-type> E`
with no `<ref-qualifier>`, and `type_spelling`'s Function arm wrote neither the
ref-qualifier nor 8.3.5p1's cv-qualifier-seq after the parameter-clause — it
put the cv where every other type's goes, before the result:

```cpp
template<class T> struct holder { static int f() { return 0; } };
template<> struct holder<int(char) const>   { static int f() { return 1; } };
template<> struct holder<int(char) const &> { static int f() { return 2; } };
int main() { return holder<int(char) const>::f() * 10
                  + holder<int(char) const &>::f(); }   // 22, not 12
```

The two are two specializations here — the entity is keyed by the interned
argument list — and they came out as one LowIR label
`@holder_const_int_char____f` and one symbol `_ZN6holderIKFicEE1fEv`, so the
second definition was never emitted and both calls reached the first. The
reference and `g++` write `_ZN6holderIKFicEE1fEv` and `_ZN6holderIKFicREE1fEv`
and both return 12. The fix is one field on `AbiType` written by the one arm
that builds a function type and emitted inside the `F…E` where the ABI puts it,
and the Function arm of `type_spelling` spelling its own qualifiers; the four
mangled names of `int(char)`, `int(char...)`, `int(char) const &` and
`int(char) volatile &&` now agree with `g++` byte for byte, as do the three of
`int (C::*)() const &`, `int (C::*)() &&` and `int (C::* const)(char)`.

**2. 8.3.3p1's ptr-operator was a form the second reading never had.** The
type-specifier-seq is read until the first word a declarator writes, and a
nested-name-specifier standing on its own is not one of them — so `int C::*`
was a seq spelling `int C::` and `no declaration of int C is in scope`, and
`int (C::*)(char) const` was `does not name a type`. Five spellings — a data
member, a nested owner, a member function with 8.3.5p1's qualifiers, a
cv-qualified member pointer and a partial specialization pattern `R K::*` —
were type-ids this build refused and both oracles accept. The split already
leaves a nested-name-specifier as a word of its own exactly where the character
after its `::` opens no name, so a word that ends in one and is followed by `*`
is the ptr-operator and nothing else: that is the whole of the test, the owner
is looked up the way the seq looks its own name up, and the arm sits beside
`*`'s because it takes the cv-qualifiers after it the same way. A grouping
paren opens on it too, which is what `int (C::*)(char) const` needs.

**3. `match_run` read an expansion over one pack where the checkpoint's other
two readings read one over as many as its pattern names.** 14.5.3p4 lets one
expansion be written over more than one pack and 14.5.3p6 makes them the same
length. `expand_type` reads that, and `substitute_entry` — which D itself put
under `canonical_pattern` and `substitution_agrees` — reads it too. The
deduction reading refused it outright:

```cpp
template<class... A, class... B> struct split<box<pr<A, B>...> > { … };
split<box<pr<int, char> > >   // the primary, where both oracles take the pattern
```

D2 had just given the reading what it needed — each element is a pair of its
own over the bindings the elements before it made, minus the pack place — so
the generalization is a run per place rather than one, and 14.5.3p6 falls out
of every element deducing every place once. `pa22/cppgm++-ref` takes the
one-element case and answers `box<>` and a run of two from the primary, so the
course fixture pins the run of one and records the other two.

### What the review confirmed rather than found

- **14.3.2p1's value at a dependent place agrees with the settled reading.**
  The entry the pattern holds carries the source constant's bits, and probed
  through both spellings and through the emitted symbol the two name one
  specialization even where the conversion is not width-preserving:
  `wrap<char>::type` for `ic<T, 300>`, `ic<char, 300>` and `ic<char, 44>` are
  one entity and one `_ZN2icIcLc44EE1fEv`, where `pa22/cppgm++-ref` writes two.
  A pointer place is refused before it reaches this door and stays recorded.
- **14.8.2.5p5's second reading is at both tiers.** `substitution_agrees` is
  the class tier's; the function tier already refuses to deduce through
  `typename A<T>::type`, accepts it beside a deducible parameter, refuses a
  mismatch and takes an explicit argument — four shapes, both oracles agreeing.
- **8.3.5p5's `semantics()` gate is the gate the tree reading already has.**
  `parameter_types` makes the same adjustment under the same predicate because
  PA11 describes the declarator as written; the 2568 tests of PA1–PA21 pass
  unchanged.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch keyed on anything
  but a dialect, a timeout or an environment read. `valgrind -q
  --error-exitcode=9` is clean over 87 inputs, the seven largest scaling ones
  among them. `pa22/cppgm++-ref` generated the `.ref` of each of the three new
  `course/pa22` fixtures and `g++ -pedantic-errors -x c++` accepts and runs all
  three.
