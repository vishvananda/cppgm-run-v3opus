# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **186 / 228** of its fixtures, with pa1-pa16 at **1494 / 1494**
and the file audit passing with the three recorded header-weight warnings it
already had.

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
- `sema_lifetime.cpp` owns what running the four comes to, and what *ending* an
  object's lifetime comes to. 12.8p15/p28's definition is one walk of the same
  subobject list 12.6.2p10 and 12.4p8 walk. 12.4p8's `vacuous_destruction` is
  the one question every end of a lifetime asks - a local leaving its block, a
  temporary, a subobject of a synthesized destructor - and it is broader than
  12.4p5's triviality by exactly the clause that says so: a destructor whose
  body writes no statement, or which 12.4p4 gave no body, comes to what its
  subobjects come to. **That fact is about a definition, and 3.4.1p8 lets a
  definition stand after a body that asks about it**, so the unit's
  out-of-class definitions are taken from the syntax once before the first
  declaration is read and `writes_no_statement` is the one reading of a
  definition both that and `open_special_member_body` do - and it reads
  12.6.2p8's mem-initializer-list beside the compound-statement, because a
  constructor that writes one comes to something however empty the body under it
  is. **A constructor asks the same two questions a destructor does**, so
  `note_definition_body` is asked of both. It also owns **12.8p31's
  elision**, written once as `creates_its_object` and read by both the analysis
  and the lowering.
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
- 6.6.3p2's boundary is one answer, written by `LowirUnitLowering::open_signature`
  and read by the declaration, the definition and a call through a pointer.
- 8.5.3p5's name for the storage a class prvalue with no object of its own is
  given is what asked for the object, and the analysis writes it on the node.
- **3.1p2's "which declaration defines the object" is a fact of the line**, not
  of the entity under it: 9.4.2p2's definition written with a
  nested-name-specifier is a second line describing one object, and only the
  line that lays the storage out may write the image.

Facts PA17 adds: `SemaEntity::transfer`, `::transfers`, `::conversion_function`,
`::conversions`, `::conversions_above`, `::empty_body`, `::nonthrowing`,
`UserType::copy_deleted`, `TypeTable::returns_indirectly`, `RefQualifier` on the
function type, `Value::object_category`, `Fact::subobject_step`,
`Fact::array_form`, `Fact::object_definition`, `Fact::counted`, `Fact::may_fail`,
`FactKind::StorageTransfer`, `FactKind::DeleteExpression`,
`AstKind::CarriedTypeId` (12.3.2p1's conversion-type-id, carried beside the
syntax as 7.1.6.2p1's decltype operand already is), and the six questions lifted
out of the analyzer so each layer asks them once - `observable_expression`,
`creates_its_object`, `vacuous_destruction`, `vacuous_construction`,
`default_constructor` and `writes_no_statement`.

## Current Failure Map

42 fixtures fail, grouped by the compiler behaviour they are waiting on and by
the message each one stops at today.

| group | n | what is missing |
| --- | --- | --- |
| 12.8p31 / 8.5.3p5 placement diffs | 15 | a copy written where the reference elides one, the slot an argument's temporary is named after, and the materialization a cast to an rvalue reference asks for |
| 12.2p3 full-expression temporaries | 9 | the lowering marks no full-expression boundary, so a temporary whose destructor does something is refused outright; 12.2p5's lifetime extension through a reference is the other half |
| 8.5.4 braced-init-list of a non-aggregate | 5 | 13.3.1.7's constructor call from a list, and a braced-init-list written as a call argument at all |
| 12.6.2p6 delegating constructors | 3 | a mem-initializer naming the class itself |
| 9.5 unions | 3 | variant lifetime and p1's one default member initializer |
| 12.8p15 array member / other | 3 | an array of a class whose transfer needs a call, a reference member's copy, and a move-only member's implicit assignment |
| 5.2.9p4 cast to a class type | 2 | a cast to a class reads the operand's bytes instead of direct-initializing a prvalue of it, which has to reach `construct_object` with 13.3.1.4's explicit constructors left in |
| 8.4.2 out-of-class defaulted definitions | 1 | `S::~S() = default;` written outside the class is not parsed, so the unit is refused whole |
| 13.3.1.1.2 surrogate call functions | 1 | an object whose conversion yields a function pointer, called |
| 3.4.3.2 using-directive ambiguity | 1 | two namespaces one level reaches declare one name |

Four holes earlier sweeps found that no fixture covers and that are not this
milestone's: 9.2p1's refusal of a member declared twice in one class, 5.5's
`.*` in the lowering, which member pointers being out of scope leaves, 13.5.6's
overloaded `operator->`, which `object_region` refuses along with every other
`->` on a class operand, and 10.3's virtual dispatch, which the README puts
after this milestone outright.

## Active Checkpoint

None open. C5 is complete, swept and audited at `6c785249`; seven blockers and
one refusal were found and fixed, and `audit.md` holds the review.

The next checkpoint is **C6: 12.2p3's full-expression boundary**, at 9 fixtures
and the largest group left that is one fact rather than a set of diffs. It is
the point a temporary's destructor runs, which 12.2p5's binding of a reference
to a temporary then moves to the end of the reference's scope; its owner is
`sema_lifetime.cpp`, which already owns every other end of a lifetime, and the
lowering already writes destructor actions over explicit storage - so what the
group adds is *where* the action stands and not a second lifetime model.

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
- 6.6.3p2's boundary is one probe of the type, and 12.8p31's result object is
  one operand threaded down the initializer - no node is read twice and no
  instruction is rewritten once written. **The threading is linear in nesting
  depth, not exponential in it**: n nested calls each returning and each taking
  a class by value are 3 n + 14 lines in under 0.02 s, and conditionals of
  class type nested n deep are 11 n + 19 lines in 0.02 s.
- 12.8p31's return-slot local is settled by **one** walk of the function body,
  done once per function that returns indirectly and only then. n functions each
  with two returns are 0.03/0.06/0.11/0.22 s and 27 n lines; one function with n
  returns is 0.02/0.02/0.04/0.07 s and 11 n lines.
- 5.16p3's copy of a glvalue operand is one transfer chosen per operand:
  n conditionals each with a glvalue arm are 0.03/0.05/0.09/0.18 s and 24 n
  lines. One class with n class members each initialized from a call is
  0.02/0.03/0.05/0.09 s and 12 n lines.
- 9.6p2's storage unit is carried once however many bit-fields share it: n
  one-bit fields are n/32 units, 0.00/0.00/0.01/0.01 s.
- 8.3.6p1's "does this parameter have a default argument" and 8.4.3p2's refusal
  of a deleted function are each one probe of a map or one flag read, so a
  program that copies nothing pays one test per name.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 12.8's four value-transfer special members: p2/p3/p17/p19's classification, p7/p9/p18/p20's declaration of what the program did not write, p11/p23's deletion and p12/p25's triviality, p15/p28's synthesized definition as one walk of the subobjects with a leading trivial storage prefix, 8.5p14's copy-initialization from a glvalue, 5.2.2p4/6.6.3p2's argument and returned object as objects of their own, 8.4.2/8.4.3 on an ordinary member declarator, and 8.4.3p2's refusal of every name of a deleted function. Audited at `c2894e79`: six blockers found and fixed | 61 -> 86 -> **88 / 228**; pa1-pa16 1494 / 1494 |
| C2 | 6.6.3p2's returned object and 12.8p31's result object: `TypeTable::returns_indirectly` as one fact of the type, `open_signature` as the one writer of the boundary a declaration, a definition and an indirect call all read, `%ret [pass=indirect_result]` bound in the body, and result-object placement threaded through initialization, return, argument, conditional arm and discarded value. 12.8p31's return-slot local settled by one walk of the body; 12.8p32's access check for the copy the elision removed; 1.9p12's operand of an empty-class transfer still evaluated where evaluating it is observable. `lowir_lower_body.cpp` split at the statement/expression seam. Audited at `be9d930d`: seven blockers found and fixed, which added 5.16p3's initialization of a conditional's result object, made `creates_its_object` the one answer both layers read, and brought 3.6.2p2's namespace-scope initialization into the same hand-off | 88 -> 112 -> **117 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C3 | 12.3.2's conversion functions, end to end. The conversion-type-id travels with the name as `CarriedTypeId`, so 12.3.2p1's "two spellings of one type are one function" holds at the declaration, at the out-of-class definition and at `a.operator T()`; the class holds its own conversions and chains the classes above it; the ABI's `cv` terminal names them. 13.3.1.5/13.3.3.1.2 run the user-defined conversion sequence the other direction under `standard_only_`, with 13.3.3.2p3's second standard sequence ordering the candidates; 8.5.3p5 binds a reference to the lvalue a conversion returns; 4p3 and 6.4.2p2's contextual conversions answer a condition, a condition-declaration, `!`, `&&` and `?:`; 5.2.9p4/5.4p4/8.5p16's explicit conversion is allowed with 13.3.1.5p1's qualification-only restriction; 13.6's built-in candidates are gathered from the operand's class and ranked against the operator function 13.3 chose. Two lowering defects the group exposed: 5.7p5 read `n + p` as a pointer difference, and 5p4's left operand was read after the right ran. 12.4p8's `vacuous_destruction` made the end of a lifetime one question. Audited at `8c59f91a`: six blockers found and fixed, which set 13.3.3.1.2p1's one-conversion flag in the converting-constructor direction too, moved 8.5.3p5's hook above the refusal of a temporary, took 12.4p8's `empty_body` out of the unit's syntax instead of out of the read order, ordered 13.3.1.5's candidates by where the conversion gets to, gave a cast no conversion answers a refusal instead of the object's bytes, and let 13.6p3/p5's `++E` be reached | 117 -> 149 -> **149 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C4 | 8.3.5p1's ref-qualifiers, end to end. The ref-qualifier is a field of the *function type*, interned beside 8.3.5p7's cv-qualifier-seq, so `declarator_type` writes it once and the declaration, the out-of-class definition, `member_signature`'s key, a using-declaration's brought-in copy, the Itanium name and a pointer to member all read the one fact; 8.3.5p6 refuses it on a non-member, a static member, a constructor and a destructor, and 13.1p2 refuses a set that mixes it with the unqualified spelling under any of the four cv-qualifications, because 8.3.5p4's parameter-type-list is what the rule is keyed on. `Value::object_category` carries what 9.3.1p3's pointer drops, and `object_match` is where 13.3.1p4's viability and 13.3.3.2p3's ordering are read off it - reached by a member access, a call with no object expression, an operator's left operand and 13.3.1.5's own candidate set alike, so a conversion function's ref-qualifier is ranked by the same question; 5.2.5p4 is what a member access hands it, which makes a reference member an lvalue before it makes a subobject an xvalue. The Itanium `R`/`O` qualifier joins `K` and `V` in the object name, and the two qualifiers written after the parameter-clause are spelled on the type the declarator wrote and not a second time on the form 9.3.1p3 lowered. 9.3p2's sibling hole closed with it: a definition written with a qualified declarator-id defines a declaration that region already made, so `int X::f() &&` against a declared `int X::f() &` - and equally a mistyped parameter list or cv-qualifier-seq - is refused rather than declaring a second member. Audited at `9f693145`: four blockers found and fixed, which carried the ref-qualifier through a using-declaration's rebuilt type, spanned 13.1p2's probe across the cv-qualifications, put both qualifiers back in PA11's description and took the ref-qualifier out of PA12's, and gave a reference member 5.2.5p4's lvalue category | 149 -> **163 / 228**; pa1-pa16 1494 / 1494; no regressions |
| C5 | 5.3.4 and 5.3.5, end to end, in `sema_allocation.cpp`. 3.7.4.1p2/3.7.4.2p2's four functions are declared in the global namespace before the unit is read, so a program that writes one redeclares it and the object file names it `cppgm_builtin_operator_*`; `operator new` and `operator delete` are the two operator-function-ids a use spells with a space, and the id-expression's spelling is packed to the one the declaration is bound under. 5.3.4p6's first array-suffix is read as the one bound that need not be a constant, the bytes are scaled in the count's own type and reach the parameter through 5.2.2p4's conversion, the ABI's count is written in front of an array of class type and read back off it by `delete[]`, and 12.6p1's construction is one loop with 15.2p2's handler and 5.3.4p18's deallocation behind it; 8.5p7's `()` over a scalar array is one byte loop. 5.3.5 is 12.4p3's end of a lifetime and 3.7.4.2's return of the storage, with 5.3.5p9's lookup, 12.5p4's usual deallocation function, 5.3.5p2's `contextual_pointer` and the null test written where a class type leaves something to guard. 15.4p1's `nonthrowing` joins the declaration, and 5.3.4p15's test is written where 15.4 *and* 18.6.1.1p3's `std::nothrow_t` both say the call reports failure with a value. Three defects the group exposed: 9.4.2p2's second line describing one object let a declaration write the image, so 3.1p2 became a fact of the line; 8.5p14's initialization folded a widening to an unsigned type the rest of 4 spells out; and 3.9.1p2's two eight-byte integral types share one LowIR spelling and are still two types a copy stands between. Audited at `6c785249`: seven blockers and one refusal found and fixed, which made 5.3.4p6's count a fact of its own rather than a zero sentinel, wrote 8.5p7's zero as one instruction over an extent the translation knows, gave 8.5p7's value-initialization of an array the vacuity exit 8.5p6's already had, made `vacuous_construction` 12.4p8's walk of the subobject tree the other way round, settled 5.3.4p15's test once for both forms, named 15.2p2's cleanup destructor by the ABI's complete-object entry, put `unwind=no` back on every reserved builtin, and let 5.3.5p5's incomplete class be deleted rather than refused | 163 -> **186 / 228**; pa1-pa16 1494 / 1494; no regressions |
