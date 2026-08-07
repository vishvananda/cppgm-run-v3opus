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
  sequences and 13.3.3.2p4's ordering of them, and with them the two expressions
  whose call 13.3 chooses without a callee the program wrote: 5.2.3's explicit
  type conversion and 5.3.4's new-expression, whose allocation function 5.3.4p9
  looks up.
- `lowir_lower*.cpp` reads only the resolved tree. It never re-resolves a name
  and never reads syntax. `lowir_lower_object.cpp` holds the part of it that is
  about one object: 12.1/12.4's lifetime calls, 12.2's temporary, 12.6.2's
  member initializations, 12.8p15's copy, 8.5p5's zero, 8.5.1's aggregate and
  5.3.4p12's object standing at an address rather than in storage a name
  reaches.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, a friendship
relation between two entities, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` /
`base-conversion` / `temporary-object` / `new-expression` nodes, and a
demand-driven definition worklist in the unit lowering.


## Current Failure Map

After C13: 272 / 286 of the fixtures that were checked in at the turn's start,
and 277 / 291 with the five this checkpoint adds. The 14 that remain, 3 refusing
a program the references accept and 11 accepting one and writing a different
shape:

| group | count | what is missing |
| --- | --- | --- |
| 15.2p2's cleanup around a partly built object | 3 | `eh_cleanup` / `eh_try` / `resume` around a constructor's body and around each element of a member array, and the region a destructor's own body carries |
| `#pragma pack` | 2 | a phase-4 fact that has to reach 9.2p13 in phase 7 |
| single defects, one fixture each | 9 | listed below |

The 9 singles, each its own fact: 5.2.4's pseudo-destructor call on a scalar; a
user-defined string-literal operator; `store ptr nullptr` spelled `store ptr 0`,
which two fixtures reach; an incomplete class as the return type of a declared
function, written `obj<0x1>` where the references write `void`; a defaulted
constructor called where the references elide it; an explicit destructor call
through an enclosing namespace's type; a `const` subobject a member function is
called on; and the `_GLOBAL__N_1` name an unnamed namespace gives what it
declares - which 3.5p4's internal linkage and the reference's emission of a
trivial constructor helper stand beside in the one fixture.

Defects no fixture reaches, kept here because a sweep found them and not a test:

- 5.2.9p1's conversion is not written around the operand of a cast to a scalar,
  in either the parenthesized or the functional spelling, so 4.12's `bool` is the
  operand's own value: `(bool)2` and `bool(2)` each pass 2 where the references
  write `cmp ne` and pass 1, and `int(bool(2))` is 2 here and 1 there. It is
  5.2.9's own path and predates the object model. `bool{2}` is right, because
  8.5.4 reads the list through the initialization.
- An aggregate one of whose members is of class type has no constructor built
  from its members (C11), so where such a class is a prvalue of its own - a
  functional cast, an argument, the object a new-expression creates - the
  clauses have nothing to initialize and the program is refused rather than
  written differently: `f(T{V{1,2},0})` and `::new(p) T{{1,2},3}`. The
  references write the synthesized constructor there and build each class-typed
  argument in an `argobj__n` slot, which is the divergence named below.
- An anonymous *struct* member declares nothing, so `s.a` for
  `struct S { struct { unsigned a; unsigned b; }; unsigned c; };` names nothing
  here and 4 bytes are laid out where the references and g++ lay out 12; and a
  member of an anonymous *union* is addressed without the union's own `index`
  step. 9.5p1's injection is written for the anonymous union alone.
- 12.8p7's implicit copy constructor is declared by no class, so
  direct-initialization from an object of the class's own type is refused -
  `YA q(p);`, a mem-initializer `: m(v)`, `YB(YB(2))` - and so is 4.10p3's
  derived object passed to a base parameter by value. Copy-initialization
  reaches the same object through 12.8p31's elision, which is why only the
  direct forms show it.
- A returned prvalue of class type is copied into its storage after the call
  rather than before it: the same three instructions in the other order, because
  the storage is decided in the lowering and not in the tree. A returned prvalue
  no one reads gets no storage at all, where the references give it one.
- A conditional over two lvalues of class type is an lvalue here and an object
  for the references, and `static_cast<const YA&>(YA(4))` names its temporary
  `tmpobj` where they name it `arg`.
- A subscript of a multi-dimensional array decays the row it reached, so
  `w[0][0]` writes one `unary decay ptr` the references do not.
- A clause of an aggregate is evaluated before the storage unit a bit-field
  joins it into is loaded, where the references load and then evaluate. 1.9
  leaves the order open; it is 9.6p2's initialization path and predates C8.
- A cast written on a null pointer constant is one `copy ptr 0` the references
  do not write, so `f((char*)0)` passes a temporary where they pass the constant.
  It is 5.2.9's own path and has nothing to do with the class it is passed to.
- A member named through a qualified-id on an object is refused - `d.YD::f()`
  and `d.YB::f()` alike, with and without a base and with and without a
  using-declaration. It is 5.2.5p1's qualified-id and predates the object model.
- 5.3.4's array form `new T[n]` is refused: it calls `operator new[]` for a
  count 5.3.4p6 lets an expression give, which is a second allocation function
  and a second lifetime this milestone does not write.
- `E a[3] = {}` is 12.6p1's per-element construction here and, for the
  references, one call per element of that same aggregate constructor with a
  zero for every member. The elements are given the same values.

Shapes the references and this unit disagree about what a program means, each
resolved for the standard, and each judged against g++ where g++ reads the
program: 3.4.1p8 looks a name written *before* a qualified declarator-id up
where the declaration stands and not in the class it reaches, so `T C::f()` for
a member `typedef int T;` is refused here and accepted there, which is what g++
does; 10.1p2 asks a base class to be complete, so `struct B; struct D : B {};`
is refused here and accepted there, which is again what g++ does; 8.5p7
zero-initializes a value-initialized object whose class wrote no constructor
*before* the non-trivial one it was given runs, which the references do not
write; 12.1/12.4 run a constructor and a destructor whose body is empty, which
the references elide along with the object's whole lifetime - including
3.7.2p2's per-thread pair and 12.4p8's destruction of a class-typed member of an
aggregate; 8.5.1p11's braces are elided for a member that is an array of
aggregates, which the references refuse and g++ accepts; 8.5.1p2 copy-initializes
a subobject from its clause, so a constructor declared `explicit` is not one a
clause may choose, which the references let it and g++ does not; 8.3.5p5 drops
the top-level cv of a parameter, so the aggregate constructor of
`struct T { const int a; int b; };` is `_ZN1TC1Eii` here and `_ZN1TC1EKii`
there; 3.6.2p2 folds a constructor whose members hold what its arguments do, so
`struct T { int a[2]; int b; }; T v[2] = {{1,2},{3,4}};` at namespace scope is an
image here and a startup body there; 12.8p31 elides `T x = T{...}` into `x`, so
an aggregate holds what 8.5.1 stores where the references call the constructor
built from its members, and a prvalue of such a class an argument asks for is
built by that constructor here too but with no class-typed member passed by
value; the references pass a class holding a bit-field by address where they
pass every other class of the same size by value; 12.9p1's candidate set of
inherited constructors holds the base's parameter-type-list and the shorter ones
its defaulted parameters leave, with 12.9p2's characteristics carrying no
default-argument, where the references inherit one declaration and copy its
defaults onto it; 3.6.2p2's constant initialization covers thread storage
duration, so `thread_local A g{};` holds its zero in the image here and is a
per-thread store there - and where such an object's class has a destructor,
12.4p11 still hands it to `__cxa_thread_atexit` here and is dropped there, which
is what g++ writes; a thread-local array of a class with a non-trivial
constructor, and a thread-local built by a constructor from a written argument,
are constructed here and never constructed there; a thread-local whose
initialization is 3.6.2p1's zero is given no guard here and one there; a use of a
thread-local written before its definition runs what initializes it here and
nothing there, which is what g++'s `_ZTW` wrapper does; a namespace-scope
thread-local scalar whose initializer is a call is accepted here and refused
there; a block-scope object declared `static` or `thread_local` is refused here
and lowered there; an unused `extern thread_local` is a wrapper naming a global
no entry declares there, which is malformed LowIR; and a program that declares
`__builtin_memcpy` itself is accepted here and refused there.

One shape of the references this unit reproduces rather than resolves, because a
fixture asks for it: 8.5.1p2's initialization of a member of a class with no
non-static data member and no base subobject writes nothing at all - not the
call of the constructor the clause chose, and not the clause. 12.8p15's copy of
such a class copies nothing, which is what the shape is written from; a
constructor whose body does something loses that call there. It is 8.5.1's
member alone: an element of an array a declaration names, an argument and a
mem-initializer each write their call, and the constructor's definition is still
what the object file owes.

Three divergences are deliberate and named in the Performance Model: past 64
bytes the zero of a class object is one `zeroinit` where the references write
one eight-byte store however many there are - 512 of them for a 4 KB object; the
tail of a namespace-scope array no clause reached is one `zero` item where they
write one per element - 1 048 575 of them for a 1 MB array; and an array of a
class whose constructor does nothing, or whose whole initialization is 3.6.2p1's
zero, opens no `@__cppgm_init` at all.

15.4p14's `unwind=no` is separate and needs no test result: the relaxed
comparison strips the field, and emitting nothing is silence rather than a false
claim. The four functions 1.4p8 reserves carry it, because there it is a fact of
the reserved function rather than of a declaration this unit read.

## Active Checkpoint

Done: **C13 - 5.3.4's new-expression and 5.2.3p3's braced functional cast**,
272 / 286 from a 269 / 286 baseline and 277 / 291 with five regression tests
added, at pa1-pa15 1174 / 1174.

5.3.4 says two things happen and the resolved tree now holds both: 3.7.4.1's
allocation function is called for the bytes one object of the type occupies,
with 5.3.4p9's lookup starting in the class for an unqualified new-expression
over a class type and in the global namespace for `::new` and for every other
type, and 13.3 choosing among what that lookup reached; and 8.5p16's
initialization of an object of the type at the address the call returned. That
address is the one thing this initialization does not share with a declaration's:
every other object of class type this lowering writes stands in storage a name
reaches, and this one stands at a value, so the `new-expression` node holds the
call and the initialization side by side and the lowering hands the pointer to
the same construction. 5.3.4p8's byte count is one integer constant written with
the type 2.14.2p2 gives a literal of its value and converted where 5.2.2p4
converts any argument, which leaves `std::size_t` a type this unit needs no name
for.

5.2.3p3's `T{...}` is written where `T(...)` is and the one braced list standing
where the arguments do is the whole of what says so, in the parser for a
type-name, for a keyword simple-type-specifier and for a decltype-specifier
alike. What it makes is list-initialized from that list rather than built from
its clauses as arguments, which is 8.5.1 for an aggregate and 13.3.1.7 for any
other class; and 12.8p31 makes `T x = T{...}` create `x` itself, so the braces
reach the object being declared and the subobject an aggregate's clause reached
in the same words a list written on the declarator would.

Two sweeps of 125 programs against `cppgm++-ref` - the allocated type crossed
with the initializer form and with `::`, and every type a functional cast is
written over - left 98 byte-identical after the metadata the relaxed comparison
strips. Of the 27 that remain, 12 are the internal LowIR symbol name or the
top-level order, 4 are the `store ptr nullptr` and 5.2.9 defects the map already
holds, 5 are 12.8p31's elision of `T{...}` into the object it initializes, and 6
are the aggregate with a class-typed member the map now holds.

Next: **C14 - 15.2p2's cleanup around a partly built object**, which is the
largest group left and the three lifetime fixtures it holds.

- owner: `sema_class.cpp` for which subobjects are already built where a
  constructor's body is entered, `lowir_lower_object.cpp` and
  `lowir_lower_body.cpp` for the `eh_cleanup` / `eh_end` / `resume` region and
  the blocks it opens.
- data flow: 12.6.2's member initializations are already written in declaration
  order under the constructor, and 12.4p8's destructions are that list walked
  backwards - so the cleanup a partly built object needs is the tail of the same
  list, taken at the point the construction had reached. The lowering opens one
  region per point rather than recomputing which subobjects those are.
- expected complexity: one region per constructor and one per element of a
  member array, each holding the destructions of the subobjects before it - which
  is what 15.2p2 asks for and is the same n(n+1)/2 the source describes.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`,
  `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`, valgrind over
  the fixtures, and a differential sweep of the subobject kinds a constructor
  can leave half built against `cppgm++-ref`, with each disagreement judged
  against g++.

## Performance Model

Invariants, one line each, and the measurements that hold them.

- Class layout is one pass over `Scope::declarations` where 9.2p2 completes the
  class and is never recomputed; a base contributes its cached size and
  alignment, so a chain of n classes is laid out in n passes. Field access reads
  `SemaEntity::offset`, and 9.6p2's storage unit is the same one pass with a
  carried byte cursor, so n bit-fields cost n. 7.6.2's alignment is one constant
  evaluation and one comparison per specifier.
- 10.2's base chain is a pointer on the region, so a program with no inheritance
  pays one null test per enclosing region. A derived-to-base conversion is one
  node however many classes it spans: 4000 accesses 4000 deep are 20 007 lines
  in 2.0 s, where a node per link had been 16 012 007 lines in 54.6 s.
- 11.3's friendship is a set of entity pairs, asked only after `has_friends`.
  3.4.2's association follows the type rather than searching, with separate
  probes for a region already in the set and a class whose bases are already
  walked: one call with 4000 arguments of 4000 associated classes, 0.29 s.
- 13.3.1.2's candidate set is gathered once per operator expression. One class
  with n friend `operator<<` overloads used n times is quadratic and is meant to
  be - n calls each ranking n candidates: 250/500/1000/2000 at
  0.05/0.15/0.62/2.99 s.
- 7.3.3p1's using-declaration costs the declarations the base has of the name;
  7.3.3p14's hiding is asked once where 9.2p2 completes the class, one pass over
  each brought-in name: n brought-in overloads all hidden, 0.03/0.06/0.13/0.27 s
  at 500/1000/2000/4000, where asking per member declaration had been
  0.05/0.15/0.61/2.53 s. 12.9's inheriting is one pass over the base's chain
  with one probe of 13.1's index per candidate, so a chain n deep costs n per
  class and not n^2.
- 13.1's index is keyed by the chain the class holds and the parameter list, so
  declaring, redeclaring and 12.9p1's "unless the class declares one" are each
  one probe. A hidden declaration is never in it.
- 12.6.2p10's order is `Scope::declarations` with the base before it, walked
  backwards for 12.4p8; mem-initializers are indexed by member name once per
  constructor; a subobject whose default-initialization does nothing gets no
  node. 12.8p25 is one flag settled in the layout pass, and 12.8p15's copy is one
  `copyobj` of the class's span.
- 12.2p1's temporary costs one slot and one constructor selection, and the
  object is named once however many readers it has. A copy is one `copyobj`
  written where the object it goes into is already known, so no call allocates a
  slot for a copy the place asking already owns storage for.
- 12.6p1's array of class type is one action naming the array and n calls, with
  the element addressed a dimension at a time. An array of 2^d elements d
  dimensions deep costs one step per dimension per element - 110 614 lines at
  d = 12 in 0.26 s - which is what the source asks for.
- 3.2p3's emission is a closure from the roots: `emitted_functions_` admits each
  symbol once, and the walk that reads uses stops at a deferred body, so a
  definition the program never reaches is neither read nor written. 4000 unused
  inline functions are 4 lines in 0.07 s, where the same chain reached from
  `main` is 24 003 lines in 0.12 s; 4000 unused `extern` declarations are 4 lines
  in 0.02 s. 11.3p5's friend body is the one exception, and it is read once.
- 3.7.2's thread storage duration costs one flag per declaration. The
  definitions that have it are lowered in one walk of the unit's top level
  before any body is, each written once - the ordinary pass reaches the same
  nodes and writes nothing, which `emitted_globals_` is what says. A definition
  that asks for a body, because 3.6.2p2 left an initialization to run or 12.4p11
  a lifetime to end, gets one wrapper declaration, one guard global and one
  body; a use of the name costs one flag test and, for a thread-local this unit
  initializes, one probe of the object's own name and one call. Measured at
  500/1000/2000/4000, each doubling 2.0-2.3x: n thread-local objects each with a
  constructor, 0.02/0.05/0.10/0.23 s for 26 n + 6 lines; n with only a
  destructor to register, 0.03/0.05/0.12/0.25 s; the same n declared `extern`,
  used in one body and then defined - the shape the pass exists for -
  0.03/0.06/0.12/0.25 s for the same lines plus the n calls; n uses of one of
  them in a body, 0.01/0.01/0.03/0.06 s. `__cxa_thread_atexit` and the image
  handle are declared once per program, by the first object that needs them.
- 7.1.1p1's agreement between the declarations of one variable is one probe of
  the region's own names per declaration: 4000 namespace-scope variable
  declarations are 0.09 s, linear at 2.0-2.3x per doubling.
- 1.4p8's reserved functions are declared by the first use and found by ordinary
  lookup after it, so n calls are one declaration and n resolutions:
  0.01/0.02/0.06/0.11 s at 500/1000/2000/4000. A program that writes none pays a
  prefix comparison on the names no lookup reached, and a use that is not a call
  reaches the same one declaration through the same lookup.
- 8.5p5's zero of a class object is one `zeroinit` past `kZeroSpanLimit`
  (64 bytes), and the tail of a namespace-scope array no clause reached is one
  `zero` item; both are where the references write one store or item per element.
  An object with static storage duration whose whole initialization is 3.6.2p1's
  zero opens no startup body: a namespace-scope array value-initialized by `{}`
  is 14 lines at 500 elements and at 4000, where it had been 5 n + 18.
- The internal LowIR symbol of a function is indexed by the name the object file
  gives it, which is one probe and is what keeps two operator functions of one
  region from collapsing onto one symbol. The ABI's two entry points are two
  definitions of one body, so n such classes write 2 n and not 2^n.
- 8.5.1p2's subobject of class type costs one constructor selection per clause -
  what the source wrote - and 8.5.1p11's elided braces are told from a clause
  that initializes the subaggregate by reading that clause into a line nothing
  keeps, which is one extra reading of one expression and only for a member whose
  class is an aggregate.
- 8.5.1's constructor of an aggregate is declared once and held on the class, so
  n arrays of one class are one function and n calls: 250/500/1000/2000 at
  0.00/0.01/0.02/0.05 s for 3 n + 25 lines and one definition. An array of n
  elements is n calls addressed from one byte cursor - 0.00/0.00/0.01/0.02 s for
  2 n + 32 lines - and an aggregate of n members is one call of n arguments.
- 3.6.2p2's fold of a constructor call reads that constructor's definition once
  per call, from an index of the unit's definitions keyed by what each defines,
  and binds its parameters to the call's arguments in one pass - so an array of
  n folded elements is n walks of one short body and n data items:
  0.00/0.01/0.02/0.05 s at 250/500/1000/2000 for 2 n + 33 lines and no startup
  body at all.
- 5.2.1p1's subscript is lowered once however wide the element is, so an element
  with no register width no longer reads its index twice.
- 3.8p1 makes a return destroy every object of every block it leaves, so n
  nested blocks each holding an object and a return emit n^2/2 calls: 400 deep,
  166 425 lines in 0.36 s. That is what the source asks for.
- A constructor or a destructor defined outside its class is one probe of 13.1's
  index of the class's chain and the same body pass a definition written in the
  class body costs: n classes each with one are 0.63 s at 4000 for 35 n + 9
  lines, and n such definitions of one class's overloads 0.26 s, each doubling
  1.9-2.1x. 9.3p2 says how many of the ABI's entry points the object file owes -
  both for a definition only this unit holds, and one plus an alias for a
  definition every unit that needs one may hold - so a class with n constructors
  writes 2 n and not 2^n.
- 7.1.6.2p1's decltype-specifier before `::` costs one reading of its expression,
  kept by the parser rather than re-parsed, and one lookup in the region that
  type names: 4000 uses in one body are 0.18 s for 24 014 lines. 7.1.1p10's
  `mutable` is one flag on the member's declaration and one mask where 5.2.5p4
  carries the object's cv - 4000 written through a const object, 0.22 s - and a
  class with no mutable member pays one test per access.
- 3.3.7p1's class scope in the PA10 name table costs one scope per class body,
  and 10.2p2's base is one entry per base-specifier - the direct bases alone,
  resolved to the class they name where the base-clause is read, so a chain n
  deep is n entries and a step along it is one probe of the names and one of the
  chain. A name no declaration of the unit wrote is in no region for a prefix to
  reach, which one probe of the declared names settles before any base or any
  7.3.4p2 directive is searched: n classes in a chain each declaring a member
  are 0.02/0.03/0.06/0.12 s at 500/1000/2000/4000, which is what they cost
  before the class scope existed, where the walk without that probe had been
  0.08/0.28/1.13 s. A name that *is* declared n levels up costs the chain, and n
  classes each naming one is the only super-linear axis this milestone has -
  0.09/0.33/1.25/6.06 s at 500/1000/2000/4000 for 15 lines of output at every
  size. It is n steps for each of n classes, which is what a lookup that misses
  at every level asks for; it is named rather than averaged away, and no fixture
  reaches a chain more than a few classes deep. n out-of-class definitions naming a member
  typedef are 0.02/0.05/0.12/0.26 s, and n classes over one base with a member
  and a member function 0.02/0.05/0.12/0.25 s - both linear at 2.1-2.4x per
  doubling.
- 3.4.1p8's region opens a scope only for a declarator-id that names one, so an
  ordinary declaration pays one comparison and the version the template-id memo
  is keyed by does not move.
- 13.1's index is keyed by the written parameter-type-list only where a class
  declares the name; a namespace declares no function with an object parameter,
  so there the type's own list is the list and no list is rebuilt.
- 5.3.4's new-expression costs one qualified lookup of `operator new`, one
  overload resolution over what it reached, and the initialization a declaration
  of the same object costs - no scan of the unit and no second pass over the
  type: n placement new-expressions in one body are 0.04/0.08/0.15/0.30 s at
  500/1000/2000/4000 for 13 n + 36 lines, linear at 2.0x per doubling.
- 5.2.3p3's `T{...}` costs one comparison of the token after a primary-expression
  and, where the braces follow, the one reading of the list the initialization
  would cost anyway: n of them as arguments are 0.02/0.04/0.08/0.15 s at the same
  sizes for 7 n + 24 lines. 12.8p31's elision of one into the object it
  initializes is one probe of the region's names per initializer written as a
  call and nothing at all for any other form: n declarations `T x = T{a, b}` are
  0.03/0.06/0.11/0.25 s for 14 n + 8 lines, linear at 2.0-2.2x per doubling.
- Nested block scopes and nested namespaces cost more than linearly in their
  depth and did before the object model - 4000 nested blocks holding one scalar
  each take 0.23 s, and 4000 namespaces deep with one ADL call at the bottom
  0.24 s. The cost is the lookup walking enclosing regions; it belongs to the
  scope layer.

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
| audit of C7 | 5.2.2p4's copy owned by the call taken out of `converted`, which every initialization, assignment, return, conditional arm and cast reaches, and written at each of those places instead - one `copyobj` into the object already known, and no call's slot allocated for a copy that place already owns storage for; the storage a temporary takes named by what asked for it, which only a call's argument and a return's value do, so a reference a declaration binds keeps `tmpobj__n` and 13.3.3.1.2's temporary reaching a by-value class parameter is the `argobj__n` 12.8p31 makes it; 12.2p1 given to the other prvalue of class type, so a call that returns one by value no longer hands back a value where `this` or a reference needs an object; 12.8p25 made a fact the class carries and asked at the one place a class object is copied, so a copy the program wrote is refused rather than written as the copy of the bytes; a declaration and the initialization under it naming one address rather than two | 186 / 243 held, the same set passing and failing; pa1-pa15 1173/1173; byte-identical passing fixtures 110 -> 116 of 164; valgrind clean over 243 fixtures and 130 probes; eight axes linear and the per-copy slot and instruction growth gone |
| C8 | 12.6p1's array of class type constructed element by element and 12.4p8's destroyed the same way, as one action naming the array that the lowering writes the calls of; an element addressed the way the source would name it, a dimension at a time, which 8.5p7's value-initialized array member, 3.6.2p2's dynamic initialization of a namespace-scope array and both lifecycle calls share; 3.6.2p2's static image of an array of class type, with an element's own padding and a fallback to `@__cppgm_init` for a clause the translation does not know; an aggregate clause's value computed before the address it is stored into; 7.6.2p1's alignment-specifier kept where a decl-specifier stands and read by 9.2p13's layout; 5.3.6p1's `alignof` as an expression; 9.1p2's qualified class-head-name defining the class that region declared | 186 -> 199 / 243; pa1-pa15 1173/1173; valgrind clean over the new fixtures and the probes; a 70-shape differential sweep against `cppgm++-ref` found and closed four defects no fixture reaches; every axis linear at 1.9-2.1x per doubling |
| audit of C8 | an alignment-specifier read from wherever it stood among the decl-specifiers, where 7p1 gives one written before them to what the declaration declares and one written after them to the type those specifiers named, so `struct S { char c; int alignas(8) x; };` laid `x` at 8 and made its class 16 where g++ and the references write 4 and 8; 7.6.2p3's fundamental alignment never asked for, so `alignas(6)` allocated a member at every sixth byte, `alignas(6)` on a class-head made the class six and `alignas(-4)` made it one, all three refused by g++ and by the references; 8.5p7's zero of an object with static storage duration written into a startup body 3.6.2p1 had already made unnecessary, one store per element - `YA g[4000] = {};` at 20 018 lines writing zero into storage the program image holds zero, and the empty `@__cppgm_init` for `YA g = YA();` with it | 199 / 243 held, the same set passing and failing; pa1-pa15 1173 / 1173; byte-identical passing fixtures 127 of 177, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, with `pass=` agreeing on every one; valgrind clean over 243 fixtures and 116 probes; nine axes linear at 2.0-2.2x per doubling and the per-element startup work gone - 14 lines at 500 elements and at 4000; file audit passes with the two recorded header-weight warnings |
| C9 | 7.3.3p1's using-declaration in a class made a declaration of that class per declaration the base has of the name, carrying 11p1's access and naming the base's through `shadowed`, with 13.3.3.1p4's object parameter naming the derived class and 11.2p5 leaving the base subobject `this` reaches unchecked; 7.3.3p14's hiding asked in both orders through a signature that leaves the object parameter out; 12.9's inheriting constructors declared from the base's with 12.9p4's access, the base's parameters and names, and 12.9p8's definition, with 12.1p5's default constructor still given to a class that only inherits; 13.3.3.2p3's cv tie-break kept through 4.10p3's derived-to-base conversion; the ABI's two entry points written as two definitions where a complete object and a base subobject both asked for one | 199 -> 206 / 243; pa1-pa15 1173/1173; valgrind clean over the seven fixtures and 72 probes; a 72-shape differential sweep against `cppgm++-ref` found and closed four defects no fixture reaches, each now a test, and g++ agrees with this unit on the well-formedness and the value of all 72; every C9 axis linear at 1.9-2.3x per doubling |
| audit of C9 | 13.3.1.2's operator expression and 13.4's address of an overloaded name read through `shadowed`, so what a using-declaration brought in is called and named as the base's declaration rather than as a symbol no unit defines, on the base subobject and with the base's definition emitted; 8.3.6's default-arguments read from the declaration that wrote them; a hidden declaration kept out of 13.1's index, so a third overload declared after two were hidden is a function of its own; 7.3.3p14's parameter-type-list made the one a declarator wrote, with 8.3.5p7's cv-qualifier-seq beside it, so a static and a non-static member function of one class hide each other where 9.4.1p2 does not let them overload; that hiding and 12.9p1's "unless the class declares one" both settled where 9.2p2 completes the class, which makes the order the body wrote them in irrelevant, gives a constructor the access of its own section, and takes the hiding from n^2 to one pass; 12.9p1's candidate set read as the shorter parameter lists a base constructor's defaulted parameters leave, with 12.9p2's characteristics carrying no default-argument; the ABI's base-object entry declared where this unit holds no body; a constructor declared with an ellipsis no longer reading one type past its parameter list, in the analysis or in the lowering | 210 / 247 held, the same set passing and failing, and 220 / 257 with ten regression tests added; pa1-pa15 1173 / 1173; byte-identical passing fixtures 143 of 197; valgrind clean over 257 fixtures and 460 synthesized inputs; a 458-program differential sweep against `cppgm++-ref` with every disagreement judged against g++; ten axes linear at 1.9-2.2x per doubling and the n^2 hiding gone (2.53 s -> 0.27 s at 4000) |
| C10 | 3.7.2's thread storage duration made a fact of the variable, with `storage=thread_local`, 3.7.2p2's `_ZTW` wrapper over the object's own encoding, a per-object guarded body where 3.6.2p2 does not settle the initializer, a call of that body at each use of the name, and 12.4p11's end of the lifetime handed to `__cxa_thread_atexit` with the ABI's image handle; 1.4p8's four reserved functions declared by the use that names one, with the object name and the boundary facts a call may assume, and 6.8p1's ambiguity settled for a reserved name in the parser; 3.2p3's emission made a closure from the roots of the unit, with 11.3p5's friend definition the one body the walk still reads, and a declaration of an object this unit does not define written where a use asks for it | 220 -> 228 / 257, and 233 / 262 with five regression tests added; pa1-pa15 1173/1173; valgrind clean over 335 inputs; an 87-program differential sweep against `cppgm++-ref` found and closed five defects no fixture reaches, with 78 of the 87 byte-identical after top-level order and the other 9 judged; every C10 axis linear at 2.0-2.3x per doubling, and the unused-definition and unused-declaration output gone (24 003 lines -> 4 at 4000) |
| audit of C10 | 3.7.2's thread storage duration made a fact of the variable, with the `_ZTW` wrapper, the per-object guarded body, the call at each use and 12.4p11's `__cxa_thread_atexit`; 1.4p8's four reserved functions declared by a use of the name; 3.2p3's emission made a closure from the roots of the unit | 3.7.1p3 and 3.7.2p1's storage duration written as an object of its block, so `thread_local int x = 0;` was one object per call and `static int count = 0;` one automatic object, the refusal having sat where only an object of class type reached it; a use of a thread-local written before its definition running nothing that initializes it, and the map that turns a use into that call keyed by the declaration rather than by the name the object file gives the object; one thread-local's body reading another object of its own thread before anything initialized it; 12.4p11's end of a lifetime reached only from inside the body 3.6.2p2 opened, so a statically initialized thread-local with a destructor registered nothing; 7.1.1p1 cited and not written; 1.4p8's reserved function declared by a written call and by no other use of the name, so its address and its `::`-qualified spelling were refused | 233 / 262 held, the same set passing and failing, and 240 / 269 with seven regression tests added; pa1-pa15 1173 / 1173; byte-identical passing fixtures 150 of 204; valgrind clean over 389 programs - every pa16 fixture source and every synthesized input of the sweep; a 134-program differential sweep against `cppgm++-ref` with every disagreement judged against g++, which agrees with this unit on each of the three the audit turns; seven axes linear at 2.0-2.3x per doubling, the unused-definition closure still 4 lines at 4000; file audit passes once 3.5's linkage and 3.7's storage duration came out of `init_declarator` as one unit |
| C11 | 8.4.2p4's user-provided special member made a fact of the declaration that neither defaulted nor deleted it; 8.5.1p2's subobject of class type copy-initialized from the clause that reached it - list-initialized from a braced one, converted from an expression, 12.8p31's copy from one of its own class - with 8.5.1p7 value-initializing one no clause reached and 8.5.1p11's elided braces left for the clause that cannot initialize the subaggregate alone; 13.3.1.7's element of an array of class type made an object of its own, built by the class's own constructor or by the one 8.5.1 gives an aggregate from its non-static data members, declared once and held on the class; 3.6.2p2's constant initialization through a constructor whose members hold what its arguments hold, which keeps a namespace-scope array an image; 12.1p5's deleted default constructor for a member of const-qualified or reference type that nothing initializes; 5.2.1p1's subscript of an element with no register width read once | 240 -> 244 / 269, and 251 / 276 with seven regression tests added; pa1-pa15 1173/1173; valgrind clean over 1315 programs - every pa16 fixture source and every synthesized input of both sweeps; two differential sweeps against `cppgm++-ref`, 405 programs over class shape x initializer form x storage duration and 648 over the constructors a subobject of class type reaches, leaving no status disagreement at all, with the two defects the first found closed and all 57 the second found judged for this unit by g++; every C11 axis linear at 2.0-2.5x per doubling, one synthesized constructor however many arrays name the class, and the namespace-scope array still an image and not a startup body |
| C12 | 3.3.7p1's member name made a fact of the class that declares it, so the PA10 name table stopped declaring a member into the region around its class; 3.4.1p8's region put in force for the rest of a qualified declarator and for the body after it, in the parser and in the analysis; 10.2p2's base recorded as the class the base-clause reaches, resolved once, walked where a name misses; 8.3.5p2's trailing-return-type as the type a function returns, with 7.1.6.4's `auto` standing for it alone; 13.1's index keyed by the parameter-type-list a declarator wrote wherever a class declares the name, which 9.3.1p3's object parameter had collapsed; 7.1.1p10's `mutable` stopping the const an object carries into a member; 7.1.6.2p1's decltype-specifier before `::` carried to the semantics as its expression; 5.1.1p6's parenthesized callee, with 3.4.2p1's associated namespaces the one thing the parentheses take away | 251 -> 258 / 276, and 265 / 283 with seven regression tests added; pa1-pa15 1173/1173; valgrind clean over 403 programs - every pa16 fixture source and every synthesized input of both sweeps; two differential sweeps against `cppgm++-ref`, 113 programs over where a name is declared x where it is used x the region between them and 10 over source order and the sibling paths of an out-of-class definition, leaving 111 and 9 byte-identical after canonicalization and every disagreement either out of PA16's scope or resolved for this unit by g++; the class scope's cost held to what it was before it existed (1.13 s -> 0.04 s at 2000) and every new axis linear at 2.1-2.4x per doubling |
| audit of C12 | 3.3.7p1's member name made a fact of the class that declares it; 3.4.1p8's region for the rest of a qualified declarator and the body after it; 10.2p2's base; 8.3.5p2's trailing-return-type; 13.1's index keyed by the written parameter-type-list; 7.1.1p10's `mutable`; 7.1.6.2p1's decltype-specifier before `::`; 5.1.1p6's parenthesized callee | a constructor or destructor defined outside its class declared nothing and defined nothing, the node reaching the arm of `declaration` written for an access-specifier, so the unit called `@YA__YA` and only declared it - and a definition matching no declaration and one written twice were accepted; 3.4.1p8's region opened after the parameter clause and the mem-initializers had been read, so `YA::YA(int v) : n((held)v)` was not a translation unit; the ABI's two entry points counted from the uses rather than asked of 9.3p2, which left a strong definition owing one name where the object file owes both; 7.1.6.2p1 written for the type a declaration names and for nothing else, so `decltype(a)::held` in an expression was refused; the expression that specifier carries written into the syntax tree, which is PA10's own output; 7.1.1p10's refusal asked only where the declarator went on to declare an object | 266 / 283 from a 265 / 283 baseline, the same set passing and `200-nested-out-of-class-constructor-enclosing-type` with it, and 269 / 286 with three regression tests added; pa1-pa15 1174 / 1174 with the pa10 test the AST-dump defect asks for; byte-identical passing fixtures 152 of 216; valgrind clean over 438 programs; an 88-program differential sweep against `cppgm++-ref` leaving 77 byte-identical and every disagreement named or judged for this unit by g++; eight axes measured, seven linear at 1.9-2.1x per doubling and 10.2p2's chain named with its numbers; file audit passes with the two recorded header-weight warnings |
| C13 | 5.3.4's new-expression: 3.7.4.1's allocation function found by 5.3.4p9's lookup - the class first for an unqualified one over a class type, the global namespace for `::new` and for every other type - chosen by 13.3 from the byte count 2.14.2p2 spells and the new-placement's own arguments, with 8.5p16's object built at the address it returned rather than in storage a name reaches; 5.2.3p3's `T{...}` in the parser for a type-name, a keyword simple-type-specifier and a decltype-specifier, list-initialized from its braces by 8.5.1 for an aggregate and 13.3.1.7 for any other class; 12.8p31's elision of `T{...}` into the object it initializes, at a declaration and at an aggregate's clause alike; 8.5.1p2's member of a class that holds nothing written as the nothing the references write | 269 -> 272 / 286, and 277 / 291 with five regression tests added; pa1-pa15 1174/1174; valgrind clean over 183 programs - every pa16 general fixture source and every synthesized input of the sweeps; two differential sweeps against `cppgm++-ref`, 125 programs over the allocated type x the initializer form x `::` and over every type a functional cast is written over, leaving 98 byte-identical after the stripped metadata and each of the other 27 named or judged; three new axes linear at 2.0-2.2x per doubling |
