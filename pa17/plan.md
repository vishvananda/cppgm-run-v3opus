# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **149 / 228** of its fixtures, with pa1-pa16 at **1494 / 1494**
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
  caller named. One fact each, on the type, with one writer.
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
  declaration is read and `writes_no_statement` is the one reading of a body
  both that and `open_special_member_body` do. It also owns **12.8p31's
  elision**, written once as `creates_its_object` and read by both the analysis
  and the lowering.
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
- 4p3's contextual conversion is one call - `contextual_bool` - reached by a
  condition, `!`, `&&`, `||` and `?:`, with 12.3.2p2's `explicit` left in;
  6.4.2p2's is `contextual_integral`, with it left out. 5.2.9p4, 5.4p4 and
  8.5p16 reach `explicit_conversion`, which is the same question with
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
- 6.6.3p2's boundary is one answer, written by `LowirUnitLowering::open_signature`
  and read by the declaration, the definition and a call through a pointer.
- 8.5.3p5's name for the storage a class prvalue with no object of its own is
  given is what asked for the object, and the analysis writes it on the node.

Facts PA17 adds: `SemaEntity::transfer`, `::transfers`, `::conversion_function`,
`::conversions`, `::conversions_above`, `::empty_body`, `UserType::copy_deleted`,
`TypeTable::returns_indirectly`, `Fact::subobject_step`,
`FactKind::StorageTransfer`, `AstKind::CarriedTypeId` (12.3.2p1's
conversion-type-id, carried beside the syntax as 7.1.6.2p1's decltype operand
already is), and the four questions lifted out of the analyzer so each layer
asks them once - `observable_expression`, `creates_its_object`,
`vacuous_destruction` and `writes_no_statement`.

## Current Failure Map

79 fixtures fail, grouped by the compiler behaviour they are waiting on and by
the message each one stops at today.

| group | n | what is missing |
| --- | --- | --- |
| 5.3.4/5.3.5 new and delete | 20 | the array form, the class-specific and placement allocation functions, `::operator new` and `::operator new[]` in scope at all, the null a nothrow allocation returns, and a `new` whose bound PA11 will not evaluate |
| 8.3.5 ref-qualifiers | 14 | `&`/`&&` on a member is parsed and dropped, so seven fixtures stop at "is defined twice" where a declaration collides with its out-of-class definition, five write no refusal 8.3.5p4 asks for, and 13.3.1p4 ranks nothing by the object's value category - including a conversion function's |
| 12.8p31 / 8.5.3p5 placement diffs | 12 | a copy written where the reference elides one, the slot an argument's temporary is named after, and the materialization a cast to an rvalue reference asks for |
| 12.2p3 full-expression temporaries | 9 | the lowering marks no full-expression boundary, so a temporary whose destructor does something is refused outright; 12.2p5's lifetime extension through a reference is the other half |
| 8.5.4 braced-init-list of a non-aggregate | 7 | 13.3.1.7's constructor call from a list, and a braced-init-list written as a call argument at all |
| 5.2.9p4 cast to a class type | 4 | a cast to a class reads the operand's bytes instead of direct-initializing a prvalue of it, which has to reach `construct_object` with 13.3.1.4's explicit constructors left in |
| 12.6.2p6 delegating constructors | 3 | a mem-initializer naming the class itself |
| 9.5 unions | 3 | variant lifetime and p1's one default member initializer |
| 12.8p15 array member / other | 3 | an array of a class whose transfer needs a call, a reference member's copy, and a move-only member's implicit assignment |
| 4.7 conversion spelling | 1 | an `int` operand of a compound assignment is folded rather than written as the `convert` the reference writes |
| 8.4.2 out-of-class defaulted definitions | 1 | `S::~S() = default;` written outside the class is not parsed, so the unit is refused whole |
| 13.3.1.1.2 surrogate call functions | 1 | an object whose conversion yields a function pointer, called |
| 3.4.3.2 using-directive ambiguity | 1 | two namespaces one level reaches declare one name |

## Active Checkpoint

None open. C3 is complete, audited at `8c59f91a` with six blockers found and
fixed, and its ledger row is below.

The next checkpoint is **C4: 8.3.5's ref-qualifiers**, at 14 fixtures and the
last piece of the member-call model this milestone names. 5.3.4/5.3.5's new and
delete is the larger group at 20, but it is a subsystem of its own - allocation
functions, the array form, and 5.3.5's deallocation - rather than a fact of the
member-call model C1-C3 built, so it is the checkpoint after this one.

Its owner is the same one C3 used: 9.3.1p3's object parameter is where a
ref-qualifier lands, so `with_object_parameter` writes it into the type, the
type is what a declaration and its out-of-class definition agree on (seven of
the failures stop at "is defined twice", which is the collision a dropped
qualifier makes), 8.3.5p4 refuses one on a non-member and on a static member,
and 13.3.1p4/13.3.3.2p3 rank the implicit object argument by the value category
the object expression has - which `match_reference` already answers for every
other reference and would answer for this one too. 12.3.2's conversion
functions are in the group: `general/400-conversion-function-ref-qualifier-viability`
writes `operator int() &` and `operator int() &&`, so the candidate set
`gather_conversions` hands 13.3.1.5 has to be ranked by the same object
argument, which is the one place C3 and C4 meet.

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
  0.01/0.02/0.05/0.11 s and 11 n lines.
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
