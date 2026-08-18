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
- `sema_template_signature.h/.cpp` — 14.5.6.1p5 alone: `TemplateSignature` and
  the one canonical form per declaration that *four* tiers compare - a
  redeclaration of a function template, a definition written out for one,
  7.3.3p14's hiding of a base's member template, and 13.1's own index of what a
  region declares of one name, which every declaration passes and which the list
  a declarator wrote answers neither of 13.1's questions from. The places stand
  for their positions, the type is substituted onto them, and the stand-ins
  themselves stand at the end of it, because a place neither declarator
  mentioned stands in no type at all.
- `sema_overload.cpp` with `sema_argument_lookup.cpp` — 13.3 whole: one candidate
  set per call, and 3.4.2p3's answer about which lookups filled it. 13.4p1's own
  set is here too: `folded_name` is 14.2 at a name a reading with no overload set
  to hand on looks up - 5.19's three readings and 7.1.6.2p4's - asking the same
  two doors `id_expression` asks in the same order and ending on
  `named_function`, which is what makes the naming 3.2p2's use where the operand
  is evaluated and nothing where 5p8 leaves it unevaluated. `names_specialization`
  is how large that set is: 14.8.1p2 leaves a declaration the written list did not
  complete to a deduction that no such reading makes, so it is no member of one. A callee the
  argument-dependent search still has declarations to add to denotes none of them
  yet, so 8.4.3p2's refusal and the line the callee is written on both wait for
  the choice; the search itself is made with the name a template-id names, and
  14.8.1p2's written list is read against the declarations it reaches exactly as
  it is against the ordinary lookup's. 13.3.3.1p4's flag is held over the
  temporary a user-defined conversion builds, which is what leaves one such
  conversion in a sequence - and it is held by a guard, because every reading
  that holds it refuses by throwing and a refusal a probe catches would leave it
  standing over everything the unit reads after.
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
  8.3.4p1's bound is a pair of its own here (`match_bound`), and 14.8.2.5p17 is
  what bounds it: an argument deduced from a bound may be of any *integral* type
  and of no other, so the pair is refused at a place 3.9.1p7 does not name one
  of rather than converted onto it.
- `sema_type_id.cpp` — `SpelledTypeId`, the second implementation of 8.1p1's
  type-id and 8.3.5p1's parameter clause. 14.2 writes a template-argument-list
  inside a name, so an argument reaches the semantic layer as text; every rule
  the tree reading knows has to be written here too.
- `sema_value_expression.cpp` — `TemplateArgumentReader`, the same second
  implementation for the *other* kind of argument: 5.19's constant expression
  read out of one spelling. It owns the split into terminals - a name closes up
  with its argument lists, its `::`, 14.2p4's keyword and 5.3.3p1/5.3.6p1's
  parenthesized operand - and 5.19's own precedence walk, 5.18p1's comma inside
  5.1.1p6's parentheses, 5.2.9p4's discarded value, 14.5.3p4's expansion inside
  5.2.2p1's argument list, and 5.2.1p1's subscript, whose three left operands are
  `ConstexprReading::element_at` - the one reading the tree's own walk asks too.
  What no reading of words can answer it asks of
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
  (`collect_packs`, through a template-id whose template is a place and through
  8.3.4p1's bound, which is as much of what an array is written over as its
  element type), what an
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

Z stands at **372 / 375** — 306 of the 309 `tests/` fixtures plus all 66
`course/pa22` ones, two of which Z added. The 3 that fail group by the compiler
behaviour that owns them, from the diagnostic each one reaches:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 2 | the reference drops an initializer this build writes | none - **the reference's** | none. `T x = a + b;` over an *empty* class and an **operator-syntax** call is the whole of it: the reference emits no call at all, where `T x = operator+(a, b)`, `T x = a.m()` and the same program over a class with one member are three shapes it does write. Probed at 12 spellings; recorded below rather than reproduced |
| 1 | one-off | mixed | `an expression is outside the PA12 subset`, from two captureless closures a non-primary body collapsed to one anchor |

9.5p1's anonymous aggregate is now the object no name reaches at every reading
that walks a class's subobjects, rather than a member of class type like any
other. `SemaEntity::anonymous_storage` is the one fact, written where
`inject_anonymous_members` declares the object and read by
`collect_member_targets` beside `declares_subobject` - which flattens a class's
subobjects, descending through such an object and carrying `one_of`, the union
whose single storage each member stands in. 12.6.2p8's construction, 12.4p8's
destruction and 12.6.2p2's mem-initializer-id all walk that one list, so a
variant member is named where the program named it and the unnamed class is
named nowhere. Beside them `trivial_default_construction` reads 9.5p1's storage
the way `vacuous_destruction` already read it - a union's constructor
initializes no variant member, so it does nothing whatever its members ask for -
`through_anonymous_storage` leaves the *operand* below the step it takes so
10.2's base conversion has a line of its own, `ConstexprReading` walks 9.5p1's
object with the same index the layout gave it, and `name_regions` writes the
name the translation gave an unnamed class where a `<source-name>` stands.

8.3.4p1's bound is now a bound and a *place*. `types_.bound` still counts the
elements every reader of a type already asks it for, and `types_.bound_place`
beside it is the entry the constant-expression came to where an argument list
has yet to settle one - `kNoType` for every settled bound, so a settled array is
interned by its number exactly as it was before there was a place to write, and
two patterns whose bounds name two places are two types where the 1 both stand
in with is one. The clause has two readers and both write it: 14.2's spelled
type-id asks `template_argument_value` for the bound it did not read as digits,
and the declarator's own reading asks `named_place` *before* `counted_where` may
throw - which is what a function template's parameter-declaration-clause needs,
being read where nothing else is standing in. What comes back is one fact three
readings then use: `TypeTable::substituted_array` puts the argument's number
back wherever a substitution reaches the bound, `Deduction::match_bound` is
14.8.2.5p13's pair of its own - the place deduced to the number the argument's
declarator arrived at, converted by 14.3.2p5 to the type the place declared -
and 14.5.5.2's ordering reads a place against a place there exactly as it reads
a type place against a type place, which is what leaves `const T[N]` the more
specialized of it and `T[M]`.

14.7.2p10 is now asked where a use asks for the definition and nowhere else.
`instantiation_is_suppressed` is one fact on whatever p9 named - a specialization
at the declarator form, the class at the class form - plus `out_of_line_definition`,
9.3p2's own question about the definition, and a walk up the classes a member
stands in; the three readers `has_written_definition`, `require_definition` and
`demand_definition_by_id` ask it and nothing walks the members to write it. That
is what makes the clause order-free: a member's out-of-class definition written
below the declaration is left to the other unit exactly as one written above it
is, an `inline` written on such a definition does not keep it here where 9.3p2's
body in the class does, and p11's explicit instantiation *definition* takes the
suppression back on either side of the declaration. Beside it 12.2p1's array
prvalue is an object at all three readings that find one - the discarding, the
expression, and `initialize`, where the list stands beside a destination 4.2's
pointer is taken from - and 8.5.3p5's name for its storage is the caller's rather
than the list's own `spelling`, which is 2.14.5p1's code units.

15.4p1 is now read as a fact of one call and not of the step it stands in.
`note_call` records that a call was made and settles a region 12.2p3's temporary
already asked for however the callee is specified, and only the fresh region
*this* call would need is the clause's; `pending_calls_` counts every call an
operand's temporary still stands under. Where no object stands yet the handler
would end no lifetime, so `throwing_since_mark_` and `pending_throwing_calls_`
keep 15.4p1's answer at that one place. Beside them 9.3.1p3 with 11.2p5 reaches
the object of a call written with no object expression through the class the
nested-name-specifier named, `built_in_place_trivially` tells 8.5.1p2's copy from
an object standing elsewhere apart from a subobject built in place by a trivial
constructor, and 6.6.3p2's chain - `PendingDefinition::returned_object_chain`,
stamped where an entry joins the list rather than where it is read - is which
instantiated definitions the object file owes both of the ABI's entry points for.

6.6.3p2's chain now has a *beginning* as well as a length. A body 14.7.1p1 made
when the class was instantiated and `queue_definition` put aside is the class's
own: the grant that later joins it to the list is a use asking for a definition
that already existed and not the reading that asked for the object, so
`PendingDefinition::held_by_class` bounds the start and leaves the length alone -
a body queued while a chain already stands carries the chain it was queued under.
9.3p2 draws the same line here it draws at `writes_base_entry`, so an out-of-class
definition of the same member starts a chain where the in-class spelling does not.

Known gaps probed and deliberately left:

- 9.5p1's zero-offset step in a *constructor*: `member_storage` folds the object
  an anonymous aggregate declared into the one `index` the member it holds
  stands at, which `pa22/cppgm++-ref` also does at every member access and does
  *not* do in a ctor-initializer - so a mem-initializer designating a variant
  member is one `index` here and two there, at all 12 shapes swept. The fold is
  the documented reading and the offsets add up the same way, so nothing runs
  differently; it means no `course/pa22` fixture may pin such a constructor,
  which is why Z's two fixtures pin the *uninitialized* member and the access
  through a base instead.
- 9.5p2's implicit deletion: a union whose variant member has a non-trivial
  default constructor, copy constructor or destructor has that member of its own
  deleted, so `struct N { union { V v; }; };` with a `V()` of V's own is
  ill-formed. `g++` refuses it; `pa22/cppgm++-ref` and this build both take it
  and write a constructor that does nothing, which is what 12.6.2p8 leaves it
  when a program does write one. No fixture pins either answer.
- 12.4p8 through 9.5p1's object: `pa22/cppgm++-ref` writes the unnamed union's
  own destructor - an empty function under `_ZN1N18__anonymous_union1D1Ev` - and
  calls it from the class's, where this build's `vacuous_destruction` already
  answers that destroying it comes to nothing and writes neither. Both run the
  same; the reference's shape needs a class 9.5p1 leaves unnamed to own an
  object-file name, which `name_regions` now writes but nothing asks for.
- 9.3.1p3's *data-member* sibling of the naming class W landed at the call:
  `pa22/cppgm++-ref` folds the step to the naming class into the field
  projection where the member is declared *in* that class and writes the step
  where it is declared in a base of it, so `d.A::v` is one `index
  [projection=field]` at the offset within the complete object there and a base
  step plus a field here, while `d.M::v` is a base step plus a field in both.
  Both source orders, `this->`, a pointer, a class template and a use with no
  object expression are the same; it is the pre-W build's shape as much as this
  one's, and no `course/pa22` fixture pins it.
- 15.4p8's dynamic exception-specification: `void f() throw()` makes
  `pa22/cppgm++-ref` guard the whole function with an `eh_try` over an
  `eh_call_unexpected`/`eh_personality` pair, which this build writes nowhere -
  so every probe written with `throw()` diverges whatever 15.4p1 then answers.
  `noexcept` is the spelling the fixtures use and is read alike in both.
- 15.2p2 at an *array* element: the reference opens the region before the
  element's address is computed and this build opens it before the element's
  call, and the handler the construction replays recomputes 4.2's decay per
  element there where this build takes it once. Both are the pre-W build's
  shapes too, and `note_call` never sees those three `IK_CALL` sites - the
  element loop's, the destruction loop's and `destruction_step`'s.
- `&D::B::f`, 5.3.1p3's pointer-to-member whose qualified-id names a base of the
  class the pointer is to, is `an expression has no conversion to the type it
  initialises` here where both oracles read it.
- Three programs `g++` reads differently from *both* this build and
  `pa22/cppgm++-ref`: 8.5.1p1's aggregate with a base class (`struct H : E { int
  q; }; H h = {s, 3};`, which C++11 makes ill-formed and both readings accept),
  an array of aggregates holding an empty member (`H h[2] = {{s,1},{s,2}}` runs
  to a different value out of the reference's own LowIR as it does out of this
  one), and a four-parameter member returning an object, which **segfaults** out
  of both frontends' LowIR through `lowir2cy86` + `cy86`.
- 6.6.3p2's chain where `pa22/cppgm++-ref` starts one this build does not: a
  member of a class template written in its class body, called on a *temporary*
  of that class with no other mention of the specialization above it
  (`maker<int>().mk(3)`), a static member of one where no object of the class is
  declared, and a `typedef` of the specialization written above the call. The
  reference's own line moves with a `maker<int>* p = 0;` or a
  `sizeof(maker<int>)` written earlier, which is no clause of the standard; the
  fixture pins the three tiers all readings agree on.

- 12.1 and the ABI at a *throwing* twin of the shape W landed: `pa22/cppgm++-ref`
  opens the region around a temporary's own constructor *before* the naming of
  its storage where the region is `begin_object_lifetime`'s and *after* it where
  the constructor's own call opened one, which is the split this checkpoint
  wrote - but `R().execute()` with every member throwing is still the other way
  round there. 47 of the checkpoint's 50 probes match the reference through the
  real comparator; that one, `H h(5, C())` with a throwing temporary the
  reference leaves inside one region where this build opens a second at the end
  of the full-expression, and 6.8's `derived_t<T>(x);` - a *declaration* both
  `g++` and this build refuse for want of a default constructor and the
  reference reads as an expression - are the three.
- 14.7.2p10 at 12's special members and at 10.3's table: `pa22/cppgm++-ref`
  **defines** a constructor written outside its class under an `extern
  template`, and defines a polymorphic class's vtable and its `_ZTI`/`_ZTS`
  where it declares the virtual functions the table names. `g++` leaves the
  constructor and the vtable alike undefined and writes no RTTI, which is what
  this build does. The same clause one form along: `extern template struct
  box<int>;` with `template int box<int>::f();` below it defines `f` here and in
  `g++` and is a declaration in the reference, which takes p11's take-back at
  the class form and not at the member one. So no `course/pa22` fixture pins any
  of the three - the `.ref` would carry the reference's answer.
- 14.7.2p10 at a member defined *in* its class body is the other way round:
  `pa22/cppgm++-ref` and this build both write the definition and `g++` leaves
  it undefined, which is p10's note read strictly - the exception keeps a body a
  call may be folded into and no out-of-line copy. The fixture pins the
  reference's line, and 7.1.2p1's `inline` written on an out-of-class definition
  is on the other side of it in all three.
- 5.2.3p3's array prvalue at three readings `pa22/cppgm++-ref` refuses and `g++`
  and this build run: a subscripted matrix prvalue (`M23{{1,2,3},{4,5,6}}[1][2]`
  is `unsupported expression in PA14 LowIR lowering kind=br` there), an array of
  class type, and one written in a conditional. 8.5.2p1's string literal
  standing as the whole of such a list - `C4{"abc"}` - is `invalid array
  braced-init-list` there at both the discarding and the decaying reading. So
  the `course/pa22` fixture pins the shapes all three agree on and stops short
  of these five.
- `pa22/cppgm++-ref` **drops the whole initializer** of `T x = a + b;` where `T`
  is an empty class and the `+` is an overloaded operator written in operator
  syntax: `E f = 1 + e;` and `E2 i = 1 + h;` emit no call, where `E a =
  operator+(1, e);`, `E c = e.m();`, `E g = mk();`, `(void)(1 + e);` and the same
  program over a class with one data member all emit one there. `g++` and this
  build run every shape of it. It is what
  `general/300-dependent-hidden-friend-static-member-definition` and
  `general/300-friend-existing-template-private-ctor-access` both fail on - both
  write `Iter<int,long> next = 1 + it;` over an empty class - and reproducing it
  would be a rule about the size of a class and the *syntax* of a call, which is
  no clause of the standard.
- 12.2p3 puts the end of a temporary's lifetime at the end of the
  full-expression, and `pa22/cppgm++-ref` ends a comma's discarded operand where
  the operand ends: `int c = (D(), 3);` is construct, destruct, `store i32 3` -
  where this build and `g++` store first. No fixture writes a destructor under a
  comma, so the `course/pa22` fixture for 5.18p1 pins the classes that have none.
- 8.5.1's member constructor - the one this build gives an aggregate so that an
  *element* of an array of it can be initialized - is written with no
  `unwind=no`, where `pa22/cppgm++-ref` writes one for `C{int a;}`. 15.4p14 makes
  it nonthrowing where every member's own initialization is, which is
  `default_construction_nonthrowing`'s question at a different list; `unwind` is
  one of the seven keys the comparison strips, so no fixture reaches it and the
  object tier a later PA builds is where it would show.
- `const int (&r)[3] = A3{1, 2, 4};` - 8.5.3p5 binding a reference to an array
  prvalue - is refused by this build (`an expression has no conversion to the
  type it initialises`) and by `pa22/cppgm++-ref` (`unsupported lvalue address in
  PA14 LowIR lowering`), where `g++` runs it. And `M23{1, 2, 4, 8, 16, 32}`,
  8.5.1p11's elided braces at the *top* of a functional cast, is `invalid array
  braced-init-list` there and read here and by `g++`. So the `course/pa22`
  fixture for 5.2.3p3 pins the seven shapes all three agree on.

- 13.4p1 needs a target type to choose among several specializations one written
  list makes, and a reading with no call and no target has none to defer to - so
  `folded_name` refuses a spelling two declarations of the name are *completed*
  by. A head the list only partly fills is no member of that set (14.8.1p2 leaves
  it to a deduction such a reading never makes) and 14.5.3p1's trailing pack the
  list reached is, so `f<int>` beside `template<class T, class U> f()` names one
  and beside `template<class T, class... U> f()` names neither - which is what
  `g++` answers at both. Where a target type *is* written the expression layer's
  own 13.4 answers first and this door is never asked. The reference refuses the
  family outright - `constexpr fp p = &f<int>;` over two templates of one name is
  `unsupported constexpr variable initializer` there - where `g++` and this build
  both read it.
- `sizeof(&f<T>)` and `&f<T>` written with no target at all are `address of
  overloaded function with no contextual type information` in `g++`, which reads
  a template-id naming one specialization as an unresolved set there, and are
  folded here and by `pa22/cppgm++-ref` alike.
- 5.19p2 at a `constexpr` static data member of *pointer* type that 9.4.2p2's
  definition was written out for: `g++` and `pa22/cppgm++-ref` both fold the
  naming and this build loads it out of the storage the definition laid down.
  `named_value`'s `declared_constant` requires `!object_definition`, which is
  the line the two fixtures the plan already records draw for the *integral*
  member - so this is the pointer arm of the same clause and no part of what a
  spelling's reading owns. The non-template spelling of it diverges too, so it
  is not the template path's.
- 5.19p2 over a pointer *into* an array read as a template argument -
  `constexpr const size_t* into = sizes + 1;` and then `box<into[1]>` - and the
  same through a `constexpr const char*` at a string literal, are two programs
  `g++` and this build read and `pa22/cppgm++-ref` calls `failed non-type
  template argument evaluation`. So the `course/pa22` fixture for 5.2.1p1 pins
  the array, the matrix, the literal itself, the class `operator[]` and the two
  member paths, and stops short of the pointer the reference cannot fold.
- 5.2.1p1's other operand order - `2[a]`, which is `*(2 + a)` - is folded here at
  all four left operands, 2.14.5p8's string literal among them, and refused by
  `pa22/cppgm++-ref` at both readings, so no fixture pins it; `g++` runs every
  shape of it.

- 15.4p1's exception-specification is folded rather than required to be a
  constant expression: `void f() noexcept(chk(1));` over a `chk` that is not
  `constexpr` is refused by `g++ -pedantic-errors` and read here as
  `noexcept(false)`. It is 5.19's own reader at a declarator and no part of what
  13.3 owns.
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
- 8.3.4p1's bound written as an *expression* over a place - `T[N - 1]`,
  `T[sizeof(U)]` - is refused by the spelled reader rather than stood in for:
  such a bound names no place a substitution could put a number back into, and
  standing one element in would leave the pattern silently read as `T[1]`.
  `split_type_id` is a second wall on the same shape and one tier above it:
  every character that is not a name char, `*`, `&`, `(`, `)`, `[`, `]`, `,` or
  `...` refuses the whole spelling, so a *settled* arithmetic bound written
  inside a name - `P<int[2 + 3]>` - is `a template argument is written outside
  the PA12 subset` here where both oracles read it, while `P<int[sizeof(int)]>`
  and `P<int[MAX]>`, which split into words, reach the value reading and are
  read. Widening the split is a change to every type-id spelling and not to what
  a bound owns.
  14.8.2.5p5 makes it a non-deduced context, so what it needs is not a deduction
  but an entry a substitution *re-reads*, which `dependent_value`'s spelling-keyed
  stand-in is not. `pa23/tests/general/400-array-bound-expression-is-nondeduced`
  is where the clause is next pinned; both oracles read it.
- 8.3.4p1's bound of **zero** after substitution: `template<unsigned long N>
  struct z { int a[N]; };` named `z<0>` is `an array bound is zero` here and in
  `g++ -pedantic-errors` and is accepted by `pa22/cppgm++-ref`, which lays out an
  empty array. The refusal is the instantiation's own re-reading of the member
  declarator with the argument bound, so it is the clause read strictly and no
  part of what a place carries; no fixture pins it, since the `.ref` would carry
  the reference's answer.
- 14.5.3p4's pack written as an array *bound*: `collect_packs` walked the
  element type alone until the Y audit, so `P<list<int[Ns]...> >` named no pack,
  14.8.2.5p9 deduced nothing and the partial specialization was silently left
  unselected - a compiled program that ran to the primary's value. It is fixed
  and `g++` agrees, but `pa22/cppgm++-ref` writes the *old* answer:
  `P<list<int[2],int[3],int[4]> >::w` is 9 in `g++` and here and 0 there, at the
  pack alone, the pack beside a type place, and a run of 100. So no fixture pins
  it - a `.ref` would carry the reference's 0 - and the shape is recorded here
  instead.
- 14.8.2.5p17 at an *enumeration* place: an argument deduced from an array bound
  may be of any integral type and 3.9.1p7 does not name an enumeration one, so
  `template<class T, E N> struct P<T[N]>` matches no array. `g++` leaves
  `P<int[3]>` to the primary; this build did too only after the Y audit, and
  `pa22/cppgm++-ref` still selects the specialization. The same enumeration
  written as an *argument* - `H<int, e3>` over `T c[N]` - is read alike in all
  three, so it is the deduction that the clause narrows and no fixture pins the
  refusal.
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
  about: `TemplateArgumentReader` has no address arm. Both oracles accept. A
  place of pointer or reference to *function* type is refused one tier earlier
  and by name - `a non-type template parameter of pointer to function … is
  outside the PA20 subset` - so `z<f<int> >`, the decayed naming 14.2's own door
  now reaches, and `f<int> != 0` folded beside it both stop there.
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

**Z made 9.5p1's anonymous aggregate the object no name reaches, and the Z audit
carried the widened fact to the constexpr reader, to 10.2's base step and to the
ABI's unnamed component — see the ledger.** What the PA still holds is the 1
one-off and the 2 the reference itself drops, so the next one is **AA**: `an
expression is outside the PA12 subset`, which
`general/300-nonprimary-chained-member-template-lambda-anchor-collision`
reaches - two captureless closures parsed from a *non-primary* function body
that a collapsed body-token anchor gives one identity to, where each written
lambda-expression is a class of its own.

- Owner. The reading that makes a closure class - `sema_lambda.cpp` for
  5.1.2p3's class per lambda-expression and for the anchor a body's tokens are
  keyed by, and `sema_pattern.h`'s non-primary reading for why two of them
  collapse to one key. `ast_names.h` holds what the parse knows about a body it
  has already read once.
- Data flow. One closure identity per *written* lambda-expression rather than
  per token anchor: the identity is settled where the expression is read and
  carried on the typed lambda, so a second reading of the same body under a
  different specialization asks for its own and neither steals the other's.
- Expected complexity. One entry per lambda-expression read, keyed by the
  reading rather than by the token span - no scan of the bodies already read and
  no second parse of either operand.
- Validation. The fixture, plus a sweep of closure shapes through the real
  comparator under a scratch `pa22/tests` directory - two captureless lambdas in
  one statement, in one argument list, in a member template of a non-primary
  class, and the same body read under two specializations - judged against `g++
  -std=c++11 -pedantic-errors -x c++` and against `pa22/cppgm++-ref`, run
  through `lowir2cy86` + `cy86`; a valgrind sweep; and the two dimensions it
  touches - n lambdas in one body, and n specializations reading one body.

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
way says the opposite of what happened, the *first* pass over a corpus
measures the page cache and not the compiler: 2.62 s cold against 1.53 s warm,
and `CPPGM_APP_ARGS` is read by the *perl* harness and not by the binary - a
hand-run with the variable exported and no flags on the command line is
`ERROR: not yet implemented` and exit **86**, which times as a refusal.
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
| **N audit** n declarations of one function template name, each defined | 100 → 800 | 0.00 → 0.06 s, 8 → 21 MB | 0.75 s at 100, **over 60 s at 400**; the turn-start build is 0.07 s |
| **N audit** n member templates of one class, each redeclared out of class | 100 → 800 | 0.01 → 0.06 s, 8 → 23 MB | 0.55 → 0.82 s; the turn-start build is the same 0.06 s |
| **N audit** n calls of a template-id callee reached through 3.4.2 | 200 → 3200 | 0.01 → 0.11 s, 7 → 33 MB | 0.57 → 1.28 s; the turn-start build **refuses** |
| **N audit** n constructor templates of one class | 100 → 800 | 0.00 → 0.03 s, 7 → 16 MB | 0.54 → 0.65 s; the turn-start build is the same 0.03 s |
| **S** n namings of one function template specialization in a constant expression | 100 → 800 | 0.02 s, 7 → 10 MB | 0.65 s at 800; the turn-start build **refuses** |
| **S** n *distinct* specializations named that way | 100 → 800 | 0.00 → 0.04 s, 7 → 15 MB | 0.90 s at 800; the turn-start build **refuses** |
| **S** n subscripts in one template-argument spelling | 100 → 800 | 0.00 s, 6 → 7 MB | 0.55 s at 800; the turn-start build **refuses** |
| **S** a subscript nested d deep in its own index | depth 8 → 128 | 0.00 s, 6 MB | 0.53 s flat; the turn-start build **refuses** |
| **S** n plain names read in constant expressions, the door the probe was added to | 100 → 800 | 0.02 s, 7 → 10 MB | 0.59 s at 800; the turn-start build is the same 0.02 s and 10 MB |
| **S audit** n plain names read in constant expressions, the door widened | 100 → 800 | 0.01 → 0.03 s | 0.69 s at 800; the turn-start build is the same 0.03 s |
| **S audit** n `decltype`s of one function template specialization | 100 → 800 | 0.01 → 0.03 s | 0.56 → 0.78 s; the turn-start build **refuses** |
| **S audit** n `decltype`s of n *distinct* specializations | 100 → 800 | 0.01 → 0.05 s | 0.58 → 1.09 s; the turn-start build **refuses** |
| **S audit** n namings beside one head the written list does not complete | 100 → 800 | 0.01 → 0.03 s | 0.57 → 0.83 s; the turn-start build **refuses** |
| **S audit** one naming beside n heads of growing arity the list does not complete | 25 → 100 | 0.01 → 0.03 s | 0.54 → 0.62 s; the turn-start build **refuses** |
| **L** n `extern template` class declarations, each over a specialization of 3 members, each used | 100 → 800 | 0.02 → 0.23 s, 13 → 56 MB | 1.06 s at 400, 1.65 s at 800 |
| **L** one comma expression of n class-typed operands, every one of them discarded | 200 → 1600 | 0.00 → 0.03 s, 8 → 15 MB | 0.87 s at 1600 |
| **L** n discarded `A3{…}` array prvalues, one slot and 3 elements each | 200 → 1600 | 0.02 → 0.14 s, 9 → 25 MB | 0.95 s at 1600 |
| **L** one discarded array prvalue of n elements | 200 → 1600 | 0.00 → 0.04 s, 8 → 13 MB | 0.65 s at 1600 |
| **S audit** n string-literal subscripts in one argument spelling | 100 → 800 | 0.01 s flat | 0.56 s at 800; the turn-start build is the same 0.01 s |
| **S audit** n *reversed* string-literal subscripts in one spelling | 100 → 800 | 0.00 → 0.01 s | 0.72 s at 800; the turn-start build **refuses** |
| **S audit** a string literal wrapped in d parentheses and subscripted | depth 8 → 128 | 0.00 → 0.01 s | 0.53 s at 8, **killed at 60 s at 32**; the turn-start build is the same 0.01 s |
| **S audit** n numeric literals in one argument spelling, read as an object then a value | 800 → 12800 | 0.01 → 0.04 s | 0.58 s at 3200, **segfaults** at 12800; the turn-start build is the same 0.04 s |
| **L audit** n `extern template` classes of 3 out-of-class members, every member used | 100 → 800 | 0.03 → 0.33 s, 15 → 73 MB | 0.73 → 2.64 s; the turn-start build is 0.04 → 0.35 s |
| **L audit** a class nested d deep in a class template, one `extern template` over it | depth 8 → 64 | 0.00 s, 7 → 8 MB | 0.53 s flat; the turn-start build is the same 0.00 s |
| **L audit** n discarded array prvalues in one comma chain, each under a cast | 200 → 1600 | 0.01 → 0.05 s, 7 → 20 MB | 1.20 s at 1600; the turn-start build is the same 0.05 s |
| **L audit** n array prvalues, each initializing a pointer | 200 → 1600 | 0.01 → 0.11 s, 9 → 33 MB | 0.90 s at 1600; the turn-start build is 0.08 s and **stores a null pointer** |
| **L audit** an array prvalue nested d deep in its own call argument | depth 8 → 128 | 0.00 → 0.08 s, 6 → 8 MB | **killed at 60 s** at depth 32; the turn-start build is the same 0.08 s |
| **L audit** commas nested d deep, each discarding an array prvalue | depth 8 → 128 | 0.00 s, 6 → 7 MB | **killed at 60 s** at depth 128; the turn-start build is the same 0.00 s |
| **W** a qualified implicit-object call at a base chain of depth d, 200 calls | depth 64 → 512 | 0.01 → 0.05 s, 8 → 16 MB | 0.61 → 2.91 s; the turn-start build is the same 0.05 s |
| **W** n classes, each with one qualified implicit-object call through its base | 100 → 800 | 0.02 → 0.19 s, 11 → 51 MB | 0.62 → 1.45 s; the turn-start build is 0.18 s |
| **W** n full-expressions, each a temporary with a destructor under a noexcept call | 200 → 1600 | 0.01 → 0.08 s, 9 → 34 MB | 0.57 → 0.85 s; the turn-start build is 0.05 s and **writes no region** |
| **W** a chain of d class-returning instantiations over one base subobject | depth 25 → 200 | 0.00 → 0.02 s, 7 → 11 MB | 0.55 → 0.64 s; the turn-start build is the same 0.02 s |
| **W audit** n class templates, each with an in-class member returning a class, each called on a named object | 100 → 800 | 0.02 → 0.21 s, 12 → 53 MB | 0.64 → 1.49 s; the turn-start build is the same 0.21 s |
| **W audit** n members of *one* class template, each returning a class, each called | 100 → 800 | 0.01 → 0.10 s, 9 → 31 MB | 0.58 → 1.03 s; the turn-start build is 0.11 s |
| **W audit** n free function templates returning a class, each called | 100 → 800 | 0.01 → 0.11 s, 9 → 34 MB | 0.61 → 1.19 s; the turn-start build is the same 0.11 s |
| **W audit** a chain of d class-returning instantiations, each calling the next | depth 25 → 200 | 0.00 → 0.02 s, 7 → 11 MB | 0.54 → 0.65 s; the turn-start build is the same 0.02 s |
| **Y** n class templates, each partially specialized over `T[N]`, each named | 50 → 400 | 0.01 → 0.06 s | 0.56 → 0.73 s; the turn-start build **refuses** |
| **Y** a pattern whose array bound is d dimensions of d places | depth 4 → 16 | 0.00 s flat | 0.53 s flat; the turn-start build **refuses** |
| the whole 372-file corpus, one process per file | — | **1.65 s** | 1.66 s at the pre-W build; the loop's own floor is 0.58 s |
| **Y audit** a pattern of d dimensions of d places | depth 4 → 32 | 0.005 s flat | **killed at 600 s** at depth 32 |
| **Y audit** n patterns over `T[N][k]`, each distinct, one named | 400 → 1600 | 0.019 → 0.069 s | 0.79 s at 1600; the turn-start build is the same 0.069 s |
| **Y audit** n array declarators bounded by one named constant | 1600 → 6400 | 0.058 → 0.246 s | 1.31 s at 6400; the turn-start build is the same 0.240 s |
| **Y audit** n spelled `P<int[MAX]>` arguments, each its own instantiation | 800 → 3200 | 0.050 → 0.210 s | 0.80 s at 3200; a *digit* bound is 0.216 s, so the value reading the widened bound needs costs nothing measurable; the turn-start build **refuses** |
| **Y audit** n members of one template, each bounded by a place | 800 → 3200 | 0.015 → 0.050 s | 0.65 s at 3200; the turn-start build is the same 0.051 s |
| **Y audit** n patterns against n instantiations, the cross product | 100 → 400 | 0.016 → 0.082 s | 0.62 → 1.39 s |
| **Y audit** one pattern whose only pack is a bound, against a run of n | 100 → 1600 | 0.005 → 0.014 s | 0.55 → 0.71 s; the turn-start build **answers the primary's value** |
| **Y** the whole 309-file `pa22/tests` corpus, one process per file | — | **1.33 s** at the audited build | 1.38 s at the checkpoint and 1.35 s at the pre-Y build, which is one measurement: `named_place` is a node-kind test at every settled bound and one scope lookup at a bound written as a name |
| **Z** n anonymous unions in one class, each with a designated variant member | 100 → 800 | 0.01 → 0.07 s, 9 → 27 MB | 0.57 → 2.13 s; the turn-start build **refuses** |
| **Z** anonymous aggregates nested d deep in one class | depth 4 → 64 | 0.00 s flat, 6 → 7 MB | 0.53 s flat; the turn-start build **refuses** |
| **Z** n classes, each with a constructor initializing 3 ordinary members - the per-constructor constant the flattening added | 200 → 3200 | 0.03 → 0.57 s, 15 → 146 MB | the turn-start build is the same 0.57 s and 146 MB |
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

The N audit's own cost is one canonical form per declaration and one set per
call, and both replace work rather than adding it. 13.1's index is keyed by
`TemplateSignature::indexed`, which is one substitution over the places the
declaration's head declared - and what it replaces is `equivalent_template`'s
walk of the chain, which the index only fell through to: 800 declarations of one
function template name is 0.06 s against the turn-start build's 0.07 s, 800
member templates each redeclared out of class 0.06 s against 0.06 s, and 800
constructor templates of one class 0.03 s against 0.03 s, where the reference is
over 60 s at 400 on the first of the three. The form is held under the
declaration's id in `TemplateSignatures::built` wherever a chain is walked, so a
walk costs one comparison per link. `explicit_specializations` is the reading
already made once per candidate, with the refusal of a candidate the list does
not fit kept in a string rather than thrown - no reading is made twice and
nothing is re-read. `call_candidates` is one `unordered_set` of the chains the
ordinary lookup already read plus one reading of the list per declaration 3.4.2
adds: 3200 calls of a template-id callee reached that way is 0.11 s and 33 MB
against the reference's 1.28 s, where the turn-start build refuses. `StandardOnly`
is a bool written twice. The whole 364-file corpus is 1.56 s against the
turn-start build's 1.57 s over a 0.59 s loop floor - 0.97 s of compiler work
against 0.98 s.

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

The L audit's own cost is two field reads per definition demanded and one walk
up the classes a member stands in where an `extern template` named one - so a
program that writes none never leaves `instantiation_is_suppressed`'s first
line, and one that writes 800 of them over 3 out-of-class members each, every
member used, is 0.33 s and 73 MB against the turn-start build's 0.35 s and the
reference's 2.64 s. What it *replaced* is a walk of every member of every
specialization an `extern template` named, made where the declaration stands, so
the depth dimension went the other way too: a class nested 64 deep in a class
template is 0.00 s where the reference is 0.53 s. The array prvalue is one slot
and one `initialize` per list, and `names_a_discarded_array` reads a vector that
holds the array prvalues one full-expression's discardings named - at most one
per operator the descent passes, cleared where the full-expression opens: 1600
discarded array prvalues in one comma chain is 0.05 s in both builds, 1600 of
them initializing pointers 0.11 s against the turn-start build's 0.08 s - which
is the work it was owed, because it stored a null pointer and laid nothing out -
and one nested 128 deep inside its own call argument 0.08 s in both, where
`pa22/cppgm++-ref` is killed at 60 s at depth 32. The whole 371-file corpus is
1.64 s against the turn-start build's 1.65 s over a 0.69 s loop floor - 0.95 s
of compiler work against 0.96 s.

W's own cost is four counters, one derivation walk and two stores, and none of
them scans. 15.4p1's two questions are `call_since_mark_`/`pending_calls_` and
`throwing_since_mark_`/`pending_throwing_calls_`, written where a call already
was and read where a lifetime begins: 1600 temporaries with destructors under
noexcept calls is 0.08 s and 34 MB against the turn-start build's 0.05 s, which
is the work it was owed - it wrote no handler at all - and against the
reference's 0.85 s. 9.3.1p3's naming class is one `Derivation::base_in` per
qualified implicit-object call, and the region it names is the *one* walk 10p1
already makes: a base chain of depth 512 with 200 such calls is 0.05 s, equal to
the turn-start build, where the reference is 2.91 s, and 800 classes each with
one such call 0.19 s against 0.18 s. `resolve` hands the naming region back
rather than resolving the prefix a second time, so the door costs a pointer
write. 6.6.3p2's chain is two bools stamped where an entry joins the list -
which is where the *grant* is made and not where the class instantiation that
built it was, because a body put aside stands at the end of the chain that asked
for it - so a chain of 200 class-returning instantiations is 0.02 s in both
builds. And 10p1's three conversions of a value came out of `sema_expression.cpp`
into `Derivation`, which is what freed both files' room and which also drops two
`Derivation` constructions per `base_value`: the whole 371-file corpus is 1.70 s
against the turn-start build's 1.62 s over a 0.61 s loop floor - 1.09 s of
compiler work against 1.01 s, which is the handlers, the addresses and the entry
points the turn-start build did not write.

W's run evidence: all 50 probes are judged against `g++ -std=c++11
-pedantic-errors -x c++` and against `pa22/cppgm++-ref` through the assignment's
own comparator under a scratch `pa22/tests` directory. All 50 agree with `g++`
on acceptance *and*, where they translate, on the value `lowir2cy86` + `cy86`
runs them to; 47 match the reference byte for byte after canonicalization - 14
unwind shapes over a noexcept free function, a member returning a class with and
without a destructor, an indirectly returned one, a discarded temporary, an
argument temporary, a declared object and a second full-expression; 8 base-step
shapes over `B::f()`, `this->B::f()`, `(*this).B::f()`, `c.B::f()`, `A::f()`,
`C::g()`, a data member and a two-level chain; 4 aggregate shapes over an empty
member reached by a clause, by a prvalue and by nothing; and 24 entry-point
shapes over a template and a non-template derived class, a void-returning
wrapper, a class-returning one, two constructors of one class, a static local, a
reference return, a pointer return, a by-value parameter and both source orders
of the first use. The 3 that differ are recorded above. `valgrind -q
--error-exitcode=9` is clean over 54 inputs after W, 0 errors.

The L audit's run evidence: 62 probes are judged against `g++ -std=c++11
-pedantic-errors -x c++` and against `pa22/cppgm++-ref` through the assignment's
own comparator under a scratch `pa22/tests` directory, and 54 match the
reference byte for byte - 20 `extern template` shapes over an in-class
definition, an out-of-class one above and below the declaration, an `inline`
one, a nested class's member, a member template, a static data member, an
implicit and an out-of-class special member, a virtual member, a `constexpr`
one, an operator, a conversion function, a defaulted member, p11's take-back in
both orders and a member no unit defines; and 20 array and comma shapes over an
empty list, a short one, a matrix, class elements, elements with destructors, a
subscript, an argument that decays, an unevaluated operand, a conditional, a
nest, a statement, a namespace-scope initializer, a for-statement's expression
and three comma spellings, with 14 of them run through `lowir2cy86` + `cy86` for
the value `g++` gives. `g++` accepts 61 of the 62 and refuses the one both other
readings refuse. The 8 the reference answers differently are three it refuses
outright - a subscripted matrix prvalue, an array prvalue of class type and one
written in a conditional - two it refuses over a string literal, and three
recorded above; this build agrees with `g++` on all eight.

`valgrind -q --error-exitcode=9` is clean over 47 inputs after the L audit, 0
errors: its 62 probes' four largest scaling inputs, the `extern template` and
array families, and the `course/pa22` fixture it adds. It was clean over 55
after the S audit and 56 after the N audit. It
was clean over 78 after the U audit, 46
after U, 92 after the B audit, 79 after B, 69 after the C audit, 58 after C, 62
after the R audit, 34 after R, 37 after X, and 57 after the O audit.

U's run evidence stands under the audit's: its 36 probes over 14.6.2.1p6's
member of an unknown specialization, 14.6.2.2p1's `decltype` and 3.2p3's naming
agree with `g++` on 34, and every one that translates runs through `lowir2cy86`
+ `cy86` and returns the value `g++` gives it. The two are the reference's own
divergences, recorded above.

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

B's run evidence stands under the audit's: its 58 probes over 11.2p4's base
path, 11.2 at a nested-name-specifier's components, head equivalence, 14.7.3p5's
`template<>` over a member, 5.1.1p13's non-static data member and the friend
shapes agree with `g++` on 55, and the three are one shape - pointer-to-member
data lowering, outside the PA15 subset here and at the turn-start build alike.
The eight fixtures B wrote are accepted or refused alike by all three oracles
and every one that translates returns the value `g++` gives it.

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

The N audit's run evidence: all 51 probes are judged against `g++ -std=c++11
-pedantic-errors -x c++` and against `pa22/cppgm++-ref`, and 50 agree with `g++`
on acceptance *and* on value - 6 shapes of 13.1's index over two heads that wrote
one parameter-type-list, at a namespace, in a class body, over a class's
constructors, over four heads at once, over a pack head beside a fixed one and
over two heads 14.5.6.1p5 does make one declaration of; 10 of the naming that
reads the written list, at the unqualified, qualified, member, `.template` and
address-of exits in both written orders; 10 of 3.4.2p3's set, over a template-id
callee whose only viable declaration the search adds, a deleted one, a private
static member, a friend no region binds, a declaration both lookups reach and one
candidate from each; 7 abstract-declarator spellings that must still read and 4
that must not; 6 uses of an object of a specialization no declaration named; and
6 ellipsis-constructor shapes. Every probe that translates runs through
`lowir2cy86` + `cy86` and returns the value `g++` gives it, and so does the
`course/pa22` fixture the audit adds - which the turn-start build refuses with
`e is defined twice`. The one that differs is 15.4p1's noexcept-specification,
recorded above. `pa22/cppgm++-ref` parts from `g++` and from this build on two:
a private static member reached through a qualified name, and a class declaring
one member template both with and without a ref-qualifier.

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

S's own cost is one probe per name a fold looks up and one operand split per
subscript, and neither adds a scan. `folded_name` asks the template layer before
ordinary lookup exactly as `id_expression` does, and the ask ends on the first
character test for a name that holds no `<` - 800 plain names read in constant
expressions is 0.02 s and 10 MB against the turn-start build's identical 0.02 s
and 10 MB. What it costs where the name *is* a template-id is the reading the
list was owed: 800 distinct specializations named that way is 0.04 s and 15 MB
where the reference is 0.90 s, and 800 namings of one of them 0.02 s, because
`specialize` interns and 14.7.1p1's demand is made once per specialization
however many namings reach it. The subscript is one bracket matched in the words
already split - 800 of them in one spelling is 0.00 s, and one nested 128 deep in
its own index is 0.00 s and 6 MB flat, because the index is read by the same
walk and not by re-reading the spelling. The whole 366-file corpus is 1.62 s
against the turn-start build's identical measurement.

The S audit's own cost is one walk of the set a naming already built and one
`scan_literal` per literal already scanned. `folded_name` ends on the `<` test
for a name that holds none, so the fourth door it was landed at costs plain names
nothing - 800 of them read in constant expressions is 0.03 s against the
turn-start build's identical 0.03 s - and where the name is a template-id,
`names_specialization` reads four fields per entry of the set
`explicit_specializations` had already built: 800 namings beside a head the
written list does not complete is 0.03 s and one naming beside 100 such heads
0.03 s, both equal to the turn-start build, which refuses them, and 800
`decltype`s of 800 distinct specializations 0.05 s against the reference's
1.09 s. The literal is read as 2.14.5p8's object and then, where it is none, as a
value - which is the two readings the tree's own `evaluate` has always made:
12800 numeric literals in one argument spelling is 0.04 s in both builds, where
the reference segfaults. Dropping the operator's two special cases costs nothing
and removes a depth: a string literal under 128 parentheses is 0.01 s where
`pa22/cppgm++-ref` is killed at 60 s at 32. The whole 367-file corpus is 1.44 s
against the turn-start build's 1.49 s over a 0.46 s loop floor - 0.98 s of
compiler work against 1.03 s.

The S audit's run evidence: all 51 probes are judged against `g++ -std=c++11
-pedantic-errors -x c++` and against `pa22/cppgm++-ref`, and 45 agree with `g++`
on acceptance *and* on value — 20 namings of a function template specialization
at the value, the object, the word and 7.1.6.2p4's `decltype`, over a free
template, a static member template, a member template of a class template,
14.2p4's keyword, a namespace-qualified name, a deleted declaration, a private
one, a list that fits none and a naming made inside a class instantiation; 18
subscript shapes at both readings over an array, a matrix, a pointer, a class
`operator[]`, a member array, an array member and a nested index, in both of
5.7p5's orders; 6 string-literal shapes including two out of bounds and a whole
literal written where a value belongs; 5 shapes of a specialization named from a
class template member's own initializer, one of them demanded from inside
another pattern's reading; and the fixture the audit adds. Every probe that
translates runs through `lowir2cy86` + `cy86` and returns the value `g++` gives
it. The six that differ are two shapes recorded above: a non-type place of
pointer or reference to function is outside the PA20 subset, and `sizeof(&f<T>)`
is refused by `g++` and folded by the reference and by this build alike.

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
| **N, N audit** the four calls 13.3 had to resolve before it had a set, and the index every declaration of a name passes | Four fixtures, four clauses, none of them 13.3's own ranking. 8.1p1 leaves no declarator-id inside a type-id's parentheses and this build read one, so `(dispatch<T>(ex))(f, w)` was `( type-id ) cast-expression` with `(ex)` as a *named* nested declarator - where `Recognizer` already required a ptr-abstract-declarator there and the two parsers had answered one rule two ways. 3.4.2p3's set is the ordinary lookup's *and* the argument-dependent one's, and `named_value` wrote the line for a name it found one declaration of, so 8.4.3p2 refused a deleted `adl_only::make_error_condition` before 3.4.2 had added the one the argument's own namespace declares - the naming is deferred to 13.3 now, and 14.7.1p1's demand it also carried is made on its own, because a specialization an explicit argument list made has 14.5.3p4's expansion still to come to as many parameters as the run is long. 13.3.2p2 counts a declaration whose whole parameter-declaration-clause is `...` viable for one argument, which `converting_constructor` did not - and once it does, 13.3.3.1p4 is what stops the class's copy constructor from being a second way in, a regress with no bottom that `standard_only_` held over the resolution closes. And 5.2.2p1's call on an object of class type asked its class for `operator()` and 13.3.1.1.2p2's surrogates without asking this unit for the class: a declaration of a *reference* to a specialization asks for nothing, so the call is the first use that does. Then the audit: 7.3.3p14's hiding is written about two heads' lists and this build compared the types, so `const K&` under one head and `const K&` under another were two lists - `hiding_signature` is 14.5.6.1p5's own form, held under the declaration's id - and the clause has two source orders, of which only one was read: `equivalent_template` read a brought-in declaration as one 14.5.6.1p5 makes the class's own a redeclaration of, so a using-declaration written *above* the declaration that hides it was `also is defined twice`. Under both was the same hole: a place neither declarator mentioned stands in no type at all, so `template<int N> f(int)` and `template<class T> f(int)` had one signature - the stand-ins now stand at the end of the form itself, and 14.5.6.1p5 came out of `sema_template.cpp` whole as the file's own module.  Then the audit found that form landed at 7.3.3p14's three doors and **13.1's own index left keyed by the list a declarator wrote** - the door every declaration of a name passes, probed *before* `equivalent_template` at a chain, at 11.3p6's hidden chain and at a class's constructors alike, and holding the first entry under a key - so two heads over one list were one declaration and `template<int N> int e(int)` beside `template<class T> int e(int)` was `e is defined twice` at namespace scope, in a class body and over a constructor chain, three programs both oracles accept.  `TemplateSignature` is the reader all four tiers ask, and moving it out of `sema_analyzer.h` is what freed the header's own ceiling.  Under it the naming: 14.1p4 makes what an argument *is* a fact of the declaration it is bound to, and one candidate's refusal ended the *naming* - `e<3>` was `3 does not name a type` and `e<Key>` was `Key is not a constant expression` at all five exits, in either order, where the refusal is now that candidate's and 14.6p8's stand-in count says which are still the arguments' to settle.  And 3.4.2p3's set: the search that fills it was made with the whole spelling of a template-id callee, which no namespace declares, so `reach<tag>(t)` reached nothing 3.4.2 declares and a call with one candidate from each lookup was translated where both oracles call it ambiguous - `call_candidates` searches for the name the template-id names and reads the written list against what it finds, through the same `explicit_specializations` the ordinary exit walks.  Beside them, 13.3.3.1.2p1's flag is held over three readings that refuse by throwing and was put back only where they returned, which a caught refusal leaves standing over every conversion the unit reads after: `StandardOnly` is `sema_lifetime.cpp`'s own `DirectInitialization` shape. | **349 / 364** |
| **S, S audit** 14.2 at the three readings 5.19 makes of one name and at the fourth 7.1.6.2p4 makes, and 5.2.1p1 at the reading that has words | 14.2 leaves a template-id naming the specializations its list makes and no declaration bound to the whole spelling, and `id_expression` asks the template layer before ordinary lookup for exactly that reason - but 5.19 makes *three* readings of one name and every one of them asked only the second door: `id_constant`'s value, `designated`'s object and `TemplateArgumentReader::name`'s word. So `&f<int>` was `no declaration of f<int> is in scope` in a constant expression and read one line down in a function body, at a free function template, a static member template, a member template of a class template and 14.2p4's keyword alike. `folded_name` is the one door all three now ask, and 13.4p1 is why it is a door and not a call: the tree hands its set on for a target type to choose from and a fold has no later to defer to, so a list that fits several declarations of the name names none here. Choosing one is 14.7.1p1's own ask, which `named_function` makes - except under 14.6p8's reading, where the member template of a class template's own body has no pattern recorded at all and the instantiation reads the same spelling again. Under that demand was a second defect: 14.7.3p1's `holds_written_definition` is written about a member of a class an argument list made, and it was asked of the *function* template's own instantiation too - so a specialization named from a member's initializer inside a class instantiation had its body read and then dropped, and the unit emitted a `declare function` for a symbol it owed. Beside them 5.2.1p1, which the by-spelling reading had no postfix arm for at all: the index is read by the same walk over the same words, and `element_at` is the one reading both doors share - so 13.5.5p1's `operator[]`, 5.7p5's pointer and 8.3.4p6's element are written once. And once they were one reading, 5.2.1p1's own sentence closed: `E1[E2]` is `*(E1 + E2)` and 5.7p5 writes that either way round, so `2[a]` names the element `a[2]` does - which the tree reading answered by handing the literal `2` to `string_element` and the spelling by never reaching the walk.   Then the audit: 7.1.6.2p4 is a **fourth** reading that looks one name up with nothing to hand a set on to, and it asked ordinary lookup alone - `decltype(f<int>)` was `no declaration of f<int> is in scope` at namespace scope, at a member, at 14.2p4's keyword and inside a class template's own body, four programs both oracles name the specialization for.  5p8 leaves that operand unevaluated, so `folded_name` takes `used` and the naming asks 14.7.1p1 for no body: a `decltype` nothing else reaches emits neither a definition nor a declaration, byte for byte the reference's output.  Under the door was the *size* of 13.4p1's set: `explicit_specializations` answers for a call, where every place the written list left over is one 14.8.2 deduces from the arguments, and the checkpoint counted those candidates as members - so `&f<int>` beside `template<class T, class U> f()` was `names more than one function template specialization` where both oracles name the one declaration the list completed, and a list that completed nothing would have named a candidate outright.  `names_specialization` is 14.8.1p2 at one entry, with 14.5.3p1's trailing pack the list reached the one place a candidate still is a specialization.  Beside them 5.2.1p1's fourth left operand: `literal_operand` read its own subscript out of the word, so 2.14.5p8's literal was the array only where it stood immediately left of the `[` - `2["abcd"]` and `2[("abcd")]` were two programs `g++` runs and the *tree* reading of the same words already answered.  A literal is that object at both readings now, and the operator's last two special cases - `operand`'s parenthesized-literal arm and `subscript_constant`'s `string_element` shortcut - are what one reading over four operands leaves unwritten. | **354 / 367** |
| **L, L audit** 14.7.2p9's declaration at its three tiers, what a discarded expression is worth at the comma and the array, and when either clause may be asked | `extern template` said nothing at all here: p9's form read p2's requirement that the declaration name a specialization and then returned, so a call of the specialization it named instantiated the body p10 leaves to another unit - a function template's specialization, a member function a class template defines outside its class, and the storage 9.4.2p2's definition of a static data member lays out were three definitions this object file wrote and three the reference declares. `SemaEntity::instantiation_suppressed` is the one fact p9 writes and three readers ask: `has_written_definition`, where a use asks for the body and the answer is the position a specialization whose template defines no pattern already stands in; `require_definition`, where the body an instantiation put aside stays held; and `demand_definition_by_id`, where a line already in the dump is left undemanded so the name goes out as a declaration.  p8's own member walk turned out to be p10's too - `reach_member_definitions` is one predicate read twice - and its `!inline_function` is what keeps a member defined *in* its class, which is what the reference and `g++` both do; p10's note is why an explicitly `inline` function template is not excepted, because what the exception keeps is a body a call may be folded into and not an out-of-line copy this unit writes, which `g++ -c` agrees on symbol for symbol.  Beside it, what a *discarded* expression is worth, at the two things the door that names one had no object for.  5.18p1's left operand is a discarded-value expression exactly as 6.2p1's statement and 5.2.9p4's cast to `void` are, and only the two of those reached `register_discarded_object` - so an object of class type a comma threw away stood in storage named after nothing at all; and p1's *result* is the right operand, so the door's walk descends a comma as it already descended a cast, and `(P(), D(), 5)` discards the object `D()` made rather than the comma that handed it on.  Then 5.2.3p3's `T{...}` over an *array* type, which is the one spelling that writes an array prvalue: the lowering read a `braced-init-list` standing where an expression does as 8.5.4's scalar and took its **first clause** alone, so `(void)A3{ bump(1), bump(2), bump(4) }` ran one of the three calls the program wrote and laid out no array at all - with no template and no pack in it. 12.2p1 makes that prvalue an object, so `array_object_slot` gives it storage of the function's and `initialize` fills its elements there, which is the walk a declaration of an array and 8.3.5p5's one array parameter already had; 8.5.3p5 names the storage `discardarr` at the discarding door and `arraytmp` everywhere else, and 4.2's `unary decay` is marked because the function *named* the array - which storage nothing named leaves unmarked. Seven shapes agree with `pa22/cppgm++-ref` byte for byte: an empty list, a short one, a matrix, an array of class type, an unevaluated `sizeof` operand, an argument that decays and a subscript.  Then the audit: p10 is written about a *definition* and the checkpoint asked it of the declaration's own position - `reach_member_definitions` walked the class's members and wrote the fact onto each one `defined && !inline_function` **there**, so a member whose out-of-class definition stands below the `extern template` was a definition this object file wrote where both oracles declare it, the nested-class spelling with it; and `inline_function` carries 7.1.2p1's specifier as much as 9.3p2's body in the class, so an out-of-class `inline` definition was kept where both oracles suppress it.  p11's take-back was written at the function tier alone and asked about no order at all: `extern template struct box<int>;` with `template struct box<int>;` below it left every member declared where both oracles define them, and the two written the other way round withdrew a definition this unit had been asked for - two object files short the symbols they owe.  `instantiation_is_suppressed` is the clause read where a use asks for the definition instead: one fact on whatever p9 named, `out_of_line_definition` as 9.3p2's own question, and a walk up the classes a member stands in - and 14.5.2's member template, whose specializations no declaration over the class settles, is what the walk leaves out.  Beside them the array: 12.2p1's object was given at the two readings that ask for a value and not at `initialize`, which read the list as 8.5.4's scalar and took its first clause - the checkpoint's own defect one door along - so `const int* p = A3{1, 2, 4};` **stored a null pointer** where all three read the array and decay it.  And two namings: `array_object_slot` took the storage's name from `fact.spelling`, which on a braced-init-list is 2.14.5p1's *code units*, so `C4{"abc"}` opened a slot spelled `abc`; and 8.5.3p5's name descends the cast and the comma in `register_discarded_object` and did not in the lowering, so the operand a discarded comma hands on was `arraytmp` where the reference writes `discardarr`. | **362 / 371** |
| **W, W audit** 15.4p1 at one call rather than at the step it stands in, the class 9.3.1p3 reaches the object through, and the entry points 6.6.3p2's chain owes - and where that chain may begin | `value.require(next_value).execute()` over an all-noexcept member function template opened no handler where the reference opens two: 15.4p1 answers whether an exception leaves *this* call and this build read it as a fact of the whole step, so a temporary with a destructor standing in a full-expression whose calls all throw nothing was left with no region around anything after it.  `note_call` now records the call and settles a region 12.2p3's temporary already asked for however the callee is specified, and `pending_calls_` counts every call an operand's temporary still stands under - with `throwing_since_mark_` and `pending_throwing_calls_` keeping the clause's own answer at the one place a handler would end no lifetime, which is what leaves 12.6.2's mem-initializer its empty region and keeps one out of `Holder holder(5, Counter())`.  The two regions a temporary's own construction may stand in turned out to be two questions - the *call*'s, which the naming of the storage stands in front of, and 12.2p1's object's, which covers the step whole - so the place before that advance is what `begin_object_lifetime` is handed.  Beside it 8.3.5p11's unnamed parameter, numbered by the LowIR parameter list where 6.6.3p2's storage for the returned object is no parameter of the function at all; 8.5.1p2's clause reaching an *empty* subobject, which names the storage it reached where one built in place by a trivial constructor names nothing - `built_in_place_trivially` tells them apart by what the constructor was handed and not by what it does; and 9.3.1p3 with 11.2p5, which reaches the object of a call written with no object expression through the class its nested-name-specifier named, so `super::insert_()` is one step per class named and not one step to the class that declared the member.  Then the entry points: a base subobject built inside an instantiation asks for the entry it names because the instantiation that wrote it owes the rest - except where 6.6.3p2's chain of readings reaches the program's own code through returned objects alone, which is storage the caller itself named, and there the object file owes both.  `PendingDefinition::returned_object_chain` is that chain, stamped where an entry joins the list - at the *grant* and not at the class instantiation that built it - because a queued body never stands inside another.  And 10p1's three conversions of a value came out of `sema_expression.cpp` into `Derivation`, which is where they belong and what freed both files' room.  Then the audit: that chain had a length and no *beginning*.  `queued_chain_` asked two things of the reading about to be made - that the program's own code asked for it and that it hands back an object of class type - and a member of a class template written **in** its class body satisfies both while being neither, because 14.7.1p1 made that body when the class was instantiated and `queue_definition` put it aside, so the grant that later joins it to the list is a use asking for a definition that already existed and not the reading that asked for the object.  `maker<int>::mk` returning a class with a base wrote **both** of the ABI's entry points for that base's constructor where the reference writes one, at every spelling of the object the call is made on - a named local, a global, a reference parameter, a member of another class, a copy temporary, a pointer, a static member, a second call and the same call one statement later - 20 programs the pre-W build answered the reference's way.  `PendingDefinition::held_by_class` bounds the start alone and leaves the length: a body queued while a chain already stands carries the chain it was queued under, and 9.3p2's out-of-class definition is on the other side of the line exactly as it is at `writes_base_entry`.  The three sibling exits the plan named came out clean, and one of them wanted nothing at all: `note_destruction_entry` asks 14.7.1p1's question with the same words and does *not* read the chain, which is right - the reference writes one destructor entry in every shape probed, so a reader there would have written a symbol neither oracle owes. | **367 / 372** |
| **Y, Y audit** 8.3.4p1's bound as a bound *and* a place, and the two walks the new edge had not reached | `template<class T, unsigned long N> struct r<T[N]>` was `r is a template whose parameters PA20 does not instantiate`: a bound the reading had not settled stood in as **1**, so the spelled reader refused anything but digits and the declarator's own reader let `counted_where` throw wherever nothing else was standing in - which is every function template's parameter-declaration-clause. `TypeTable::Node::bound_place` is the entry the constant-expression came to, `kNoType` for a settled bound and in `extra_of` for a dependent one, so `T[N]` and `T[M]` are two types where the stand-in is one and every settled array interns exactly as before. Both readers of the clause write it - `template_argument_value` for the text 14.2 left, `named_place` asked *before* the throw for the tree - and three readings use it: `substituted_array` puts the argument's number back, `match_bound` is 14.8.2.5p13's own pair with 14.3.2p5's conversion to the type the place declared, and 14.5.5.2's ordering reads a place against a place, which is what makes `const T[N]` more specialized than `T[M]`. 8.3.4p1 moved to `ConstexprReading` beside `counted_where`, which is where a clause that counts with a constant expression belongs and which freed the room `sema_analyzer.h` no longer had. 20 shapes swept through the real comparator against `pa22/cppgm++-ref`, `g++ -pedantic-errors` and `lowir2cy86` + `cy86`: all 20 agree.  The audit found the place is a new *edge* on the type graph and two walks of it were never told: `collect_packs` read an array's element type alone, so a pattern whose only pack is its bound named none and 14.8.2.5p9 left the partial specialization silently unselected; and `match_bound` converted the bound to whatever type the place declared, where 14.8.2.5p17 admits an integral type and no other. Both were wrong values with no diagnostic and `pa22/cppgm++-ref` writes both, so `g++` is what settles them and neither can be pinned by a `.ref`. | 367 → **369 / 373** |
| **Z, Z audit** 9.5p1's anonymous aggregate as the object no name reaches, and the four readers the widened fact had to reach | `struct N { union { V v; }; N() {} };` was `abi-mangle: empty source name` and `N() : v() {}` was `a mem-initializer for v names neither a base class nor a non-static data member` - the whole of 9.5's surface with a member whose own class asks for anything. Two facts settle it: `SemaEntity::anonymous_storage`, written where `inject_anonymous_members` declares 9.5p1's object, and `collect_member_targets` beside `declares_subobject`, which flattens a class's subobjects and descends through such an object carrying `one_of`, the union whose single storage each member stands in. 12.6.2p8's construction, 12.4p8's destruction and 12.6.2p2's mem-initializer-id then walk one list: a variant member is initialized where a mem-initializer designated it or 9.5p2's brace-or-equal-initializer reaches it, none at all where neither does, and a second member of one union is 12.6.2p8's refusal. `trivial_default_construction` reads 9.5p1's storage the way `vacuous_destruction` already did - a union's constructor initializes no variant member, so it does nothing however much its members ask for - which is what leaves the unnamed class named nowhere. The audit found three more readers of the same fact. `through_anonymous_storage` handed `member_value` the node it had just wrapped *as the operand too*, so 10.2's base conversion was written into the line the member expression then overwrote - `n.v` through a base's anonymous union reached the lowering as a node with no fact. `ConstexprReading` walked only the class's own direct subobjects, so `constexpr N() : a(3) {}` refused, and after the reader was widened it folded the *wrong value* until the ctor fold descended with the same index the layout gave 9.5p1's object; `pa22/cppgm++-ref` refuses all six shapes and `g++` takes them, so `g++` is what settles them. And `name_regions` wrote `SemaEntity::name` for every region, which is empty for a class 9.5p1 left unnamed - the name the translation gave the *type* stands there now. 34 shapes swept through the real comparator against `pa22/cppgm++-ref`, `g++ -pedantic-errors -x c++` and `lowir2cy86` + `cy86`, valgrind-clean: every accept/refuse and every run value agrees, and the 12 LowIR differences are one thing - `member_storage`'s documented fold of 9.5p1's zero-offset step, which the reference writes as two `index` instructions in a constructor and as one everywhere else. | 369 → **372 / 375** |
