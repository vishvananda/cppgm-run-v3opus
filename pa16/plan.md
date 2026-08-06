# PA16 Plan — `cppgm++ --emit-lowir` object model

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs:

- `sema_scope.*` owns declarations, regions and lookup. Where a member sits in
  its object, which special member function a declaration declares, which class
  a class derives from and which class befriended it are facts about the
  declaration, so they live on its own `SemaEntity`; the region it declares
  carries the one edge 10.2's lookup follows.
- `sema_class.cpp` owns the object model: 10p1's base-clause, 9.2p13 layout, 11
  access, 11.3 friendship, 12.1/12.4/12.6.2 special members and subobject order,
  and 3.7.1/3.8p1 lifetime. Each is settled from the same one fact - what the
  class's own region declares, in the order it declares it - and a question
  about a subobject is asked in the same words whether it is a base, a member or
  an object of its own.
- `sema_operator.cpp` owns the calls ordinary lookup does not name: 13.3.1.2's
  candidate set for an operator expression, 13.5p6's rule on a non-member
  operator, and 3.4.2's associated namespaces and classes.
- `sema_analyzer.cpp` walks the syntax, resolves names and types, and hands each
  class to that owner once, where 9.2p2 makes it complete.
- `sema_overload.cpp` owns 13.3, including 4.10p3/8.5.3p4's derived-to-base
  sequences and 13.3.3.2p4's ordering of them.
- `lowir_lower*.cpp` reads only the resolved tree. It never re-resolves a name
  and never reads syntax.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, a friendship
relation between two entities, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` /
`base-conversion` nodes, and a demand-driven definition worklist in the unit
lowering.

## Current Failure Map

After C5 and its audit: 163 / 243. The 80 that remain, 44 refusing a program the
references accept and 36 accepting one and writing a different shape:

| group | count | what is missing |
| --- | --- | --- |
| LowIR shape diffs, the program otherwise accepted | 36 | see below |
| refusals, scattered | 22 | see below |
| bit-fields | 10 | 9.6 layout and access |
| class using-declarations, inheriting constructors | 8 | 7.3.3p1 into a class, 12.9 |
| `alignas` / `alignof` | 4 | 5.3.6 outside the constant subset, `alignas` on a member |

Of the 36 shape diffs, the named ones are: 8.5p8's zero-initialization of a
value-initialized class with no user-provided constructor; the exception cleanup
regions the references write around partially constructed and partially
destroyed subobjects (`eh_cleanup` / `eh_try` / `resume`); arrays of class type
constructed and destroyed element by element; a `declare global` written for an
`extern` object nothing uses; an aggregate's reference member, whose storage
this unit addresses before it reads what binds to it; the `_GLOBAL__N_1` name an
unnamed namespace gives what it declares; and an argument of class type passed
by value, which the references give a generated `argobj__n` slot and this unit
writes as a whole-object load — the same class-prvalue gap C6 is for, reached
from the argument side rather than refused.

The 22 scattered refusals are, grouped by what they ask for: a class prvalue
that has to be materialized — `T(args)` bound to a reference parameter, an
object of class type passed by value, 13.3.3.1.2's user-defined conversion
sequence — which is 9 of them; namespace-scope arrays of class type, 5; the
`__builtin_*` names, 3; placement `new`, 2; a trailing return type on a member
function, 2; and one of `mutable`. Two more stand alone and are named in the
audit: 13.5.6's `operator->` is not read as a call, and a static and a
non-static member function of one class reach the same overload key because
9.3.1p3 put the object parameter in the type, so
`struct block { void unlink(); static void unlink(block*); };` is refused as a
redefinition — the fix is to put which kind of member a declaration is into the
key beside the parameter list.

15.4p14's `unwind=no` is separate and needs no test result: the relaxed
comparison strips the field, and emitting nothing is silence rather than a false
claim. It is the whole difference in 6 of the 163 passing fixtures; 34 more
differ only in top-level order and 2 only in an internal symbol name, both of
which the README makes a presentation convention.

## Active Checkpoint

Done: **the audit of C5**, reviewed at `1bd1885f`. 161 -> 163 of 243, with
pa1-pa15 held at 1173/1173. Seven findings, in two shapes: one walk asked to
answer two different questions - 3.4.2p2's associated set read as if every class
in it had had its bases walked, and the climb to the innermost enclosing
namespace read as if every class it passed were the class the type is a member
of - and a rule written for the exits it had in hand and not the one beside
them: 11.2p5 and 11.4p1 asked at `.` and `->` but not where an operator names a
member on an object, 13.5p6 written for its non-member half and not its
static-member one, 9.4.1p2's `static` read from the definition's own specifiers
so that an out-of-class definition of a static member declared a second
function, `- + * &` given the unary Itanium terminal because the arity left out
the operand 9.3.1p3 had put in the type, and a pointer condition branched on
through a comparison the references do not write. See `audit.md`.

Next: **C6 — the class prvalue that has to stand somewhere** (12.2p1, 5.2.3p2,
8.5.3p5, 13.3.3.1.2), which is what 9 of the remaining refusals ask for and what
several shape diffs need: `T(args)` is a temporary the unit gives storage to and
runs a constructor on, a reference parameter binds that storage, and
13.3.3.1.2's user-defined conversion sequence is the same temporary made by a
converting constructor. The by-value argument the references write into a
generated `argobj__n` slot is the same fact reached from the argument side, so
it is part of this checkpoint rather than of PA17's copy semantics.

- owner: `sema_class.cpp` for the constructor selection, which is
  `construct_object` over an object no declaration named; `lowir_lower_body.cpp`
  for the slot the temporary stands in.
- data flow: a `temporary-object` node holding the same `constructor-action` a
  declaration writes, so the lowering allocates one generated slot and calls
  what the action names. The refs name that slot three ways and slot names are
  not canonicalized before comparison: `arg__n` for a temporary a written
  argument is, `argobj__n` for the storage an argument of class type is passed
  in, and `tmpobj__n` for one no argument named.
- expected complexity: one constructor selection per written temporary, which is
  what a declaration of an object of the same class already costs.
- what is not in it: 12.2p3's destruction at the end of the full-expression,
  which needs the full-expression boundary the lowering does not mark yet - so a
  temporary of a class with a non-trivial destructor is refused rather than
  silently left alive.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`,
  `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`, valgrind over
  the temporary and by-value fixtures, and a sweep of temporaries per
  full-expression and of nesting depth.

## Performance Model

- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
  A base contributes its own cached size and alignment, so a chain of n derived
  classes is laid out in n passes and not n^2.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- 10.2's base chain is a pointer on the region, walked only where a class has
  one: a lookup in a program with no inheritance pays one null test per
  enclosing region, and one with inheritance pays the depth of the hierarchy
  rather than its size.
- 11.3's friendship is a set of entity pairs held by the model, and every access
  check asks `has_friends` first, so a program with no friend declaration pays
  one test per region it walks and nothing else. 11.2p5's naming class adds a
  walk of the object's base chain, taken only for a protected member the
  declaring class did not already grant.
- 13.3.1.2's candidate set is gathered once per operator expression: one lookup
  in the operand's class, one unqualified lookup, and one pass over the
  associated namespaces and classes the operand types reach. Which declarations
  the set already holds is a probe, so gathering costs the declarations there
  are and not their square.
- 3.4.2's association follows the type rather than searching: the depth of the
  type and of the base chain, both of which the source wrote. Whether a region is
  already in the set and whether a class's base chain has been walked are two
  separate probes, so a second argument of one type stops at once while a class
  the set holds without its bases does not stop the walk. One call with 4000
  arguments of 4000 distinct associated classes gathers its set in 0.29 s.
- A derived-to-base conversion is one `base-conversion` node however many
  classes it spans. n accesses to a member d classes up cost n*d to analyse and
  emit n instructions rather than n*d: 4000 accesses 4000 deep are 20 007 lines
  in 2.0 s, where writing a node per link was 16 012 007 lines in 54.6 s.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, and 3.2p3's uses are read from the whole resolved tree in one walk, so a
  function used n times is lowered once and a body no written body asked for is
  asked for once, after them.
- The internal LowIR symbol of a function is indexed by the name the object file
  gives it, which is one probe and is what keeps two operator functions of one
  region that flatten to one base name with the same parameter types from
  collapsing onto one symbol.
- 12.6.2's mem-initializers are indexed by member name once per constructor, so
  a class with n members each named in the ctor-initializer costs n lookups
  rather than n^2 comparisons.
- 12.6.2p10's order is `Scope::declarations` with the base before it, so one pass
  writes every subobject initialization, and 12.4p8 walks the same list backwards
  with the base after it.
- 12.1p5: a subobject whose default-initialization does nothing gets no node at
  all, so an empty base or member costs nothing in the tree or in the output.
- A slot named after an identifier another slot already took starts from the
  suffix that identifier last used, so n blocks declaring one name cost n steps.
- A jump out of a set of blocks writes one destructor action per object those
  blocks hold, which is what 3.8p1 asks for. Whether any object is alive at all
  is a carried count, not a walk of the open blocks.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in under 0.01 s.
- Measured after the audit of C5, sizes 500/1000/2000/4000, each doubling
  2.0x-2.4x: one call with n arguments of n distinct associated classes,
  0.02/0.06/0.12/0.29 s; base-chain depth with one ADL call two arguments deep,
  0.00/0.01/0.03/0.06 s; n ADL calls each four classes above the friend,
  0.01/0.02/0.05/0.10 s; n calls whose first argument is a nested enum and whose
  second is a class eight deep, 0.01/0.03/0.06/0.11 s, and that shape by chain
  depth with one call, 0.00/0.01/0.03/0.08 s; a chained `operator<<`,
  0.00/0.01/0.01/0.03 s; operator nesting depth, 0.01/0.01/0.03/0.06 s; n
  namespaces each with a class and an ADL free function, one use each,
  0.04/0.10/0.21/0.43 s; n friend declarations revealed by as many
  namespace-scope definitions, 0.03/0.07/0.15/0.35 s.
- One class with n friend `operator<<` overloads used n times is quadratic and
  is meant to be - n calls each ranking n candidates: 250/500/1000/2000 take
  0.05/0.15/0.62/2.99 s. Scanning the candidates already gathered had made it
  cubic.
- Nested namespace depth is super-linear and belongs to the scope layer: 500 to
  4000 namespaces deep with one ADL call at the bottom take 0.01/0.02/0.07/0.24 s,
  unchanged by the object model.
- Measured after the audit of C4, sizes 500/1000/2000/4000, each doubling
  2.0x-2.3x: derived-to-base conversions in one body four deep,
  0.01/0.02/0.03/0.07 s; accesses to a base's member in one body four deep,
  0.01/0.01/0.03/0.08 s; chain depth with one access to the root member,
  0.01/0.02/0.04/0.09 s; 11.4p1 protected accesses through a five-deep chain,
  0.00/0.00/0.01/0.02 s; classes in a chain with nothing used,
  0.01/0.02/0.04/0.09 s; a chain of 100/200/400/800/1600 derived classes each
  with its own member and constructor, 0.00/0.01/0.02/0.05/0.10 s;
  500/1000/2000/4000 classes each deriving from one base, constructed and
  called, 0.04/0.09/0.20/0.43 s.
- Measured earlier and unchanged, each doubling about 2.1x-2.3x: 4000 default
  member initializers in one class, 0.06 s; 4000 mem-initializers in one
  constructor, 0.07 s; 4000 locals with destructors in one block, 0.09 s; 4000
  namespace-scope objects constructed and destroyed, 0.09 s; 4000 loops each with
  a `break` leaving one object, 0.33 s; 2000 constructor overloads chosen between
  for one call, 0.06 s; a single `break` unwinding 800 nested blocks, 0.04 s;
  8000 members laid out and initialized twice, 0.14 s; 4000 member accesses and
  calls in one body, 0.20 s; nested aggregate depth 800, 0.80 s.
- 3.8p1 makes a return destroy every object of every block it leaves, so n nested
  blocks each holding an object and a return emit n^2/2 calls: measured 400 deep
  at 0.36 s for 166425 lines. That is what the source asks for, not a re-walk.
- Nested block scopes cost more than linearly in their depth, and did before the
  object model: 4000 nested blocks holding one scalar each take 0.23 s. The cost
  is the lookup walking enclosing regions; it belongs to the scope layer.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
| audit of C1-C2 | 9.4.2p2's definition told from its declaration by the declarator-id; 12.6.2p8 refused rather than dropped; 7.6.2p1's type-id form; 5.2.5p1's object expression kept or refused, never dropped; 13.3.3.2p3 ordering two sequences that differ only in qualifiers; 11p6's naming context; 9.3p2 read from where the definition is written; O(n^2) slot naming | 65 -> 70 / 243; pa1-pa15 1173/1173; valgrind clean over 249 inputs; every axis linear |
| C3 | 12.1/12.4 user-declared constructors and destructors in a class body, chained on the class; 13.3.1.3/13.3.1.4/8.5.4p3 constructor selection over 8.5's four initializer forms with `explicit`; 12.6.2 member initializations in declaration order, 12.6.2p8 default member initializers, 12.4p8 member destructions; 3.8p1 lifetime at block exit, at `return` and in 3.6.3p1's `@__cppgm_fini`; 8.4.2/8.4.3 `= default` and `= delete`; 12.8p31 copy elision from a value of the object's own type; 5.2.4 explicit destructor calls; C1/C2 and D1/D2 ABI names with the `alias object` line | 70 -> 102 / 243; pa1-pa15 1173/1173; valgrind clean on the new paths |
| audit of C3 | six ways out of a region that ended no lifetime - `break`, `continue`, `goto`, the for-init-statement's own region, a static data member at shutdown, an aggregate initialized from braces; 9.3.2p1's `this` in a destructor separated from 12.4p12's object parameter; a deleted destructor refused where the object is declared; `= T(...)` no longer refused; a mem-initializer that names nothing or is written twice refused; 9.3p2's inline read from where the definition is written; one `_` per character an identifier cannot hold; the goto check made a carried count | 102 / 243 held; pa1-pa15 1173/1173; valgrind clean over 273 inputs; every axis linear |
| C4 | 10p1's base-clause recorded on the class and its region; 9.2p13 layout with the base subobject at offset 0; 9p2's injected-class-name; 10.2p2/p6 lookup through the base chain; 11.2p2/p4 and 11.4p1's access; 12.6.2p5's base initialization first and 12.4p8's base destruction last; 12.1p5/12.4p3 triviality through the base; 4.10p3, 8.5.3p4 and 5.2.9p11's conversions as one `base-conversion` node with 13.3.3.1.4p1's rank; 5.9p2's composite pointer type; 5.16p3's conditional over a class and a base of it; the object model split out into `sema_class.cpp` | 102 -> 126 / 243; pa1-pa15 1173/1173; valgrind clean over 243 inputs and the sweeps |
| audit of C4 | one derived-to-base conversion written as one node however many classes it spans; 11.2p5 asked of every link; 5.16p6's operands brought to the composite pointer type; 8.5.3p4's base half of reference-related; 5.2.9p11's reference downcast written and its pointer downcast access-checked; 12.4p11's destructor access asked for an object, a member and a base; 11.4p1's additional check on a protected member named through an object; the ABI entry a constructor or destructor stands under made a fact the analysis records | 126 / 243 held; five `.ref` files newly byte-identical; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and 88 probes; the n*d output blow-up gone |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for - member candidates from 13.3.1.2p3, non-member ones from ordinary lookup with the member functions left out, 13.3.1.2p4's first operand offered to both, and the built-in operator left to the caller where nothing is viable; 13.5.7p1's `x++0`; 13.5.3/13.5.4/13.5.5's member-only `= () []`; 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and bound nowhere, 7.3.1.2p3 revealed by a matching declaration there, 11.3p11's elaborated-type-specifier declared in that namespace too, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward, without which `nnn::f(a)` parsed as a declaration; 3.2p3's uses read from the whole resolved tree; two operator functions of one region no longer collapsing onto one internal symbol | 126 -> 161 / 243; pa1-pa15 1173/1173; valgrind clean over the 42 operator, friend and ADL fixtures and the sweeps; chain, nesting, namespace-depth, ADL-multiplicity and reveal axes all linear, and the one quadratic axis - n overloads ranked by n calls - made quadratic rather than cubic |
| audit of C5 | 3.4.2p2's base chain abandoned wherever the class was already associated - which it is, without its bases, whenever it is the class a nested type is a member of; every class around a nested type associated where the clause associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither operator-call site; a member `- + * &` given the unary Itanium terminal because the arity left out the operand 9.3.1p3 put in the type; an out-of-class definition of a static member function given an object parameter, declaring a second function the unit then called with no argument and, where `inline`, never defined; 13.5p6's static-member half; a pointer condition branched on through a `cmp ne ptr` the references do not write | 161 -> 163 / 243, nothing that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; every ADL association axis linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of every unary and binary form of `+ - * &` agree with g++ |
