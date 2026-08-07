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
  class to that owner once, where 9.2p2 makes it complete. It also asks the one
  question about a declaration that no declaration answers: where in the token
  stream it stands, which is what 16.6's `#pragma pack` is read by.
- `preprocessor.cpp` owns 16.6. A pragma is a phase 4 directive whose effect is
  on phase 7, so what it leaves behind is a value and an epoch counter;
  `ast_tokens.*` turns that into a `PackTable` - the positions the value changes
  at - and the class layout asks it where the definition ends.
- `sema_expression.cpp` owns what a token is worth: 2.14's literal as one line,
  2.14.8's user-defined-literal as the call of the literal operator its
  ud-suffix names, and 5.2.4's pseudo-destructor call, which names no function
  at all and is the value of its operand discarded.
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
- `lowir_lower.cpp` owns 3.6.2p2's image: what one item of it holds is 5.19's
  fold for an integral type and, for a floating one, the digits 2.14.4 says are
  the value - which is why the fold refuses a floating operand rather than
  answering with the integer no floating literal has.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, a friendship
relation between two entities, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` /
`base-conversion` / `temporary-object` / `new-expression` nodes, whether a
constructor-action is the object 12.8p31 elided a prvalue into and whether a
literal is 8.5p7's zero rather than a constant the program wrote, and a
demand-driven definition worklist in the unit lowering. The lowering's own such
fact is the walk down to a subobject - the object, the member, then one subscript
per dimension - carried as the walk rather than as the address it produced, so
that 15.2p2's handler, which stands in a block naming nothing another block made,
writes the same naming again.


## Current Failure Map

After C15: 301 / 306 of the fixtures that were checked in at the turn's start,
and 307 / 312 with the six it adds. C15 closed the whole "the construct has no
path at all" group - the two `#pragma pack` fixtures, 5.2.4's pseudo-destructor
call and 2.14.8's literal operator - so what remains is 5 fixtures that accept
the program and write a different shape, each its own fact:

| fixture | what differs |
| --- | --- |
| `100-incomplete-class-return-function-address` | an incomplete class as the return type of a declared function, written `obj<0x1>` where the references write `void` |
| `200-const-subobject-member-call` | one `addr $m` written twice for a `const` subobject a member function is called on |
| `200-friend-derived-private-base-defaulted-constructor` | a defaulted constructor called where the references elide it, which is the empty-body elision named below |
| `200-unnamed-namespace-hidden-friend-single-definition` | the `_GLOBAL__N_1` name an unnamed namespace gives what it declares, beside 3.5p4's internal linkage and the reference's emission of two trivial constructor helpers |
| `300-operator-nullptr-t-from-zero` | a `std::nullptr_t` parameter lowered as `ptr` here and `i64` there, with `0` for the argument rather than `nullptr` |

Defects no fixture reaches, kept here because a sweep found them and not a test:

- 15.2p1 and 15.2p2's region around a whole *full-expression* is not written.
  This milestone puts a region around the call that builds a subobject; the
  references put one around every full-expression that can throw while an object
  is still owed a destruction. Three shapes are the same one rule: an object a
  declaration named earlier in the block, so that `A a; A b;` in a function body
  is two plain calls here and a region there and a destructor's body owing its
  12.4p8 suffix wraps the block-scope objects it declares; a *call* written as a
  mem-initializer's argument, so `: a(), b(side())` is one region here and two
  there; and a new-expression written in one, which is a region there and none
  here. It is a checkpoint of its own because the value the expression produces
  has to cross the region - the references materialize it into a `$call__n` slot
  - and because the live objects of a block are a stack that shrinks rather than
  the growing list 12.6.2's steps are. 109 of the 1 097 programs of the C14 audit
  sweeps are this and nothing else. No checked-in `.ref` has an `eh_try` outside
  a constructor.
- The ABI's two entry points are counted from every use the *analysis* read,
  including uses inside definitions 3.2p3's closure never emits, so a class no
  program reaches makes its base owe both names: `struct B { ~B(); };
  struct D : B { ~D(); }; int main() { B b; }` writes two definitions of `~B`
  where the references write one and an alias. The failure mode is bounded - the
  extra name is a definition nothing calls, never a call of a name nothing
  defines - but answering it properly needs the closure to run before the naming
  is decided, which is a pass this milestone does not have. 3 of the 1 097
  sweep programs are this.
- 12.6p1's array of class type is written as its elements however many there
  are, where the references write a loop past 16 of them, with an index object
  of the function and a second one for the elements an exception unwinds. This
  predates the object model - the elements were always unrolled - but 15.2p2's
  per-element handler now makes the unrolled form quadratic: `struct X { A w[n]; };`
  is 7946 lines at n = 50 here and 126 there. It is named in the Performance
  Model with its numbers and is the checkpoint after this one.
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
- 5.19 folds no floating operand, because 2.14.4's value is not one this
  translation carries: `double d = 1.5 + 2.5;` is 3.6.2p2's startup body here and
  an image item there, and a braced clause written `-1.5` or `0.5f + 0.25f` for a
  narrower floating member is 8.5.4p7's refusal here where the references accept.
  A clause that is one literal is answered - the digits are decoded where
  8.5.4p7's exception asks - so what is left is arithmetic and the unary sign.
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
  for the references; `static_cast<const YA&>(YA(4))` names its temporary
  `tmpobj` where they name it `arg`; and a prvalue whose value a statement
  discards is `tmpobj__n` here and `discard__n` there.
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

16.6 and 2.14.8 are where the references stop short, and each disagreement below
is resolved for g++, which implements both: the references act on
`#pragma pack(push, n)` and on nothing else, so `pack(n)`, `pack()`, a bare
`pack(push)` whose value a later `pop` restores, `pack(push, label, n)` with
`pack(pop, label)`, and `_Pragma("pack(push, 1)")` are all silence there and the
alignment g++ gives here; a directive written *between two members* is the value
at the class head there and the value at the `}` here, which is g++'s rule and
what makes the layout one fact settled where 9.2p2 completes the class; the
references replace macros in a pragma's tokens and 16.6 and g++ do not; a
character literal's ud-suffix is dropped there, so `'q'_x` is `'q'` and a program
that declares no operator at all is accepted; 2.14.8p3's raw literal operator is
refused there and is the fallback here; and the cooked numeric argument is the
literal's own type converted there - `convert sext i64 i32 42`, `convert fpext
f80 f64 1.5` - where 2.14.8p3 names `unsigned long long` and `long double`, which
is what this unit passes. 2.14.8p5's *length* is the one number of the call the
translation computes rather than reads, and it is spelled as the constant 2.14.2
gives it, which is the `int` the references and the checked-in fixture write.
5.2.4p2's scalar type is asked for here and not there, so `p->~I()` for
`typedef int I[3]` is refused here and by g++ and accepted there.

Shapes the references and this unit disagree about what a program means, each
resolved for the standard, and each judged against g++ where g++ reads the
program: 8.5.4p7's second bullet refuses a floating clause whose value the
narrower type does not keep, so `struct S { float m; }; S s = { 0.1 };` is
refused here and accepted there and by g++, which diagnoses no double-to-float
narrowing at all; a non-placement `new T(3)` calls the `operator new` 3.7.4.1p2
lets the program declare, where the references call a builtin
`cppgm_builtin_operator_new`; 3.4.1p8 looks a name written *before* a qualified
declarator-id up where the declaration stands and not in the class it reaches, so
`T C::f()` for a member `typedef int T;` is refused here and accepted there,
which is what g++ does; 10.1p2 asks a base class to be complete, so
`struct B; struct D : B {};` is refused here and accepted there, which is again
what g++ does; 8.5p7 zero-initializes a value-initialized object whose class
wrote no constructor *before* the non-trivial one it was given runs, which the
references do not write; 12.1/12.4 run a constructor and a destructor whose body
is empty, which the references elide along with the object's whole lifetime -
including 3.7.2p2's per-thread pair and 12.4p8's destruction of a class-typed
member of an aggregate; the references emit the constructor of an empty-class
subobject whose construction they then do not write, and at namespace scope open
an `@__cppgm_init` that computes its address and nothing else, where 3.2p3's
closure writes neither; 8.5.1p11's braces are elided for a member that is an
array of aggregates, which the references refuse and g++ accepts; 8.5.1p2
copy-initializes a subobject from its clause, so a constructor declared
`explicit` is not one a clause may choose, which the references let it and g++
does not; 8.3.5p5 drops the top-level cv of a parameter, so the aggregate
constructor of `struct T { const int a; int b; };` is `_ZN1TC1Eii` here and
`_ZN1TC1EKii` there; 3.6.2p2 folds a constructor whose members hold what its
arguments do, so `struct T { int a[2]; int b; }; T v[2] = {{1,2},{3,4}};` at
namespace scope is an image here and a startup body there; 12.8p31 elides
`T x = T{...}` into `x`, so an aggregate holds what 8.5.1 stores where the
references call the constructor built from its members, whose parameter list is
the members a clause reached there and every member here - `A{1}` calls
`_ZN1AC1Ei` there and `_ZN1AC1Eii` with a zero here; the references pass a class
holding a bit-field by address where they pass every other class of the same size
by value; 12.9p1's candidate set of inherited constructors holds the base's
parameter-type-list and the shorter ones its defaulted parameters leave, with
12.9p2's characteristics carrying no default-argument, where the references
inherit one declaration and copy its defaults onto it; 3.6.2p2's constant
initialization covers thread storage duration, so `thread_local A g{};` holds its
zero in the image here and is a per-thread store there - and where such an
object's class has a destructor, 12.4p11 still hands it to
`__cxa_thread_atexit` here and is dropped there, which is what g++ writes; a
thread-local array of a class with a non-trivial constructor, and a thread-local
built by a constructor from a written argument, are constructed here and never
constructed there; a thread-local whose initialization is 3.6.2p1's zero is given
no guard here and one there; a use of a thread-local written before its
definition runs what initializes it here and nothing there, which is what g++'s
`_ZTW` wrapper does; a namespace-scope thread-local scalar whose initializer is a
call is accepted here and refused there; a block-scope object declared `static` or
`thread_local` is refused here and lowered there; an unused
`extern thread_local` is a wrapper naming a global no entry declares there, which
is malformed LowIR; a namespace-scope floating array whose clauses are not
literals is accepted here and refused there; and a program that declares
`__builtin_memcpy` itself is accepted here and refused there; 12.4p8 ends the
lifetime of every element of an array a declaration named, so
`N w[4] = { N(), N() };` at block scope is four destructions here and none there,
which is what g++ writes; and an array of aggregates written from braces calls
the constructor 8.5.1 gives the element here where they store its members, which
is C11's synthesized constructor and the mirror of the `T x = T{...}` divergence
named above.

One shape of the references this unit reproduces rather than resolves, because a
fixture asks for it: 8.5.1p2's initialization of a member of a class with no
non-static data member and no base subobject *from a prvalue of its own class
carrying arguments or braces* writes nothing at all - not the call of the
constructor the clause chose, and not the clause. 12.8p31 makes the prvalue and
the subobject one object and 12.8p15's copy of such a class copies nothing, which
is what the shape is written from; g++ runs the constructor there. It is that
clause alone: 5.2.3p2's `T()`, a braced clause 13.3.1.7 hands to a constructor,
an element of an array a declaration names, an argument and a mem-initializer
each write their call, and the constructor's definition is still what the object
file owes.

The same elision reaches an *object* of empty class type at namespace scope for
the references - `V g = V{1,2};` and `V g = V(3,4);` compute the address in
`@__cppgm_init` and call nothing, where the same declaration in a function body
writes the call there and here. This unit writes the call wherever the object
stands, which is what g++ does and what the program says; the reproduction above
is bounded to 8.5.1p2's subobject, because that is the shape a checked-in `.ref`
asks for.

A spelling the references write two ways and this unit writes one: a floating
value in a scalar global is rendered from its value there - `1e2` is `100f` -
and echoed from the program's token in a structured item, where a `double` token
initializing an `f32` item keeps its `double` spelling. This unit echoes the
token in both, re-suffixed for the storage's width, so no item carries the suffix
of another width. Every value agrees.

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

Done: **C15 - the three constructs of the PA16 subset that had no path at all**,
301 / 306 from a 297 / 306 baseline and 307 / 312 with six regression tests
added, at pa1-pa15 1174 / 1174.

The failure set split cleanly in two: four fixtures where a construct the
program wrote reached no rule of this unit, and five where a construct it does
write comes out a different shape. C15 is the first half, and the three
constructs share one seam - a fact a phase before 7 establishes that phase 7 was
never handed.

16.6's `#pragma pack` is that seam exactly. The directive is read in phase 4 and
9.2p13's layout is settled in phase 7, and the one thing both name is a position
in the token stream: `Preprocessor` keeps the value and an epoch counter,
`AstTokenStream` records `(position, alignment)` wherever the epoch moves - one
integer comparison per token, one record per directive that changed anything -
and the layout binary-searches that table at the `}` its definition ends on. A
unit that writes no such pragma stores nothing and searches nothing. Where the
value applies is g++'s rule and not the references': the layout is one fact
settled where 9.2p2 completes the class, so a directive written between two
members reaches every member.

2.14.8's user-defined literal is the same shape one phase further down: phase 3
already decoded the ud-suffix and the value, and phase 7 dropped both. It is now
the call p2 says it is, chosen not by 13.3 but by the one written
parameter-type-list p3 to p6 name per form - so a `char` operator is no candidate
for an integer literal, and a numeric literal no cooked operator declares falls
to p3's raw form. 13.5.8's `li` terminal was already in PA14's encoder and only
needed the suffix handed to it.

5.2.4's pseudo-destructor call is the third: `p->~I()` for a scalar `I` names no
function, so it is settled before the call's node is opened, from the name after
the `~` alone - which is what tells it from the destructor of a class without
reading the object expression twice. 5.2.4p1 says its only effect is evaluating
the operand, and that is 5.2.9p4's conversion to `void`, so that is the one node
the tree holds.

Three defects the sweeps found and no fixture reaches, each now a test. A
concatenated string-literal reached the semantic layer as the *joined* spelling
of its parts, and re-scanning that as one literal made `"ab" "cd"` the 8
characters `ab" "cd`; the parts are separated by a token boundary, so the
sequence is rebuilt by lexing the terminal again. 12.4p12 binds a destructor
under `~` and the class's own name, but 5.2.4p2 writes a *type-name* there, so
`p->~Alias()` for `typedef Box Alias` named nothing. And 5.2.4p2's scalar type
was not asked for, so `p->~I()` for `typedef int I[3]` was accepted.

Four differential sweeps of 761 programs against `cppgm++-ref`, with every
disagreement judged against g++: 340 layout programs (pack value x member set x
class shape) and 23 directive forms, where this unit agrees with g++ on all 363
and the two families g++ reads differently - bit-fields and `alignas` under a
pack - are pre-existing and shared with the references; 102 push/pop programs
byte-identical to the references; 288 literal-operator programs leaving 56
disagreements in the three named families; and 110 pseudo-destructor programs
leaving 10 that differ only in top-level order and 3 the array refusal.
Valgrind is clean over 581 programs - every pa16 general fixture source and a
sampled half of each sweep.

Next: **C16 - the five shapes the remaining fixtures write differently**, which
is the second half of the same split and the last group with a fixture behind it.

- owner: `lowir_lower.cpp` for the LowIR type an incomplete class and
  `std::nullptr_t` are written as, `lowir_lower_body.cpp` for the one `addr`
  written twice, `lowir_abi.cpp` and `sema_scope.cpp` for 7.3.1.1p1's
  `_GLOBAL__N_1`, and `lowir_lower_object.cpp` for the empty-body elision.
- data flow: each is a fact the analysis already holds and the lowering reads -
  the return type of a declaration nothing defines, the cv of the object a member
  call is made on, the unnamed namespace a region was opened for, and whether a
  constructor's body writes anything - so none of them needs a new pass or a new
  node, only the fact read where the text is written.
- expected complexity: constant per declaration for the naming and the types, and
  one flag per constructor for the elision, which removes calls rather than
  adding them.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`, the file audit, valgrind, and a differential
  sweep against `cppgm++-ref` over the linkage a declaration has x where it is
  written x whether anything defines it, with g++ judging every disagreement.

`sema_class.cpp` is 2 990 lines against the audit's 3 000, so the next
checkpoint that adds to the object model has to split it first - 12.6.2/12.4p8's
subobject order and 3.7.1/3.8p1's lifetime are the seam.

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
  type: n placement new-expressions in one body are 0.03/0.05/0.12/0.22 s at
  500/1000/2000/4000 for 10 n + 36 lines, linear at 2.0x per doubling. n classes
  each declaring an allocation function of its own and each new'd once are
  0.09/0.19/0.37/0.76 s for 34 n + 12 lines, which is the class scope's lookup
  and each function's definition rather than a search.
- 12.5p1's question is one comparison of the name a member declaration wrote,
  asked where 9.3.1p3 decides whether the function has an object parameter: 4000
  member functions declared are 0.13 s and 21 lines at 2.0-2.2x per doubling, and
  a class with no allocation function pays that one comparison per member.
- 5.2.3p3's `T{...}` costs one comparison of the token after a primary-expression
  and, where the braces follow, the one reading of the list the initialization
  would cost anyway: n of them as arguments are 0.02/0.03/0.07/0.14 s at the same
  sizes for 7 n + 36 lines. 12.8p31's elision of one into the object it
  initializes is one probe of the region's names per initializer written as a
  call and nothing at all for any other form: n declarations `T x = T{a, b}` are
  0.03/0.06/0.11/0.25 s for 14 n + 8 lines, linear at 2.0-2.2x per doubling.
- 8.5.1p2's empty-class subobject is one flag the analysis puts on the action and
  one test in the lowering, so n aggregates built from `Z{1,2}` are
  0.03/0.06/0.12/0.25 s for 13 n + 20 lines - and the address such a subobject no
  longer needs is two instructions per subobject the output no longer holds.
- 2.14.4's floating image is one spelling per item and 5.19's fold refuses a
  floating operand at the first node it reaches, so n namespace-scope floating
  objects are 0.01/0.03/0.05/0.10 s for 2 n + 5 lines. The one decode 8.5.4p7's
  exception asks for happens only under a braced clause whose two types differ in
  width.
- An element of an array of class type is indexed the one way from every place
  that names one, so an aggregate whose member is an n-element array of a class
  is 0.02/0.04/0.07/0.14 s for 14 n + 14 lines - the instructions per element the
  element walk already wrote, from one description rather than two.
- 15.2p2's cleanup costs one list of the subobjects a constructor has built,
  appended to as the calls are made, and one handler block per step that needs
  different destructions from the step before it - the list only grows, so equal
  length is equal content and one probe answers. What a handler writes is the
  instructions that named the subobject where it was built, copied and renamed,
  which is one pass over a handful of instructions per subobject. n members of
  class type therefore cost the n(n+1)/2 calls 15.2p2 asks for, which is what
  the references write and is bounded by the n members the source wrote:
  50/100/200/400 members are 0.01/0.03/0.12/0.43 s for 5098/17673/65323/250623
  lines, byte-identical to `cppgm++-ref` at every size. 12.4p8's suffix is the
  same n(n+1)/2 up to `kUnwindSuffixLimit` (16) destructions and a chain of n
  blocks past it, each running one destruction and entering the next - which is
  where the references stop writing them out too.
- 12.6p1's array of class type is still written as its elements, so the same
  n(n+1)/2 now applies to a count a single number in the source sets:
  `struct X { A w[n]; };` is 7946 lines at n = 50 where the references write 126,
  and 1.5 s and 810 824 lines at n = 400. An array subobject written from a
  braced clause is the same shape now that each of its elements is a step -
  50/100/200/400 elements are 0.01/0.05/0.20/0.83 s for
  9396/33746/127446/494846 lines, with the constructor byte-identical to the
  references and only the destructor differing, where they write the loop. Past
  16 elements the references write that loop instead, which is C15; until then
  this is the one axis of this milestone that grows quadratically from a number
  rather than from what the program spells out, and it is named rather than
  capped.
- The walk down to a subobject is carried as the walk - the object, the member,
  then one subscript per dimension - and written again wherever an address is
  asked for, which is one `decay` and one step per dimension and never twice for
  one use: an element that stores its clause names itself once, not once for the
  step and once for the store. 8.5.1p7's tail of an array of class type is one
  construction per element, which is linear and is what the references write:
  an n-element array subobject with two clauses is 6 n + 41 lines at
  0.01/0.01/0.03/0.06 s for 500/1000/2000/4000, where the references take
  0.52/0.54/0.64/0.86 s for the same count.
- 16.6's packing alignment costs one integer comparison per token while the
  stream is built and one record per directive that changed the value, so a unit
  that writes none stores none; 9.2p13 asks one binary search per class
  definition, and an empty table answers before searching. n classes each under
  its own push/pop pair, n classes under one, and n classes under none are the
  same 0.01/0.02/0.05/0.10 s at 500/1000/2000/4000, each doubling 2.0-2.2x.
- 2.14.8's user-defined literal costs one lookup of the literal-operator-id and
  one walk of the declarations it reached, comparing interned type ids rather
  than ranking a candidate set - and the chain of one such id is bounded by the
  parameter lists 13.5.8 lets it have. n of them in one body are
  0.01/0.02/0.04/0.08 s for 6 n + 26 lines, and n literal operators declared and
  one used 0.02/0.05/0.10/0.20 s for 11 n + 14 lines.
- 2.14.5p12's concatenated literal is lexed back into its parts once per reading
  of the expression, which is the scan the single reading already cost: a literal
  of n parts is 0.00 s and 2 n + 13 lines at 4000.
- 5.2.4's pseudo-destructor call costs one lookup of the name after the `~`,
  asked before the object expression is read, so a member call that is not one
  pays one comparison of the first character: n of them in one body are
  0.00/0.00/0.01/0.03 s for n + 11 lines.
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
| audit of C13 | 5.3.4's new-expression with 5.3.4p9's lookup and 8.5p16's object at the address it returned; 5.2.3p3's `T{...}` and 12.8p31's elision of it; 8.5.1p2's member of a class that holds nothing | 12.5p1's allocation function of a class given 9.3.1p3's object parameter where `static` was not written, and an operator-function-id spelled apart by a separator binding a second name wherever a declarator was qualified - so an out-of-class definition defined `_ZN1T12operator newEmPv` while the call named `_ZN1TnwEmPv`; 8.5.1p2's empty-class subobject written for the class rather than for the clause, so `{ Mark(), 1 }`, `{ {}, 1 }` and `{ {3, 4}, 1 }` each lost the constructor the program wrote, and the address of one whose initialization writes nothing computed anyway, an element of an array of them included; 5.19's fold answering for a floating operand with the integer no floating literal sets, so every floating value the image carried was zero; 5.2.3p2's floating `T()` spelled `0.0` at all three widths; 8.5p7's zero of a pointer written as the integer 4.10p1 converts from; an element of an array of class type indexed two ways; 8.5.4p7 refusing a floating clause whose value the narrower type keeps | 278 / 291 from a 277 / 291 baseline, the same set passing and `100-default-member-initializer-scalar-brace` with it, and 283 / 296 with five regression tests added; pa1-pa15 1174/1174; byte-identical passing fixtures 165 of 241; valgrind clean over 780 programs; three differential sweeps of 468 programs against `cppgm++-ref` leaving 6 status disagreements, all the floating arithmetic this milestone does not fold, and every text disagreement named; seven new axes linear at 1.9-2.1x per doubling; file audit passes with the two recorded header-weight warnings |
| C14 | 15.2p2's cleanup around a partly built object: the subobjects a constructor has built kept as the calls it made, in order, for as long as the walk is inside 12.6.2's initializations, with each call after the first standing in an `eh_try` region whose handler destroys that list backwards - at the granularity of the call, so an aggregate member's own class members are entries of their own and a constructor that does nothing is no entry at all; the instructions that named a subobject written again in the handler's own block, because a block an exception reaches names no temporary of the block it left; a step needing what the step before it needed naming that block again, which equal length settles because the list only grows; 12.4p8's suffix given 15.2p2's shape - the destructor's body in one `eh_cleanup` whose handler destroys every subobject, each destruction but the last in a region that destroys the ones behind it, and the whole suffix written wherever control leaves the body, which is what a `return` in a destructor had been skipping entirely; the suffix chained past `kUnwindSuffixLimit` (16) destructions, where the references stop writing them out; and 15.2p2's odr-use asked of the whole list of steps at once, so all but the last of them get 12.4's entry point and 12.4p6's definition | 286 / 296 from a 283 / 296 baseline, the same set passing and the three lifetime fixtures with it, and 291 / 301 with five regression tests added; pa1-pa15 1174/1174; a 504-program differential sweep against `cppgm++-ref` over the subobjects a class holds x its base x its constructor x its destructor x where the object stands, leaving no status disagreement and 3 text disagreements, all the empty-constructor elision already named, with two defects it found closed - a destructor declared for a call nothing writes, and a call naming one ABI entry point making the unit owe both; valgrind clean over the fixtures and the sweep; n members of class type byte-identical to the references at 50/100/200/400 in 0.01/0.03/0.12/0.43 s, and 12.6p1's array named with its numbers as the one axis still quadratic in a count the source only wrote a number for; file audit passes with the two recorded header-weight warnings |
| audit of C14 | 15.2p2's list of the subobjects a constructor has built, each call after the first in a region whose handler destroys it backwards; 12.4p8's suffix in one `eh_cleanup` with a region per destruction; 15.2p2's odr-use asked of the whole list at once | membership of the list decided by whichever constructor call found the step's mark, so 12.2p1's temporary and 5.3.4p12's object joined it - the heap object of `T() : p(::new(buf) N), a() {}` gave a handler that re-ran the allocation function and named temporaries of a block a handler may not name - and an element of an array subobject past the first joined nothing, leaving `w{V(1), V(2), V(3)}, a()` two elements standing; that element addressed from one byte cursor, which no handler can write again, so the walk down to a subobject is now carried as the walk and a two-dimensional braced clause no longer emits a handler naming another block's temporary; 5.2.4's explicit destructor call asking for neither the entry point nor 12.4p6's definition, so `p->~Box()` named a symbol no unit defines; 8.5.1p7's tail of an array of class type written as a span of zero bytes, so `N w[4] = { N(), N() };` destroyed four objects two constructors had built and a class with no default constructor was accepted; 7.1.2p2's `inline` read only from the declaration the body is written on | 292 / 301 from a 291 / 301 baseline, the same set passing and `300-explicit-destructor-call-enclosing-namespace-type` with it, and 297 / 306 with five regression tests added, one per finding; pa1-pa15 1174 / 1174; byte-identical passing fixtures 162 of 253; valgrind clean over 642 programs; four differential sweeps of 1 097 programs against `cppgm++-ref` with every disagreement named and every shape the audit turns judged against g++; the two 15.2p2 axes byte-identical to the references at 50 / 100 / 200 / 400 and 8.5.1p7's tail linear at 6 n + 41 lines for 4000 elements in 0.06 s; file audit passes with the two recorded header-weight warnings |
| C15 | the three constructs of the PA16 subset that had no path at all, each a fact a phase before 7 establishes and phase 7 was never handed: 16.6's `#pragma pack` carried from the directive to 9.2p13 as the positions in the token stream the value changes at, with a width, a reset, a labelled or bare push and a matching pop, and the value read where the definition ends because that is where 9.2p2 completes the class; 2.14.8's user-defined literal made the call p2 says it is, chosen by the one written parameter-type-list p3 to p6 name per form rather than by 13.3, with p3's raw fallback and 13.5.8's `li` terminal in the object-file name; 5.2.4's pseudo-destructor call on a scalar settled from the name after the `~` before the object expression is read, and written as the operand's value discarded, which is what p1 says the call comes to; and three defects no fixture reaches - a concatenated string-literal re-scanned from the joined spelling of its parts, so `"ab" "cd"` was the 8 characters `ab" "cd`; 12.4p12's destructor named through a typedef-name for its class reaching nothing, because 12.4p1 binds only `~` and the class's own name; and 5.2.4p2's scalar type never asked for, so `p->~I()` for `typedef int I[3]` was accepted | 301 / 306 from a 297 / 306 baseline, the same set passing and the four C15 fixtures with it, and 307 / 312 with six regression tests added; pa1-pa15 1174 / 1174; four differential sweeps of 761 programs against `cppgm++-ref` with every disagreement judged against g++, which this unit agrees with on all 363 layout and directive-form programs where the references act only on `pack(push, n)`, on 2.14.8p3's raw operator and on 5.2.4p2's scalar type; 102 push/pop programs byte-identical to the references; valgrind clean over 581 programs; four new axes linear at 2.0-2.2x per doubling; file audit passes with the two recorded header-weight warnings |
