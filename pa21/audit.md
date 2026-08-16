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

## Current Checkpoint Review

T is the checkpoint that took the fold out of the business of choosing which
declaration a call reaches: `ConstexprReading::chosen` ranked by arity, and now
`callee_candidates` writes the lookup `call_expression` writes and `selected`
hands that set to the analysis's own `select_overload`.  The rule is right and
the sentence it states is the one to hold the rest of the reading to - *a call
is one construct, and a constant expression is not a dialect of it*.  Every
shape swept through it answers as g++ answers: two overloads a count of places
cannot tell apart, a callee only 3.4.2's associated namespaces declare, a
template-id and a deduced template, 14.5.3p4's run at 0, 1 and n elements,
8.3.5p10's unnamed place, 8.3.6p1's default argument, a member call that 13.3.1p3
leaves `const` to decide, and 13.5.4p1's object.  8.3p1's declarator-id reads a
reference parameter and a nested declarator alike.

What the review found is that the sentence was landed at *one* of the readings
that choose a declaration and not at the others.  Two calls were still chosen by
a ranking of the fold's own, and one read still answered from a table instead of
from the declaration a lookup had already found.

### Findings

**1. 12.3.2p1's conversion function was chosen by a filtered set, so a class
whose best conversion is an ordinary one answered through a worse one.**
`converted` walked `owner->conversions` and dropped every declaration that is
not `constexpr_function` with a body *before* ranking what was left, took the
first that reached the place where none reached it exactly, and asked nothing of
12.3.2p2's `explicit` or 8.4.3p2's deleted declaration.  Every one of those is a
fact 13.3 reads and this reading did not:

```cpp
struct C {
	int v;
	constexpr C(int x) : v(x) {}
	operator int() const { return v; }              // 13.3 chooses this one
	constexpr operator bool() const { return true; }
};
constexpr C c(7);
enum E { e = c };            // e == 1
```

g++ calls that conversion ambiguous and `pa21/cppgm++-ref` refuses the
enumerator; this build gave `e` the value **1**, because the one declaration
13.3 chooses was dropped for not being a constexpr function and the ranking then
had only the other.  `constexpr int n = c;` was the same answer at another
place, and `int a[c]` at a third.  This is the shape the S+O audit found and
fixed for the *reading* of a class constant, one layer further in: there the
identifier of a list was taken for a number, here a conversion the program does
not perform is performed instead - and both turn a diagnostic into a wrong
answer rather than into a refusal.

Beside it, 12.3.2p2: a conversion function declared `explicit` is reached by a
direct-initialization, an explicit cast and 4p3's contextual conversion, and by
no other place.  `constexpr explicit operator int` answered `enum E { e = c }`
and `int a[c]` alike, which both oracles refuse.

The fix is the checkpoint's own sentence: `SemaAnalyzer::conversion_match` is
13.3.3.1.2's user-defined conversion sequence, and it is what `int n = c;`,
13.3's argument matching and 4p3's contextual `bool` already ask - so the fold
asks it too, over the whole set `gather_conversions` reaches through 10.2's
chain, and `builtin_conversion_type` answers 13.6's question where the place
named no type at all.  Whether the declaration chosen is a constexpr function
this unit defined is asked *after* 13.3 has chosen, in `call`, exactly as it is
for a call the fold ranks - a set narrowed by that test before ranking is a set
that answers a question the standard did not ask.

Which places leave `explicit` in is the second half of the fix.  4p3's
contextual conversion is `truth`, and 5.4p4's cast in either notation
direct-initializes, so those three ask for it and 5.19p3's converted constant
expression - an enumerator, an array bound, an alignment, a template argument,
an initialization - does not.  5.3.1p9's `!` had been reading the operand's bits
off `promote` rather than asking `truth` at all, which is why
`300-constexpr-contextual-bool-operators` had been passing through a reading
that ignored `explicit` instead of through the one place 12.3.2p2 allows it; it
now asks the reading `&&`, `||` and a condition ask, and one arm answers a
floating operand and an integral one alike.

**2. The other door that folds a call was left with the ordinary lookup.**  A
call written *as a template argument* reaches the fold as a flattened spelling
and not as a tree, and `TemplateArgumentReader::call_operand` resolved the word
with `resolve` alone and handed the one entity it found to `called_entity`.  So
the shapes T had just taught `called` were still refused one door over:

```cpp
namespace N2 { struct S { ... }; constexpr int f(S s) { return s.v + 1; } }
template<class T> constexpr int twice(T x) { return x + x; }
static_assert(H<f(N2::S(3))>::value == 4, "");    // f names nothing
static_assert(H<twice<int>(3)>::value == 6, "");  // twice<int> names nothing
```

Both oracles fold both.  3.4.2p2's associated namespaces and 14.2's
specializations are not a fact of *where the call was written*, so the reading
that answers them is one both doors ask: `called_name` takes the name and the
node where there is one, and `called` is now its caller rather than its
implementation.  `called_entity` is gone with the single-candidate set it built.

**3. A static data member read through an object expression was answered from
the subobject list.**  `member_value` looked the name up through
`accessed_member` - the same lookup `member_call` asks - and then searched
`data_members` for what it found, refusing when the declaration was not there.
9.4p2 makes a `static` member a member of the class and no subobject of any
object of it, so it is never there:

```cpp
struct S { static constexpr int value = 5; };
constexpr S s = S();
static_assert(s.value == 5, "");   // value names no subobject ...
```

Both oracles read it.  The access is worth what the declaration is worth, which
is the reading an id-expression naming it already asks - so the arm is one call
of `entity_constant`, split out of `id_constant` for the two doors that have
already found the declaration and need not look it up again.

### What the review confirmed rather than found

**An lvalue argument reaches `const T&` and a temporary reaches `T&&`.**  T
makes every constant a prvalue, which is what tells `read(T&)` from
`read(T const&)` - and leaves the question of how the *category* of the operand
reaches the ranking.  It reaches it as cv: a `constexpr` declaration's constant
carries the `const` its type has and a literal or a temporary does not, so
`which(a)` chooses `const int&` and `which(5)` chooses `int&&`, which is what
both oracles answer.

**The candidate walk is 13.3's own cost and the deferral is linear.**  A fold
loop calling one function found by 3.4.2 at 1e3 / 4e3 / 16e3 iterations is
0.02 / 0.08 / **0.37 s**, against 0.01 / 0.03 / **0.15 s** for the same loop
calling one found by the ordinary lookup - the associated-region walk is 2.4x
one lookup and linear in the calls, with no term in the size of the program.
A class with 17 conversion functions costs 18% more per conversion than a class
with one, which is the walk of the candidates and nothing else.

**The audit's own readings move nothing.**  Every shape shared with the
checkpoint binary times within noise of it: the conversion path at 16e3 folds is
0.44 s against 0.43 s, and 17 candidates 0.52 against 0.50.  The one measurable
cost is the template-argument door, where asking 14.2 for specializations before
the ordinary lookup is **7%** over 8000 arguments (0.93 s against 0.87 s) - the
price of the shapes finding 2 closed, and 18x under the reference's 16.59 s for
the same file.

**Two units reading one class agree.**  Two translation units in one invocation,
each folding an argument through 3.4.2 and a `bool` through 12.3.2p2's
`explicit` conversion, emit exactly the reference's image for the arguments; the
whole of the difference is `constexpr bool ok = !(!B());` taking a dynamic
initializer where the reference writes `= 1`, which the checkpoint binary writes
identically and which is group I - the lowering re-folding from the dump rather
than taking the analysis's answer.

**Fifty-two shapes were swept for exit status through this compiler,
`pa21/cppgm++-ref` and g++**: 18 over which declaration a conversion reaches an
arithmetic place (the place named or not, `explicit`, deleted, non-constexpr,
non-const, inherited, two that tie, and each of `!`, `&&`, a cast, a condition
and a `static_assert` asking), 15 over which declaration a call reaches
(overloads, 3.4.2, a template-id, deduction, packs at 0/1/n, unnamed places,
default arguments, `operator()`, member calls by cv, and an lvalue against a
temporary), 9 written as template arguments, 2 over 9.4p2's static member and 4
over what a fold *names* and the object file then holds.  All agree but the four
recorded below.

**Valgrind is clean over 183 inputs** - all 129 pa21 fixture sources and every
probe this review wrote.

### Recorded, not landed

**7.1.5p8's implicit `const` is not implemented, and implementing it would
change every member's symbol.**  A constexpr member function that is not a
constructor is a `const` member function, and this build takes the declarator's
word for it - as `pa21/cppgm++-ref` does, which refuses `constexpr C c(7);
c.get()` over `constexpr int get()` exactly as this build's analysis does.  The
fold's conversion reading used to be the one place that did not, because it
asked nothing about the object at all; it now asks 13.3.1p3's object argument
like everyone else, so `constexpr operator int()` written without `const` is
refused on a `constexpr` object here as it already was at an initializer.  g++
folds it.  Closing it means putting the `const` in the member's *type*, which is
what its object-file name is spelled from, and the `.ref` files hold the
reference's spelling.

**A deleted conversion function is skipped where 13.3 makes it a candidate.**
8.4.3p2 refuses a program that names one, so `struct C { constexpr operator
int() const = delete; constexpr operator bool() const; }` at an enumerator is
ambiguous to g++ and refused by the reference, and answered `1` here.  All four
of the analysis's readers of 12.3.2p1 - `conversion_match`, `contextual_bool`,
`contextual_integral`, `builtin_conversion_type` - drop a deleted candidate
before ranking, so the fold now answers what the rest of the build answers;
making it a candidate is one change to those four and no rule of 5.19.

**`--emit-types` collects no conversion functions at all.**
`collect_conversions` stands inside `declare_special_members`, which is behind
`semantics()`, so `entity.conversions` is empty in that dialect and a fold that
reaches one is refused there while `--emit-lowir` folds it.  The checkpoint
binary refuses the same inputs in the same dialect, so this is neither T's nor
this review's; it is one line beside `settle_specifications` when a suite asks
for it, and moving it changes what every earlier dialect's readers of
12.3.2p1 can see.

**A static member read through an object expression that is not itself a
constant.**  `S s; s.value` is folded by both oracles - 5.2.5p1 evaluates the
object expression and discards it, and nothing about a non-constant object is
*read* - where this build refuses at the object expression, because the fold
reaches the member lookup only through a value it has already folded and has no
type for an operand it has no value for.

## Changes

- **`sema_constexpr.cpp` - `converted` asks `conversion_match`**: 13.3.3.1.2's
  user-defined conversion sequence answers which declaration, over the set
  `gather_conversions` reaches, with `builtin_conversion_type` for the place
  that named no type; the constexpr test moved behind the choice, into `call`.
- **`sema_constexpr.h`, `sema_constexpr_statement.cpp`, `sema_constant.cpp`,
  `sema_value_expression.cpp` - `at_arithmetic_place` takes 12.3.2p2's door**:
  4p3's contextual conversion and 5.4p4's cast leave `explicit` in, and 5.19p3's
  converted constant expression does not.
- **`sema_constexpr.cpp` - 5.3.1p9's `!` asks `truth`**, which is the reading
  `&&`, `||` and a condition ask, and one arm answers a floating operand and an
  integral one alike where there had been two.
- **`sema_constexpr.cpp`, `sema_constexpr.h`, `sema_value_expression.cpp` -
  `called_name` is the one reading both doors that fold a call ask**;
  `callee_candidates` takes the name and the node where there is one, and
  `called_entity` is gone.
- **`sema_constexpr.cpp` - `member_value` answers a static member from its
  declaration**, through `entity_constant` split out of `id_constant`.

## Performance Evidence

Best of three per shape, alternating between the binaries:

| shape | this build | `33422f2f` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| a fold loop calling one function found by 3.4.2, 1e3 / 4e3 / 16e3 | 0.02 / 0.08 / **0.37 s** at 11 / 26 / **85 MB** | 0.02 / 0.08 / **0.37 s** at 11 / 25 / **84 MB** | folds none of it |
| the same loop calling one found by the ordinary lookup | 0.01 / 0.03 / **0.15 s** at 8 / 16 / **43 MB** | 0.15 s at 42 MB | folds none of it |
| 1e3 / 4e3 / 16e3 folds reaching an `int` place through one conversion function | 0.02 / 0.10 / **0.44 s** at 13 / 34 / **117 MB** | 0.02 / 0.09 / **0.43 s** | refuses |
| the same with 17 conversion functions declared, one of them viable | 0.03 / 0.12 / **0.52 s** at 13 / 34 / **117 MB** | 0.02 / 0.11 / **0.50 s** | refuses |
| 500 / 2000 / 8000 template arguments each holding a call | 0.04 / 0.20 / **0.93 s** at 17 / 49 / **177 MB** | 0.04 / 0.20 / **0.87 s** at 17 / 49 / **177 MB** | 16.59 s at 216 MB |
| the same where 3.4.2 is what names the callee | 0.05 / 0.23 / **1.05 s** at 19 / 55 / **201 MB** | refused | 45.43 s at 225 MB |

The `33422f2f` column is the checkpoint's own binary, built with `make build`
and measured on this machine in the same session.  The last row has no entry
there because that binary refuses the input - which is finding 2.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **75 / 132**, the count the
  turn started at with a byte-identical failing list: no fixture that passed
  then fails now, and `300-constexpr-contextual-bool-operators` still passes
  through the reading 12.3.2p2 allows rather than through one that ignored it.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- 52 shapes swept for exit status against `pa21/cppgm++-ref` and g++, each
  compile-pass case self-checking through a `static_assert`, and the conversion
  and template-argument shapes swept through `--emit-types` and
  `--emit-semantics` as well - where both binaries answer alike.
- A two-unit invocation compared as emitted LowIR against the reference.
- `valgrind --error-exitcode=99 -q` over 183 inputs - all 129 pa21 fixture
  sources and every probe this review wrote: **clean**.
