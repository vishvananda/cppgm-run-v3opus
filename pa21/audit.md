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
| A | `d9d9a8af` | 3 / 3 + 5 recorded | **the two sentences one name is read by, each asked at one of the places it is written.**  A made `named_value` ask 5.3.1p3 before it reads a static data member the class initialized, and made 14.6p8's stand-in a fact the readings that decide something carry rather than lose - and both rules are right.  What the review found is that each was written at one exit of its own family.  7.2p1 gives an enumerator with no constant-expression the value of the one before it plus one, and the stand-in was asked only of the enumerator that *wrote* one - so `enum { first = sizeof(T), second };` marked `second` a constant holding the stand-in's arithmetic, and `char check[second == 5 ? 1 : -1]` beside it was **refused as a negative array bound** where both oracles translate the program, with the spelling `second = first + 1` one character away already right.  And 4.2p1 - the sentence that makes `&numbers[2]` an address constant at all - was asked at the subscript's left operand and nowhere else, by a `named_array` lookup of the spelling: `numbers + 1`, `1 + numbers`, `numbers == numbers`, `numbers != other`, `!numbers`, `numbers ? a : b` and `*numbers` were each `numbers is not a constant expression`, where g++ folds all seven and `pa21/cppgm++-ref` folds four.  It is one door - what a name of array type is *worth* is which object it is, because an array has no value for any reader to wait on - and each reader applies the conversion it was already written to apply.  Beside them 3.2p2 itself: the pointer half of the substitution was gated on `lowering()` where the arithmetic half above it was not, so `--emit-semantics` wrote the member's *name* where `pa12/cppgm++-ref` writes the initializer read at the name, which is the same answer PA21 gives |
| M | `8a154542` | 2 / 2 + 4 recorded | **the operand 5.1.1p13's third bullet is written about, and the stand-in 14.6p8 carries on a value but not on a refusal.**  M is the checkpoint that owns nothing: its three groups delete a second reading of a question the expression layer already answers, and all three rules are right - the cast-from-call door now agrees with `call_expression` at all eight shapes a hidden class may be written in, and `unsettled_callee` stands a value in only where the definition is a *pattern's* and 7.1.5p2 has not already refused the call.  What the review found is that each of the two new facts was written at one exit of its own family.  5.1.1p13's bullet is about an *unevaluated operand* and the depth was taken at `sizeof`'s door alone, so `decltype(S::first + 0)` and `noexcept(S::first)` were each **refused** with a diagnostic about `this` - the very sentence the group exists to say does not apply - where g++ translates both; the `decltype` half hides one step behind an id-expression operand, which `resolve` answers before the expression layer is reached at all.  And 14.6p8's stand-in is an `int` of 1 whatever the call returns, so a place that reads *through* it runs out - and checkpoint A's audit had made the places that decide something carry that answer on the value they arrived at and not on the refusal, which only `array_bound` also caught.  7.2p1's enumerator, 7p4's `static_assert`, 14.3.2p5's template argument, 9.6p1's bit-field width and 7.6.2p1's alignment-specifier each **refused** `make().v` for a `make` the pattern declares, one line from a bound that already folded it - five programs both oracles translate, turned into errors by a stand-in that exists to keep the pattern's reading from deciding anything.  `counted_where` is now the one reading all four places that count ask, and 7p4 and 14.3.2p5 ask the same question at their own doors |

## Current Checkpoint Review

Checkpoint M is the one that owns nothing.  Its three groups are three
questions the *expression layer* already answers and that a second reading
answered differently, so what M does is delete a reading rather than add one.
3.3.10p2 hides a class or an enumeration behind a variable, a data member, a
function or an enumerator of that name declared in the same region, and 3.4.4p2
leaves it reachable through an elaborated-type-specifier alone - so
`LookupKind::Type` is 3.4.4p2's question and belongs at `elaborated`, and what
tells 5.2.3's cast from 5.2.2's call at the tree door and at the flattened one
is 3.4.1's ordinary lookup with `names_a_type` after it (group H).  14p1
declares no function until a template is instantiated, so a reading of a
*pattern* holds the definition of nothing the pattern declares, and
`unsettled_callee` is that sentence: 14.6p8 stands a value in for the call
rather than calling the missing body 7.1.5p2's error (group T').  And 5.1.1p13's
third bullet is the one place a non-static data member is named with no object
at all - an unevaluated operand, which `unevaluated_` is the depth of (group U).

All three rules are right.  The cast-from-call question now has one answer at
all three doors: over the eight shapes a hidden class can be written in - an
object declaration, a pointer declarator, a parameter, a base-specifier, a
template argument, a functional cast, a `static_cast` and `sizeof` - this build
and g++ agree at every one, where `pa21/cppgm++-ref` reads all eight as the
class; the tree door folds `tone(4)` to 12 and the flattened `H<tone(4)>` with
it, and `struct S` still reaches the class a function of that name hides.  The
stand-in fires only where the definition is genuinely a pattern's: a member of a
nested class, a default argument, a brace-or-equal-initializer, a bit-field
width and a base pattern's member each fold, a member the program did not write
`constexpr` on is still refused, and 14.7.1p1's own reading answers each of them
differently for two argument lists.  And `sizeof(scalars::keys)` reads at
namespace scope, in a static member's initializer, inside a class template and
from a member function of an unrelated class alike, at 0.01 / 0.06 / 0.26 s for
500 / 2000 / 8000 of them where the reference is 0.60 / 0.83 / 2.44 s.

What the review found is that each of the two new facts was written at one exit
of its own family: 5.1.1p13's bullet is about an *unevaluated operand* and was
asked at one of the three doors that open one, and 14.6p8's stand-in is carried
by the places that decide something only where the reading *arrived* at a value
and not where it ran out on the stand-in it had already made.

### Findings

**1. 5.1.1p13's third bullet asked at one of the three unevaluated operands.**
The depth was taken at `sizeof_expression`, and 7.1.6.2p4's decltype-specifier
and 5.3.7p1's `noexcept` operand are unevaluated by the same sentence - the
header comment already claimed the first of those.  Neither took it:

```cpp
struct S { int first; unsigned keys[8]; };
typedef decltype(S::first + 0) width;      // "`this` is written outside a
constexpr bool safe = noexcept(S::first);  //  member function"
```

Both are programs g++ translates, and both were **refused** with a diagnostic
about `this` - the very sentence group U exists to say does not apply.  The
`decltype` half hides behind one more step: `decltype(S::keys)` reads, because
an *id-expression* operand is answered by `resolve` before the expression layer
is reached at all, so only an operand that is anything more than a name - `S::a
+ 0`, a comparison, a call - falls through to the reading that asks for an
object.  `nonthrowing_operand` is the one door both of 5.3.7's readers come
through, the fold's and the expression layer's, so one record there answers the
operator wherever it is written.  `pa21/cppgm++-ref` refuses both shapes itself
(`failed to resolve member id-expression` for the first, `static_assert
unevaluated` for the second), so no `.ref` can pin the acceptance and g++ is the
oracle; the existing course fixture is written over `sizeof` alone for that
reason and stays so.

**2. 14.6p8's stand-in carried on the value and not on the refusal.**  A reading
of a pattern stands one value in wherever an argument list is what settles
something, and that value is an `int` of 1 whatever the call's declared return
type is.  So a place that reads *through* it runs out - a member access, a
member call and a subscript each ask what kind of thing the operand is, and an
`int` is no object of class type and no array.  Checkpoint A's audit made three
of the places that *decide* something carry the answer, and it made them carry
it on the value alone; `array_bound` was the only one that also caught the
refusal.  Five did not:

```cpp
struct box { int v; constexpr box(int n) : v(n) {} };
template<class T> struct holder
{
  static constexpr box make() { return box(sizeof(T)); }
  enum { first = make().v };                  // 7.2p1
  static_assert(make().v > 0, "");            // 7p4
  typedef chosen<make().v> by_member;         // 14.3.2p5
  unsigned counted : make().v;                // 9.6p1
  alignas(make().v) unsigned char aligned;    // 7.6.2p1
};
```

Every one of those five is `a constant expression reads a member of what is not
an object of class type` - a program both oracles translate, **refused**, and
refused with a diagnostic that says nothing about a template - where
`typedef char sized[make().v];` one line away already folded.  A member *call*
on the stand-in and a subscript of a pointer one hands back are the same
refusal one word over.  The rule was already written at `array_bound` and its
comment already says it: what such a reading ran out on is the stand-in and not
the program.  `ConstexprReading::counted_where` is now that reading for all four
places that count - 8.3.4p1's bound, 7.2p1's enumerator, 9.6p1's width and
7.6.2p1's alignment ask one door, which takes the count where the expression
stands and answers whether it is an answer at all - and 7p4's condition and
14.3.2p5's argument ask the same question at their own doors.

### What the review confirmed rather than found

**`unsettled_callee` refuses what 7.1.5p2 refuses.**  The gate is
`constexpr_function` before anything else, so a member the program declared
without it is the program's error at the pattern and at the instantiation
alike, and both oracles agree; a `static_assert` the arguments make false is
found at the instantiation, not swallowed at the pattern.  The two arms - a
callee whose own region is a pattern's, and a specialization made over a
dependent argument - are asked only where the body is missing *and* a definition
is being checked, so a call that folds pays neither.

**The flattened door and the tree door tell a cast from a call the same way.**
`SpelledTypeId::read` and `call_or_cast` both ask 3.4.1 and then `names_a_type`,
which is what `call_expression` in the expression layer has always asked, and
`elaborated` keeps 3.4.4p2's reading for the one spelling it belongs to.  Probed
over the hidden-class cross-product against g++ and the reference; the four
shapes this build refuses at the recognizer are refused by g++ too.

**The unevaluated depth has no reachable leak.**  It is a fact of the whole
reading rather than of the operand's region, so a declaration read on the way -
14.7.1p1's instantiation of a class named inside the operand, a member's
brace-or-equal-initializer folded there - carries it.  Every such shape was
probed: a static_assert, an array bound and an enumerator written in a pattern
instantiated from inside a `sizeof` each still refuse the member named with no
object, because the *fold* has nothing to read even where the type is given.  A
put-aside record for the depth was written, measured to change nothing anywhere,
and removed rather than kept as code no probe motivates.

**The catch costs nothing where nothing refuses.**  8000 enumerators folding a
scalar call are 0.14 s and the same count reading through the stand-in 0.19 s;
8000 template arguments 0.43 s against 0.49 s.  Both are linear, and the
difference is one refusal per place rather than a second reading of anything.

**Every course fixture is the reference's own output.**  All twenty-seven were
regenerated from `pa21/cppgm++-ref` and compare byte-identically, exit status
included, the twenty-five that predate this review unchanged.

**Valgrind is clean** over the course fixtures, every probe of the two findings
and every scaling program above.

### Recorded, not landed

**`sizeof` over an expression is no template argument here.**  The flattened
reader reads `sizeof` over a type-id alone, so `H<sizeof(g)>` folds and
`H<sizeof(g + 0)>` is `written inside sizeof as a template argument and names no
type` - for a plain global as much as for a member, which is what says it is not
group U's.  g++ folds both.  It is `TemplateArgumentReader`'s own reading and
predates the checkpoint.

**The reference reads every name where a type may stand as 3.4.4p2 does.**
`sizeof(S)` where a function of that name hides the class measures the class
there, measures the function's return type here and is refused by g++; the
plan's stage design already records the family, and no fixture reaches it.

**A pattern's ill-formed member initializer waits for a use.**  `template<class
U> struct W { static constexpr int n = S::a; };` for a non-static `S::a` is
accepted until something reads `n`, where the same declaration outside a
template is refused at the class.  It is not the depth's - the shape is
accepted identically with `unevaluated_` never set - and it is not this
checkpoint's.

**Two oracles part on the pointer a `->` reaches.**  `make().at()[0]` written as
a class member's initializer is `unsupported constexpr class member
initializer` in the reference and folded by g++ and this build, so the finding-2
fixture pins the member access, the member call and the four counting places and
leaves the subscript to g++.

## Changes

- **`sema_constexpr_statement.cpp` - `counted_where`**: the one reading the four
  places that count ask, taken where the expression stands, so 14.6p8's answer
  travels with the count instead of being asked again by each place - on the
  refusal as much as on the value.
- **`sema_constant.cpp` - `array_bound`**: the two hand-written halves of that
  question replaced by the one door, leaving 8.3.4p1's own three rules.
- **`sema_enum.cpp` - `enumerators`**, **`sema_layout.cpp` -
  `requested_alignment` and `bit_field_declaration`**: 7.2p5, 7.6.2p1 and 9.6p1
  ask it too; a width the reading has no answer for is one bit and an alignment
  it has none for asks for nothing, until the specialization reads its own.
- **`sema_analyzer.cpp` - `static_assert_declaration`**,
  **`sema_value_expression.cpp` - `template_argument_value`**: 7p4's condition
  and 14.3.2p5's argument carry the same answer at their own doors.
- **`sema_constant.cpp` - `decltype_type`**, **`sema_noexcept.cpp` -
  `nonthrowing_operand`**: 7.1.6.2p4's and 5.3.7p1's operands are unevaluated,
  so the depth is taken at each of the three doors that opens one.

## Performance Evidence

Best of three per shape.  `8a154542` is the checkpoint as it landed.

| shape | this build | `pa21/cppgm++-ref` |
| --- | --- | --- |
| 500 / 2000 / 8000 `sizeof` of a member with no object | **0.01 / 0.06 / 0.26 s** at 9.3 / 18.6 / 55.0 MB | 0.60 / 0.83 / 2.44 s at 18 / 30 / 75 MB |
| the same count as `decltype(<member> + 0)` | **0.02 / 0.08 / 0.38 s** at 10.9 / 24.8 / 80.3 MB | refuses the shape |
| the same count as `noexcept(<member> + <n>)` | **0.01 / 0.06 / 0.25 s** at 9.6 / 18.7 / 56.6 MB | refuses the shape |
| 50 / 100 / 200 nested `sizeof` over one such operand | **0.00 s** at 6.4 / 6.5 / 6.9 MB | - |
| 500 / 2000 / 8000 enumerators reading through a stand-in | **0.01 / 0.04 / 0.19 s** at 8.5 / 13.7 / 34.7 MB | refused at the first before `8a154542` |
| the same count of enumerators folding a scalar call | **0.01 / 0.03 / 0.14 s** at 8.1 / 13.1 / 32.5 MB | - |
| 500 / 2000 / 8000 template arguments reading through a stand-in | **0.02 / 0.11 / 0.49 s** at 11.2 / 22.6 / 70.0 MB | - |
| the same count of template arguments folding a scalar call | **0.02 / 0.09 / 0.43 s** at 10.7 / 21.7 / 67.3 MB | - |
| 500 / 2000 / 8000 bit-field widths reading through a stand-in | **0.01 / 0.04 / 0.18 s** at 9.0 / 15.0 / 39.9 MB | - |
| 2000 / 8000 / 32000 ordinary bit-fields, which now ask the same door | **0.02 / 0.08 / 0.39 s** at 11.4 / 27.2 / 90.3 MB | - |

The three unevaluated rows are the finding-1 shapes, two of which no reading
answered before it.  The pairs below them are finding 2 against the same count
of the same construct folding a call that needs no stand-in: 36% and 14% at
8000, which is one refusal per place, and linear either way.  The last row is
the path the shared door added a `counted` call to for every bit-field a program
writes, template or not.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **154 / 159**, from
  152 / 157: the same 5 failures the turn started with, name for name, and the
  two course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Two course fixtures added, both oracles agreeing on each:
  `300-the-value-a-pattern-stood-in-for-is-read-through` is byte-identical to
  `pa21/cppgm++-ref`'s LowIR and accepted by g++, and
  `500-a-pattern-stands-no-value-in-for-the-call-it-may-not-make-bad` is refused
  by g++, by the reference and by this build.  All twenty-seven course fixtures
  pass, and every one - the twenty-five that predate this review included - was
  regenerated from the reference and compared unchanged.
- Fifty-four probe programs over the three unevaluated operands, the eight
  shapes a hidden class may be written in, the places 14.6p8's stand-in is read
  through, the deciders that turn one into an answer, and the readings taken
  inside an instantiation - swept through `pa21/cppgm++-ref`, `pa12/cppgm++-ref`
  and g++ with each disagreement judged rather than copied.
- `valgrind -q --error-exitcode=99` over both new fixtures, every finding's
  probes and every scaling program: **clean**.
