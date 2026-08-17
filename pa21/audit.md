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
| **stage** | `6d4b25ce` | **8 / 8 + 11 recorded** | **the clauses one element takes out of the list, and the four facts written at one exit of their own family.**  8.5.1p11's elision was asked of a class *member* and not of an array *element* at a declaration, so `int g[2][3] = {1,2,3,4,5,6};` was **refused**; 8.5.4p1's list was read at four of 8.5's places and not at 6.6.3p2's return or 8.3.6p1's default-argument; 9.5p1's one member was asked of the walk that builds a union from a list and not of the one that builds it from a constructor; and 3.5p3's linkage was asked of the declaration that spelled the `const` rather than of the variable, so `extern const int k; const int k = 5;` was `_ZL1k` where 3.5p3, g++ and the reference all say `_Z1k`.  Beside them three the sweeps found that no row had named: 8.3.5p10 declares `this` in a member function's own region, so a fold that had bound no object read through the *analysis's* declaration of the name and `constexpr A() : n(sq(3)) {}` was **a constant expression reads through a null pointer** for every constexpr constructor whose mem-initializer holds a call; 12.6.2p2's member of class type had no arm in the image walk at all; and 8.3.2p1's reference read 5.19p3's `const` off a type that carries none, so `constexpr const int &r = one;` was no constant expression |


## Stage Audit

The final audit read the assignment README, the stage commits and the changed
source, reconstructed the six ownership lines from `dev/src` rather than from
the checkpoint conclusions, and traced eight facts end to end: an
initializer-clause, a subobject index, an address identifier, a linkage, the
object `this` names, an image item, a reference binding and a deduced bound.
Each was swept across the places that ask it, against three oracles —
`pa21/cppgm++-ref`, `g++ -std=c++11 -pedantic-errors`, and the emitted program
run through `lowir2cy86`/`cy86` — with every disagreement judged rather than
copied.

The stage's standing failure shape held again: four of the eight facts were
written at one exit of their own family, and the sibling exits were each a
program both other oracles translate.

### Findings

**1. 8.5.1p11's elision was asked of a class member and not of an array
element.** `array_from_clauses` — the walk every array *declaration*, every
array argument and every array member comes through — took one clause per
element with no 8.5.1p11 question at all, and `list_initialize_into` refused
before it ran wherever the list was longer than the bound. So `int g[2][3] =
{1,2,3,4,5,6};`, `P a[2] = {1,2,3,4};` and `A() : e{1,2,3,4}` over `int e[2][2]`
were each **`an array initializer has more clauses than the array has
elements`** — the standard's own 8.5.1p11 example among them — while a class
whose *member* is that array was accepted one line away, because
`aggregate_subobject` had asked the question all along. The elision is now one
question per element, asked of an element of array type and of an elidable
aggregate alike, and `elided_element` writes the one child every other element
writes. `pa21/cppgm++-ref` refuses all three itself (`too many array initializer
elements`), so g++ is the oracle and every accepted shape was re-run.

**2. 8.3.4p3's bound counted clauses.** Finding 1 makes the length of the list
no longer the number of elements, so `int g[][3] = {1,2,3,4,5,6}` became a
**six-row** array with a wrong `sizeof` and `S ss[] = { 1, "asdf", 2, "asdf" };`
— 8.5.1p11's own example — a four-element one. The bound is what the walk
arrives at: `deduced_array_bound` runs that same walk once with nothing kept,
under a frame of its own so the temporaries go with the node, and only where
`clause_capacity` says an element can take more than one clause. Counting it a
second way was the alternative and is the shape this stage has been burned by
before — two implementations of one rule.

**3. 8.5.4p1's list was read at four of 8.5's places and not at two.** A
braced-init-list is no expression, so what one comes to is the initialization of
the place it fills — and `evaluate` has no node for one. `return {4,5,6};` from
a constexpr body and `int f(A a = {8,9})` were each **`a constant expression
holds a construct PA11 does not evaluate`**, where both oracles fold them.
`operand_constant` now takes the place where the reader knows it: 6.6.3p2's
return type, carried on the frame the call opened, and 8.3.6p1's parameter.
13.3.3.1.5 ranks a list against a parameter and the fold reads its arguments
before 13.3 has chosen, so `f({4,5})` and `H<f({7,8})>` are recorded rather than
landed — the reference refuses both itself.

**4. 9.5p1's one member was asked of one of the two walks that build a union.**
`object_from_constructor` reads every member of the class in turn, and a union's
other members have no mem-initializer to read — 9.5p1 says they may not have
one. So `union U { int a; double b; constexpr U(double v) : b(v) {} };
constexpr U u(1.5);` was **refused** for the member the constructor did not
name, and laid out `zero 8` with a startup body where both oracles write `f64
1.5`. The entry a member whose lifetime never began stands at now holds nothing,
the list stops at the last one an initialization reached — which is the shape
`object_of` already gave a union an aggregate initializer wrote — and one
reading of an entry past the end answers both. Beside it `bind_subobjects`
stopped at the first entry the list did not hold, which for a union is the
*first* member: `constexpr double get() const { return b; }` on such an object
was refused because `b` was never bound.

**5. 8.3.5p10's own declaration of `this` was read as the fold's binding.** A
member function's declarator region declares `this` as the parameter 9.3.1p3
makes it, so `called_name`'s lookup of the *name* found that declaration
wherever the fold had bound no object — and read through it. `struct A { int n;
constexpr A() : n(sq(3)) {} }; constexpr A a;` was **`a constant expression
reads through a null pointer`** for *every* constexpr constructor whose
mem-initializer holds a call, one line from the same call written on a
declaration's initializer. `folded_this` tells the two apart by which object the
binding names, because the fold writes one and the declaration names none.

**6. 12.6.2p2's member of class type had no arm in the image walk.**
`global_constructed` refused a mem-initializer of a member of class or array
type outright, so `struct A { P p; constexpr A() : p{1,2} {} }; constexpr A a;`
laid out `zero 8` with a startup body where both oracles write `i32 1, i32 2`.
The clauses are `global_subobjects` at the byte 9.2p13 laid the member out at,
and a mem-initializer that wrote a constructor call instead is
`global_constructed` one level down — which is where 12.6.2p2's own
`constructor-action` node stands, keyed by the object the call was written on. A
member of *array* type stays a startup body: the reference draws the line there
and `300-the-array-a-mem-initializer-writes-the-elements-of` pins it.

**7. 5.19p2's answer had nothing the image could stand.** A constant of pointer
type holds the identifier its object was interned under and never a number, so
the fallback checkpoint I wrote for a *value* — where the second walk over the
dump lines stops, the analysis's own answer stands — had no reading for a
pointer. `constexpr const int *p = true ? &one : &two;` took `zero` and a
startup body where the reference and g++ both write `addr @one`. The
`AddressTable` now travels to the lowering beside the types and the source
places, and `folded_address` reads the entry back. Two lines bound it, and both
are lines this file already draws: 5.2.2p1 makes an initializer holding a *call*
work the program runs, which is what leaves `by_default`, `from_return` and
`through_conversion` starting as zero here as they do in the reference; and 5.19
answers *which object* and not which byte of it, so a path down to a subobject
is answered where the dump lines spell the step — `numbers + 1` is one this walk
reads and `&numbers[2]` is one it does not, which is what
`300-the-array-an-operator-is-written-on` pins.

**8. 8.3.2p1's reference read 5.19p3's `const` off a type that carries none.**
`fold_declared_object` asks whether the declared type is const-qualified, and a
reference type never is — so a *declaration* of reference type folded nothing
and made no binding, while a reference *place* a call filled had carried one all
along. `constexpr int one = 7; constexpr const int &r = one; static_assert(r ==
7, "");` was **`r is not a constant expression`**, and the same declaration
inside a constexpr body was refused as a conversion no arithmetic types reach.
`bind_declared_reference` reads the initializer as 8.5's operand — there need be
no value — and records the object on the declaration, which is where
`entity_constant` was already reading it from.

**9. 3.5p3 was asked of the declaration and not of the variable.** The clause
gives a `const` object internal linkage only where it is *neither* explicitly
declared `extern` *nor previously declared to have external linkage*, and only
where the type is non-volatile. Both halves were missing: `extern const int k;
const int k = 5;` was `binding=internal` and `_ZL1k` here where 3.5p3, g++ and
the reference all say `_Z1k` — a definition no other unit could link against —
and `const volatile int k = 5;` was internal where g++ and the clause make it
external. `decl_parser_object.cpp` had the volatile half right, which is the
same rule written twice and answered differently.

### What the review confirmed rather than found

- 8.5.1p2's walk is one walk with one cursor: a class two members wide and 8 /
  10 / 12 / 14 deep costs **the same to the byte** written fully braced and
  written fully elided, and neither is 2^depth in anything but the type.
- The interned list and the address path index the same order at all four
  readers — a member access, a base conversion, a `&`, an image — checked over a
  base at depth 1 / 5 / 20, a member of array type, an array of class type and a
  union.
- 5.19's `covered` reaches every reader: a declaration whose fold ran out takes
  3.6.2p2's dynamic initialization and the *name* that reaches it runs out too,
  in all three dialects.
- 13.3 is asked once per call and the fold ranks nothing; `--emit-semantics`,
  `--emit-types` and `--emit-lowir` answer the swept shapes alike.
- `lowir_image.cpp` writes no instruction and `lowir_lower.cpp` writes no item.
- No fixture gate, no environment switch, no embedded output, no interpreter or
  trampoline substitute, and no phase skipped: the only `getenv` in the tree is
  the test runner's, and the two comments that name a fixture explain why the
  implementation matches the references rather than gating on one.

### Recorded, not landed

- **`f({4,5})` and `H<f({7,8})>` inside a constant expression.** 13.3.3.1.5
  ranks a list against a parameter; the fold reads its arguments to values
  first. The reference refuses both; g++ folds them.
- **`int t[3] = {7};` lays out `i32 7, zero 8`** where the reference writes two
  `i32 0`; `char s[4] = "ab";` the same one byte over.
- **1.4p8's hint keeps its operand's width** in the reference and is converted
  here, which is what g++'s value needs.
- **An array member of an object of class type decays where the reference
  indexes** — one extra `unary decay` per element step inside a member of
  array-of-array type.
- **The implicit default constructor's definition** (`struct held { int inside =
  4; }; constexpr held h;`): the reference emits `@held__held` and this build
  does not — and **g++ emits none either**, so the plan's row is corrected: it
  is the reference's and not this build's.
- **An explicit specialization's local static** is one unit's here and weak in
  the reference; 14.7.3p6 and g++ agree with this build.
- **An element of an array of class type** is built by 8.5.1's own constructor
  here and stored in place by the reference; the image is identical either way.
- **12.2p5's lifetime-extended temporary at namespace scope** is given a *slot
  of the startup body* and its address stored into the reference — byte-identical
  to the reference and a dangling pointer in both, where g++ gives it static
  storage.
- **A reference declared `constexpr` has external linkage here** and internal in
  the reference; a reference type carries no cv-qualification for 3.5p3, and g++
  agrees with this build.
- **The reference emits a duplicate `alias object _ZN…C2E…`** beside a base
  entry it also defines, so two definitions of one object symbol.
- **`lowir2cy86` truncates no floating conversion**: `double d = 1.5; (int)d`
  runs as `2` with the LowIR byte-identical to the reference, so run evidence
  over floating values is that backend's and not this stage's.

## Changes

- **`sema_init_list.cpp` — `array_from_clauses`, `elided_element`,
  `list_initialize_into`**: 8.5.1p11's question asked per element, one child per
  element whether the braces were written or left out, and 8.5.1p6's refusal
  moved behind the walk that knows how many elements the clauses filled.
- **`sema_init_list.cpp` — `deduced_array_bound`**, called from
  `sema_analyzer.cpp`'s 8.3.4p3 site: the bound is that walk's own count, taken
  with nothing kept and only where an element can take more than one clause.
- **`sema_address.cpp` — `operand_constant`**, with `sema_constexpr_statement.cpp`
  and `sema_constexpr.cpp`'s default-argument site and `ConstexprFrame::returns`:
  8.5.4p1's list read as the initialization of the place, where the reader knows
  the place.
- **`sema_constexpr_object.cpp` — `object_from_constructor` and
  `subobject_value`**, with `sema_constexpr.cpp`'s `bind_subobjects`: 9.5p1's one
  member, and an entry that holds nothing read as such wherever it stands.
- **`sema_constexpr.cpp` — `folded_this`**, asked by `called_name` and
  `this_constant`: the fold's binding told from 8.3.5p10's declaration of the
  same name.
- **`lowir_image.cpp` — `global_constructed`, `constructed_member`**: 12.6.2p2's
  member of class type walked at 9.2p13's byte, as clauses or as a constructor
  call one level down.
- **`lowir_image.cpp` — `folded_address`**, with the `AddressTable` threaded
  through `lowir_emit.cpp`, `LowirProgramBuilder::add_unit` and
  `LowirUnitLowering`: 5.19p2's answer standing where the second walk of the
  lines spells no address.
- **`sema_constexpr.cpp` — `bind_declared_reference`**, called from
  `fold_declared_object` and from the fold's own declaration statement: 8.3.2p1's
  binding written by a declaration of reference type.
- **`sema_analyzer.cpp` — `record_storage`**: 3.5p3 asked of the variable, with
  its non-volatile clause.
- **`sema_using.cpp` (new)**: 7.3.3's using-declaration and 7.3.4's
  using-directive moved out of `sema_analyzer.cpp`, which the audit's own lines
  had pushed past the file-size limit. Structural room rather than trimmed
  comments.

## Performance Evidence

Measured with `/usr/bin/time`, best of three, against a build of `6d4b25ce` in a
worktree (`make build`) so every number has a baseline rather than a memory.

| shape | this build | pre-audit build | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| `int g[n][2] = {…}` elided, n = 2000 / 8000 / 32000 | **0.02 / 0.07 / 0.28 s** at 9 / 20 / 64 MB | refused | refused |
| the identical array written `{{…},…}` | 0.01 / 0.06 / 0.26 s at 9 / 19 / 61 MB | 0.01 / 0.06 / 0.26 s | — |
| `P a[n] = {…}` over a two-member class, same n | **0.02 / 0.08 / 0.33 s** at 11 / 25 / 82 MB | refused | refused |
| `int g[][2] = {…}`, the deduced bound, same n | **0.03 / 0.10 / 0.42 s** at 12 / 29 / 98 MB | refused | refused |
| a class 2 wide and 8 / 10 / 12 / 14 deep, every level elided | **0.00 / 0.01 / 0.05 / 0.20 s** at 7.5 / 9.5 / 18 / 54 MB | refused | refused |
| the same nesting written fully braced | 0.00 / 0.01 / 0.05 / 0.20 s at 7.0 / 9.0 / 17 / 50 MB | 0.00 / 0.01 / 0.05 / 0.19 s | — |
| a member call on a constant *union*, 1e3 / 4e3 / 16e3 folds | **0.01 / 0.03 / 0.11 s** at a flat 6.8 MB | refused | — |
| the same loop over a `struct` | 0.01 / 0.03 / 0.12 s at a flat 6.6 MB | 0.01 / 0.03 / 0.12 s | — |
| 500 / 2000 / 8000 `constexpr` objects whose member is a class a mem-initializer fills | **0.02 / 0.10 / 0.43 s** at 12 / 27 / 89 MB | 0.02 / 0.10 / 0.45 s | — |
| 500 / 2000 / 8000 `constexpr` references read back | **0.01 / 0.05 / 0.23 s** at 9 / 18 / 55 MB | refused | — |
| 500 / 2000 / 8000 elided two-member class members of one aggregate | 0.01 / 0.04 / 0.18 s at 8.8 / 15 / 44 MB | 0.01 / 0.04 / 0.17 s | **20.23 s** at 8000 |
| 3200 members off one 6400-term place | 0.12 s at 39 MB | 0.11 s at 39 MB | 0.94 s |
| 500 / 2000 / 8000 distinct literal classes with a constexpr object | 1.77 s at 8000, 367 MB | 1.75 s at 8000, 367 MB | 9.31 s |
| the whole 169-file PA21 corpus | **0.70 s** | 0.70 s | — |

Every dimension is linear and every one has a baseline it matches to within
measurement. The one shape that costs more than the build before it is 8.3.4p3's
deduced bound — one extra walk of the same list, about 50%, and only for a
declaration that wrote no bound and whose element can take more than one clause;
the alternative was a second implementation of 8.5.1's walk. The nesting rows
are the audit's own gate on finding 1: an elided nesting costs what the braced
one costs, so the elision carries no term of its own. The 2^depth left at depth
20 is 8.5.1's dump of the aggregate — `n20 deep = {};` costs 2.19 s and 833 MB
*without* `constexpr` — and is PA15–PA20's description of the initialization.

## Validation

- `make test-report-through-pa21` — **pass**, 2568 / 2568, 21 / 21 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth; the
  new file keeps `sema_analyzer.cpp` under the limit structurally.
- 86 systematic probe programs swept against `pa21/cppgm++-ref`, g++ and the
  run: 20 over
  8.5.1p11's elision (array of arrays, array of aggregates, a deduced bound, a
  partly braced row, a string literal, a class holding an array, a mem-initializer,
  an argument, one clause too many); 10 over 9.5p1's union (a member at each
  position, a member call, an aggregate union, a read of the wrong member, a
  brace-or-equal-initializer, a member of class type); 10 over 12.6.2p2's member
  image (clauses, a constructor call, a base, an array member, a call in a
  clause, no `constexpr`); 10 over 5.19p2's pointer image (a conditional, a
  pointer from a pointer, `&arr[2]`, `arr + 3`, a literal, `nullptr`, a
  function, `&s.b`, a two-dimensional subscript, a reference); 8 over 8.3.2p1's
  reference declaration; 8 over 3.5p3's linkage; and 20 over the assignment's own
  cross-product — recursion, member calls, floating values, arrays, enumerators,
  a class template, a static data member, a loop, a base, a string literal, a
  `noexcept` operand, `sizeof`, a union, a local static, a default argument, a
  reference, a factory. Every disagreement judged against the standard and g++
  rather than copied.
- Nesting-depth sweep to 20 levels and multiplicity sweep to 32000 clauses on
  each new walk; both linear.
- `valgrind -q --error-exitcode=9` over all 112 probe programs — the 86 above
  and the 26 narrowing repros beside them — and over the scaling programs:
  **clean**, 0 errors.
- Course and spec fixtures unchanged and all 169 passing; no `.ref` regenerated,
  because no producer this audit changed writes a shape a fixture pins — the two
  that would have (`300-the-array-a-mem-initializer-writes-the-elements-of` and
  `300-the-array-an-operator-is-written-on`) are what drew the two lines in
  findings 6 and 7.
