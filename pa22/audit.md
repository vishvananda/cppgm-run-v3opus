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
| R | `c706f7d2` | 3 / 3 + 3 recorded | **the region an out-of-class definition's own head bound, recorded at the one exit that declares a function and derived from a class that may not be the one the head stands over - and the count 14.5.3p4 made a fact of the settled run, asked where nothing has settled it.**  Checkpoint R bound 14.1p2's names a definition written outside its class wrote: `TemplateInfo::reading_region` at the four sites that read a qualified declarator-id under a head, `Member::carried` one tier down, `open_bindings` and `open_member_parameters` standing those names beside this head's own.  That reading is right and swept clean - a member function template, its owner's own names, two heads renaming in opposite orders, a member alias template chain, a nest 40 deep, and the ABI names of thirteen such members agree with `g++` byte for byte.  What none of it carried is that 14.7.1p1 leaves the *body* to the use that names the member, so the region has to stand again where the body is finally read - and `PendingDefinition::stands_in` was written at `declare_function` alone, as `target.scope->parent`.  12's three entry points reach `queue_definition` through `open_special_member_body`, which named no class at all, and a declarator-id may name a class *nested below* the one the head stands over: `template<class A> outer<A>::outer()`, `~outer()`, `operator int()`, `outer<A>::inner::f()`, `outer<A>::inner::inner()`, `outer<A>::mid::inner::f()` and the static data member of each were programs `g++` and `pa22/cppgm++-ref` both accept and this build refused with **`no declaration of A is in scope`** - every one of them the moment the head spelled the class's place with a name of its own, and every one of them accepted where it spelled the class's own.  The pair is now asked once, at the one door every body the program wrote is queued through, of the region that body is read in.  Beside it, R's own clause - "the count is what the expansions came to and never the count of entries the list wrote" - was written at the exit where the run is settled and left at the exit where it is not: `one<int, As...>` inside `template<class... As> struct wrap` is `a template-argument-list gives one more arguments than it has parameters` at 14.6p8's reading of the pattern, where `As` stands for itself and no count exists - three programs both oracles accept, and `three<int, As...>` is the same clause's `too few` twin.  14.1p4 travels with it: an entry standing past the last place of a template that declared no pack has no place to be read at, so `num<3, Ns...>` was `Ns does not name a type` where what says type or value is the pack itself.  Recorded rather than fixed: `pa22/cppgm++-ref` refuses a member class template whose *inner* head renames, and emits a `declare function` its own LowIR never defines for a member of a class nested two deep, so the course fixture pins neither; and 9.2p9 is enforced nowhere - `struct A { A a; };` is accepted here and refused by `g++`, which is 9.2p1's shape one clause along |
| C | `02e5d6cc` | 3 / 3 + 4 recorded | **the two things 5.3.3p1 and 5.3.6p3 ask of one operand, asked by one of the three readings that write the operator - and the demand the other two make by reading the same text a second time.**  Checkpoint C made `TemplateArgumentReader` the second implementation of 5.19 that `SpelledTypeId` is of 8.1p1: 5.18p1's comma inside 5.1.1p6's parentheses alone, 5.2.9p4's discarded value refused at every reader that takes an operand's worth, 14.5.3p4's expansion in 5.2.2p1's argument list settled by `operand_end` before the operand is read, 14.2p4's keyword closed up inside a component, and 5.3.3p1's *expression* operand answered off the tree the parse kept beside the spelling it flattened to.  Those five are right and swept clean: 13 discarded-value shapes, 5 comma shapes, 10 expansion shapes - two packs, a nested call, a template-id pattern, an empty run, `sizeof...` written beside one - and 6 `::template` shapes agree with `g++` and with `pa22/cppgm++-ref`, and the kept tree's first-wins key is sound because one spelling is one parse, so `sizeof(g())` written in two namespaces over two `g`s is 2 and 8.  What none of it carried is the *other* two sentences of the same two clauses.  `TypeTable::object_align` is a table lookup that gives an incomplete class an alignment of zero, and the operator was written out of it at two of the three readings - so `S<alignof(wrap<int>)>` was **1 where `g++` and the reference both give 8**, at a template argument, in an array bound and in a static_assert alike, and `alignof(never)` over a class the unit only declared was a program both oracles refuse and this build ran.  Beside it 14.7.1p1's demand: `trait_value` probes the type-id in a reading of its own, which asks for no definition and leaves no mark for `require_complete_type` to read, and 14.6p8's reading leaves none either - so `sizeof(box<4>)` was `sizeof names an incomplete type` inside any class template's body and inside a static data member's initializer, three programs both oracles translate.  The commit made the demand by reading the operand's type-id a **second** time, which is one reading per level doubled at every level below it: a `sizeof` of a specialization nested in its own operand was 1.00 s at depth 16, 15.42 s at 20 and **killed at 60 s at 24**, where `pa22/cppgm++-ref` is 0.60 s flat.  `size_of` and `align_of` are now the one door each clause is answered at, and what they demand is `require_settled_type` - the demand asked of the type rather than of the mark, so it reaches a specialization no reading asked for.  Recorded rather than fixed: `A<sizeof x>`, 5.3.3p1's unparenthesized operand, is the sixth exit and is refused here where both oracles take it; and three shapes where `pa22/cppgm++-ref` parts from `g++` and from this build - a `sizeof` of the class whose own body writes it, an expansion whose pattern names no pack, and a pattern expanded over a run of none |
| B | `5f70d915` | 4 / 4 + 2 recorded | **the walk 11.2p4 opened, asked link by link and answered with the whole derivation read again at each of them - and the three clauses the checkpoint landed at one of the exits each is written at.**  Checkpoint B made 11.2p4's answer a question about the member *as a member of the naming class*: `Access::base_path` is the one walk down to the declaring class, `resolve_prefix` asks 11.2 of every component, and five clauses no door enforced got one.  The reach itself is right and swept clean - 10 base-path shapes, 10 per-component shapes, 12 of 5.1.1p13 and 8 of 11.3p2 and 3.4.3p1 all agree with `g++`.  What the walk carries is what it asks at each link: `base_accessible`'s 11.2p1 second sentence and `befriends_between`'s 11.2p5 each answered one link by walking the whole derivation, so a protected base chain of depth 512 was **0.97 s** and a befriending class between was **1.30 s**, both slower than `pa22/cppgm++-ref`'s 0.69 s and 0.56 s, where one walk of what the point derives from is 0.07 s and 0.04 s - the shape the checkpoint's own note says it avoided, fixed at the outer walk and left at both inner ones.  `Derivation::path` is the third: it opens a reader per link, which held that walk at d² until the reader is held for the length of the descent - 0.55 s to 0.09 s at depth 512 against the reference's 3.03 s.  Beside them, 14.5.6.1p5's value places were compared behind `a.type != kNoType && b.type != kNoType`, which the *new* head never satisfies because nothing binds its places before the comparison - so the arm was dead at its only caller and `template<int N> struct A; template<char N> struct A {};` was accepted where `g++` refuses; 14.5.4p1's grant was written at one of its three spellings, so a qualified `friend int n::peek<int>(…)` was refused where both oracles accept and an *unqualified definition* was accepted with **the body it wrote silently dropped**, where both oracles refuse; and 14.7.3p5 was asked of the class the declarator-id's last prefix names and of the `template<>`-over-a-declaration shape alone, so `template<> void A<int>::in::f() {}` and `template<> template<class U> void A<int>::g(U) {}` over an explicitly specialized class were two programs `g++` refuses and this build translated |


## Current Checkpoint Review

Checkpoint B is where 11 became one reader. 11.2p4 answers whether a member may
be named here *of the member as a member of the class the name was written on*,
and this build read the member's own access-specifier alone — so `derived::type`
reached a public member of a private base from anywhere. `Access::base_path` is
the one walk down to the class that declared the member, asking of each
base-specifier on the way what 11.2p1 asks; `resolve_prefix` asks 11.2 of every
component rather than of the last, with 14.2's template-id component asked of
the *template* the lookup found; and five clauses no door enforced got one -
14.5.6.1p5's equivalent parameter lists, 14.7.3p5's `template<>` over a member
of an explicitly specialized class, 5.1.1p13's id-expression naming a non-static
data member, 14.5.4p1's friend declaration whose declarator-id is a template-id,
and 11.3p2's `friend typename C::self;`. `sema_access.h/.cpp` is 11.2's reach and
11.3's grant as one reader, which is what freed both files' room.

The reach itself is right and swept clean. Ten base-path shapes — a private
base's typedef, its nested class, its member function, its data member, its
operator, its conversion function, a protected base from outside and from a
class derived from it, a friend of the derived class — agree with
`g++ -pedantic-errors` and with `pa22/cppgm++-ref` on acceptance and on value;
ten per-component shapes tell a private prefix from a public one at the first,
the middle and the last component, through a template-id component, from a
friend, from a member and from a class derived from the owner; twelve 5.1.1p13
shapes place the member in a mem-initializer, a default member initializer, a
`sizeof`, a `decltype`, an array bound, an enumerator, a template argument, a
static member's initializer and a default argument; eight more cover 11.3p2's
dependent friend, 11.3p3's ignored one and 3.4.3p1's class reached through a
typedef-name, through a place an argument list bound and through `this->`.
10.1p3 is what makes the walk well defined and it is enforced before any of it:
a class holding two subobjects of one base is refused where it is read, so a
walk that visits each class once costs the path and never the paths into it.

What none of it carried is **what the walk asks at each of its links, and the
three clauses whose refusal was written at one of the exits each is spelled
at.**

### Findings

**1. The walk answers each link by reading the whole derivation again, so a
protected base chain of depth d costs d² and this build was slower than the
reference.** `base_path` visits one class per level; `base_accessible`, which it
asks at every level, answers 11.2p1's second sentence — *a class derived from
the one that named the base reaches a protected base-specifier of it* — by
asking `derives_from` of every base of every class the access occurs in. The
classes the access occurs in do not move as the walk descends, so that is one
walk of the derivation per level over the levels below it. 11.2p5's
`befriends_between` is the same shape one clause along: it chose which base to
descend into by asking `derives_from` of each. `perf` on the depth-512 case put
**92 %** of the run in `derives_from`:

| depth (200 accesses) | before | after | `pa22/cppgm++-ref` |
|------|--------|-------|--------------------|
| 11.2p1 protected chain, 256 | 0.17 s | 0.03 s | 0.59 s |
| 11.2p1 protected chain, 512 | **0.97 s** | 0.07 s | 0.69 s |
| 11.2p5 befriending class between, 256 | 0.22 s | 0.02 s | 0.56 s |
| 11.2p5 befriending class between, 512 | **1.30 s** | 0.04 s | 0.56 s |
| 10.2 conversion through d base-specifiers, 512 | 0.55 s | 0.09 s | 3.03 s |

What the point the name was written at derives from is one walk of its own, made
where the first link asks for it and read by every link after — which is what
`Access::context_derives` is, and the reader is held for the length of the walk
that asks it. `befriends_between` became the same descent `base_path` already
makes. `Derivation::path` is the third site: it opened a reader per link, which
held that walk at d² until the reader became a member of the walk itself.

**2. 14.5.6.1p5's value places were compared behind a guard the comparison can
never pass.** The clause the checkpoint landed reads

```cpp
if (a.value && a.type != kNoType && b.type != kNoType &&
    place_signature(left, index) != place_signature(right, index))
```

and what a value place names a value of is settled only in 14.6.1p1's region,
which `open_region` opens lazily. The *new* head is a local `TemplateInfo` read
one line above, whose places nothing has ever bound — so `b.type` is `kNoType`
at every index and the arm was dead at its only caller:

```cpp
template<int N> struct A;
template<char N> struct A { };   // accepted; `g++` refuses
```

The comparison is one of the readings 14.6.1p1's region exists for, so it opens
it — of both heads, and only where the head declares a value place at any depth,
so the heads a program mostly writes open nothing. `template<template<int> class
F>` against `template<template<char> class F>` is the same refusal one level
down, because opening a head records the places of the heads its template places
wrote.

**3. 14.5.4p1's grant was written at one of the three spellings a friend
declaration names a specialization by — and the one that got no arm translated
by dropping the body it read.** A friend declaration whose declarator-id is a
template-id names a specialization of a template some region already declares
and declares nothing of its own, so all it does is grant. The checkpoint wrote
that at `declare_function_declarator`, under `!spelled.qualified()`:

```cpp
namespace n { template<class T> int peek(T); }
struct vault { friend int n::peek<vault>(vault); … };   // refused; both oracles accept
template<class T> void g(T);
struct A { friend void g<int>(int) { } };               // accepted, body dropped
```

The qualified spelling differs from the unqualified one by which region declares
the template, which `target` already names. And 8.4p1's form of the same
declaration reaches `function_definition`, which had no arm at all: it declared
nothing, emitted nothing and translated a program both oracles refuse — a
success path that succeeds by discarding what the program wrote.

**4. 14.7.3p5 was asked of the class the declarator-id's last prefix names, and
of the one shape that writes the head over a declaration.** A member of an
explicitly specialized class is defined the way an ordinary class's members are,
because the body the program wrote out has no member the pattern declared. The
refusal read `resolve_prefix`'s final region alone and `specialized_declarator_id`
stopped at the first declaration under the head:

```cpp
template<> struct A<int> { struct in { void f(); }; template<class U> void g(U); };
template<> void A<int>::in::f() { }              // accepted; `g++` refuses
template<> template<class U> void A<int>::g(U) { }   // accepted; `g++` refuses
```

14.7.3p5 is written about the class the *member* belongs to, and a declarator-id
may name a class nested below one the program wrote out — so every class the
prefix walked through is asked. 14.5.2p3 writes one head per class the member is
nested in, so the declarator-id a `template<>` clause parameterises is the
innermost declaration's, which is the descent `record_template` already makes.

### What the review confirmed rather than found

- **Nothing else re-reads and nothing scans per element.** `resolve_prefix` asks
  11.2 once per component of a name the program wrote, over the lookup that
  component made anyway; `require_component_access` is one `lookup_in` of the
  template a template-id names; `require_unspecialized_owner` is one walk up the
  regions `resolve_prefix` already resolved. n = 800 classes each with a
  protected base and one access through it is 0.12 s and 36 MB, equal to the
  turn-start build at every n measured; n = 800 redeclared heads over value
  places is 0.07 s and 26 MB against the turn-start build's 0.06 s and 20 MB -
  the 6 MB is the regions the comparison now opens - and against the reference's
  0.66 s. A public base chain is 0.04 s at depth 512, unchanged.
- **The corpus is unchanged.** 357 files one process per file is 1.63 s against
  the turn-start build's 1.63 s over a 0.59 s process floor, so 1.04 s of
  compiler work either way.
- **`valgrind -q --error-exitcode=9` is clean over 92 inputs**: the 79 probes of
  this review, its 5 largest scaling inputs, the fixture it adds and the 10.1p3
  refusal's own.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch keyed on anything
  but a dialect, a timeout, an environment read or a caught exception. The file
  audit passes with the five `bad-division` warnings it already had. All 48
  `course/pa22` fixtures regenerate byte for byte from `pa22/cppgm++-ref`,
  including the one this audit adds.
