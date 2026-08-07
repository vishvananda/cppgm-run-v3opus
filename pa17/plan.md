# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **88 / 228** of its fixtures, with pa1-pa16 at **1494 / 1494**
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
- The same place settles what an object of the class is *carried by*: whether
  the bytes stand for the copy, and whether 8.4.3p2 leaves the program a copy of
  one at all. Those two are one fact each, on the type, with one writer - the
  layout, 5.2.2p4's argument and the lowering's copy of an object all read
  there rather than asking the declarations again.
- `sema_lifetime.cpp` owns what running them comes to. 12.8p15/p28's definition
  is one walk of the same subobject list 12.6.2p10 and 12.4p8 walk: the base,
  then the members in declaration order, each carried by the member its own
  class has or by its storage. `write_transfer_steps` is that walk.
- `sema_overload.cpp` owns 13.3, and with it which of the four a transfer
  chooses - the value category of the argument is what separates the copy from
  the move, at a call's argument, at a return, and at every subobject of a
  synthesized body.
- `lowir_lower*.cpp` still reads only the resolved tree. The two shapes PA17
  adds to it are typed facts, not lowering-time recognition: a
  `storage-transfer` node for the run of storage a copy carries exactly, and
  `SemaEntity::transfer` on the constructor a `constructor-action` names, which
  is what says a trivial transfer is `copyobj` rather than nothing at all.
- Which of the two forms a transfer takes at a site is 8.4.3p2 read where the
  program named it. An initialization of a complete object of a class whose copy
  constructor no program may name is the call of the member the program declared
  for it; one step inside the definition of that member is not that
  initialization, because 12.8p15 chose the constructor there.
- The course ABI passes an object of class type as the storage it occupies. The
  caller has already run 12.8p15's copy into it, so the callee's own
  materialization is a payload move and not a second copy of the object -
  `copy_object_storage` is that move, and `copy_class_object` remains the
  12.8p15 copy that only a class carried by its bytes may be given.

Facts PA17 adds: `SemaEntity::transfer` on a function, `SemaEntity::transfers`
on a class, `UserType::copy_deleted` beside `trivially_copied`,
`Fact::subobject_step`, and `FactKind::StorageTransfer` with the byte, the span
and the scalar the two ends are read and written with.

## Current Failure Map

140 fixtures fail. Grouped by the compiler behaviour they are waiting on, most
of a group failing for the one reason named.

| group | ~n | what is missing |
| --- | --- | --- |
| class value ABI | 42 | `pass=indirect_result` / `pass=by_address` are never produced, so a class that is not carried by its bytes cannot be returned or passed, and a returned one is materialized into a second object at every call |
| 12.3.2 conversion functions | 35 | `operator T()` is refused at its declaration |
| 5.3.4/5.3.5 new and delete | 12 | the array form, the class-specific allocation functions and `::operator new[]` |
| 12.2p3 full-expression temporaries | 10 | the lowering marks no full-expression boundary, so a temporary with a destructor is refused |
| 8.3.5 ref-qualifiers | 8 | `&`/`&&` on a member are parsed and ignored, and never refused where 8.3.5p4 refuses them |
| out-of-class definitions | 7 | a definition written outside the class declares a second function |
| empty-destructor elision | 6 | the references elide a destructor whose body is empty; this unit runs it (a PA16 divergence the PA17 fixtures price) |
| 12.8p15 array member | 4 | an array of a class whose transfer needs a call has no form; see below |
| 5.2.9p4 cast to a class type | 3 | a cast to a class reads the operand's bytes instead of direct-initializing a prvalue of it, which for `static_cast<box>(7)` writes `addr 7` and fails LowIR validation with `EXIT_SUCCESS` - 5.2.9p4 is a direct-initialization, so the fix has to reach `construct_object` with 13.3.1.4's explicit constructors left in |
| 9.5 unions | 3 | variant lifetime and p1's one default member initializer |
| 12.6.2p6 delegating constructors | 3 | a mem-initializer naming the class itself |

## Active Checkpoint

None open. C1 is complete, audited (`pa17/audit.md`) and its ledger row is below.

The next checkpoint is **C2: the class value ABI** - 5.2.2p4's indirect
parameter and 6.6.3p2's indirect return destination for a class that is not
carried by its bytes, with 12.8p31's direct use of the return slot for
`return local;`. It is by far the largest group left, it is what the second
`argobj` and the trailing `copyobj` in 26 of the shape diffs are, and everything
above it in the table depends on nothing it does not already have: the
constructor a transfer names is chosen, the definition is written, and what is
missing is the boundary the object crosses.

C2 should take 12.8p15's array member with it. It is the same walk one step in:
the element's own transfer member run on each element, both objects indexed by
the same index - which past `kArrayLoopLimit` is the loop 12.6p1 and 12.4p8
already write, so a bound the source wrote as one number stays one number.

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
