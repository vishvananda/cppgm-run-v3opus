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

## Current Checkpoint Review

Checkpoint R is where 5.19p2's address became a constant of the same standing as
3.9.1p8's two kinds of value.  `sema_address.cpp` owns the whole of it:
`ConstantAddress` is a base - a declaration, 2.14.5p8's literal that no
declaration named, or 12.2p1's temporary interned by what it holds - and the
path of subobject indices down to the object designated, which indexes exactly
the interned list a constant of class or array type already holds; `AddressTable`
gives each one number, so `&x` written twice is one constant and 5.10p1's
equality is a comparison of identifiers.  `SemaConstant::object` is the same
fact one step earlier - 3.10p1's glvalue - and `::valued` says whether the
fields beside it say what that object holds.

That split is right, and it is the split this layer needed: an address and a
value never meet, so a reading that carried one in the other's field would read
the identifier of an object as a number the first time a place asked for one.
The readings it wires are right too and land byte-identically: `&n`, `*p`,
5.7p5's arithmetic with its one-past-the-end bound, 5.9 and 5.10's comparisons,
`->` read as 5.2.5p2's `(*E).`, 8.3.2p1's binding that makes `&value` inside a
body the *argument's* address, and 5.19p2's own requirement that a pointer a
declaration keeps designate an object with static storage duration - which is
what the checkpoint's two course fixtures pin from both sides.

What the review found is that the one door those readings come through was cut
at two walls of eleven, and that the walk behind it read every operand twice.
4.2p1 and 4.3p1 are conversions that read *no value* - which is the whole reason
`::valued` exists - so every place 8.5 fills from an initializer has to ask for
the object, and `operand_constant` was asked at two of them.

### Findings

**1. 4.2p1's decay was asked at two of the eleven places 8.5 fills a pointer
from.**  `operand_constant` stood at `initialized_value`'s `= expr` arm and at
`call_or_cast`'s argument loop.  Every other spelling of the same initialization
read the operand as a *value*, which `static char buf[3];` has none of:

```cpp
static char buf[3];
void fn();
constexpr char *paren(buf);                       // refused: "buf is not a constant expression"
constexpr char *braced{buf};                      // refused
struct holder { char *p; };
constexpr holder aggregate = { buf };             // refused
constexpr char *elements[1] = { buf };            // refused
struct held { char *p = buf; };                   // 12.6.2p8: refused
struct built { char *p; constexpr built(char *v) : p(v) {} };
constexpr built made(buf);                        // 12.6.2p2: refused
constexpr char *pick(char *p = buf) { return p; } // 8.3.6p1: refused
constexpr char *give() { return buf; }            // 6.6.3p2: refused
constexpr char *cast = static_cast<char *>(buf);  // 5.2.9p4: refused
struct S { void (*p)(); };
constexpr S fnptr = { fn };                       // 4.3p1: refused
```

Every one of the ten is a program `pa21/cppgm++-ref` and g++ both fold, and
every one is the same conversion the accepted `= buf` performs.  8.5's operand
is now one reading: `operand_constant` is what an initializer-clause comes to
wherever a place is filled from one - the clauses of `initialized_value`,
`clause_of` down an aggregate or an array, a mem-initializer, the
default-argument 8.3.6p1 stands where the argument is missing, the return
statement 6.6.3p2 hands back, and 5.2.9's cast - and which of the two the place
wanted stays `convert`'s answer, unchanged.

**2. The lvalue walk behind that door re-ran the value reading it had just been
refused by, once per level of nesting.**  `operand_constant` evaluates, and on a
refusal asks `designated` for the object; `designated`'s own last resort for a
shape it has no lvalue reading of is `analyzer_.evaluate(node, ctx)` - the
reading that has already refused.  So an operand that fails costs its subtree
twice, and a call written on a call is four walks:

| `f(f(...(nonconst)))` | 12 deep | 16 deep | 18 deep |
| --- | --- | --- | --- |
| `6d975910` | 0.05 s | 0.67 s | **2.67 s** |
| this build | 0.00 s | 0.00 s | 0.00 s |

`designated` now carries whether a value reading is still worth doing, and
`operand_constant` - its one caller that knows the answer is no - says so.  The
walk that succeeds is untouched: 64 nested calls that fold are 0.00 s in both.

**3. A pointer object with no constant value was read as 4.10p1's null pointer
value.**  `holds_address` asks the *type* alone, and `at_pointer_place` took its
answer for a value it holds, so a valueless constant handed the place its `bits`
- which is zero:

```cpp
static int *held;
constexpr int *identity(int *from) { return from; }
static_assert(identity(held) == 0, "");   // passed
```

Both oracles refuse the program; this build proved it.  4.10p1's null pointer
value is what a *null pointer constant* comes to and nothing else, so the place
now asks for a value as well as a type, and the refusal that is left carries
which object had none.  `500-a-pointer-with-no-constant-value-is-not-null-bad`
pins it against the reference's own refusal.

**4. 5.19p3's user-defined conversion was asked at every arithmetic place and at
no pointer place.**  `at_arithmetic_place` sends an operand of class type to
`converted`, which is 13.3.3.1.2's ranking and the one door a conversion
function is chosen through.  `at_pointer_place` had no such arm, so
`constexpr int *p = c;` over `struct C { constexpr operator int *() const; };`
was refused as "no address it holds" where both oracles fold it.  A converted
constant expression is one rule and not one per destination, so the pointer
place asks the same door - and 5.2.9's cast passes 12.3.2p2's `explicit`
through it exactly as the arithmetic cast already did.

**5. The refusal a valueless operand makes claimed 5.19's answer about the
program, which is checkpoint V's rule undone one name further along.**  V settled
that a fold which *ran out* is not a program 7.1.5p9 refuses, and carried the
answer on `SemaEntity::covered_constant`.  R's `held_at` hands back a valueless
constant for a declaration whose own fold ran out - and the refusal that then
reads it was `covered` by default:

```cpp
struct B { int y; };  struct D : B { int x; };    // group B: no class with a base folds
constexpr D d = {};
constexpr int f(int a) { return a; }
constexpr int r = f(d.x);   // refused here, accepted at 4853971d, folded by both oracles
```

The valueless constant carries the object it designates, so the refusal now asks
that object: `covered_object` is `SemaEntity::covered_constant` read through
`AddressTable`, and the three refusals a valueless operand reaches -
`at_arithmetic_place`, `address_operation`, `at_pointer_place` - each ask it.
A declaration whose fold this build ran out on is 3.6.2p2's dynamic
initialization again, whichever name reaches it.

### What the review confirmed rather than found

**The address readings hold byte-for-byte.**  Eleven places of 8.5 crossed with
an array name, a function name, an address and a user-defined conversion lower
to LowIR identical to `pa21/cppgm++-ref`'s after the comparison's own
canonicalization, which is `300-the-object-a-place-of-pointer-type-is-filled-from`.
So do 5.7p5's arithmetic and 5.9/5.10's comparisons, the two checkpoint fixtures,
and a two-unit invocation where both units take addresses and both intern the
same `@__strlit__1`.

**An address is one number however many namings reach it.**  `SemaEntity::address`
memoises a declaration's own address, so a name read n times interns once.  500 /
2000 / 8000 declarations each keeping the address of an object are 0.01 / 0.07 /
0.29 s at 10 / 22 / 70 MB against the reference's 9.92 s at 111 MB for the last;
500 / 2000 / 8000 folds each binding a reference place to a different object are
0.02 / 0.11 / 0.46 s at 12 / 29 / 99 MB against 6.61 s at 117 MB.  Both are
within measurement noise of `6d975910`.

**Pointer arithmetic inside a body interns nothing that accumulates.**  A
`while` walking a four-element array to its one-past-the-end bound, 1e3 / 4e3 /
16e3 times over, is 0.02 / 0.07 / 0.29 s at a flat 6 MB: `advanced` interns the
address it moves to, and the addresses of one array are the same handful however
many passes reach them.  A path 8 / 16 / 24 subobjects deep is 0.00 s - one type
read per level and no search.

**Valgrind is clean** over all seventeen course fixtures, every probe program of
the five findings and every scaling probe above.

### Recorded, not landed

**The definition of an implicitly-declared default constructor an image was
folded through is owed by nobody.**  `struct held { int inside = 4; };
constexpr held h;` lays out the same image as the reference and emits no
`@held__held`, which the reference does: `owe_folded_construction` reads
`implicit_declaration` as "named by nothing the program wrote", which is 8.5.1's
aggregate and not a class 12.6.2p8 leaves a constructor to run.  It is
checkpoint I's rule and reaches no fixture R owns, so it is a failure-map row
rather than a fix here.

**3.5p3's `extern` before a `const` definition is not read.**  `extern const int
k; const int k = 5;` gets `binding=internal` and `_ZL1k` here and
`binding=strong` and `_Z1k` in the reference, which 3.5p3 agrees with.  It is
gap L's symbol naming read from the linkage side and has nothing to do with
addresses - the same program with no `extern` is identical in both.

**A non-type template parameter of pointer type is outside the subset.**
`template<int *P> struct H {};` is refused by this build and by
`pa21/cppgm++-ref` alike and accepted by g++, so no fixture can pin the
acceptance and the flattened-spelling door R opened in
`TemplateArgumentReader::name` is reached only by the parameters PA20 already
took.

## Changes

- **`sema_address.cpp`, `sema_constexpr.h` - `designated(node, ctx,
  value_fallback)`**: the lvalue walk told whether a value reading is still
  worth doing, and `operand_constant` - which reaches it because one has already
  refused - saying no.  2^depth becomes one walk.
- **`sema_constexpr.cpp`, `sema_constexpr_statement.cpp` - `operand_constant` at
  8.5's exits**: `initialized_value`'s clauses, `clause_of`'s expression and its
  non-aggregate clause, `object_from_constructor`'s two mem-initializer arms,
  `passed_arguments`' default-argument and `statement`'s return - so 4.2p1's
  decay and 8.3.2p1's binding are one reading and not one per spelling.
- **`sema_address.cpp` - `at_pointer_place`**: 4.10p1's null pointer value asked
  of a value this reading holds and not of a type alone, and 5.19p3's
  user-defined conversion asked at a pointer place as it is at an arithmetic
  one, with 12.3.2p2's `explicit` carried in from `cast_constant`.
- **`sema_address.cpp`, `sema_constexpr.cpp` - `covered_object`**: which of
  5.19's two answers a refusal about a valueless operand is, read off the
  declaration `AddressTable` holds - so checkpoint V's rule survives the name
  after the one that ran out.
- **`sema_address.cpp` - `address_distance`**: 12.2p1's temporary is part of
  which object an address is into, so two of them are 5.7p6's two different
  objects and not one.

## Performance Evidence

Best of three per shape, alternating between the binaries.  `6d975910` is the
checkpoint as it landed.

| shape | this build | `6d975910` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| `f(f(...(nonconst)))` 12 / 16 / 18 calls deep, refused | **0.00 / 0.00 / 0.00 s** at 6 MB | 0.05 / 0.67 / 2.67 s | 0.00 s |
| the same shape 8 / 16 / 32 / 64 deep and folding | **0.00 s** at 7 MB throughout | 0.00 s | - |
| 500 / 2000 / 8000 declarations each keeping an object's address | **0.01 / 0.07 / 0.29 s** at 10 / 22 / 70 MB | 0.01 / 0.07 / 0.29 s | 9.92 s at 111 MB at 8000 |
| 500 / 2000 / 8000 folds each binding a reference place to a different object | **0.02 / 0.11 / 0.46 s** at 12 / 29 / 99 MB | 0.02 / 0.11 / 0.45 s | 6.61 s at 117 MB at 8000 |
| a `while` walking a 4-element array to 5.7p4's bound, 1e3 / 4e3 / 16e3 passes | **0.02 / 0.07 / 0.29 s** at a flat 6 MB | 0.02 / 0.07 / 0.29 s | 1.20 s at 14 MB at 16e3 |
| a subobject path 8 / 16 / 24 levels deep | **0.00 s** at 6 MB | 0.00 s | - |
| the ten initializations of a pointer place this review reopened, in one unit | **0.00 s** at 7 MB | refused at the first one | 0.54 s at 15 MB |

Row one is finding 2 and row seven is finding 1: the first is work the
checkpoint did and this build does not, and the last is a program the checkpoint
refused and this build translates.  Every other row is unchanged, because
nothing here adds a walk - the door moved and the retry was removed.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **126 / 146**, from
  124 / 144: the same 20 failures the turn started with, name for name, and the
  two course fixtures this review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- Two course fixtures added, each with an oracle that agrees:
  `300-the-object-a-place-of-pointer-type-is-filled-from` is byte-identical to
  `pa21/cppgm++-ref`'s LowIR after canonicalization, and
  `500-a-pointer-with-no-constant-value-is-not-null-bad` is a refusal the
  reference makes too.  All seventeen course fixtures pass, and the fifteen that
  predate this review were regenerated from the reference unchanged.
- Forty-four probe programs over 8.5's places, 5.19p2's addresses and the two
  dialects swept through `pa21/cppgm++-ref` and g++; every disagreement that
  remains is one the reference alone makes or one of the three recorded above.
  `--emit-types` and `--emit-semantics` refuse nothing `--emit-lowir` accepts.
- `valgrind -q --error-exitcode=9` over all seventeen course fixtures, every
  finding's probes and every scaling probe: **clean**.
