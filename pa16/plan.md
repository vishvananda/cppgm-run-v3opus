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
  12.2p1's temporary, and 3.7.1/3.8p1 lifetime. Each is settled from the same one fact - what the
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
  and never reads syntax. `lowir_lower_object.cpp` holds the part of it that is
  about one object: 12.1/12.4's lifetime calls, 12.2's temporary, 12.6.2's
  member initializations, 12.8p15's copy, 8.5p5's zero and 8.5.1's aggregate.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, a friendship
relation between two entities, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` /
`base-conversion` / `temporary-object` nodes, and a demand-driven definition
worklist in the unit lowering.

## Current Failure Map

After C7: 186 / 243. The 57 that remain, 32 refusing a program the references
accept and 25 accepting one and writing a different shape:

| group | count | what is missing |
| --- | --- | --- |
| arrays of class type | 8 | 12.6p1 element-by-element construction and 12.4p8 destruction, an array element initialized by an expression, 3.6.2 dynamic initialization of a namespace-scope one; 2 of them write a shape rather than refuse |
| class using-declarations, inheriting constructors | 7 | 7.3.3p1 into a class, 12.9; 2 write a shape rather than refuse |
| `alignas` / `alignof` | 4 | 5.3.6 outside the constant subset, `alignas` on a member |
| a reference member whose storage is addressed before what binds it | 3 | the two operands in the order the references evaluate them |
| `thread_local` | 3 | `storage=thread_local` and the accessor function the references write |
| `__builtin_*` names | 3 | `__builtin_memcpy`, `__builtin_unreachable` |
| the exception cleanup regions around a partly built object | 2 | `eh_cleanup` / `eh_try` / `resume` in a destructor body |
| `#pragma pack` | 2 | a phase-4 fact that has to reach 9.2p13 in phase 7 |
| placement `new` | 2 | 5.3.4's placement form |
| a trailing return type on a member function | 2 | 8.3.5p2 with `auto` |
| a program the parser does not read | 2 | a nested braced member initializer, a reference member named after its class |
| single defects, one fixture each | 24 | listed below |

The 24 singles, each its own fact: an aggregate member of class type no clause
reaches; `mutable` under a `const` member function; an object of a nested class
the enclosing class declared privately; `(a.f)(x)`; a static and a non-static
member function of one class reaching one overload key, because 9.3.1p3 put the
object parameter in the type; 5.2.4's pseudo-destructor call on a scalar; a
user-defined string-literal operator; `decltype` in a qualified-id;
`__builtin_expect` folded where the references keep the definition;
`store ptr nullptr` spelled `store ptr 0`; an incomplete class as the return
type of a declared function, written `obj<0x1>` where the references write
`void`; one dead `addr` before an aggregate initialization that writes nothing;
a defaulted constructor called where the references elide it; an out-of-class
constructor definition not matched to its in-class declaration; an array
subscript, which converts its index where the references multiply it as it
stands and evaluates it twice in the read form; an explicit destructor call
through an enclosing namespace's type; `std::nullptr_t` as a parameter type,
written `i64`; the `_GLOBAL__N_1` name an unnamed namespace gives what it
declares; a `declare global` for an `extern` object nothing uses; 13.5.6's
`operator->` not read as a call; and four more that differ only inside one
function body.

Two defects no fixture reaches, both of the anonymous-member group: an anonymous
*struct* member declares nothing, so `s.a` for
`struct S { struct { unsigned a; unsigned b; }; unsigned c; };` names nothing
here and 4 bytes are laid out where the references and g++ lay out 12; and a
member of an anonymous *union* is addressed without the union's own `index`
step. 9.5p1's injection is written for the anonymous union alone.

One divergence is deliberate and named in the Performance Model: past 64 bytes
the zero of a class object is one `zeroinit` where the references write one
eight-byte store however many there are - 512 of them for a 4 KB object.

15.4p14's `unwind=no` is separate and needs no test result: the relaxed
comparison strips the field, and emitting nothing is silence rather than a false
claim.

## Active Checkpoint

Done: **C7 - the class prvalue that has to stand somewhere**, 174 -> 186 of 243
with pa1-pa15 held at 1173/1173. 12.2p1 now makes a prvalue of class type an
object the function holds: `T(args)` and `T()` declare a temporary no name
reaches and run the constructor 8.5/13.3.1.3 chooses on it, and its storage is
named after what asked for it - `tmpobj__n` where the expression wrote it,
`arg__n` where an argument's reference binding made it, `argobj__n` where the
argument is passed by value. 13.3.3.1.2's user-defined conversion sequence is
the same temporary reached from the argument side, ranked below every standard
conversion sequence. 5.2.2p4's argument of class type is a copy the call owns,
which 12.8p31 lets a prvalue be created in rather than copied into, and 12.8p15
makes memberwise - so a class that holds nothing moves nothing. 8.5p7's
zero-initialization of a value-initialized class with no user-provided
constructor is the zero of its bytes, which for such a class is also nothing.

Next: **C8 - the array of class type**, which is 6 of the remaining refusals and
2 of the shape diffs.

- owner: `sema_class.cpp` for 12.6p1's per-element construction and 12.4p8's
  reverse destruction, which are the same `construct_object` and
  `destructor_action` asked of an element rather than of an object;
  `lowir_lower_object.cpp` for the element cursor the actions are written over,
  and `lowir_lower.cpp` for 3.6.2p1's namespace-scope array, whose elements are
  constructed in `@__cppgm_init` and destroyed in `@__cppgm_fini`.
- data flow: an `array-lifecycle` node holding one `constructor-action` and one
  `destructor-action` written against an element index, so the tree names the
  element rather than the loop, and the lowering decides between writing the n
  actions out and writing the loop that runs them.
- expected complexity: one constructor selection per array declaration and not
  per element; the output is n actions while n is small enough to be a
  description of the array and one loop past that, which is the same choice
  `kZeroSpanLimit` already makes for a span of zero bytes.
- beside it: an array element initialized by an expression PA15 does not lower,
  which is the same element cursor reached from 8.5.1 rather than from 12.6.
- what is not in it: 12.6.2's array member of a class with a bit-field, whose
  dynamic initialization the references write and which needs 3.6.2p2's static
  image first.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`,
  `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`, valgrind over
  the array fixtures, a sweep of array bound and of element class size, and a
  differential sweep of both through `cppgm++-ref`.

## Performance Model

- 12.2p1's temporary costs one slot and one constructor selection, both of which
  a declaration of an object of the same class already costs, and the object is
  named once however many readers it has: the slot is made the first time the
  node is reached and the address it was constructed at is what every later
  reader uses, so a temporary bound to a reference and then read through it
  emits one `addr` and not two. Measured at 500/1000/2000/4000, each doubling
  1.9-2.3x: n written temporaries in one full-expression each bound to a
  `const T&`, 0.01/0.02/0.04/0.09 s; n by-value class arguments,
  0.01/0.01/0.03/0.07 s; n arguments reaching a class parameter through
  13.3.3.1.2's converting constructor, 0.01/0.02/0.03/0.08 s; n statements each
  calling a temporary functor, 0.01/0.02/0.05/0.11 s; n value-initialized
  32-byte class objects, 0.02/0.04/0.08/0.17 s. Nesting `f(T(...))` 25 to 200
  deep - the parser's depth limit - costs two output lines per level.
- 13.3.3.1.2's search is one walk of the declarations of the class's constructor
  name, so a class with n constructors costs n per argument that reaches it and
  a class the argument already has the type of never starts the walk.
- 12.8p15's copy is one `copyobj` of the class's own span rather than one store
  per member, and a class that holds nothing is written nothing at all - which
  is what `TypeTable::is_empty_class` answers in one probe, from the flag 9.2p13
  layout already computed.
- 8.5p5's zero of a class object is written as the widest stores that fit while
  the object is small enough for that to be a description of it, and as one
  `zeroinit` past `kZeroSpanLimit` (64 bytes). The references have no such
  limit: they write one eight-byte store per eight bytes however many there are,
  which is 512 instructions for a 4 KB object and 131072 for a one-megabyte
  member. No fixture reaches past 64 bytes, and the same limit already governs
  8.5.1p7's span of value-initialized array elements, so the two agree.
- 9.6p2's allocation unit is the same one pass: the layout carries a byte cursor
  and the open unit's type, first byte and used bits, so a bit-field costs one
  comparison and one addition and a class with no bit-field never opens a unit.
  Each access reads `bit_width`, `bit_offset` and `bit_access` off the member
  and emits a fixed number of instructions, so n accesses to a field cost n.
  Measured at 500/1000/2000/4000, each doubling 2.0-2.3x: n one-bit fields in
  one class, aggregate-initialized then assigned one by one,
  0.02/0.05/0.10/0.23 s; n fields of four alternating types, so every one opens
  its own unit, 0.02/0.03/0.07/0.12 s; n classes nested one inside the next,
  each with two fields, 0.01/0.02/0.06/0.13 s; n reads of two fields in one
  body, 0.02/0.04/0.08/0.16 s; n namespace-scope objects of a two-field class,
  each dynamically initialized, 0.02/0.04/0.10/0.20 s.
- Which storage units an initialization has already written is one carried byte
  count per open initialization - a bit-field may take its unit whole exactly
  when the unit begins at or after it - so a class with n bit-fields is
  initialized in n steps and not n^2. Under 9.6p2's unit model that count also
  says a unit can never reach back over a member the initialization has already
  written, which is what keeps a whole-unit store out of the base subobject and
  out of the members beside the field.
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
| C6 | 9.6p1's width read as a constant expression and the four facts it settles put on the member's own declaration - `bit_field`, `bit_width`, `bit_offset` and 4.5p3's `bit_access`; 9.6p2's allocation into storage units by a layout cursor counted in bits, with the unnamed zero-width separator and the field that would straddle a unit moved on; a read that loads the unit at the promoted type, shifts the field down and masks it, while the value keeps the type the member was declared with; 9.6p2's write as a read-modify-write, and as a plain store where the initialization still owns every byte of the unit; 8.5.1's unnamed field stepped over by the clauses; 12.6.2 and 8.5.1 writing the two instruction orders the references write; 5.17 and 5.3.2 over a field, with 4.5p3's promoted type for the arithmetic; 5.3.1p3 and 5.3.3p1 refusing the address and the size of one; 3.6.2p2's static data written as the bytes the fields' bits fall in rather than as the units they are read through | 163 -> 173 / 243; pa1-pa15 1173/1173; all ten bit-field fixtures byte-identical; valgrind clean over the fixtures and 8 probes; layout and static-data bytes agree with g++ over 17 layout shapes and 9 value shapes; every axis linear |
| audit of C6 | 9.6p2's storage unit made a run of bytes with its own width, alignment and type, opened by the first field that cannot share the one before it and shared only by fields declared with its type - so a bit-field no longer packs into the bytes of the member before it and a derived class's field no longer covers the base subobject its constructor then stored over; the unit loaded and put back at the signed integer of its own width where 4.5p3's promoted type had named a type narrower or wider than the storage; an initialization joining the unit as an expression of the member's own type, with 4.5's promotion and 4.7's conversion at each step; both masks dropped for a field that owns every bit of its unit; 3.6.2p2's static image given to `@__cppgm_init` where a data item cannot name a share of a unit, and the zero of a unit written once for every field in it; a clause let through to an unnamed bit-field; an assignment converting its value before naming the object it writes into; a constant initializer spelled as the value it produces; 4.12's conversion to `bool` compared at the type of what it converts | 173 -> 174 / 243, nothing that passed before failing after; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and 87 probes; every bit-field shape the reference accepts agrees byte for byte over 17 layout shapes and 15 declared types; every axis linear |
| C7 | 12.2p1's prvalue of class type made an object the function holds - `T(args)` and `T()` declaring a temporary no name reaches, 8.5/13.3.1.3 choosing its constructor, and the storage named `tmpobj__n`, `arg__n` or `argobj__n` after what asked for it; 8.5.3p5's reference bound to that object; 13.3.3.1.2p1's user-defined conversion sequence as the same temporary made by a converting constructor, ranked below every standard conversion sequence and above the ellipsis, with 12.3.1p2's `explicit` left out and one user-defined conversion per sequence; 5.2.2p4's argument of class type copied into a generated `argobj__n` slot the call is passed, with 12.8p31 creating a prvalue argument in that slot rather than copying into it; 12.8p15's copy made memberwise, so a class that holds nothing moves nothing - at an argument and at a parameter's entry alike; 8.5p7's zero-initialization of a value-initialized class with no user-provided constructor, written as the zero of its bytes; 12.2p3's temporary of a class with a non-trivial destructor refused rather than left alive past the full-expression; the object model of the lowering split out into `lowir_lower_object.cpp` | 174 -> 186 / 243; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and the probes; 18 of 21 synthesized prvalue, by-value, conversion and value-initialization shapes byte-identical to the reference, the other 3 differing only in the `zeroinit` limit; every axis linear at 1.9-2.3x per doubling |
