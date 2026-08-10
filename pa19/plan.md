# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 **passes**: **388 / 388** (65 spec + 254 general + 69 course), from a
turn-start baseline of 377 / 381, with pa1-pa18 at **1778 / 1778** and the file audit
passing with the five header-weight warnings it inherited.  The build itself
prints none.

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
- **Which copy is the bytes, and which definition the unit writes.**  12.8p12
  is asked of a class that has every definition it is going to have, so an
  8.4.2p2 `= default` written *outside* the class settles it again: the class is
  complete, the walk is the closing brace's own, and 12.1p5's triviality,
  12.8p11's deletion, 15.4p14's specification and the type's own copy facts are
  written once more rather than patched.  12.8p15's memberwise transfer then
  carries a subobject by its bytes only where it *is* bytes - 8.3.2p1's
  reference member is not an object, so it ends the leading run and takes
  8.5.3's initialization of a reference instead - and 9p6's empty base names
  only the subobject it builds, because 4.10p3's base conversion of the source
  observes nothing.  Which definitions the object file then holds is 3.2p4 and
  14.7.1p1 read of the *use*: a definition the program wrote outside its class
  is this unit's whether or not the call 12.8p12 left out stands, a use inside a
  body an instantiation made is what made that definition and keeps it, and the
  ABI's two entry points are both owed where the use that made the definition
  was one the program wrote out - a base subobject written inside another
  instantiation asks for the entry it names alone.  A member the *standard*
  declared is no part of what an instantiation made, so none of this reaches it.
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
- **A head stands where the declaration it parameterises belongs.**  3.4.1p8
  reads the rest of a qualified declarator-id in the region that name reaches
  and 14.1p1 encloses the declaration in its head's own region, so the two
  compose one way: the head is opened *inside* that region for as long as the
  declarator and the body are read (`StandingIn`), and the names they write are
  looked up in the head first and in that region after it.  So
  `template<class T> int n::f(T)` finds `T` before a typedef-name `n` declares
  of that spelling, `declaring_region` still steps out to `n` for the
  declaration, and `SemaContext::template_head` is what says a declaration
  whose target region is a namespace declares a template at all.  A class-head
  name is the same rule read by `record_template`: the pattern is recorded on
  the declaration that region already has, and 9.4.2p1 refuses one it does not.
  3.4.3p1's own rule is beneath both - each component of a
  nested-name-specifier is looked up in the region the one before it reached,
  which is what finds `v::S<T>::f`'s owner, with the empty first component of a
  name written `::v::...` naming the global namespace and `region_of` answering
  for a typedef-name that has no region of its own.
- **14.5.2's member template declares a member, and its object parameter has
  four readers.**  Which region a declaration belongs to is `declaring_region`'s
  answer and not the region it is read in, so 9.3.1p3 gives a member template
  the object parameter every other non-static member has - and 14.8.2.1p1's P/A
  pairs begin after it, because no argument is written for that place and its
  type is the class's, which a class template's own argument list settled before
  this head declared anything.  The other three are questions that used to be
  answered from a function type and cannot be, because 14.1p2 gives each head
  parameters of its own: 9.4.1p2's "which declaration does this out-of-class
  definition redeclare" is 14.5.6.1p5's signature and not 13.1's index of the
  list as written; what a *specialization* is called on is the template's own
  declaration's fact, because the region 14.7.1p1 reads a pattern under binds
  arguments rather than parameters and neither question can be asked there at
  all; and 14.5.6.2p2's ordering is over the places the two declarators wrote -
  `ordering_parameters` gives the one non-static member a reference where the
  other declaration wrote its own first operand, and drops it against 13.3.1p4's
  static member of the same class, whose implicit object parameter matches any
  object and orders nothing.
- **14.8.1p2's written arguments belong to the name, not to the use.**  A
  template-id may write a leading part of the list and leave the rest to be
  deduced, so what it names is neither the template nor a specialization: it is
  a declaration of its own (`SemaEntity::partial_of`), made once per template
  and written list, carrying that list in `template_arguments`.  A call,
  13.4p1's target type and 14.7.2p1's explicit instantiation each start their
  deduction from it - the written arguments are substituted into the type
  *before* the pairs are read, which is what leaves a parameter they made
  non-dependent to 13.3's conversion rather than to a deduction that would
  refuse the argument.  **And the clause's other arm is 14.1p9's default**: a
  trailing argument may be omitted where it can be deduced *or* obtained from a
  default, so a place a deduction leaves empty takes the type-id its head wrote,
  kept beside the parameter's own declaration because 14.1p2 lets each
  declaration spell its places as it likes, read in the region the head declared
  them in because 14.1p9 lets a default name the places before it, and
  substituted with what the deduction has settled so far.
- **14p1's template-declaration declares no object either.**  A declarator
  under a head that is not a function names storage an argument list is what
  makes, so it is a pattern exactly as a class template's class-specifier is -
  and what tells it from 14.5.1.3p1's static data member is the region the
  declaration *belongs* to and not the spelling that reached it, because a
  qualified declarator-id reaching a namespace declares the same pattern an
  unqualified one there does.  A class is what `record_template` already took for
  9.4.2p2's member, whose own definition still lays out its storage.
- **What a definition an instantiation made owes the program before it runs is
  what it *runs*.**  14.7.1p6 leaves the initialization of a static data member
  to the use that names the member, so an initialization that writes nothing
  leaves this unit owing no startup body at all - where a definition the program
  itself wrote owes 3.6.2p2's entry however little it does.  The startup body is
  therefore opened as it always was and kept only where something owes it, which
  costs one mark of where the body stands per initialization and no walk of it.
  3.6.3p1's end is the other thing that owes it: the end of the lifetime of an
  object with static storage duration is *registered* where the program starts,
  so a unit that owes the program that end owes the entry it stands in however
  little the initialization beside it came to.
- **12.2p1's temporary a reference binds is an object, and the place that asked
  names it.**  3.10p1 makes a prvalue a value rather than an object, so a
  reference bound to one binds a temporary the function gives storage to -
  `bound_address` where a declaration or a mem-initializer wrote the binding and
  `converted` where a conversion did.  The name is 12.2p1's: `retref` for a
  return's own, `refarg` for a call's argument, and `tmpref` for the temporary a
  declaration wrote, an 8.5.1 clause of one included.  Taking the address of the
  value instead is what the storage replaces - there is no object there to have
  one - and the *image* is where that has no answer at all: a reference's storage
  holds an address, so a value is no image for one and the binding a
  namespace-scope reference wrote is an action the program runs.
- **A branch that tests a literal is the jump one of its edges is, and what says
  so is the expression.**  `folded_edge` asks the *node* - a literal, an
  enumerator, a constant a class declared, 8.5p7's zero - and not the operand a
  lowering of it would leave standing, because `int(1)`, `(int *)0` and
  `((void)0, 1)` are each an expression *over* a literal and are where 5.19's own
  reading of a constant expression begins.  It emits nothing, which is what lets
  the answer be had before a block is reserved: an operand of `&&` or `||` that
  folds leaves the operator standing for its other operand, so the block the
  short-circuit would read that operand in is not reserved at all - and the
  blocks of a body are numbered by the order they were asked for, so reserving
  one nothing reaches renames every block after it.  The operator itself stands
  for no edge: it writes the jump where its operand stopped it, and where its
  *value* is named the same rule leaves it worth its right operand's truth, read
  in the block the left one was, with no slot for two paths that are one.
- **An initialization that runs nothing names nothing.**  12.1p5's constructor
  of a subobject that holds nothing has nothing to do, so the walk from `this`
  to that subobject is no part of the program either; 12.8p15's transfer is the
  exception, because a constructor *given an object* carries a value whatever
  the bytes come to.  8.5p7's zero is the same question read the other way at
  6.6.3p2: the prvalue a return wrote and the object the function hands back are
  one object, so what initializes the result object is the constructor 12.8p31's
  elision left standing - which is the reference's answer and the checked-in
  fixtures', against 8.5p7's own text, and is recorded as that.
  And 8.5p7's zero over a *scalar* is a value of the object's own type, so
  4.10p1 makes a value-initialized pointer the null pointer value LowIR spells
  `nullptr` rather than the integer a null pointer constant is written as - in a
  body and in the image alike, where a *subobject* of pointer type says the same
  value by being storage (`null_pointer_item`), which is the item an element of
  an array of pointers already took.
- **A specialization is declared where it is named and defined where a complete
  type is required, and naming it is never that.**  14.7.1p1 instantiates a
  class template specialization where it is used in a context requiring a
  *completely-defined* type **and nowhere else**, so writing the name is one
  side of the rule and the demand is the other, and neither is asked by where
  the walk happens to stand.  7.1.3p1's typedef-name, 7.1.3p2's
  alias-declaration, 8.3.5p6's parameter and return type of a function nobody is
  defining, a trailing-return-type, a default argument, a cast's type-id, a
  `sizeof` or a `new` over a pointer to one, a condition, a friend declaration
  and a template argument are each a name and no demand - a list the grammar
  keeps adding to, which is why `asked_specialization` only *marks*, however
  many times the name is written.  `require_complete_type` is the one demand,
  read at 3.9p5's own list: 3.4.3p1's lookup through a prefix, 3.4.5p1's member
  access, 10p1's base, 5.3.3p1's `sizeof` and 5.3.6p3's `alignof`, 5.3.4p1's
  new-expression, 3.9p6's object being built, **the declarator that *defines* an
  object** - which is the question 10.4p2 is already asked under and not every
  declarator, because 3.1p2's `extern` and 9.4.2p2's static data member
  declaration define none - **8.3.5p6's other arm**, the return type and
  parameter objects of a function *definition*, and **3.9p5 over an
  expression**, because 3.4.2p2's associated classes, 13.3.1.2p3's member
  candidates, 13.3.1.1.2's surrogate calls, 13.3.1.4's conversion functions and
  12.4p11's destructor are all read off an expression's class.  That last one is
  asked where every expression the layer reads leaves, so it is one call and two
  tests for the pointer, reference and scalar it usually finds; the rest cost
  one integer test, and that integer is zero in a unit that named no
  specialization.  14.6p8's reading asks for none of them - a demand answered
  there would read the pattern in the checking dialect and leave a class with
  none of 12.1's members - which is the same answer `instantiate_class` gives a
  template-id written in a definition.  Which point a specialization is then
  completed at is what settles a member the class writes *below* the typedef
  that named it and an argument a later declaration completes.
- **A class the current instantiation declares is dependent too.**
  14.6.2.1p9: a nested class or enumeration of the current instantiation is a
  dependent type however plainly its own declaration is written, because what
  its members come to is what the enclosing argument list says - so a
  template-id written over one names no class while the pattern is read, a
  base-specifier that writes one is 14.6.2p3's dependent base, and a bound
  computed from what such a type is *worth* is 14.6p8's rather than 8.3.4p1's.
  The fact is settled where the type is made, from the region the declaration
  belongs to, so a class nested two deep is reached through a level that was
  asked the same question.
- **A base-clause is read where the class-head-name reached.**  3.4.1p8 and
  3.3.2p5: a member class defined outside its class writes its base-specifiers
  in the region that name reaches, and its own name is in that region from its
  class-head on - so a member type of the enclosing class and the class being
  defined are both found there, and a template-argument-list written in the
  base-specifier is looked up the same way.
- **A class-key before a name is 3.4.4p2's lookup and 3.3.2p6's declaration.**
  14.2 leaves a template argument as text, so `wrapper<struct S>` reaches
  `sema_type_id.cpp` as a type-specifier-seq of two words: the key is split off
  and the name looked up as a type alone, which finds the class an object of
  that spelling hides, and a class-key that reaches no class declares one in
  the smallest namespace or block scope around the declaration - not in the
  class, the parameter-clause or 14.1p1's head it happens to stand inside.
  7.2p3 leaves `enum` no such arm.
- **3.4.3.1p2 has two arms and both name a constructor.**  In a
  using-declaration that is a member-declaration, a name is the constructors of
  the class its nested-name-specifier nominates where the lookup finds that
  class's injected-class-name *or* where the name is the identifier - the
  template-name of the simple-template-id - the last component of that
  specifier was written with.  So `using Alias::Alias` names a base's
  constructors through a typedef-name, and the name is not looked up in the
  base at all.  14.6.2p3 leaves the reading of the pattern nothing to answer:
  what the specialization derives from is what says whose constructors these
  are.
- **8.2p7 is answered by 3.4 and not by the grammar.**  `T (X)` in a
  parameter-declaration-clause is a parameter of function type where `X` names
  a type and redundant parentheses around the declarator-id where it does not,
  and the parse - which matches the reference - writes the parameter-clause
  both spellings share.  `parenthesized_place` reads that clause back as the
  declarator it is, keeping whatever the parentheses wrote after the id, so
  `T (X[10])` declares an array; nothing is reparsed and no node is rewritten.
- **A plain template-name is a type-specifier at one place only.**  14.2 makes
  a template-name say which class once an argument list is written after it, so
  the one spelling that names a type on its own is 14.6.1p1's
  injected-class-name - and 9p2 makes that a *member* of the class, so it
  stands where a member of that class does: in the class's own body, in a
  definition written on its declarator-id, and in a class derived from it.
  `DeclaredNames::injected` is the question and the parse asks it where the
  answer decides 6.8p1's ambiguity, so `close_impl(which);` written where no
  class of that name encloses it is the call 3.4.1 finds and not a declaration
  of `which`.  10.2p2's chain is what a base reaches through, and the answer is
  kept per prefix and spelling because a class derived from a chain n deep asks
  the question its own base already answered - a yes forever, since a
  base-clause is read once and nothing takes one away, and a no for as long as
  the chain it was answered over stands.
- **13.3.1.2p1's other operand is the enumeration.**  An operator expression
  with an operand of class *or enumeration* type is a call of an operator
  function, so 13.6's built-in candidates are ranked beside the declarations
  either way - and an enumeration reaches one without asking anything of a
  conversion function, because 13.6 writes the integral operators over 4.5p3's
  promoted operands.  `better_builtin` is where the two are ranked, so `E | F`
  is `operator|(int, int)` and an `operator|` declared over a class an
  enumerator converts to is a candidate that loses to it; 5p9's usual
  arithmetic conversions are what bring the two written operands to the one
  type a candidate is written over, and two operands of one enumeration type
  are already that type, which is 13.6p15's own candidate.
- **5.3.3p1's operand is an expression wherever the grammar allows one**, and
  5.19 asks it for its size rather than for its value - so a `sizeof` written
  in a constant expression reads the unevaluated operand the expression layer
  already reads, however much a parenthesized name inside it looks like a
  type-id.  14.6p8 is the one reading that cannot: how large the operand is, an
  argument list is what says, so the pattern's reading looks the names up and
  stands one value in its place.  5.3.6p1 gives `alignof` the type-id alone.
- **14.1p10's defaults are every declaration's, merged.**  A head that adds a
  default to a place an earlier one left empty is read for that alone, whether
  or not it is the one that writes the body - so the merge and 14.1p2's
  parameter-count check are asked of each declaration and only the *names* are
  the definition's.  What a merged default names the places before it by is the
  head that wrote it, kept beside the type-id (`TemplateInfo::Default`), so the
  region 14.1p9's default is read in binds the arguments settled so far under
  that head's own spellings and not under the ones the merge left standing.

## Current Failure Map

**0 of 388 fail.**  C14 took the last four and its sweeps left seven fixtures
behind.  What remains is what no fixture reaches, grouped by owner:

| n | group | what is missing |
| --- | --- | --- |
| 3 | 14.5.2's second clause | `s.f<int>(2)` - a template-id after a member access - which **our parser refuses outright**, `template<class T> template<class U> int S<T>::f(U)`'s two clauses, and 12.3.2p1's conversion function template.  All three are accepted by both oracles and reached by no fixture; C10's sweep found them |
| 1 | 9.2p2 at the *parse* | a plain class-name a member function declared *below* the use hides: `close_impl(which);` where `close_impl` is an ordinary class at namespace scope reads as a declaration here and as the member's call in both oracles, because the parse fills its name table in source order and no member body is held for the closing brace.  14.6.1p1's arm of the same question is landed - a plain *template*-name is now a type only where the injected-class-name stands |
| 1 | 14.6p8 over a non-dependent base | a **non-dependent** specialization named as a base class in a template's own definition is not completed by the reading, so `template<class K> struct P { struct N : adaptor<int> {}; };` is refused where both oracles accept it.  Completing it there means suspending the reading - the pattern's members would otherwise be read in the checking dialect - and a base written over the current instantiation takes 14.6.2p3's arm instead |
| 2 | the definition a use *nothing calls* makes | C13's pair, below |
| 1 | 12.1's two entry points | the C13 audit's, recorded in `audit.md` under Open Gaps |

Four disagreements with `reference-binaries/cppgm++` that C14's own sweeps
turned up, each decided against it by g++ and by the standard, and each reached
by no fixture:

- **13.6p8's unary `-` and `~` over an enumeration.** `-e` where a class
  `operator-(W)` and `W(int)` are in scope is the built-in on the promoted
  operand here and in g++ - which runs the program to the built-in's value -
  and the class operator's call in the reference.  A standard conversion
  sequence beats a user-defined one, so the fixture that would pin it cannot be
  written from the reference's output.
- **5.3.3p1 over a bit-field**, which shall not be measured: refused here and
  in g++, accepted by the reference.
- **6.4.2p2's case label**, converted to the promoted type of the condition:
  `case sizeof(char):` is the immediate `1` here and a `const i64` operand in
  the reference.  It predates C14 - the same difference stands for a label the
  reading always folded - and no fixture switches on one.
- **9.2p2 at the parse**, the row above.

C13 leaves two of its own, both about a definition **nothing calls** and neither
reached by a fixture.  Where a transfer this unit carries as bytes is named
inside an instantiated body, the reference writes the definition only when the
specialization is one the *signature* of that body's own function names -
`f(const A<T> &)` and `A<T>::get()` write it, `f(T x)` copying a local `A<T>`
does not, and a base subobject's is the derived class's signature and not the
base's.  We write it for every such naming, which is 3.2p3 read of 12.8p31's
note that the copy constructor is odr-used however the call is left out, and the
definition is weak - so a unit that writes one the reference does not costs the
program nothing.  The other way round is 12.8p31's *returned* object: the
reference writes the copy constructor of a specialization a template returns by
value and we name nothing there, because the elision leaves no initialization
behind.  Landing either means the enclosing function's own type reaching the
demand, which is a fact the walk does not carry today.

The C13 audit leaves one, recorded in `audit.md` under Open Gaps: a constructor
**only a base subobject ever ran** stands under one of the ABI's two entry
points here and both in the reference.  Widening `writes_base_entry` to every
base subobject the program wrote regressed 43 tests across pa16, pa17 and pa18,
so the rule the reference follows is narrower than that and is not the one this
compiler has; no fixture writes the shape.

What the reference accepts and this milestone is *right* to refuse is a **non-type
template parameter**: `template<int N> int counted() { return N; }` is `N names
nothing where the template that writes it is defined` here and compiles there,
and the README puts semantic support for non-type parameters and arguments on
PA19's Out Of Scope list and names it as PA20's first item.  A probe of the
reference over one is measuring the later milestone.

## Active Checkpoint

**C14 audit - the sibling exits of the four rules C14 landed**, which is the
work a passing PA is left with: each of the four answers one question at one
call site, and what an audit is for is the other call sites that ask the same
question and did not learn the answer.

- **owner**: `sema_overload.cpp` for 13.6's candidate list, `sema_constant.cpp`
  for 5.19's readings of an operand, `ast_parser_declarator.cpp` with
  `ast_names.h` for what a name written where a type could stand is worth, and
  `sema_template.cpp` for what a second declaration of one template says.
- **data flow**, one per rule: 13.6's built-in candidate is ranked in
  `better_builtin` and *chosen* in `builtin_operands`, so the second is where a
  conditional operator, a subscript and 13.3.1.2's other spellings ask the same
  question of an enumeration; 5.3.3p1's operand reaches `evaluate` at seven
  readers (an enumerator, an array bound, a bit-field width, a case label, a
  static member's initializer, an aggregate's element, a `new` bound) and only
  `sizeof` was widened, while 5.19's other constructs over an expression - a
  `?:`, a comma, a call of a constexpr function - are each still `a constant
  expression holds a construct PA11 does not evaluate`; `DeclaredNames::injected`
  is asked at one place in `parse_specifier_seq` and `is_definite_type_id` asks
  a neighbouring question of the same spelling; and 14.1p10's merge is a class
  template's, where a *function* template's defaults are a fact of each
  parameter entity and 14.1p12's "not by two declarations" is refused nowhere.
- **expected complexity**: each is a question already asked once per construct,
  so widening a reader costs the reader and no walk.  The one thing to keep is
  `injected_`'s memo, which is what makes a base chain n deep n steps.
- **validation**: a sweep of the other readers of each of the four - 13.6's
  table under `?:`, `[]` and a compound assignment over an enumeration, 5.19's
  other operand contexts, `is_definite_type_id` over a plain template-name, and
  a function template's merged defaults - through
  `reference-binaries/cppgm++` with g++ beside it, each program run through
  `lowir2cy86`; a regeneration of every `.ref`; then the pa19 report and
  pa1-pa18.  The four disagreements the failure map records are the shapes a
  sweep must *not* be allowed to quietly move onto the reference.

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

C14's own risk is four, and three of them are O(1) at the place they are asked:
one `kind`/`is_scoped_enum` test per non-class operand `better_builtin` ranks,
one expression read per `sizeof` a constant expression writes over one, and one
head read plus one merge per class template *declaration* rather than per
definition - with 14.1p9's default now read in a region of its own head's, so a
naming that fills k defaults opens k regions instead of one and the whole answer
is still memoised per template and written list.  The fourth is the one that
could have been quadratic: `DeclaredNames::injected` walks 10.2p2's base chain,
so n classes each deriving from the one before it and each writing a base's
injected template-name is n^2 steps - which is why the answer is kept per prefix
and spelling.  Measured against an `fb39e2f1` worktree build made with
`make build`, each shape timed twice, `-O0`:

| shape | 32 | 128 | 512 | 2048 | before |
| --- | --- | --- | --- | --- | --- |
| n `E \| F` over two enumerations with a class `operator\|` in scope | 0.00 s | 0.00 s | 0.01 s | 0.05 s | refused |
| n enumerators each `sizeof` over a call | 0.00 s | 0.00 s | 0.01 s | 0.04 s | refused |
| the same in a class template, over a dependent cast | 0.00 s | 0.00 s | 0.01 s | 0.07 s | refused |
| n classes in a chain, each writing a base's injected template-name | 0.00 s | 0.01 s | 0.02 s | 0.30 s | same |
| the same writing `S<int>` instead | 0.00 s | 0.01 s | 0.02 s | 0.31 s | same |
| n classes nested in a class template, the innermost writing `S` | 0.00 s | 0.01 s | 0.04 s | 0.42 s | 0.43 s |
| n uses of the injected name in one member body | 0.00 s | 0.01 s | 0.01 s | 0.05 s | same |
| n declarations of one class template | 0.00 s | 0.00 s | 0.00 s | 0.01 s | same |
| n class templates with a defaulted parameter, each instantiated | 0.00 s | 0.01 s | 0.04 s | 0.20 s | same |
| one template of two defaults named n times | 0.00 s | 0.00 s | 0.00 s | 0.02 s | same |
| n class templates each named by a template-id | 0.00 s | 0.01 s | 0.05 s | 0.24 s | same |

The first three are the shapes the checkpoint makes compilable at all, and each
is linear.  Nothing else moves: the base chain is **0.30 s against 0.30 s** at
n = 2048 with the memo and **0.59 s** without it, which is what says the memo is
the checkpoint's and not an optimisation of the chain - `reference-binaries/cppgm++`
is 2.46 s on the same shape at n = 512 against our 0.04 s.  Peak RSS is flat
within 0.3% at n = 2048 on all four of the shapes that hold state: 29.9 MB
against 29.8 for the chain, 177.1 against 177.0 for the nest, 66.7 against 66.9
for the template-ids and 59.8 against 60.4 for the defaults.

C13 and its audit cost one walk of a class's subobjects per out-of-class
`= default`, one further pass of those walks over the classes already completed
where a unit wrote any such definition at all, one walk of the regions over a
declaration per body an instantiation made, and one pointer per class completed.
Each is per *declaration* rather than per use, and the pass runs once.  Measured
against a `b60697fa` worktree build, each shape timed twice, `-O0`:

| shape | 32 | 128 | 512 | before |
| --- | --- | --- | --- | --- |
| n classes, each holding the previous | 0.01 s | 0.01 s | 0.05 s | same |
| the same with one out-of-class `= default` below them | 0.01 s | 0.01 s | 0.05 s | 0.08 s |
| n classes with a copy constructor each | 0.01 s | 0.02 s | 0.05 s | same |
| the same with one out-of-class `= default` beside them | 0.01 s | 0.02 s | 0.05 s | same |
| n out-of-class member definitions of one class | 0.00 s | 0.01 s | 0.02 s | same |
| n out-of-class member definitions of one template | 0.01 s | 0.01 s | 0.04 s | 0.03 s |
| n class templates, none instantiated | 0.00 s | 0.01 s | 0.02 s | 0.03 s |
| a class of n reference members and n scalars, moved | 0.00 s | 0.01 s | 0.03 s | same |

None of them moved the wrong way.  The shape the resettling pass is worst for is
n classes each holding the previous and each with an out-of-class `= default`
copy - the pass asked n times, the walk n times within it - and it is flat at
0.011, 0.016, 0.017 and 0.017 s to n = 256, because each class reads its
subobject's answer rather than walking it.  The class chain is the one shape the
work makes faster, for the reason the checkpoint's own rule gives: a copy the
standard defines is the bytes where it was a call, so the unit stops writing a
constructor definition per class in the chain.
The resettle is the closing brace's own walk done once more per out-of-class
definition, so it costs the definitions the program wrote and not the objects it
copies.

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

C9 opened one region and one declaration per place a declarator binds and one
region chain rebuilt per decltype-specifier an instantiation reaches; C9's audit
made the specifier's key the declarator's rather than the reading's.  All seven
shapes are linear and stay linear to n = 2048 - n definitions of one parameter,
n declarations of two named parameters, n definitions with a trailing-return-type
over their own places, n members with one, n function templates with a dependent
decltype return each called, one such called over n classes, and n
specializations each with a member: 0.02, 0.01, 0.03, 0.02, 0.07, 0.09 and 0.10 s
at n = 512, and 0.13, 0.29 and 0.39 s at 2048 for the three the checkpoint
opens.  One such specialization *named* 2048 times is 0.05 s and 18.7 MB, which
is what an ordinary function called 2048 times costs, so naming it again reads
nothing again; what a place's region costs is memory, about 1.5 KB per declarator
that binds one.  Two narrower rules are left unlanded and recorded in `audit.md`:
opening that region only where the rest of the declarator can hold an expression,
which costs the walk it saves, and one region rebuild per specifier - the shape
that wants it is one clause of n places each typed by a decltype over the first,
0.19 s and 99.9 MB at n = 512, where the pre-audit binary segfaults at n = 8.

C10 added one prefix resolution and one region re-parenting per qualified
template declaration, one memoised declaration per template and written argument
list, and one lookup per parameter written as `T (X)`; its audit added
14.5.6.1p5's signature and 14.1p9's default, both per declaration and memoised.
Every shape is per *declaration* and linear to n = 2048: n qualified function
template definitions each called (0.05 s at 512, 0.23 s at 2048), n qualified
class template definitions each instantiated (0.12 s and 0.54 s), n member
templates of one class each called (0.05 s and 0.23 s), n templates named by a
partial explicit list (0.06 s and 0.26 s), n out-of-class static member template
definitions (0.02 s at 512) and n parenthesized places in one clause (0.01 s).
The common paths do not move at all, and 14.8.1p2's memo is what keeps them
flat - a name written twice reaches one candidate, so the substitution its
arguments make is done once however many times the list is written.  The shape
the audit makes compilable is **n member operator templates each ordered against
a non-member**, 0.63 s at n = 512, which is 13.3p1's own quadratic over the
ADL-reachable declarations rather than the ordering's: the same call count over
two *non-member* operator templates per class is 1.17 s in the pre-audit binary
too, and `reference-binaries/cppgm++` is 21.01 s at n = 128 against our 0.05 s.

C11's own risk is three, and each is per *use* rather than per specialization:
one literal test at every branch a statement writes, one slot per reference
bound to a prvalue, and one mark of where the startup body stands per
namespace-scope object with a dynamic initializer.  The third is what the
milestone could have made quadratic - counting the body's instructions is a
walk of every block it has already accumulated - so what is recorded is the
block count and the last block's size, which is O(1) whatever the unit has
already added.  Measured against a `b95b5da0` worktree build, each shape timed
twice, `-O0`:

| shape | n = 32 | n = 128 | n = 512 | before at 512 |
| --- | --- | --- | --- | --- |
| n functions of three constant conditions each | 0.00 s | 0.01 s | 0.04 s | 0.04 s |
| n functions binding two references to prvalues and passing one | 0.00 s | 0.01 s | 0.06 s | 0.05 s |
| n namespace-scope objects with a dynamic initializer | 0.00 s | 0.00 s | 0.01 s | 0.01 s |
| n instantiated static data members, each reached | 0.00 s | 0.01 s | 0.07 s | 0.07 s |
| n functions returning a value-initialized class | 0.00 s | 0.00 s | 0.02 s | 0.03 s |
| n classes over an empty base and an empty member | 0.00 s | 0.02 s | 0.08 s | 0.08 s |

Every one is linear and stays linear at n = 2048 - 0.19 s, 0.23 s and 0.33 s for
the first, second and fourth.  What moves is memory, and only where an object
was made that was not there before: 512 functions binding two references each is
19.2 MB -> **21.6 MB**, about 2.3 KB per temporary the reference now binds,
which is the storage the previous `addr <literal>` had no object to name.  The
two shapes that emit *fewer* instructions move the other way - 26.9 -> 25.0 MB
on the empty-subobject shape and 13.8 -> 13.3 MB on the returned one.

C11's audit adds four questions and each is O(1) at the place it is asked and
asked once per construct.  `folded_edge` is four questions of one node and
*replaces* the reading a folded condition used to be lowered through; the image
gains two type questions per scalar and one per item; and 3.6.3p1's entry is one
flag store per registered end.  Measured against a `52c679e1` worktree build at
n = 32, 128 and 512 and again at 2048: n functions of three folded conditions
(0.07 s at 512, 0.28 s at 2048), n functions of two folded short-circuits (0.06
and 0.24 s), n namespace-scope references bound to values (0.01 s), n
value-initialized pointers (0.01 s), n pointer members in two aggregates (0.00
s), n instantiated static members with a destructor (0.00 s), n variable
templates through a qualified name (0.01 s) and n references bound to a
conditional (0.03 s) are each where the checkpoint left them and each linear.
The one shape that *moves* moves down - **n `&&`/`||` values whose left operand
folds, 0.22 s -> 0.17 s and 68.5 MB -> 51.5 MB at n = 2048**, because the slot
two paths wrote and the three blocks they needed are no longer built - and the
one that costs is the reference bound to a value: 12.8 MB -> 19.4 MB at n = 2048,
about 3.2 KB for each temporary the startup body now holds where an image item
stood, which is the storage 12.2p1 says is there.

C12's own risk is four, and each is O(1) at the place it is asked: one region
test per class or enumeration declared, one extra field read in
`TypeTable::is_dependent`'s answer for a class, one `find(' ')` per
type-specifier-seq read out of a spelling, and one integer test at each place
3.9p5 requires a complete type - which is what `require_complete_type` costs in
a unit that named no specialization.  Measured against an `8515a2d4` worktree
build, each shape timed
twice, `-O0`, at n = 32, 128, 512 and 2048:

| shape | 32 | 128 | 512 | 2048 | before at 2048 |
| --- | --- | --- | --- | --- | --- |
| n typedef-names of one specialization | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.02 s |
| n specializations over n classes, each an object | 0.01 s | 0.02 s | 0.06 s | 0.32 s | 0.30 s |
| n nested classes in one class template, instantiated | 0.00 s | 0.01 s | 0.02 s | 0.10 s | 0.11 s |
| n function declarations returning one specialization | 0.00 s | 0.00 s | 0.01 s | 0.03 s | 0.03 s |
| n elaborated template arguments, each declaring its class | 0.01 s | 0.02 s | 0.06 s | 0.27 s | 0.27 s |

Every one is linear and none moves: 2048 specializations each with an object is
0.32 s against 0.30 s and 73.1 MB against 73.2 MB, and a 200-deep chain of class
templates each nesting a class and deriving from the one before it is 0.02 s and
13.3 MB against 0.02 s and 13.0 MB.  What the deferral could have cost is a
second walk per demand, and it costs none: the demand is a flag on the
declaration, cleared the first time it is met.

C12's audit moved that demand to 3.9p5's own list and added one place it is
asked *per node* rather than per construct - `SemaAnalyzer::expression`, where
every expression the layer reads leaves - so that is what its sweep is built
around.  Against a `7afd0f26` worktree build made with `make build`, each shape
timed twice, `-O0`:

| shape | 32 | 128 | 512 | 2048 | before |
| --- | --- | --- | --- | --- | --- |
| n arithmetic expressions in one body | 0.00 s | 0.00 s | 0.01 s | 0.07 s | same |
| the same with one specialization outstanding | 0.00 s | 0.00 s | 0.01 s | 0.07 s | same |
| n expressions of a specialization's own class type | 0.00 s | 0.00 s | 0.01 s | **0.05 s** | 0.06 s |
| n specializations named by a typedef and never used | 0.00 s | 0.00 s | 0.02 s | 0.12 s | same |
| n specializations named by a typedef and each used | 0.00 s | 0.02 s | 0.09 s | 0.43 s | same |
| n function definitions taking one by value | 0.00 s | 0.00 s | 0.02 s | 0.11 s | same |
| n specializations of one template, each with a member | 0.00 s | 0.01 s | 0.06 s | - | same |
| n out-of-class member definitions of one template | 0.00 s | 0.01 s | 0.03 s | - | same |
| n class templates, each deriving from the previous | 0.00 s | 0.01 s | 0.04 s | - | same |

The expression demand is two tests for the pointer, the reference and the scalar
it usually finds and one hash probe for a class, and the row that moves moves
*down* - a body of 2048 class-typed expressions is 0.06 -> 0.05 s, because the
class it names is now read once at the first of them instead of at the
declaration and every naming after it.  The quadratic the tier already had is
unmoved: n out-of-class member definitions of a template with n specializations
is 0.03, 0.11 and 0.47 s at n = 32, 64 and 128 against 0.03, 0.11 and 0.48 s.
Peak RSS is flat at n = 2048 - 36.2 MB for the typedef-only shape, 96.8 MB for
the used one, 21.4 MB for the class-typed expressions - because what the change
adds per specialization is one `bool` and one counter.

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

Valgrind is clean over all 363 fixtures.  A 20000-deep parenthesized
expression is refused by the parser at about 1000, so the definition-time walk's
recursion is bounded by the same limit the expression layer already is; a
declarator's own parenthesis nest is refused between 8000 and 16000, which is
what bounds every walk that reads one.

`dev/src/sema_analyzer.h` sits at 2399 lines against the audit's 2400,
`dev/src/sema_lifetime.cpp` at 2998 against 3000 and
`dev/src/sema_template.cpp` at 2958 against 3000: C14 added no declaration to
the header at all - 5p9's arithmetic operand became a free function of
`sema_overload.cpp`, and 14.6.1p1's injected-class-name belongs to
`DeclaredNames` - because there was one line left.  C12 made seven `static`
members of `SemaAnalyzer` that ask nothing of one free functions of the files
that ask them - 8.3.5p5's and 8.3.5p1's declarator qualifiers, 5.17p7's
compound operator, 5.2.9p1's lifted operand, and 10.3's three questions about
what a member dispatches - when the header reached **2402**, and moved 3.9p6's
demand for a complete class into `class_constructors`, which every place that
builds an object already asks, when `sema_lifetime.cpp` reached **3001**; C10
moved `NotConstant` beside
5.19p3's value in `sema_declaration.h` and `SemaDialect` beside 3.10's value
category in `sema_facts.h` when the header reached **2409**; C9's audit split 8.1p1's
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
| C10 | the region a qualified declarator-id declares into, the member a member template declares, and the arguments a template-id left out: 9.4.2p1's class-head-name and 3.4.1p8's declarator-id recording their pattern on the declaration the region that name reaches already has, with 14.1p1's own head opened *inside* that region while the declarator and the body are read - so `T` is found before a typedef-name of that spelling the region declares, `declaring_region` still steps out for the declaration, and 14.7.1p1's instantiation reads the same definition with the bindings standing where the head did; 3.4.3p1's prefix walked component by component, which is what finds `v::S<T>::f`'s owner; 14.5.2's member template given 9.3.1p3's object parameter, with 14.8.2.1p1's pairs beginning after it; 14.8.1p2's partly written argument list made a declaration of its own that a call and 13.4p1's target each deduce the rest from; and 8.2p7's `T (X)` read back as the declarator 3.4 says it is | 310 / 340 -> **322 / 346**, the six new tests being six of the shapes these leave and the failing 24 the 30 C9 left less the six taken; pa1-pa18 1778 / 1778; the file audit passes, which it did not - `sema_analyzer.h` reached 2409 lines against the limit of 2400 - and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests`; 48 synthesized shapes through this compiler, `reference-binaries/cppgm++` and g++, 36 compared as emitted LowIR and identical to the reference in all 36 and identical in `object=`, `binding=` and `linkage=` - which the comparison strips - in all 35 both compilers accept, with the shapes only the reference accepts decided against it by g++ and the two it alone refuses left as its own; two units defining one qualified class template and one member template, order-free and symbol for symbol and `object=` for `object=` the reference's; a unit writing all four rules run through `lowir2cy86` to the 0 g++ builds it to return; six scaling shapes linear to n = 2048 and four common ones unchanged against an `e7eb1c1a` worktree build; valgrind clean over all 346 fixtures |
| C10 audit | the readers 14.5.2's object parameter was not given, and 14.8.1p2's other arm: 9.4.1p2's out-of-class definition of a **static** member template matched to its declaration by 14.5.6.1p5's signature rather than by 13.1's index of the list as written; 14.7.1p1's specialization saying what it is called on, because the region it is read under binds arguments and neither question can be asked there; 14.5.6.2p2's places two templates are ordered by, which gives the one non-static member a reference where the other wrote its own first operand and drops it against 13.3.1p4's static member of the same class; 14.1p9's default filling the trailing argument 14.8.1p2 lets a use omit; 3.4.3p1's leading `::` and typedef-name component in a member definition's prefix; and 14.8.1p2's partial list deduced from at 14.7.2p1's explicit instantiation | 322 / 346 -> **327 / 351**, the five new tests being five of the shapes these leave and the failing 24 the same 24 by name, with the ordering one a **regression C10 shipped** - it passes against the pre-C10 `e7eb1c1a` build; pa1-pa18 1778 / 1778; the file audit passes and the build prints nothing, `sema_analyzer.h` at 2391 against the limit of 2400; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 27 checked-in under `cppgm.tests/course/pa19`; 96 synthesized shapes through this compiler, the `0b3f72b8` pre-audit and `e7eb1c1a` pre-C10 builds and `reference-binaries/cppgm++` with g++ beside them, 80 compared as emitted LowIR and identical to the reference in all 80, with no shape in the sweep accepted by both oracles and refused here and six the reference alone refuses; one unit writing all six rules run through `lowir2cy86` to the 42 g++ builds it to return; two units order-free and `object=` for `object=`; nine scaling shapes at n = 32, 128 and 512 against a `0b3f72b8` worktree build, unchanged within 1% of their memory, and the one the audit makes compilable measured against the same call count the pre-audit binary already compiled; valgrind clean over all 351 fixtures |
| C11 | the LowIR an instantiated unit owes, and the entries it does not: 14p1's template-declaration made to declare no *object* either, so a variable template and its partial specialization lay out nothing while 14.5.1.3p1's qualified static data member still does; 14.7.1p6's initialization of a member an instantiation defined left to the use that names it, so a startup body every one of whose initializations writes nothing is no body of this unit's; 12.2p1's temporary a reference binds given storage of its own and named after the place that asked - `retref`, `refarg` and `tmpref` - where the address of a *value* stood before; 6.4's condition that is a literal lowered as the jump one of its edges is, with 5.14's operand that folds leaving the operator standing for its other one and the block a short-circuit reserved opened only where something reaches it; 12.1p5's constructor of a subobject that holds nothing naming no address while 12.8p15's transfer still names one; 12.8p31's returned object taking the constructor the elision left rather than 8.5p7's zero; and 4.10p1's null pointer value for a value-initialized pointer | 327 / 351 -> **343 / 358**, the seven new tests being seven of the shapes these leave and the failing 15 the 24 C10's audit named less the nine taken; pa1-pa18 1778 / 1778; the file audit passes with its five inherited warnings and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 39 under `cppgm.tests/course/pa19`; 40 synthesized shapes through this compiler and `reference-binaries/cppgm++`, 38 identical once the comparison's own stripped metadata is off, with the one shape it alone refuses decided against it by g++ and 8.5.3p5 and the one remaining difference 12.8p31's mem-initializer elision the failure map already names; all seven new fixtures run through `lowir2cy86` to the value g++ builds them to return, and each of them differing against the `b95b5da0` pre-C11 build; three two-unit programs - an instantiated static member beside a program-written object, beside a second instantiated one, and the first pair in either order - identical to the reference and order-free, which is what says the startup body's new fact belongs to the *program* and not to a unit; six scaling shapes at n = 32, 128 and 512 against that build, unchanged and linear to n = 2048, with the startup-body mark made O(1) before it was measured and the one memory move 19.2 -> 21.6 MB for the temporaries a reference now binds; valgrind clean over all 358 fixtures |
| C11 audit | the readers a landed rule was not given, and the entry a registered end stands in: 6.4p4's condition that *is* a literal asked of the node rather than of the operand a lowering wrote, with the block 5.14's right operand is read in reserved after that question rather than before it and the same rule left standing where the operator's *value* is named; 12.2p1's temporary and 4.10p1's null pointer value asked of the **image** as well as of a body, so a reference's storage holds an address and a pointer subobject's zero is storage; 3.6.3p1's registered end of a lifetime owing 3.6.2p2's entry it stands in; and 14p1's pattern told from 14.5.1.3p1's static data member by the region the declaration belongs to rather than by the spelling that reached it | 343 / 358 -> **348 / 363**, the five new tests being five of the shapes these leave and the failing 15 the same 15 by name; pa1-pa18 1778 / 1778; the file audit passes with its five inherited warnings and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 44 checked-in under `cppgm.tests/course/pa19`, diagnostics included; 379 synthesized shapes through this compiler, the `52c679e1` pre-audit and `b95b5da0` pre-C11 builds and `reference-binaries/cppgm++` with g++ where a value can be run, every shape both compilers accept identical as emitted LowIR but the four the reference is alone on; one unit byte-identical to the reference's LowIR and run through `lowir2cy86` to the 39 g++ builds it to return; two units emitting `__cppgm_init` and `__cppgm_fini` byte-identical to the reference's and order-free; nine scaling shapes at n = 32, 128 and 512 against a `52c679e1` worktree build, each linear to n = 2048, with the value-path fold 0.22 s -> 0.17 s and 68.5 -> 51.5 MB and the one memory cost 12.8 -> 19.4 MB for the temporaries 12.2p1 says the program has; valgrind clean over all 363 fixtures |
| C12 | the region a name written before its arguments is looked up in, and the point an argument list settles: 14.6.2.1p9's nested class of the current instantiation made a dependent type, so a template-id over one names no class while the pattern is read, a base-specifier that writes one is 14.6.2p3's dependent base, and a bound computed from what such a type is worth is 14.6p8's rather than 8.3.4p1's; 3.4.1p8 and 3.3.2p5 reading the base-clause of a member class defined outside its class in the region its class-head-name reached; 7.1.6.3p1's elaborated-type-specifier read where 14.2 leaves a template argument as text, with 3.4.4p2's type-only lookup and 3.3.2p6 declaring the class a class-key reaches none of; 3.4.3.1p2's second arm, so a using-declaration whose name is the last component of its own nested-name-specifier names that class's constructors however that component was spelled; 14.7.1p1's instantiation asked only where a *completely-defined* type is required, so a simple-declaration's decl-specifier-seq leaves the specialization declared and `require_complete_type` is the demand 3.9p5's contexts make; and 5.3.6's operator under the two spellings 1.4p8 reserves for it | 348 / 363 -> **360 / 369**, the six new tests being six of the shapes these leave and the failing 9 nine of the same 15 by name; pa1-pa18 1778 / 1778; the file audit passes with its five inherited warnings and the build prints nothing, `sema_analyzer.h` at 2395 against 2400 and `sema_lifetime.cpp` at 2998 against 3000 after seven static members became free functions and 3.9p6's demand moved into `class_constructors`; 28 synthesized shapes through this compiler and `reference-binaries/cppgm++` sweeping every context 3.9p5 requires a complete type in - object, member, base, `sizeof`, `alignof`, `new`, qualified lookup, by-value argument, returned value, array, reference, derived-to-base, assignment, static member, two-level typedef and a specialization over one - agreeing on exit status in all 28 and as emitted LowIR in every one but the order the harness canonicalizes and the `operator new` spelling that already differed; all six new fixtures run through `lowir2cy86` to the value g++ builds them to return; five scaling shapes at n = 32, 128, 512 and 2048 against an `8515a2d4` worktree build, each linear and none moved, with memory within 0.1%; valgrind clean over the fixtures the checkpoint moved |
| C12 audit | 14.7.1p1's point read at the demand instead of at the naming: `asked_specialization` only marks, however many times a name is written; `require_complete_type` is the one demand and is read at 3.9p5's own list - the declarator that *defines* an object rather than every declarator, 8.3.5p6's return type and parameter objects of a *definition*, and 3.9p5 over an expression at the one place every expression the layer reads leaves; and 14.6p8's reading asks for nothing, because a demand answered under `checking_` reads the pattern in the checking dialect | 360 / 369 -> **365 / 374**, four of the five new tests failing against the `7afd0f26` pre-audit build; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically; 47 synthesized shapes - 30 that name a specialization and require no complete type, 17 that do - through this compiler, that build and `reference-binaries/cppgm++` with g++ beside them, 46 of 47 identical as emitted LowIR and the pre-audit build refusing 20 of the 30 and 1 of the 17; one unit writing eleven namings over ten of the spellings run through `lowir2cy86` to the 39 g++ builds it to return; two units, both orders; ten scaling shapes at n = 32, 128, 512 and 2048, each where the checkpoint left it with the class-typed expression path 0.06 -> 0.05 s at n = 2048 and peak RSS flat; valgrind clean over all 374 fixtures |
| C13 | which copy is the bytes, and which definition the unit writes: 8.4.2p2's `= default` outside the class settling 12.8p12 again against a complete class, so a copy the standard defines there is the bytes and not a call; 8.3.2p1's reference member ending 12.8p15's leading run and taking 8.5.3's initialization of a reference; 4.10p3's base conversion made an expression whose evaluation observes only its operand, so 9p6's empty base names only the subobject it builds; 3.2p4 keeping a definition the program wrote outside its class however 12.8p12 carried the call, and 14.7.1p1 keeping one a body an instantiation made named - `note_instantiated_transfer` over `instantiated_body_`, asked of 12.8p15's transfer alone because every other constructor the standard defines has 12.1p5's answer, and of a declaration the pattern wrote rather than one the specialization's own class-specifier gave; and both of the ABI's entry points owed where the use that made the definition was one the program wrote out, `source_base_entry` telling that from a base subobject written inside another instantiation, which asks for the entry it names alone | 365 / 374 -> **374 / 378**, the four new tests being four of the shapes these leave and the failing 4 four of the same 9 by name; pa1-pa18 1778 / 1778; the file audit passes with its five inherited warnings and the build prints nothing, `sema_analyzer.h` at 2400 against 2400 and `sema_lifetime.cpp` at 2998 against 3000 after 12.1's entry marking moved beside 12.4p3's; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 59 under `cppgm.tests/course/pa19`; 56 synthesized shapes through this compiler and `reference-binaries/cppgm++` - defaulted, deleted, in-class, out-of-class and `inline` out-of-class copies and moves, reference members, empty bases, arrays, member classes, virtual members, and the base and complete entries under instantiated and program-written callers - identical as emitted LowIR and symbol for symbol in 51, the five differences being the two narrower rules the failure map records; the C12 audit's sixth shape, `struct d : holder<box> { };`, now among them; all four new fixtures and all five fixtures taken run through `lowir2cy86` to the value g++ builds them to return; six scaling shapes at n = 32, 128 and 512 against a `0d7f7fe0` worktree build, none moved the wrong way and two of them halved; valgrind clean over the nine fixtures the checkpoint moved |
| C13 audit | which definitions the object file holds, asked of the definition instead of of an elided call: 9.3p2's member function this unit's own source defined outside its class never deferred, with 2.2p1's definition read from an included file and 14.7.1p1's specialization still left to the use that asks; 9.2p2's complete-class answers made one `settle_class_answers`, called where the class-specifier closes and again over every class settled before an 8.4.2p2 definition arrived, so a class holding, deriving from or holding an array of one reads the answer that moved; and 12.8p12's base subobject carried as bytes naming no entry point, because nothing runs | 374 / 378 -> **377 / 381**, the three new tests being three of the shapes these leave and the failing 4 the same 4 by name, all three failing against the `b60697fa` pre-audit build; pa1-pa18 **1778 / 1778**; the file audit passes with its five inherited warnings and the build prints nothing, `sema_analyzer.h` back to 2399 against 2400 after three accessors and the `observable` wrapper moved to the `.cpp` that owns them; every `.ref` regenerates byte-identically over all 65 fixtures under `pa19/tests/spec`, all 254 under `pa19/tests/general` and all 62 under `cppgm.tests/course/pa19`, the 28 diagnostics included; 49 synthesized shapes through this compiler and `reference-binaries/cppgm++` - 25 over which definition the unit holds, 11 over 8.4.2p2's answer reaching a holder, a base, an array and the class written after the definition, 5 over 8.3.2p1's reference member, 4 over an instantiated body with no call under its transfer, and 4 over 9p6's empty base and the ABI's two entry points - identical as emitted LowIR in 48, the survivor the base-entry gap the failure map now records; thirteen programs run through `lowir2cy86`, every one to what the reference's own LowIR runs to; one header included by two units, identical to the reference as one unit and as two; ten scaling shapes at n = 32, 128 and 512 against a `b60697fa` worktree build, none moved the wrong way and the class chain 0.08 -> 0.05 s, with the resettling pass's own worst shape flat at 0.017 s to n = 256; valgrind clean over all 381 fixtures |
| C14 | what a name or an operand is worth where the reading has to decide before it has the answer, which is the last four: 13.3.1.2p1's *enumeration* operand reaching 13.6's built-in candidate without asking a conversion function for one, so `E \| F` is `operator\|(int, int)` on 4.5p3's promoted operands and a class `operator\|` an enumerator converts to loses to it, with 5p9's usual arithmetic conversions bringing the two written operands to the one type a candidate is written over; 5.3.3p1's operand read as the *expression* the grammar allows wherever 5.19 asks for a constant, with 14.6p8 standing one value in its place while an argument list is what says how large it is; 14.2 and 14.6.1p1 leaving a plain template-name a type-specifier only where 9p2's injected-class-name stands - the class's own body, a definition on its declarator-id, or a class derived from one - so 6.8p1's `close_impl(which);` is the call 3.4.1 finds, with 10.2p2's chain memoised per prefix and spelling; and 14.1p10's defaults merged from every declaration rather than the definition alone, each read in a region its own head spelled, with 14.1p2's parameter count now asked of a redeclaration too | 377 / 381 -> **388 / 388, the PA passing**, the seven new tests being six shapes these leave and one guard for the injected-class-name the restriction could have broken, six of the seven failing against the `fb39e2f1` pre-checkpoint build; pa1-pa18 **1778 / 1778**; the file audit passes with its five inherited warnings and the build prints nothing, `sema_analyzer.h` at 2399 against 2400 with no declaration added to it; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 69 under `cppgm.tests/course/pa19`; 68 synthesized shapes through this compiler, that build and `reference-binaries/cppgm++` with g++ beside them - 22 over 13.6's table against a class operator, 18 over `sizeof` in every constant-expression context, 16 over the injected-class-name and 12 over merged defaults - 39 of them moved and every shape both compilers accept identical as emitted LowIR to the reference but the four the failure map now records, each decided against it by g++ and by the standard; 58 of them run through `lowir2cy86` to the value g++ builds them to return, with the two the empty-class-by-value scaffold cannot run confirmed against the reference's own LowIR running to the same wrong value; two units naming one merged-default specialization identical to the reference and order-free; eleven scaling shapes at n = 32, 128, 512 and 2048 against that worktree build, the three the checkpoint makes compilable linear and none of the other eight moved - the base chain 0.59 s -> **0.30 s** once 9p2's answer is kept per prefix, which is the pre-checkpoint time - and peak RSS flat within 0.3%; valgrind clean over all 69 course fixtures and the four tests the checkpoint took |
