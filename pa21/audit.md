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
| E | `bc25598c` | 4 / 4 + 6 recorded | **the second spelling 5.2.3p1 gives one cast, the braces 8.5.4p1 lets a literal stand in, and the members a union does not hold.**  E turned the fold's reading of an initializer list inside out - the walk goes down the *subobjects* with a cursor, so 8.5.1p11's elided braces are one question asked per subobject - and set two sentences beside it: 5.2.9p4's cast to cv `void` and 1.4p8's branch hint.  All three rules are right, and what the review found is one exit of each.  5.2.9p4's refusal stood at `cast_expression`, which is 5.2.9's own spelling and `static_cast` alone - 5.2.3p1 makes `int((void)0)` the same construct, and it was **accepted**, lowering `convert zext i32 void`, an instruction with no operand, where both oracles refuse it; `cref((void)0)` bound a reference to the same nothing.  8.5.2p1's literal reached the brace-or-equal-initializer E gave it and not 12.6.2p2's mem-initializer written with braces: `struct A { char s[4]; A() : s{"ab"} {} };` stored `'a'` and then **three zeroes** where the reference and g++ both write `'a', 'b'` - the units standing under a node opened *inside* the list rather than under the list itself, which is one shape a reader of a list does not know.  9.5p1's union: `aggregate_constant` stops at the first member and `object_of` then padded the list back out to every member the union declares with 8.5.1p7's zeroes, so `constexpr U u = {5}; constexpr double d = u.b;` **folded to 0** where both oracles refuse the read - a refusal turned into a wrong answer by the one rule the walk had just stopped for.  And 8.5.3p5's temporary was laid out at the *operand's* width rather than the referenced type's, which are two widths exactly where a call computes nothing: `const long &r = __builtin_expect(6, 0)` named four bytes and read eight |

## Current Checkpoint Review

Checkpoint E is where 8.5.1p2 stopped being a walk of the clauses and became a
walk of the *subobjects*.  8.5.1p11 lets the braces around a subaggregate's own
clauses be left out, so how many clauses a subobject takes is what its own walk
arrives at and not a count anything works out in front of it -
`aggregate_constant` is that walk, `subobject_constant` is one step of it, and
`list_constant` is where the two questions only the braces' own place can answer
stand: 8.5.2p1's string literal for a whole array of character type, and
8.5.1p6's clause that reached no subobject.  Beside it two sentences about what
is not a value - 5.2.9p4's cast to cv `void`, which is evaluated and holds
nothing, and 1.4p8's `__builtin_expect`, the one reserved function whose
definition the implementation states here.

All three rules are right.  The four places a list stands - a declaration's
initializer, a clause of an enclosing list, 12.6.2p2's mem-initializer and
12.6.2p8's brace-or-equal-initializer - were probed against an array, an
aggregate class, a union, a string literal, an empty list and a list one clause
too long, and they answer alike: 8.5.1p6's refusal, 8.5.1p7's value-initialized
tail, 8.3.4p3's deduced bound and 8.5.1p15's one initialized member are each
settled *inside* the walk now and each still answered.  The branch hint is
byte-identical to the reference at an argument, a default-argument, a template
argument, an unevaluated operand, through a pointer to it and across two
translation units.

What the review found is that each of the four new sentences was written at one
exit of its own family: 5.2.9p4 at one of the two spellings 5.2.3p1 makes one
cast, 8.5.2p1 at one of the two forms 8.5.4p1 lets a literal be written in,
8.5.1p15 at the walk that *builds* a union and not at the list a reader takes a
member out of, and 5.2.2p10's prvalue at the value rather than at the storage
8.5.3p5 gives it.

### Findings

**1. 5.2.9p4 asked at one of the two spellings of one cast.**  5.2.3p1 says a
simple-type-specifier followed by a parenthesized expression is equivalent to
the corresponding cast expression, so `int(e)` and `(int)e` are one construct
read at two doors - `cast_expression` and `functional_cast` - and the refusal
was written at the first alone:

```cpp
void report();
int measured() { return int((void)report()); }   // accepted
const int &held = cref((void)0);                 // accepted, cref = const int &
```

Both are programs `pa21/cppgm++-ref` and g++ refuse and this build
**translated**, the first of them into `%t1 = convert zext i32 void ` - a
conversion whose operand is not written, because there is no value there to
write.  The two doors already share their two exits, which is where the reading
belongs: every cast to a type that is not a reference comes through
`cast_conversion`, whose own comment already said 5.2.9p4's cast to `void` is
the one that asks for no value, and every cast to a reference comes through
`cast_to_reference`, where 5.4p4's last resort is reinterpreting the storage the
operand named and an expression of type `void` names none.  One sentence at
each, and `cast_expression`'s own copy is gone.

**2. 8.5.2p1's literal read at one of the two forms 8.5.4p1 lets it be written
in.**  E gave the analysis 12.6.2p8's brace-or-equal-initializer its 8.5.2p1
door - `char s[4] = "ab";` written as a member - and 12.6.2p2's mem-initializer
reaches the same array through the *braced* form, which is a list:

```cpp
struct A { char s[4]; A() : s{"ab"} {} };   // s holds 'a', 0, 0, 0
```

The constructor stored `'a'` and then three zeroes where the reference and g++
both store `'a'` and `'b'`: the elements of that list are written by the one
reading in `sema_string_init.cpp`, which *opens the list itself* - so where the
caller had already opened one, as `list_initialize_into` does, the units stood
under a node inside the list rather than under the list, and what reads a list
found it empty and value-initialized every element.  `units_into` is that
reading asked of a list already open and `as_object` is it plus the node, which
leaves one list shape for every reader whether the clauses were units or
expressions.  The declaration `char s[4] = {"ab"};` was right all along because
the walk under a *declaration's* initializer reaches the units either way; only
a place that reads the list's own children - a mem-initializer's - could tell.

**3. 8.5.1p15 asked of the walk that builds a union and not of the list a
reader takes a member out of.**  `aggregate_constant` stops after the first
member, which is 8.5.1p15 exactly; `object_of` then padded the list back out to
every member the class declares with 8.5.1p7's value-initialized tail, which is
the sentence a union has none of:

```cpp
union U { int a; double b; };
constexpr U u = { 5 };
constexpr double d = u.b;     // folded to 0.0
```

9.5p1 leaves at most one member of a union holding a value, so the read is the
program's own error - both oracles refuse it - and it was **answered with a
zero**, which is the one answer that cannot be told from a real one:
`static_assert(u.b == 0.0, "")` passes.  A union's list now holds the member an
initialization began the lifetime of and stops there, so every reader that
indexes it - `subobject_value`, `member_value` through it, and the address walk
in `sema_address.cpp` that reaches the same entries - refuses the rest by the
rule that already stood there for an index past the end.  No image moved: the
reference and this build write the same bytes for `constexpr U u = {5}` before
and after, because 3.6.2p2's image is the analysis's own walk and not this list.

**4. 8.5.3p5's temporary laid out at the operand's width.**  A reference bound
to a prvalue binds a temporary *of the referenced type*, initialized from the
value - and the two are usually one type already, because a conversion the
initialization needed stands in the tree as a line of its own.  1.4p8's branch
hint is where they part: a call of it computes nothing, so what a body lowers is
the operand's own reading at the operand's own width, whatever 5.2.2p10 says the
call's type is.

```cpp
const long &bound = __builtin_expect(6, 0);   // slot of four bytes, read as eight
```

`bound_address` laid the temporary out from the value's type, so the storage was
four bytes and every read through the reference took eight - four bytes past the
end of a slot, in a program neither oracle writes that way.  The conversion
belongs where 8.5.3p5 puts it, at the initialization of the temporary, so the
storage has the type the reference reads it at; the reference binary writes the
same `i64` slot and the same `store i64 6`, and this build now matches it byte
for byte.

### What the review confirmed rather than found

**The cursor is the walk's own and costs one probe per level.**
`elides_its_braces` reads the clause standing at the cursor with a scratch-node
probe, once per level of a nesting, and the product is the depth times the size
of that first clause: 10 / 40 / 80 levels over a 1600-term clause are 0.09 /
0.33 / 0.65 s, linear in each.  The fold pays it beside the analysis rather than
instead of it - the same program without `constexpr` is 0.33 s - and
`probe_expression`'s own full-expression frame is what keeps the temporaries the
probe registers out of the enclosing one.  What it demands is what the fold
demands anyway: the emitted LowIR for an aggregate whose clause holds a call is
the reference's own, definition for definition.

**The reserved declaration is made once and written where something names it.**
`callee_candidates` declares the branch hint where its lookup found nothing,
which is a write on a refusal path - `declare_function` makes the one
declaration the expression layer's own lookup makes, a program that only folds a
call of it emits no `declare function` line at all, and a program that names it
without calling it emits exactly the reference's, `effects=readnone` included,
in one unit and across two.

**8.5.1p6, p7, p15 and 8.3.4p3 are each still answered inside the walk.**  A
list one clause too long is refused at every one of the four places, an array of
unknown bound takes the clauses it was given, a union takes its first member
alone and a class's tail is value-initialized - and 8.5.1p11's elision two and
three levels down, an elided member of union type and a mem-initializer whose
braces list-initialize an aggregate all fold, where `pa21/cppgm++-ref` refuses
all three outright and g++ agrees with this build.

**Every course fixture is the reference's own output.**  All thirty-seven were
regenerated from `pa21/cppgm++-ref` and compare byte-identically, exit status
included, the thirty-three that predate this review unchanged.

**Valgrind is clean** over the four new fixtures and 124 probe programs,
including every finding's shapes and the scaling programs above.

### Recorded, not landed

**A mem-initializer written with parentheses reads no string literal.**
`struct A { char s[4]; A() : s("ab") {} };` is `an expression has no conversion
to the type it initialises`, where g++ accepts it and `pa21/cppgm++-ref` refuses
it too (`array member initializer requires braced-init-list`) - so no `.ref` can
pin the acceptance.  It is 8.5p16's arm for a member written with an
expression-list, one door short of finding 2's.

**A union built by a constexpr constructor is not folded.**  `union U { int a;
double b; constexpr U(double v) : b(v) {} }; constexpr U u(1.5);` leaves `u.b`
`an object it holds no value of`, and the image is `zero 8` with a startup body
calling the constructor where both oracles write `f64 1.5` - the walk in
`object_from_constructor` reads every member in turn and a union's other members
have no mem-initializer to read.  It is the same owner as the plan's gap I'''':
what a constexpr constructor settles is not what the image is written from.

**A value-initialized tail is written as one span.**  `int t[3] = {7};` lays out
`i32 7, zero 8` where the reference writes two `i32 0`, and `char s[4] = "ab";`
the same one byte over.  It is not the fold's and not this checkpoint's: the
plain declaration writes it in every dialect, and the same array as a *member*
writes its elements out one at a time.

**The reference elides 8.3.5p10 for the hint's operand.**  Where the operand is
not already `long`, `pa21/cppgm++-ref` writes `binary sub i64` over a `u32`
operand and `return i64` of an `i32` value; this build converts, which is what
g++'s answer for the value needs, so the two differ for every argument of
another width.  The new fixture is written over `long` operands for that reason,
and finding 4 is the one place the difference was a program this build got wrong
rather than a spelling.

**`noexcept(__builtin_expect(1, 0))` is `true` here and in g++ and `false` in
the reference.**  15.4p14 leaves an implementation-provided function
non-throwing and `reserved_function` writes that on the declaration; the
reference reads the call as it reads any other.

**`constexpr A a = { P{1, 2}, 3 };` folds to data here and to a startup body in
the reference.**  5.2.3p3's `P{1,2}` standing as a clause is an aggregate this
build lays out in the image; the reference emits the member constructor and
calls it before `main`.  g++ folds it, so no defect is recorded, but the two
shapes are not interchangeable in a fixture.

## Changes

- **`sema_overload.cpp` - `cast_conversion`**, **`sema_cast.cpp` -
  `cast_to_reference`**: 5.2.9p4's operand with no value asked at the two exits
  both spellings of a cast come through, and `cast_expression`'s own copy
  removed - so 5.2.3p1's functional notation is refused where 5.2.9's is.
- **`sema_string_init.cpp` / `.h` - `units_into`**, **`sema_init_list.cpp` -
  `list_initialize_into`**: 8.5.2p1's code units written into the list the
  caller already opened, so `{"ab"}` and `"ab"` leave one list shape at every
  place a list stands.
- **`sema_constexpr_object.cpp` - `object_of` and `subobject_value`**: 8.5.1p7's
  value-initialized tail is no part of a union, so the list holds the member
  9.5p1 leaves holding a value and a read of any other is refused where it is
  read.
- **`lowir_lower_body.cpp` - `bound_address`**, with its two other callers and
  its declaration: 8.5.3p5's temporary is an object of the *referenced* type, so
  the storage is laid out at that type and the operand converted into it.

## Performance Evidence

Best of three per shape, measured on this review's build; the shapes
`pa21/cppgm++-ref` refuses outright are marked.

| shape | this build | `pa21/cppgm++-ref` |
| --- | --- | --- |
| 10 / 40 / 80 levels of 8.5.1p11 elision over a 1600-term first clause | **0.09 / 0.33 / 0.65 s** at 26 / 78 / 148 MB | refuses the shape |
| the same 80 levels with no `constexpr` on the object | **0.33 s** at 78 MB | refuses |
| 3200 members of aggregate class type, each taking two clauses | **0.08 s** at 22 MB | 2.50 s at 46 MB |
| 3200 elements of an array of class type, braces written | **0.05 s** at 14 MB | 0.69 s at 32 MB |
| 400 / 1600 / 6400 mem-initializers holding a 60-unit literal | **0.04 / 0.19 / 0.89 s** at 19 / 61 / 226 MB | 0.65 s at 1600 |
| 400 / 1600 / 6400 declarations of `char[61]` written `{"..."}` | **0.04 / 0.17 / 0.70 s** at 17 / 49 / 181 MB | 1.02 s at 1600 |
| 400 / 1600 / 6400 casts to cv `void` | **0.00 / 0.02 / 0.10 s** at 7 / 11 / 28 MB | - |
| the same count in 5.2.3p1's functional notation | **0.01 / 0.04 / 0.17 s** at 8 / 15 / 44 MB | - |
| 400 / 1600 / 6400 calls of the branch hint in one body | **0.01 / 0.07 / 0.27 s** at 9 / 21 / 68 MB | 0.77 s at 1600 |
| 400 / 1600 / 6400 constant unions read back by a `static_assert` | **0.01 / 0.05 / 0.24 s** at 9 / 16 / 47 MB | 0.73 s at 1600 |

Every row is linear in its count.  The nesting rows are the one product the
checkpoint's design predicted - the depth times the size of the clause standing
at the cursor, which `elides_its_braces` reads once per level - and the pair
below them is what that costs the fold beside the analysis: a second reading of
that one clause per level and nothing per subobject.  The union row is one
comparison per object and the cast rows one type test per cast; neither adds a
scan.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **162 / 166**, from
  158 / 162: the same 4 failures the turn started with, name for name, and the
  four course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Four course fixtures added, an oracle agreeing on each:
  `300-the-object-a-reference-bound-to-a-hint-names` and
  `300-the-code-units-braces-around-a-literal-hold` are byte-identical to
  `pa21/cppgm++-ref`'s LowIR, and
  `500-the-cast-a-functional-notation-writes-bad` and
  `500-the-member-a-union-does-not-hold-bad` are refused by the reference, by
  g++ and by this build.  All thirty-seven course fixtures pass, and every one -
  the thirty-three that predate this review included - was regenerated from the
  reference and compared unchanged.
- 124 probe programs over the four places a list stands crossed with an array,
  an aggregate, a union, a string literal, an empty list and one clause too
  many; the branch hint at an argument, a reference, a default-argument, a
  template argument, an unevaluated operand, through a pointer and across two
  units; and the `void` operand at every place 5.2.9, 5.2.3 and 5.4 reach a cast
  - swept through `pa21/cppgm++-ref` and g++ with each disagreement judged
  rather than copied, and every LowIR difference re-judged through the suite's
  own relaxed comparison rather than by hand.
- `valgrind -q --error-exitcode=99` over every one of those and over the scaling
  programs above: **clean**.
