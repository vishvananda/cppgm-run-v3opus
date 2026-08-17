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
| O | `bbe701cd` | 1 / 1 + 2 recorded | **which of a template's bodies a declarator-id names, asked at the tier a definition written at namespace scope reaches and written out at the tier the same commit opened beside it.**  Checkpoint O gave `TemplateInfo::Partial` what `info.current` already was - a class over its own head's places, read from its own body in its own region, keyed in `TemplateInfo::patterns` by the interned list that class carries - and taught 14.6p8's reading to record 14.5.2p1's member class template, so `outer<T>::inner` is a name the current instantiation declares and 14.5.2p3's own tier reads each further head against the class the one above it settled.  Those rules are right and swept clean: 20 out-of-class shapes over a pattern - a static data member, a constructor, a destructor, a nested class, a member function template, a conversion function, an operator, two-parameter patterns, a redeclaration that renamed its places, a use written before the definition, a namespace-qualified owner - translate and run the value `g++` gives them; the six mangled names a pattern's members are written by agree with `g++` and with `pa22/cppgm++-ref` byte for byte; every dimension is linear and depth is flat to 40, where the reference is OOM-killed at 32; and `valgrind` is clean over 57 inputs.  What none of it carried is that same question at the *nested* tier: `record_template` asks `Specialization::member_pattern` which body the arguments the declarator-id wrote name, and `PatternReading::read_declaration` - 14.5.2p3's own tier, opened by this checkpoint - wrote `Specialization::kNoPartial` outright, so a member of *any* partial specialization of a member class template defined outside its class was read against the primary's places, could not reach the class the program named, and came out as `f is written after a name that is not a namespace, class or enumeration`: `A<T>::in<U*>::f`, two patterns beside each other, one nested three heads deep and one under a partial specialization of the enclosing template are four programs `g++` and the reference both accept and this build refused.  Recorded rather than fixed: `pa22/cppgm++-ref` **accepts** those programs and emits a `declare function` for the member it never defines - `_ZN6holderIiE4slotIPcE5widthEv` is a symbol its own LowIR leaves undefined and `lowir2cy86` refuses - which is why the course fixture pins the reading and not the materialization; and the pattern a declarator-id writes that matches no body of the template it named is left to the primary at both tiers, where the refusal that follows is the declarator's rather than 14.5.5p1's own |


## Current Checkpoint Review

Checkpoint O is where a template stopped having one body. 14.5.5p1 leaves a
class template holding the primary's and one per pattern written beside it, and
14.6.1p1 gives *each* of them the class its own definition declares over its own
places — so `TemplateInfo::Partial` got what `info.current` already was, keyed in
`TemplateInfo::patterns` by the interned argument list that class already
carries. Beside it, 14.6p8's reading learned to record 14.5.2p1's member class
template, and 14.5.2p3's own tier reads each further head against the class the
head above it settled.

Those rules are right and each was written at the exits it has. Twenty
out-of-class shapes over a pattern — a static data member, a constructor, a
destructor, a nested class, a member function template, a conversion function, an
operator, a two-parameter pattern beside two more, a redeclaration that renamed
its places, a use written before the definition, a namespace-qualified owner —
translate and run the value `g++ -pedantic-errors` gives them, and the six
mangled names a pattern's members are written by agree with `g++` and with
`pa22/cppgm++-ref` byte for byte. The reading is flat where the reference is
exponential: a member class template nested 40 deep is 0.09 s and 16 MB here
against 17.78 s and 7.99 GB at 24 and the OOM killer at 32 there.

What none of it carried is that the same question has a *second* exit, and that
the same commit opened it: **which of a template's bodies a definition written
outside the class belongs to is asked by `record_template` and written out by
`PatternReading::read_declaration`, the nested tier beside it.**

### Findings

**1. A member of a partial specialization of a member class template was a
definition of nothing.** `record_template` reaches an out-of-class definition
written at namespace scope and asks `Specialization::member_pattern` which body
the arguments its declarator-id wrote name. A *second* head does not reach it —
14.5.2p3 leaves that declarator-id to `read_declaration`, which found the member
template with `nested_owner` and then recorded against
`Specialization::kNoPartial` outright:

```cpp
template<class T> struct A {
  template<class U> struct in { int f() { return 1; } };
  template<class U> struct in<U*> { int f(); };
};
template<class T> template<class U> int A<T>::in<U*>::f() { return 2; }
```

The definition joined the *primary* member template's `members`, was read
against places that never bound `in<U*>`, and came out as `f is written after a
name that is not a namespace, class or enumeration` — where `g++` and
`pa22/cppgm++-ref` both accept. Four shapes were refused: this one, two patterns
beside each other, one written three heads deep as `A<T>::in<U>::deeper<V*>::f`,
and one under a partial specialization of the enclosing template. The fix is the
`wrote` out-parameter `owner` already hands back for exactly this question,
taken at the component `nested_owner` stopped walking at, and the same
`member_pattern` call the other tier makes — one head and one argument list per
definition of a member template that *has* patterns, and a test of an empty
vector for every one that has none. 14.1p2's renamed places fall out of it,
because what is compared is 14.5.6.1p5's signature: `in<K*>` defines the body
`in<U*>` declared.

### What the review confirmed rather than found

- **The pattern's own current instantiation is the one class its body
  declares.** `current_pattern` registers it under `partial.pattern` before the
  body is read, so a recursive `complete_specialization` finds it; the primary's
  is a different interned list because each head's places are distinct types.
  A specialization the pattern's body itself names — `A<T*>` inside
  `template<class T> struct A<T*>` — is that same entity and not a second one,
  and `A` written bare inside it is the injected-class-name 14.6.1p1 binds.
- **The current instantiation is on no `specializations` list.** `record`'s loop
  over them would otherwise read a member definition against the places and
  write it into the unit: probed through a body that names `A<T>` and requires
  it complete, the emitted symbols are the specialization's alone.
- **The `kNoPartial` fallback is not a success path.** A declarator-id writing a
  pattern no body of the template has — `A<T*>::f` with no such pattern, `A<T* const>::f`
  beside a `T*` one, `in<U&>::f` beside a `U*` one — falls to the primary at
  both tiers and is then refused by the declarator reading, which is the answer
  both oracles give. It is recorded because the refusal is the declarator's and
  not 14.5.5p1's own.
- **Nothing scans and nothing re-reads per element.** `member_pattern` reads one
  head per definition, `nested_owner` walks the declarator-id once per head and
  stops at the first component the region cannot settle, and `instantiate` asks
  `TemplateInfo::chosen`, which is a hash lookup on a number the specialization
  carries. Six dimensions are linear to n = 800 and depth is flat to 40.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch keyed on anything
  but a dialect, a timeout or an environment read. `valgrind -q
  --error-exitcode=9` is clean over 57 inputs, the six largest scaling ones
  among them. Two `course/pa22` fixtures were generated by `pa22/cppgm++-ref`
  and `g++ -pedantic-errors -x c++` agrees with both.
