# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 stands at **310 / 340** (54 spec + 235 general + 21 course), from a
turn-start baseline of 304 / 334, with pa1-pa18 at **1778 / 1778** and the file
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
  there is reached by the same walk.  `deferred_conversion<incomplete>` is what
  the clause is for: the class has a layout an object needs, and only the body
  of the conversion function nothing calls names `sizeof(T)`.
- **Two demands have no expression behind them, and each is asked over a walk.**
  10.3p10's virtual member is named by a *table* and not by any use a body
  writes, and a table is a fact of every class this instantiation made - the
  specialization and each class the pattern nests inside its body - so
  `require_table_definitions` asks over the region each of them opened.
  14.7.2's explicit instantiation is the other: it is the one declaration 3.2p3
  has no use to point at, so it asks for the held body exactly as a call does.
  A body nobody grants is a `declare function` and no definition, which is a
  program that does not link over a suite that compares LowIR and never links,
  so a rule reaching one demand and not the other is not a narrower rule but a
  broken one.
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
- **A type-id's spelling is read as a declarator, not as a word list.**  It is
  `sema_type_id.cpp`'s and nothing in it knows what a template is: the only
  reason a type-id arrives as text at all is that 14.2 writes an argument list
  inside a name, and what the layer answers is what 8.1p1 says.
  `split_type_id` keeps a name whole, and
  `type_id_words`/`abstract_declarator_words`/`suffix_words` read 8.1p1's
  type-specifier-seq and 8.3p1's abstract-declarator from what is left.  What
  belongs to the name after a template-argument-list or a decltype-specifier is
  what a `::` joins to it and nothing else, because 7.1.6.1p1's cv-qualifier
  may be written after a template-id and PA10 spells the two with no space
  between.  And the operand such a spelled decltype-specifier can answer for is
  5.1.1p8's id-expression, which 3.4 looks up here as it looks up any other
  name: an expression written there was never read as one.
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
- **A declarator's own places are a region, and it is gone with the
  declarator.**  3.3.7p1 makes a place's potential scope begin at its
  declarator-id and end with the *function declarator*, so a later parameter's
  own type-id and 8.3.5p2's trailing-return-type name what the clause has
  already declared - and the clause a trailing-return-type follows is read for
  those names before the type is read, though the type is still built from the
  last suffix inwards, so the clause is read once and 8.3p1's order is
  untouched.  The region is opened where a name could reach it and nowhere
  else: the last place of a clause nothing follows has nothing left to name it.
  Nothing it binds outlives the declarator - 8.3.5p10 leaves a parameter's name
  out of the function's type and 8.4.1p1's definition declares its own objects
  again - so 3.3.2p6 keeps a class an elaborated-type-specifier in the clause
  first declares out of it, which is the same question 14.1p1 already asked of
  a template head's region and is answered in the same place.  5.1.1p3 stands
  over the same span: `this` is a member function's from its cv-qualifier-seq
  to the end of its declarator, and the object it names is built from the class
  the declarator-id reaches and the qualifiers written beside the clause,
  because the declaration 9.3.1p3 would take it from has not been made yet.
  *Which* declarators those are is the decl-specifier-seq beside them and no part
  of the declarator: 9.4p1's static member is called on no object, 11.3p1's
  friend declares into the region around the class, and 7.1.3's typedef declares
  no function - so the caller answers, and every other declarator is read with
  the reading around it put aside and no object at all.
- **A decltype-specifier over a dependent expression is a type, and the
  instantiation reads the expression.**  14.6.2.2p1 makes an expression
  type-dependent exactly where a name it writes can reach a type an argument
  list has yet to say, and the regions that carry such a type are the
  template-parameter ones standing over the reading - so the same walk tells a
  definition apart from every reading of it that has the arguments.  While it
  is one, 7.1.6.2p4 has no answer and the specifier stands for a type of its
  own, kept beside the specifier and the region it was written in; 14.7.1p1
  answers it by reading that expression *again*, against those regions rebuilt
  with every name they bind standing for what the substitution made of it.
  Nothing is substituted into the expression, which is the tier's rule
  everywhere else, and what comes out is substituted in its turn - a
  specialization over a dependent argument list is one of these readings.
  **Which two of them name one type is 14.4p1's, not the reading's**: the
  specifier as it was written and the declarations the names it writes reach,
  which is the shape of 14.1p1's and 3.3.7p1's regions standing over it with what
  each binds, and the region outside them.  What a head *spelled* is no part of
  either - 14.1p2 lets each declaration of one template name its parameters as it
  likes and 14.4p1 makes a parameter equivalent to the one at the same position,
  so a head's names stand for the places they were declared in, in the key and in
  the spelling the key carries alike; a place 3.3.7p1's clause bound keeps its
  name, because that name is what the expression writes.  Two declarations of one
  template build that key alike - 14.5.6.1p5's signature has already stood one
  head's parameters in the other's places, and an out-of-class member definition
  is read against the class its declaration was - so one function written twice
  has one return type rather than a declaration and a definition that never meet.
  And the region the second reading rebuilds stops where the specifier's own
  reading did: 3.3.7p1 begins a place's potential scope at its own
  declarator-id, so a place written *after* the specifier is one it could not
  name, and rebuilding it would ask the substitution for the type it is in the
  middle of computing.
- **7.1.6.2p4 has two arms and both name an entity.**  An unparenthesized
  id-expression and an unparenthesized class member access each stand for the
  type their entity was *declared* with - not for what the expression is worth,
  so neither is qualified by the object holding it and neither gains a reference,
  while `decltype((c.v))` is 5.2.5p4's lvalue and does.  Every other expression
  is the value category's, which is the same question the expression layer
  already answers.
- **What a definition is being read *for* belongs to that reading.**  14.7.1p1's
  specialization is taken before the definition's own specifiers and declarator
  are read, because reading either can name another specialization: a dependent
  qualified return type instantiates the class it is a member of, and every
  member function that class defines in its body is read there and then.
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
  template specialization already made where the prefix names one.  What says
  the prefix names one is asked of every class region that prefix reaches and
  not of the innermost alone, because a class the pattern nests inside its body
  is made by the same instantiation and its members are as much the
  specialization's as the ones written beside it.  9.3.1p3's
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

30 of 340 fail - the same 30 by name C9 left, the six tests C9's audit added
all passing.  Grouped by the compiler behaviour that owns them:

| n | group | what is missing |
| --- | --- | --- |
| 7 | a name written in a template that only the argument list settles | `typename Iter::owner::later` deferred to a typedef written later, a nested member's base alias, a member `operator>>` reached lazily, an elaborated argument named in an enclosing scope, 13.5p6's builtin beating a class operator, `__alignof` over an instantiation, 12.9p1 through an alias template |
| 5 | a template declared or defined with a qualified name | `template<class T> class v::C {...}`, `template<class T> int n::f(T) {...}`, `types::item<int>(value)` as a parameter-declaration, `fusion::remove<X>` as a dependent qualified return, 14.8.1p2's partial explicit argument list |
| 8 | 12.1/12.8/8.5p7: the definitions a class owes and what an initialization writes | the reference emits an explicitly-defaulted copy constructor this unit elides everywhere and elides an out-of-class defaulted one this unit calls; a reference member left memberwise; 8.5p7's zero written before a constructor call the reference writes alone; the complete-object entry of a base constructor (see the probe below); `this` recomputed |
| 6 | what an instantiation writes that nothing runs | two spurious empty `[role=init]` entries - one from a specialization completed while its argument was incomplete, one over a static data member of POD class type; a global emitted for a *dependent* specialization (`@v_T_T_`); a branch left unfolded; `0` where a pointer parameter wants `nullptr`; a temporary named for an argument where a return names it |
| 3 | 14.1p10 and 3.4.2p2's remainder | a later redeclaration's default template argument, and two decltype-through-an-inline-namespace lookups |
| 2 | an expression the reading types where a parameter stands in the way | 5.3.3p1's `sizeof(test((From)0))` written as an enumerator of a class template, where `(From)0` is read as a type-id rather than a cast; and a call of a member whose spelling a *namespace-scope class template* also declares, which 3.4.1 answers with the member |

## Active Checkpoint

**C10 - a template declared or defined with a qualified name**: five tests, one
owner, and the one group left whose whole failure is that the declaration is
never made.  3.4.1p8 and 9.4.2p1 say the rest of a declarator whose
declarator-id is qualified is read in the region that name reaches, and 14.1p2
says the head standing over it spelled that template's places itself - so
`template<class T> class v::C {...}` and `template<class T> int n::f(T) {...}`
have to declare into `v` and `n` while the names their heads bound stand over
the reading, and today `record_template` refuses a qualified class-head-name
outright and `declare_function` reaches the namespace with the head's names no
longer in force (`no declaration of T is in scope`).  Beside them 14.8.1p2's
partial explicit argument list, which deduces the parameters the list stopped
short of instead of refusing.

- **owner**: `sema_template.cpp` for the head and the pattern, `sema_function.cpp`
  for the declaration a qualified declarator-id makes.  `record_template` is
  where a template-declaration's target is decided; `resolve_prefix` is what the
  head is read against.
- **data flow**: the class-head-name or declarator-id is split -> the prefix is
  resolved from the region the declaration stands in -> the pattern is recorded
  on the declaration that region already has, or a new one is made there ->
  the head's region is chained over that region for as long as the declarator
  and the body are read, and is gone afterwards, exactly as C5's
  `open_member_parameters` does for 14.5.1.3p1's out-of-class member definition.
- **expected complexity**: one prefix resolution per qualified template
  declaration and no change to any other declaration; the head region is the one
  the ordinary path already opens.
- **known obstacle**: 14.5.2's member template writes a second head, and
  `record_template` refuses two clauses today - a qualified declarator-id of a
  member of a class template is the same shape, so the two have to be told apart
  by what the prefix reaches rather than by how many clauses were written.
- **validation**: the five fixtures of the group, then a qualified definition
  whose head spells the places in another order, then the pa19 report and
  pa1-pa18.

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
n = 12, 16, 18 and 20 in 23 lines of source - unchanged since it was measured.
`reference-binaries/cppgm++` is 0.19 s, 2.85 s, 12.28 s and **46.31 s** on the
same inputs, so this is the milestone's shape rather than the tier's; g++ does
n = 20 in 0.06 s because it never materialises the spelling. Fixing it means not
storing a specialization's written-out name at all.

C8 measured what 14.7.1p1's held definitions are worth: 512 specializations of
a 16-member class template with one member called is 8192 body readings before
and 512 after - 0.28 s and 68.0 MB against 0.15 s and 47.5 MB - and the probe
itself is free, because `require_definition` returns on an empty map.  Its
audit measured the table walk, which costs the *members* of a specialization
and not its uses: 512 sibling member classes with a virtual member each moved
0.09 -> 0.10 s and 24.3 -> 26.2 MB.  The one shape that moved back is **a class
nest n deep, each level virtual**, 0.40 s at n = 512 - the pre-C8 binary's time,
quadratic in the nesting *depth* and not the count (0.41 s and 1.51 s at 512 and
1024) because a body granted at depth n looks its names up through an n-deep
chain, which is 3.4.1p1's own quadratic; the same nest with no virtual member is
0.06 s, and `reference-binaries/cppgm++` is 0.03 s because it emits a class's
table only where an object of it is built.  A class nest 512 deep is not a
translation unit, so the narrower rule is recorded in `audit.md` rather than
landed.

C9's own risk is two: one region and one declaration per place a declarator
binds, and one region chain rebuilt and one expression read again per
decltype-specifier an instantiation reaches.  Measured against a worktree build
of `e067d2e9`, each shape timed twice, `-O0` (`-` where the pre-C9 binary
refuses the shape, which is the checkpoint):

| shape | n = 32 | n = 128 | n = 512 | before |
| --- | --- | --- | --- | --- |
| n function definitions of one parameter | 0.00 s | 0.01 s | 0.02 s | same |
| n declarations of two named parameters | 0.00 s | 0.01 s | 0.01 s | same |
| n definitions with a trailing-return-type over their own places | 0.01 s | 0.01 s | 0.03 s | - |
| n member functions with a trailing-return-type in one class | 0.00 s | 0.01 s | 0.02 s | same |
| n function templates with a dependent decltype return, each called | 0.01 s | 0.02 s | 0.07 s | - |
| one such template called over n classes | 0.01 s | 0.02 s | 0.09 s | - |
| n specializations of one class template, each with a member | 0.01 s | 0.03 s | 0.10 s | same |

Every one of them is linear and stays linear: the three shapes the checkpoint
opens are 0.03, 0.07 and 0.09 s at n = 512 and 0.13, 0.29 and 0.39 s at
n = 2048, and one such specialization *named* 2048 times is 0.05 s and 18.7 MB
- which is what an ordinary function called 2048 times costs in this binary and
in the pre-C9 one alike, so naming a specialization again reads nothing again.  What the region costs is memory rather than
time: 2048 declarations of two named parameters are 0.04 s before and after and
15.2 MB -> 18.2 MB, about 1.5 KB for each declarator that binds a place - one
`Scope` and one declaration, on the same terms as the region a function
*definition* already opens for its own.  The narrower rule - open it only where
the rest of the declarator can hold an expression, which is the only way a name
can reach a place - is left unlanded, because telling that apart costs a walk of
the syntax the reading is about to do anyway.

C9's audit changed what a decltype-specifier is keyed by, and the key is built
once per reading and is as long as the places and parameters standing over it -
so it costs the *declarator*.  Against a `1b135271` worktree build at n = 32, 128
and 512, n function templates with a decltype return each called (0.06 s), one
called over n classes (0.55 s, which is 13.3p1's own quadratic over the n
`operator+` declarations the shape writes), n declarations of two named
parameters (0.01 s), n specializations each with a member (0.09 s), n
out-of-class member definitions (0.02 s) and n members with a decltype return
(0.02 s) are all where the checkpoint left them; the one that moves is **n
declarations of one such template, 0.04 s -> 0.02 s**, because
`equivalent_template` now matches on a type it already has instead of
substituting each pair again.  The shape the audit opens is **one clause of n
places, each of whose type is a decltype over the first**: 0.01, 0.05 and 0.19 s
and 13.0, 30.7 and 99.9 MB at n = 128, 256 and 512, quadratic in the places
because 14.7.1p1's second reading builds one region per specifier and each holds
the places above it.  The pre-audit binary does not measure it - it **segfaults
at n = 8** - and a 512-place clause is not a translation unit, so the narrower
rule of one rebuild per region and bindings is recorded in `audit.md`.

The lookup C7 added - one probe per prefix in force, and only for a name every
open scope and every region around it has already missed - is at the pre-C7
build's times to the hundredth: n-deep namespaces named from the innermost,
a reopened namespace of n names, and n using-directives over n names are
0.14 s, 0.01 s and 0.07 s at n = 512, the first of them 3.4.1p1's own quadratic.
8.3.5p10's record is unchanged: n unnamed places named from below, n
declarations of one function template, n out-of-class defaulted copy
constructors and n inherited constructors are 0.02 s, 0.01 s, 0.13 s and 0.08 s
at n = 512.  8.3p1's inward walk is asked once per *definition* and the shape
that asks it at every level - n parentheses around a pointer-returning
definition - is 0.03, 0.11, 0.42 and 1.72 s at n = 1000, 2000, 4000 and 8000,
inside the quadratic the AST walk above it already is.

Valgrind is clean over all 334 fixtures.  A 20000-deep parenthesized
expression is refused by the parser at about 1000, so the definition-time walk's
recursion is bounded by the same limit the expression layer already is; a
declarator's own parenthesis nest is refused between 8000 and 16000, which is
what bounds every walk that reads one.

`dev/src/sema_analyzer.h` sits at 2395 lines against the audit's 2400 and
`dev/src/sema_template.cpp` at 2653 against 3000: C9's audit split 8.1p1's
reading of a type-id out of a spelling into `sema_type_id.cpp` when the latter
reached **3029**, and kept the header where it was by making 14.4p1's key a free
function of the file that asks it and 5.1.1p3's question a declaration beside
`DeclSpecifiers` - as C9 moved 3.7.2p2's `ThreadLifetime`, 8.5p1's
`ObjectPlacement` and 12.2p1's `TemporaryRequest` to `sema_declaration.h`, as C8
moved 8.5.1p2's `InitializerClauses`, and as C5 moved 9.6p2's bit-field storage
unit and 3.4.2p2's associated regions.  Both files are near their limits, so the
next thing either grows by owes a split of its own.

Two disagreements C9 leaves standing, both in what no fixture compares.  The
ABI writes a function template's decltype return type as the *expression*
- `_Z3addIiEDTplfp_fp0_ET_S1_` in `reference-binaries/cppgm++` and in g++ alike
- where this compiler writes the type the substitution made, `T_`; the encoding
needs 5's whole expression grammar in `abi_mangle.cpp`, the two oracles disagree
with each other on two of the three shapes swept (`Dt` against `DT`, and how a
call on a temporary of dependent class type is written), and `object=` is
stripped before the comparison, so it is recorded rather than half-landed.  It
costs one thing that is not cosmetic: two templates of one name and one
parameter list whose return types differ only in the decltype would be named
alike - a shape all three compilers refuse the moment both are called, because
13.3 has nothing to order them by.  And the operand a *spelled*
decltype-specifier can answer for is 5.1.1p8's id-expression alone, so
`Hold<decltype(o.v)>` is refused here and accepted by both oracles: 14.2's
argument list reaches this layer as text, and a member access written there is
an expression the parser would have to have kept - which is the one arm of
7.1.6.2p4 a spelling cannot reach, the same access being answered wherever the
specifier arrives as a tree.

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
| C8 audit | the two demands a held definition has no expression behind: 10.3p10's table asked of every class the instantiation made rather than of the specialization's own declarations, so a virtual member of a class nested in the pattern is defined and not just declared; 14.7.2p1's explicit instantiation asking the instantiation for the body it put aside, which is the one declaration 3.2p3 has no use to point at; and that same clause's target read from every class region its prefix reaches, so a member of a nested class is named as the specialization's | 294 / 331 -> **297 / 334**, the three new tests being the three shapes these leave and the failing 37 the same 37 by name; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; thirty use shapes swept for a symbol some entry names that no definition writes, all clean here and matching the reference's definition count where `43aa2aa0` leaves two unresolved; eight 14.6.2p3 shapes picking the reference's callee in every one with g++ compiling all eight; a rooted explicit instantiation run through `lowir2cy86` to the 42 g++ builds it to return; eight scaling shapes against a `43aa2aa0` worktree build, unchanged but for the class nest 512 deep that returns to the pre-C8 0.40 s; valgrind clean over all 334 fixtures |
| C9 | the region a declarator's own places stand in, and the type an argument list reads: 3.3.7p1's function prototype scope, each place declared as its declarator-id is read so that a later parameter's type-id and 8.3.5p2's trailing-return-type name it, with the clause a trailing-return-type follows read for those names before the type is and the type still built from the last suffix inwards; 3.3.2p6 keeping a class an elaborated-type-specifier in the clause first declares out of that region; 5.1.1p3's `this` over a member declarator's trailing-return-type; 14.6.2.2p1's type-dependent decltype-specifier made a type of its own, kept beside the specifier and the region, and answered by 14.7.1p1 reading the same expression again against those regions rebuilt over the arguments; 14.2's template-id carrying 7.1.6.1p1's cv-qualifier read as two words, and 5.1.1p8's id-expression read where a template argument spells a decltype-specifier; and the specialization a definition is read for taken before its own specifiers, so a dependent qualified return type that instantiates a class no longer gives every member that class defines in its body the type of the reading around it | 297 -> **304 / 334**, the whole decltype group and the failing 30 all named by C8's map; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing; 26 synthesized shapes through this compiler, `reference-binaries/cppgm++` and g++, with the five the reference alone refuses decided by g++ and by the checked-in fixtures, and the symbols byte-identical to g++ in every shape but the ABI's decltype encoding; a two-unit call of one such specialization sharing one definition; seven scaling shapes at n = 32, 128 and 512 against an `e067d2e9` worktree build, three of them new and all linear to n = 2048; valgrind clean over all 334 fixtures |
| C9 audit | the type a decltype-specifier stands for made a fact of the expression rather than of the reading: 14.4p1's key - the specifier as written, the shape of 14.1p1's and 3.3.7p1's regions standing over it, and the region outside them - so a declaration and the definition below it declare one template and 14.5.1.3p1's out-of-class member definition writes one return type; 3.3.7p1 bounding the region 14.7.1p1's second reading rebuilds to the declarations that stood when the specifier was read; 5.1.1p3's `this` given to the declarators a decl-specifier-seq leaves declaring a member function and to no others; and 7.1.6.2p4's class member access arm, which names the entity's declared type as its id-expression arm does.  Beside them 8.1p1's reading of a type-id out of a spelling split into `sema_type_id.cpp` | 304 / 334 -> **310 / 340**, the six new tests being the six shapes these leave and the failing 30 the same 30 by name; pa1-pa18 1778 / 1778; the file audit passes, which it did not - `sema_template.cpp` reached 3029 lines against the limit of 3000 - and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; 52 synthesized shapes through this compiler, the `1b135271` pre-audit and `e067d2e9` pre-C9 builds and `reference-binaries/cppgm++` with g++ beside them, 42 compared as emitted LowIR and identical to the reference in 41; a unit writing all three forms run through `lowir2cy86` to the 42 g++ builds it to return; two units defining one such specialization order-free; seven scaling shapes at n = 32, 128 and 512 against a `1b135271` worktree build, unchanged but for n declarations of one template 0.04 s -> 0.02 s; valgrind clean over all 340 fixtures |
