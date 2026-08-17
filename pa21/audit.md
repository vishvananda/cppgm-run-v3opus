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

## Current Checkpoint Review

Checkpoint A is where one *name* stopped being one question.  3.2p2 says that an
lvalue-to-rvalue conversion applied to a static data member the class
initialized is no odr-use of the object 9.4.2p2's definition would give it, and
5.3.1p3's `&` is one - so `named_value`, the door every naming comes through,
asks which of the two the use is before it reads, and `addressed` is taken at
the operand in `unary_expression` rather than carried down the reading.  For a
member of pointer type the value is 5.19p2's address, which no number holds:
what the name is worth is the class's own brace-or-equal-initializer read again
where the name stands, against the region 9.2p2 gave it, and the storage the
member would have had is named by nothing.  Beside it, 14.6p8 one layer up: a
reading that stood a value in for something an argument list settles has arrived
at nothing, so 8.3.4p1's bound, 7.2p1's enumerator and 7.1.5p9's declaration
each carry that answer instead of the arm the stand-in chose.

Both rules are right and both hold where the checkpoint wrote them.  The pointer
member reads as `"xy"`'s own literal object, `&counter`'s declaration, `&step`'s
function, `&numbers[2]`'s element and 4.10p1's null pointer, at the qualified
name, at the unqualified one inside a member function and at one after an object
expression alike; `&holder::text` and `&text` name the storage and the unit
declares it; a class template's member is its specialization's; and the fold
reads the same member through `entity_constant`, so `holder::text[1]`,
`*holder::text` and `holder::third - 1` are constants where the reference itself
answers `static_assert unevaluated`.  A read costs one reading of the initializer
per use and no re-fold of the declaration: 1000 / 4000 / 16000 reads of a
`const char *` member are 0.03 / 0.11 / 0.44 s against the reference's 0.63 /
0.95 / 2.18 s, and of a `&numbers[2]` member 0.03 / 0.14 / 0.57 s against 0.66 /
1.07 / 2.70 s.  The pattern half holds too: the stand-in is one counter compare
beside a walk the reading already made, and a variadic class folding
`first_set(flags, flags + sizeof...(T))` over a pack of 100 / 400 / 1600 is
0.00 / 0.00 / 0.01 s at 7 / 8 / 12 MB, where the reference times out at 300 s.

What the review found is that each of the two sentences was written at one exit
of its own family, and that the third - 3.2p2 itself - was asked of one dialect.

### Findings

**1. 7.2p1's successor of an enumerator a value was stood in for.**  The
checkpoint asked 14.6p8 of the enumerator that *wrote* a constant-expression;
7.2p1 gives the one that writes none the value of the one before it plus one,
which is the same answer one enumerator further along:

```cpp
template<class T>
struct sized
{
  enum { first = sizeof(T), second };
  typedef char check[second == 5 ? 1 : -1];   // "an array bound is negative"
};
typedef sized<int>::check one;
```

`first` stands in at 1, `second` is marked a constant holding 2, and the bound
takes the arm that stand-in chose - so a program `pa21/cppgm++-ref` and g++ both
translate is **refused**, and refused with the one diagnostic that says nothing
about a template.  Written `second = first + 1` it was already right, because
that spelling reads `first` through `entity_constant`, which stands a value in
for it.  The answer now travels the way 7.2p1 writes it: an enumerator with no
constant-expression carries what this reading knew of its predecessor, one that
writes its own is settled by it again, and outside a template nothing stands in
at all.

**2. 4.2p1 asked at one of the operand positions a name of array type stands
at.**  The checkpoint's `named_array` is a lookup of the spelling made at the
subscript's left operand, so the sentence that makes `&numbers[2]` an address
constant reached no other operator:

```cpp
int numbers[4] = {1, 2, 3, 4};
int other[4] = {5, 6, 7, 8};
constexpr int* stepped = numbers + 1;            // "numbers is not a constant expression"
constexpr int* mirrored = 1 + numbers;           // the same
static_assert(numbers == numbers, "");           // the same
static_assert(numbers != other, "");             // the same
static_assert(!numbers == false, "");            // the same
static_assert((numbers ? 1 : 0) == 1, "");       // the same
static_assert(*numbers == 1 || true, "");        // the same
```

g++ folds all seven and `pa21/cppgm++-ref` folds four of them, refusing the
three its own fold has no pointer difference or ordering for.  The reading is one
door: an array has no value for any operand to wait on, so what a name of array
type is worth is *which object it is* - `held_at` on the address the declaration
memoises, exactly as `static int n;` already answered - and each reader applies
4.2p1 as it was already written to.  `decayed_operand` stood in front of 5.7's
and 5.9's operands before this checkpoint; it now stands in front of 4.12p1's
contextual `bool`, which is the conversion `!`, `&&`, `||` and a condition each
ask for, and in front of 5.3.1p1's `*`.  `named_array` and the extra lookup it
spent per subscript are gone with it, and `array_object` asks the declaration
nothing of its own.  What stays refused is what 5.19 refuses: `numbers[0]` and
`*numbers` read a value the array holds no constant of, and `loaded` refuses
them with the declaration's own `covered_constant`, which is the hard error both
oracles give.  One shape the fold newly reaches had no image to go with it -
`global_address` read 5.7p5's pointer operand on the left alone, so
`constexpr int* p = 1 + numbers;` took `zero` and a startup body where the
reference writes `addr @numbers + 4` - and `+` is commutative, so the walk now
tries either side and `-` still takes the pointer on the left.

**3. 3.2p2's substitution gated on the dialect.**  The pointer half of the
reading was written `declared_constant && lowering()`; the arithmetic half one
`if` above it was never gated, and 3.2p2 is one sentence:

```
struct holder { static constexpr int* counted = &counter; };   // --emit-semantics
  ref  : unary-expression prvalue pointer to int OP_AMP:&
  ours : id-expression lvalue const pointer to int holder::counted
```

`pa12/cppgm++-ref` writes the initializer read at the name in its own dialect,
which is the answer PA21 gives, and the gate is what parted them.  Removed: the
PA12 dump is byte-identical to `pa12/cppgm++-ref` on that program now, and
`--emit-types`, which walks no expression to that depth, was identical either
way.  A pattern being checked is read in the `Types` dialect by
`DialectReading`, so nothing here fires while a definition is checked and no
declaration leaks out of one - the instantiated body writes `addr @__strlit__1`
in both builds.

### What the review confirmed rather than found

**The stand-in is carried by every place that turns it into a decision.**
7p4's `static_assert`, 9.6p1's bit-field width and a non-dependent bound whose
evaluation happened to stand a value in are each already answered where the
pattern stands and again where the arguments settle it, and `static_assert
(sizeof(T) == 4, "")` inside a pattern is accepted by this build and by both
oracles.  A stood-in value of class type - `bits = 1` under a type whose bits
are a list identifier, which is what checkpoint F's segfault was - reaches no
reader, because `fold_declared_object` and `array_bound` each ask the counter
before the value is used; probed through a member, a pointer to it and a call
taking it by value, all clean under valgrind.

**The address half is linear in every direction.**  1000 / 4000 / 16000
declarations each folding `numbers + k` are 0.01 / 0.04 / 0.19 s at 8.6 / 15.5 /
43.9 MB (ref 0.58 / 0.75 / 2.99 s at 18 / 33 / 96 MB).  A constexpr `for` loop
walking a pointer over 1000 / 4000 / 16000 elements is 0.00 / 0.02 / 0.07 s and
the same loop subscripting a constexpr array 0.00 / 0.02 / 0.08 s, both at a
flat 6-10 MB.  A subscript nested 8 / 12 / 16 / 20 deep, read as an address and
as a value, is 0.00 s at 5.8-6.2 MB throughout - linear in depth, not 2^depth,
which is what removing the pre-lookup had to be checked against.

**Every course fixture is the reference's own output.**  All twenty-five were
regenerated from `pa21/cppgm++-ref` and compare byte-identically, exit status
included, the twenty-three that predate this review unchanged.

**Valgrind is clean** over all twenty-five course fixtures, every probe of the
three findings and every scaling program above.

### Recorded, not landed

**`&` written after an object expression asks 5.3.1p3 of nothing.**
`addressed` is taken at `unary_expression` for an id-expression, so `&one.text`
and `&this->text` reach `named_value` through `member_expression` with the
question unasked and read the member's *value*, which is a prvalue `&` then
refuses.  `pa21/cppgm++-ref` refuses both with `address-of requires lvalue` and
g++ accepts both; the two oracles part, no fixture reaches it, and this build
follows the reference.

**4.3p1's function name is not the array's sentence twice.**  `step == step` is
refused here and folded by both oracles, and it cannot be closed the way finding
2 closed the array: 8.3.2p1's binding of an `int (&)(int)` parameter reads the
same door, and decaying there is what
`100-constexpr-function-pointer-reference-call` shows breaking.  It wants the
operand reading to ask, which is 5.7's door and not the name's.

**The reference folds no pointer difference and no pointer ordering.**
`element - numbers`, `numbers < element` and `holder::last - other` are
`static_assert unevaluated` in `pa21/cppgm++-ref` and folded here and by g++, so
the fixture pins the equality shapes and 5.7p6 is pinned by g++ alone.

**A pointer whose second fold over the dump stops takes no image.**  Checkpoint
I's rule is that where the walk of the dump lines cannot re-fold the initializer
the analysis's own answer stands - and `SemaEntity::value`/`::real` are numbers,
so a *pointer* has nothing there to stand.  `constexpr int* p = true ? &one :
&two;` and `constexpr int* p = <another constexpr pointer>;` each take `zero`
and a startup body where the reference writes `addr @one`, and a null pointer
read through a name is written `= 0` where 4.10p1's own image is `= zero`.  It
is `global_address`'s question and reaches no reading checkpoint A owns.

**PA12 spells a static data member differently.**  `pa12/cppgm++-ref` writes
`text` where this build writes `holder::text`, and drops the `const` a
`constexpr` member carries, for an array, a class and an enumeration member
alike - a pointer is the one kind it agrees on, which is what finding 3 closed.
No PA12 fixture writes one and the 2399 through pa20 are green either way.

## Changes

- **`sema_enum.cpp` - `enumerators`**: 7.2p1's successor carries what this
  reading knew of its predecessor, so an enumerator with no constant-expression
  after one 14.6p8 stood a value in for holds no constant either, and one that
  writes its own is settled by it again.
- **`sema_constexpr.cpp` - `entity_constant`**: 4.2p1 at the one door a name
  comes through - a name of array type with no constant of its own is worth
  which object it is, because there is no value for an operand to wait on.
- **`sema_constexpr_statement.cpp` - `truth`, `sema_constexpr.cpp` -
  `unary_constant`**: `decayed_operand` in front of 4.12p1's contextual `bool`
  and 5.3.1p1's `*`, which is where the conversion 5.7's and 5.9's operands
  already applied was missing.
- **`sema_address.cpp` - `array_object`**: the `named_array` pre-lookup and its
  spelling test are gone, because the value reading no longer refuses the name.
- **`lowir_image.cpp` - `global_address`**: 5.7p5's operands are a pointer and
  an integer in either order, so `1 + numbers` owes the image `numbers + 1`
  already had.
- **`sema_expression.cpp` - `named_value`**: 3.2p2 is one sentence in every
  dialect, so the pointer half of the substitution is no longer `lowering()`'s.

## Performance Evidence

Best of three per shape, alternating between the binaries.  `d9d9a8af` is the
checkpoint as it landed.

| shape | this build | `pa21/cppgm++-ref` |
| --- | --- | --- |
| 500 / 2000 / 8000 declarations each folding `numbers + k` | **0.01 / 0.04 / 0.19 s** at 8.6 / 15.5 / 43.9 MB | 0.58 / 0.75 / 2.99 s at 18 / 33 / 96 MB |
| 1000 / 4000 / 16000 reads of a `const char *` member | **0.03 / 0.11 / 0.44 s** at 13 / 33 / 116 MB | 0.63 / 0.95 / 2.18 s at 23 / 51 / 159 MB |
| 1000 / 4000 / 16000 reads of a `&numbers[2]` member | **0.03 / 0.14 / 0.57 s** at 14 / 37 / 130 MB | 0.66 / 1.07 / 2.70 s at 27 / 63 / 206 MB |
| 1000 / 4000 / 16000 reads of an `int` member | **0.01 / 0.06 / 0.24 s** at 10 / 22 / 67 MB | - |
| a constexpr `for` walking a pointer over 1000 / 4000 / 16000 elements | **0.00 / 0.02 / 0.07 s** at 6.3 / 7.1 / 10.1 MB | - |
| the same loop subscripting a constexpr array | **0.00 / 0.02 / 0.08 s** at 6.4 / 7.1 / 10.1 MB | - |
| a subscript nested 8 / 12 / 16 / 20 deep, as an address and as a value | **0.00 s** at 5.8-6.2 MB throughout | - |
| a pack of 100 / 400 / 1600 folded through `first_set(flags, flags + sizeof...(T))` | **0.00 / 0.00 / 0.01 s** at 7.1 / 8.2 / 12.5 MB | 0.70 / 1.60 s, **times out at 300 s** at 1600 |
| 500 / 2000 / 8000 enumerators after one a value was stood in for | **0.00 / 0.01 / 0.03 s** at 6.8 / 9.1 / 18.0 MB | - |

Rows one, two and three are the checkpoint's own paths re-measured after the
lookup finding 2 removed; row one is the family the checkpoint refused
outright.  The pack row is the plan's carried-forward `0.10 s throughout`
re-measured without a per-run process spawn in the harness, which is what that
figure was.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **145 / 154**, from
  143 / 152: the same 9 failures the turn started with, name for name, and the
  two course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Two course fixtures added, each with both oracles agreeing:
  `300-the-enumerator-a-stood-in-value-comes-before` and
  `300-the-array-an-operator-is-written-on` are byte-identical to
  `pa21/cppgm++-ref`'s LowIR and accepted by g++.  All twenty-five course
  fixtures pass, and every one of them - the twenty-three that predate this
  review included - was regenerated from the reference and compared unchanged.
- Sixty-one probe programs over the declared types a static data member may
  have, the doors `&` and a read each reach a name through, 4.2p1 at every
  operand position, 14.6p8's stand-in at the places that decide something, and
  the two earlier dialects, swept through `pa21/cppgm++-ref`, `pa12/cppgm++-ref`
  and g++ with each disagreement judged rather than copied.
- `valgrind -q --error-exitcode=9` over all twenty-five course fixtures, every
  finding's probes and every scaling program: **clean**.
