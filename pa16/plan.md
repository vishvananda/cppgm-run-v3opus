# PA16 Plan — `cppgm++ --emit-lowir` object model

PA16 is complete: **306 / 306** of its fixtures, **1494 / 1494** through pa16,
the file audit passing with the two recorded header-weight warnings, and no
axis of the milestone growing faster than the source it is written from but the
two 3.8p1 and 10.2p2 axes named in the Performance Model.

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs, and the object model is added as typed facts
at those owners rather than as a second pipeline:

- `ast_parser*.cpp` and `ast_names.h` own the syntax boundary and 6.8p1's
  ambiguity. A block item is read as a declaration first, and a declaration
  needs its decl-specifier-seq to name a type - so what says whether a name
  could be one is every declaration of the spelling the unit wrote, kept per
  spelling as the kinds it was declared with. That is what lets `f(c);` be the
  call it looks like and 3.4.2 be asked about it.
- `sema_scope.*` owns declarations, regions and lookup. Where a member sits in
  its object, which special member function a declaration declares, which class
  a class derives from and which class befriended it are facts about the
  declaration, so they live on its own `SemaEntity`; the region it declares
  carries the one edge 10.2's lookup follows.
- `sema_class.cpp` owns what a class *is*: 10p1's base-clause, 9.2p13 layout
  with the ABI's empty subobjects, 11 access, 11.3 friendship, 12.1/12.4's
  special members and 9.5p1's anonymous class. Each is settled from the same
  one fact - what the class's own region declares, in the order it declares it -
  once, where 9.2p2 completes the class.
- `sema_lifetime.cpp` owns what running them comes to: 12.1p5 and 8.5's chosen
  constructor, 12.6.2p10's order, 12.4p8's reverse of it, 12.2p1's temporary,
  and 3.7.1/3.8p1's region. Both halves read the same `SemaEntity` and neither
  re-reads syntax the other read; a question about a subobject is asked in the
  same words whether it is a base, a member or an object of its own.
- `sema_operator.cpp` owns the calls ordinary lookup does not name: 13.3.1.2's
  candidate set for an operator expression, 13.5p6's rule on a non-member
  operator, and 3.4.2's associated namespaces and classes - which an ordinary
  unqualified call whose lookup found nothing asks for too.
- `sema_analyzer.cpp` walks the syntax, resolves names and types, and hands each
  class to that owner once, where 9.2p2 makes it complete. It also asks the one
  question about a declaration that no declaration answers: where in the token
  stream it stands, which is what 16.6's `#pragma pack` is read by.
- `preprocessor.cpp` owns 16.6. A pragma is a phase 4 directive whose effect is
  on phase 7, so what it leaves behind is a value and an epoch counter;
  `ast_tokens.*` turns that into a `PackTable` - the positions the value changes
  at - and the class layout asks it at the `}` 9.2p2 completes the class on,
  which the class-specifier carries as its own `completed`.
- `sema_expression.cpp` owns what a token is worth: 2.14's literal as one line,
  2.14.8's user-defined-literal as the call of the literal operator its
  ud-suffix names, and 5.2.4p1's pseudo-destructor call. *Which* of 2.14p1's
  literals a terminal is is `string_literal.cpp`'s question: `scan_literal`
  lexes the terminal back into the pp-tokens phase 3 read it as, and every layer
  that reads a parsed literal asks that one owner. It also owns 5.2.5p1's member
  access, whose id-expression may be a qualified-id naming a class 10.2's chain
  reaches.
- `sema_overload.cpp` owns 13.3, including 4.10p3/8.5.3p4's derived-to-base
  sequences and 13.3.3.2p4's ordering of them, and with them the two expressions
  whose call 13.3 chooses without a callee the program wrote: 5.2.3's explicit
  type conversion and 5.3.4's new-expression.
- `lowir_lower*.cpp` reads only the resolved tree. It never re-resolves a name
  and never reads syntax. `lowir_lower_object.cpp` holds the part of it that is
  about one object: 12.1/12.4's lifetime calls, 12.2's temporary, 12.6.2's
  member initializations, 12.8p15's copy, 8.5p5's zero, 8.5.1's aggregate and
  5.3.4p12's object standing at an address rather than in storage a name
  reaches.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.
- `lowir_lower.cpp` owns 3.6.2p2's image: what one item of it holds is 5.19's
  fold for an integral type and, for a floating one, the digits 2.14.4 says are
  the value.

The facts the object model adds: field offsets on members, a base class on the
class, the ABI's empty subobjects on the class, a friendship relation between
two entities, an implicit-object argument in 13.3, `constructor-action` /
`destructor-action` / `member-initialization` / `base-conversion` /
`temporary-object` / `new-expression` nodes, whether a constructor-action is the
object 12.8p31 elided a prvalue into, whether a literal is 8.5p7's zero, how
many consecutive elements one action builds, and a demand-driven definition
worklist in the unit lowering. The lowering's own such fact is the walk down to
a subobject - the object, the member, then one subscript per dimension - carried
as the walk rather than as the address it produced, so that 15.2p2's handler,
which stands in a block naming nothing another block made, writes the same
naming again.

## Performance Model

One line per invariant, and the measurement that holds it. Every axis is linear
in what the source wrote but the two named at the end.

- Class layout is one pass over `Scope::declarations` where 9.2p2 completes the
  class and is never recomputed; a base contributes its cached size and
  alignment. Field access reads `SemaEntity::offset`, and 9.6p2's storage unit
  is the same one pass with a carried byte cursor, so n bit-fields cost n:
  2000 of them are 0.06 s. 7.6.2's alignment is one constant evaluation and one
  comparison per specifier.
- The ABI's empty subobjects are a list on the class, built from the base's and
  each member's; a class with none answers "does this member collide" before
  asking anything, which is one comparison per member. n classes each with an
  empty base and an empty member are 0.02/0.04/0.10/0.22 s at 500/1000/2000/4000.
  A chain of n empty classes is 0.03/0.08/0.26 s at 1000/2000/4000, which is the
  n distinct subobjects an object of the n-th class actually has.
- 10.2's base chain is a pointer on the region, so a program with no inheritance
  pays one null test per enclosing region. A derived-to-base conversion is one
  node however many classes it spans: n classes deep are 0.00/0.01/0.02/0.05 s
  at 250/500/1000/2000 for 16 lines at every size.
- 11.3's friendship is a set of entity pairs, asked only after `has_friends`.
  3.4.2's association follows the type rather than searching, with separate
  probes for a region already in the set and a class whose bases are already
  walked. 3.4.2's candidate set for an ordinary call is gathered only where
  ordinary lookup found nothing: 4000 hidden-friend calls in one body are
  0.02 s for 2 n + 19 lines.
- 6.8p1's answer for a name no scope in force declares is one probe of a map
  from spelling to the kinds the unit declared it with, asked after every scope,
  using-directive and base has been asked - so a name that is declared pays
  nothing for it.
- 13.3.1.2's candidate set is gathered once per operator expression. One class
  with n friend `operator<<` overloads used n times is quadratic and is meant to
  be - n calls each ranking n candidates. n *distinct* operators each used once
  are 0.04/0.13/0.57/2.22 s at 250/500/1000/2000, which is the lookup each of
  the n classes pays.
- 7.3.3p1's using-declaration costs the declarations the base has of the name;
  7.3.3p14's hiding is asked once where 9.2p2 completes the class, one pass over
  each brought-in name. 12.9's inheriting is one pass over the base's chain with
  one probe of 13.1's index per candidate.
- 13.1's index is keyed by the chain the class holds and the written
  parameter-type-list, so declaring, redeclaring and 12.9p1's "unless the class
  declares one" are each one probe. A hidden declaration is never in it.
- 12.6.2p10's order is `Scope::declarations` with the base before it, walked
  backwards for 12.4p8; mem-initializers are indexed by member name once per
  constructor; a subobject whose default-initialization does nothing gets no
  node. 12.8p25 is one flag settled in the layout pass.
- 15.2p2's cleanup costs one list of the subobjects a constructor has built and
  one handler block per step that needs different destructions from the step
  before it - the list only grows, so equal length is equal content and one
  probe answers. Past `kUnwindSuffixLimit` (16) a handler destroys the one
  subobject the step before it built and enters that step's own handler, which
  is the chain 12.4p8's suffix already used: n members of class type are
  8146/15896/31396/62396/124396 lines at 250/500/1000/2000/4000 in
  0.01/0.02/0.05/0.11/0.22 s, where writing them out had been 6 053 039 lines
  and 12.48 s at 2000.
- 12.6p1's array of class type is one action naming the array. Past
  `kArrayLoopLimit` (16) elements it is one loop over an index the function
  holds - in the analysis, which leaves 8.5.1p7's tail as one action carrying
  how many elements it is; in the construction and the destruction; and in
  12.4p8's suffix, which counts that array as one step. `YA w[n]` as a member, a
  local and a namespace-scope object is 147 lines at n = 50 and at n = 4000, and
  an array subobject with two clauses and a tail is 199 lines at every size.
  Under the limit the elements are written out, which is what the references
  write and what the checked-in fixtures hold.
- 15.2p2's handler for a loop reads the loop's own index back and destroys that
  many elements, which is the same loop run backwards - so an exception out of
  the k-th element costs the k destructions it owes and no more.
- 3.2p3's emission is a closure from the roots: `emitted_functions_` admits each
  symbol once, and the walk that reads uses stops at a deferred body. 4000
  unused inline functions are 4 lines in 0.07 s; the same chain reached from
  `main` is 24 003 lines in 0.12 s. 11.3p5's friend body is the one exception.
- 3.7.2's thread storage duration costs one flag per declaration; the
  definitions that have it are lowered in one walk of the unit's top level
  before any body is, each written once. n thread-local objects with a
  constructor are 0.02/0.05/0.10/0.23 s at 500/1000/2000/4000.
- 1.4p8's reserved functions are declared by the first use and found by ordinary
  lookup after it: 0.01/0.02/0.06/0.11 s at the same sizes.
- 8.5p5's zero of a class object is one `zeroinit` past `kZeroSpanLimit`
  (64 bytes), and the tail of a namespace-scope array no clause reached is one
  `zero` item. An object with static storage duration whose whole initialization
  is 3.6.2p1's zero opens no startup body.
- 8.5.1's constructor of an aggregate is declared once and held on the class, so
  n arrays of one class are one function and n calls. 3.6.2p2's fold of a
  constructor call reads that constructor's definition once per call, from an
  index keyed by what each defines.
- The walk down to a subobject is written again wherever an address is asked
  for, which is one `decay` and one step per dimension and never twice for one
  use. An element or a difference counted in bytes writes no scale where the
  element is one byte.
- 9.5p1's anonymous class is one object no name reaches, and a member of it is
  one offset from the enclosing object however many anonymous classes stand
  between them - the chain the analysis holds is what the lowering adds up, so
  an access is one `index`. n anonymous structs in one class are
  0.01/0.02/0.04/0.10 s at 250/500/1000/2000.
- 5.2.5p1's qualified-id in a member access costs one lookup of the prefix in
  the object's class and, where that misses, one where the expression stands:
  2000 of them in one body are 0.04 s.
- 3.3.7p1's class scope in the PA10 name table costs one scope per class body,
  and 10.2p2's base is one entry per base-specifier. A name no declaration of
  the unit wrote is in no region for a prefix to reach, which one probe settles
  before any base or 7.3.4p2 directive is searched.
- 16.6's packing costs one integer comparison per token while the stream is
  built and one record per directive that changed the value; 9.2p13 asks one
  binary search per class definition, and an empty table answers before
  searching.
- 2.14.8's user-defined literal costs one lookup of the literal-operator-id and
  one walk of the declarations it reached, comparing interned type ids. Which of
  2.14p1's literals a terminal is is asked of phase 3 by lexing that terminal's
  own spelling back into pp-tokens, paid once per literal the tree holds.
- Two axes grow faster than the source and are named rather than averaged away.
  3.8p1 makes a return destroy every object of every block it leaves, so n
  nested blocks each holding an object and a return emit n^2/2 calls - which is
  what the source asks for. And a name that *is* declared n levels up a chain of
  n classes costs the chain at every level: 0.09/0.33/1.25/6.06 s at
  500/1000/2000/4000 for 15 lines of output at every size, which is a lookup
  that misses at every level and no fixture reaches a chain more than a few
  classes deep. Nested block scopes and nested namespaces cost more than
  linearly in their depth for the same reason and did before the object model.

## Architecture Review

Reconstructed from the source rather than from the checkpoints, the stage is
four boundaries and one rule about each.

**Syntax to semantics.** The PA10 parser answers "what could this be" and the
analysis answers "what is it". The one place the two have to agree is 6.8p1,
because a statement that reads as a declaration is one - so the parser carries
what the unit has declared each spelling as, and the analysis never has to undo
a declaration reading. Nothing below the parser reads a token again except
`scan_literal`, which lexes a terminal's own spelling back into pp-tokens
because that is the question phase 3 answered and no later layer should
re-answer.

**Class facts to object actions.** `sema_class.cpp` settles everything about a
class once, where 9.2p2 completes it: its layout, its access, its special
members, its friends, its empty subobjects. `sema_lifetime.cpp` settles
everything about one object where the program names it. The seam holds because
the first half never asks about an object and the second never re-derives a
class fact.

**Semantics to lowering.** The lowering reads the resolved tree and nothing
else. Every question it could ask of syntax is instead a typed fact on a node -
which constructor, which destructor, which member, which element, how many
elements, whether the zero comes first, whether the prvalue was elided. That is
what lets the lowering be one walk that emits in source order and never
rewrites what it emitted.

**Lowering to the object file.** `lowir_abi.cpp` is the one owner of what the
object file calls a declaration, and 9.3p2 rather than a count of uses is what
says how many of the ABI's two entry points a special member owes.

The three costs the stage watches are the ones a source of size n could turn
into output larger than n: 12.6p1's array (a bound written as one number),
15.2p2's handlers (a class's members), and 12.4p8's suffix (the same). All
three are now the same answer - write them out while a reader wants to count
them, and past that write the order as a loop or a chain.

## Final Architecture Review

This audit re-derived the stage from the source and the README rather than from
the checkpoint record, and found seven blockers. All seven are fixed and are in
the ledger below. What is left is what a sweep found and no fixture asks for,
and each entry is a place this unit and the references disagree about what a
program means, resolved for the standard and judged against g++ wherever g++
reads the program.

Deliberate divergences from the references, each resolved for g++:

- 9.2p13's empty subobjects one level in: the references give an empty base
  offset zero and then let a member of a class *containing* one stand there too,
  and misalign it doing so; g++ and this unit do not.
- 12.4 runs a destructor whose body is empty, which the references elide along
  with the object's whole lifetime - including 3.7.2p2's per-thread pair and
  12.4p8's destruction of a class-typed member of an aggregate.
- 8.5p7 zero-initializes a value-initialized object whose class wrote no
  constructor before the non-trivial one it was given runs.
- 3.7.2's thread storage duration: 12.4p11's `__cxa_thread_atexit`, 3.6.2p2's
  constant initialization of a thread-local, the initializer a use written
  before the definition runs, and 12.6p1's construction of a thread-local
  array's elements. The references write none of the four; g++ writes all four.
- 3.4.1p8 looks a name written *before* a qualified declarator-id up where the
  declaration stands; 10.1p2 asks a base class to be complete; 8.3.5p5 drops a
  parameter's top-level cv; 8.5.1p2 copy-initializes a subobject from its
  clause, so an `explicit` constructor is not one a clause may choose; 8.5.1p11
  elides braces for a member that is an array of aggregates; 12.9p1's candidate
  set is the base's parameter-type-list and the shorter ones its defaults leave.
- 16.6 acts on every form of `#pragma pack` and reads the value at the `}` that
  completes the class; 2.14.8 keeps a character literal's ud-suffix, has p3's
  raw fallback, and passes p3's `unsigned long long` and `long double`;
  2.14.5p13 lets the parts of one sequence carry the same ud-suffix; 5.2.4p2
  asks for a scalar type.
- 3.5p4 gives a variable in an unnamed namespace internal linkage, and PA14's
  encoder writes the ABI's local-name marker on the last component of a data
  name that has it - `_ZN12_GLOBAL__N_1L1xE` here and by g++.

Presentation this unit writes leaner than the references and that no fixture
asks for: an `index` or a `div` by a one-byte element writes no scale; a
qualified-id naming a data member keeps the base subobject step the references
drop; a member of an anonymous class is one step and not two; a cast between
two `i64` types of different signedness writes no `copy`; a constant cast is
the immediate it produces; past 16 elements or 16 subobjects the order is a
loop or a chain where the references write it out.

Constructs outside the milestone that are refused rather than written: 12.8p7's
implicit copy constructor and everything that needs it, 5.3.4's array form,
block-scope `static` and `thread_local` objects (PA15 puts them out of scope),
an anonymous struct outside a class, and 5.19's fold of a floating operand.
5.2.9p1's conversion around a cast to `bool` was the one of these that was a
defect rather than a boundary, and it is written now.

The one shape of the references this unit reproduces rather than resolves,
because a checked-in fixture asks for it: 8.5.1p2's initialization of a member
of a class with no non-static data member and no base subobject, from a prvalue
of its own class carrying arguments or braces, writes nothing at all.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean |
| C3 | 12.1/12.4 user-declared constructors and destructors, 13.3.1.3/13.3.1.4/8.5.4p3 constructor selection, 12.6.2 member initializations, 12.4p8 member destructions, 3.8p1 lifetime at block exit and in `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31's elision, 5.2.4's explicit destructor call, the ABI's two entry points | 70 -> 102 / 243 |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset 0, 9p2's injected-class-name, 10.2p2/p6 lookup, 11.2/11.4 access, 12.6.2p5/12.4p8 order, 4.10p3/8.5.3p4/5.2.9p11 as one `base-conversion` node, 5.9p2's composite pointer type; `sema_class.cpp` split out | 102 -> 126 / 243 |
| C5 | 13.3.1.2p1's operator expression as the call it stands for, 13.5p6, 11.3p6's friend declared into the innermost enclosing namespace, 3.4.2p1/p2/p3's associated namespaces and classes, 3.4.3's prefixes tried outward, 3.2p3's uses read from the resolved tree | 126 -> 161 / 243 |
| C6 | 9.6p1's width and the four facts it settles on the member, 9.6p2's allocation into storage units, the read-shift-mask and the read-modify-write, 5.17/5.3.2 over a field, 3.6.2p2's static data as the bytes the fields' bits fall in | 163 -> 173 / 243 |
| C7 | 12.2p1's prvalue of class type made an object the function holds, 8.5.3p5's reference to it, 13.3.3.1.2p1's user-defined conversion sequence, 5.2.2p4's argument copy, 12.8p15's memberwise copy, 8.5p7's zero; `lowir_lower_object.cpp` split out | 174 -> 186 / 243 |
| C8 | 12.6p1's array of class type constructed and destroyed element by element, an element addressed a dimension at a time, 3.6.2p2's static image of one, 7.6.2p1's alignment-specifier among the decl-specifiers, 5.3.6p1's `alignof`, 9.1p2's qualified class-head-name | 186 -> 199 / 243 |
| C9 | 7.3.3p1's using-declaration in a class, 7.3.3p14's hiding through a signature that leaves the object parameter out, 12.9's inheriting constructors with 12.9p4's access and 12.9p8's definition, 13.3.3.2p3's cv tie-break through 4.10p3 | 199 -> 206 / 243 |
| C10 | 3.7.2's thread storage duration with `_ZTW`, a per-object guarded body, a call at each use and 12.4p11's `__cxa_thread_atexit`; 1.4p8's four reserved functions; 3.2p3's emission made a closure from the roots | 220 -> 228 / 257 |
| C11 | 8.4.2p4's user-provided special member, 8.5.1p2's subobject of class type copy-initialized from its clause, 13.3.1.7's element of an array of class type, 3.6.2p2's constant initialization through a constructor, 12.1p5's deleted default constructor | 240 -> 244 / 269 |
| C12 | 3.3.7p1's member name as a fact of the class, 3.4.1p8's region for the rest of a qualified declarator, 10.2p2's base resolved once, 8.3.5p2's trailing-return-type, 13.1's index keyed by the written parameter-type-list, 7.1.1p10's `mutable`, 7.1.6.2p1's decltype-specifier before `::`, 5.1.1p6's parenthesized callee | 251 -> 258 / 276 |
| C13 | 5.3.4's new-expression with 5.3.4p9's lookup and 8.5p16's object at the address it returned; 5.2.3p3's `T{...}`; 12.8p31's elision of it; 8.5.1p2's member of a class that holds nothing | 269 -> 272 / 286 |
| C14 | 15.2p2's cleanup around a partly built object, at the granularity of the call; 12.4p8's suffix given the same shape and chained past `kUnwindSuffixLimit`; 15.2p2's odr-use asked of the whole list at once | 283 -> 286 / 296 |
| C15 | 16.6's `#pragma pack` carried from phase 4 to 9.2p13; 2.14.8's user-defined literal as the call p2 says it is; 5.2.4's pseudo-destructor call on a scalar; a concatenated string-literal rebuilt from its parts | 297 -> 301 / 306 |
| C16 | 3.9p5's incomplete return type written `void`; 3.9.1p10's `std::nullptr_t` as `i64`; 8.5.1p1's destination dropped where the list writes nothing; 7.3.1.1p1's `_GLOBAL__N_1` as a second prefix and a second name | 309 -> 313 / 314 |
| C17 | 12.1p5 read as what running a constructor *does* rather than what the class wrote, memoized per constructor | 313 -> **314 / 314** |
| final audit | 12.6p1's array and 15.2p2's handlers as a loop and a chain; 6.8p1's ambiguity and 3.4.2 for an ordinary call; the ABI's empty subobjects; 5.2.9's conversion to `bool`; 9.5p1's anonymous struct; 5.2.5p1's qualified-id; six fixtures no reference binary wrote; `sema_lifetime.cpp` split out | 305 -> 299 / 299 with the six removed, **306 / 306** with seven regression tests; pa1-pa16 **1494 / 1494** |
