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
| R | `6d975910` | 5 / 5 + 3 recorded | **the object an initializer designates, asked at two of the eleven places 8.5 fills a pointer from - and the lvalue walk that asks it running the value reading a second time at every level of nesting.**  R made 5.19p2's address a constant of the same standing as a value and the rule is right: `SemaConstant::object` is 3.10p1's glvalue, `ConstantAddress` is *which object*, and 4.2p1's decay of `static char buf[3];` is a conversion that reads no value because there is none to read.  `operand_constant` is the door that asks it, and it stood at two exits: `= buf` on a declaration of pointer type, and an argument of a call.  `(buf)`, `{buf}`, a clause of an aggregate, an element of an array, 12.6.2p8's brace-or-equal-initializer, a mem-initializer, 8.3.6p1's default-argument, 6.6.3p2's `return buf;`, `static_cast<char *>(buf)` and a function name written at one of them each **refused a program both oracles translate**, one place of 8.5 at a time.  Under that door the retry was the whole operand twice: `designated`'s own last resort is `evaluate`, which is the reading that had just refused - so a call written on a call written on a call cost 2^depth, and eighteen levels of `f(f(...(nonconst)))` was **2.67 s** where the checkpoint's own fixtures are one level deep.  Beside them 4.10p1: `holds_address` asked the *type* and not whether this reading holds a value of it, so `static int *held;` handed a place of pointer type the identifier **zero**, and `static_assert(identity(held) == 0, ...)` **passed** where both oracles refuse the program.  And 5.19p3's user-defined conversion, which `at_arithmetic_place` asks at every arithmetic place, was asked at no pointer place at all - `constexpr int *p = c;` over a `constexpr operator int *` was refused where both oracles fold it.  And the refusal a valueless operand makes claimed 5.19's answer about the *program*, which undid checkpoint V's own rule one name further along: `constexpr D d = {}; constexpr int r = f(d.x);` over group B's class with a base became a hard error where the checkpoint before R accepted it and lowered 3.6.2p2's dynamic initialization |
| B | `afbfc093` | 3 / 3 + 6 recorded | **the declared types a subobject may have, asked at the one walker of the three that lays a list out - and the half of 13.3.1.4p1 no place of class type asked.**  B made 10p1's base subobject an entry of the interned list and the rule is right: one index reaches a subobject for a member access, a base conversion, an address path and the image alike, and every reading that already existed asks it.  The sweep of the *declared types* it left is where the review landed.  `object_of`'s aggregate arm and `array_of` each have an arm for a subobject of array type; `subobject_initialized`, which B split out and rewrote, has none - so `pair{1, 2}`, `partial{5}`, `emptied{}` and an array of class type no mem-initializer names were each **refused** where both oracles fold them, and `emptied{}` came to the *empty* interned list under an array type, so `one.emptied[0]` was "subscripts an array outside its bounds" and not 8.5p7's zero.  Beside it 13.3.1.4p1: a place of class type filled from a value of another class asked the place's converting constructors and never the value's conversion functions, and asked them at three doors that each performed the initialization themselves - so `constexpr payload p = source(1);` over `constexpr operator payload() const` was refused at a declaration, an aggregate clause, an array element, 12.6.2p8's brace-or-equal-initializer and 6.6.3p2's return alike, where both oracles fold every one.  8.5p16 is now one reading, `at_class_place`, which `initialized_value` and `clause_of` ask instead of building the object themselves.  And the checkpoint's own known gap was six times wider than it was written: the reference emits the *complete-object* entry of a constructor the program declared `constexpr` wherever it writes that definition out for a base subobject, folded image or none - so every single-file program with such a base differed by one function, which is what the fixture's 12.1p5 workaround was hiding |

## Current Checkpoint Review

Checkpoint B is where 10p1's base class subobject became an entry of the list a
constant of class type holds, rather than the reason `object_of` refused the
class outright.  12.6.2p10 says where the entry goes - before the members, in
base-specifier order, which is the order 9.2p13 lays the storage out in - so one
index reaches a subobject for a member access, for a base conversion, for an
address path and for the image alike, and `Derivation::subobject_path` answers
the *steps* of the one derivation walk that already answered the byte.

That is the right shape and it holds at every reading that already existed.
`member_path` and `member_address` for a name 10.2 found in a base,
`subobject_initialized` for a mem-initializer that writes a base's type and for
12.6.2p4's default-initialized one, `at_class_place` and `bound_object` for
4.10p3 down and 5.2.9p11 back up, `constant_image` and `global_constructed` for
the image at the byte the class laid the base out at.  A derivation 160 deep is
0.03 s and a class with 200 direct bases 0.05 s, both linear; twelve probes over
a base of a class template, a typedef-named base, a qualified base, an empty
base, a base of a base, 10.2's hiding, 11p1's private member, 12.8p15's copy and
14.5.3p4's `Parts(value)...` all lower byte-identically to `pa21/cppgm++-ref`.

What the review found is the sweep the widened list did *not* carry: the
declared types a subobject may have, asked at the one walker of the three that
lays a list out, and the half of 13.3.1.4p1 that no place of class type asked.

### Findings

**1. A member of array type is the declared type `subobject_initialized` has no
arm for, at all four of its exits.**  `object_of`'s aggregate arm has one and
`array_of` has one; the walker checkpoint B split out and rewrote asks only
`held.base || is_class(type)` and reads everything else as one value `convert`
reaches:

```cpp
struct written { int pair[2]; int partial[3]; int emptied[2];
  constexpr written() : pair{1, 2}, partial{5}, emptied{} {} };
struct built { counted elements[2]; constexpr built() {} };
struct nested { int grid[2][2]; constexpr nested() : grid{{1, 2}, {3, 4}} {} };
struct carried : written { int extra; constexpr carried() : written(), extra(9) {} };
```

Every one is a program `pa21/cppgm++-ref` and g++ both fold.  `pair{1, 2}` and
`grid{{1, 2}, {3, 4}}` were "a mem-initializer writes more than one value for a
member", `built` was 7.1.5p4's "initializes no value for elements", and
`emptied{}` was worse than a refusal: 8.5p7's empty list took the `value.bits =
0` the scalar arm writes, which under an array type is the identifier of the
*empty* interned list - so `one.emptied[0]` read "a constant expression
subscripts an array outside its bounds" where 8.5p7 gives it zero.  The
mem-initializer of an array is now `array_of` over `clause_of`'d elements, which
is the reading `clause_of` gives a braced-init-list written anywhere else, and
12.6p1's default-initialization of an array of class type is `array_of` with no
clause - the same sentence the `built` arm one line above already writes for a
member of class type.  Checkpoint B is what made `carried` reachable; the other
four were refused before it too.

**2. A place of class type filled from a value of another class asked one half
of 13.3.1.4p1's candidate set, at three doors that each performed the
initialization themselves.**  8.5p16 gives that set two halves - the converting
constructors of the place's class and the conversion functions of the value's -
and `match_by_value` asks the second exactly where no one of the first takes the
value.  `at_class_place` went straight to `object_of`, which is the first half
alone; `initialized_value` and `clause_of` did not even reach it, each calling
`object_of` themselves:

```cpp
struct payload { int held; constexpr payload(int v) : held(v) {} };
struct source { int seed; constexpr source(int v) : seed(v) {}
  constexpr operator payload() const { return payload(seed + 1); } };
constexpr source origin(1);
constexpr payload assigned = origin;                       // refused
constexpr aggregate clauses = { origin, source(5) };       // 8.5.1p2: refused
constexpr payload elements[2] = { origin, source(7) };     // refused
struct holding { payload kept = source(20); };             // 12.6.2p8: refused
constexpr int reads(payload given) { return given.held; }  // reads(origin): refused
constexpr payload gives(source given) { return given; }    // 6.6.3p2: refused
```

Both oracles fold all six.  8.5p16 is now one reading: `at_class_place` asks
`converting_constructor` and, where no one of them takes the value, the same
`converted` door 5.19p3's arithmetic and pointer places already ask - and
`initialized_value` and `clause_of` ask *it* rather than building the object.
8.5p16's other half stays where it was: parentheses are a
direct-initialization, whose clauses are 13.3.1.3's arguments over the class's
own constructors, so `constexpr payload p(origin);` is still `object_of`'s
question and not this one.

**3. The complete-object entry of a base's `constexpr` constructor, which the
checkpoint recorded as a fold's gap and is not one.**  The plan's row read the
difference as "a constructor of a base a folded image went *through*".  The
sweep says it needs no fold at all:

| base constructor | ref emits | this build, at `afbfc093` |
| --- | --- | --- |
| `A(int v) : a(v) {}` | `C2` | `C2` |
| `constexpr A(int v) : a(v) {}` | `C2` **and `C1`** | `C2` |
| `constexpr A() = default;` | `C1` | nothing |
| implicit, 12.1p5's | `C2` | `C2` |

`int main() { B x(1); }` over the second row differs by one whole function, with
no `constexpr` object, no fold and no image anywhere in the program - so every
single-file program with a base the program wrote a `constexpr` constructor for
differed from the reference, which is what the checkpoint fixture's 12.1p5
workaround was hiding.  7.1.5p2 makes such a constructor implicitly `inline` and
5.19 may name it in any unit that reads its class to build a *complete* object,
which is a use no call in this one spells - so `writes_base_entry` owes both of
the ABI's entry points for it, exactly as it already does for a definition no
other unit may hold.  Twenty base probes that differed by that one function now
compare identically.

### What the review confirmed rather than found

**The widened list is linear in every direction.**  A derivation 20 / 40 / 80 /
160 deep, read at both ends, is 0.00 / 0.00 / 0.01 / 0.03 s at 6.9 / 7.9 / 9.7 /
12.8 MB; a class with 25 / 50 / 100 / 200 direct bases 0.00 / 0.01 / 0.02 /
0.04 s.  A member call on an object at depth 5 / 10 / 20 / 40, folded 2000 times
with a distinct argument each pass so no memo answers, is 0.05 / 0.08 / 0.14 /
0.29 s - `bind_subobjects` binds one constant per subobject per call and walks
the hierarchy once, not once per name.  The same shape at 1000 / 4000 / 16000
calls is 0.04 / 0.16 / 0.67 s.

**The image reads the same list the fold wrote.**  A base holding a `char`
before a `double` member, an empty base 9p6 gives no bytes, a base whose members
carry 12.6.2p8's brace-or-equal-initializers and a base of a base each lay out
byte-identically to `pa21/cppgm++-ref`, padding included.

**The dialects part where the assignment parts them.**  `--emit-semantics`
refuses a base clause outright ("PA12 does not describe"), and `--emit-types`
walks none - so `subobjects` there is the members alone, which `member_path`,
`address_type` and `bind_subobjects` all read the same way.  The answers stay
consistent with one another rather than silently shifting by one index, and the
reference refuses those programs in `--emit-types` too.

**Valgrind is clean** over all twenty-one course fixtures, every probe of the
three findings, the depth and width sweeps and every scaling program above.

### Recorded, not landed

**`arr()` is the one spelling of an array mem-initializer no `.ref` can pin.**
`constexpr B b;` over `struct B { int arr[2]; constexpr B() : arr() {} };` folds
here and in g++ and is `unsupported constexpr variable initializer` in
`pa21/cppgm++-ref`, which takes `arr{}` two characters away.  12.6.2p8 makes the
two one value-initialization and this build reads them as one.

**A direct-initialization of a class place through a conversion function is
refused.**  `constexpr payload p(origin);` is 13.3.1.3 over `payload`'s own
constructors with one user-defined conversion inside the argument's sequence;
`selected` does not find it.  g++ folds it and `pa21/cppgm++-ref` **segfaults**
on it, so no fixture can pin either answer, and the copy-initialization spelling
one character away is what finding 2 opened.

**Where the reference draws the base's `C1` line is a file position.**  With the
two class definitions written in the primary source file the reference emits
both entry points; with the identical tokens reached through `#include`, or in a
two-unit invocation where both units use the base, it emits the base entry
alone.  Phases 1-7 keep no source position, so this build owes both wherever it
writes the definition out - which is what g++'s object file holds too, and which
no fixture reaches, because none of the 150 uses an `#include`.

**A reference *declaration* folds nothing.**  `constexpr int const &r = n;` is
refused, and so is every reference to a class or to a base of one, because
`fold_declared_object` gates on `const` standing on a type that is built,
addressed or arithmetic and a reference is none of the three.  Both oracles fold
it.  It is `fold_declared_object`'s and reaches no reading checkpoint B owns -
`at_reference_place` itself is right, which is what `as_base()` and
`static_cast<tagged_base const &>(one)` in the checkpoint fixture pin.

**9.5p1's union is still the one shape `valued_class` parts.**  `constexpr U
u(5);` over a union with a constexpr constructor is refused - `subobjects` gives
an entry per member where 9.5p1 has one storage - and both oracles fold it.
Checkpoint V's row already records it; checkpoint B closed the base half of that
sentence and left this one.

**11.2's access on a base-specifier is not asked, and g++ is alone in asking.**
`struct D : private B` reaching `take(B const &)` is accepted here and by
`pa21/cppgm++-ref` and refused by g++.  The mirror is 10.2's ambiguity, where
two different bases declaring one member name are refused here and by g++ and
accepted by the reference; this build follows the standard on both.

## Changes

- **`sema_constexpr_object.cpp` - `subobject_initialized`**: 3.9p10 and 8.3.4p6
  make a member of array type a subobject holding a list of its own, so a
  mem-initializer's clauses are 8.5.1p2's elements read through `clause_of` and
  handed to `array_of`, and 12.6p1's default-initialization of an array of class
  type is that same call with no clause.
- **`sema_constexpr.cpp` - `at_class_place`**: 13.3.1.4p1's second half, asked
  where no converting constructor of the place's class takes the value - the
  order `match_by_value` asks the two halves in.
- **`sema_constexpr.cpp` - `initialized_value`, `sema_constexpr_object.cpp` -
  `clause_of`**: 8.5p16's copy-initialization of a place of class type from one
  value is `at_class_place` and not each door's own `object_of`, so a
  declaration, an aggregate clause and an array element reach 4.10p3's base
  subobject and 13.3.1.4p1's conversion function where an argument already did.
  8.5p16's direct-initialization stays `object_of`'s.
- **`lowir_lower.cpp` - `writes_base_entry`**: 7.1.5p2's implicitly inline
  constructor is one 5.19 may name for a complete object in any unit, so a unit
  that writes its definition out for a base subobject owes both of the ABI's
  entry points.

## Performance Evidence

Best of three per shape, alternating between the binaries.  `afbfc093` is the
checkpoint as it landed.

| shape | this build | `afbfc093` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| 1000 / 4000 / 16000 elements of one array mem-initializer, read back | **0.01 / 0.05 / 0.23 s** at 10 / 23 / 76 MB | refused at the mem-initializer | 0.59 / 0.80 / 1.70 s at 25 / 55 / 178 MB |
| 125 / 500 / 2000 array members each writing `{1, 2}` | **0.00 / 0.02 / 0.09 s** at 7.7 / 13 / 33 MB | refused | - |
| 1000 / 4000 / 16000 folds reaching a class place through a conversion function | **0.04 / 0.19 / 0.83 s** at 17 / 49 / 178 MB | refused at the first one | refuses the loop (`static_assert unevaluated`) |
| 16000 folds reaching a class place a converting constructor takes | **0.69 s** | 0.66 s | refuses the loop |
| the same with 0 / 8 / 32 constructors declared, 4000 folds | **0.16 / 0.18 / 0.25 s** at a flat 41 MB | 0.16 s at 0 | - |
| a derivation 160 deep, read at both ends | **0.03 s** at 12.8 MB | 0.03 s | - |
| a class with 200 direct bases | **0.05 s** at 13 MB | 0.04 s | - |
| a member call at depth 40, 2000 folds with a distinct argument each | **0.27 s** at 81 MB | 0.27 s | - |
| 16000 such calls at depth 10 | **0.63 s** at 199 MB | 0.63 s | - |

Rows one to three are programs the checkpoint refused and this build translates.
Row four is the one row that pays for finding 2: a value of class type at a
place of class type now asks `converting_constructor` before `object_of` ranks
the same set, which is 4% and is linear in the candidates 13.3 has to walk
anyway.  A value that is *not* of class type asks nothing new - 16000 folds of
an `int` at a `payload` place are 0.43 s in both.  Every other row is unchanged.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **138 / 150**, from
  136 / 148: the same 12 failures the turn started with, name for name, and the
  two course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Two course fixtures added, each with both oracles agreeing:
  `300-the-array-a-mem-initializer-writes-the-elements-of` and
  `300-the-class-place-a-conversion-function-fills` are byte-identical to
  `pa21/cppgm++-ref`'s LowIR after canonicalization and accepted by g++.  All
  twenty-one course fixtures pass, and the nineteen that predate this review
  were regenerated from the reference unchanged.
- Fifty-one probe programs over the base subobject's readers, the declared types
  a subobject may have, 8.5's places of class type, the ABI's two entry points
  and the two earlier dialects, swept through `pa21/cppgm++-ref` and g++ with
  each disagreement judged; twenty of them run through the stage's own
  comparator in a scratch directory, where all twenty now pass.  A two-unit
  invocation and an `#include`d header are the one boundary left, recorded above.
- `valgrind -q --error-exitcode=9` over all twenty-one course fixtures, every
  finding's probes and every scaling program: **clean**.
