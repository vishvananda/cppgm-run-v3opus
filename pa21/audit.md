# PA21 Audit — `cppgm++ --emit-lowir` with full `constexpr`

A review of each landed checkpoint, in the order a fact travels: the storage a
declaration asks the program for, the name the image gives it, the value the
image holds, and the initialization and destruction left for the program to run.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| L | `af299cb9` | 3 / 3 + 6 recorded | **the function part of a name that has to say *which* function, which two functions can spell the same way.**  3.7.1p3's object is named in the image by the body that declared it and where in that body it stands, and the where is a span of this unit's terminals only until 7.1.2p4 leaves a definition every unit may hold - then it is a counter per function, and the function part is a flattened qualified name that `f(int)` and `f(double)` share, and `value<1>` and `value<2>` with it.  So `template<int N> int value() { static int data = N; }` laid out *one* global for both instantiations, holding 2, which is what `300-nested-function-template-local-static-array` shows and what makes `value<1>()` answer 2.  Beside it, 3.5p3's internal linkage was left off both readings of "a definition every unit may hold", so `static inline int k() { static int t; }` gave the object a *weak* symbol and a counter place where the reference and 3.5p3 both give this unit's own object a `tokens` place and an internal one.  And 12.4p8 ends the lifetime of *each element* of an array where 3.6.3p3's runtime takes one function and one object, so a block-scope `static P p[2]` handed the runtime `~P` and the address of the array and ended one lifetime of the two - the same shortcut 3.7.2p2's hand-off already had, both now handing a body of the program's own that ends them all |
| S+O | `46d8b2f4` | 3 / 3 + 4 recorded | **what a constant of class type is worth where a place asks for a number, and what a statement costs the second time the walk runs it.**  O made an object of literal class type a constant its declaration holds, and such a constant's bits are the identifier of an interned list - so the readings that take a constant as a number had to be told, and only four of them were.  `promote` refused a class operand, `truth` converted it and `convert` and every reader that spelled `evaluate(...).bits` took the *identifier* as the number: `constexpr C c(7)` with `constexpr operator int` laid out `int a[c]` with 12 elements where g++ lays out 7, gave `enum E { e = c }` the value 12, passed `static_assert(c)` where the conversion is `false`, and wrote zero for `constexpr int n = c ? 2 : 3`.  Every one of those was *refused* before O landed, so the checkpoint turned five diagnostics into five wrong answers, and 5.19p3's one rule - a converted constant expression may reach its type through a user-defined conversion - is now asked once and by all of them.  Beside it, S's claim that a block's objects are made once per fold held for the objects and not for the names: 7p3's typedef, alias, using-declaration and class were handed to `SemaAnalyzer::declaration` on *every* pass, so a `typedef` inside a loop of 102400 cost 73 MB against the 7 MB the same loop costs without one, and `struct P { int a; } p;` inside a loop was 3.2p1's class defined twice on the second pass.  And `fold_local`, the flag the checkpoint added to say which object an evaluation may write, was never set on a place the call filled - so `constexpr int f(int n) { n = n + 1; return n; }` was refused by the rule that exists to allow it |
| N | `76c1c8fd` | 3 / 3 + 3 recorded | **where a class is complete for the condition it wrote, and which line of a tree names the function a call reaches.**  N made 15.4p1's condition a *fold* rather than the spelling `true`, and folded it at the declarator - but 9.2p2 regards a class as complete inside an exception-specification, so `struct S { void f() noexcept(k); static constexpr bool k = true; };` folded to no in the class and to yes on the definition written after it, and `void S::f() noexcept(k) {}` was **refused** as two declarations 15.4p1 does not make the same, where both oracles accept it; with no definition written it was silently the wrong fact, and the object's `boundary.unwind` said an object of `S` may unwind.  Pre-N the spelling match answered no in both places alike, so N is where the disagreement became reachable.  Beside it, `nonthrowing_tree` answered a new- and a delete-expression from `node.fact.entity`, which neither writer fills - the allocation function 3.7.4.1 chose and the deallocation function 5.3.5p9 paired are named by a `callee` line of their own - so the arm was null at every reach and `noexcept(delete p)` was false for every operand, right by accident for the throwing default `operator new` and wrong for the rest.  And 5.3.7p3's second bullet was a walk of the whole operand for a node only the *statement* parser builds, so it could not fire and cost that walk per operator |
| F | `6cdd7e1d` | 3 / 3 + 4 recorded | **the value a clause is judged by, and the destination a conversion has no value for.**  F gave `SemaConstant` a `real` beside its bits and made an array a constant object, and the review followed both out to their readers.  8.5.4p7's second bullet was the reader the widening left behind: `floating_round_trips` decoded `value.payload` with `strtold` and wanted the node to be a *literal*, so `constexpr double d = 1.5; struct S { float a; }; constexpr S s = { d };` - and every `float` clause off a name, an operator or a folded call - was refused as narrowing where g++ and the reference both take it.  Asking the *fold* instead uncovered the rule beneath it: 8.5.4p7 excepts a constant whose value is "within the range of values that can be represented, **even if it cannot be represented exactly**", and the old test was an exactness, so `float a{0.1}` was refused too.  Under that widening `SemaAnalyzer::convert` had no arm for a destination of class type at all: it fell through to the integral width path and handed back the operand's bits *under the object's type*, which is the one number a class constant's bits may not be - so `struct P { int x; constexpr P(int v) : x(v) {} }; constexpr P ps[1] = { 999999 };` interned a member list identified by 999999 and `ps[0].x` read `parameter_lists_[999999]`, a **segfault** valgrind calls an invalid read of a page that was never mapped.  Beside them 5p4's overflow: an infinity is what `1e400` and `1e308 * 10.0` come to, `TypeTable::real_type` keyed one by an undefined cast of `ldexp(inf, 64)`, and `spell_floating` took the `f` of `inf` for 2.14.4p1's suffix and wrote `= in` into the image where the reference writes `= inf` |
| T | `33422f2f` | 3 / 3 + 4 recorded | **the two calls the fold still chose for itself, after it had handed choosing away.**  T's rule is that a fold ranks nothing: `callee_candidates` writes the lookup a call writes and `select_overload` chooses.  Two readings of one construct were left standing outside it.  12.3.2p1's conversion function was chosen by a walk of `owner->conversions` that dropped every declaration that is not a constexpr function *before* ranking what was left - so a class whose best conversion 13.3 chooses is an ordinary one silently answered through a worse one: `struct C { operator int() const; constexpr operator bool() const; }` gave `enum E { e = c }` the value **1** where g++ calls the conversion ambiguous and the reference refuses it, and it ignored 12.3.2p2, so `constexpr explicit operator int` answered an enumerator and an array bound that only a direct-initialization may reach it through.  And a call written *as a template argument* arrives as a flattened spelling at a door of its own, which still resolved the word with the ordinary lookup alone - so `H<f(N2::S(3))>` and `H<twice<int>(3)>`, the very shapes T taught `called`, were refused there where both oracles fold them.  Beside them 9.4p2: `member_value` looked the declaration up and then answered from the *subobject list*, so `s.value` on a `constexpr` object named a static member the list cannot hold and was refused |
| I | `e797cd19` | 3 / 3 + 4 recorded | **the image an object holds, asked of two of the three families of declared type - and whose the definition is, asked at one of the three exits that lay one out.**  Group I made 3.6.2p2's image an owner: 9.4.2p3 one declaration on both sides, `clause_of` reading a list against the subobject it initializes, 14.7.3p1's written-out definition given somewhere to live, and I3's line between what an image lays out and what a startup body runs.  The held brace-or-equal-initializer was pulled only for a class or an array, which is nothing 9.4.2p3 says - so `static constexpr const char *text = "ab";` with its definition written out laid out `ptr = zero` with no `__strlit__` and no startup body, and `&helper` and `&target` the same: a program that reads through a **null pointer** where both oracles write the address, and no diagnostic anywhere.  Beside it 8.4.2p1, which I3 read as a fact about who *declared* the constructor: `Zero z = Zero();`, `Zero a[3];` and `static Zero z = Zero();` each wrote out a definition the reference does not, while `constexpr Zero z = Zero();` two lines away needs one - what tells them apart is whether working the image out went *through* 8.4.2p1's definition, which 5.19's fold and `global_constructed`'s walk do and 3.6.2p1's zero does not.  And 5.2.2p1's own sentence - an object of class *or array* type is built, so a clause holding a call is work the program runs - was asked at two of the three walkers, so `constexpr int a[2] = { square(3), 4 };` was laid out as data and the identical clauses written as a class's member were not.  The walk that asks it cost the member count times the argument's size, which 3200 members off one 6400-term place made **4.55 s** and is now 0.14 s |

## Current Checkpoint Review

Group I is where 3.6.2p2's *image* became an owner of its own.  Checkpoint I
made 9.4.2p3 one declaration on both sides - `member_initializers_` holds a
static data member's brace-or-equal-initializer beside 12.6.2p8's, the
out-of-class definition reads it in the class it was written in, and
`ConstexprReading::clause_of` folds a list against the *subobject* it
initializes rather than as a row of expressions - and split `lowir_image.cpp`
out of `lowir_lower.cpp`.  I2 gave a function template that declares no pattern
a `TemplateInfo` anyway, so 14.7.3p1's written-out definition has somewhere to
live and 14.7.3p6 binds it as this unit's own.  I3 drew the line the reference
draws between what an image lays out and what a startup body runs, and asked who
owes the definition of a constructor whose work the image kept none of.

The three rules are right.  `{{1,2},{3,4}}` written for an array of class type
is 8.5.1p1's initialization of each element and not a row of clauses this
declaration then places, which is what `clause_of` says; a template's
specializations are definitions the unit owns whether or not the template ever
defined a pattern; and a clause holding a call is work the program runs.  Nine
declared types of static data member, nine defaulted-constructor shapes and
eleven clause shapes were swept through `pa21/cppgm++-ref`, and the disagreements
are the three below and the two recorded after them.

What the review found is that each of the three sentences was landed at *part*
of what it is about.  I asked 9.4.2p3 of the types an object is built in and not
of the ones it is not; I3 asked "whose definition is this" of the object's own
constructor at one of the three exits, and asked "is this clause a call" at two
of the three walkers that lay an image out.

### Findings

**1. A static data member whose initializer is an address laid out zero and no
initialization at all.**  `describe_object_initialization` pulled the
brace-or-equal-initializer the class wrote only where the member's type was a
class or an array:

```cpp
if (written_clause == nullptr && declared != nullptr &&
    (types_.is_class(types_.strip_cv(type)) ||
     types_.kind(types_.strip_cv(type)) == TypeKind::Array))
```

For an arithmetic member that costs nothing, because 5.19p3 folded the object to
one value and `SemaEntity::value` carries it to the image whatever the
definition's own line says.  A *pointer* has no such value - 4.10p1's is an
address, which no `bits` of a constant holds - so the definition read its own
silence as 8.5p6's default-initialization, and

```cpp
template<class = void> struct table { static constexpr const char *text = "ab"; };
template<class T> constexpr const char *table<T>::text;
```

laid out `global @table_void___text : ptr = zero` with **no `__strlit__` and no
startup body at all**.  `&helper` and `&target` were the same zero.  That is not
a refusal and not a mismatch: the program reads through a null pointer, and both
oracles write the address.  It is checkpoint I's own sentence - the image an
object holds is the declaration's - asked of two of the three families of
declared type, and 9.4.2p3 does not know about type at all.  The pull is now
asked of every one; the nine types swept - `int`, `double`, `char`, `unsigned
long`, `bool`, an enumerator, a function pointer, an object pointer and
`nullptr` - all answer as the reference does.

**2. The definitions a folded image owes were owed at one exit and by the wrong
test.**  I3 read 8.4.2p1 as a fact about *who declared* the constructor:
`built.implicit_declaration` chose between `owe_internal_definition` and
`demand_definition`, on the one branch where the constructor writes nothing.
The class declaring it is necessary and not sufficient.  `Zero z = Zero();`,
`Zero a[3];` and `static Zero z = Zero();` over `struct Zero { int x; Zero() =
default; };` each wrote out `@Zero__Zero`, which the reference does not, while
`constexpr Zero z = Zero();` two lines away needs it - and the source difference
between them is not who declared anything.  What tells them apart is whether
working the image out *went through* the definition 8.4.2p1 gives: 5.19 goes
through it when it folds the object to a value, `global_constructed` goes
through it when it lays the storage out member by member, and storage that
merely holds 3.6.2p1's zero goes through neither.  `owe_folded_construction` is
that one question, asked at all three exits of `global_image` and at
`global_constructed`'s own; the `!member_entry` test that stood at one of them
is 8.5.1's entry being named by nothing the program wrote, which is the same
clause as `implicit_declaration` and now stands beside it.

**3. `runs_a_call` was asked at two of the three walkers, and once per member
rather than once per argument.**  I3's sentence is that an object of class *or
array* type is built, so a clause holding a call is work the startup body does.
`global_subobjects` and `global_constructed` ask it; `global_array_initializer`
did not, so one array got two images depending on where it was written:

```cpp
constexpr int a[2] = { square(3), 4 };            // i32 9, i32 4
struct Q { int a[2]; };
constexpr Q q = { { square(3), 4 } };             // zero 8 and a startup body
```

The reference writes `zero 8` and a startup body for the second and refuses the
first outright - it lowers no array of scalars that needs one - so the shape it
can answer is the shape it answers the same way as every other object 5.2.2p1
builds.  The third walker now asks too.

Beside it the walk itself: `global_constructed` asked `runs_a_call` of the node
each member initialization reads, which for a constructor carrying one place
into *n* members is *n* walks of one expression - the product of the member count
and the argument's size, on top of the same product `image_value` already paid
for the same reason.  Both are facts of the argument and are now settled once
where the place is bound.  `constexpr P p(<6400-term expression>)` over a class
of 3200 members initialized from that one place is **0.14 s against 4.55 s**,
with byte-identical output.

### What the review confirmed rather than found

**9.4.2p3's mirror still holds in both directions.**  A definition that writes
its *own* initializer - `template<class T> const int S<T>::v = 7;` - takes it
and not the class's, an array of unknown bound takes the bound the class's
initializer deduced, two members defined in the reverse of their declaration
order answer alike, and a member whose initializer names the member declared
above it folds where it stands.  The three other readers of
`member_initializers_` are untouched by the widening:
`write_member_initializations` and `vacuous_construction` both reach it only
under `declares_subobject`, which a static data member is not.

**The two `.ref` files I3 was written for still pin it.**
`400-constexpr-function-and-constructor` wants `zero 4` and a startup body for
`constexpr Point p(square(3))`, and `300-constexpr-defaulted-constructor` wants
`@Zero__Zero` written out for `explicit Zero() = default;` and nothing at all for
`struct Empty {}` - both still hold, through the rule rather than beside it.

**Neither the fold nor the image is superlinear in what it walks.**  `clause_of`
over a class nested 8 / 10 / 12 deep with two members at each level - 256 / 1024
/ 4096 leaves - is 0.00 / 0.01 / 0.03 s, linear in leaves and not 2^depth times
depth.  A static data member of 1000 / 4000 / 16000 two-member elements is
0.01 / 0.05 / 0.19 s.  An aggregate of 2000 / 8000 / 32000 clauses is 0.04 /
0.16 / 0.63 s, and the same clauses each holding a call - the shape finding 3
refuses - 0.05 / 0.22 / 0.90 s.

**Two units reading one class agree, and better than the reference.**  Two
translation units in one invocation, each defining the same static data members
of one class template out of class, emit one `@S_void___a` and one
`@S_void___text`; `pa21/cppgm++-ref` emits `global @S_void___a` **twice** into
one module.

**Valgrind is clean** over the two `.ref` fixtures the group was written for,
the four other course fixtures, and every scaling probe this review wrote.

### Recorded, not landed

**A base or member subobject the fold left with nothing to do owes its
constructor's definition too.**  `struct Base { int x; Base() = default; };
struct Derived : Base { Derived() = default; }; constexpr Derived d =
Derived();` gets `@Derived__Derived` here and both `@Derived__Derived` and
`@Base__Base` from the reference; `struct Outer { Inner i; }` over `struct Inner
{ int x; Inner() = default; }` is the same one class in.  Finding 2's rule says
they are owed - the fold went through them - and the dump does not say they
exist: `write_member_initializations` writes no step at all for a member of
class type no initializer reaches whose construction is trivial, which is
exactly this case, and the base walk does the same.  Reaching them means asking
the *class* for its default constructor from the lowering, which is a walk of
the class's region the image layer has no business doing; the difference is a
weak definition no unit needs, because any unit that names one writes its own.

**The reference refuses a folded pointer's read, and gives storage to nothing.**
`static constexpr const char* text = "ab";` read as `traits<int>::name` is
`addr @__strlit__1` there and `load ptr @traits_int___name` here, and having
folded the read the reference never instantiates 14.7.1p1's definition of the
member at all.  That is the failure map's group P, one layer earlier than the
image: with finding 1 landed the storage this build lays out is *right*, and the
read that would make it unnecessary is still the pointer-valued constant the
fold has no value for.

**`constexpr P p = P(7);` is folded here and run there.**  The reference lays out
`zero 4` and a startup body wherever the initializer is written as a functional
cast, and `i32 7` where the same constructor is called as `P p(7)`; this build
folds both, which is what g++ does and what 3.6.2p2 asks for.  No `.ref` pins
it.  Making the two agree means telling 12.8p31's elided prvalue from a
direct-initialization at the image, which `fact.elided_prvalue` already says -
one line, held back because it reproduces a difference nothing asks for and
loses an image the standard requires.

**8.5p7's value-initialization does not read a brace-or-equal-initializer.**
`struct Zero { int x = 3; }; constexpr Zero z = Zero(); static_assert(z.x == 3);`
is refused here and folded by both oracles, and the namespace-scope object then
takes `zero 4` and a startup body where the reference lays out `i32 3`.  The
class is no aggregate - 8.5.1p1 excludes one with a brace-or-equal-initializer -
so `object_of` goes to `object_from_constructor` and the implicitly-defined
default constructor it finds is not one this build calls constexpr.  7.1.5p4 is
what says it is, and that is checkpoint O's reading and group O2's row, not this
group's.

## Changes

- **`sema_analyzer.cpp` - 9.4.2p3's held initializer is pulled for every
  declared type**, not only for the ones an object is built in, so a scalar
  static data member whose initializer is an address is initialized rather than
  laid out as zero.
- **`lowir_lower.cpp`, `lowir_lower.h` - `owe_folded_construction`**: 3.2p2 and
  8.4.2p1 asked once, over whether working the image out went through the
  definition 8.4.2p1 gives, with 8.5.1's own entry and a constructor the standard
  declared beside `owe_internal_definition`'s 3.5p4.
- **`lowir_image.cpp` - the three exits of `global_image` and
  `global_constructed`'s own tail ask that one question**, over a
  `folded_object` the whole reading now shares with the two places that already
  asked it.
- **`lowir_image.cpp` - `global_array_initializer` asks `runs_a_call`**, so the
  clauses of an array of scalars are read the way a class's clauses and a
  constructor's member initializations already were.
- **`lowir_image.cpp` - `BoundArgument`**: 5.2.2p4's argument carries the two
  walks of it - whether it runs a call, and what it is worth at each type it is
  read at - so a place carried into *n* members is read once and not *n* times.

## Performance Evidence

Best of three per shape, alternating between the binaries.  `1301e41b` is the
checkpoint's own binary, built with `make build` in a worktree on this machine
in this session.

| shape | this build | `1301e41b` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| 3200 members from one place, argument of 1600 terms | **0.10 s** at 33 MB | **1.05 s** at 33 MB | 4.10 s at 64 MB |
| the same at 6400 terms | **0.14 s** at 44 MB | **4.55 s** at 44 MB | 0.94 s at 43 MB |
| the same at 400 terms | **0.08 s** at 30 MB | **0.29 s** at 30 MB | 1.52 s at 54 MB |
| an aggregate of 2000 / 8000 / 32000 clauses | 0.04 / 0.16 / **0.64 s** at 13 / 33 / **113 MB** | 0.63 s at 113 MB | 2.34 s at 199 MB |
| the same, every clause holding a call | 0.05 / 0.22 / **0.91 s** at 22 / 66 / **243 MB** | 0.89 s at 243 MB | 3.96 s at 357 MB |
| a static data member of 1000 / 4000 / 16000 two-member elements | 0.01 / 0.05 / **0.20 s** at 8 / 15 / **42 MB** | 0.19 s at 42 MB | 1.32 s at 110 MB |
| a class nested 12 deep with two members at each level | **0.03 s** at 13 MB | 0.03 s at 13 MB | 0.77 s at 39 MB |
| an array of 2000 / 8000 / 32000 scalar clauses | 0.01 / 0.03 / **0.11 s** at 7 / 11 / **26 MB** | 0.11 s at 26 MB | refuses |
| the same, every clause holding a call | 0.05 / 0.20 / **0.80 s** at 19 / 56 / **204 MB** | 0.02 / 0.08 / **0.34 s** at 9 / 18 / **57 MB** | refuses |

The last row is what finding 3 costs: the array is now a startup body of *n*
stores where it had been *n* data items, which is the same 0.91 s and 243 MB the
row above it - the identical clauses written as a class's - has always paid.
Both are linear in the clauses.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **88 / 135**: the 47
  failures the turn started with, byte-identical, and the two course fixtures
  this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Two course fixtures added, each checked against `1301e41b`: it writes
  `= zero` for four pointer members of
  `300-the-image-a-scalar-static-data-member-holds` where the reference writes
  their addresses, and an extra `@never_folded__never_folded` in
  `300-the-definitions-a-folded-image-owes`.  Both pass now; all six course
  fixtures pass.
- Nine declared types of static data member, thirteen defaulted-constructor and
  value-initialization shapes, eleven clause shapes and a two-unit invocation
  swept through `pa21/cppgm++-ref` with the real comparator, one test directory
  per shape because it stops at the first failure.
- `valgrind -q --error-exitcode=9` over the group's fixtures and every scaling
  probe: **clean**.
