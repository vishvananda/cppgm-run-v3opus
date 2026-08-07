# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **112 / 228** of its fixtures, with pa1-pa16 at **1494 / 1494**
and the file audit passing with the three recorded header-weight warnings it
already had.

## Stage Design

PA17 gives the PA16 object model value semantics. Nothing becomes a second
pipeline: every fact the milestone adds is hung on the owner that already
answers the question it belongs to.

- `sema_class.cpp` owns what a class *is*, and 12.8's four value-transfer
  members are four more facts of it, settled where 9.2p2 completes the class.
  p2/p3/p17/p19 say which of the declarations the program wrote is which - read
  from the parameter list 9.3.1p3 put in the type, never from syntax, and read
  from the class's own region, never from a lookup that reaches a base or an
  enclosing class - and p7/p9/p18/p20 add the ones it did not write. p11/p23 say
  which of them the standard cannot define, p12/p25 which do nothing but carry
  an object's bytes. The class holds all four as `SemaEntity::transfers`, so no
  later layer searches a class for its copy constructor.
- `type_model` owns what an object of the class is *carried by*: whether the
  bytes stand for the copy, whether 8.4.3p2 leaves the program a copy of one at
  all, and - added by C2 - whether 6.6.3p2 hands one back as bytes or through a
  destination the caller named. Those are one fact each, on the type, with one
  writer; the layout, 5.2.2p4's argument, every signature and every call read
  there rather than asking the declarations again.
- `sema_lifetime.cpp` owns what running the four comes to. 12.8p15/p28's
  definition is one walk of the same subobject list 12.6.2p10 and 12.4p8 walk.
  It also owns 12.8p32: a copy 12.8p31 elides is still one the program had to be
  allowed to write, so the constructor it would have named is asked for where
  the initialization stands.
- `sema_overload.cpp` owns 13.3, and with it which of the four a transfer
  chooses - the value category of the argument is what separates the copy from
  the move, at a call's argument, at a return, and at every subobject of a
  synthesized body.
- `lowir_lower*.cpp` reads only the resolved tree. Its one shape rule is
  **result-object placement**: the storage an object of class type will stand in
  is named *before* the initializer that fills it runs, and handed to it. An
  initializer that creates an object - a temporary, a call returning one
  indirectly, a conditional whose arms each do - creates it there and no copy
  stands between the two; everything else is read and copied. `creates_object`
  is that question and `place_class_object` is that hand-off.
- 6.6.3p2's boundary is one answer, written by `LowirUnitLowering::open_signature`
  and read by the declaration, the definition and a call through a pointer, so
  the three cannot disagree about one function.
- The course ABI passes an object of class type as the storage it occupies. The
  caller has already run 12.8p15's copy into it, so the callee's own
  materialization is a payload move and not a second copy of the object.

Facts PA17 adds: `SemaEntity::transfer` on a function, `SemaEntity::transfers`
on a class, `UserType::copy_deleted` beside `trivially_copied`,
`TypeTable::returns_indirectly`, `Fact::subobject_step`,
`FactKind::StorageTransfer`, and `observable_expression` lifted out of the
analyzer so the lowering asks 1.9p12 the same question the analysis does.

## Current Failure Map

116 fixtures fail. Grouped by the compiler behaviour they are waiting on, most
of a group failing for the one reason named.

| group | ~n | what is missing |
| --- | --- | --- |
| 12.3.2 conversion functions | 34 | `operator T()` is refused at its declaration |
| 5.3.4/5.3.5 new and delete | 12 | the array form, the class-specific allocation functions and `::operator new[]` |
| 12.2p3 full-expression temporaries | 10 | the lowering marks no full-expression boundary, so a temporary with a destructor is refused |
| out-of-class definitions | 8 | a definition written outside the class declares a second function |
| 8.3.5 ref-qualifiers | 6 | `&`/`&&` on a member are parsed and ignored, and never refused where 8.3.5p4 refuses them |
| 5.2.2p4 `by_address` parameters | 5 | a class the bytes are not the copy of is still passed as `obj<NxA>`; the refs pass its address and name the argument object `arg` rather than `argobj` |
| empty-destructor elision | 6 | the references elide a destructor whose body is empty; this unit runs it (a PA16 divergence the PA17 fixtures price) |
| 5.2.9p4 cast to a class type | 4 | a cast to a class reads the operand's bytes instead of direct-initializing a prvalue of it, which for `static_cast<box>(7)` writes `addr 7` - 5.2.9p4 is a direct-initialization, so the fix has to reach `construct_object` with 13.3.1.4's explicit constructors left in |
| 12.8p15 array member / union transfer | 5 | an array of a class whose transfer needs a call has no form, and a union's `operator=` picks the copy where 13.3 picks the move |
| 9.5 unions | 3 | variant lifetime and p1's one default member initializer |
| 12.6.2p6 delegating constructors | 3 | a mem-initializer naming the class itself |
| 4.7 conversion spelling | 4 | an `int` initializer of a `long` object is folded rather than written as the `convert sext` the refs write |

## Active Checkpoint

None open. C2 is complete and its ledger row is below.

The next checkpoint is **C3: 12.3.2's conversion functions** - by far the
largest group left, 34 fixtures, and refused at the *declaration*, which means
nothing downstream of it has ever been asked to run. `operator T()` is a member
whose name is a type, so it belongs where 9.2p2 settles what a class declares
and where 13.3.1.5/13.3.3.1.2 already rank a user-defined conversion; the
conversion machinery `sema_overload.cpp` holds for a converting constructor is
the same machinery the other direction.

## Performance Model

One line per invariant, and the measurement that holds it. Best of three on this
host, at 250/500/1000/2000 unless said otherwise.

- 12.8's classification is one walk of the class's constructor chain and one of
  the declarations of `operator=` the class's **own region** holds, done once
  where 9.2p2 completes the class. Asking 3.4 for that name instead reaches a
  base's declaration and chains this class's member onto it, which was O(n²) in
  the depth of an inheritance chain: 0.91 s at 2000, and 0.14 s now. 12.8p11's
  deletion and p12's triviality are one further walk of the subobjects, reading
  each subobject class's own four members through a pointer rather than
  searching it. n classes each declaring nothing and each copied and assigned
  are 0.09/0.16/0.32/0.61 s.
- 12.8p15's definition is one node per subobject, and the leading run whose
  bytes a copy carries exactly is one `copyobj` however many members it covers.
  A class with n such members and one non-trivial tail is **29 lines of output
  at every size**, in 0.00/0.00/0.01/0.02 s - which is the whole point of the
  prefix: a member count the source wrote is not a thing the output counts. An
  array member carried by its bytes is one `copyobj` of the array for the same
  reason, wherever in the class it stands.
- A class with n members that each need a call is 10 n lines and
  0.02/0.03/0.07/0.14 s, which is the n calls per transfer the source asks for.
  A base subobject in front of them costs 5 n lines and 0.01/0.03/0.07/0.10 s -
  linear, because the prefix only ever covers a run beginning where the object
  does. 15.2p2's suffix over those steps is PA16's chain past
  `kUnwindSuffixLimit` and grows no faster.
- 6.6.3p2's boundary is one probe of the type, and 12.8p31's result object is
  one operand threaded down the initializer - no node is read twice and no
  instruction is rewritten once written. n nested calls each returning and each
  taking a class by value are **3n + 19 lines** and under 0.01 s at n = 400: one
  argument object and one call per level, and no copy at any of them, because
  each call creates its returned object directly in the next argument's storage.
- 12.8p31's return-slot local is settled by **one** walk of the function body,
  done once per function that returns indirectly and only then. n functions each
  with two returns are 0.01/0.03/0.07/0.15 s; one function with n returns is
  0.01/0.01/0.03/0.06 s and 11 n lines - the walk is the body, not the body per
  return.
- 9.6p2's storage unit is carried once however many bit-fields share it: n
  one-bit fields are n/32 units, 0.00/0.00/0.01/0.01 s.
- 8.3.6p1's "does this parameter have a default argument", which 12.8p2 asks of
  every parameter after the first, is one probe of the map the declaration
  already filled.
- 8.4.3p2's refusal of a deleted function is one flag read where a name is
  resolved, and 12.8p11's at a by-value boundary is one flag read on the type,
  so a program that copies nothing pays one test per name.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 12.8's four value-transfer special members: p2/p3/p17/p19's classification of what the program declared, p7/p9/p18/p20's declaration of what it did not, p11/p23's deletion and p12/p25's triviality, p15/p28's synthesized definition as one walk of the subobjects with a leading trivial storage prefix, 8.5p14's copy-initialization from a glvalue as the call 13.3 chooses, 5.2.2p4/6.6.3p2's argument and returned object as objects of their own, 8.4.2/8.4.3 on an ordinary member declarator, and 8.4.3p2's refusal of every name of a deleted function. Audited at `c2894e79`: six blockers found and fixed | 61 -> 86 -> **88 / 228**; pa1-pa16 1494 / 1494 |
| C2 | 6.6.3p2's returned object and 12.8p31's result object: `TypeTable::returns_indirectly` as one fact of the type, `open_signature` as the one writer of the boundary a declaration, a definition and an indirect call all read, `%ret [pass=indirect_result]` bound in the body, and result-object placement threaded through initialization, return, argument, conditional arm and discarded value so a class prvalue's storage is named before the initializer that fills it. 12.8p31's return-slot local settled by one walk of the body; 12.8p32's access check for the copy the elision removed; 1.9p12's operand of an empty-class transfer still evaluated where evaluating it is observable. `lowir_lower_body.cpp` split at the statement/expression seam | 88 -> **112 / 228**; pa1-pa16 1494 / 1494; no regressions |
