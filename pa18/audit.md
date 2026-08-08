# PA18 Audit — `cppgm++ --emit-lowir` scoped polymorphism

A review of each landed checkpoint, in the order a fact travels: declare,
settle, lay out, lower. The final entry is a review of the whole stage, taken
from the source rather than from the checkpoints that built it.

## Final Audit

**Reviewed at `ddb2130e`, the commit that completed the stage at 41 / 41.**
The architecture was reconstructed from `sema_virtual.cpp`, `sema_layout.cpp`,
`lowir_vtable.cpp`, `lowir_abi.cpp` and the lowering, and compared with the
README's Assignment Boundary; every conclusion below was re-derived by probe
against `reference-binaries/cppgm++` and g++ rather than taken from a
checkpoint. The design holds: every fact the milestone adds is a fact of the
class the standard hangs it on, settled once and read by name.

**Six blockers survived every checkpoint, and every one of them was found by
taking a rule the stage landed at one shape to the shapes beside it.**

### Findings

**1. A class a function body left unnamed was named from a counter this unit
kept for itself.** `class_declaration` gave it `__local_typeN`, and 9.8p1 was
never asked, because nothing binds such a declaration in a region and
`declare_in` - where C3 settled the local name - never sees it. Two translation
units each declaring an unnamed local class therefore emitted **one** vtable
under **one** object symbol `_ZTV13__local_type1` for **two different classes**,
and `emitted_globals_` dropped the second: the objects of one unit got the
other's overriders. Both oracles name it after the function and its place among
the types that function left unnamed - `_ZTSZ1gvEUt_` - and this now does.
`Scope`, `SemaEntity` and `TypeTable` carry `local_unnamed` beside the function
and the number, `settle_unnamed_local_name` settles it where the declaration is
read, and PA14's encoder gained `<unnamed-type-name>` as a name component, so
`Ut_` stands where a source name would in a type, in a member's `<local-name>`
context and as the last component of a nested one alike.

**2. `load obj<4x4>` is not an instruction, and we emitted it.** A call whose
value is an object returned in registers was spilled to a slot and reloaded
wherever it stood under a handler - and the harness's own `validate_lowir_text`
refuses it, so **the generated LowIR was invalid**, not merely divergent.
12.2p1 already gives that value a temporary of its own and the copy into it
stands in the step the call belongs to, so it needs no second place to stand.
Reachable with no virtual function in the input - a call returning a small class
beside a live local is enough - and unmissable with one, because a polymorphic
local is exactly such a live local.

**3. 15.4p14 was never asked of the implicitly declared default constructor.**
It kept the `false` `create` gives it, so the standard's own definition
"allowed all exceptions" and every place that reads it armed a handler around a
constructor that throws nothing: `new T[n]` of an implicitly-constructed class,
a class holding an array of them, and a local built beside a throwing call.
`default_construction_nonthrowing` is the same walk 12.1p5's triviality is,
asked of what each subobject's own default constructor allows.

**4. The deleting entry was owed by the table rather than by the definition.**
10.4p2's pure slots name the runtime's own function and ask for nothing, and a
class whose key function stands elsewhere has its table in that unit - so a
definition of a virtual destructor that no other unit may hold got no `D0` at
all. Both the reference and g++ emit it. `owe_deleting_entry` is now the one
place the table and the definition both ask.

**5. The complete-object entry was owed by every derived class.** The walk
marked the user-provided constructors and destructors of every base up to the
first polymorphic one, so a class *between* the vpointer's own class and the
object's gained a `C1` nothing had asked for - and the walk was O(depth) per
class, quadratic over a polymorphic chain. The classes an object is built out of
that this unit's source wrote are the derivation *below* the vpointer: the class
that introduces it and the non-polymorphic classes under it.
`settle_shared_entry_points` runs only for that class, so each class in a
derivation is asked once for the program.

**6. 12.1p11 had no reader in 3.6.2p2's image.** A namespace-scope object of a
class that declares no data member was zeroed and given a `[role=init]` function
that called a constructor whose whole definition is one store. 9p6 leaves such a
class holding nothing but what 10.3p1 gave it, so the image holds the table's
address and the program runs nothing before it - which is what both oracles
emit, and what `vpointer_image` now folds.

**And one performance defect that no checkpoint had a shape for.** 10.3p2's
override matching rebuilt a `std::unordered_map<std::string, unsigned>` over
*every* inherited slot for every class and threw it away again: a chain of 512
classes each introducing 16 virtuals paid **2.1 million string constructions and
hash inserts** to answer sixteen questions per class, and the profile put 40 % of
the settlement in keys it read once. A slot's index is fixed where the name
first took one, so the record belongs to the class that introduced it and a
class below reads it: the records are linear in the declarations rather than
quadratic in the derivation, and the settlement went from **1.12 s to 0.44 s**.

### Changes

| what | where |
| --- | --- |
| 9.8p1 and the ABI's `<unnamed-type-name>` for a class or enumeration a body left unnamed | `sema_scope.h/.cpp`, `sema_analyzer.cpp`, `type_model.h/.cpp`, `lowir_abi.cpp`, `abi_mangle.h/.cpp` |
| an object-valued call result left where 12.2p1's temporary reads it | `lowir_lower_expression.cpp` |
| 15.4p14's exception-specification for the implicit default constructor | `sema_class.cpp`, `sema_analyzer.h` |
| the deleting entry owed by the definition as well as by the table | `lowir_lower.cpp`, `lowir_vtable.cpp`, `lowir_lower.h` |
| the complete-object entry owed by the derivation below the vpointer | `sema_virtual.cpp`, `sema_analyzer.h` |
| 12.1p11 folded into 3.6.2p2's image | `lowir_lower.cpp`, `lowir_lower.h` |
| 10.3p2's override index recorded where the slot was introduced | `sema_virtual.cpp`, `sema_analyzer.h` |
| `class_declaration` split at the seam this widened - which declaration a class-head names | `sema_analyzer.cpp/.h` |

Four regression tests: `300-unnamed-local-class-abi-names`,
`300-static-image-vpointer`, `300-owed-entry-points`,
`300-virtual-call-class-result`.

### Performance Evidence

- **The settlement, measured rather than argued.** 512 classes of 16 new
  virtuals, no object created: **1.12 s -> 0.44 s**, with the 2.1 M string keys
  gone. The whole program, with an object of the deepest class created, 3.66 s
  -> **2.16 s**.
- **What is left quadratic is the program's.** That shape emits 16 K, 48 K,
  162 K, 587 K and **2.2 M** lines of LowIR at n = 32 … 512, because 12.1p11
  makes every base constructor write its own vpointer and one object of the
  deepest class therefore demands all 512 tables.
  `reference-binaries/cppgm++` emits the same program - 162 648 lines against
  our 162 520 at n = 128 - in **13.88 s against our 0.19 s**, and does not
  finish at all at n = 256.
- **Twelve scaling shapes** to 512, of which nine are linear and the two that
  are not are quadratic in the output; the twelfth is the unit count, linear in
  both the units and the output to 512 units with the rename path active
  throughout.
- **Depth is linear and bounded.** A 512-deep expression nesting and a 64-deep
  nest of class definitions each cost the 114 ms an empty program does;
  `parse_depth.h` refuses 1024 rather than recursing into the stack. The
  reference does not finish a 32-deep expression nesting.
- **The generated programs.** `pa9/300-binary-calculator` runs in **1.15 s**
  against its 10 s limit, and the whole pa1-pa18 report is **9.3 s**.

### Validation

- **1777 / 1777** through pa18: pa1-pa17 **1732 / 1732**, pa18 **45 / 45**.
- **File audit passes** for pa18 over `dev/src`, with the four header-weight
  warnings the shared headers have carried since C3 - and no suppression. The
  fix for finding 1 pushed `class_declaration` over the 240-line limit, which is
  why it is split.
- **Every checked `.ref` file in the repository** regenerates byte-identically
  from the reference binaries through `make ref-test`, so no fixture holds our
  own output.
- **102 differential probes** through `reference-binaries/cppgm++`, and g++
  wherever it has an opinion: dispatch through an object, a pointer and a
  reference; covariant pointer and reference returns; pure declarations and a
  pure destructor; virtual `delete` and array `delete[]`; a non-polymorphic
  base under the vpointer; the key function and the external table; 25 declared
  return and parameter types over one class; source order; multiplicity; the
  local-name shapes; the entry-point matrix.
- **16 multi-unit programs**, each in both orders and the three-unit ones in
  four, through a checker that refuses a duplicate top-level entry, a
  declaration beside a definition and a dangling symbol: all valid, and the
  canonical output of a permutation is **byte-identical** to its reverse.
- **Valgrind clean** with `--error-exitcode` over all 45 fixtures, over 102
  probes and over the scaling shapes.

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
locals first, so the two are the same program written two ways.

**Owed to later milestones by their own READMEs**, and refused rather than
mis-lowered: a block-scope static object (pa15 puts it out of scope, pa21 owns
it), `try`/`catch`/`throw` (pa22), a pointer to member function, and multiple
inheritance (PA18 Out Of Scope). `replay_unwind` still reuses one `addr`/`decay`
pair across the elements of a local array where the reference recomputes it per
element.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1 | the class knows what it dispatches: 10.3p2's overriding and the ABI slot order, 10.3p4/p5/p7, 10.4p2/p3, the vpointer's place in 9.2p13's layout, and 4.10p3's null-preserving base cast | `37a34dea` | 7 / 7, in two families - the exits a question about dispatch has at the forms the checkpoint never reached, and the readers of a fact it widened: 9.2p8's `final` on a member that is not virtual **accepted** where g++, clang and the reference all refuse; the same virt-specifier accepted at namespace scope, on a member function defined outside its class, and on a constructor, destructor or conversion function defined outside one, which asked neither half; 9.4.1p2's static member function with an inherited virtual's signature **accepted**, leaving the base's declaration standing in the slot; 12.4p9's `virtual ~B() = 0;` with no form in the special-member production at all, so a valid program inside the Assignment Boundary was **refused outright**; 10.3p7's covariant return asking only that the return type's class derive and not that the base be accessible from the class the override wrote; 12.8p12 read at one of its three readers, so a polymorphic class was **passed by value and returned as bytes** at 5.2.2p4's boundary and its own assignment wrote `copyobj` over the vpointer - **an assignment that changes an object's dynamic type**; and 4.10p3's test asked of the operand's type rather than of the pointer value the step moves, so `p->v` wrote no test and `r.f()` wrote one, each the reverse of the reference, with 5.16p4's `c ? p : q` dragged into its address form beside them | 6 / 29 -> **9 / 32**, three of them the regression tests these leave; pa1-pa17 1732 / 1732; file audit passes; 99 accept/reject probes against clang, g++ and the reference, and 40 lowering probes byte-identical to the reference but for the globals C2 owes |
| C2 | the emitted polymorphic object model: `lowir_vtable.cpp`'s tables, records and name strings, the key function, 12.1p11/12.4p11's vpointer stores, 12.4's D1/D2/D0 triple with 5.3.5p3's deallocation, 10.3p12's dispatch on `SemaFact::dispatches`, and 5.4p4's folded null pointer constant | `c18c1b2c` | 7 / 7, in three families - a fact of the *program* settled per translation unit, the third reader of a fact the checkpoint widened, and the sibling spellings of a rule it landed at one: the table's spelling asked of one unit, so a program whose *second* unit owns the table left the first unit's vpointer stores naming **a symbol nothing defines**; `__cxa_pure_virtual` declared from a per-unit memo, so two units with a pure slot wrote **invalid LowIR** outright; a `declare function` left standing in front of a definition a later unit wrote, which is the same shape and makes any multi-file program invalid; 12.1p11 unread by `vacuous_construction`, so `new T[n]` of a polymorphic class emitted **no element construction at all** and left n vpointers holding whatever the allocation returned; 15.2p2's handler armed around an element constructor that throws nothing, which fixing that made reachable; 12.6.2p6's delegating constructor writing a vpointer the target writes again; and 5.4p4's fold taking `reinterpret_cast<T*>(0)`, which is 5.2.10p5's reading of the integer as an address | 34 / 36 -> **37 / 39**, three of them the regression tests these leave; pa1-pa17 1732 / 1732; file audit passes; all 36 checked `.ref` files regenerated from the reference binary, 44 lowering probes, four multi-unit shapes, seven scaling shapes and a valgrind sweep over all of them |
| C2 audit | the three answers about the *program* moved into `LowirProgramBuilder::finish`, 12.1p11's third reader, and the two spellings C2 over-claimed | `bb5ff708` | 1 / 1, and it is inherited rather than the increment's: all seven of C2's audit fixes hold and were re-derived here - the table's name is one symbol and order-free, `__cxa_pure_virtual` is one declaration of the program, a declaration is dropped in front of a definition, `rename_global` reaches every operand a vtable symbol can occupy, `vacuous_construction`'s memo writes the guard before the walk and the answer after it, and the array-new handler gate is `note_call`'s own reading rather than skipped work. The blocker sat under the whole report: `pa9/300-binary-calculator`'s generated program took **6.84 s against the reference's 0.35 s** for byte-identical output at a 10 s limit, so the pa1-pa17 check was passing on margin and failing on any loaded machine. It was not work but a stall - callgrind puts the two within 14 % on instructions - because CY86 is one writable and executable segment and `Cy86Codegen` laid statements down in source order, so a `data` statement next to code put the program's variables in a line it also fetched instructions from and every store took an x86 machine clear. A loop whose counter shares a line with its body measures **43.9 s against 0.885 s**; the sibling direction, data after code, was **28.1 s against 0.33 s** and had to be written to be found. The fix is the reference's own, settled by probing it | 6.84 s -> **1.15 s**, pa9 21 / 21, the whole pa1-pa17 report 10.3 s and **1732 / 1732**; pa18 holds 37 / 39; nine multi-unit programs, a unit-count sweep to 512, valgrind clean |
| C3, C4 | 9.8p1's `<local-name>` and 2.2p1's file a definition was read from, reviewed together as the two checkpoints the stage's completion landed | `ddb2130e` | folded into the final audit below, because the sibling paths of both are what it found: 9.8p1 was settled for a class the region *names* and never for one it does not, and C4's complete-object rule was owed by the wrong classes |
| final audit | the whole stage, re-derived from the source rather than from the checkpoints: what a class dispatches, where the vpointer stands, what the object file names it, and what the program owes | `ddb2130e` | 6 / 6, in one family - a rule landed at the one shape a fixture reached, asked at the shapes beside it: an unnamed local class named from a per-unit counter, which made two units' classes **one vtable under one object symbol**; a class-typed call result spilled with `load obj<NxM>`, which is **invalid LowIR** and reachable with no virtual function in the input; 15.4p14 unasked of the implicit default constructor, so every array-`new` of an implicitly-constructed class armed a handler around something that throws nothing; the deleting entry owed by the table, so a pure destructor's definition and one whose class's table stands in another unit each **owed a `D0` no unit wrote**; the complete-object entry owed by every derived class rather than by the vpointer's own, which gave a class between them a `C1` nothing asked for and made the walk quadratic; and 12.1p11 unread by 3.6.2p2's image, so an object whose whole storage is the vpointer ran a function before the program to write what the image could hold. Beside them, the settlement's own cost: 10.3p2's match rebuilt a string-keyed index over every inherited slot for every class - **2.1 M string constructions at 512 classes** - where the slot a name first took never moves | 41 / 41 -> **45 / 45**, four of them the regression tests these leave; pa1-pa17 1732 / 1732; **1777 / 1777** in 9.3 s; file audit passes; every checked `.ref` in the repository regenerates byte-identically; 102 differential probes against the reference and g++, 16 multi-unit programs order-free in every permutation, twelve scaling shapes, a depth sweep to 512, the settlement 1.12 s -> 0.44 s, and valgrind clean over all of them |
