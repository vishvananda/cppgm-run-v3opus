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
| P | `90faa85d` | 4 / 4 + 3 recorded | **which reading an operator expression has, asked of the values a fold had already computed and of the shape its operand was written in - rather than of the declarations its operands reach.**  P's rule is that a fold gathers 13.3.1.2p3's set and hands it to `select_overload`, and the rule is right; what the review found is that the *question* in front of it was asked three different wrong ways and that two of the operators it covers had no declaration to gather at all.  5.14p1's short-circuit was read off the operand values, so a left operand of class type meant the right one was evaluated before anything was gathered: `!(no && quotient(1, 0))` over a class with `operator bool` was **refused** as a division by zero where both oracles fold it, and a write the program guarded behind the same `&&` ran.  Pre-P the built-in reading short-circuited and both folded, so P is where it became reachable.  The mirror half was there before P and is closed with it: `0 && mark` with `constexpr int operator&&(int, token)` in scope answered **false**, because the clause is about *every* operand and the fold had read one.  Beside them the set itself: `operator_constant` gathered the non-member half for all 38 operators, so `c[0] == 42` over a non-member `int operator[](C, int)` - a declaration 13.5.5p1 lets no program write, and one the expression layer's own door already left out - was **folded to 42**.  And 5.17p1's write-back was asked before 13.3.1.2 wherever the left operand was a *name*: `one = two`, `one += two` and `++one` over a class that declares each of them were refused as "written by a constant expression that did not declare it", where P had already opened the same door for `counted{4} += two` one exit over.  Under that, `is_operator_token` had never held `<<=` or `>>=`, which `OperatorCall::overloadable` and the recognizer's own list both name - so two of 13.5p1's operators could not be declared at all |
| I | `e797cd19` | 3 / 3 + 4 recorded | **the image an object holds, asked of two of the three families of declared type - and whose the definition is, asked at one of the three exits that lay one out.**  Group I made 3.6.2p2's image an owner: 9.4.2p3 one declaration on both sides, `clause_of` reading a list against the subobject it initializes, 14.7.3p1's written-out definition given somewhere to live, and I3's line between what an image lays out and what a startup body runs.  The held brace-or-equal-initializer was pulled only for a class or an array, which is nothing 9.4.2p3 says - so `static constexpr const char *text = "ab";` with its definition written out laid out `ptr = zero` with no `__strlit__` and no startup body, and `&helper` and `&target` the same: a program that reads through a **null pointer** where both oracles write the address, and no diagnostic anywhere.  Beside it 8.4.2p1, which I3 read as a fact about who *declared* the constructor: `Zero z = Zero();`, `Zero a[3];` and `static Zero z = Zero();` each wrote out a definition the reference does not, while `constexpr Zero z = Zero();` two lines away needs one - what tells them apart is whether working the image out went *through* 8.4.2p1's definition, which 5.19's fold and `global_constructed`'s walk do and 3.6.2p1's zero does not.  And 5.2.2p1's own sentence - an object of class *or array* type is built, so a clause holding a call is work the program runs - was asked at two of the three walkers, so `constexpr int a[2] = { square(3), 4 };` was laid out as data and the identical clauses written as a class's member were not.  The walk that asks it cost the member count times the argument's size, which 3200 members off one 6400-term place made **4.55 s** and is now 0.14 s |
| V | `b8bd105a` | 4 / 4 + 5 recorded | **the requirement 7.1.5 puts on a declaration, asked of a fold that had run out rather than answered - and asked at one of the four declarators 7.1.5 is written about.**  V made `ConstexprRequirement` the owner of what a declaration written `constexpr` shall be, and the rule is right: a fold that came to nothing is not "an object with no constant value", it is a program 7.1.5p9 refuses.  What the review found is that the fact standing in front of it - is this failed fold the program's error or this build's edge - was `valued_class`, which answers about the type of the object *declared* and says nothing about the values the initializer *read*.  So `constexpr int n = *(values + 1);`, `constexpr char first = text[0];`, `p->v` on a constexpr pointer, a brace-elided aggregate and `f(&x)` were each **refused** where both oracles fold them - five valid programs turned into errors by a requirement that exists to catch one that is not - and the same requirement asked in `--emit-types` refused a declaration PA12 and PA21 both fold, because that dialect collects no conversion functions.  `NotConstant::covered` is now the fact: 5.19's answer about the program on one side, and this reading running out of a value kind, an operator or a dialect on the other, with the declaration carrying the answer forward so a *name* that reaches it runs out too.  Beside it, 7.1.5p3's literal return and parameter types stood at `function_definition` alone, which a constructor and a conversion function never reach - so `struct T { constexpr T(NL) {} };` and `constexpr operator NL() const` were accepted where both oracles refuse them, and the checkpoint's own `-bad` fixture pinned the one exit of the four that asked.  And 3.9p10's third bullet read only a constructor a *declaration* wrote, so `struct D : B { int x; }; constexpr D d = {};` and every class 8.5.1p1 leaves no aggregate - a base, a virtual function, 11p1's access - was **refused as a non-literal type** where both oracles build the object and where the reference's own lowering is byte-identical to this build's once it is let through |

## Current Checkpoint Review

Checkpoint V is where 7.1.5 became two questions instead of one.  What an
expression *comes to* stays `sema_constexpr.cpp`'s; whether the program was
allowed to write the declaration at all is `sema_constexpr_declaration.cpp`'s
`ConstexprRequirement` - 7.1.5p3's literal return and parameter types and
non-virtual dispatch at the declarator, p4's initialized member and base
subobject at the two places 12.6.2p10's walk already finds nothing to
construct, p9's literal type and constant initializer beside the fold.  3.9p10
is a fact of the *class*, settled where 9.2p2 completes it beside 12.1p5's
triviality, and `valued_class` is that same walk read again over the narrower
set `SemaConstant` holds.  It carried the fold the requirement made answerable:
8.5p6's default-initialization of an object that wrote no initializer,
12.6.2p8's brace-or-equal-initializer for a member no mem-initializer names, and
3.9p10's array of arrays as a list of lists.

That split is right, and it is the split this layer needed: a compiler that asks
only whether some later fold happened to succeed accepts every 7.1.5 violation
silently and lowers a dynamic initialization for a `constexpr` object 3.6.2
gives no constant one.  The three fold-side rules are right too and land
byte-identically: `constexpr standard_constructor implied;`, `= X()` beside it,
and `constexpr int grid[2][3] = {{1,2,3},{4,5,6}}` each lower to LowIR identical
to `pa21/cppgm++-ref`'s, and a partly written `constexpr int g[1000][1000] =
{{1}}` is 0.00 s and 6.4 MB against the reference's 5.41 s and 1.13 GB.

What the review found is that the requirement was asked of the wrong fact and at
the wrong number of doors.  A requirement is only as good as the fact standing
in front of it saying whether a failure is the program's, and V's fact -
`valued_class` - answers about the type of the object *declared* and says
nothing about the values the initializer *read*.  Beside it, 7.1.5 is written
about a declarator, and this build reads a declarator at four walks.

### Findings

**1. The requirement refused every program the reading merely ran out on.**
`NotConstant` has one kind, and its own comment says so: "not one of the constant
expressions 5.19 defines, *or* not one of the subset of them PA11 evaluates".
V made the first half a refusal and inherited the second half with it, gated only
by `valued_type` on the object's declared type:

```cpp
constexpr int values[3] = {1, 2, 3};
constexpr int n = *(values + 1);          // refused: "a type that is not arithmetic"
constexpr char text[] = "ab";
constexpr char first = text[0];           // refused: "text is not a constant expression"
constexpr const S *p = &s; constexpr int v = p->v;   // refused
template<class T> struct A { T e[2]; };
constexpr A<int> v = {3, 5};              // refused: 8.5.1p11's brace elision
constexpr int f(const int *p) { return *p; }
constexpr int m = f(&x);                  // refused: "an operator PA11 does not evaluate"
```

Every one of the five is a program `pa21/cppgm++-ref` and g++ both fold, every
one was accepted at `4853971d` and lowered as 3.6.2p2's dynamic initialization,
and every one names a value `SemaConstant` has no room for - which is group R,
the next checkpoint, and not the program's error.  `valued_type` cannot see any
of them: `n`, `first` and `m` are `int` and `char`, which it says yes about.
The fact is now `NotConstant::covered`, set where the refusal is thrown: 5.19's
answer about the program on one side - an overflow, a division by zero, a call of
a function no declaration made constexpr, 7.1.5p4's uninitialized member - and
this reading running out on the other.  Forty throw sites carry it, and each one
already said which it was in its own words.  The declaration carries the answer
forward on `SemaEntity::covered_constant`, because a *name* that reaches a
declaration whose fold ran out runs out for the same reason one name further
along.

**2. The same requirement was asked in a dialect that reads fewer of the
declarations 13.3 answers through.**  `--emit-types` is PA11's, and
`collect_conversions` stands behind `semantics()` there, so
`constexpr int n = c;` over a class with a `constexpr operator int` folds under
`--emit-semantics` and `--emit-lowir` and not under `--emit-types`.  Before V
that was a silent gap in one dialect; after it, PA11's reading **refused a valid
program**.  `demanded` now asks `semantics()`: which milestone a reading is for
decides how much of 5.19 it can answer, and a milestone that answers less
refuses nothing on that account.

**3. 7.1.5p3's requirements stood at one of the four declarators 7.1.5 is
written about.**  `require_function` was called from `function_definition`
alone.  A constructor is declared by `special_member`, a destructor with it, and
a conversion function by `conversion_function` - none of which reach that walk:

```cpp
struct nonliteral { nonliteral(); };
struct holder { constexpr holder(nonliteral) {} };            // accepted here
struct source { constexpr operator nonliteral() const; };     // accepted here
```

Both oracles refuse both, and 7.1.5p4's second bullet is word-for-word 7.1.5p3's
third.  The checkpoint's own `500-constexpr-class-parameter-nonliteral-bad`
pinned the one exit of the four that asked.  The requirement is now asked at all
three declarators that write one, with 12.1p1 and 12.4p1's single difference -
neither a constructor nor a destructor writes a return type - the only thing that
differs between them.

**4. 3.9p10's third bullet read only a constructor a declaration wrote.**
`constexpr_default_construction` is 12.1p5's question - would a written
`constexpr X() {}` satisfy 7.1.5p4 - and `settle_class` used its answer for
3.9p10 as well.  But 7.1.5p4 asks that every non-static data member be
initialized and 3.9p10 has no such sentence, so every class 8.5.1p1 leaves no
aggregate and that has a member no initializer reaches was **not a literal
type** here:

```cpp
struct B { int y; };
struct D : B { int x; };
constexpr D d = {};        // "declared with const struct D, which is not a literal type"
struct H { private: int v; };
constexpr H h = H();       // the same, and so is a class with a virtual function
```

Both oracles accept all four families, `__is_literal_type` in g++ says yes to
each, and the LowIR the reference writes for them is byte-identical to this
build's once the declaration is let through.  The two questions are now two
walks over the same members - `holds` asks 7.1.5p4's last bullet and gates the
fold, and without it the answer is 3.9p10's, which asks a base and a member of
class type only for a literal type that *has* a default constructor.  With
3.9p10 widened, 7.1.5p9's other half had to be asked on its own: a declaration
that wrote no initializer is initialized by 8.5p6, which for anything but a
class 12.1p5 gives a constexpr default constructor does nothing at all - a
refusal about the declaration that needs no value, and the one `valued_type`
does not gate, so `constexpr D d;` and `constexpr U u;` over a union stay
refused as both oracles refuse them.

### What the review confirmed rather than found

**The three carried fold rules hold byte-for-byte.**  8.5p6's
default-initialization, 12.6.2p8's held initializer and 3.9p10's array of arrays
each lower to LowIR identical to `pa21/cppgm++-ref`'s, and so do the four class
families finding 4 reopened.  Eighteen class shapes crossed with three
initializer forms - `= X()`, `= {}` and none - were swept through
`pa21/cppgm++-ref`, g++ and `__is_literal_type`.

**3.9p10 is one walk per class and one `unsigned char` read afterwards.**  8000
distinct non-aggregate classes each with a base and a `constexpr` object are
1.07 s at 220 MB, against 1.10 s at the checkpoint; one class of 6400 members of
class type is 0.15 s, unchanged.  Nothing in the two walks descends into a
subobject: each reads the answer that subobject's own class already carries.

**The value-initialized tail is still interned once per level.**  `constexpr
int deep[2]...[2] = {}` at depth 20 - a million elements - is 0.004 s and 6 MB,
and `constexpr int g[1000][1000] = {{1}}` is 0.004 s and 6 MB.

**Valgrind is clean** over the three course fixtures this review added, each
finding's probe programs, and every scaling probe it wrote.

### Recorded, not landed

**12.6.2p8's held initializer is read in the class and not beside the members
12.6.2p10 already settled.**  `struct S { int a = 1; int b = a + 1; };
constexpr S s;` is refused with "`a` is not a constant expression", because the
clause is folded in `held.scope` - the complete-class context 9.2p2 gives it -
where the constants `bind_constant` put on the constructor's own region are not
visible.  g++ folds it; `pa21/cppgm++-ref` refuses it as this build does, so no
`.ref` can pin the acceptance and the two readings would have to be one scope
that is the class *and* the settled members.

**A constexpr function declared and never defined is not asked.**  The reference
refuses `constexpr NL f();` and `constexpr int h(NL);` at the declaration;
N3485's 7.1.5p3 is written about "the definition of a constexpr function" and
g++ accepts both, so this build follows the standard and g++ against the
reference alone.  `constexpr ~X()` is the mirror: the reference and this build
accept it, and g++ makes a constexpr destructor a C++20 feature.

**A class with a reference member is value-initialized here and refused by both
oracles.**  `struct X { int &r; }; constexpr X x = X();` folds to nothing
`valued_class` covers, so the requirement asks nothing - which is the same
reference-value gap group R holds, read from the refusing side.

**8.5p7's zero-initialization is not laid down.**  Value-initializing an object
whose default constructor is neither user-provided nor constexpr zero-initializes
it before that constructor runs, and this reading has no arm for those zeroes -
so the refusal is `covered = false` and the object takes 3.6.2p2's dynamic
initialization.  For `struct H { private: int v; };` that is byte-identical to
the reference; for a class with a virtual function the reference lays out
`ptr addr @X__vtable + 16` where this build writes `zero 16` and a startup body,
which is the `vpointer_image` row group R already holds.

**A class with a base still folds to nothing.**  `constexpr D d = {};` is
accepted and lowered exactly as the reference lowers it, and `constexpr
base_ctor b = base_ctor();` - where the reference folds the base's own
constructor into an image - is the group B row it always was.

## Changes

- **`sema_declaration.h`, and forty throw sites across `sema_constexpr.cpp`,
  `sema_constant.cpp` and `sema_constexpr_statement.cpp` - `NotConstant::covered`**:
  the two halves of that class's own first sentence told apart, so 7.1.5p9's
  requirement reads 5.19's answer about the program and never this reading's
  edge.  `SemaEntity::covered_constant` carries it to the next name.
- **`sema_constexpr.cpp` - `fold_declared_object`**: returns whether it covered
  the declaration, which is what `require_object` beside it then asks of.
- **`sema_constexpr_declaration.cpp` - `demanded`, `uninitialized`**: the
  dialect a reading is for is part of how much of 5.19 it answers; and
  7.1.5p9's "shall be initialized" is a refusal about the declaration that needs
  no value, asked where `valued_type` says nothing.
- **`sema_constexpr_declaration.cpp`, `sema_class.cpp` - `require_function` at
  three declarators**: 7.1.5p3 and 7.1.5p4's parameter types asked wherever a
  declaration writes `constexpr` on a function, with 12.1p1's missing return
  type the one thing that differs.
- **`sema_constexpr_declaration.cpp` - `constexpr_default_construction(holds)`,
  `subobject_default_construction`**: 12.1p5's question and 3.9p10's third
  bullet as two readings of one walk, differing in the one bullet 7.1.5p4 has
  and 3.9p10 has not.

## Performance Evidence

Best of three per shape, alternating between the binaries.  `b8bd105a` is the
checkpoint as it landed.

| shape | this build | `b8bd105a` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| 500 / 2000 / 8000 non-aggregate classes with a base, each with a `constexpr` object | **0.06 / 0.26 / 1.07 s** at 19 / 59 / 220 MB | refused outright | - |
| 500 / 2000 / 8000 distinct classes with a `constexpr` object and a `static_assert` | **0.05 / 0.24 / 1.03 s** at 18 / 58 / 214 MB | 0.05 / 0.22 / 1.00 s at the same memory | - |
| one class of 400 / 1600 / 6400 members of class type, folded | **0.02 / 0.07 / 0.15 s** at 9 / 17 / 50 MB | 0.15 s at 6400 | - |
| one class of 400 / 1600 / 6400 scalar members with brace-or-equal-initializers | **0.01 / 0.04 / 0.14 s** at 8 / 16 / 47 MB | 0.15 s at 6400 | - |
| 500 / 2000 / 8000 `const` class objects that wrote no initializer | **0.03 / 0.09 / 0.17 s** at 8 / 15 / 43 MB | 0.17 s at 8000 | - |
| 500 / 2000 / 8000 `constexpr` declarations whose fold runs out on an address | **0.03 / 0.10 / 0.40 s** at 11 / 27 / 92 MB | refused at the first one | 0.57 / 0.72 / 2.00 s |
| `constexpr int deep[2]...[2] = {}` at depth 8 / 16 / 20 / 24 | **0.004 s** at 6 MB throughout | 0.004 s | - |
| `constexpr int g[1000][1000] = {{1}}` | **0.004 s** at 6 MB | 0.004 s | 5.41 s at 1.13 GB |

Row one is finding 4 and row six is finding 1: both are programs the checkpoint
refused and this build translates, so what they cost is the whole cost of
translating them and not an addition to anything.  Every other row is within
measurement noise of the checkpoint, because the two new walks read facts their
subobjects already carry and the one new field is a `bool` on a declaration.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **110 / 142**, from 107 / 139:
  the same 32 failures the turn started with, name for name, and the three
  course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Three course fixtures added, each byte-identical to `pa21/cppgm++-ref` after
  the comparison's own canonicalization: a class with a base, a class 11p1's
  access leaves no aggregate, a `constexpr` function returning one, and a
  constructor and a conversion function whose types are literal; and the two
  refusals findings 3 names.  All thirteen course fixtures pass, and the ten
  that predate this review were regenerated from the reference unchanged.
- Eighteen class shapes crossed with three initializer forms swept through
  `pa21/cppgm++-ref`, g++ and `__is_literal_type`; every disagreement that
  remains is one the reference alone makes, or one of the four recorded above.
- `valgrind -q --error-exitcode=9` over the three new course fixtures, each
  finding's probes and every scaling probe: **clean**.
