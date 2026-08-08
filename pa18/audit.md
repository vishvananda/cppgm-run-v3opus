# PA18 Audit — `cppgm++ --emit-lowir` scoped polymorphism

A review of each landed checkpoint, in the order a fact travels: declare,
settle, lay out, lower.

## Current Checkpoint Review

**C2 reviewed at `c18c1b2c`, the commit that gave the class's table, record and
name string a place in the object file, the object its vpointer stores, and a
call the slot it runs out of.** The architecture holds: `lowir_vtable.cpp` owns
the three globals a polymorphic class owns and emits each on demand and
memoised, `settle_vtable_ownership` closes the settlement with the ABI's two
questions where 9.2p2 completes the class, the vpointer store is an action of
the lowered body so the shared AST PA10-PA12 dump is untouched, and dispatch
travels as `SemaFact::dispatches` rather than being rediscovered from syntax.
Every one of the 36 checked-in `.ref` files regenerates byte-identically from
`reference-binaries/cppgm++`, so the fixtures are an oracle and not a record of
our own output.

**What the review looked for is the boundary a fact of the *program* was settled
at, the readers of a fact C2 widened, and the sibling spellings of a rule it
landed at one.** Seven blockers, in three families.

The first family is **a fact of the program held by one translation unit**.
`--emit-lowir` takes as many source files as the command line names and writes
one LowIR program; C2 put three answers about that program in
`LowirUnitLowering`, and each is wrong as soon as a second unit exists:

**1. The table's own spelling.** `vtable_symbol` asks whether *this* unit
defines the class's key function and writes `__external_vtable__X` where it does
not. A unit is read before the ones after it, so the unit that owns the table
can come second - and then the program held `declare global @__external_vtable__X`
beside `global @X__vtable`, with the first unit's constructor and destructor
storing a **vpointer that names a symbol nothing in the program defines**. The
name is a fact of the program: the units record what each did and
`LowirProgramBuilder::finish` settles the two into one.

**2. `__cxa_pure_virtual`.** The runtime's own name was declared from a per-unit
memo, so two units each holding a class with a pure final overrider wrote two
declarations of one name and the output **failed LowIR validation outright** -
`duplicate LowIR symbol entries: __cxa_pure_virtual`. 3.5p9 makes it one entity
of the program, so the second ask is answered from the program's `declared_`.

**3. The declaration a use writes in front of a definition another unit holds.**
The same shape one layer down, and C2's deleting entry is one of its writers:
a unit that only uses a name emits `declare function @f`, the unit that defines
it emits `function @f`, and one program held both. The reference writes the
definition alone. `finish` now drops a declaration the program went on to
define, which is what makes any multi-file program valid at all - the shape
reproduces with no virtual function in it, so this one is inherited rather than
C2's, and C2's `__deleting_entry` declaration merely joined it.

The second family is **the readers of the fact C2 widened**:

**4. 12.1p11 was read at two of its three readers.** C2 taught
`vacuous_destruction` that a virtual destructor is never nothing and
`construction_writes_nothing` that a polymorphic class's constructor always
writes, and left `vacuous_construction` - which is what 5.3.4p15 asks about the
elements a new-expression creates - asking only the constructor's body. A
polymorphic class whose constructor the standard gave it, or whose body is
empty, came out vacuous, so `new T[n]` emitted **no element construction at
all**: n objects whose vpointer held whatever the allocation returned, and a
virtual call on one dispatched through uninitialized storage. The reference
emits the constructor loop.

**5. 15.2p2's handler was opened around an element construction that throws
nothing.** Fixing 4 made the loop appear and the handler with it, and the
handler is one the reference does not write: `construct_array_new_run` reserved
the cleanup blocks and armed the region for every element constructor, whatever
its exception-specification. The blocks are not reserved now where the
constructor cannot throw, which is the reading `note_call` already had. This one
also reproduces with no virtual function in it.

The third family is **the sibling spellings of a rule C2 landed at one**:

**6. 12.6.2p6's delegating constructor was given a vpointer store of its own.**
The store is written in front of the first child that is not a base
subobject's construction, and a delegating ctor-initializer is such a child - so
`C::C() : C(3) {}` wrote `C`'s table into the object and then called the target,
which writes it again. A delegating constructor initializes no base and no
member; the object is constructed when the target returns, and the store is the
target's.

**7. 5.4p4's fold took the one cast spelling that is not 4.10p1.** C2 folded a
written cast whose operand is a null pointer constant to the null pointer value,
and `reinterpret_cast<T*>(0)` is 5.2.10p5's reading of the integer as an
address, which 5.2.10p1 lets no other conversion stand in for. The reference
computes it; we produced the constant. `(T*)0` and `static_cast<T*>(0)` are
unchanged, and an enumerator was already excluded.

`LowirProgramBuilder::settle_vtable_names`, `settle_external_declarations`, the
`writes_vpointer` reading in `vacuous_construction`, the throwing-element gate in
`construct_array_new_run`, `delegates_to` read at the vpointer store and
`KW_REINTERPET_CAST` excluded from the fold are what came out of it.

## Evidence

- **Every `.ref` regenerated.** All 36 checked-in fixtures - the 32 under
  `tests/` and the four C2 added - reproduce byte-identically from
  `reference-binaries/cppgm++` under the relaxed comparison, so no fixture holds
  our own output.
- **Lowering, 44 probes** against the reference binary over the call-form,
  constructor-form, cast-spelling, array, destructor-body and multi-unit cross
  product. Three new fixtures pin what the fixes changed
  (`300-array-new-polymorphic-elements` - which emits *no* element constructor
  before the fix - `300-delegating-constructor-vpointer` and
  `300-reinterpret-cast-null-pointer`); the multi-unit blockers cannot be
  written as fixtures, because the local harness passes one source file per
  test, so they are pinned by probes alone.
- **Multi-unit**, four shapes on one command line: two units each with a pure
  slot, two units with different pure signatures (one declaration, the first
  signature - the reference's own answer), the same polymorphic class in both,
  and a class whose key function one unit defines and another uses. The last
  produces `@KF__vtable` from both units now, which is what the reference
  writes; the single-unit case still writes `__external_vtable__X`, which the
  checked `300-external-key-function-vtable` pins.
- **Scaling**, seven shapes, each timed twice, `cppgm++ --emit-lowir -O0`:

  | shape | 32 | 64 | 128 | 256 | 512 |
  | --- | --- | --- | --- | --- | --- |
  | 16 *new* virtuals per level | 0.02 s | 0.05 s | 0.13 s | 0.40 s | 1.54 s |
  | 64 virtuals *overridden* per level | 0.03 s | 0.08 s | 0.14 s | 0.30 s | 0.61 s |
  | n objects of an n-deep chain | 0.02 s | 0.04 s | 0.07 s | 0.16 s | 0.37 s |
  | the same with a throwing call after each | 0.02 s | 0.04 s | 0.08 s | 0.18 s | - |
  | n `new`/`delete` pairs over that chain | 0.02 s | 0.04 s | 0.08 s | 0.18 s | 0.43 s |
  | n polymorphic classes over one n-deep non-polymorphic chain | 0.02 s | 0.03 s | 0.06 s | 0.13 s | 0.28 s |
  | n array-`new`/`delete[]` pairs of a polymorphic element | 0.01 s | 0.02 s | 0.03 s | 0.06 s | 0.15 s |

  Only the first is superlinear, and it is superlinear in the *settlement* and
  not in the output: a class's table is the base's table with the overridden
  slots replaced, so n classes introducing 16 virtuals each cost 16*n(n+1)/2
  slot copies - 2.1 million at n=512, in 1.5 s - while the LowIR stays linear
  because only the classes an object is created of have a table emitted. That
  cost is what "the vtable is a fact of the class" is; it buys the O(1) call
  site. The sixth shape is the one `settle_vtable_ownership`'s walk of a
  non-polymorphic base chain could have made quadratic, and it is linear.
- **Valgrind** clean with `--error-exitcode` over all 39 fixtures, over all
  seven scaling shapes at n=64, and over every multi-unit probe.

## Open Gaps

**Recorded, not defects**, and unchanged from C1: `void g(B);` and `B h();` over
an abstract `B` are refused at the declaration where the external compilers
complain only at a call, which the checked
`100-abstract-class-by-value-argument-bad` pins; `extern B e;` and a static data
member of abstract class type are accepted, where g++ and the reference stand
and clang does not; and a static member function whose cv-qualification differs
from an inherited virtual's overrides nothing and so is accepted, which is where
the reference stands too.

**The deleting entry's handler shape.** The reference registers 5.3.5p3's
deallocation as a 15.2p2 live entry, so every throwing call in the deleting
entry's body opens a region whose dispatch gives the storage back and pops the
outer cleanup; we let the one `eh_cleanup` the epilogue already opens cover the
whole body. Six probes differ - a virtual call, a local object, a temporary, a
`new`/`delete`, two nested scopes and a live local across a throwing call - and
every one of them frees the storage exactly once on both paths and destroys the
locals first, so the two are the same program written two ways. It is the one
place C2's own mechanism and the reference's disagree.

**Inherited, reproduced with no virtual function in the input, and owed to no
milestone yet.** An implicitly declared default constructor never has 15.4p14's
exception-specification settled - it keeps the `false` `create` gives it - so an
array-`new` of a class whose default constructor the standard gave it still
opens the handler the reference omits; and `replay_unwind` reuses one `addr`/
`decay` pair across the elements of a local array where the reference recomputes
it per element. A block-scope static of dynamic initialization is refused
outright, which PA16 wrote and no milestone since has lifted.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2's overriding and the ABI slot order, 10.3p4/p5/p7, 10.4p2/p3, the vpointer's place in 9.2p13's layout, and 4.10p3's null-preserving base cast | `37a34dea` | 7 / 7, in two families - the exits a question about dispatch has at the forms the checkpoint never reached, and the readers of a fact it widened: 9.2p8's `final` on a member that is not virtual **accepted** where g++, clang and the reference all refuse; the same virt-specifier accepted at namespace scope, on a member function defined outside its class, and on a constructor, destructor or conversion function defined outside one, which asked neither half; 9.4.1p2's static member function with an inherited virtual's signature **accepted**, leaving the base's declaration standing in the slot; 12.4p9's `virtual ~B() = 0;` with no form in the special-member production at all, so a valid program inside the Assignment Boundary was **refused outright**; 10.3p7's covariant return asking only that the return type's class derive and not that the base be accessible from the class the override wrote; 12.8p12 read at one of its three readers, so a polymorphic class was **passed by value and returned as bytes** at 5.2.2p4's boundary and its own assignment wrote `copyobj` over the vpointer - **an assignment that changes an object's dynamic type**; and 4.10p3's test asked of the operand's type rather than of the pointer value the step moves, so `p->v` wrote no test and `r.f()` wrote one, each the reverse of the reference, with 5.16p4's `c ? p : q` dragged into its address form beside them | 6 / 29 -> **9 / 32**, three of them the regression tests these leave; pa1-pa17 1732 / 1732; file audit passes; 99 accept/reject probes against clang, g++ and the reference, and 40 lowering probes byte-identical to the reference but for the globals C2 owes |
| C2 | the emitted polymorphic object model: `lowir_vtable.cpp`'s tables, records and name strings, the key function, 12.1p11/12.4p11's vpointer stores, 12.4's D1/D2/D0 triple with 5.3.5p3's deallocation, 10.3p12's dispatch on `SemaFact::dispatches`, and 5.4p4's folded null pointer constant | `c18c1b2c` | 7 / 7, in three families - a fact of the *program* settled per translation unit, the third reader of a fact the checkpoint widened, and the sibling spellings of a rule it landed at one: the table's spelling asked of one unit, so a program whose *second* unit owns the table left the first unit's vpointer stores naming **a symbol nothing defines**; `__cxa_pure_virtual` declared from a per-unit memo, so two units with a pure slot wrote **invalid LowIR** outright; a `declare function` left standing in front of a definition a later unit wrote, which is the same shape and makes any multi-file program invalid; 12.1p11 unread by `vacuous_construction`, so `new T[n]` of a polymorphic class emitted **no element construction at all** and left n vpointers holding whatever the allocation returned; 15.2p2's handler armed around an element constructor that throws nothing, which fixing that made reachable; 12.6.2p6's delegating constructor writing a vpointer the target writes again; and 5.4p4's fold taking `reinterpret_cast<T*>(0)`, which is 5.2.10p5's reading of the integer as an address | 34 / 36 -> **37 / 39**, three of them the regression tests these leave; pa1-pa17 1732 / 1732; file audit passes with its three inherited warnings; all 36 checked `.ref` files regenerated from the reference binary, 44 lowering probes, four multi-unit shapes, seven scaling shapes and a valgrind sweep over all of them |
