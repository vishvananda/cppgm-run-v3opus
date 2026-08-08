# PA18 Audit — `cppgm++ --emit-lowir` scoped polymorphism

A review of each landed checkpoint, in the order a fact travels: declare,
settle, lay out, lower.

## Current Checkpoint Review

**The C2 audit reviewed at `bb5ff708`, the commit that moved three answers about
the program out of the unit that happened to be read first, gave 12.1p11 its
third reader, and let go of the two spellings C2 had over-claimed.** All seven
of its fixes hold, and each was re-derived here rather than taken from the
commit message. The architecture is sound: `LowirProgramBuilder::finish` is the
one place a fact of the *program* is settled, `declared_` is a reference to the
builder's set and not a per-unit memo, and the settlement is two linear passes
over the finished program.

**What this review looked for is whether the fixes are complete over the paths
they touch, and whether the checkpoint's own pass baseline is real.** The
increment came out clean; the one blocker is inherited and sat underneath the
whole report.

**The blocker: a store and an instruction fetch sharing a cache line.** The
required check that pa1-pa17 still pass was failing on
`pa9/tests/300-binary-calculator`, whose generated program timed out. It was not
load. The program took **6.84 s where the reference's took 0.35 s** for
byte-identical output, against a 10 s limit - so the check was passing on margin
and not on merit, and any loaded machine failed it. Under callgrind the two
execute nearly the same number of instructions (2.85 M against 3.25 M), so the
gap was never work: it was a stall. CY86 is one writable and executable segment
and `Cy86Codegen` laid statements down in source order, so a `data` statement
written next to code put the program's variables in a line it also fetched
instructions from, and x86 answers a store into such a line with a machine
clear. Isolated, a loop whose counter shares a line with its own body measures
**43.9 s against 0.885 s** for the same loop whose counter does not - the same
instructions either way.

**The fix is the one the reference already makes**, which six probes through
`reference-binaries/cy86` settled rather than guessed: a body of code that
follows data is emitted on the next 64-byte line, and the labels on it stay
where the data left them with a `jmp rel32` standing in their place. The label
may not move, and both directions of that constraint are pinned by checked
fixtures: `110-hello-world` takes the length of a string as *the label after it
minus the label on it*, so a code label must stay byte-exact against the data
before it, and `500-string-literal-element-alignment` measures 2.14's alignment
*inside* a run, so a run's first statement must keep the offset it had. Data
that follows code moves whole onto its own line instead, which no distance is
measured across - and that direction was the worse of the two, at **28.1 s
against 0.33 s** before the fix.

**The pattern was traced to both ends rather than to the one shape that failed.**
The timing test only reached data-before-code; the sibling direction was found
by writing it, and was 85x rather than 50x. Both now measure what the reference
measures.

**What the review confirmed about the increment itself**, each independently
rather than from the checked fixtures:

- **The table's name is settled once for the program and is order-free.** Two
  units in both orders, and three units in three orders, all emit exactly one
  `global @X__vtable`, with every vpointer store naming it and no
  `__external_vtable__X` left standing. The single-unit program still names the
  external table, which `300-external-key-function-vtable` pins.
- **`__cxa_pure_virtual` is one declaration of the program.** Two units each
  emitting an abstract class's table write exactly one, in both orders. C2's
  per-unit memo wrote two and failed validation outright.
- **A declaration is dropped in front of a definition**, in both orders, and the
  shape reproduces with no virtual function in it.
- **`rename_global`'s reach is complete.** It visits `first`, `second`, `third`
  and `args`; the operand fields it does not visit are `SwitchCase::value`,
  which is an integer constant, and the alias and export lists, whose only
  writer names a constructor or destructor function. No vtable symbol can reach
  them, so the omission is not a defect.
- **`vacuous_construction`'s memo is sound.** The recursion guard is written
  before the walk and the answer after it, and `writes_vpointer` gates both of
  the two places `nothing` is set rather than only the first.
- **The array-new handler gate is a reading, not skipped work.** `throwing` is
  `!constructor.nonthrowing`, the same question `note_call` asks, and the
  cleanup blocks are never reserved rather than reserved and left unreached.
- **No fallback success path, source-specific gate or file-audit bypass** is in
  the increment; the file audit passes with the three header-weight warnings it
  inherited and no suppression.

## Evidence

- **The blocker, measured rather than argued.** 6.84 s -> 1.15 s on
  `300-binary-calculator`, byte-identical output, against the reference's
  0.35 s; the whole pa1-pa17 report 10.3 s. Raising the separation from 64 bytes
  to 4096 measures **1.12 s against 1.15 s**, so no line is shared any more and
  the rest of the distance to the reference is this backend's `movabs`
  addressing rather than a stall. pa9 is 21 / 21 and the two fixtures that pin
  the label constraint both pass.
- **Multi-unit, nine programs** through a checker that refuses a duplicate
  top-level entry, a declaration beside a definition, and a reference to a name
  nothing defines: two units in both orders, three units in three orders, two
  units with a pure slot in both orders, and the declaration-before-definition
  shape in both orders. All nine valid.
- **Scaling of the settlement, which the model had no shape for.** `finish`'s
  two passes are linear in the finished program, with the rename path active
  throughout - the shared class's key function is in the *last* unit read:

  | units | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | `--emit-lowir` over all of them | 0.00 s | 0.01 s | 0.01 s | 0.03 s | 0.06 s | 0.11 s | 0.22 s |
  | emitted LowIR, lines | 1047 | 2046 | 4046 | 8046 | 16075 | 32203 | 64459 |

  64x the units is 64x the output and 0.22 s, and the program is still valid at
  512. `settle_external_declarations` runs for every program including the
  single-unit one, and is two hash-set builds over the finished program;
  `settle_vtable_names` returns before walking at all where the units agree,
  which is every single-unit program.
- **Valgrind** clean with `--error-exitcode` over all five multi-unit shapes and
  over `cy86` on the layout probes and the binary-calculator fixture.
- **The stage baseline is intact**: pa18 37 / 39 with the same two G1/G2
  failures, pa1-pa17 **1732 / 1732**, file audit passing.

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
| C2 audit | the three answers about the *program* moved into `LowirProgramBuilder::finish`, 12.1p11's third reader, and the two spellings C2 over-claimed | `bb5ff708` | 1 / 1, and it is inherited rather than the increment's: all seven of C2's audit fixes hold and were re-derived here - the table's name is one symbol and order-free over two units in both orders and three in three, `__cxa_pure_virtual` is one declaration of the program, a declaration is dropped in front of a definition, `rename_global` reaches every operand a vtable symbol can occupy, `vacuous_construction`'s memo writes the guard before the walk and the answer after it, and the array-new handler gate is `note_call`'s own reading rather than skipped work. The blocker sat under the whole report: `pa9/300-binary-calculator`'s generated program took **6.84 s against the reference's 0.35 s** for byte-identical output at a 10 s limit, so the pa1-pa17 check was passing on margin and failing on any loaded machine. It was not work but a stall - callgrind puts the two within 14 % on instructions - because CY86 is one writable and executable segment and `Cy86Codegen` laid statements down in source order, so a `data` statement next to code put the program's variables in a line it also fetched instructions from and every store took an x86 machine clear. A loop whose counter shares a line with its body measures **43.9 s against 0.885 s**; the sibling direction, data after code, was **28.1 s against 0.33 s** and had to be written to be found. The fix is the reference's own, settled by probing it: code following data is emitted on the next 64-byte line with a `jmp rel32` holding the label byte-exact where the data left it, because `110-hello-world` takes a string's length as the label after it minus the label on it and `500-string-literal-element-alignment` measures 2.14's alignment inside a run | 6.84 s -> **1.15 s**, pa9 21 / 21, the whole pa1-pa17 report 10.3 s and **1732 / 1732**; pa18 holds 37 / 39; file audit passes with its three inherited warnings; 4096-byte separation measures 1.12 s against 64-byte's 1.15 s, so no line is shared any more; nine multi-unit programs through a duplicate/dangling/declaration-beside-definition checker, a unit-count sweep to 512 units that is linear in the output, and valgrind clean over all of them |
