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
| X | `0c922d29` | 1 / 1 + 5 recorded | **which definition of one member of one class specialization this unit holds, asked of the object file and of 5.19p2 - and answered for both by one flag that only the object file may read.**  Checkpoint X made 14.7.3p1's written definition replace 14.7.1p1's reading of the pattern rather than stand beside it: `SemaEntity::instantiated_definition` is written for a function under a new `instantiating_pattern_` depth, `supersede` drops the held body, `Pending::from_pattern` drops one already queued, and `require_replaceable` is that same rule at 12's three entry points.  Those rules are right and swept clean: a member function, a static data member, a constructor, a destructor, a conversion function, a member template of a written-out class, a pattern's member, a `template<>` declaration with no body and a redeclaration ahead of a definition all translate and run the value `g++ -pedantic-errors` gives them; 14.7.3p6's new refusal of a specialization written after the pattern was read agrees with `g++` where `pa22/cppgm++-ref` accepts; five scaling dimensions are linear to n = 800 and the members x specializations cross product to 6400 uses, where the reference is 21.27 s at n = 800 against 0.34 s; `valgrind` is clean over 31 inputs.  What none of it carried is that the *same* commit's other half answers a second question with the first one's field: `note_object` said "a use of this member reads the object rather than the value" by clearing `SemaEntity::constant`, which is 5.19p2's own answer - so `template<> const int code<int>::value = 7;` written anywhere in the unit made `code<char>::value` no constant expression at all, and `int arr[code<char>::value];` and `box<code<char>::value>` were two programs the reference, `g++` and the pre-X build all accept and this one refused with `code<char>::value is not a constant expression`.  The two answers part company at one site - `storage_of`, where the lowering writes the folded value beside the place - so the fact is `member_specialized` and 5.19p2 keeps its own.  Beside it, 12's three entry points queue their bodies through `open_special_member_body`, which set no `from_pattern` at all: a body 10.3p10's table demanded before the `template<>` was read is one `supersede` can no longer reach.  Recorded rather than fixed: an explicitly specialized static data member no use names is dropped where both oracles write it, because the object tier of `supersede` has to withdraw the *definition line* the pattern's reading already wrote and not just its mark; 14.7.3p14's declaration form - `template<> int code<int>::value;` with no initializer - is read here as a definition; `template<> int tag<int>::f();` reads the pattern's out-of-class definition where both oracles write a declaration, and the reference contradicts itself on the in-class spelling of it; one explicit class specialization written twice is accepted; and a `template<>` over a member *class* is refused here where both oracles accept and the reference then runs the wrong body |


## Current Checkpoint Review

Checkpoint X is where a specialization stopped having two definitions of one
member. 14.7.1p1 reads the template's own body again for every argument list and
14.7.3p1 lets the program write one out for exactly one of them, so the written
one *is* that member's definition and the read one is no second definition of
anything — which is what `X is defined twice` had been. `explicit_functions` is
keyed by a template and an argument list and a member of a class specialization
has neither, so the answer lives on the declaration:
`SemaEntity::instantiated_definition` is written for a function too, under a new
`instantiating_pattern_` depth that tells 14.7.1p1's reading of a pattern from
the class body `template<>` wrote; `Specialization::supersede` drops the held
body and `Pending::from_pattern` drops one already queued; and
`require_replaceable` is that one rule read at 12's three entry points.

Those rules are right and each was written at the exits it has. Nine written
shapes over a specialization — a member function, a static data member, a
constructor, a destructor, a conversion function, a member template of a class
the program wrote out, a member of a *pattern*, a `template<>` declaration with
no body, and a declaration written ahead of its own definition — translate and
run the value `g++ -pedantic-errors` gives them, and 14.7.3p6's new refusal of a
specialization written after the pattern was already read for that list agrees
with `g++` where `pa22/cppgm++-ref` accepts. Five dimensions are linear to
n = 800 and the members × specializations cross product is linear in the 6400
uses it writes; the reference is 21.27 s at n = 800 against 0.34 s here.

What none of it carried is that the same commit's other half answers a *second*
question with the first one's field: **which definition an argument list is read
from is the object file's question, and 5.19p2 asks its own — `note_object`
answered both by clearing `SemaEntity::constant`.**

### Findings

**1. A `template<>` for one list made every other list's member no constant
expression.** `Specialization::note_object` implements the reference's own
reading: `code<char>::value` is folded in generated code while the pattern's
definition is the only one the unit has, and read out of the object once the
program writes `template<> const int code<int>::value = 7;` beside it. It said
so by writing `constant = false` and `covered_constant = false` onto every
instantiated member of that name — and those two are 5.19p2's answer about the
declaration, not the lowering's about a use:

```cpp
template<class T> struct code { static const int value; };
template<class T> const int code<T>::value = 3;
template<> const int code<int>::value = 7;
int arr[code<char>::value];                      // ERROR: not a constant expression
```

`pa22/cppgm++-ref`, `g++ -pedantic-errors` and the pre-X build all accept it and
all fold the bound to 3; `box<code<char>::value>` written as a template argument
is the same refusal one door along. The two questions part company at exactly
one site — `LowirFunctionLowering::storage_of`, where 9.4.2p3's value is written
*beside* the place a use reads — so the fact the reading needs is its own,
`SemaEntity::member_specialized`, and 5.19p2 keeps `constant` and the value it
folded. The reference's fold-versus-load is unchanged by the fix:
`spec/300-explicit-specialization-static-data-member` and every probe still load
`code<char>::value` in a body and fold `code<int>::value`, and the pa19 fixture
still folds where the unit writes no `template<>` at all.

**2. 12's three entry points queue a body `supersede` cannot reach.**
`function_definition` marks its queued body `from_pattern` and
`open_special_member_body` — the one door a constructor's, a destructor's and a
conversion function's body is queued through — marked nothing. A body still in
`held_definitions_` is dropped by `supersede` either way, so the shapes the
fixtures write are unaffected; a body 10.3p10's table demanded at the class's own
completion is already in `pending_`, and the end of the unit would have written
it beside the definition `require_replaceable` had just accepted. The mark is
taken at the same depth `function_definition` reads.

### What the review confirmed rather than found

- **The two 14.7.3p1 refusals are still refusals.** Two written definitions of
  one member, and two instantiated ones, are `is defined twice` here and in both
  oracles; `require_replaceable` throws under `instantiating_pattern_ > 0`, which
  is what keeps 14.5.4p1's second instantiation of a class defining a friend a
  redefinition.
- **The empty head parameterises nothing, at every exit.**
  `template<> int tag<int>::id()` is 13.1's redeclaration, `template<> void
  f<int>(int) {}` is still the function template's specialization, `template<>
  struct A<int> {…}` still records its body, `template<> template<class U> int
  box<void>::apply()` is still a member template, and `template<> struct A<int>;`
  ahead of a definition is still accepted — five shapes over the one line X
  changed in `read_template_head`.
- **14.7.3p6's new refusal is bounded by what the unit read.** A forward
  declaration of the specialization, and a use of a specialization the unit only
  named, still reach the definition; `sizeof(A<int>)` and `A<int> a;` written
  above it are refused, which is `g++`'s answer where the reference accepts.
- **Nothing scans and nothing re-reads per element.** `note_object` walks the
  specializations a template already has once per `template<>` definition of a
  member, `supersede` erases one hash entry, `from_pattern` is one flag test per
  definition written at the end of the unit, and `member_specialized` is one test
  per use lowered. n specializations, n explicitly specialized members, n uses of
  a superseded member and n specialized destructors are each linear to n = 800,
  and n members × n specializations is linear in its own n² uses.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch keyed on anything
  but a dialect, a timeout or an environment read. `valgrind -q
  --error-exitcode=9` is clean over 31 inputs. The two `course/pa22` fixtures
  were generated by `pa22/cppgm++-ref` and `g++ -pedantic-errors -x c++` agrees
  with both.
