# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **294 / 331** (53 spec + 223 general + 12 course), from a
turn-start baseline of 288 / 331, with pa1-pa18 at **1778 / 1778** and the file
audit passing with the five header-weight warnings it inherited.  The build
itself prints none.

The milestone gives the PA16-PA18 object model its first template tier: a
template-declaration records a pattern instead of declaring anything, and
14.7.1p1's instantiation is that same pattern read once more against a region
that binds each parameter to its argument. Nothing is substituted into syntax
and no text is replayed, so the ordinary PA11-PA18 machinery settles a
specialization exactly as it settles a class the program wrote out.

Three facts about the harness shape what has to be right, read out of
`scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.** `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape.
- **Top-level entries are sorted**, so emission order never matters;
  instruction order, global item order and vtable slot order do.
- **`object=`, `binding=`, `linkage=` and every `alias object` line are
  stripped before the comparison.** The object file's own name for a
  specialization, and whether two units may both define it, are therefore
  requirements no fixture can fail on - they are checked by regenerating every
  `.ref` from `reference-binaries/cppgm++` and by sweeping the emitted symbols
  against it and against g++.

## Stage Design

- **`sema_template.cpp` owns the whole tier**, in the three steps 14 splits it
  into: the template-argument list is read where a name is turned back into
  what was written, the bindings are a region, and the specialization is one
  declaration however many times it is named.
- **A template is a pattern, not a declaration.** `TemplateInfo`
  (`sema_template.h`) is the syntax the template-declaration parameterises, the
  region it was written in, the parameters its head declared with 14.1p9's
  defaults beside them, 14.5.1.3p1's out-of-class member definitions with the
  head each of them wrote, and the specializations an instantiation asked for.
  It hangs off the declaration (`SemaEntity::templated`), because the
  declaration is what a use finds.  `record_template` writes it under
  `SemaDialect::Lowering` only: PA11 and PA12 describe what a
  template-declaration *says* and instantiate nothing.
- **Instantiation is a second reading of the pattern, not a copy of it.**
  `instantiate_class` makes the declaration, and `complete_specialization`
  reads the body against a `ScopeKind::TemplateParameters` region whose
  bindings are typedef-names of the argument types. The declaration and the
  completion are apart because their points are: 14.7.1p1 lets a specialization
  be named before its template is defined, and a dependent argument list
  (`TypeTable::is_dependent`) makes a declaration and no body at all.
- **14.6.1p1's current instantiation is one of those specializations.** It is
  the class the template's own definition declares - the specialization over a
  region binding each parameter to a type standing for itself - so `S<T>`
  written inside `S` and `S<T>` written anywhere else in the template are one
  declaration, found the way every other specialization is.  It is the one
  specialization over a dependent argument list that *is* completed, and
  `TemplateInfo` keeps it beside its region because 14.5.1.3p1's out-of-class
  member definitions are read against the same two however long after the class.
- **A specialization is bound to no name.** It is reached from the template-id
  that wrote its arguments, so ordinary lookup keeps finding the template.
  14.6.1p1's injected-class-name is the specialization, and a
  template-argument-list after it names the template it was made of.
- **Substitution belongs to the walk, not to the type table.** Every category a
  type is only made of types is rebuilt by `TypeTable::substitute`; a
  specialization is the one that is not, because `A<T>` with `T` bound to `int`
  is a class only an instantiation can make. `SemaAnalyzer::substituted` walks
  the type and delegates the rest.
- **Function templates take the same three steps.** The pattern is recorded on
  the declaration the ordinary path makes; `specialize`/`deduce_specialization`
  make the declaration; `instantiate` reads the body against the bindings.
  14.5.6.1p5's two declarations of one template write types that differ, so
  `equivalent_template` asks the question by putting one head's parameters in
  place of the other's - the chain's index of parameter type lists cannot.
- **Deduction is over the P/A pairs the *call* wrote, and nothing else.**
  14.8.2.5p3 leaves a parameter written over no template parameter deducing
  nothing, so whether the argument reaches it is 13.3's question about a
  conversion; 8.3.6p1's unwritten trailing arguments deduce nothing either;
  13.3.1.2p4's first operand is a non-member operator candidate's own first
  argument; and 14.8.2.1p6's overload set is tried one declaration at a time,
  with two that both deduce leaving a non-deduced context rather than a
  failure. 14.8.2.2's target type is the one pair a whole function type makes.
  14.8.2.1p3 is what makes a reference of the argument rather than of the
  parameter: an rvalue reference over an unqualified parameter takes an lvalue's
  type as an lvalue reference, and 8.3.2p6 collapses the two.
- **13.3.3.2p3 carries two different clauses in one field.** A reference
  binding is ordered by how qualified it made the object and every other
  sequence by what a qualification conversion made of a pointer, so a sequence
  of one kind is ordered against one of the other by neither - which is what
  leaves `f(T)` against `f(const T &)` to 14.5.6.2's ordering of the two
  templates, memoised per pair of declarations.
- **14.5.6.2's ordering is one question with two readers.** 13.3.3p1's tie
  between two specializations and 13.4p1's target type both ask which template
  is more specialized, so `more_specialized` answers it once: p5 and p7 strip
  the reference and the qualifiers, `at_least_as_specialized` deduces each
  list from the other and is memoised per pair, and p9 orders by what those two
  clauses took off - which is the only thing left to tell `f(T &)` from
  `f(const T &)` by.
- **Whether two declarations declare one template is a fact of each of them.**
  14.5.6.1p5's types differ because each head declared parameters of its own, so
  each declaration's *signature* - its type with every parameter standing for
  the place its head declared it in - is computed once and the chain is walked
  by comparing types. Asking it of a pair instead costs a substitution per pair,
  and over a class template a specialization per pair as well.
- **A definition is read where it stands as well as where it is used.**
  14.6p8 makes a template definition ill-formed - no diagnostic required - where
  no valid specialization could be generated from it, so the body is read once
  at its own point and again for each specialization.  The first reading is the
  *PA11* one: a type that depends on a parameter has no layout, no conversion
  and no overload set until an argument arrives, so what the pattern can be
  asked is what its declarations say and 3.4p1's lookup of the names it writes.
  A function template's body is `check_template_definition`'s; a class
  template's own definition is `read_class_pattern`'s, as the current
  instantiation; 14.5.1.3p1's out-of-class member definition is
  `read_member_pattern`'s, against the same class.
- **What a definition being read cannot answer is 14.6.2p1's.**  A name written
  after a prefix that depends on a parameter stands for a type of its own -
  `dependent_member_name`, one per prefix and *component*, so one definition
  writes one type for one name and a name of three components is a member of a
  member.  What the type keeps is the prefix and the name rather than the
  spelling the two make, because the ABI writes them apart: `typename
  T::car_type` is `NT_8car_typeE` and not the `T_` a parameter alone would be.
  14.6.2p3 leaves a dependent base off the lookup chain
  while a non-dependent one joins it, because 10.2p2's lookup of an unqualified
  name reaches what that base declares.  **And it is left off the
  specialization's chain too**: 3.4.1 answers a name the definition wrote where
  the definition stands, so what an argument list makes of the base changes
  nothing.  The fact is the clause's rather than the class's -
  `dependent_bases_` is the base-specifiers a reading found dependent - because
  one clause is read once by the definition and again by every specialization
  and every class an instantiated body declares.  `Scope::dependent_base` is
  what the chain carries, and `unqualified_base` is the one place 3.4.1 asks
  for it: 3.4.3's qualified lookup and 3.4.5's class member access name the
  *object* rather than read the definition, so they walk the same chain
  unchanged, and the link is dropped only where the search reaches the class
  from inside it - a class further down holds the base subobject through a link
  of its own.  And what a dependent type is worth -
  its size, a constant named through it, a decltype over an expression the
  reading does not type, the type a bit-field was declared with - an argument
  list is what says, so the reading stands one value in its place.
- **What the reading does ask of a declaration is everything the declaration
  says.**  9.3.1p3's implicit object parameter is part of that, so
  12.3.2p1's conversion function and 13.5p6's operator are read as the members
  they are; 15.4p1's exception-specification is another, and it is a fact of
  each declaration rather than of the function, so two declarations of one
  function are compared wherever one redeclares the other.  What is left out is
  the body of a special member: the reading has no definition to write, and
  12.6.2's mem-initializers name members an argument list is what settles.
- **An instantiation reads the declarations and leaves the definitions.**
  14.7.1p1: instantiating a class template specialization instantiates the
  *declarations* of its members and not their definitions, so a member body the
  reading of the pattern arrives at is put aside - `held_definitions_`, keyed by
  the declaration it defines - and the use that names the member is what asks
  for it.  `require_definition` is that ask, and it is one hash probe at the
  three places a function becomes a use: `function_value`, where a callee, an
  `&` and a target type's chosen declaration all pass; the demand a constructor
  gets where the object it builds is; and the destruction entry every end of a
  lifetime notes.  A definition granted joins the same list 9.2p2's already
  walks, by index over a deque, so a use written inside a body being written
  there is reached by the same walk.  10.3p10's virtual member has no
  expression to point at - a table names it - so the class being complete is
  what asks for those.  `deferred_conversion<incomplete>` is what the clause is
  for: the class has a layout an object needs, and only the body of the
  conversion function nothing calls names `sizeof(T)`.
- **The reading leaves nothing behind.**  Its lines stand in a dump nothing
  reads, and 14.7.1p1 makes a template-id it names a declaration rather than a
  use requiring a definition.  `TemplateInfo::specializations` is not an
  inventory of what exists: it is what a declaration arriving *later* is read
  for, so `require_specialization` is where a specialization an instantiation
  asked for joins it, and one only a reading ever named is on it nowhere.
- **One body's facts belong to that body.**  `FunctionReading` puts aside what
  the reading around it knew, because naming a specialization in the middle of a
  body is what asks for another body to be read; `DialectReading` does the same
  for which of the three dialects the walk is in.  9.2p2's complete-class
  context is the same idea over a class: a member body written in a class body
  is held on `held_bodies_` until the class-specifier closes, so it may name a
  member the class declares below it, and a reading standing inside another
  takes only the entries above its own mark.  Reading a held body can hold
  another - a class declared in it writes member functions of its own - so the
  list is *drained* back to that mark rather than walked once, and a function
  template's own reading drains what its body held the same way.
- **A template parameter is redeclared by a fact of the regions.**  14.6.1p6
  refuses a declaration of a parameter's name anywhere in the template, so the
  question is asked at *every* declaration that binds a name, and the
  template-parameter regions standing over a region are chained as they are
  opened, so it costs the number of template heads above the declaration and
  not the block nesting it happens to be written at.  The one name no region can
  be asked about is the template's own, bound before its head is read, so
  `record_template` asks the head directly.
- **A name is declared once as one kind of type.**  7.1.3p3 lets a region
  redeclare a *typedef-name* and 7.1.3p6 lets a class redeclare a class-name as
  one, and neither runs the other way, so `declare_type_alias` refuses a
  typedef-name a class already declares (9.2p1) and `declare_type_name` refuses
  a class-name or an enum-name where a typedef-name of that spelling stands.
- **A member definition written outside its class declares into the region its
  name reaches.**  9.4.2p1 and 3.4.1p8: what encloses the class a qualified
  class-head-name defines is that region and not the one the definition stands
  in, which is what lets a nested class defined outside its owner name 9p2's
  injected-class-name of the owner.
- **The parameters a member definition's head wrote are that definition's
  alone.**  14.1p2 lets each head spell the places the argument list is in the
  order of as it likes, and the body is read from a region enclosed by the
  class - so `open_member_parameters` opens a region of the definition's own,
  inside the one the class was completed against, and `EnclosedBy` stands it
  between the class and that one for as long as the definition is read.  A name
  the head wrote then reaches the argument its own place took whatever the
  class-head called that place, and nothing it binds is standing when the next
  definition is read.  Binding them into the class's own region instead makes
  the *second* definition's names collide with the first's.
- **A specialization has a second point of instantiation at the end of the
  unit.** 14.6.4.1p1: the pending entry a name leaves is settled where the walk
  reaches it, so the definition the template has by the end of the unit is the
  one the specialization stands for however far above it the name stands.
- **An object-file name is walked, never split.** `lowir_abi.cpp` builds the
  components of every encoded name from the *declaration's own regions*, and
  what a specialization is named by is two facts - the template's own qualified
  name and the argument `TypeId`s - because the ABI writes them apart.  Its
  *spelling* is a name too: `canonicalize_lowir_for_compare` masks a function's
  LowIR symbol and leaves a global's, so a specialization writes its argument
  list the way a program does, comma and space alike.
- **14.7.1p1's definition is nobody's.** `LowirUnitLowering::shared_definition`
  answers one question for the three that follow from it: what the object file
  binds the symbol as, which of 12.1's entry points the definition owes, and
  whether 3.2p3 waits for a use before writing it.
- **A type-id's spelling is read as a declarator, not as a word list.**
  `split_type_id` keeps a name whole, and
  `type_id_words`/`abstract_declarator_words`/`suffix_words` read 8.1p1's
  type-specifier-seq and 8.3p1's abstract-declarator from what is left.
- **What a definition did not write, the other declarations of the same entity
  did.**  8.3.5p10 leaves a parameter's name out of the function's type, so the
  name is a fact of the *function*: the record each declaration already leaves
  about its parameters (`ParameterRecord`) carries the name beside the
  default-argument, first-namer wins, and the definition's own name beats both.
  A definition the *standard* writes - 12.8p28's and 12.9p8's - has no
  declarator to spell its places with, so it asks that record where it makes its
  objects, which is after the whole unit has been read, and asks the base's
  record where the constructor is an inherited one.  14.7.1p1 asks a narrower
  question for a specialization: it is a declaration nothing wrote, so the
  spelling is the template's *first* declaration's and no later one's, and
  `spelled_for` is the one place the two questions are told apart.
  9.4.2p3 is the same shape over an object: the class wrote the
  brace-or-equal-initializer and the definition at namespace scope shall write
  none, so the storage that definition lays out holds what the class wrote -
  and 3.2p3 makes a read of the member's *value* that constant rather than a
  load, while its address still names the object.  Only the object file asks
  either question, so PA11 and PA12 go on describing each declaration as it
  stands.
- **8.3.5p5 adjusts two different things.**  The array and the function become
  pointers in the *type of the function* and in *the object the body names*
  alike, so `parameter_object` is what `declare_parameters` builds the entity
  from; the top-level cv-qualifiers the same clause drops are dropped from the
  function type alone, because a by-value parameter written `const` is a const
  object and 13.3 asks that object which overload a call on it reaches.
- **What a declarator declares is the type its declarator-id ends up under.**
  8.3p1 builds it outside in - the ptr-operators of a level are applied before
  its suffixes, and the suffixes from the last inwards - so the constructor the
  id is left with is the *first* suffix at its own level, or what the level
  around it handed down where that level wrote none.  8.4p1's function
  definition and 8.3.5p10's places are one question with two readers, so
  `declares_function` and `declarator_type` ask it the same way:
  `T (&f(P))[2]` and `T (*f(P))(Q)` are both the function their own
  parameter-clause makes however many suffixes stand outside it, and the names
  they bind are that clause's - `Q`'s places belong to the type `f` returns and
  to nothing its body can name, while `T (f)(P)` takes the clause beside it
  because its own level wrote none.
- **A `<` is answered by the overload set, and a using-directive is answered
  last.**  14.2p3 asks whether *any* member of what lookup found is a function
  template, so a spelling declared as both is one answer and not the later
  declaration's; 3.3.10p2 keeps every other pair at the later one.  And 7.3.4p2
  puts the names a directive reaches in the namespace enclosing both it and
  what it nominates, so every region the name is written inside is searched
  first - the open scopes for the ones this parse still holds, and the spelling
  the prefixes in force give the name for a namespace closed and reopened.
- **14.7.2's explicit instantiation is a demand with no use behind it.**  It is
  the one declaration that asks this unit for a definition 3.2p3 would
  otherwise leave to a use, so `SemaEntity::explicitly_instantiated` is what
  `collect_definitions` reads instead of deferring, and the object file writes
  `object_root=yes` beside the weak binding it already had.  p11 bounds it to
  the members *defined* where it stands, and 9.3p2's member defined in its
  class is left where it was: an inline definition belongs to every unit that
  needs one, so no unit is asked to root a copy nothing there reaches.
  Whether this unit owes the definitions is a fact about the declaration rather
  than a terminal inside it, so 14.7.2p9's `extern template` and p1's `template`
  are each a node of its own - which is what the PA10 dump the shared AST feeds
  spells them by.  The grammar's other target is a simple-declaration, and it
  *declares* nothing either: the declaration is read for the type it writes and the specialization
  is looked up by it - 14.8.1's explicit argument list where one is written,
  14.8.2.2's deduction from that type where none is, and the member a class
  template specialization already made where the prefix names one.  9.3.1p3's
  object parameter is part of what a declaration says rather than of what this
  one wrote, so each candidate is asked with the spelling its own declaration
  carries, and 14.7.2p2 refuses a declaration that names no specialization at
  all.
- **An object owes an action only where something would run.**  8.5p8's zero is
  what its base subobject and its non-static data members hold, so an object
  every subobject of which holds nothing has no byte a trivial constructor
  could write - and a class whose one member is of an empty class, which 9p6
  leaves not empty, is one of those.  12.1p11's vpointer is asked about first.

## Current Failure Map

37 of 331 fail - the 43 C8 found, less the 6 it landed, and no test moved the
other way.  Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| 8 | 7.1.6.2p1's decltype-specifier written in a declarator | `-> decltype(y - x)` and `decltype(begin(*t))*` read where 3.3.7p1 has already put the declarator's own parameters in scope; `decltype(object)::executor_type` and `basic_regex<BidiIter> const` read as a *type-id* where a template-argument list spells one, which today is a word split rather than a declarator parse; and `decltype(declval<F>()())` over a call whose callee is dependent |
| 7 | a name written in a template that only the argument list settles | `typename Iter::owner::later` deferred to a typedef written later, a nested member's base alias, a member `operator>>` reached lazily, an elaborated argument named in an enclosing scope, 13.5p6's builtin beating a class operator, `__alignof` over an instantiation, 12.9p1 through an alias template |
| 5 | a template declared or defined with a qualified name | `template<class T> class v::C {...}`, `template<class T> int n::f(T) {...}`, `types::item<int>(value)` as a parameter-declaration, `fusion::remove<X>` as a dependent qualified return, 14.8.1p2's partial explicit argument list |
| 8 | 12.1/12.8/8.5p7: the definitions a class owes and what an initialization writes | the reference emits an explicitly-defaulted copy constructor this unit elides everywhere and elides an out-of-class defaulted one this unit calls; a reference member left memberwise; 8.5p7's zero written before a constructor call the reference writes alone; the complete-object entry of a base constructor (see the probe below); `this` recomputed |
| 6 | what an instantiation writes that nothing runs | two spurious empty `[role=init]` entries - one from a specialization completed while its argument was incomplete, one over a static data member of POD class type; a global emitted for a *dependent* specialization (`@v_T_T_`); a branch left unfolded; `0` where a pointer parameter wants `nullptr`; a temporary named for an argument where a return names it |
| 3 | 14.1p10 and 3.4.2p2's remainder | a later redeclaration's default template argument, and two decltype-through-an-inline-namespace lookups |

## Active Checkpoint

**C9 - 7.1.6.2p1's decltype-specifier, and the region a declarator's own
parameters stand in**: this is the largest group left and the one with a single
owner.  Two clauses meet in it.  3.3.7p1 makes a parameter's potential scope
begin at its declarator-id and run to the end of the *function declarator*, so
`-> decltype(y - x)` and `decltype(dep::begin(*t))*` are read with `x`, `y` and
`t` already declared - today the parameter list is built into a
`std::vector<Parameter>` and nothing is bound until `declare_parameters` opens
the function's own region, so both are `no declaration of ... is in scope`.
And 14.2's template-argument list reaches the semantic layer as the *spelling*
inside a name, which `split_type_id` splits into words - so
`decltype(object)::executor_type` is looked up as a name and
`basic_regex<BidiIter> const` loses the cv-qualifier a template-id may carry.

- **owner**: `sema_declarator.cpp` for the region, `sema_template.cpp` for the
  spelling.  `declarator_type` is where 8.3.5p1's parameter-clause is read and
  where a trailing-return-type and a later parameter's own type-id are read
  after it; `type_id_words` is the one place a template argument's spelling is
  turned back into a type.
- **data flow**: `read_parameters` builds the list -> a region of its own is
  opened over the declaration's, each place bound as it is read -> the
  trailing-return-type and every later parameter-declaration are read from that
  region -> the region is closed and `declare_parameters` goes on declaring the
  objects the body names.  For the spelling: `type_id_words` sees a word
  beginning `decltype(` -> the operand is read as an expression against the
  region the argument list stands in -> what follows `::` is looked up in the
  type it named, and a trailing `const`/`volatile` qualifies it.
- **expected complexity**: one region per function declarator that writes a
  parameter, opened only where one is written, and one expression reading per
  decltype-specifier - so a declarator with no decltype in it costs one branch.
- **known obstacle**: 8.3p1's declarator is read outside in and a nested
  parameter-clause may stand under another (C7's `T (*f(P))(Q)`), so the region
  belongs to the clause that spells the places rather than to the declarator,
  and it has to be gone before the declaration is made - 8.3.5p10 leaves a
  parameter's name out of the function's type, and nothing the head bound may
  outlive it.
- **validation**: the eight fixtures of the first group, then a declarator
  writing a decltype over a parameter of an *enclosing* clause, then the pa19
  report and pa1-pa18.

## Performance Model

The dominant operation is one reading of one pattern per specialization, which
is linear in the pattern, plus 14.6p8's one reading per template *definition*.
What is superlinear is superlinear in the *program*.

Measured on the shapes the tier makes scaling-sensitive, each timed twice,
`cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n distinct class templates, each with a body, none instantiated | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.03 s |
| n distinct specializations of one class template, each with a member function | 0.00 s | 0.01 s | 0.02 s | 0.04 s | 0.10 s |
| n qualified dependent names in one class template's body | 0.00 s | 0.00 s | 0.00 s | 0.00 s | 0.00 s |
| n out-of-class member definitions of one class template | 0.00 s | 0.00 s | 0.01 s | 0.01 s | 0.03 s |
| n member function bodies in one class template | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.02 s |
| one specialization named n times | 0.00 s | 0.00 s | 0.01 s | 0.01 s | 0.03 s |
| n specializations of one class template over n classes, through a call | 0.01 s | 0.02 s | 0.03 s | 0.07 s | 0.15 s |
| n function templates, each an 8-statement body of initializers, none called | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.04 s |
| n nested blocks in a function template, each declaring a name | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.02 s |
| n declarations of one template name, none of them called | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.02 s |
| n class templates, each deriving from the previous one's current instantiation | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.04 s |
| **n out-of-class member definitions of a template with n specializations** | 0.04 s | 0.18 s | 0.73 s | - | - |
| **n function templates overloading one name, each called once** | 0.00 s | 0.01 s | 0.03 s | 0.11 s | 0.41 s |
| **n target types each choosing among n function templates** | 0.01 s | 0.01 s | 0.03 s | 0.07 s | 0.17 s |

The first five are what 14.6p8's reading is measured by, and each is linear:
the reading is one walk of each definition's syntax, one ordinary lookup per
name it writes, and one hash probe per declaration it makes, so it costs the
*source* and not the specializations the unit goes on to make.  The sixth is
what says the memos work; the tenth is what the 14.5.6.1p5 signature answers.
The region each out-of-class member definition opens for its own head names is
one region per reading, which is the count the tier had before those names were
bound into the class's own.

The last three are quadratic.  Two of them are the program's own shape rather
than the tier's: 13.3p1 gathers every declaration of a name, so n calls over an
n-declaration chain are n^2 candidates however they are ranked, and n classes
each deriving from the one before it are n^2 base links however a name is looked
up through them.  The third is this milestone's *reading* of 14.5.1.3p1 - every
out-of-class member definition is read for every specialization, where 14.7.1p1
instantiates the declarations a class needs and leaves each definition to the
use that requires it - so n = 128 is 16384 readings for one emitted function.
`reference-binaries/cppgm++` is **1.00 s** on that shape against our 0.73 s,
**3.88 s** at n = 512 on the derivation chain against our 0.04 s, and 0.89 s
against our 0.10 s on n distinct specializations.

**One shape is exponential and it is the spelling.** `typedef P<t,t>` repeated
n times names a class whose written-out spelling doubles at every level, and a
specialization is named by that spelling: 0.01 s, 0.16 s, 0.67 s and 2.75 s at
n = 12, 16, 18 and 20 in 23 lines of source - unchanged by this checkpoint and
by its audit.
`reference-binaries/cppgm++` is 0.19 s, 2.85 s, 12.28 s and **46.31 s** on the
same inputs, so this is the milestone's shape rather than the tier's; g++ does
n = 20 in 0.06 s because it never materialises the spelling. Fixing it means not
storing a specialization's written-out name at all.

C8's own risk is two: one hash probe per name that becomes a function value,
and one held body per member of every specialization.  Measured against a
worktree build of `4ec3d164`, each shape timed twice, `-O0`:

| shape | n = 32 | n = 128 | n = 512 | before |
| --- | --- | --- | --- | --- |
| n specializations of one class template, each with a member | 0.00 s | 0.01 s | 0.05 s | 0.06 s |
| n member bodies in one class template | 0.00 s | 0.00 s | 0.01 s | same |
| n class templates, each deriving from the previous one | 0.00 s | 0.00 s | 0.01 s | same |
| n specializations over n classes, through a call | 0.00 s | 0.02 s | 0.08 s | same |
| one specialization named n times | 0.00 s | 0.00 s | 0.02 s | same |
| **n specializations of a 16-member class template, one member called** | 0.01 s | 0.03 s | **0.15 s** | 0.06, **0.28 s** |
| n ordinary calls in a unit that also holds a specialization | 0.00 s | 0.01 s | 0.03 s | same |
| n out-of-class member definitions with n specializations | 0.03 s | 0.12 s (n=64) | 0.48 s (n=128) | 0.03, 0.11, 0.47 s |

The sixth is what 14.7.1p1 is worth: 512 specializations of a class with 16
members, one of which is called, is 8192 body readings before and 512 after -
0.28 s and 68.0 MB against 0.15 s and 47.5 MB.  The seventh is what says the
probe is free: `require_definition` returns on an empty map, and a unit that
holds one specialization pays one hash lookup per function name.  The last is
14.5.1.3p1's reading, which C8 does not defer and which is unchanged: an
out-of-class member definition is read where it stands, for every
specialization already made, so it is still the milestone's one quadratic in
the *tier* rather than in the program.

The C7 audit's own risk is one inward walk per declarator level, which every
declarator of every unit goes through, and one reading per explicit
instantiation.  Measured against a worktree build of `c3f2411f`, each shape
timed twice:

| shape | n = 32 | n = 128 | n = 512 | before |
| --- | --- | --- | --- | --- |
| n ordinary function definitions | 0.00 s | 0.01 s | 0.02 s | same |
| n definitions of `int (*f(int))(int)` | 0.00 s | 0.01 s | 0.03 s | refused |
| one declarator under n redundant parentheses | 0.00 s | 0.00 s | 0.01 s | same |
| the same over `int (&f(int))[2]` | 0.00 s | 0.00 s | 0.01 s | same |
| n explicit instantiations of one template over n classes | 0.01 s | 0.02 s | 0.07 s | did nothing |
| n explicit instantiations of one specialization | 0.00 s | 0.00 s | 0.01 s | did nothing |
| n members of one specialization, each explicitly instantiated | 0.01 s | 0.01 s | 0.04 s | did nothing |

The declarator question is asked once per *definition* rather than once per
level, because the level that spells the places takes them away from the ones
inside it.  The shape that does ask it at every level - n parentheses around a
pointer-returning definition - is 0.03, 0.11, 0.42 and **1.72 s** at n = 1000,
2000, 4000 and 8000, against 0.41 and 1.76 s for the same nest with no pointer
in it; both are inside the quadratic the AST walk above them already is, and
16000 is not a translation unit.  The explicit instantiation shapes are each
linear and each larger than the pre-audit build's, which did nothing for them at
all.

The lookup C7 added - one probe per prefix in force, and only for a name every
open scope and every region around it has already missed - is at the pre-C7
build's times to the hundredth: n-deep namespaces named from the innermost,
a reopened namespace of n names, and n using-directives over n names are
0.14 s, 0.01 s and 0.07 s at n = 512, the first of them 3.4.1p1's own quadratic.
8.3.5p10's record is unchanged from two turns ago: n unnamed places named from
below, n declarations of one function template, n out-of-class defaulted copy
constructors and n inherited constructors are 0.02 s, 0.01 s, 0.13 s and 0.08 s
at n = 512.

Valgrind is clean over all 332 fixtures.  A 20000-deep parenthesized
expression is refused by the parser at about 1000, so the definition-time walk's
recursion is bounded by the same limit the expression layer already is; a
declarator's own parenthesis nest is refused between 8000 and 16000, which is
what bounds every walk that reads one.

`dev/src/sema_analyzer.h` sits at 2381 lines against the audit's 2400: C8 moved
8.5.1p2's `InitializerClauses` to `sema_declaration.h` before adding to it, as
C5 moved 9.6p2's bit-field storage unit and 3.4.2p2's associated regions and as
the C7 audit moved 8.5p16's `WrittenInitializer`.  The next checkpoint that adds
to the header has to move another record out first.

Two more disagreements were decided against the reference by C8, on the same
grounds.  14.6.2p3 is one rule about *every* unqualified name, and the
reference applies it only to the one a call writes: an unqualified `value`
naming a non-static data member of a dependent base, and an unqualified `kind`
naming a typedef in one, each reach the base there and reach the namespace in
g++ and here - and 3.4.1's own text and every checked-in fixture are on our
side.  And 12.1's entry points: a constructor of a specialization that only a
base subobject ran gets both of the ABI's names from g++ and from the reference
where the *deriving* class is an ordinary one, and only the base-object name
from the reference where the deriving class is itself a specialization - which
is 14.7.1p1 answered two ways by one binary.  `200-defaulted-template-arg-base-
initializer-match` wants both and `100-member-cv-overload-deduction-argument`
wants one, so the rule that tells them apart is a fact about the deriving class
and not about the definition; C8 tried `base_object_entry && abi_instantiated`,
which trades one fixture for the other, and left the pre-C8 answer standing
until the rule is one that can be stated.

Eight disagreements with `reference-binaries/cppgm++` that no fixture covers
were left standing deliberately, because the standard and g++ are on the other
side: it writes `zero` for a static data member its class gave a
brace-or-equal-initializer, where g++ emits the value and 9.4.2p3 says the
object holds it; it folds a read of a `volatile` member and of a `double` one,
which 9.4.2p3 and 5.19p2 leave as objects to read; it cannot find a parameter
name in the body of a function whose declarator-id stands under a nested clause
- `int (&f(int a))[2]`, `int (*f(int a))(int)` and six shapes beside them -
which 8.3p1, its own `_Z1fl` and g++ say is the function's own; it refuses
`template int t<int>::v;`, which 14.7.2p1 lists outright and g++ accepts; it
roots no definition for a member *class*'s members at an explicit instantiation,
where g++ emits all 40 of a 40-deep chain; and 14.7.2p11's static data member
defined before an explicit instantiation it never writes.  What is *followed*
rather than argued with is its reading of an in-class member definition at an
explicit instantiation of the *class*, which it roots only where an out-of-class
definition also precedes: g++ roots it always, and the rule kept here - 9.3p2's
inline definition is no unit's to root - is the one every checked-in fixture
through pa24 agrees with.  An explicit instantiation naming that member
directly roots it in all three.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern a template-declaration parameterises, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading of that pattern against a bindings region, 14.1p9's defaults, 14.6.1p1's injected-class-name, the ABI's `<template-args>` on a specialization and on its members, 12.1p1's constructor named by the template-name, and 8.1p1's type-id read as a declarator; `sema_value.h` and `sema_template.h` split out of `sema_analyzer.h` | 27 -> **114 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 | the function tier and what a name cannot spell: a function template's pattern recorded on the declaration the ordinary path makes and read again for the specialization that named it; the ABI's `<template-args>`, result type and `T_` signature; 14.5.1.3p1's out-of-class member definitions read once the specialization's body is complete; a static data member of a specialization named by handing PA14's encoder the components a data name's one spelling cannot be split into | 114 -> **172 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| C2 completion | the two points a specialization has, and what a dependent argument list names: the declaration made where the template-id stands and the body read where the definition is; 14.6.2p1's dependent argument list making a declaration and no body; `SemaAnalyzer::substituted` owning what `TypeTable::substitute` cannot rebuild; 14.8.2.1p2's reference parameter and 14.8.2.5p4's `A<T>` against `A<int>` | 172 -> **194 / 293**; pa1-pa18 1777 / 1777; file audit passes |
| tier audit | the object file's name for a specialization, which this suite cannot see: the components of an encoded name walked out of the declaration's own regions rather than split out of a spelling, each owning class asked where its own declaration stands, a class named through a specialization written as a member type, the ABI's `<template-param>` made a substitution candidate, and 14.7.1p1's shared definition answered once for the three readers that follow from it; 14.6.2p1's answer kept per type | 194 -> **200 / 295**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 -> 9 tests; 13 names byte-identical to g++; valgrind clean |
| C3 | the call a template joins: 14.8.2.5p3's parameter written over no template parameter, 14.8.2.1p4's reference top, 8.3.6p1's unwritten trailing arguments read against the arguments the specialization was made from, 13.3.1.2p4's first operand and 13.4p1's overloaded name left standing where an operator gathers its candidates, 14.8.2.2's target type, 14.8.2.1p6's overload set, 3.4.2p2's template arguments, 13.5p6 asked of the specialization rather than the template, 14.5.6.2's ordering of two templates whose conversions tie, 14.5.6.1p5's equivalent declarations, and 8.5.3p5's temporary for a literal a reference binds; 8.5.1's aggregate walk and 8.5.4's list-initialization split into `sema_init_list.cpp` | 200 -> **223 / 295**; pa1-pa18 1777 / 1777; file audit passes; valgrind clean over the newly reached paths and the four new scaling shapes |
| C3 audit | the declaration a target type chooses, the clauses an ordering strips, and the definition a name written above it still gets: 14.5.6.2p4's ordering as one question with 13.3.3p1's tie and 13.4p1's target as its two readers, 14.5.6.2p9 and p10 beneath the p5 and p7 that strip what they order by, 14.8.2.1p3's lvalue reference collapsed by 8.3.2p6, 5.3.3p2 and 5.3.6p3 over a reference type-id, 14.6.4.1p1's point of instantiation at the end of the unit for a specialization named above its template's definition, and 14.5.6.1p5's equivalence as a signature each declaration has on its own | 223 -> **227 / 299**; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 60 run-and-compare programs agreeing with `reference-binaries/cppgm++` and g++; declaring 512 overloads of one template name 0.36 s -> 0.04 s and 44.8 MB -> 15.8 MB; valgrind clean |
| C4 | the reading a template definition gets where it stands: 14.6p8's body read once at its own point in the PA11 dialect and again for each specialization, with 14.6.2p1's member name, 14.2's template-id and 3.4.2p2's callee left to the instantiation; 14.7.1p1's naming made a declaration and not a use; 14.6.1p6's redeclared template parameter as a fact of the chained template-parameter regions; 9.2p1's member type declared twice against 7.1.3p3's namespace redeclaration; `FunctionReading` and `DialectReading` over the three readings of one body; `sema_declaration.h` and `sema_function.cpp` split out | 227 / 299 -> **235 / 301**; pa1-pa18 1777 / 1777; file audit passes; 1024 nested blocks under a template head 0.10 s -> 0.08 s; valgrind clean |
| C4 audit | the reading's own edges: 14.7.1p1's list of the specializations an *instantiation* asked for; 14.6p8 over a declaration's initializer as well as a statement's operand; 3.4.2p2's callee looked up where the arguments associate nothing; 14.6.1p6 asked wherever a declaration binds a name; and 3.3.10p2's type-name refused where a typedef-name of that spelling already stands | 235 / 301 -> **242 / 308**, the seven new tests being the seven regressions these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 95 synthesized programs through three compilers; `object=` differences 9 -> 7 tests and `binding=` 12 -> 10; valgrind clean over all 308 fixtures |
| C5 | the class a template makes of its own parameters: 14.6.1p1's current instantiation as the class a class template's own definition declares, read once where it stands against a kept region binding each parameter to a type standing for itself; 14.5.1.3p1's out-of-class member definition read against the same two, with 14.1p2's own head names bound beside the class's where 3.4.1p8's body looks them up; 14.6.2p1's dependent qualified name given one type per prefix and spelling, 14.6.2p3's dependent base left off the lookup chain, and what a dependent type is worth stood in for; 9.2p2's complete-class context as a held-body list; 9.4.2p1's qualified class-head defining into the region its name reaches; 9.3.1p3's object parameter read for a pattern so 12.3.2p1 and 13.5p6 see the member; 15.4p1 asked wherever one declaration redeclares another; 7.3.3p1's `using typename` | 242 -> **254 / 308**, the five accepted `-bad` fixtures all refused; pa1-pa18 1777 / 1777; file audit passes; 512 class-template definitions read in 0.03 s and 512 out-of-class member definitions in 0.01 s, both linear; valgrind clean over all 306 pa19 fixtures |
| C5 audit | the region a member definition's head names stand in, and the two facts a dependent name is written from: 14.1p2's out-of-class head read against a region of its own, standing between the class and the one it was completed against, so a head spelling the class's places in another order is no longer read against the class's own spelling and no name it binds outlives it; 9.2p2's held bodies drained back to the mark rather than walked once, and a function template's own reading draining what its body held; 14.6.2p1's dependent member name kept as the prefix and the name the ABI writes apart, one type per component; and 14.7.1p1's specialization spelled the way a program writes an argument list | 254 / 308 -> **256 / 310**, the two new tests being the two regressions these leave and the failing 54 the same 54; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 73 synthesized programs through three compilers with 56 compared as emitted LowIR; the dependent-member names byte-identical to `reference-binaries/cppgm++` *and* to g++; `object=` differences 7 and the last passing test that hid one now green in that sweep; fourteen scaling shapes measured against a pre-C5 worktree build; valgrind clean over all 308 fixtures under `pa19/tests` |
| C6 | what a definition takes from the other declarations of the same entity, and what it owes the object file: 9.4.2p3's in-class brace-or-equal-initializer read as the value the definition's storage holds and, through 3.2p3, as what a read of the member *is* rather than a load of it, while its address still names the object; 8.3.5p10's parameter name made a fact of the function, held beside the default-argument each declaration already records, first-namer winning and the definition beating both, and asked of 12.1p1's constructor too; 8.3.5p5's array and function parameters made the pointer objects they are, with the cv-qualifiers the clause drops kept on the object so 13.3 still sees a `const` by-value parameter; and 8.5p8's "holds nothing" read of the whole object, so a class whose one member is of an empty class owes no `[role=init]` entry | 256 -> **261 / 310**; pa1-pa18 1777 / 1777; file audit passes; 44 synthesized programs swept against `reference-binaries/cppgm++` with the three surviving disagreements decided against it by g++ and 9.4.2p3; three scaling shapes measured against a pre-C6 worktree build, all linear at about 3% more memory; valgrind clean |
| C6 audit | the declaration a specialization is not one of: 8.3.5p10's "any declaration" narrowed by 14.7.1p1, so a declaration of a *template* written below the pattern's definition declares the template and renames no object a specialization has already made - the pattern's spelling frozen where the definition giving it a body is read, and the waiting list of objects a definition leaves unnamed restricted to the ones a program's own declaration made | 262 / 311 -> **264 / 313**, the two new tests pinning the regression C6 shipped and the failing 49 the same 49; pa1-pa18 1777 / 1777; file audit passes; every `.ref` regenerates byte-identically over all 313 fixtures; 30 synthesized programs through the pre-audit binary, `reference-binaries/cppgm++` and g++, 15 of them moved onto the reference by C6 and 2 off it, all 17 now agreeing; three scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build at about 2% more memory; valgrind clean over all 313 fixtures |
| C6 audit review | the declaration a specialization's places are spelled by, and the definition the standard writes them for: 14.7.1p1's spelling frozen at the template's *first* declaration rather than at its definition, which is what the reference does and what 16 of 120 declaration orderings told apart; and 8.3.5p10 asked where 12.8p28's and 12.9p8's definitions make their objects, so a defaulted special member and an inherited constructor spell a place the declaration their parameter list came from left unnamed, however far below them the declaration that named it stands | 266 / 315 -> **272 / 321**, the six new tests being the six shapes these leave and the failing 49 the same 49; pa1-pa18 1777 / 1777; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 321 fixtures; 240 declaration orderings and 55 further shapes through this compiler, the pre-audit binary and `reference-binaries/cppgm++`, all six regressions failing against the pre-audit binary; four scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build within 1% of its memory; valgrind clean over all 321 fixtures |
| C7 | the type a declarator-id ends up under, and what lookup answers before a `<`: 8.3p1's constructor read from the level the id stands in, so 8.4p1's `T (&f(P))[2] {}` is a function definition and 8.3.5p10's places are the nested clause's; 14.2p3 asked of the overload set rather than of the last declaration of a spelling; 7.3.4p2's using-directive answered after every region the name is written inside, which a closed and reopened namespace has only as the spelling its prefixes give; and 14.7.2's explicit instantiation - the target the grammar allows, 14.7.2p2's refusals, p11's members defined where it stands, and `object_root=yes` as the demand 3.2p3 has no use to point at | 272 / 321 -> **283 / 326**, the five new tests being the four forms both oracles refuse and 14.7.2p11's later definition, and the failing 43 the same 43 by name; pa1-pa18 1777 / 1777; file audit passes and the build prints nothing; 48 synthesized programs through this compiler, `reference-binaries/cppgm++` and g++, with every surviving disagreement decided by g++ and the checked-in pa22/pa24 fixtures; a two-unit explicit instantiation byte-identical to the reference and run through `lowir2cy86`; five scaling shapes at n = 32, 128 and 512 unchanged against a `4280a07d` worktree build; valgrind clean over all 326 fixtures |
| C7 audit | the walk a landed rule was not asked at, the target it left a `return` on, and the fact it carried as a terminal: 8.3p1's binding clause read at the level the declarator-id ends up at rather than at the outermost one, so `T (*f(P))(Q)` spells `P`'s places and `T (f)(P)` still takes the clause beside it; and 14.7.2p1's simple-declaration target read for the type it writes and resolved - 14.8.1's explicit argument list, 14.8.2.2's deduction from that type, and the member a class template specialization already made - with 14.7.2p2 refusing a declaration that names none; and 14.7.2p1's definition form given a node of its own, so the PA10 dump spells it `explicit-instantiation-definition` as the reference does rather than hanging `KW_TEMPLATE` on the declaration node.  Beside them 8.5p16's `WrittenInitializer` moved to `sema_declaration.h` | 283 / 326 -> **288 / 331**, the five new tests being five of the six shapes these leave and the failing 43 the same 43 by name; pa1-pa18 1777 -> **1778 / 1778** with the sixth, which is the pa10 dump; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 under `cppgm.tests/course/pa19`; 20 declarator shapes now agreeing with g++ in every one, 21 explicit-instantiation shapes agreeing with `reference-binaries/cppgm++` symbol for symbol and root for root in twenty, and 15 lookup shapes agreeing everywhere; four later-PA `.ref` files that were missing `object_root=yes` now matching; a function returning a function pointer run through `lowir2cy86` to the value g++ builds it to return; seven scaling shapes at n = 32, 128 and 512 and a parenthesis nest measured to the 8000 the parser accepts; the PA10 dump byte-identical to the reference for both explicit instantiation forms and over 58 of the 59 swept shapes; valgrind clean over all 332 fixtures |
| C8 | what a name in an instantiated body reaches, and which of a specialization's bodies an instantiation reads: 14.6.2p3's dependent base left off 3.4.1's chain for the *specialization* as well as for the definition, as a fact of the base-specifier the program wrote once, with 3.4.3 and 3.4.5 walking the same chain unchanged and the link dropped only where the search reaches the class from inside it; and 14.7.1p1's instantiation of the *declarations* of a class's members, each body held on `held_definitions_` for the use that names it - a callee, an `&`, a target type's chosen declaration, the demand a constructor gets where its object is built, the entry every end of a lifetime notes, and the class being complete for 10.3p10's table.  Beside them 8.5.1p2's `InitializerClauses` moved to `sema_declaration.h` and the `Scope` constructor's two out-of-order initializers put back | 288 -> **294 / 331**; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing, which it did not before; the failing 37 are 37 of the same 43 by name; 17 synthesized shapes through this compiler, `reference-binaries/cppgm++` and g++, agreeing symbol for symbol with the reference in all 17 and telling the two disagreements above apart by g++; eight scaling shapes at n = 32, 128 and 512 against a `4ec3d164` worktree build, with 512 specializations of a 16-member class template 0.28 s -> **0.15 s** and 68.0 MB -> 47.5 MB and every other shape unchanged; valgrind clean over all 331 fixtures |
