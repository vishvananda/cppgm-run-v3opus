# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The owners PA19–PA22 left standing carry the stage, extended rather than
replaced:

- `ast_parser_*.cpp` with `ast_names.h` — the syntax boundary, and the one fact
  the parse has about a name no scope it models declares: what some declaration
  of the unit made the spelling. 14.2's `<` is settled from it. It is also where
  a tree an argument list flattened away is *kept*: `keep_spelled` holds the
  reading of a `decltype`, a `noexcept` and a parenthesized `sizeof` operand
  under the spelling it flattens to, because none of the three is a question the
  text can answer.
- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. One place per parameter, one reading per written argument, and
  what a place *is* settled once in 14.6.1p1's own region - which is opened by
  the first reading that needs it, and 14.5.6.1p5's comparison of two
  declarations' heads is one of those, because a value place compared before its
  own head is bound is compared against nothing.
- `sema_template.h/.cpp` — the template entity graph: `TemplateInfo` per
  template, `instantiate_class`/`specialize` per argument list, and `substituted`
  as the one door a dependent type comes back through. `record_template` is also
  14.5.4p1's tier: a head over a friend elaborated-type-specifier declares a
  class template of the enclosing namespace and grants to it.
- `sema_template_signature.cpp` — 14.5.6.1p5 alone: `TemplateSignatures` and the
  one canonical form per declaration that three tiers compare - a redeclaration
  of a function template, a definition written out for one, and 7.3.3p14's hiding
  of a base's member template. The places stand for their positions, the type is
  substituted onto them, and the stand-ins themselves stand at the end of it,
  because a place neither declarator mentioned stands in no type at all.
- `sema_overload.cpp` with `sema_argument_lookup.cpp` — 13.3 whole: one candidate
  set per call, and 3.4.2p3's answer about which lookups filled it. A callee the
  argument-dependent search still has declarations to add to denotes none of them
  yet, so 8.4.3p2's refusal and the line the callee is written on both wait for
  the choice - and 13.3.3.1p4's flag is held over the temporary a user-defined
  conversion builds, which is what leaves one such conversion in a sequence.
- `sema_class.cpp` — 12's special members, which 14.5.2p1 lets a head stand
  over.
- `sema_access.h/.cpp` — 11 whole: which contexts reach what a class declared,
  and 11.3's two ways a class gives that reach away. They are one reader because
  they are one question asked from either end - 11.2p4 walks from 11.2p5's
  naming class down to the class that declared the member and asks each
  base-specifier on the way what 11.2p1 asks, and every step of that walk asks
  11.3p1's record. Nothing here reads syntax: the two facts it stands on are
  settled before any name is looked up. What a step asks is a question about the
  *point* the name was written at, which does not move as the walk descends - so
  `context_derives` is one walk of what that point derives from, made where the
  first step needs it and read by every step after, and the reader is held for
  the length of the walk that asks it rather than opened at each of its links.
- `sema_function.cpp` with `Scope::hidden`/`hidden_index` — 11.3p6's chain of
  declarations one region holds and binds nothing of, indexed by the declaration
  it was made with so 7.3.1.2p3's reveal costs one probe. `declare_function` is
  also where 11.3p6 and 3.4.1p10 part company: a friend declaration is *made* in
  the namespace and *written* inside the class, and what 14.7.1p1 reads a
  pattern against is the second of those.
- `sema_definition_names.cpp` — 14.6p8's first of the two readings: the names a
  template definition writes, looked up where the definition stands.
- `sema_specialize.h/.cpp` — the three heads whose declaration the primary's own
  three steps cannot answer for: 14.5.5's partial specialization, 14.5.1p1's
  variable template and 14.5.7p1's alias template. `member_pattern` is also
  which of those bodies an out-of-class member definition was written over, and
  14.7.3p1's *other* question lives here too: which of the two definitions of a
  member of a class specialization this unit holds - the pattern's, read again,
  or the one the program wrote out for exactly those arguments.  The program
  writes the two in either order and the clause is about which one the unit
  holds, so each of the three doors asks it both ways: `supersede`, `note_object`
  and `require_replaceable` for a written definition arriving below the reading,
  and `holds_written_definition` for the reading arriving below the written one,
  which 14.7.1p1 then does not make at all.
- `sema_pattern.h/.cpp` — 14.6p8's reading of a template definition where it
  stands, and 14.6.1p1's class it is read as. One per body a naming may be read
  from - the primary's `TemplateInfo::current` and each `Partial::current` - and
  14.5.1.3p1's out-of-class member definition is that same reading arriving
  later, against whichever of them its declarator-id named. `owner` and
  `nested_owner` are 14.5.2p3's two tiers of one question, and each is asked
  *before* the class tier that would read the same nested-name-specifier as a
  prefix it must resolve.
- `sema_name.h/.cpp` — the one place a written spelling is turned back into what
  the program wrote: components, template-argument lists, and 14.2p4's keyword.
- `sema_derivation.cpp` — 10p1's tree, and the one walk down it every reader
  makes. 11.2p1's question about one base-specifier is asked from here and from
  `Access` alike, so `base_accessible` is written once and `link_accessible` is
  4.10p3's conversion asking it at the region the conversion stands in.
- `sema_deduce.cpp` — one P/A walk, shared by 14.8.2 and 14.5.5.1. What a type
  *is* is settled here as much as what it deduces: a function type's
  cv-qualifier-seq and ref-qualifier and a value argument's bits are compared,
  and a fixed place facing an entry that stands for a run takes nothing.
- `sema_type_id.cpp` — `SpelledTypeId`, the second implementation of 8.1p1's
  type-id and 8.3.5p1's parameter clause. 14.2 writes a template-argument-list
  inside a name, so an argument reaches the semantic layer as text; every rule
  the tree reading knows has to be written here too.
- `sema_value_expression.cpp` — `TemplateArgumentReader`, the same second
  implementation for the *other* kind of argument: 5.19's constant expression
  read out of one spelling. It owns the split into terminals - a name closes up
  with its argument lists, its `::`, 14.2p4's keyword and 5.3.3p1/5.3.6p1's
  parenthesized operand - and 5.19's own precedence walk, 5.18p1's comma inside
  5.1.1p6's parentheses, 5.2.9p4's discarded value, and 14.5.3p4's expansion
  inside 5.2.2p1's argument list. What no reading of words can answer it asks of
  the tree the parse kept: 5.3.7p3's operator and 5.3.3p1's expression operand.
  What 5.3.3 and 5.3.6 *come to* is not this reader's and not the type table's:
  `SemaAnalyzer::size_of` and `align_of` are the one answer apiece, because
  14.6p8's stand-in, p3's demand and p3's refusal are three things a lookup of a
  type's size cannot make and three readings write each operator. The demand is
  `require_settled_type` - asked of the type rather than of the mark
  `instantiate_class` leaves, so it reaches a specialization a reading that asked
  for nothing named, and so that no reader makes it by reading the same text
  twice.
- `sema_pack.cpp` — 14.5.3p4's expansion: which packs a pattern names
  (`collect_packs`, through a template-id whose template is a place), what an
  expansion comes to (`expand_type`), how a substitution reaches through one
  (`substitute_entry`), and `run_of`/`element_region`, the two primitives every
  by-element reading is built out of.
- `sema_declarator.cpp` — 3.4.3's walk of a qualified name, and the one place
  that says what a component no lookup answers *stands for*.
  `dependent_member_name` is the stand-in for a name written after a prefix no
  region was found for; `member_of_unknown_specialization` is 14.6.2.1p6's
  second door to the same stand-in - a class whose definition the reading has
  and whose base-clause an argument list has still to settle - and all three
  walks that look a component up ask it.
- `type_model.h/.cpp` — every argument list is a list of `TypeId`.
- `lowir_abi.cpp` — the ABI record for one argument, handed to PA14's encoder.
- `lowir_lower_body.cpp` with `LowValue::storage_owed` — 3.2p3 at the naming: a
  name of 9.4.2p3's member that reads the value asks the program for no storage,
  and the reader that takes the *place* is what asks. The claim to define an
  object is one line's rather than one entity's, which is what lets 14.7.3p1's
  written definition take it from 14.7.1p1's reading.

## Current Failure Map

The N audit stands at **348 / 363** — 293 of the 308 `tests/` fixtures plus all
55 `course/pa22` ones, the last two of which N wrote. The 15 that fail group by
the compiler behaviour that owns them, from the diagnostic each one reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 10 | compiles and the LowIR does not match | `lowir_*`, mixed | none; `extern template` suppression is 2 of them |
| 2 | 5.19's own readers over a name the value layer cannot spell | `sema_address.cpp`, `sema_value_expression.cpp` | `no declaration of slot::template impl<Handler> is in scope`, `no declaration of position<2,sizes[2]> is in scope` |
| 3 | one-offs | mixed | `abi-mangle: empty source name`, `an expression is outside the PA12 subset`, `PA20 does not instantiate` |

The four calls 13.3 had to resolve are gone: 7.3.3p14's hiding of a base's member
template, 5.2.3's parenthesized callee, 3.4.2p3's block-scope using-declaration
and 13.3.2p2's ellipsis constructor were four fixtures and are now four passes,
and the last of them was misdiagnosed - 13.3.1.1.2's surrogate set was already
built, and what refused the program was a converting constructor whose whole
parameter-declaration-clause is `...` and a call written on an object whose class
no naming had asked this unit for.

The two left are 5.19's *second* readers: `&slot::impl<Handler>` is
`ConstexprReading::designated`'s id-expression arm, which asks `resolve` for one
spelling and has no 13.4 candidate set to choose a function template
specialization from - the same program in a function body is read - and
`position<2, sizes[2]>` is `TermReader`'s missing 5.2.1 subscript. Both are the
shape C already named: a clause the tree reading knows and the by-spelling
reading does not.

Known gaps probed and deliberately left:

- 14.8.1p2 at a *member* callee reads the written argument list against the first
  declaration of the chain alone, so a chain holding a value place and a type
  place at position 0 is `3 does not name a type` for `d.e<3>(0)` and
  `Key is written as a template argument and names no constant` the other way.
  `template_specializations` reads the list once per candidate at the unqualified
  exit and the member exit does not; both oracles read both.
- 13.1 keys a class's chain by 8.3.5p4's parameter-type-list alone, so
  `template<int N> int e(int)` and `template<class T> int e(int)` in one class
  are `e is defined twice` where `g++` accepts. 14.5.6.1p5's own comparison now
  answers it - the stand-ins stand in `template_signature`'s form - but
  `declaration_signature`'s index is asked first and has no head to key on.
- 5.2.2p7's class object passed through the ellipsis: `A(...)` reaches the class
  from an argument of class type at 13.3, and the lowering then has `a value of
  the class type struct B is read where this milestone has no object to hold the
  copy 12.8p15 makes of it`. Both oracles run it; the unevaluated operand the
  PA22 fixture writes is the shape that matters and it is read.
- 5.4p2's ambiguity at a *nested* parenthesized cast is exponential in the
  nesting depth and was before this checkpoint too: `(W((W(1))(0)))(0)` nested d
  deep is 0.10 s at 14, 1.30 s at 18 and 5.51 s at 20, byte for byte the
  turn-start build's numbers, where `pa22/cppgm++-ref` is 5.51 s at **16**. The
  `Recognizer` memoizes its type-id attempt and `AstParser` does not.
- `(int (*)(char))(&f)` is refused by `pa22/cppgm++-ref` where `g++` and this
  build read it, and `(W())(2)` - a functional cast on a *temporary* whose
  spelling is also the type-id `W()` - is accepted here and by the reference and
  refused by `g++`, which is 5.4p2 read the way 8.2p1 writes it.
- 3.4.2p3's *first* bullet: a class-scope `using other::g;` naming a namespace
  function is accepted here and refused by `g++`, which is 7.3.3p3's requirement
  that a class-scope using-declaration name a base member.
- 3.2p1 is enforced nowhere at the object tier: `int g = 1; int g = 2;` at
  namespace scope, `const int S::v = 1;` written twice outside its class, and two
  `template<>` definitions of one member of one specialization are three programs
  `g++ -pedantic-errors` refuses and this build accepts with the first definition
  standing. It is the general object reading's and no part of what a template
  path owns - the function tier refuses all three shapes of it.
- 14.7.3p6 at the object tier: `template<> const int code<int>::value = 7;`
  written below a use of `code<int>::value` is accepted here and by
  `pa22/cppgm++-ref` and refused by `g++`, where X landed the same clause's
  refusal at the function tier. And `g++` refuses a `template<>` that
  re-initializes a member the class initialized in-class - `static const int
  value = 1;` in the pattern with `template<> const int code<int>::value = 7;`
  below it - where the reference and this build both take it.
- `A<sizeof x>` is 5.3.3p1's *unparenthesized* operand, the sixth exit of the
  reader C opened five: the split leaves `sizeof` a word of its own and the
  parse keeps no tree under a spelling whose end the enclosing expression is
  what says. Both oracles take it. Reaching it means the reader finding the span
  of one unary-expression before it can ask the parse for a key, which is the
  operand boundary 5.3.3 owns and not one the words carry.

- `Trait<int>::value && constraints<Traits...>::value` inside a partial
  specialization over a template-template pack is `no declaration of Trait<int>
  is in scope` here where `g++` and `pa22/cppgm++-ref` both run it. Either half
  alone is read: the recursion on its own and the bound place on its own are two
  programs this build takes. So a nested instantiation of the same template made
  *while* the outer body is being read is what loses the place - the same
  re-entrancy shape `StandingIn` has, and it belongs to the general instantiation
  path rather than to anything a value place owns.
- `pa22/cppgm++-ref` accepts an out-of-class definition of a member of a class
  nested **two** deep inside a class template and emits only a `declare
  function` for it — `_ZN5outerIcE3mid4deep1fEv` is a symbol its own LowIR never
  defines and `lowir2cy86` refuses to link — where `g++` and this build both
  write the definition and run its value. One level of nesting it defines, so
  the `course/pa22` fixture for 14.5.1.3p1's entry points pins that tier and
  stops short of the one the reference cannot materialize. Its inner-head twin
  is the same shape: a member class template whose out-of-class definition
  renames the *inner* head's places is `invalid sizeof type-id` there for a
  destructor, a constructor, a conversion function, an operator and an ordinary
  member function alike, all of which `g++` and this build run.
- 9.2p9 is enforced nowhere: `struct A { A a; };` is accepted, and so is a
  member whose type is the class an alias template in the same body names —
  `template<class U> using rebind = box<U>; rebind<int> other;` inside
  `box<T>` is `field 'other' has incomplete type` in `g++` and translates here.
  It is 9.2p1's shape one clause along, and like it belongs to the general class
  reading rather than to anything a template path owns.
- 5.19p2 at a static data member 9.4.2p2 defined outside its class: `code<char>::
  value` is folded in generated code where the unit writes no `template<>` for
  that member and read out of the object where it writes one, which is what
  `pa19/tests/general/300-class-template-static-member-out-of-class-definition`
  and `spec/300-explicit-specialization-static-data-member` pin between them and
  what `SemaEntity::member_specialized` carries into `storage_of`. The strict
  reading - 14.6.4.1p1 puts an instantiated definition's initialization at the
  end of the unit, so it precedes no use at all - is what `g++` gives and what
  the pa19 fixture refuses.
- 14.7.3p14 is what is left of the object tier U closed: an explicit
  specialization of a static data member with no initializer is a *declaration*,
  and `template<> int code<int>::value;` is read here as a definition of zero
  where both oracles leave the storage to another unit and fail to link.
  `note_object` is asked only where the definition wrote an initializer, so the
  declaration form reaches neither the withdrawal nor 14.7.3p1's own question -
  while `holds_written_definition`, which reads the *declaration* rather than the
  initializer, does leave the pattern unread below it.
- 3.2p3 is answered for 7.1.5p9's `constexpr` static data member and not for
  9.4.2p3's `const` one, because that is the line `pa22/cppgm++-ref` draws and
  two fixtures pin each side of it: `const int box<T>::k` read for its value
  alone is written out there and here and left out by `g++`, and the
  `constexpr` spelling of the same program is left out by all three. Every
  combination of the specifier on the declaration and on the definition was
  probed; the reference defers wherever either wrote `constexpr`.
- 3.2p2's use this build makes and `pa22/cppgm++-ref` does not: binding a
  `const int&` parameter to an instantiated `constexpr` static data member asks
  the program for the storage here and in `g++` and asks the reference for
  nothing.
- `template<> int tag<int>::f();`, a declaration of an explicit specialization of
  a member function, is read here as leaving the pattern's out-of-class
  definition in place: `g++` and `pa22/cppgm++-ref` both write a declaration and
  fail to link. The reference contradicts itself one spelling along, so the shape
  is recorded rather than pinned.
- One explicit class specialization written twice - `template<> struct A<int>
  {}; template<> struct A<int> {};` - is accepted here and refused by both
  oracles; 14.7.3p6's refusal is written about a specialization the *pattern*
  was read for, which this is not. And a `template<>` over a member **class** -
  `template<> struct tag<int>::inner {…};` - is `a template declaration of inner
  redeclares a name that is not a class template` here, where `g++` accepts and
  runs the written body and the reference accepts and runs the pattern's.
- `template<> outer<int>::inner::id()`, a member of a class a class template's
  body declares, is accepted here and by `g++` and refused by
  `pa22/cppgm++-ref`; the three special-member exits of 14.7.3p1 are the other
  way round, accepted here and by `g++` and refused by the reference. So no
  `course/pa22` fixture pins either: the ref would write the refusal into the
  `.ref`.
- `pa22/cppgm++-ref` accepts a member of a partial specialization of a member
  class template defined outside its class and then emits a `declare function`
  for it: `_ZN6holderIiE4slotIPcE5widthEv` is a symbol its own LowIR leaves
  undefined. Its static-data-member twin is worse - it leaves *both* bodies'
  globals undeclared. This build writes the definitions and runs the value
  `g++` gives.
- 14.5.2p3's declarator-id says which head parameterises which class by how far
  the region has already bound, so a head written over a class the *enclosing*
  head has yet to name is read as the enclosing one's. Every spelling the
  fixtures write is read; what is not is a definition whose second head names a
  member of a class the first head's own arguments do not reach.
- A **qualified** declarator-id written in a class *template*'s body is read
  against the region its nested-name-specifier names alone, so the enclosing
  head's places are on none of the regions around it: `friend int n::peek<>(
  box<T>);` and `friend int n::peek(box<T>);` inside `box<T>` are both `T does
  not name a type` where `g++` and `pa22/cppgm++-ref` accept, and the same
  declaration written at namespace scope is read. 3.4.1p8's region and 14.1p1's
  head are two things `looked_up` has to be, and it is one; the concrete
  spelling - `friend int n::peek<vault>(vault);` in a non-template class - is
  what the `course/pa22` fixture for 14.5.4p1's qualified grant pins.
- 11.3p11 leaves the name a friend elaborated-type-specifier first declares
  unbound until a matching declaration is written in the namespace, and both
  spellings bind it: `class host { friend class late; };` and `class host {
  template<class U> friend class late; };` each leave `late` findable, so `late*
  p = 0;` and `late<int>* p = 0;` are two programs `pa22/cppgm++-ref` and `g++`
  both refuse and this build accepts. The fix is one hidden *type* chain read by
  the two declaration sites - `class_declaration`'s elaborated arm and
  `record_template`'s class tier - rather than anything 14.5.4p1 owns.
- 14.5.4p1's grant is recorded in the lowering dialect alone, because PA11 and
  PA12 model a class template's class inside the head's own region and a friend
  declaration's class in the namespace, so the two can never be one entity
  there. The PA11/PA12 dumps themselves agree byte for byte.
- 9.2p1 is enforced nowhere: `struct A { int f(); int f(); };` is accepted, and
  so are two equivalent member-template declarations. `g++ -pedantic-errors`
  refuses all three.
- A conversion function template is keyed by the *spelling* of its
  conversion-type-id, so `operator U()` and `operator V()` over one head are two
  members and an out-of-class definition that renames its place matches none.
  `pa22/cppgm++-ref` refuses that shape outright, so no fixture pins it.
  14.8.2.3 at the *named* exit rests on the same fact: `a.operator int()`
  reaches no declaration here or in `pa22/cppgm++-ref`.
- `pa22/cppgm++-ref` cannot read a member *class* template whose out-of-class
  definition renames the enclosing head's places; `g++` accepts all four
  spellings and this build runs the value it gives, so the `course/pa22` fixture
  for 14.5.1.3p1's rename pins the three tiers the reference agrees on.
- An **empty** out-of-class destructor of a class template is elided by 12.4p8
  where `pa22/cppgm++-ref` and `g++` both write the definition.
- `template<class U> friend class W;` inside a class that is itself a *private*
  nested class, and then `W<T>` naming that class: we refuse where `g++` accepts
  and the non-template spelling of the same program is refused by `g++` too.
- 8.3.4p1's array bound is an `unsigned long long` on the type, and a bound the
  reading has not settled stands in as **1** - so `template<class T, unsigned
  long N> struct r<T[N]>` writes the pattern `T[1]` and `read_pattern` refuses
  the head outright. Deducing `N` needs a bound that is a `TypeId`, which the
  ABI's `ABI_ARRAY_BOUND_RAW` and every reader of `types_.bound` would move
  with it. `spec/100-class-partial-specialization-array-size.t` is the fixture.
- 14.3.2p5's conversion of a value argument at a *dependent* place is made
  where the argument is read and not where the place settles, so the entry
  `value_type(place, bits)` holds carries the source constant's bits. What is
  left is the pattern side: `probe<ic<T, 300> >` does not take `ic<char, 44>`,
  which is the answer `g++` gives too.
- 14.3.2p2's narrowing conversion at a value place is refused by neither this
  build nor `pa22/cppgm++-ref`, and `g++ -pedantic-errors` refuses both. 8.3.5p6
  is the same shape - `h<int(char)(long)>` and `h<int(char)[3]>` are function
  types this reading and the reference both build.
- A non-type argument of *pointer* type - `template<class T, T* p>` named
  `&g`, and `template<int* p>` too - is refused before the place is ever asked
  about: `TemplateArgumentReader` has no address arm. Both oracles accept.
- A rooted nested-name-specifier written directly after a type-specifier is not
  recoverable from the spelling PA10 flattened: `int ::C::*` arrives as
  `int::C::*`, one word. The reference reads it; every other spelling of the
  form is read here.
- One expansion over *two* function parameter packs - `template<class... A,
  class... B> int f(pr<A, B>...)` - deduces nothing here and nothing in
  `pa22/cppgm++-ref` either, where the class-tier spelling of the same rule now
  deduces in both. `g++` takes it.
- `pa22/cppgm++-ref` answers `split<box<> >` and
  `split<box<pr<int, char>, pr<long, short> > >` from the primary where the
  pattern `box<pr<A, B>...>` takes both here and in `g++`; a run of *one* it
  reads. So the course fixture for 14.5.3p4's two packs pins the run of one.
- `pa22/cppgm++-ref` reads three declarations of one pattern over a pack as three
  partial specializations and calls the naming ambiguous. `g++ -pedantic-errors`
  accepts it and so does this build.
- Three shapes of C's own surface where `pa22/cppgm++-ref` parts from `g++` and
  from this build, so no `course/pa22` fixture pins them: a `sizeof` of the
  specialization whose own body writes it - `template<int N> struct A { char
  pad[N]; static const int v = sizeof(A<1>); };` - is accepted there and refused
  here and by `g++`; `sum(1 ...)`, an expansion whose pattern names no parameter
  pack, is accepted there and refused by both; and `sum(0, Ns...)` over a run of
  none is refused there and taken by both.

## Active Checkpoint

**N landed and its audit closed 7.3.3p14's other source order and the head
14.5.6.1p5 compares — see the ledger.** The next one is **S**, 5.19's two
*second* readers, which are the last two refusals the PA holds:
`no declaration of slot<Signatures>::template impl<Handler> is in scope` is
`ConstexprReading::designated`'s id-expression arm, which asks `resolve` for one
spelling where 5.3.1p3's `&` over a function *template* wants 13.4p1's candidate
set and the specialization a target type chooses from it - the same program in a
function body is read - and `no declaration of position<2,sizes[2]> is in scope`
is `TermReader`'s missing 5.2.1 subscript, an operator the by-spelling reading
has no exit for at all. Owner `sema_address.cpp` with
`sema_value_expression.cpp`, data flow one reading per spelling with the tree the
parse kept under it as the second door, expected cost one candidate set per `&`
and one operand split per subscript, and validation the two fixtures plus a sweep
of 5.19's remaining operators at both readers.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp/perf22*`,
against `pa22/cppgm++-ref` and, where a row says so, against the turn-start
build in a worktree of the checkpoint before the row's own (`32ba715b` for C's).
A turn-start build that **refuses** the generated input times a refusal and not
the work, so such a row says so rather than carrying the number. Eleven traps are
recorded rather than re-measured: `timeout`/`date` spawned per run invents a
~0.1 s floor that reads as 33 s over the corpus, `cppgm++` run by hand needs
`-o` or it compiles nothing, a relative binary path measures 0.00 s from a shell
whose directory moved, the whole corpus handed to one process is one ill-formed
unit and times as 0.00 s, `/usr/bin/time` writes to stderr so a child whose
stderr is discarded loses every measurement, `g++ file.t` treats a `.t` as a
*linker input* and `-x c++` written *after* the input file has no effect, `bc` is
absent so a best-of-three written around it silently keeps the first run,
`date +%s.%N` interpolated into `python3 -c` loses the second reading, `make
ref-test` regenerates only `tests/` - a course fixture needs `TEST='course/pa22/
x.t'` spelled **relative** - a git worktree cannot be added under
`/home/vishvananda/work`, which is read-only, `cppgm++ … | head` reports the
*pipeline's* status, so a **segfault reads as exit 0** and a probe judged that
way says the opposite of what happened, and the *first* pass over a corpus
measures the page cache and not the compiler: 2.62 s cold against 1.53 s warm.
Every generated input is checked for exit 0 before it is timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| n partial specializations of one template over a function type, each named | 100 → 800 | 0.01 → 0.23 s, 10 → 38 MB | 10.62 s at 800 |
| n namings of a pattern that writes 14.8.2.5p5's non-deduced context | 100 → 800 | 0.01 → 0.12 s, 10 → 39 MB | 0.91 s at 800 |
| n pointer-to-member type-ids written as template arguments | 100 → 800 | 0.01 → 0.09 s, 9 → 32 MB | 0.82 s at 800 |
| a function type of n parameters written as a template argument | 100 → 800 | 0.00 s, 6 → 7 MB | 0.54 s at 800 |
| one pack pattern matched against a run of n | 800 → 3200 | 0.00 → 0.01 s, 7 → 8 MB | 0.64 s at 3200 |
| *two* packs expanded together over a run of n | 800 → 3200 | 0.01 → 0.03 s, 8 → 13 MB | 1.21 s at 3200 |
| a function type reached through d nested member typedefs | depth 4 → 256 | 0.00 → 0.02 s, 6 → 13 MB | 0.56 s at 256 |
| **O** n patterns of one template, each with an out-of-class member definition, each named | 100 → 800 | 0.02 → 0.36 s, 13 → 59 MB | 12.13 s at 800 |
| **O** a member class template nested d deep, each level defined out of class | depth 4 → 40 | 0.00 → 0.09 s, 6 → 16 MB | 17.78 s and **7.99 GB** at 24; killed at 32 |
| **O audit** n specializations of one pattern that has 8 out-of-class members | 100 → 800 | 0.03 → 0.26 s, 13 → 64 MB | — |
| **X** n class specializations, each with one explicitly specialized member | 50 → 400 | 0.00 → 0.04 s, 8 → 18 MB | 0.68 s at 400 |
| **X audit** n `template<>` static members of one template, over 8 specializations | 100 → 800 | 0.04 → 0.34 s, 14 → 69 MB | 0.75 → **21.27 s** |
| **X audit** n `template<>` static members × n specializations, n² uses | 20 → 80 | 0.02 → 0.29 s, 10 → 60 MB | 0.62 → 1.92 s |
| **R** n out-of-class member function templates of one class template, each renaming the owner's places | 100 → 800 | 0.02 → 0.19 s, 12 → 50 MB | 1.04 → **28.48 s** and 3.66 GB |
| **R** a member class template nested d deep, every head renaming the places above it | depth 4 → 40 | 0.00 → 0.03 s, 7 → 11 MB | 23.81 s and **8.53 GB** at 24; killed at 40 |
| **R audit** n out-of-class members of one nested class, each head renaming | 100 → 800 | 0.01 → 0.11 s, 10 → 32 MB | 0.88 → **22.25 s**; the turn-start build **refuses** |
| **R audit** a class nested d deep in a class template, one out-of-class member | depth 4 → 64 | 0.00 s, 7 → 8 MB | 0.53 → 0.54 s; the turn-start build **refuses** |
| **C** n operands written in one call as a template argument | 100 → 800 | 0.00 s, 7 → 9 MB | 1.13 s at 800 |
| **C** one pattern expanded into a call's arguments over a run of n | 400 → 3200 | 0.00 → 0.03 s, 9 → 22 MB | 12.46 s at 3200 |
| **C** calls nested d deep inside one template argument | depth 4 → 128 | 0.00 → 0.01 s, 7 MB | **killed at 120 s** at depth 24 |
| **C** n `sizeof` type-ids in one argument spelling, each a specialization | 100 → 800 | 0.00 → 0.01 s, 6 → 7 MB | 1.73 s at 800; the turn-start build **refuses** |
| **C audit** n `alignof` type-ids in one argument spelling, each a specialization | 100 → 800 | 0.00 → 0.01 s, 6 → 7 MB | 0.70 s at 800 |
| **C audit** a `sizeof` of a specialization nested d deep in its own operand | depth 8 → 128 | 0.00 → 0.01 s, 6 → 7 MB | 0.60 s flat; the turn-start build is **2^d** — 15.42 s at 20, killed at 60 s at 24 |
| **C audit** n class templates, each named once under `alignof` at a declarator | 100 → 800 | 0.02 → 0.16 s, 9 → 29 MB | 1.21 → 1.44 s; the turn-start build is 0.08 s and **answers 1** |
| **C** n member class templates of one class template, each defined out of class, against the turn-start build | 50 → 400 | 0.03 → 0.25 s, 9 → 28 MB | 0.03 → 0.24 s there; ref 1.73 s |
| **C** n nested classes of member class templates, each defined out of class | 50 → 400 | 0.06 → 0.49 s, 11 → 40 MB | 2.03 s at 400; the turn-start build **refuses** |
| **B** a member inherited from a class d deep in a chain, named 200 times | depth 4 → 512 | 0.00 → 0.05 s, 7 → 12 MB | 0.55 → 3.01 s; the turn-start build is the same 0.05 s |
| **B** n classes, each with one base, each with one member access through it | 100 → 800 | 0.02 → 0.18 s, 11 → 48 MB | 1.33 s at 800; the turn-start build is the same 0.18 s |
| **B** n three-component qualified paths through one specialization | 100 → 800 | 0.00 → 0.01 s, 6 → 8 MB | 0.63 s at 800; the turn-start build is the same 0.01 s |
| **B** n `template<>` member definitions, each over its own specialization | 100 → 800 | 0.01 → 0.07 s, 9 → 24 MB | 0.76 s at 800; the turn-start build is the same 0.07 s |
| **B audit** 11.2p1's protected base chain of depth d, 200 accesses | depth 64 → 512 | 0.01 → 0.07 s, 8 → 16 MB | 0.55 → 0.69 s; the turn-start build is **d²** — 0.17 s at 256, 0.97 s at 512 |
| **B audit** 11.2p5's befriending class between, chain of depth d, 200 accesses | depth 64 → 512 | 0.01 → 0.04 s, 8 → 15 MB | 0.56 s flat; the turn-start build is **d²** — 0.22 s at 256, 1.30 s at 512 |
| **B audit** 10.2's conversion through d base-specifiers, 200 conversions | depth 64 → 512 | 0.02 → 0.09 s, 10 → 18 MB | 3.03 s at 512; the turn-start build is 0.12 s at 256 and 0.55 s at 512 |
| **B audit** a public base chain of depth d, 200 accesses | depth 64 → 512 | 0.01 → 0.04 s, 8 → 16 MB | — |
| **B audit** n classes, each with a protected base and one access through it | 100 → 800 | 0.01 → 0.12 s, 9 → 36 MB | — ; the turn-start build is the same 0.12 s |
| **B audit** n redeclared heads over value places, each compared | 100 → 800 | 0.01 → 0.07 s, 8 → 26 MB | 0.66 s at 800; the turn-start build is 0.06 s and 20 MB |
| **U** n names of a member of an unknown specialization, one class | 100 → 800 | 0.00 → 0.02 s, 6 → 12 MB | 0.54 → 0.69 s; the turn-start build **refuses** |
| **U** a chain of d classes, each over a dependent base, each naming through itself | depth 100 → 800 | 0.01 → 0.24 s, 8 → 34 MB | 0.55 → 1.19 s; the turn-start build **refuses** |
| **U** n specializations of one such class, each reading the stand-in | 100 → 800 | 0.01 → 0.15 s, 11 → 44 MB | 0.64 → 1.73 s; the turn-start build **refuses** |
| **U** n instantiated `constexpr` static members, each named for its value | 100 → 800 | 0.01 → 0.09 s, 9 → 29 MB | 0.56 → 0.76 s; the turn-start build is 0.01 → 0.10 s |
| **U audit** n class templates over a dependent base, each naming a member of an unknown specialization | 100 → 800 | 0.02 → 0.15 s, 10 → 41 MB | 0.61 → 1.28 s; the turn-start build **refuses** |
| **U audit** a chain of d classes over a dependent base, each naming through itself | depth 100 → 3200 | 0.01 → 4.69 s, 9 → 182 MB | 0.55 → 1.03 s; the non-template twin is 0.22 → 4.04 s here **and at the turn-start build** |
| **U audit** n members of one template, each explicitly specialized above the pattern's own definition | 100 → 800 | 0.01 → 0.13 s, 10 → 32 MB | 0.61 → 1.31 s; the turn-start build **refuses** |
| **U audit** n specializations of one such member | 50 → 400 | 0.00 → 0.04 s, 8 → 16 MB | 0.57 → 0.84 s; the turn-start build **refuses** |
| **N** n member templates of one name in a base, hidden by n of the derived class's, the using-declaration written first | 50 → 800 | 0.11 → 0.31 s | 1.01 → 2.50 s; the turn-start build is 0.10 → 0.21 s |
| **N** the same with the using-declaration written last | 50 → 800 | 0.10 → 0.20 s | 0.60 → 2.10 s |
| **N** n calls of an ADL-reached name found through a block-scope using-declaration | 200 → 3200 | 0.10 s flat | 0.60 → 1.00 s; the turn-start build is the same 0.10 s |
| **N** n parenthesized functional casts called as objects | 500 → 8000 | 0.10 → 0.50 s | 0.70 → 2.50 s; the turn-start build is 0.10 s and reads every one as a cast |
| **N** 5.4p2's ambiguity at a cast nested d deep | depth 12 → 20 | 0.10 → 5.11 s | 5.51 s at depth **16**; the turn-start build is the same 5.11 s |
| the whole 363-file corpus, one process per file | — | **1.54 s** | 1.53 s at the turn-start build; the loop's own floor is 0.70 s |
| *(carried from F)* n friend declarations of one name, each revealed | 800 → 3200 | 0.19 → 0.79 s | 22.40 s at 800 |

B's own cost is the walk 11.2p4 added, and it is paid once per access at a
naming class the member was not declared in - every other access returns before
it. The walk that finds the path visits one class per level rather than asking
reachability again at every level, and so does everything it asks *at* a level:
11.2p1's second sentence and 11.2p5's befriending class between are questions
about the point the name was written at, which does not move as the walk
descends, so what that point derives from is one walk of its own -
`Access::context_derives`, made where the first link needs it and read by every
link after, with the reader held for the length of the walk that asks it. Asking
`derives_from` per link instead is that walk once per level over the levels below
it: **0.97 s** at depth 512 for a protected chain and **1.30 s** for a
befriending class between, where `perf` put 92 % of the run in `derives_from`
and where the one-walk shape is 0.07 s and 0.04 s and the reference is 0.69 s
and 0.56 s. `Derivation::path` asks the same question per link of its own
descent and opened a reader at each of them, which is the same d²: 0.55 s at
depth 512 against 0.09 s once the reader is a member of the walk, where the
reference is 3.03 s. The other three doors cost one call apiece of work already
done: `require_access` at a prefix component reads the entity the component's own
lookup returned, `require_component_access` is one `lookup_in` of the template a
template-id component names, and `require_unspecialized_owner` is one
`resolve_prefix` per `template<>` head - which the declaration below it makes
anyway - and one walk up the regions it resolved. 14.5.6.1p5's comparison opens
14.6.1p1's region of either head, which is 6 MB over 800 redeclared heads and
nothing at all for a head that declares no value place.

C's four costs are each paid once and nothing scans. `operand_end` is one
forward scan of the words the reading below it is about to read anyway, made
before the operand rather than after it because 14.5.3p4's `...` stands *after*
the pattern and a pattern that is a pack's own name runs out a word before the
reading reaches the ellipsis that says how to read it - a list nested d deep
scans its own contents once per level, which is 0.01 s and 7 MB at depth 128
where `pa22/cppgm++-ref` is killed at 120 s at depth 24. `expand_operand` is one
reading of the pattern per element, over the words already split, so a run of
3200 is 0.03 s against the reference's 12.46 s. 14.7.1p1's demand under
`sizeof` and `alignof` is one call of `require_settled_type` on the type the
probe already has in hand, made inside `size_of` and `align_of` so that every
reading of either operator makes it: the second `template_argument_type` the
checkpoint wrote in its place was a *reading* per level doubled at every level
below it - 15.42 s at depth 20 and killed at 60 s at 24, where the reference is
0.60 s flat and this build is 0.01 s at depth 128. What the demand costs is the
work it was owed: 800 class templates each named once under `alignof` at a
declarator is 0.16 s and 29 MB against the reference's 1.44 s, where the
turn-start build was 0.08 s because it laid none of them out and answered 1. And
asking
`nested_owner` before the class tier costs one components walk per second-head
declaration, which the tier below made anyway: n = 400 member class templates
defined out of class is 0.25 s against the turn-start build's 0.24 s.

N's own cost is one canonical form per declaration and one failed
abstract-declarator per parenthesized callee, and both are held or bounded.
7.3.3p14's key over a member template is 14.5.6.1p5's substitution, and a class's
declarations of one name are walked three times over one using-declaration - once
where it is written and twice where 9.2p2 completes the class - so the form is
held under the declaration's id in `TemplateSignatures::built`, the same memo the
clause's own comparison already keeps: 800 member templates hidden by 800 is
0.31 s against the turn-start build's 0.21 s and the reference's 2.50 s, and it
grows with n and not with n². The parser pays one abstract-declarator attempt per
`(` that a declarator-id then ends, which is bounded by the words of that one
parenthesis: 8000 parenthesized functional casts is 0.50 s against the turn-start
build's 0.10 s - where it read every one of them as a cast - and the reference's
2.50 s. 5.4p2's own ambiguity nested d deep is exponential and was before this
checkpoint: 5.11 s at depth 20 in both builds, byte for byte, where the reference
is 5.51 s at depth **16**. 3.4.2p3's deferral costs nothing measurable - the
gathering it lets happen was already made at every other unqualified call - and
14.7.1p1's demand that replaces the naming is one `instantiate` of the one
specialization an explicit argument list already made. 13.3.3.1p4's flag over the
temporary a user-defined conversion builds is one bool held and restored, and it
is what stops the copy constructor from being a second way into a class whose
only converting constructor takes the ellipsis - a regress with no bottom before
it. The whole 363-file corpus is 1.54 s against the turn-start build's 1.53 s.

U costs nothing that scans. `member_of_unknown_specialization` is four field
reads on a region a lookup has already failed in, so a program with no dependent
base never reaches it and one with a dependent base pays it once per name the
class does not declare; the stand-in it hands back is `dependent_member_name`'s,
which is memoized by prefix and component, so n names of one class is n entries
and n specializations reading them is n substitutions of a type each already
interned. 3.2p3's door is one bool per naming and one pointer on the value: the
demand it defers is the demand the address path makes, so nothing is asked twice
and nothing is asked that was not owed - 800 instantiated `constexpr` members
named for their value is 0.09 s against the turn-start build's 0.10 s, which is
the same work minus 800 definitions nothing reached. `object_definitions_` is
one pointer written where a definition line is opened and read once per
`template<>` the program writes, which is what makes 14.7.3p1's withdrawal a
lookup rather than a walk of the dump.

The U audit's own door costs five field reads per definition read.
`holds_written_definition` is asked where 14.7.3p1's other half is already asked
- at `declare_function`, at `declare_object_declarator` and inside
`require_replaceable` - so nothing walks and nothing is looked up: 800 members of
one template each explicitly specialized above the pattern's own definition is
0.13 s and 32 MB against the reference's 1.31 s, and 400 specializations of one
such member 0.04 s against 0.84 s, where the turn-start build refuses both. The
one dimension that is not linear is 10.2's own and not U's: a chain of d classes
each naming a member through its own derivation is d² here - 0.27 s at 800, 4.69 s
at 3200 - and the **non-template twin** of that program, `struct p_i : p_{i-1} {
typedef p_i::value_type v; };`, is the same 4.04 s at the turn-start build, which
never compiled the dependent spelling at all. The reference is linear on both
(1.03 s and 1.52 s), so the walk of a derivation chain per class is where a later
checkpoint would look and no part of what 14.6.2.1p6 opened.

The corpus row is re-measured after the U audit over the same 360 files with a
harness that reads the file list once rather than spawning a timer per run, and
after a warm-up pass: 1.62 s of wall clock against the turn-start build's 1.64 s,
over a 0.70 s floor the loop's own fork-and-exec costs - so 0.92 s of compiler
work against 0.94 s, about 2.6 ms per fixture. Read cold, or with the audit's own
fixture in the list, the same measurement is 2.6-2.8 s, which is the page cache
and the one heavy input and not the compiler.

Every dimension is linear in what it sweeps and flat in depth except 14.5.5.1p1's
own: n patterns beside one template and n lists naming it is n candidate scans of
n, which is what the program wrote. `perf` on the n = 800 case puts 28 % of the
run in `Deduction::match` and `match_arguments` and **0.08 %** in
`SemaAnalyzer::substituted`. It is memoized by `TemplateInfo::chosen`, which keys
the whole choice by the interned argument list. The reference is exponential in
member-class-template nesting - 0.53 s at depth 8, 17.78 s and 7.99 GB at 24, the
OOM killer at 32 - where this build is 0.02 s and 9 MB at 24; reading each level
once against the class the level above declared is what keeps it flat. Earlier
checkpoints' per-element costs are unchanged: `match_run` copies a map of one or
two entries however long the run is, `SpelledTypeId::suffix` reads `expand_type`
once per written parameter, `enclosed_by_a_head` is one walk per queued body,
`TemplateInfo::reading_region` and `Member::carried` are pointers written where
a definition is recorded, and `instantiating_pattern_` is one unsigned compare.

`valgrind -q --error-exitcode=9` is clean over 78 inputs after the U audit, 0
errors: its 74 probes and its 4 largest scaling inputs. It was clean over 46
after U, 92 after the B audit, 79 after B, 69 after the C audit, 58 after C, 62
after the R audit, 34 after R, 37 after X, and 57 after the O audit.

U's run evidence: all 36 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref` - 10 shapes of 14.6.2.1p6's
member of an unknown specialization over a type, a value, a function, a nested
class, a middle component of the prefix, a second settled base and two names no
argument list ever declares, 10 of 14.6.2.2p1's `decltype` at a return type, a
parameter, a parenthesized operand, a function, an enumerator, a nested prefix,
a `sizeof` written as a template argument and the current instantiation's own
base, and 16 of 3.2p3 at a value read alone, an address taken, a reference
bound, a by-value and a by-reference argument, an array bound, a template
argument, an enumeration member, an array member, a namespace-scope initializer
and a `template<>` written for one list. They agree with `g++` on 34. The two
are shapes `pa22/cppgm++-ref` answers differently from `g++` and from this
build, so no `course/pa22` fixture pins them: binding a `const int&` parameter
to an instantiated `constexpr` member is 3.2p2's use here and in `g++` and asks
the reference for nothing, and a `const` member of one specialization read for
its value alone is written out here and by the reference and left out by `g++`.
Every probe that translates runs through `lowir2cy86` + `cy86` and returns the
value `g++` gives it, and the three `course/pa22` fixtures U adds do too.

The U audit's run evidence: all 74 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref`, and agree with `g++` on
acceptance *and on value* on 67 - 12 shapes of `LowValue::storage_owed` over an
address and a value read in either order, a conditional, a parenthesized name, a
comma expression, a `static_cast` to a reference, a namespace-scope initializer,
a `const int&` parameter, a mem-initializer, a default argument, a returned
reference and an instantiated function template; 20 of 14.6.2.1p6's stand-in over
a middle component, a decltype prefix, a template argument, a value place, an
array bound, a `this->` access, a three-deep nest and the four refusals it has to
leave standing; 22 of 14.7.3p1 written in either order at all six tiers a member
definition can be written at; 6 multiplicity shapes over two members, two
specializations and an enumeration type; and 9 refusals the new door may not
swallow. The seven that differ are refusals `g++` makes and this build does not,
each recorded above. `pa22/cppgm++-ref` parts from `g++` and from this build on
nine: it refuses `&(true ? a : b)`, `&(a)` and `&(0, a)` outright, segfaults on
a reference `static_cast` of a folded member, calls an out-of-class `constexpr`
definition of a non-template class's member a mismatched redeclaration, answers
1 where both give 7 for a member function template specialized above its
pattern, refuses a second `template<>` written between two pattern definitions,
accepts an unqualified name 14.6.2p3 does not look up in a dependent base, and
leaves an explicitly specialized member with no initializer undefined in a way
its own LowIR cannot link. Every probe that translates runs through
`lowir2cy86` + `cy86` and returns the value `g++` gives it, and the
`course/pa22` fixture the audit adds - which the turn-start build refuses with
`plain is defined twice` - does too.

B's run evidence: all 58 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref` - 12 shapes of 11.2p4's base
path, 8 of 11.2 at a nested-name-specifier's own components, 8 head-equivalence
shapes, 7 of 14.7.3p5's `template<>` over a member, 10 of 5.1.1p13's non-static
data member, 8 friend shapes and 3 qualified member accesses - and agree with
`g++` on 55. The three are one shape: `int owner::*p = &owner::value;` is
`an expression is outside the PA15 lowering subset` here and at the turn-start
build alike, which is pointer-to-member-data lowering and no part of 11. Nine of
the 58 are shapes `pa22/cppgm++-ref` answers differently from `g++` and from this
build - the two `template<>`-over-an-explicit-specialization refusals, a
protected prefix component, a friend template-id naming no template, a qualified
member access on a class the object is not of - so no `course/pa22` fixture pins
them. The eight fixtures B wrote are each accepted or refused alike by all three
oracles, and every one that translates runs through `lowir2cy86` + `cy86` and
returns the value `g++` gives it.

The B audit's run evidence: all 79 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref`, and agree with `g++` on 77 -
10 base-path shapes over a private or protected base, 10 per-component shapes at
a first, a middle and a last component, 12 of 5.1.1p13's non-static data member,
12 head-equivalence shapes including a value place's type, an enumeration place
and a place inside a template place's own head, 12 of 14.7.3p5 over a nested
class, a member template, a constructor and a conversion function, 10 friend
template-id shapes over the qualified and unqualified spellings of a declaration
and of a definition, and 8 of 11.3p2's dependent friend and 3.4.3p1's class
reached through a name. The two are one shape: a *qualified* declarator-id
written in a class template's body loses the enclosing head's places, recorded
above, and the template-id spelling of it fails for the same reason the plain
one does. Every probe that translates runs through `lowir2cy86` + `cy86` and
returns the value `g++` gives it, and the `course/pa22` fixture the audit adds
does too.

The C audit's run evidence: all 65 probes are judged against `g++ -std=c++11
-pedantic-errors` and against `pa22/cppgm++-ref`, and agree with `g++` on every
one - 13 discarded-value shapes and 5 comma shapes, 12 `alignof` shapes over a
specialization, a reference type-id, an array bound, a static_assert and a class
template's own body, 10 expansion shapes including two packs together, an
expansion nested in a call inside another, a template-id pattern and a run of
none, 6 `::template` shapes, and 6 `sizeof`-off-the-kept-tree shapes including
one spelling written over two different overload sets in two namespaces. Every
probe that translates runs through `lowir2cy86` + `cy86` and returns the value
`g++` gives it, and the `course/pa22` fixture the audit added does too. Three of
the 65 are shapes `pa22/cppgm++-ref` answers differently from `g++` and from this
build, recorded above.

C's own run evidence stands under the audit's: its 52 probes agree with `g++` on
every one, its four `course/pa22` fixtures and the seven it turned green compile
through `lowir2cy86` + `cy86` and return the value `g++` gives them over two
translation units as well as one - where the two `mark` symbols the units owe
are owed once each and weak - and the two mangled names that pins,
`_ZNK7adaptorIiE5rangeILi7EE8iterator4markEv` and its `Li14E` twin, agree with
`g++` and with `pa22/cppgm++-ref` byte for byte. Earlier run evidence stands: the twenty-one out-of-class
shapes the R audit turned green, R's member class template nest and member
alias template shapes, X's twenty-one `template<>` shapes, O's twenty
out-of-class shapes and their six mangled names, and D's function-type and
pointer-to-member manglings, all agreeing with `g++` and, where it writes one,
with `pa22/cppgm++-ref`.

N's run evidence: all 31 probes are judged against `g++ -std=c++11
-pedantic-errors -x c++` and `pa22/cppgm++-ref`, and every accepted one is built
by `g++` and run. The four fixtures the checkpoint turned green are joined by
seven abstract-declarator shapes that must still read as type-ids - `int (*)(char)`,
`int (*[3])(char)`, `int S::*`, `int (*const *)(char)`, `int (&)[3]`,
`int (*(*)(char))(long)` and a function pointer written as a template argument -
seven 7.3.3p14 shapes over both source orders and the four boundaries that hide
nothing, six 3.4.2p3 shapes including the block-scope function declaration that
*does* suppress and the deleted declaration 13.3 still chooses, seven 13.3.2p2
shapes over `explicit`, `= delete`, a written parameter that outranks the
ellipsis and the copy constructor 13.3.3.1p4 shuts out, and five object-call
shapes over a reference to a specialization, an `extern template` one, a
dependent one and a class only declared. The two `course/pa22` fixtures it wrote
are refused by the turn-start build - `also is defined twice` and `a comparison
has operands of unrelated types` - accepted by both oracles, and run to 0 by
`g++`.

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|------------|-------------|-----------|
| **T** 14.1p2's template place | A `type-parameter` written `template<…> class` binds a template: its own clause is a head read once per clause node, a written argument is `TypeKind::TemplateName` interned per declaration, and the place's name is bound *to that declaration*. 14.3.3p1 matches the two heads by kind, by a value place's own signature, and with a pack on either side taking the rest. | 142 / 308 |
| **T2–T5, T audit** the place's own default, its object-file name, 14.1p11 and the region an argument associates | 14.1p2's default at a template place is an id-expression naming a template; `TypeKind::TemplateName` gained an `operand_of` arm, so two templates no longer intern as one type; 14.1p11 is written about a head an argument list is read against, so a pack stands anywhere in a partial specialization's; 3.4.2p2 gives an argument the region that declares the template it named; and 14.3.3p1 was asked at the class tier and at neither exit of the function tier. | 156 / 308 |
| **A, A audit** 14.5.7p1's alias template, and the three regions a template-id is looked up in | `template<…> using X = T;` records a head and a *pattern that is a type-id*, so 7.1.3p2 makes `X<A…>` another name for the type the arguments substitute into it; the declaration is a `Typedef` carrying a `TemplateInfo`. `resolve` answered a template-id at both its exits where 5.2.5p1's member lookup answered it at neither, and `QualifiedName::prefix` is read off the split rather than by `last().size()`. | 193 / 308 |
| **P, P audit** the three places a template-argument-list is read, and the two forms 14.7.2 writes one requirement in | 14.2p4 makes the keyword optional wherever the object expression is not type-dependent, so `h.get<int>(4)` is a template-id the parse has to recognise with no keyword to lean on: `DeclaredNames::names_a_template` answers it from the unit-wide record, and 5.2.2p1 bounds the guess to a list a `(` follows. `explicit_instantiation` returned on `!owed` before reading its target, so p2 was asked of nothing `extern template` wrote. | 200 / 308 |
| **M, M2, M audit** 14.5.2's member template, its four definition exits, and the two facts 12 writes about a special member | A head over a constructor or a conversion function inside a class body declares a member *template* of that class, which reached neither `function_definition` nor `declare_function`; 3.4.1p8 puts the head *inside* the region its declarator-id names; `specialize` copied `object_member` and `access` but not which special member it declares, so 12.6.2's mem-initializers never ran; and 12.3.2p1's conversion function template reached *no* use at all - `Deduction::from_conversion` is 14.8.2.3's one P/A pair. | 229 / 318 |
| **F, F audit** 14.5.4's friend templates, and the two readings one friend definition gets | `SemaModel::befriended` asks the pair as the use spelled it and then with each side replaced by its `primary`, which is what 14.5.4p1 means. A friend declaration written in a class *template* is read twice and 11.3p6 puts both in one namespace, so the pattern reading's `defined` made one instantiation `unwrap is defined twice`; and `record_function_template` recorded the *namespace* as the region 14.7.1p1 reads the pattern against where 3.4.1p10 reads a friend definition where it was written. `Scope::hidden_index` keys a hidden chain by the declaration it was made with: 15.64 s to 0.79 s at n = 3200. | 249 / 326 |
| **D, D audit** 8.3.5's function type as a template argument, and the fact 8.3.5p7 made part of its identity | `SpelledTypeId` had learned neither 14.5.3p4 nor 8.3.5p7's trailing qualifiers nor 8.3.5p5's adjustment; four of 14.5.5's own rules came with it, each of which had left one list matching two patterns and neither more specialized. Then neither reader that turns such a type back into a name had ever written a ref-qualifier, so `holder<int(char) const>` and `holder<int(char) const &>` were one symbol; and 8.3.3p1's `nested-name-specifier *` was a ptr-operator `SpelledTypeId` had never had. | 267 / 332 |
| **O, O audit** 14.6.1p1's current specialization of a partial specialization, and which of a template's bodies a declarator-id names | This build had one current instantiation per template - the primary's - so an out-of-class definition of a member of a *pattern* bound its own head to the primary's places. `TemplateInfo::Partial` now gets what `info.current` already was, keyed in `TemplateInfo::patterns` by the interned list. 14.5.2p1's member class template: 14.6p8's reading recorded no template at all. Then `read_declaration` - 14.5.2p3's own tier - recorded against `kNoPartial` outright, so a member of any partial specialization of a member class template was read against the primary's places. | 285 / 336 |
| **X, X audit** 14.7.3p1's explicit specialization of a member, and the question 5.19p2 asks with the same words | A `template<>` definition of one member of one class specialization *is* that member's definition, so 14.7.1p1's reading of the pattern shall not write a second one: `SemaEntity::instantiated_definition` is written for a function too, under an `instantiating_pattern_` depth, and `Pending::from_pattern` lets the end of the unit drop the queued body. Then `note_object` answered two questions with one field - "a use reads the object" and 5.19p2's own answer about the declaration - so a `template<>` written for one list made every *other* list's member no constant expression at all. | 295 / 338 |
| **R, R audit** 14.1p2's names a definition written outside its class wrote, and the region its head bound | An out-of-class member definition stands under one head per class it is nested in, and 14.1p2 lets each spell the enclosing classes' places with names of its own - which this build bound nowhere 14.7.1p1 could reach. `TemplateInfo::reading_region` is the region the head above opened, taken before `StandingIn` moves the nest, and `Member::carried` is it one tier down. Then R bound those names where the *declaration* is made and 14.7.1p1 leaves the *body* to the use, so `enclosed_by_a_head` asks it once at the one door every queued body passes; and 14.5.3p4's count may not be asked at 14.6p8's reading, where the expansion stands for itself. | 306 / 343 |
| **C, C audit** 5.19 read out of one spelling, at the five operators the reader had no answer for, and the two sentences the last of them is written about | 14.2 writes an argument list inside a name, so `TemplateArgumentReader` is the second implementation of 5.19 exactly as `SpelledTypeId` is of 8.1p1 - and four of the clause's own operators had no exit there. 5.18p1's comma is read inside 5.1.1p6's parentheses alone, because outside them a comma separates one argument from the next; 5.2.9p4's cast to cv void is a *discarded* value, which is what `valued` refuses at every reader that takes an operand's worth and what makes `((void)B, true)` read as `true`; 14.5.3p4's expansion stands in 5.2.2p1's argument list, and whether an operand is a pattern is settled by `operand_end` *before* it is read, because the `...` stands after it and `sum(Ns...)` runs out on `Ns` a word early; and 14.2p4's keyword is written inside a component, so `X::template f<A>::v` is one word the split closes up rather than two. 5.3.3p1's other arm is the fifth: how large the type an *expression* has is, is 13.3's answer over a typed operand, so the parenthesized operand closes up with its operator in the split and the tree the parse kept under that spelling is what answers it - beside 5.3.3p2 and 5.3.6p3's reference, which `measured_type` now owns for the three readers that write the operator. Two sweeps came with it: 14.7.1p1's demand is made outside the probe that settles 5.4p2's ambiguity, so `A<sizeof(box<4>)>` lays `box<4>` out; and 14.5.2p3's `nested_owner` is asked *before* the class tier, which reads the same nested-name-specifier as a prefix it must resolve - so `adaptor<T>::range<M>::iterator` is a class nested in a member class template rather than `M is written as a template argument and names no constant`. Then that last operator's own clauses were answered out of a table: `TypeTable::object_align` gives an incomplete class an alignment of zero and a dependent one a number too, and two of the three readings that write `alignof` called it bare - so `S<alignof(wrap<int>)>` was **1 where both oracles give 8**, at a template argument, in an array bound and in a static_assert alike, and `alignof(never)` was a program both oracles refuse and this build ran. `SemaAnalyzer::align_of` is `size_of`'s twin and all three ask it. Under it, 14.7.1p1's demand reads a mark `instantiate_class` writes only where the naming was a use, and neither 14.6p8's reading nor `trait_value`'s own probe leaves one - so `sizeof(box<4>)` inside any class template's body was `sizeof names an incomplete type`; the demand is now `require_settled_type`, asked of the type rather than of the mark, once inside `size_of` and `align_of`. And the demand the checkpoint did make, it made by reading the operand's type-id a *second* time, which is one reading per level doubled at every level below it: a `sizeof` nested 24 deep in its own operand was killed at 60 s where the reference is 0.60 s flat and this build is now 0.01 s at depth 128. | **318 / 348** |
| **B, B module split, B audit** 11.2's access at a path the arguments built, the five refusals no reading made, and what the walk asks at each link | 11.2p4's answer is written about a member *as a member of the naming class*, and this build read the member's own access-specifier alone - so `derived::type` reached a public member of a **private** base from anywhere, at a prefix component as much as at the last one. `Access::base_path` is the one walk down to the declaring class asking each base-specifier on the way, which `Derivation::link_accessible` now asks too; and `resolve_prefix` asks 11.2 of every component rather than of the last, with 14.2's template-id component asked of the *template* the lookup found, because the specialization its arguments make is no declaration an access-specifier was written over. Then five clauses no door enforced: 14.5.6.1p5's equivalent template-parameter-lists, which `record_template` compared by *arity* alone - so `template<class> class F` and `template<int> class F` declared one template; 14.7.3p5's `template<>` over a member of an explicitly specialized class, whose body is unrelated to the pattern's and has no member of a template for a head to specialize; 5.1.1p13's id-expression naming a non-static data member, which `entity_constant` folded out of the member's own default initializer where no object was written; 14.5.4p1's friend declaration whose declarator-id is a *template-id*, which `declare_function` declared a namespace function literally named `operator+<>` and granted to *that*; and 11.3p2's `friend typename C::self;`, refused where 14.6p8's reading cannot see the class an argument list has yet to name and 14.7.1p1 reads the same declaration again where it can. 11.3p3 came with the last of those - a friend declaration naming no class is *ignored* - and 3.4.3p1 with the access ones: `this->Matcher::match(…)` names a class through whatever name reached it, a place an argument list bound as much as a typedef-name. The walk itself cost d² before it cost d: asking `derives_from` at every level is one reachability question per level over the levels below it, which was 3x the turn-start build at depth 128, where one visit per class is equal to it at depth 512. And 11 came out of `sema_class.cpp` whole: `sema_access.h/.cpp` is 11.2's reach and 11.3's grant as one reader, which is what freed both files' room. Then what the walk asks *at* each link was the whole derivation read again there: 11.2p1's second sentence and 11.2p5's befriending class between are questions about the point the name was written at, which does not move as the walk descends - so a protected chain was 0.97 s at depth 512 and a befriending class between 1.30 s, both slower than the reference, where one walk of what the point derives from is 0.07 s and 0.04 s, and `Derivation::path` opened a reader per link of its own descent for the same 0.55 s. Beside them, three clauses landed at one of the exits each is written at: 14.5.6.1p5's value places were compared behind `b.type != kNoType`, which a head nothing has bound never satisfies, so `template<int N> struct A; template<char N> struct A {};` was accepted - the comparison is one of the readings 14.6.1p1's region exists for and now opens it; 14.5.4p1's grant was written at the unqualified *declaration* alone, so `friend int n::peek<vault>(vault);` was refused where both oracles accept and `friend void g<int>(int) { }` was accepted with the body it wrote **silently dropped**, where both refuse; and 14.7.3p5 read `resolve_prefix`'s last region and the first declaration under the head, so a member of a class nested in an explicitly specialized one and a member template of it were two programs `g++` refuses and this build translated. | **336 / 357** |
| **U, U audit** 14.6.2.1p6's member of an unknown specialization, the naming 3.2p3 leaves no use of, and the order 14.7.3p1 says nothing about | 14.6.2p3 leaves a base an argument list has still to settle off the chain 3.4.1 searches inside the definition, and this build then let the *qualified* lookup refuse outright - so `typename impl::expr` and `typename impl::data`, a name of the current instantiation whose only declaration is in that base, were `no declaration of … is in scope` where both oracles read them. 14.6.2.1p6 says such a name is a member of a class no argument list has named yet, which is the stand-in a prefix that named no region at all already got: `member_of_unknown_specialization` is that one door, asked at all three walks that look a component up - the name behind the prefix, a middle component of the prefix itself, and the one written after 7.1.6.2p1's decltype-specifier. Then 7.1.6.2p4 asks what an id-expression *names*, and 3.10p1 has no answer for a name that may turn out to be an object, a function, an enumerator or a type - so `decltype(D::pointer)` was refused where every other dependent operand of the specifier already came back through `dependent_expression_type`. Under the third fixture was a rule of its own: the definition that lays out a static data member of a class template specialization is storage no unit wrote, and 3.2p3 puts it in the program only where a use reaches it - but `storage_of` asked for it at the *naming*, before knowing whether the use would read the place or the value 9.4.2p3 folded. `LowValue::storage_owed` carries the unasked demand to whatever takes the address, so `box<int>::k == 4` writes no storage and `&box<int>::k` writes it, which is what `g++` does. That un-hid what the plan had recorded and nothing had reached: 14.7.3p1's `template<> const int code<int>::value = 7;` was still marked `instantiated_definition` by the pattern's own reading, so with the eager demand gone it was deferred and never written. `supersede` is the function tier of that clause and drops a held body; the object tier has no held thing but a line already in the dump, so `object_definitions_` keys that line by the declaration it defines and 14.7.3p1 takes its claim to define anything away.  Then that clause was read in one source order only: `PatternReading::record` reads a member definition again for every specialization already made, so a `template<>` written *above* the template's own out-of-class definition reached none of the three doors - an ordinary member function, a constructor, a destructor, a conversion function, a member function template and a member of a nested class were six programs both oracles accept and this build called `is defined twice`, and a static data member was the silent one, running the pattern's value where the program had written its own.  `holds_written_definition` is the clause the other way round, asked at the three doors that already ask it the first way, and leaving the reading unmade rather than refusing it; and `note_object`'s withdrawal was narrowed to the line 14.7.1p1's reading itself wrote. | **342 / 361** |
| **N, N audit** the four calls 13.3 had to resolve before it had a set, and the head 14.5.6.1p5 compares two heads by | Four fixtures, four clauses, none of them 13.3's own ranking. 8.1p1 leaves no declarator-id inside a type-id's parentheses and this build read one, so `(dispatch<T>(ex))(f, w)` was `( type-id ) cast-expression` with `(ex)` as a *named* nested declarator - where `Recognizer` already required a ptr-abstract-declarator there and the two parsers had answered one rule two ways. 3.4.2p3's set is the ordinary lookup's *and* the argument-dependent one's, and `named_value` wrote the line for a name it found one declaration of, so 8.4.3p2 refused a deleted `adl_only::make_error_condition` before 3.4.2 had added the one the argument's own namespace declares - the naming is deferred to 13.3 now, and 14.7.1p1's demand it also carried is made on its own, because a specialization an explicit argument list made has 14.5.3p4's expansion still to come to as many parameters as the run is long. 13.3.2p2 counts a declaration whose whole parameter-declaration-clause is `...` viable for one argument, which `converting_constructor` did not - and once it does, 13.3.3.1p4 is what stops the class's copy constructor from being a second way in, a regress with no bottom that `standard_only_` held over the resolution closes. And 5.2.2p1's call on an object of class type asked its class for `operator()` and 13.3.1.1.2p2's surrogates without asking this unit for the class: a declaration of a *reference* to a specialization asks for nothing, so the call is the first use that does. Then the audit: 7.3.3p14's hiding is written about two heads' lists and this build compared the types, so `const K&` under one head and `const K&` under another were two lists - `hiding_signature` is 14.5.6.1p5's own form, held under the declaration's id - and the clause has two source orders, of which only one was read: `equivalent_template` read a brought-in declaration as one 14.5.6.1p5 makes the class's own a redeclaration of, so a using-declaration written *above* the declaration that hides it was `also is defined twice`. Under both was the same hole: a place neither declarator mentioned stands in no type at all, so `template<int N> f(int)` and `template<class T> f(int)` had one signature - the stand-ins now stand at the end of the form itself, and 14.5.6.1p5 came out of `sema_template.cpp` whole as the file's own module. | **348 / 363** |
