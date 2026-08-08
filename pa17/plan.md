# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **227 / 230** of its fixtures, with pa1-pa16 at **1494 / 1494**
and the file audit passing with the three recorded header-weight warnings it
already had.

`reference-binaries/cppgm++` accepts PA17 input, and **it reproduces every
checked-in fixture exactly**: `pa17/scripts/ref_vs_fixtures.pl` runs it over the
whole suite under the harness's own relaxed comparison and reports 208 same, 0
different, 0 exit-status different - the other 22 tests are the ones whose
`.ref.exit_status` is a refusal. So the binary is not merely a second oracle:
where it and a fixture could disagree they do not, and a synthesized shape it
answers differently from us is a defect until judged otherwise.
`pa17/scripts/probe_diff.pl` runs one synthesized unit through both. Probe it
before guessing at a rule the fixtures pin only one point of - it is what
settled 5.2.2p4's boundary below, what showed 5.3.4's zero is not 8.5p8's, and
what settled every rule C10 wrote. It takes a `MYCC` binary, which is how a
disagreement is told from a *new* disagreement: `git worktree add` at the commit
before the checkpoint, one build, and the same probe answers whether the
checkpoint introduced it. Three of C11's audit's five blockers were older
holes its new answers reached and two were its own, and nothing but that
comparison says which - keep a worktree at the commit before the checkpoint *and*
one at the checkpoint itself, because the second is what the audit's own before
and after are measured against.

## Stage Design

PA17 gives the PA16 object model value semantics. Nothing becomes a second
pipeline: every fact the milestone adds is hung on the owner that already
answers the question it belongs to.

- `sema_class.cpp` owns what a class *is*. 12.8's four value-transfer members
  are four more facts of it, settled where 9.2p2 completes the class from the
  parameter list 9.3.1p3 put in the type and from the class's **own** region -
  never from a lookup that reaches a base or an enclosing class. p11/p23 say
  which the standard cannot define, p12/p25 which carry only bytes; the class
  holds all four as `SemaEntity::transfers`. 12.3.2's conversion functions are
  the same shape: a member function whose *name is a type*, so the region binds
  the type's own spelling and never the tokens, the class holds the ones it
  declares as `SemaEntity::conversions`, and `conversions_above` chains the
  classes that declare any so 13.3.1.5's candidate set is one walk of those and
  not of every base.
- `type_model` owns what an object of the class is *carried by*: whether the
  bytes stand for the copy, whether 8.4.3p2 leaves the program a copy at all,
  and whether 6.6.3p2 hands one back as bytes or through a destination the
  caller named. One fact each, on the type, with one writer. It also owns
  **8.3.5p1's ref-qualifier**, which is a fact of the *function type* for the
  same reason 8.3.5p7's cv-qualifier-seq beside it is: it is written after the
  parameter-clause, a declaration and the definition written outside its class
  agree on one type, and 13.1 has `f() &` and `f() &&` to tell apart.
- **Whether the bytes of an object stand for the object is one fact**
  (`TypeTable::bytes_stand_for_object`), and it is 12.8p12's copy *and* 12.4p8's
  end together: an object something runs at the end of is one the program can
  watch, so a second object made out of its bytes is a second end to run. The
  copy, the memberwise definition, 5.16p3's arm, 12.8p31's elision, 6.6.3p2's
  returned object and 5.2.2p4's argument all ask it, and the destructor half is
  the same `vacuous_destruction` every end of a lifetime asks - never 12.4p5's
  triviality, which calls a destructor that runs nothing non-trivial.
- **5.2.2p4's boundary is `TypeTable::passes_indirectly` and
  `LowirUnitLowering::describe_parameter`**, the other half of 6.6.3p2's: a
  class whose bytes do not stand for the object is `ptr [pass=by_address]`, the
  caller passes the address of the object it built, the callee reaches it
  through that address and copies nothing into a slot, and 8.5.3p5 names the
  storage `arg` rather than `argobj` because which half it is is what the name
  says. Its copy half is `carried_by_bytes`: the class's own answer where it
  derives from nothing, and 10p1's storage it is laid out over - the base
  subobject **and** the members - where it derives from something, which is what
  the checked-in ABI reads. What the boundary owes at the other end is 12.4p5's
  destruction of that one object - which 12.4p8's reading of an empty body does
  not reach, because that is an answer about an object this translation created
  and not one it was handed - **at every exit the function has**: each return,
  the end of the body, and 15.2p2's handler over anything the body does, which
  is `Fact::destruction` written on the parameter line. The caller owes it
  nothing on any path.
- **12.6.2p6's delegation is a reading of the *list* and not of one entry.**
  A mem-initializer-id that names the constructor's own class is 12.6.2p2's
  neither base nor member: it hands the whole initialization to another
  constructor of the same class, so `delegating_initializer` answers it before
  the base and the members are walked at all and `write_delegating_initialization`
  writes that one call in place of every step. **Which class a
  mem-initializer-id names is asked of what the name denotes and never of its
  characters** - `names_the_class` is that one question, asked by p2's base and
  by p6's own class alike - because the index the list is read into is keyed on
  the last component and `struct S : N::S` writes a base whose component is this
  class's own name. An unqualified spelling needs no lookup: 12.6.2p2 looks it
  up in the scope of the constructor's class first, where this class's
  injected-class-name stands, so the probe is one probe for every
  ctor-initializer that writes no nested-name-specifier.
  `Placement::Delegate` is what
  says the object is the one this constructor is already running on, so `this`
  is the address the target's *complete-object* entry is passed exactly as it
  stands - no address taken around a name, no 4.10p3 conversion to a base - and
  the constructor it builds is no subobject a later step could leave standing.
  The edge is `SemaEntity::delegates_to`, and p6's cycle is one colouring of the
  constructors that delegate, run where the unit has been read: each has at most
  one edge, so the graph is a forest of chains and every vertex is walked once
  however many chains run through it.
- **9.5p1's one storage is one question with one owner** (`one_storage`), and
  every walk of a class's members reads it because at most one of them holds an
  object. 12.6.2p8's construction reads a variant member's
  brace-or-equal-initializer only where no *other* variant member was designated
  by a mem-initializer-id, and a variant member neither reaches is not
  initialized at all - not default-initialized, which is what an ordinary member
  of the same type would be, and which is what lets a union hold a class with no
  default constructor; which of the two it is, is one probe of the class's region
  per mem-initializer the list held. **12.4p8's destruction is the same reading
  and its word is `non-variant`**: a union's destructor destroys no member of
  its own, so `write_member_destructions` writes none, `vacuous_destruction`
  answers what its body alone comes to, and `destruction_nonthrowing` has
  nothing for 15.4p14 to allow. **12.8p15/p28's transfer is that reading a third
  time**: the member the standard defines for a union copies the object
  representation, whole, because no walk of the declarations can say which member
  the bytes hold - so the output is constant in the member count where a
  memberwise walk was one step per declared member over the one storage.
  8.5.1p15's aggregate takes the first member alone, and 9.5p2's "at most one
  brace-or-equal-initializer" is a fact of the layout walk, where the members are
  already in declaration order.
- **8.4.2p2's `= default` written outside the class is a definition**, and a
  special member is parsed by one of *two* paths - `parse_special_member` for a
  constructor and a destructor, the ordinary init-declarator for an assignment
  operator - so both give it the same three answers, keyed on 3.4.3p3's
  qualified declarator-id because that is what "written outside the class"
  means on either. The class's declaration is the one it defines and 3.2p1
  refuses a second definition of it; 8.4.2p5 leaves that declaration
  user-provided; and 7.1.2p2 leaves the definition non-inline, so 3.2p4 makes
  this unit hold it whether or not anything here names it. What that splits is
  **who wrote the declaration from who wrote the definition**: 12.1p5's "nothing
  to do" is asked of the definition, so `constructor_call` and
  `owe_elided_construction` both ask `!(user_provided && !defaulted)`, while
  8.5p7's zero goes on asking `user_provided` alone. 3.4.1p8's collection takes
  these declarations beside the bodies, so 12.4p8's vacuity is settled from the
  syntax before a body reading earlier asks it.
- **An end of a lifetime asks one of two questions, and which one is which kind
  of object is ending.** An object a *declaration* named - a local leaving its
  block, a namespace-scope object, a subobject of a synthesized destructor - is
  one the program can watch leave, and 12.4p8's `vacuous_destruction` says what
  running its destructor comes to. An object the *translation* made - 12.2p1's
  temporary, the one 12.2p5 moved into a block, and the object a
  delete-expression ends - is reached through an address alone, so the call is
  the one mark its end has and it is written wherever the program declared a
  destructor below its class at all (`TypeTable::declared_destruction`, settled
  where the class completes off the same subobjects 12.4p8 walks, so every
  reader is one field read).  `ends_in_call` is where the two meet, keyed on an
  object with no name being the translation's; the one temporary that asks
  12.4p8 instead is 13.3.3.1.5p5's, the one a braced-init-list was written into,
  which demands no definition of its own.  **Whichever of the two it is, the
  action the analysis wrote is the whole answer**: the lowering writes the call
  the action names and asks 12.4p5 nothing of its own, and `register_temporary`
  notes the entry and demands the definition where it sets the fact, because an
  end 15.2p2's handler alone reaches is an odr-use like any other.
- **A read that answers a question is not one of the program's**: 8.5.1p11's
  probe of what a clause initializes, and 5.3.3p1's and 7.1.6.2p4's unevaluated
  operands, go through `probe_expression`, which holds what they create in a
  frame of its own and drops it with the throwaway node.  A temporary left in the
  program's frame by one of them is an end of a lifetime for an object nothing
  was ever asked to give storage to.
- **12.8p15/p28's array member is one step and a walk of the array around it.**
  The member the element's class has takes an *element* and no call of it takes
  the array, so the transfer is written once, for an element, with the source
  read as the element exactly as `write_constructed_object` already reads the
  destination as one: the *line* goes on naming the array - that is where the
  element stands - and the `Value` carries the element's type, which is what
  13.3 chooses on. Which element a step is at is the lowering's
  `ElementWalk`, the one cursor the destination and the source both read
  (`walks_this_array`/`walked_element`), so nothing in the step is written a
  second time to say it. A construction hangs it on the `constructor-action`
  the array already names; an assignment hangs it on `FactKind::ArrayTransfer`,
  which is that same walk over one expression-statement. Below
  `kArrayLoopLimit` the elements are written out and past it the walk is one
  loop with the index in an object of the function, which is where the cursor's
  second form - a value rather than a number - comes from. An array whose
  *element* is carried by its bytes is the whole array's bytes and no walk at
  all, which is 12.8p15's other form and the one every non-class array member
  takes.
- **A step written once and run per element is a fresh run each time, and the
  objects it creates are that element's own.** The lowering keeps where each
  object with no declaration to name it stands (`placed_` and `slots_`, whose two
  writers are `place_object` and `name_object`), and `creates_object` reads that
  to say a temporary already standing is not built a second time - so one
  element's run of a step records what it placed and drops it at its end
  (`open_element_run` / `close_element_run`). Left standing, the second element
  finds the first element's objects: the storage a by-value parameter of the
  element's own `operator=` stands in, and the temporary 8.3.6p9 asks a default
  argument for once per call. Dropping what the run added, rather than saving the
  two maps around it, is what keeps n such members linear in n.
- **3.8p1's end of an object a declaration named of array type is one end per
  element wherever that end is written**, so `begin_object_lifetime` carries the
  count and the stride onto the standing entry (`array_entry`) and 15.2p2's
  handler writes the same walk the end of the block does - the elements written
  out below `kArrayLoopLimit` and one loop past it. 12.6p1's loop over an array
  joins that list only where 12.6.2's initialization is what is running, because
  an array a declaration named is no subobject of a partly built object and two
  entries for it are one destroying the whole array and another destroying its
  first element again. What `new T[n]`'s own cleanup owes is that same standing
  list beside the elements it built and 5.3.4p18's storage, because that block is
  the whole of what an exception out of an element's constructor leaves by.
- `sema_lifetime.cpp` owns what running the four comes to, and what *ending* an
  object's lifetime comes to. 12.8p15/p28's definition is one walk of the same
  subobject list 12.6.2p10 and 12.4p8 walk. 12.4p8's `vacuous_destruction` is
  broader than 12.4p5's triviality by exactly the clause that says so: a
  destructor whose body writes no statement, or which 12.4p4 gave no body,
  comes to what its subobjects come to. **That fact is about a definition, and 3.4.1p8 lets a
  definition stand after a body that asks about it**, so the unit's
  out-of-class definitions are taken from the syntax once before the first
  declaration is read and `writes_no_statement` is the one reading of a
  definition both that and `open_special_member_body` do - and it reads
  12.6.2p8's mem-initializer-list beside the compound-statement, because a
  constructor that writes one comes to something however empty the body under it
  is. **A constructor asks the same two questions a destructor does**, so
  `note_definition_body` is asked of both. It also owns **12.8p31's
  elision**, written once as `creates_its_object` and read by both the analysis
  and the lowering - and `elide_transfer` is that elision asked **after** 13.3
  has chosen, because which constructor an initializer reaches is what says
  there is a copy here at all: an argument that reaches the copy constructor
  through a conversion function of its own class is a prvalue of this class only
  once that conversion has been applied, and a class declaring a constructor
  taking the argument's own type reaches no copy for the elision to remove.
  5.3.4p12's storage is storage the new-expression owns, so the elision reaches
  it exactly as it reaches a declaration's slot.  **What the elision does to the
  object it elided is take it out of 12.2p3's frame**, because the destination's
  own end is now its one end - and the object stands *under* 5.2.9p4's cast and
  not on it, so `created_object_node` is where every reader that needs the object
  rather than the value looks: `elide_created_object`, `release_temporary` and
  8.5.3p5's naming of an argument's storage alike.  Left in the frame it is a
  destructor call on the storage the initialization has just filled.
- **12.8p32's first resolution is `selected_transfer`.** A return whose operand
  names a non-volatile automatic object of the function's own returned type -
  a parameter included, by p32's own wording - reads that object as an rvalue,
  so 13.3 chooses the move member of its class where the class declares one;
  the ordinary lvalue reading is what stands where the first chose no
  constructor taking an rvalue reference, which is the one question 12.8p15
  already settled on the class.  A name a block declared is automatic here,
  because a block-scope object of static storage duration is one the
  declaration itself refuses - and **an lvalue reference names no automatic
  object at all**, because 3.9p9 leaves a reference none and the one its name
  reaches was declared where this function cannot see the end of it.  An rvalue
  reference is the one kind this reads through, which is the binary's rule and
  C++20's implicitly-movable entity rather than C++11's literal wording.
- `sema_allocation.cpp` owns 5.3.4 and 5.3.5, and owns them as *actions over
  storage the expression already names* rather than as a second object model:
  what `new` adds is where the storage came from and what `delete` adds is who
  gives it back. 3.7.4.1p2's four functions are declared in the global
  namespace before the unit is read, so 17.6.4.6's replacement is a definition
  of one of them and not a second function of the same name; 5.3.4p6's first
  array-suffix is the one bound that need not be a constant, **and whether it is
  one the translation knows is a fact of its own** - `Fact::counted`, because a
  count of zero is a count like any other - with the bytes it asks for scaled in
  the count's own type; 5.3.5p9 and 12.5p4 say who takes the storage back, and
  12.4p5's triviality - not 12.4p8's - is what says whether a destructor call
  stands in front of that. **What building one element and zeroing it come to
  are asked once each**: `vacuous_construction` is 12.4p8's walk of the
  subobject tree the other way round, and 8.5p7's two halves - zero where the
  default constructor is neither user-provided nor deleted, call where 12.1p6
  left it something to do - are read off that same constructor, so an array of a
  trivial class is one zero over its extent and no call at all. 5.3.4p15's test
  of the address is `Fact::may_fail`, settled once where the function is chosen
  and read by both forms: 15.4 says the call may report failure at all, and
  18.6.1.1p3's `std::nothrow_t` says it reports it with a value.
- `sema_overload.cpp` owns 13.3. Which of the four transfers a value transfer
  chooses is the argument's value category; **which user-defined conversion an
  argument takes runs in both directions** - 13.3.3.1.2's converting
  constructor of the target's class and 13.3.1.5's conversion function of the
  argument's own - and `standard_only_` is 13.3.3.1.2p1's "one user-defined
  conversion", the one flag that stops a second and stops two classes that
  convert to each other from walking forever. It is set around **both**
  sequences, because either direction is where a second one would slip in.
  Which of a class's conversions 13.3.1.5 chooses is ordered by where the
  conversion gets to - 13.3.3.2p3's second standard sequence - with the
  implicit object argument telling apart only the ones that get equally far.
  13.6's built-in operators are candidates of the same set: `better_builtin`
  ranks the one an operand of class type reaches against the operator function
  13.3 chose, and the operators written over `VQ T&` are reached by a
  conversion that hands back an lvalue.
- **13.3.1p4's implicit object parameter is a reference, and 9.3.1p3 holds it
  as a pointer**, so `object_match` is the one place the two readings meet: the
  pointer still carries 4.10p3's base conversion and 13.3.3.2p3's ordering of
  `f()` above `f() const`, and the reference facts - which categories the
  ref-qualifier binds, and which of two bindings 13.3.3.2p3 prefers - are
  written beside it from `Value::object_category`, the category the object
  expression had before its address became the argument. Every construction of
  an implied object argument writes it: a member access, a call with no object
  expression, an operator's left operand, and 13.3.1.5's synthesized one. The
  category a member access hands it is 5.2.5p4's, which answers a member
  declared to have reference type before it answers a subobject at all.
- **Every rebuilder of a member function's type carries both qualifiers 8.3.5
  writes after the parameter-clause, and every dump spells them where the
  declarator wrote them.** `with_object_parameter`, `declare_using_member`,
  `member_pointer_of` and `substitute` are the four that rebuild one, and
  9.3.1p3's lowering is what moves the cv-qualifier-seq onto the object
  parameter - so `function_description` leaves the ref-qualifier unspelled on
  that form and PA11's description of the declarator's own type spells both.
- **5.2.9p4's cast to a class type is an initialization and not a reading of
  bytes**: `T(e)` is well formed exactly where `T t(e);` is, so the cast is a
  call of the constructor 13.3.1.3 chooses with 13.3.1.4's `explicit` ones left
  in - which is what `construct_object`'s `direct` says, because 5.2.9p4's cast
  and 13.3.3.1.2's conversion reach it the same way with one operand already
  read and want opposite answers - or, where the operand's own class reaches the
  target, a conversion function 12.3.2p2 lets a cast name however it was
  declared. 12.8p31 threads through it: `creates_its_object` reads the temporary
  standing under the cast, so the object the cast is worth is built where the
  place asking for it named storage. A cast to a *reference* whose operand is a
  prvalue is what asked for the temporary the reference binds, so 12.2p1 names
  that storage `refcall`; an argument written around such a cast binds the
  object the glvalue already names and materializes nothing of its own.
  **8.5.3p4's reference-related is what says the operand itself is bound, and
  everything else binds a temporary holding a conversion of it** - so where the
  two are unrelated the conversion is *applied* rather than left as a spelling
  on the operand's line, and 12.2p3's frame holds the end of the object it made,
  because nothing but the cast asked for it. Two bounds are what make that one
  reading. **Only a reference that may bind a temporary is that initialization**
  at all - an rvalue reference, or an lvalue reference to a non-volatile const -
  because the conversion is 5.2.9p4's `T t(e);`; one that may not is 5.4p4's
  reinterpretation of the storage the operand named where a C-style cast has that
  to fall to, and a refusal where a named `static_cast` has none. And **which
  node stands for the object the conversion made is what says who names its
  storage**, because naming it is what asks the lowering for the object at all:
  13.3.3.1.2's converting constructor builds it where the operand stood, so the
  cast lifts to that node and `build_temporary` has already named it, while
  12.3.2p2's conversion function hands it back as the prvalue of a *call*, which
  goes on being one under the cast's own line and is named `refcall` there -
  spelling that call as the lvalue the cast is leaves 12.2p3 an end of a lifetime
  for an object nothing was ever asked to build. Over a referenced type that is
  not a class the cast's own line still stands for it and only 12.3.2p2's
  conversion function is written under it, which leaves PA12's reading of
  `(const int&)wide` as it was and is the one shape of this rule the milestone
  records rather than writes.
- **5.2.9/5.2.11/5.4's cast owns `sema_cast.cpp`**, because it is one question
  with two unrelated answers - a direct-initialization of a temporary of the
  target type, and 8.5.3's binding of a reference - and the second is where the
  readings part company. Nothing there writes an object of its own: it asks 13.3
  for the conversion, `sema_lifetime.cpp` for the temporary that holds one, and
  the open full-expression for that temporary's end.
- **8.5p8's zero is over the storage the bases and the members hold**
  (`TypeTable::has_zeroed_storage`, settled in the layout walk beside 9p6's
  `empty`), so a class every subobject of which holds nothing is zeroed by
  writing no byte however many bytes 1.8p5 gives an object of it. 5.3.4's zero
  is a different one: what stands at a new-expression's address is the storage
  the allocation obtained, and the scalar form covers its whole extent as the
  array form already did.
- 4p3's contextual conversion is one call - `contextual_bool` - reached by a
  condition, `!`, `&&`, `||` and `?:`, with 12.3.2p2's `explicit` left in;
  6.4.2p2's is `contextual_integral`, with it left out; 5.3.5p2's is
  `contextual_pointer`, over the pointer types a class reaches. 5.2.9p4, 5.4p4
  and 8.5p16 reach `explicit_conversion`, which is the same question with
  13.3.1.5p1's restriction that an `explicit` candidate reach the destination
  by a qualification conversion and no further. A cast is `cast_conversion`,
  which is that question with the refusal in it: an operand of class type no
  conversion carries reaches the target through nothing, and never through the
  bytes the object happens to hold.
- **12.2p3's boundary is the analysis's and 15.2p2's handler is the lowering's,
  and they meet at one fact of a node.** `sema_lifetime.cpp` holds one frame per
  open full-expression - an expression-statement, a declaration's initializer, a
  condition, a for statement's loop-continuation portion, a return's operand and
  each mem-initializer - and 12.2p1's object is a fact of the node that produced
  the prvalue (`Fact::object`), so a call and a conditional that hand one back
  name the object every reader of that prvalue reaches. 12.2p5 *moves* a
  temporary a reference binds out of that frame and into the block that declared
  the reference, so the two ends are one end. **An operand that may not run
  holds a frame of its own** - 5.14p1's right operand and each of 5.16p1's arms
  - because a temporary it created exists on that path alone, so what it made is
  ended where that operand ends and not where every path through the expression
  arrives; 13.5.7's overloaded operator is a call and a call evaluates every
  argument, so there the frame is handed back to the enclosing full-expression.
  What the lowering reads is `Fact::destruction`: the destructor the object
  whose lifetime this node begins ends in, written on the declaration and on the
  prvalue alike, because an exception has to end a lifetime wherever it began
  and not only where the program wrote its end. **Which of the two ends an
  action is, is a fact of the action** (`Fact::full_expression_end`), because a
  `return` writes both under one line and 15.2p2 tells them apart.
- **15.2p2's handler is one region at a time, and what it owes is what stood
  when it opened - `lowir_lower_unwind.cpp` owns it.** A region opens where the
  step that made a throwing call began, closes where the set of standing objects
  changes, where the full-expression ends, or where the block does, and is
  written again on each block a step spans. 12.2p3's end of a temporary is
  written *inside* the region that covered it and 3.8p1's end of a block's
  object *after* it, which is what a full-expression covering the one and not
  the other means. The handlers already written are held per number of standing
  objects, so a step needing exactly what an earlier one needed names that block
  again - and **a region a lifetime ended inside is no such block**, because
  what it destroys is no longer what stands. What an end of a lifetime takes out
  of the list is one entry with the place it stood at, never a copy of the list.
  6.6p2's jump puts back what it destroyed on its own path, because the code
  after it is reached by paths where those objects still stand.
  **Two calls open no region of their own.** One is 12.8p15's transfer filling
  an object the *translation* named - 6.6.3p2's returned object, 5.16p3's result
  object, 8.5.3p5's argument storage: what it writes is a value that already
  stood somewhere else, so an exception out of it leaves the program with
  exactly what the expression before it left and nothing new to end. An object a
  *declaration* named is not that, and the copy that fills it is a step like any
  other. The other is where a temporary's storage was just named: naming storage
  leaves nothing standing, so where objects already stand - and the region is
  therefore theirs rather than the step's - the naming stands in front of it,
  which the mark is moved past exactly once and only where the step had not
  already begun.
- **13.3.3.1.5p1's argument is not an expression, so it is carried and not
  read**: a braced-init-list written as an argument travels as the list
  (`Value::braced`) with the line it will be written on holding its place among
  the arguments, and what it converts to is `match_list`'s answer about the
  parameter alone. A class parameter is p3/p4's user-defined conversion
  sequence - whose "same user-defined conversion" is the class it initializes
  (`Match::list_class`), which is what 13.3.3.2p3 orders two of them by before
  the reference binding beside it does; a reference is p5's temporary, named
  after the argument that asked for it; anything else is p6's one clause.
  8.5.1p6 is what bounds a candidate: `clause_capacity` is the leaves an
  aggregate's subobject tree has, held per type, so a list of two clauses
  reaches `f(Two)` and not `f(One)`, and 13.3.3.1p4's third bullet keeps a
  class's own copy constructor out of the set `X{ {a, b} }` was written for.
  What one *subobject* takes out of the enclosing list is its own capacity or
  the one clause its written braces are - never nothing, because 8.5.1p11 lets
  the braces be written as readily as left out, which is what makes `{ {}, 7 }`
  two clauses of a class whose first member holds nothing. An argument written
  as a list reaches no ellipsis at all: every sequence 13.3.3.1.5 gives one is
  keyed on a parameter.
  The clauses are read **once**, where 8.5.4 has a type to read them for, so no
  candidate's probe reads them and no definition is demanded twice.
- **5.2.3p3's `T{a}` and 5.2.3p1's `T({a})` are one shape in the tree and mean
  different things**, so `AstNode::braced` records which the parse saw, the way
  `copied` already records 8.5p14's `=`: the first list-initializes the object
  the conversion makes and the second passes the list as the one argument of a
  constructor of it - which is why `X{ {a, b} }` reaches the constructor the
  braces name and `X({a, b})` is 13.3's ambiguity it is. Over anything that is
  not a class the two are one, because there a list is never an argument.
- **8.5.1p2's constructor an aggregate is given owns its by-value parameters**,
  so a class member is one of them: the parameter is an object nothing after
  that step reads, and what carries it into the member is a read of it as an
  xvalue - the class's move constructor where it has one and its copy
  constructor where it has not. A member whose class can be carried out of by
  neither is what leaves the clauses initializing the subobjects where they
  stand, and so is an **array** member, because 8.3.5p5 adjusts that parameter
  to the pointer it decays to and what the member holds is the array. A
  reference member and a bit-field are parameters like any other - the address
  the one binds, the underlying type the other holds. 8.5.1p15's **union**
  carries its first member alone, in the parameter list *and* in the definition
  that walks it, because every member of a union stands in the one storage and a
  list holding all of them would write each of them into it in turn.
- **Whether 8.5.1p11's braces were left out is one question with one owner**
  (`elides_its_braces`), asked by the walk that writes a subobject where it
  stands and by the walk that passes it as a by-value parameter, so a
  declaration and a prvalue of the same class read one list the same way. Its
  bound is 8.5.1p6's capacity: a subaggregate that takes no clause has no braces
  to leave out. What the parameter form does with the answer is
  `construct_from_clauses` - the subaggregate is one object of its class, built
  where the parameter carrying it stands by the constructor 8.5.1 gives *that*
  class, out of the run of clauses its own subobjects take from the same walk.
- **6.6.3p2's returned object is one object of the function**, so where the
  caller named no destination the storage it stands in is one slot however many
  returns write it: no two of them are ever standing at once. The slot is a
  name and its address is a value, so the name is opened once and the address
  taken again on each path.
- `lowir_lower*.cpp` reads only the resolved tree. Its one shape rule is
  **result-object placement**: the storage an object of class type will stand in
  is named *before* the initializer that fills it runs, and handed to it.
  `creates_object` is that question and `place_class_object` is that hand-off.
  5p4's "the left operand is read where it is written" is the other: a built-in
  operator takes the left operand's value before the right operand runs, which
  is a difference only where the right operand is something that runs at all.
  **4's conversion of a value to another type is one answer** - `converted` -
  so an initialization asks it the same way an argument and an assignment do,
  and 3.9.1p2's two eight-byte integral types sharing one LowIR spelling are
  still two types a copy stands between.
- **Which destination a hand-off is, is what decides whether 5.16p3's selection
  is written once or per path.** A conditional that selects among *glvalues* of
  class type hands back one of two objects, and only one exists on each path -
  so `place_class_object`, and the raw-`copyobj` transfer beside it, write the
  transfer into the destination on the path that chose the object it reads.
  `selecting_conditional` is that one question and it is asked at the hand-off
  and nowhere else, which is exactly what leaves a destination the *program*
  declared filled where its declaration stands: 6.6.3p2's returned object,
  5.2.2p4's argument object and 12.2p1's temporary are the translation's, and a
  local of class type with a constructor call on it is not.  **Every step below
  the hand-off is `place_created_object`**, which is that same placement with
  the question already asked - because 5.2.9p4's cast written between the
  destination and the selection is one step of the one hand-off and not a second
  destination, and `place_class_object` is what a cast case would otherwise
  re-enter.  `selected_arms_` makes an arm stand where the conditional does while
  its path is written, so each leaf is one transfer and nothing is read twice.
- **5.16p6's arm is *read* into the result object, and reading an object is not
  consuming it.**  Where the two operands are glvalues of one type in different
  categories the conditional is a prvalue, and what fills its result object is
  4.1's conversion of the selected operand - so the transfer 13.3 chooses there
  reads the arm as the lvalue it names unless the *program* wrote an rvalue.
  What the program wrote is the arm's **spelling**: a cast to an rvalue
  reference and a call returning one are spelled as references and are read
  through, while 5.2.5p4's subobject of a temporary is spelled as the member's
  own type - its xvalue says when the object it is part of dies, not that this
  arm is what empties it.  The question is asked at `transfer_arm_to_result` and
  nowhere else: everywhere a *use* asks 13.3 directly - an argument, a return
  operand - 5.2.5p4's xvalue stands as it always did.
- 6.6.3p2's boundary is one answer, written by `LowirUnitLowering::open_signature`
  and read by the declaration, the definition and a call through a pointer.
- **12.8p31's NRVO is the object standing in the destination, and every end of
  it is the caller's.** `returned_name` is the one reading of the operand both
  spellings of a return make - the transfer member's argument and the bare name
  a copy of the bytes carries - so a class the ABI hands back as bytes is laid
  out in `%ret` too; and 3.8p1's actions standing under the return are no bar to
  it, because the object the return names is one of them.  `leave_blocks` writes
  no end for the return-slot local at a return, at a block's end, or in 15.2p2's
  handler.  What the elision does *not* remove is 3.2p2: the definition the
  standard gives the constructor it left out odr-uses the transfer member of
  every subobject, which is the same reading `owe_elided_construction` makes of
  an elided subobject step.
- 8.5.3p5's name for the storage a class prvalue with no object of its own is
  given is what asked for the object, and the analysis writes it on the node.
- **3.1p2's "which declaration defines the object" is a fact of the line**, not
  of the entity under it: 9.4.2p2's definition written with a
  nested-name-specifier is a second line describing one object, and only the
  line that lays the storage out may write the image.

Facts PA17 adds: `TypeTable::passes_indirectly`, `::bytes_stand_for_object`,
`::has_zeroed_storage`, `UserType::subobject_bytes`, `::carried_by_bytes`,
`::zeroed_storage`, `SemaFact::object`, `SemaFact::destruction`,
`UserType::vacuous_destruction`, `SemaEntity::wrote_exception_specification`, `Value::braced`,
`Value::listed_class`, `Match::list_class`, `AstNode::braced`,
`SemaEntity::transfer`, `::transfers`, `::conversion_function`,
`::conversions`, `::conversions_above`, `::empty_body`, `::nonthrowing`,
`UserType::copy_deleted`, `TypeTable::returns_indirectly`, `RefQualifier` on the
function type, `Value::object_category`, `Fact::subobject_step`,
`Fact::array_form`, `Fact::object_definition`, `Fact::counted`, `Fact::may_fail`,
`Fact::full_expression_end`,
`SemaEntity::delegates_to`, `Placement::Delegate`,
`FactKind::StorageTransfer`, `FactKind::DeleteExpression`,
`FactKind::ArrayTransfer` with `LowirFunctionLowering::element_walk_`
(`walks_this_array`, `walked_element`, `array_transfer`) and the element run
around it (`place_object`, `name_object`, `open_element_run`,
`close_element_run`), `LowUnwind::elements`/`::stride` on an entry a declaration
made (`array_entry`),
`AstKind::CarriedTypeId` (12.3.2p1's conversion-type-id, carried beside the
syntax as 7.1.6.2p1's decltype operand already is),
`UserType::declared_destruction` with `TypeTable::has_declared_destruction`,
`LowirFunctionLowering::selected_arms_`, and the eleven questions
lifted out of the analyzer so each layer asks them once - `observable_expression`,
`creates_its_object`, `vacuous_destruction`, `declared_destruction`,
`vacuous_construction`,
`default_constructor`, `writes_no_statement`, `elides_its_braces`,
`names_the_class`, `one_storage`, `selecting_conditional` and
`created_object_node`.

## Current Failure Map

3 fixtures fail of 230, grouped by the compiler behaviour they are waiting on.

| group | n | what is missing |
| --- | --- | --- |
| 8.5p16 direct-init of a class from a class | 1 | the fixtures let `T(e)` reach 13.3.1.4's `explicit` conversion function of the operand's class, which the standard's copy-initialization of the constructor's parameter does not. The reference materializes the conversion's result and *calls* the move constructor on it rather than eliding, because the conversion hands its object back as bytes |
| 13.3.1.1.2 surrogate call functions | 1 | an object whose conversion yields a function pointer, called |
| 3.4.3.2 using-directive ambiguity | 1 | two namespaces one level reaches declare one name |

Seven holes earlier sweeps found that no fixture covers and that are not this
milestone's: 9.2p1's refusal of a member declared twice in one class, 5.5's
`.*` in the lowering, 13.5.6's overloaded `operator->`, 10.3's virtual dispatch
(which the README puts after this milestone outright), 8.3.5p5's array parameter
read as an array by the body and as a pointer by the boundary, 12.6.2p2's
mem-initializer that designates a member of an **anonymous** union
(`struct H { union { int a; int b; }; H() : a(3) {} };` - the name is a
subobject of the union's region and not of the class's, so the walk that checks
the list for names it never reached refuses it), and 12.8p31's elision through
a parenthesized single argument - `pair({1, 2})` and `pair(Pair{1, 2})` alike
build a temporary and copy it into the member where the reference constructs the
member in place, because 12.8p31's elision is asked at every exit but the
subobject ones - `elide_transfer` puts the creating node where the
`constructor-action` stood, and for a member or a base that action is what names
the subobject's address, so the elision has to choose the constructor first. The
last of those is what a delegating mem-initializer inherits too:
`S() : S(S(3)) {}` writes the copy the reference elides, which the README leaves
open by putting "copy-elision perfection" out of scope.

Two refusals of programs the reference accepts, both older than C10 and neither
this milestone's own subset: `V v = (V)h();` reaches "an object of the class
type struct V is copied", because `creates_its_object` reads 5.2.9p4's cast only
where a `temporary-object` stands under it and here a `call` does; and a `try`
block in an ordinary body is "a statement outside the PA12 subset", which is
what makes 12.8p32's exclusion of a catch-clause parameter nothing to ask yet.

**Two of the milestone's own shapes are recorded rather than written.** An
aggregate holding an *array* member has no by-value parameter list, because
8.3.5p5 adjusts that parameter to a pointer and the member is the array, so a
prvalue of such a class - `Holder{{1, 2}, 3}`, a braced argument of that type,
an element of an array of it - is refused with a message that says so. Matching
the reference means a function type holding an unadjusted array parameter, a
`describe_parameter` case writing `ptr [pass=decay]`, a `ptr` slot for the name
and an array temporary at the call. And the reference passes a class holding a
*bit-field* by address where we carry it as bytes; `g++` passes it in a
register, so `g++` and we agree against the binary, and the fact is 5.2.2p4's
`carried_by_bytes` rather than 8.5.4's.

C9's sweep added three more disagreements with the reference binary. g++
settles two of them our way: a delegating mem-initializer that spells the class
through a **typedef-name** of its own (`Alias() : Self(3) {}`), which the
reference answers with an empty constructor body that initializes nothing, and a
copy constructor **defaulted out of class**, which 8.4.2p5 leaves user-provided
and so non-trivial - `__is_trivially_copyable` agrees with us and the reference
lowers the copy as `copyobj`. The third goes the other way and is left as it
stands: g++ refuses a ctor-initializer that names two members of one union and
we and the reference both accept it, writing both stores into the one storage.

C9's audit added four more, and three of them are one reading. **12.4p8's
`non-variant`** makes a union whose destructor writes no statement vacuous here
and not in the reference: `union U { int a; D d; U() {} ~U() {} };` is passed
and returned by value and needs no call at any end, where the binary passes it
by address and calls a `~U` whose body it has itself written empty - and the
same reaches a class holding such a union as a named member. It is the recorded
12.4p8 gap one step further, and g++ cannot settle it because its rule is
12.4p5's triviality, which calls the empty `~T` the fixtures already say we do
not call. The fourth is presentation: an **`inline`** `= default` written outside
the class is emitted here only where something odr-uses it, and 12.1p5 leaves
nothing to; the reference and g++ both write the weak definition anyway. Two
older shapes the audit's probes turned up and left as they stand, both
predating C9 and both with g++ on our side: 8.5p7's **zero** in front of a
default constructor the standard defines, which the reference omits, and a dead
address the lowering takes for an **anonymous union** member it writes nothing
into.

C10's sweep left three more, all judged and left as they stand. **The
reference's own end-of-lifetime answer is contagious**, which is what bounds
`declared_destruction` above: it writes the call for a by-value parameter of a
class whose destructor is declared and trivial only where some *other* use in
the same unit already demanded that destructor's definition - `void qb(B b) {}`
alone writes none and the same function beside `delete (B*)p` writes one - so
5.2.2p4's boundary keeps 12.4p5's triviality and does not join the question the
temporary asks. The same contagion is what makes 13.3.3.1.5p5's braced
temporary the one exception on the temporary side. **`W w(s)` where `W`'s
constructor takes a by-value `R` and `s` reaches it through a conversion
function is refused here** - "an object of the class type struct R is copied" -
and accepted by the reference; it predates C10 and is the by-value parameter
object of a *constructor* argument, not of a call. And **`V v = c ? a : b;
return v;`** puts `v` in a slot and moves it into `%ret` where the reference
gives the declaration the destination straight away: `return_slot_local` reads
the outermost block's declarations, and this one's initializer is the hand-off
`place_class_object` would have made.

C10's audit added three more it judged rather than changed, and one it took
back. **We elide through 5.2.9p4's cast and the binary does not**, so
`V v = (V)V(3)`, `sink((V)V(3))`, `return (V)V(3)` and `const V& r = (V)V(3)`
each build one object here where it builds a temporary, copies and destroys; C7
settled that reading against the fixtures. **A destructor whose empty body is
written outside the class is vacuous here and by-address there** - the binary
gives `struct T { ~T(); }; T::~T() {}` `ptr [pass=by_address]` and the identical
class writing `~T() {}` inside `obj<4x4>`, which is neither 12.4p5's triviality
nor 12.4p8's body, and `g++` carries both by address. And **12.8p32 reads an
rvalue reference and not an lvalue one**: the binary moves out of `V&&` named
by a parameter or a local and copies out of every `V&`, which is C++20's
implicitly-movable entity, and we follow it there while C++11's wording would
copy out of both. What it took back is the fourth: an lvalue reference was being
moved out of here, against every oracle.

C11's sweep left five, each judged and left as it stands. Two go our way with
g++ beside us: an array member of a class whose copy constructor is
user-provided but whose object holds nothing (`struct Owner { Empty v[2]; };`)
is one the binary copies with `copyobj` - `__is_trivially_copyable` says it is
not - and the temporary `static_cast<const W&>(t)` binds is one the binary never
destroys, where 12.2p3 and g++ both end it at the end of the full-expression.
One is our own reading of scale: an array member of more elements than
`kArrayLoopLimit` is one loop here and the bound's worth of calls there, which
is the same trade 12.6p1's construction already made. Two are presentation: a
non-class array member out of the leading storage run is one `copyobj` of its
bytes here and one load-and-store per element there, and where a conditional
stands under a by-value argument the binary opens a region around the argument's
naming that we open in the arm that needs it. Three older shapes the sweep's
probes turned up and left, all predating C11: `(const int&)e` over a *nested*
array writes a second `unary decay` we do not need, the binary opens no region
around a declaration's own initializer where we open one after the storage was
named, and a region whose handler owes nothing is still written where a call
stands in front of the lifetime it began.

C11's audit added four it judged or recorded rather than changed. **A reference
to a type that is not a class binds the operand's own storage where 8.5.3p5 binds
a temporary holding the conversion of it** - `(const int&)d` over a `double`
passes the address of the `double` and `(const int&)wide` the address of the
`long long`, where `g++` converts into a slot of its own and passes that, and an
argument written `use((int)wide)` already gets exactly that (`refarg`) from us.
The binary is wrong in its own direction, converting the value and then passing
the value where a pointer is wanted. It is the one shape of C11's cast reading
left unwritten, and what it needs is a fact of the node saying which of 5.4p4's
readings a cast is, because the lowering cannot tell an 8.5.3p5 binding from a
`reinterpret_cast` from the types alone - a checkpoint's work, and silent wrong
code until then. **A transfer into 6.6.3p2's returned object opens no 15.2p2
region**, so `V ret(V& r) { T t; return r; }` writes no handler for `t`: that is
the rule C11 landed, the binary writes none either, and
`400-conditional-prvalue-member-temporary-lifetime` pins it - `g++` writes the
cleanup, so it is the fixtures' reading and not a judgment left open. **The
elements a local array has already built are no standing objects while the rest
of it is being built**, so `T a[3]` writes no handler between the elements; the
binary writes none at any size and `g++` does. And the handler for an array a
declaration named **names that array once** where the binary re-takes its address
and decays it per element, which is presentation and two lines fewer per element.

The sweeps of C8 and of its audit left these disagreements with the reference
binary, and g++ settles every one our way: 8.5.4p7's narrowing of `f({1.5})`
into an `int` member; 8.5.4p3's refusal of a copy-list-initialization that
chooses an `explicit` constructor; 13.3's ambiguity of `H({a, b})` where `H`
declares both `H(A)` and a copy constructor; a braced-init-list passed to an
ellipsis, which the reference answers with an array temporary it decays and
which g++ refuses as we do; and four shapes we accept and it refuses - a list
two aggregates deep with both sets of braces left out, `new Out{1, 2, 3}`,
`Out({1, 2, 3})`, and `f({{}, 7})` over a class whose first member holds
nothing. Two more are presentation: the reference opens an empty
`eh_try`/`resume` region around a call standing beside a by-address argument the
callee owes the destruction of, and it builds an `Empty` temporary to pass to a
member constructor where we value-initialize the subaggregate with its default
constructor and write one call fewer.

## Active Checkpoint

None open. C11 is complete and audited at `4eb9f73b`, at 227 / 230 with pa1-pa16
unchanged: five blockers found and fixed, three of them older holes the
checkpoint's new answers reached.

The next checkpoint is **C12: 8.5p16's direct-initialization of a class from a
class**, at 1 fixture and the two shapes the sweeps left beside it. Its owner is
`sema_lifetime.cpp`'s `construct_object` with `sema_overload.cpp`'s
`standard_only_`: the fixtures let `T(e)` reach 13.3.1.4's `explicit` conversion
function of the *operand's* class, which the copy-initialization of the chosen
constructor's parameter does not, so what the direct-initialization's `direct`
flag reaches has to be told from what the argument's own sequence reaches. Its
data flow is that one flag, already threaded, read one level lower - and the
object the conversion hands back, which the reference materializes and *calls*
the move constructor on rather than eliding, because the conversion returns its
object as bytes; `elide_transfer` is where that is asked and
`bytes_stand_for_object` is what it has to ask beside `creates_its_object`.  Its
expected complexity is O(1) per initialization - one flag and one type read, no
second walk of the candidates.  Its validation is
`400-direct-init-class-explicit-conversion` plus a differential sweep of `T(e)`,
`T t(e)`, `T t = e` and `static_cast<T>(e)` over a source class whose conversion
function is `explicit` and one whose is not, over a target carried by its bytes
and one carried by address.  `sema_cast.cpp` is now where 5.2.9p4's cast is read,
so the `direct` flag is threaded from there and not from `sema_expression.cpp`.

The shape to probe beside it is the one C11's audit recorded and left: **which of
5.4p4's readings a cast is has no fact of its own**, so a reference to a type
that is not a class binds the storage the operand named where 8.5.3p5 binds a
temporary holding the conversion. That is the same `direct`-vs-copy question one
level out, and C12 is where both are settled if it is taken with it.

## Performance Model

One line per invariant, and the measurement that holds it. Best of three on this
host, at 250/500/1000/2000 unless said otherwise.

- 12.8's classification is one walk of the class's constructor chain and one of
  the declarations of `operator=` the class's **own region** holds, done once
  where 9.2p2 completes the class. Asking 3.4 for that name instead reaches a
  base's declaration and chains this class's member onto it, which was O(n²) in
  the depth of an inheritance chain: 0.91 s at 2000, and 0.14 s now. 12.8p11's
  deletion and p12's triviality are one further walk of the subobjects. n
  classes each declaring nothing and each copied and assigned are
  0.09/0.16/0.32/0.61 s.
- 12.8p15's definition is one node per subobject, and the leading run whose
  bytes a copy carries exactly is one `copyobj` however many members it covers.
  A class with n such members and one non-trivial tail is **29 lines of output
  at every size**, in 0.00/0.00/0.01/0.02 s. A class with n members that each
  need a call is 10 n lines and 0.02/0.03/0.07/0.14 s; a base subobject in
  front of them costs 5 n lines and 0.01/0.03/0.07/0.10 s.
- **12.8p15/p28's array member is one step whatever the bound is.** A class with
  one array member of n elements, copied, moved and assigned both ways, is
  **413 lines of output and 11 ms at every one of 250/500/1000/2000** - the
  analysis writes one step per transfer and the lowering writes one loop, so
  neither the tree nor the text grows with the bound. Below `kArrayLoopLimit`
  the elements are written out, which is the 50 lines per element the reader
  asked for: 376/576/976 lines at 4/8/16. n such members of two elements each
  are 236 n lines in 0.14/0.27/0.53/1.00 s, and an array member of a class whose
  element has an array member n levels deep is 168 n lines in
  0.013/0.013/0.014/0.015 s at 4/6/8/10 - linear in the member count and in the
  nesting, not exponential in either.
- **One element's run of a step written once costs the objects that run created
  and nothing else.** `close_element_run` erases what the run placed rather than
  restoring a copy of the two maps, so the cost is per-element and not per
  object the function has placed so far: n array members of two elements whose
  element's `operator=` takes a **by-value** parameter - one argument object per
  element - are 170 n lines in 0.10/0.19/0.38/0.74 s, where reading the first
  element's object for the second refused the program outright at every element
  count between 2 and `kArrayLoopLimit`.
- **3.8p1's end of an array in 15.2p2's handler is the array's own walk and one
  entry.** n local arrays of four elements, each standing across a call, are
  65 n lines in 0.052/0.089/0.163/0.321 s - four ends per array per handler where
  the reading that wrote one end per array was 55 n lines and
  0.042/0.075/0.141/0.271 s, and three of the four elements leaked. It stays
  linear because 12.6.2p10's chain holds: a handler ends one object and names the
  block before it. n `new T[20]`/`delete[]` pairs with an object standing are
  96 n lines in 0.053/0.096/0.189/0.361 s, two lines per pair more than the
  cleanup that resumed without ending it.
- **8.3.6p9's default argument is evaluated once per element, and that is one
  more call and no more walks.** n local arrays of three elements whose
  constructor has a default argument creating an object are 68 n lines in
  0.050/0.086/0.170/0.314 s, against 49 n lines and 0.042/0.074/0.137/0.267 s
  when the object the first element built was passed to all three.
- **8.5.3p4's cast to a class reference costs one conversion and one temporary
  per cast.** n casts `(const W&)s` through a conversion function of `s`'s own
  class are 19 n lines in 0.022/0.035/0.061/0.111 s, where spelling the call as
  the lvalue the cast is left 12.2p3 an end for an object with no storage and
  **crashed the lowering at every size**.
- The mark a temporary's step is written from is moved once, not searched for,
  so 15.2p2's regions cost what they cost. n temporaries in one function are
  22 n lines in 0.017/0.030/0.057/0.113 s, which is what the same function cost
  before this checkpoint (0.112 s at 2000) to the millisecond.
- **12.3.2's conversions stay on the class that declares them.** A hierarchy n
  deep where every class declares one is 0.01/0.03/0.07/0.17 s and **17 lines
  at every size** - the same shape as the chain of the same depth whose root
  alone declares one (0.00/0.01/0.04/0.11 s, 18 lines) plus a constant per
  declaration. Copying each base's list down instead would give that hierarchy
  n²/2 entries; `conversions_above` chains the classes that declare any, so
  building is O(1) per class and 13.3.1.5's candidate set is gathered once per
  question asked.
- 13.3.1.5's candidate set is one walk of that list per conversion asked, and
  13.3.3.2p3's second standard sequence is what orders it, with the implicit
  object argument telling apart only the candidates that get equally far - one
  comparison each, no second walk. 13.3.3.1.2p1's second sequence is measured
  under `standard_only_` in **both** directions, so two classes whose
  conversions reach each other are one probe and not a walk that does not end.
  One class declaring n conversions is 0.01/0.03/0.06/0.14 s and 30 lines; n
  uses of one conversion are 0.00/0.01/0.02/0.04 s and 4 n lines; n
  direct-initializations of a class from a class through 13.3.3.1.2 are
  0.01/0.02/0.05/0.11 s and 11 n lines. **n uses of a class that declares n
  conversions is the one quadratic shape here** - 0.05/0.12/0.36/1.18 s - and
  it is 13.3.1.5's own rule, which makes the candidate set of every conversion
  asked the whole list: 8.3.5p1's ref-qualifier neither adds to it nor changes
  its order (2 n conversions and 2 n uses are 0.09/0.26/0.84/3.51 s, four times
  the work at every size).
- **8.3.5p1's ref-qualifier costs one field of the function type and one probe
  per candidate.** 13.1 tells two of them apart by the same interned signature
  the cv-qualifier-seq already stood in, so a class that declares both spellings
  is declared in two steps and not four; 13.3.1p4's viability and 13.3.3.2p3's
  ordering are read off the match the object pointer already made. n classes
  each declaring `f() &` and `f() &&` and each calling both are
  0.04/0.09/0.19/0.40 s and 32 n + 7 lines; one class declaring n such pairs
  with all 2 n calls written is 0.02/0.05/0.10/0.22 s and 22 n + 17 lines. A
  hierarchy n deep whose root declares `&`, `const &` and `&&`, called three
  ways, is **54 lines at every size** in 0.01/0.01/0.04/0.11 s - the same time
  and the same shape as the identical hierarchy with no ref-qualifier at all
  (0.00/0.01/0.04/0.11 s, 47 lines), so the fact is carried and never walked
  for.
- **13.1p2 is a fixed number of reads of the chain's index, and 7.3.3p14's
  hiding is one signature per brought-in declaration.** 13.1p2 is keyed on the
  name and the parameter-type-list, so a declaration asks for the other
  ref-spelling under each of the four cv-qualifications - eight probes where it
  wrote no ref-qualifier and four where it wrote one - and never walks the
  declarations already made: one class declaring 2000 unqualified member
  functions with all of them called is 0.01/0.02/0.05/0.10 s and 10 n + 9 lines.
  A using-declaration copies the base's type once with the derived class's
  object parameter in front and both qualifiers still on it, so n members
  brought in by n using-declarations are 0.01/0.03/0.06/0.13 s and 11 n + 9
  lines, and a using-declaration chained n classes deep writes **41 lines at
  every size** in under 0.01 s at 400.
- **5.3.4p1's array form is one loop however many elements there are, and
  5.3.4p6's count is a fact and not a sentinel.** `new T[N]` for a class with a
  constructor and a destructor, followed by `delete[]`, is **110 lines and
  0.01 s at N = 100, 1000, 10000 and 100000**. `Fact::counted` is what says the
  bound is one the translation knows, so a bound of zero is a bound like any
  other and never a count read back out of the bytes at run time. A local
  `T a[N]` still writes 8.5.1p7's elements out to `kArrayLoopLimit` (49/89/169
  lines at 4/8/16) and becomes one loop past it (47 lines at 32), which is the
  form the checked-in fixtures ask for there. n array `new T[3]`/`delete[]`
  pairs are 0.04/0.07/0.14/0.28 s and 83 n lines; n whose bound is a call are
  0.05/0.09/0.17/0.33 s and 91 n lines.
- **8.5p7's zero over an array is one instruction where the extent is one the
  translation knows.** `new int[N]()` is **15 lines and 0.01 s at N = 100,
  1000, 10000 and 100000** - one `zeroinit` over the extent - and a bound the
  translation does not know is the one case that is a byte loop, over
  `bytes - 8` where the count stands in front of the elements. An array of a
  class whose 12.1p6 default constructor is trivial is the same zero and no
  call at all: `new Triv[N]()` is **27 lines** at those four sizes. What decides
  both is `vacuous_construction`, which is 12.4p8's walk of the subobject tree
  the other way round and held per type: 20 objects of a class whose members
  nest n deep under `new L[4]()` are **26 lines at every depth** in
  0.01/0.01/0.02/0.03 s at 50/100/200/400.
- **5.3.4's lookup is one probe of a region and one walk of a name's chain.**
  3.7.4.1p2's four declarations are made once before the unit is read, so a
  program that writes none pays four declarations in all; 5.3.4p9's class
  lookup is 10.2's, and a hierarchy n deep whose root declares
  `operator new`/`operator delete` with the allocation written at the leaf is
  **30 lines at every size** in 0.02/0.03/0.04/0.08 s - the same time as the
  same hierarchy with no allocation function in it, so the lookup costs the
  depth the source wrote and nothing more. n scalar new/delete pairs are
  0.02/0.03/0.06/0.11 s and 15 n lines; n classes each declaring their own
  `operator new`/`operator delete` and each using them are 0.06/0.11/0.22/0.44 s
  and 36 n lines.
- **5.3.4p6's bound is read for a constant once and as a value once.** The fold
  is asked before anything is written, so a bound it answers leaves no
  arithmetic in the tree at all and one it does not is read as an expression -
  two readings of the operand and never more. Both are linear in its nesting
  depth: conditionals nested n deep inside a bound are 0.01/0.01/0.01/0.02 s and
  14 n + 17 lines at 50/100/200/400.
- **3.7.4p2's spelling costs one scan of a name and no search.** `operator new`
  and `operator delete` are the only names an id-expression writes a space in,
  so every other name is answered by one `find(' ')` before any substring
  search runs: n distinct long local names, each declared and used, are
  0.01/0.01/0.03/0.06 s and 6 n lines.
- **12.6.2p6's delegation costs one call and no walk, and its cycle check costs
  one colour per delegating constructor.** n classes each with one delegating
  constructor and one use are 27 n lines in 0.03/0.07/0.15/0.32 s. A chain of n
  delegations *inside one class* is 22 n lines in 0.05/0.21/0.85/3.52 s, and the
  same class with n constructors that delegate to nothing and n calls of them
  written instead is 0.06/0.22/0.91/3.80 s for the same shape - so the quadratic
  is 13.3.1.3's own candidate set (n resolutions over n constructors) and the
  delegation adds nothing to it. The chain written in **reverse** source order,
  where every target is a constructor the read has not reached, is
  0.05/0.21/0.88/3.60 s - the same time, which is what says the check is one
  colouring at the end of the unit and not a walk per definition.
- **12.6.2p8's union reading is one probe of the class's region per
  mem-initializer.** A union with n members and a constructor naming the first
  is **36 lines at every size** in 0.00/0.00/0.00/0.01 s: the walk asks each
  member what initializes it and leaves every variant member no
  mem-initializer and no brace-or-equal-initializer reached alone, so a union of
  2000 members writes the one store the program asked for.
- **9.5p1's one storage is what keeps every other walk of a union constant in
  its member count too.** 12.8p15's transfer is one copy of the object
  representation and 12.4p8's destruction is no member at all, so a union with n
  **class** members - each with a constructor, a destructor and a copy - copied
  and destroyed is **39 lines at every size** in 0.00/0.00/0.00/0.01 s at
  250/500/1000/2000. Walking the declarations instead was 33 n + 412 lines and
  0.01/0.03/0.06/0.12 s, and every step of it but the first wrote over the bytes
  the step before it carried. A union of n **scalar** members is 42 lines either
  way, because the leading trivial prefix already carried it in one piece.
- **8.4.2p2's out-of-class `= default` costs one entry in the index 3.4.1p8
  already builds, and one definition per line the program wrote.** n classes
  each with a constructor and a destructor defaulted outside the class, each
  used, are 30 n lines in 0.02/0.05/0.11/0.24 s - and the same program with
  every definition written *after* `main` is the same time and the same output,
  because `collect_unit_definitions` takes those declarations from the syntax
  once and `note_definition_body` asks one probe per class. n classes defaulting
  all four of a constructor, a destructor, a copy constructor and `operator=`
  outside the class are 65 n + 4 lines in 0.06/0.13/0.25/0.52 s at
  250/500/1000/2000; 13 n of those lines are the assignment operator's
  definition, which 3.2p4 makes this unit's and which no unit wrote before.
- 12.4p8's `vacuous_destruction` is one walk of the subobject tree, held per
  type: 20 objects of a class whose members nest n deep, each with an empty
  destructor, are 0.01/0.01/0.02/0.03 s at 50/100/200/400 and **19 lines at
  every size**, because what the walk answers is that there is nothing to
  write. Its one input the read has not settled yet - what an out-of-class
  definition's body writes - comes from **one** walk of the unit's syntax, done
  before the first declaration is read and keyed by the unqualified name each
  definition names, so a class asks one probe and never a scan. n classes whose
  destructors are defined after their uses are 0.02/0.04/0.10/0.22 s and 21 n
  lines, which is the same time and byte-identical output to the same program
  with the definitions written first.
- **5.2.2p4's boundary is one probe of two flags per parameter, and what it
  owes at the other end is one destruction per by-address parameter per exit.**
  n functions each taking two such parameters, each with two returns, and each
  called are 35 n lines in 0.06/0.13/0.23/0.46 s; one function with n such
  parameters is 5 n lines in 0.00/0.01/0.01/0.02 s; one with three of them and
  400 returns is 16 n lines in 0.03 s - the source's own k x m and no walk of
  the parameter list per return. n constructors each taking one are 27 n lines
  in 0.07/0.13/0.27/0.51 s, and n nested calls each passing a class by address
  are 3 n lines in under 0.01 s at 200, so the handler the parameter stands
  under is linear in nesting depth and not exponential in it. n standing objects
  each with such an argument under them are 21 n lines in 0.03/0.06/0.13/0.27 s.
- **12.8p12 and 12.4p8 are one settled field of the class, so the boundary, the
  return, the copy and 12.8p15's definition each cost one read.** The vacuity
  half is `vacuous_destruction`'s memoized walk, asked once where the class
  completes: n classes with an empty-bodied destructor, each passed by value and
  copied, are 12 n lines in 0.06/0.13/0.26/0.55 s, and members nested n deep
  under a non-vacuous destructor are 38 n lines in 0.01/0.02/0.05/0.09 s at
  50/100/200/400 - linear in the depth, which is what says the answer is held
  and not walked for at each use.
- **15.4p14's exception-specification costs no walk of its own.** The four
  transfer members take it inside the walk `settle_transfers` already makes over
  the base and the members, and 12.4p3's destructor takes one walk of the same
  two. An inheritance chain n deep, copied and assigned, is 63 n lines in
  0.02/0.04/0.08/0.15 s at 50/100/200/400.
- **8.5p8's answer is one field of the class, settled in the layout walk beside
  9p6's `empty`, so a class asks its subobjects once and never again.** A chain
  of classes nested n deep whose innermost member is an empty class with a
  constructor is 11 n lines in 0.01/0.03/0.06/0.13 s at 250/500/1000/2000 -
  linear in the depth, which is what says the answer is carried and not walked
  for at each use.
- 5.2.9p4's cast writes one temporary node per cast and no second reading of the
  operand: n casts of an integer to a class with a converting constructor are
  8 n lines in 0.01/0.02/0.04/0.08 s.
- 6.6.3p2's boundary is one probe of the type, and 12.8p31's result object is
  one operand threaded down the initializer - no node is read twice and no
  instruction is rewritten once written. **The threading is linear in nesting
  depth, not exponential in it**: n nested calls each returning and each taking
  a class by value are 3 n + 14 lines in under 0.02 s, and conditionals of
  class type nested n deep are 11 n + 19 lines in 0.02 s.
- 12.8p31's return-slot local is settled by **one** walk of the function body,
  done once per function that returns indirectly and only then, and reading past
  3.8p1's actions costs one kind test per action. n functions each with two
  returns of one local are 0.02/0.04/0.09/0.18 s and 18 n lines; one function
  with n returns of one local is 0.01/0.01/0.03/0.06 s and 11 n lines - the
  same time as before the elision reached them, with the output the elision
  leaves. n functions each returning a named local of a class with a destructor
  are 0.01/0.02/0.05/0.10 s and **5 n lines where they were 22 n**, which is
  what the elided copy and the elided end come to.
- **5.16p3's selection is one transfer per leaf and one probe per hand-off.**
  n conditionals each with two glvalue arms, each returned by value, are
  0.02/0.04/0.08/0.17 s and 25 n lines; a conditional nested n deep, whose
  n + 1 leaves are all glvalues, is 12 n + 19 lines in 0.00/0.00/0.02/0.04 s at
  100/200/400/700 - **linear in the nesting the source wrote, not exponential in
  it**, because an arm standing in for the conditional is what stops
  `selecting_conditional` finding the same one again and each leaf is written
  once. Past 700 the parser refuses the unit and the reference binary segfaults
  on it. **A cast between the destination and the selection is one step of the
  hand-off and not a second one**, so it costs one transfer and not one per arm:
  n declarations `V v = (V)(c ? a : b)` are 0.03/0.05/0.10/0.19 s and 35 n + 253
  lines, with 1 copy per selection where asking the question below the hand-off
  wrote 2. One class with n class members each initialized from a call is
  0.02/0.03/0.05/0.09 s and 12 n lines.
- **12.4p3's answer is one field of the class**, settled where it completes off
  the base and the members it already walks, so a temporary of a class whose
  members nest n deep asks one read: 20 objects of such a chain, each class
  declaring a destructor, are **31 lines at every depth** in 0.00/0.00/0.00/0.01 s
  at 50/100/200/400, and one temporary of a chain n deep is **46 lines at every
  depth** in 0.01/0.01/0.03/0.07 s. What the action says is what the lowering
  writes, so the end is one call on both paths out: n temporaries of a class
  whose declared destructor is trivial are 16 n + 22 lines and
  0.02/0.03/0.05/0.10 s, one line per temporary more than the reading that wrote
  the end only into 15.2p2's handler.
- 12.8p31's elision of the transfer 13.3 chose is one probe of the chosen
  constructor and of the argument already converted - no second resolution and
  no second reading of the initializer: n direct-initializations of a class from
  a class through a conversion function are 0.01/0.02/0.05/0.11 s and 10 n lines.
  Taking the elided object out of 12.2p3's frame is one walk of the casts written
  over the prvalue, which is one step in every shape the milestone has: n
  declarations `V v = (V)V(3)` with a use of each are 20 n + 22 lines and
  0.02/0.04/0.07/0.15 s, three lines and one destructor call per object fewer
  than leaving it in the frame - and that call was the end of `v`, written before
  the first read of it.
- **A read that answers a question costs the frame it opens and nothing else.**
  `probe_expression` opens and drops one vector per probe, so 8.5.1p11's question
  is asked once per clause it is asked about: an aggregate of n class members
  each written `D()` is 3 n + 7 lines in 0.01/0.02/0.03/0.06 s, where leaving
  what the probe created in the program's frame crashed the lowering at every
  size.
- 12.8p32's first resolution is one read of the class's settled transfer per
  return, so a return costs one probe whether or not it moves: n returns of a
  named local are 0.01/0.02/0.05/0.10 s (the same line above), and an aggregate
  returned by value through 12.8p31's byte copy is 0.01/0.02/0.05/0.10 s and
  10 n lines.
- 9.6p2's storage unit is carried once however many bit-fields share it: n
  one-bit fields are n/32 units, 0.00/0.00/0.01/0.01 s.
- 8.3.6p1's "does this parameter have a default argument" and 8.4.3p2's refusal
  of a deleted function are each one probe of a map or one flag read, so a
  program that copies nothing pays one test per name.
- **12.2p3's boundary costs one frame per full-expression and one node per
  temporary, and 15.2p2's handler costs one region per change in the standing
  set.** n statements each binding a reference to a temporary
  (`byref(make());`) are 0.01/0.01/0.03/0.06 s and 12.5 n lines at
  250/500/1000/2000; n `{ T a; g(); }` blocks are 0.01/0.02/0.04/0.08 s and 16 n
  lines; n `if(use(make()))` statements, each writing 12.2p3's two cleanup
  edges, are 0.02/0.03/0.07/0.14 s and 38 n lines; n
  `T() + (pick ? T() : T())` full-expressions, each holding two temporaries
  across a branch, are 0.03/0.05/0.11/0.23 s and 47 n lines.
- **n standing objects cost n destructions and not n(n+1)/2, in the output and
  in the walk.** `T a0; g(); T a1; g(); ...` is 22 n lines and
  0.02/0.03/0.07/0.14 s at 250/500/1000/2000, because 12.6.2p10's chain holds
  past `kUnwindSuffixLimit` and because **a region keeps no copy of the standing
  list at all**: it keeps the one entry the chain needs, and what an end of a
  lifetime takes out of the list is one entry with the place it stood at.
  Copying the list per region was **1.60 s at 2000**; copying it once per region
  a lifetime ended *inside* was the same shape one step down - n standing
  objects with n temporaries made and ended under them was 0.03/0.08/0.27/**1.13
  s** at 250/500/1000/2000 with the output linear at 34 n lines, and is
  0.02/0.03/0.05/0.09 s for the same bytes now.
- **The list a handler destroys from is the one that stood when it opened, and
  it is rebuilt only where a destruction per object is about to be written.**
  Reading it back off the standing objects at the close read past the end of
  that vector wherever 12.2p3's end of a temporary fell inside the region -
  which is every argument bound to a reference - and a call nested 20 deep
  crashed on it. n nested `f(make(), f(make(), ... ))` are 9 n lines in
  0.01/0.01/0.01 s at 50/100/200, and valgrind is clean over that, over 250
  standing objects with 250 temporaries under them, and over the depth-100
  nestings of an arm, a right operand and a block.
- **A handler block is named again only while what it destroys still stands.**
  The block written for k standing objects is held per k, and a region a
  lifetime ended inside is not held at all: what it destroys is what stood then,
  so a later step with the same count would name a handler that destroys an
  object the program has already destroyed and leaves the one that took its
  place standing. n statements each making and ending a temporary are 19 n lines
  in 0.02/0.03/0.05/0.10 s, which is one fresh handler per statement and the
  reuse the shape allows.
- **12.2p3's frames are linear in nesting depth, not exponential in it, and an
  operand that may not run costs one more frame and no more walks.** Conditional
  arms nested n deep each holding a temporary are 33 n lines in
  0.01/0.01/0.02/0.03 s at 50/100/200/400, `&&` right operands the same, and
  blocks nested n deep each declaring an object are 23 n lines in
  0.01/0.01/0.02/0.04 s. n conditionals each holding a temporary in each arm are
  53 n lines in 0.03/0.06/0.12/0.23 s; n `if` conditions each holding one are
  35 n lines in 0.02/0.04/0.08/0.14 s.
- **13.3.3.1.5's sequence costs one probe of the parameter's class and no
  reading of the clauses.** The clauses are read once, where the type they
  initialize is settled, so a candidate set of n functions costs n probes and
  not n readings of the list: `f({1, 2})` against 2000 one-member aggregates
  and one two-member one is 8 n lines in 0.02/0.03/0.06/0.13 s. n calls each
  passing a list to an aggregate parameter are 7 n lines in
  0.01/0.02/0.04/0.07 s; n calls each passing one to a `const A&` and to an
  `A&&` are 12 n lines in 0.02/0.04/0.07/0.14 s. A list nested n deep through
  n aggregates is 19 n lines in 0.01/0.01/0.01/0.02 s at 25/50/100/200 - linear
  in the depth and not exponential in it.
- **8.5.1p6's capacity is one walk of the subobject tree, held per type**, so
  it counts leaves it never walks: a class whose members *double* at each of n
  levels has 2^n leaves, and `f({})` against it is **27 lines at every n** in
  under 0.01 s at n = 8, 12, 16 and 20 - one walk per class rather than one per
  leaf. Reading the count off the tree at each candidate instead would have
  been 2^n. Bounding a subobject's contribution below at one clause changed
  neither the shape nor the time.
- **8.5.1p11's elided braces are one descent of the clause list, linear in the
  nesting the source wrote.** One flat list through n aggregates nested n deep
  is 24 n + 34 lines in 0.00/0.01/0.01/0.03 s at 25/50/100/200, and the same
  list with a 40-deep chain of calls as its first clause is 24 n + 82 lines in
  0.01/0.01/0.02/0.05 s - the descent probes that one clause once per level and
  the total stays linear, because what a probe reads it reads into a line
  nothing keeps. n calls each passing such a list are 10 n + 60 lines in
  0.01/0.03/0.05/0.10 s at 250/500/1000/2000, which is the same time and the
  same output as the same n calls with every brace written.
- **8.5.1p2's aggregate constructor is one call however many class members it
  carries**, and each is one step: an aggregate with n move-only members,
  returned by value, is 7 n lines in 0.01/0.03/0.05/0.10 s.
- **6.6.3p2's returned object is one slot per function.** A function with n
  returns of a class the ABI carries as bytes is **one `retobj` slot at every
  size** - 13 n lines in 0.01/0.02/0.04/0.08 s at 250/500/1000/2000 - which is
  what the reference writes for the same program.
- 6.6p2's own shape is the one quadratic left, and it is the source's: n
  returns, each leaving n blocks, are n² destructions because each return
  destroys everything standing. It predates this milestone and the reference
  writes the same n².
- **`begins_lifetime` is one walk of an expression and not one per call in it.**
  It is what says a call stands under a handler and needs its value stored where
  a block that handler reaches can name it; the answer is held per node, so a
  chain of n nested calls is walked once.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 12.8's four value-transfer special members: p2/p3/p17/p19's classification, p7/p9/p18/p20's declaration of what the program did not write, p11/p23's deletion and p12/p25's triviality, p15/p28's synthesized definition as one walk of the subobjects with a leading trivial storage prefix, 8.5p14's copy-initialization from a glvalue, 5.2.2p4/6.6.3p2's argument and returned object as objects of their own, 8.4.2/8.4.3 on an ordinary member declarator, and 8.4.3p2's refusal of every name of a deleted function. Audited at `c2894e79`: six blockers found and fixed | 61 -> 86 -> **88 / 228**; pa1-pa16 1494 / 1494 |
| C2 | 6.6.3p2's returned object and 12.8p31's result object: `TypeTable::returns_indirectly` as one fact of the type, `open_signature` as the one writer of the boundary a declaration, a definition and an indirect call all read, `%ret [pass=indirect_result]` bound in the body, and result-object placement threaded through initialization, return, argument, conditional arm and discarded value. 12.8p31's return-slot local settled by one walk of the body; 12.8p32's access check for the copy the elision removed; 1.9p12's operand of an empty-class transfer still evaluated where evaluating it is observable. `lowir_lower_body.cpp` split at the statement/expression seam. Audited at `be9d930d`: seven blockers found and fixed, which added 5.16p3's initialization of a conditional's result object, made `creates_its_object` the one answer both layers read, and brought 3.6.2p2's namespace-scope initialization into the same hand-off | 88 -> 112 -> **117 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C3 | 12.3.2's conversion functions, end to end. The conversion-type-id travels with the name as `CarriedTypeId`, so 12.3.2p1's "two spellings of one type are one function" holds at the declaration, at the out-of-class definition and at `a.operator T()`; the class holds its own conversions and chains the classes above it; the ABI's `cv` terminal names them. 13.3.1.5/13.3.3.1.2 run the user-defined conversion sequence the other direction under `standard_only_`, with 13.3.3.2p3's second standard sequence ordering the candidates; 8.5.3p5 binds a reference to the lvalue a conversion returns; 4p3 and 6.4.2p2's contextual conversions answer a condition, a condition-declaration, `!`, `&&` and `?:`; 5.2.9p4/5.4p4/8.5p16's explicit conversion is allowed with 13.3.1.5p1's qualification-only restriction; 13.6's built-in candidates are gathered from the operand's class and ranked against the operator function 13.3 chose. Two lowering defects the group exposed: 5.7p5 read `n + p` as a pointer difference, and 5p4's left operand was read after the right ran. 12.4p8's `vacuous_destruction` made the end of a lifetime one question. Audited at `8c59f91a`: six blockers found and fixed, which set 13.3.3.1.2p1's one-conversion flag in the converting-constructor direction too, moved 8.5.3p5's hook above the refusal of a temporary, took 12.4p8's `empty_body` out of the unit's syntax instead of out of the read order, ordered 13.3.1.5's candidates by where the conversion gets to, gave a cast no conversion answers a refusal instead of the object's bytes, and let 13.6p3/p5's `++E` be reached | 117 -> 149 -> **149 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C4 | 8.3.5p1's ref-qualifiers, end to end. The ref-qualifier is a field of the *function type*, interned beside 8.3.5p7's cv-qualifier-seq, so `declarator_type` writes it once and the declaration, the out-of-class definition, `member_signature`'s key, a using-declaration's brought-in copy, the Itanium name and a pointer to member all read the one fact; 8.3.5p6 refuses it on a non-member, a static member, a constructor and a destructor, and 13.1p2 refuses a set that mixes it with the unqualified spelling under any of the four cv-qualifications, because 8.3.5p4's parameter-type-list is what the rule is keyed on. `Value::object_category` carries what 9.3.1p3's pointer drops, and `object_match` is where 13.3.1p4's viability and 13.3.3.2p3's ordering are read off it - reached by a member access, a call with no object expression, an operator's left operand and 13.3.1.5's own candidate set alike, so a conversion function's ref-qualifier is ranked by the same question; 5.2.5p4 is what a member access hands it, which makes a reference member an lvalue before it makes a subobject an xvalue. The Itanium `R`/`O` qualifier joins `K` and `V` in the object name, and the two qualifiers written after the parameter-clause are spelled on the type the declarator wrote and not a second time on the form 9.3.1p3 lowered. 9.3p2's sibling hole closed with it: a definition written with a qualified declarator-id defines a declaration that region already made, so `int X::f() &&` against a declared `int X::f() &` - and equally a mistyped parameter list or cv-qualifier-seq - is refused rather than declaring a second member. Audited at `9f693145`: four blockers found and fixed, which carried the ref-qualifier through a using-declaration's rebuilt type, spanned 13.1p2's probe across the cv-qualifications, put both qualifiers back in PA11's description and took the ref-qualifier out of PA12's, and gave a reference member 5.2.5p4's lvalue category | 149 -> **163 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C5 | 5.3.4 and 5.3.5, end to end, in `sema_allocation.cpp`. 3.7.4.1p2/3.7.4.2p2's four functions are declared in the global namespace before the unit is read, so a program that writes one redeclares it and the object file names it `cppgm_builtin_operator_*`; `operator new` and `operator delete` are the two operator-function-ids a use spells with a space, and the id-expression's spelling is packed to the one the declaration is bound under. 5.3.4p6's first array-suffix is read as the one bound that need not be a constant, the bytes are scaled in the count's own type and reach the parameter through 5.2.2p4's conversion, the ABI's count is written in front of an array of class type and read back off it by `delete[]`, and 12.6p1's construction is one loop with 15.2p2's handler and 5.3.4p18's deallocation behind it; 8.5p7's `()` over a scalar array is one byte loop. 5.3.5 is 12.4p3's end of a lifetime and 3.7.4.2's return of the storage, with 5.3.5p9's lookup, 12.5p4's usual deallocation function, 5.3.5p2's `contextual_pointer` and the null test written where a class type leaves something to guard. 15.4p1's `nonthrowing` joins the declaration, and 5.3.4p15's test is written where 15.4 *and* 18.6.1.1p3's `std::nothrow_t` both say the call reports failure with a value. Three defects the group exposed: 9.4.2p2's second line describing one object let a declaration write the image, so 3.1p2 became a fact of the line; 8.5p14's initialization folded a widening to an unsigned type the rest of 4 spells out; and 3.9.1p2's two eight-byte integral types share one LowIR spelling and are still two types a copy stands between. Audited at `6c785249`: seven blockers and one refusal found and fixed, which made 5.3.4p6's count a fact of its own rather than a zero sentinel, wrote 8.5p7's zero as one instruction over an extent the translation knows, gave 8.5p7's value-initialization of an array the vacuity exit 8.5p6's already had, made `vacuous_construction` 12.4p8's walk of the subobject tree the other way round, settled 5.3.4p15's test once for both forms, named 15.2p2's cleanup destructor by the ABI's complete-object entry, put `unwind=no` back on every reserved builtin, and let 5.3.5p5's incomplete class be deleted rather than refused | 163 -> **186 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C6 | 12.2p3's full-expression boundary and 15.2p2's handler in an ordinary body. `sema_lifetime.cpp` holds one frame per open full-expression and `Fact::object` makes 12.2p1's object a fact of the node that produced the prvalue, so a call and a conditional that hand one back name the object every reader reaches; 12.2p5 moves a temporary a reference binds into the block that declared the reference rather than leaving it in both places, and 6.4p3's condition-declaration becomes an object of the region the selection statement opened, so a path that never reached the declaration no longer destroys what it never built. 12.2p3's end is written where the full-expression ends - inline for a statement, an initializer, a mem-initializer and a loop-continuation portion, and on the two edges out of a condition, which is what makes an `if` whose condition holds a temporary lower its `&&` as a value rather than as its own control flow. The handler machinery 15.2p2 gave 12.6.2's subobjects now covers every object standing in a body: a region opens where the step that made a throwing call began, closes where the standing set changes or the full-expression does, is written again on each block a step spans, owes what stood when it *opened* rather than at its close, and names a block an earlier step wrote wherever the two owe the same objects. Four defects the group exposed: 15.4p1's exception-specification was read off no constructor's, destructor's or conversion function's declarator; 12.4p5 did not join 12.8p12 in saying which classes the ABI hands back in registers; 5.16p3's prvalue arm was ended twice, once as the conditional's result object and once as a temporary of its own; and 8.3.2p5's condition declaring a reference read no conversion function of the class it named. Audited at `67babaa8`: eight blockers found and fixed, three of which wrote a destructor call for an object that was not standing - the region closing after 3.8p1's ends rather than in front of them, a handler block named again after the objects it owed had been destroyed, and 5.14p1/5.16p1's conditionally-evaluated operands having no frame of their own; the fourth was the standing list copied once per region a lifetime ended inside, which was quadratic under linear output; and the machinery was split into `lowir_lower_unwind.cpp` at 15.2p2's own seam | 186 -> **194 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C7 | 5.2.2p4's class argument at the boundary, and the placement facts swept beside it. `TypeTable::passes_indirectly` is 6.6.3p2's other half and `describe_parameter` the one writer a declaration, a definition and a call through a pointer all read: a class the ABI cannot carry as bytes is `ptr [pass=by_address]`, the caller passes the address of the object it built, the callee reaches it through that address, 8.5.3p5 names the storage `arg`, and 12.4p5 says the function owes its destruction at every return and where the body falls off its end. 4.2p1's decay of an array a reference names is marked where the name stands. 5.2.9p4's cast to a class type became the initialization it is - `construct_object` learned 13.3.1.4's question about the place that asked, `creates_its_object` reads the temporary standing under the cast so 12.8p31 elides through it, a conversion function of the operand's own class is reached where one exists, and a cast to a reference names the temporary it binds `refcall` while an argument around it binds the glvalue rather than materializing one. 8.5p8's zero became one over the storage the bases and members hold, with 5.3.4's zero of the storage an allocation obtained told apart from it. The reference binary settled the boundary's own reading of 12.8p12. Audited at `ba854b1d`: eight blockers found and fixed, which made 12.4p8's vacuity the destructor half of every ABI and every copy rather than 12.4p5's triviality, read 5.2.2p4's copy half off the base *and* the members a derived class is laid out over, gave 12.8p12's readers 12.4p8 beside it so a class whose destructor comes to something is copied by the member 12.8p15 defines, computed 15.4p14's exception-specification so those calls need no handler, ended the parameter at every exit the function has instead of one, took that same end out of the caller's handler, made 5.2.9p4's cast of a glvalue to its own class the initialization it is rather than a refusal, and gave `creates_its_object` the cv-qualification both its readers strip | 194 -> **204 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C8 | 8.5.4's braced-init-list of a class, and 13.3.3.1.5's sequence for an argument that is one. `Value::braced` carries the list past 13.3 with the line it will be written on holding its place, `match_list` answers what it converts to from the parameter alone - p3/p4's user-defined conversion sequence keyed on the class it initializes, p5's reference to the temporary named after the argument, p6's one clause - and the clauses are read once, where 8.5.4 has a type to read them for. 8.5.1p6's `clause_capacity` bounds an aggregate candidate and is held per type; 13.3.3.1p4's third bullet keeps a class's own copy constructor out of `X{ {a, b} }`; 5.17p9's `x = {...}` is that call's argument and reaches no built-in operator. `AstNode::braced` tells 5.2.3p3's `T{a}` from 5.2.3p1's `T({a})`, which the tree could not, and over a non-class type the two are one. 8.5.1p2's aggregate constructor took a class member by value and moves it into place; 6.6.3p2's returned object became one slot per function; 12.8p32 now asks for the constructor the copy 12.8p31 elided would have called. Swept differentially against the reference binary and g++ over 26 synthesized units, with the four disagreements judged and recorded, and valgrind clean over the depth-200 nesting and the 2000-return function. Audited at `bc50cabb`: six blockers found and fixed and one shape recorded as a refusal, all of them the one seam where 8.5.4's reading of a list for a class had two owners - 8.5.1p6's capacity gave a subobject that holds nothing no clause; 8.5.1p11's elided braces reached the walk that writes a subobject where it stands and never the by-value parameter list a prvalue is built by; that elision question had no bound, so a clause walked past a subaggregate with nothing to take it; 8.5.1p2's constructor left out a reference member and a bit-field member, which is what the reference-member fixture was waiting on; 8.5.1p15's union got a parameter per member and wrote each of them into the one storage a union is; and 13.3.3.1.5p1's list was a viable ellipsis argument | 204 -> 209 -> **210 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C9 | 12.6.2p6's delegating constructors, 9.5's unions and 8.4.2p2's out-of-class `= default`, which are one boundary: what a ctor-initializer means and who wrote the definition under it. `delegating_initializer` reads the *list* rather than an entry - the class's own name in one probe, any other spelling of it in one lookup and only where p6's own rule already bounds the list to one - and `Placement::Delegate` makes the object the one the constructor is already running on, so `this` is passed to the target's complete-object entry as it stands and no subobject step is written; `SemaEntity::delegates_to` is the edge and p6's cycle is one colouring of the constructors that delegate, run where the unit has been read. 9.5p1's one storage became 12.6.2p8's reading: a variant member's brace-or-equal-initializer is used only where no *other* variant member was designated, one neither reaches is not initialized at all, and 9.5p2's "at most one" is refused in the layout walk. 8.4.2p2's `= default` outside the class is parsed as the definition it is, read against the declaration the class made, left non-inline so this unit holds it, and collected with the bodies so 12.4p8's vacuity is settled from the syntax - which split "who wrote the declaration" from "who wrote the definition" for both readers of 12.1p5's elided call. Swept differentially against the reference binary and g++ over 23 synthesized units, with three disagreements judged and recorded and valgrind clean over the delegation, union and out-of-class shapes. Audited at `39fa3bf7`: four blockers found and fixed, every one of them a sibling of the reader the checkpoint answered at - 12.6.2p6's delegation keyed on the last component of a mem-initializer-id rather than on the class it denotes, so a base spelled `N::S` under a derived `S` was read as a delegation and the program refused; 12.4p8's `non-variant` missed, so a union's destructor called the destructor of every class-typed member it declared, on storage no lifetime began in, and `vacuous_destruction` and `destruction_nonthrowing` answered off that same walk; 12.8p15/p28's transfer walked those members too, writing the trivial prefix and then a copy call per variant member over the same bytes; and 8.4.2p2 reached only the parse path a constructor and a destructor take, so an out-of-class `= default` on an assignment operator was left inline, never demanded and never refused a second time. `one_storage` and `names_the_class` are the two questions that came out of it | 210 -> **217 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C10 | 12.8p31's remaining placement, and which of two questions an end of a lifetime asks. 5.16p3's conditional that selects among glvalues is a selection among *objects*, so a destination the translation named - 6.6.3p2's returned object, 5.2.2p4's argument object, 12.2p1's temporary - is filled on the path that chose the object it reads; `selecting_conditional` is asked at `place_class_object`, the hand-off itself, which is what leaves a destination the *program* declared filled where its declaration stands. 12.8p31's NRVO reached a class the ABI carries as bytes (`returned_name` is the one reading of the operand both spellings of a return make) and reached past 3.8p1's actions standing under the return, because the object the return names is one of them - and `leave_blocks` then writes no end for it anywhere, since with the elision the lifetime the callee began is the caller's to end. 12.8p31's elision of the transfer 13.3 chose became `elide_transfer`, asked once *after* the choice, so an argument reaching the copy constructor through a conversion function of its own class is the object being initialized - and 5.3.4p12's storage is storage a new-expression owns, so the elision reaches it as it reaches a slot. 12.8p32 joined it at both ends: a return naming an automatic object of the function's own returned type is read as an rvalue first, and the definition the elided constructor would have run is still one this unit owes what it would have called. 12.4p3 became `TypeTable::declared_destruction` beside 12.4p8's vacuity, with `ends_in_call` the one place the two meet: an object a declaration named reads what running its destructor comes to, and an object the translation made reads whether the program declared one at all. Swept differentially against the reference binary over the conditional, NRVO, conversion-elision and destruction shapes, with three disagreements judged and recorded and valgrind clean over the depth-200 nesting and the 2000-return function. Audited at `ae1e4567`: five blockers found and fixed, every one of them an object the checkpoint gave an end or a place to that something else was already holding - 8.5.1p11's probe of a clause, and 5.3.3p1's and 7.1.6.2p4's unevaluated operands, left 12.2p1's temporary standing in the program's full-expression, so `K k = {D(), 7}` over a `D` with a destructor crashed the compiler and `probe_expression` is how a question is read now; 12.8p31's elision left the object it elided in 12.2p3's frame wherever 5.2.9p4's cast stood over the prvalue, so `V v = (V)V(3)` destroyed `v` before its first read and `return (V)V(3)`, `new V((V)V(3))` and `sink((V)V(3))` each handed on a destroyed object, which `created_object_node` fixes for every reader at once; `destructor_call` re-derived 12.4p5's triviality over the analysis's own answer, so a temporary of a class with a declared-but-trivial destructor was destroyed only when something threw, and `register_temporary` noted no entry, so an end only 15.2p2 reaches named the base-object destructor of a complete object and left the symbol undefined; 5.16p3's selection was asked below the hand-off as well as at it, so a cast between the destination and the conditional distributed the transfer over both arms; and 12.8p32 read an lvalue reference as p31's automatic object | 217 -> **222 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C11 | 12.8p15/p28's array member, and the two readings a conditional arm makes. The member the element's class has takes an element, so the transfer is written once - for an element, with the source read as one exactly as `write_constructed_object` already reads the destination, the line going on naming the array because that is where the element stands - and the walk around it is what says which element each step is at: `element_walk_` is that one cursor, read by the destination and the source alike (`walks_this_array`/`walked_element`), in a number below `kArrayLoopLimit` and in the loop's own index past it, with `FactKind::ArrayTransfer` giving 12.8p28's assignment the same walk 12.6p1's construction already had. An array whose element is carried by its bytes is the whole array's bytes, which is what a non-class array member out of the leading run had been getting a truncating load of. 5.16p6's arm became a *reading* of the object it names rather than a consuming of it, keyed on the arm's spelling, so a cast to an rvalue reference is still moved out of and 5.2.5p4's subobject of a temporary is not - and 12.8p15's transfer filling an object the translation named opens no 15.2p2 region of its own, while a temporary's storage is named in front of the region already standing around it. 8.5.3p4's unrelated cast to a class reference became the initialization it is: the converting constructor is applied where the operand stood and 12.2p3's frame ends the object it made, which 12.3.2p2's conversion function under a cast to a non-class type joins. Swept differentially against the reference binary and g++ over 30 synthesized units - array members of 1/2/3/64/2000 elements, two-dimensional, under a base, of a class holding an array, mixed with a trivial prefix; conditional arms that are casts, member accesses of prvalues and of xvalues, in returns, declarations and arguments; casts to `const W&`, `W&&`, a base, a derived and a conversion function - with five disagreements judged and recorded and valgrind clean over all of them. Audited at `4eb9f73b`: five blockers found and fixed, three of them one rule the checkpoint's new walk reached - a walk of an array is the same walk for every reader of it, and a step written once is not one run twice. 12.8p28's element walk re-lowered one step per element while `placed_`/`slots_` stayed the function's, so `creates_object` handed the second element the object the first built: an element's `operator=` taking its parameter by value **refused** a valid program between 2 and `kArrayLoopLimit` elements, and 8.3.6p9's default argument was evaluated once for every element. 3.8p1's end of an array a declaration named was one call in 15.2p2's handler where the block's end walks every element, so `T a[3]` leaked two of three on every exceptional path and past the loop limit stood in the list twice. `new T[n]`'s own cleanup owed the elements and 5.3.4p18's storage and not the objects standing around it. 8.5.3p4's cast to a class reference spelled 12.3.2p2's conversion function as the lvalue the cast is, so nothing asked for the object's storage and 12.2p3's end named an object nothing built - **the compiler crashed** on `use((const W&)s)`. And 8.5.3p5's bound was missed, so `(W&)i` bound a non-const lvalue reference to a temporary and `static_cast<W&>(i)` was accepted where `g++` and the binary both refuse it. `place_object`/`name_object` with one element's run, `array_entry` on a standing entry, and `sema_cast.cpp` at 5.4p4's own seam are what came out of it | 222 / 228 -> 226 / 229 -> **227 / 230**, two of them regression tests for an array member of more than one element and for an element whose assignment takes its parameter by value; pa1-pa16 1494 / 1494; no regressions |
