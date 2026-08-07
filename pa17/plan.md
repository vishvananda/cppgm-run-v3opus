# PA17 Plan — `cppgm++ --emit-lowir` value semantics

PA17 stands at **86 / 228** of its fixtures, up from 61 at the start of C1, with
pa1-pa16 at **1494 / 1494** and the file audit passing with the three recorded
header-weight warnings it already had.

## Stage Design

PA17 gives the PA16 object model value semantics. Nothing becomes a second
pipeline: every fact the milestone adds is hung on the owner that already
answers the question it belongs to.

- `sema_class.cpp` owns what a class *is*, and 12.8's four value-transfer
  members are four more facts of it, settled where 9.2p2 completes the class.
  p2/p3/p17/p19 say which of the declarations the program wrote is which - read
  from the parameter list 9.3.1p3 put in the type, never from syntax - and
  p7/p9/p18/p20 add the ones it did not write. p11/p23 say which of them the
  standard cannot define, p12/p25 which do nothing but carry an object's bytes.
  The class holds all four as `SemaEntity::transfers`, so no later layer
  searches a class for its copy constructor.
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
- The course ABI passes an object of class type as the storage it occupies. The
  caller has already run 12.8p15's copy into it, so the callee's own
  materialization is a payload move and not a second copy of the object -
  `copy_object_storage` is that move, and `copy_class_object` remains the
  12.8p15 copy that only a trivially copied class may be given.

Facts PA17 adds: `SemaEntity::transfer` on a function, `SemaEntity::transfers`
on a class, and `FactKind::StorageTransfer` with the byte, the span and the
scalar the two ends are read and written with.

## Current Failure Map

142 fixtures fail. Grouped by the compiler behaviour they are waiting on, most
of a group failing for the one reason named:

| group | ~n | what is missing |
| --- | --- | --- |
| 12.3.2 conversion functions | 27 | `operator T()` is refused at its declaration |
| class value ABI | 20 | `pass=indirect_result` / `pass=by_address` are never produced, so a class that is not trivially copied cannot be returned or passed |
| 12.2p3 full-expression temporaries | 12 | the lowering marks no full-expression boundary, so a temporary with a destructor is refused |
| 8.3.5 ref-qualifiers | 12 | `&`/`&&` on a member are parsed and ignored, and never refused where 8.3.5p4 refuses them |
| 5.3.4/5.3.5 new and delete | 11 | the array form, the class-specific allocation functions and `::operator new[]` |
| out-of-class ctor/dtor definitions | 4 | a definition written outside the class declares a second function |
| 12.6.2p6 delegating constructors | 3 | a mem-initializer naming the class itself |
| 9.5 unions | 4 | variant lifetime and p1's one default member initializer |
| empty-destructor elision | 5 | the references elide a destructor whose body is empty; this unit runs it (a PA16 divergence the PA17 fixtures now price) |

## Active Checkpoint

None open. C1 is complete and its ledger row is below. The next checkpoint is
**C2: the class value ABI** - 5.2.2p4's indirect parameter and 6.6.3p2's
indirect return destination for a class that is not trivially copied, with
12.8p31's direct use of the return slot for `return local;`. It is the largest
single group left and every group above it in the table depends on nothing it
does not already have: the constructor a transfer names is chosen, the
definition is written, and what is missing is the boundary the object crosses.

## Performance Model

One line per invariant, and the measurement that holds it.

- 12.8's classification is one walk of the class's constructor chain and one of
  the declarations of `operator=`, done once where 9.2p2 completes the class.
  12.8p11/p23's deletion and p12/p25's triviality are one further walk of the
  subobjects, reading each subobject class's own four members through a pointer
  rather than searching it. n classes each declaring nothing and each copied,
  moved and assigned are 0.04/0.08/0.17/0.38 s at 250/500/1000/2000.
- 12.8p15's definition is one node per subobject, and the leading run whose
  bytes a copy carries exactly is one `copyobj` however many members it covers.
  A class with n such members and one non-trivial tail is **90 lines of output
  at every size**, in 0.00/0.01/0.01/0.03/0.05 s at 250/500/1000/2000/4000 -
  which is the whole point of the prefix: a member count the source wrote is
  not a thing the output counts.
- A class with n members that each need a call is 10 n + 84 lines and
  0.01/0.02/0.04/0.08 s at 125/250/500/1000, which is the n calls per transfer
  the source asks for. 15.2p2's suffix over those steps is PA16's chain past
  `kUnwindSuffixLimit` and grows no faster.
- 8.3.6p1's "does this parameter have a default argument", which 12.8p2 asks of
  every parameter after the first, is one probe of the map the declaration
  already filled.
- 8.4.3p2's refusal of a deleted function is one flag read where a name is
  resolved, so a program that names none pays one test per name.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 12.8's four value-transfer special members: p2/p3/p17/p19's classification of what the program declared, p7/p9/p18/p20's declaration of what it did not, p11/p23's deletion and p12/p25's triviality, p15/p28's synthesized definition as one walk of the subobjects with a leading trivial storage prefix, 8.5p14's copy-initialization from a glvalue as the call 13.3 chooses, 5.2.2p4/6.6.3p2's argument and returned object as objects of their own, 8.4.2/8.4.3 on an ordinary member declarator, and 8.4.3p2's refusal of every name of a deleted function | 61 -> **86 / 228**; pa1-pa16 1494 / 1494 |
