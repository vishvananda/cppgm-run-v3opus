# PA21 Audit — `cppgm++ --emit-lowir` with full `constexpr`

A review of each landed checkpoint, in the order a fact travels: the storage a
declaration asks the program for, the name the image gives it, the value the
image holds, and the initialization and destruction left for the program to run.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| L | `af299cb9` | 3 / 3 + 6 recorded | **the function part of a name that has to say *which* function, which two functions can spell the same way.**  3.7.1p3's object is named in the image by the body that declared it and where in that body it stands, and the where is a span of this unit's terminals only until 7.1.2p4 leaves a definition every unit may hold - then it is a counter per function, and the function part is a flattened qualified name that `f(int)` and `f(double)` share, and `value<1>` and `value<2>` with it.  So `template<int N> int value() { static int data = N; }` laid out *one* global for both instantiations, holding 2, which is what `300-nested-function-template-local-static-array` shows and what makes `value<1>()` answer 2.  Beside it, 3.5p3's internal linkage was left off both readings of "a definition every unit may hold", so `static inline int k() { static int t; }` gave the object a *weak* symbol and a counter place where the reference and 3.5p3 both give this unit's own object a `tokens` place and an internal one.  And 12.4p8 ends the lifetime of *each element* of an array where 3.6.3p3's runtime takes one function and one object, so a block-scope `static P p[2]` handed the runtime `~P` and the address of the array and ended one lifetime of the two - the same shortcut 3.7.2p2's hand-off already had, both now handing a body of the program's own that ends them all |
| S+O | `46d8b2f4` | 3 / 3 + 4 recorded | **what a constant of class type is worth where a place asks for a number, and what a statement costs the second time the walk runs it.**  O made an object of literal class type a constant its declaration holds, and such a constant's bits are the identifier of an interned list - so the readings that take a constant as a number had to be told, and only four of them were.  `promote` refused a class operand, `truth` converted it and `convert` and every reader that spelled `evaluate(...).bits` took the *identifier* as the number: `constexpr C c(7)` with `constexpr operator int` laid out `int a[c]` with 12 elements where g++ lays out 7, gave `enum E { e = c }` the value 12, passed `static_assert(c)` where the conversion is `false`, and wrote zero for `constexpr int n = c ? 2 : 3`.  Every one of those was *refused* before O landed, so the checkpoint turned five diagnostics into five wrong answers, and 5.19p3's one rule - a converted constant expression may reach its type through a user-defined conversion - is now asked once and by all of them.  Beside it, S's claim that a block's objects are made once per fold held for the objects and not for the names: 7p3's typedef, alias, using-declaration and class were handed to `SemaAnalyzer::declaration` on *every* pass, so a `typedef` inside a loop of 102400 cost 73 MB against the 7 MB the same loop costs without one, and `struct P { int a; } p;` inside a loop was 3.2p1's class defined twice on the second pass.  And `fold_local`, the flag the checkpoint added to say which object an evaluation may write, was never set on a place the call filled - so `constexpr int f(int n) { n = n + 1; return n; }` was refused by the rule that exists to allow it |
| F | `6cdd7e1d` | 3 / 3 + 4 recorded | **the value a clause is judged by, and the destination a conversion has no value for.**  F gave `SemaConstant` a `real` beside its bits and made an array a constant object, and the review followed both out to their readers.  8.5.4p7's second bullet was the reader the widening left behind: `floating_round_trips` decoded `value.payload` with `strtold` and wanted the node to be a *literal*, so `constexpr double d = 1.5; struct S { float a; }; constexpr S s = { d };` - and every `float` clause off a name, an operator or a folded call - was refused as narrowing where g++ and the reference both take it.  Asking the *fold* instead uncovered the rule beneath it: 8.5.4p7 excepts a constant whose value is "within the range of values that can be represented, **even if it cannot be represented exactly**", and the old test was an exactness, so `float a{0.1}` was refused too.  Under that widening `SemaAnalyzer::convert` had no arm for a destination of class type at all: it fell through to the integral width path and handed back the operand's bits *under the object's type*, which is the one number a class constant's bits may not be - so `struct P { int x; constexpr P(int v) : x(v) {} }; constexpr P ps[1] = { 999999 };` interned a member list identified by 999999 and `ps[0].x` read `parameter_lists_[999999]`, a **segfault** valgrind calls an invalid read of a page that was never mapped.  Beside them 5p4's overflow: an infinity is what `1e400` and `1e308 * 10.0` come to, `TypeTable::real_type` keyed one by an undefined cast of `ldexp(inf, 64)`, and `spell_floating` took the `f` of `inf` for 2.14.4p1's suffix and wrote `= in` into the image where the reference writes `= inf` |

## Current Checkpoint Review

F is the one checkpoint that landed since the last review, and it widens two
facts at once: `SemaConstant` gains `real`, so 3.9.1p8's other kind of
arithmetic value has somewhere to land, and 8.3.4p6's array joins 9.2p1's class
as an aggregate whose subobjects a constant holds.  Both widenings are sound in
shape.  The floating value travels on `SemaEntity::real` and `SemaFact::real` to
3.6.2p2's image, which now holds what the initializer *came to*: `constexpr
float v = 16777217.0;` writes `16777216f` and `constexpr double g = third(9.0);`
writes a value no operand spelled, both byte-for-byte what `pa21/cppgm++-ref`
writes.  `array_of` interns one list per array with 8.5p7's value-initialized
tail worked out once and repeated, which is why a `constexpr int a[1000000] =
{1};` folds in 0.10 s and 13 MB rather than a million times anything.

What the review found is on the far side of both widenings, and in one case
under them.

### Findings

**1. `convert` had no arm for a destination it can make no value of, and handed
back the operand's bits under the object's type.**  4.7-4.9 are the conversions
between arithmetic types; the function that owns them ends in a width
truncation, and `width_of` answers 64 for a type that has no arithmetic width at
all - so a destination of class type fell through it and came back as a constant
of *that* type whose bits were the operand's number.  A class constant's bits
are the identifier of the interned list its subobjects hold and nothing else, so
that number is then a list index.  With

```cpp
struct P { int x; constexpr P(int v) : x(v) {} };
constexpr P ps[1] = { 999999 };
static_assert(ps[0].x == 999999, "");
```

`array_of` converted the `int` clause to the element type, `member_value` read
`parameter_lists_[999999]`, and the compiler **segfaulted** - valgrind reports
an invalid read of eight bytes at an address never mapped.  F is where it became
reachable, because an array is the first aggregate whose *element* type a clause
may have to be converted to, but the door is older than the element: `object_of`
converts a clause to a member's type and `object_from_constructor` converts what
a mem-initializer wrote to the member it names, and both reach the same
fall-through.  So
the fix is at the door rather than at the caller: a `convert` whose destination
is of no arithmetic type, and which is not already of that type, is 8.5's
initialization of an object and not a conversion, and `object_of` and `array_of`
own that.  All three callers now refuse instead, and what they refuse is a fold
that does not happen - the declaration is simply not a constant, which is where
`constexpr P ps[1] = { 1 };` stood before F.

**2. A `float` clause off anything but a literal was refused as narrowing, and
the rule underneath it was an exactness where 8.5.4p7 writes a range.**
8.5.4p7's second bullet excepts a source that is a constant expression, and
`floating_round_trips` answered that exception by decoding `value.payload` with
`strtold` and requiring `value.what` to be `"literal"` - which was the whole
truth before F, when no floating value travelled as a value at all.  After F it
is wrong twice over.  It is wrong for the *source*: `constexpr double d = 1.5;
struct S { float a; }; constexpr S s = { d };` has a value the fold knows and no
literal spelling, so it narrowed - as did `{1.0 + 0.5}` and `{half(1.0)}`, three
shapes g++ and the reference both take.  And it is wrong for the *question*: the
clause the standard writes is "within the range of values that can be
represented, even if it cannot be represented exactly", so `float a{0.1}` is a
clause a `float` takes and the old test refused it.  Both are now one reading:
what the clause came to is the fold's answer - a literal, a name, a call and an
operator over any of them alike - and `floating_fits` asks whether an object of
the destination has room for it, which is 4.8p1's overflow and no rounding at
all.  Eighteen shapes across the bullet were swept against g++ afterwards and
all eighteen agree, including the six that must still refuse: a `float` clause
off `{1e300}`, off `{-1e300}`, off `{1e400}` and off a `constexpr double d =
1e300;`, a `double` clause off `{1e4000L}`, and the first bullet's `{1.0}` into
an `int`.

The fold that answers this costs a walk of the clause, so it is asked where the
bullet's answer needs it: a floating source reaching an integer narrows however
constant it is, and one reaching a floating type at least as wide narrows not at
all.  A list of 32000 `double` clauses into `double` members pays nothing for
it.

**3. 5p4's overflow left the finite values, and three readings had no answer for
what it left.**  An infinity is what `1e400` names and what `1e308 * 10.0` comes
to, and g++ accepts the first with a warning, so it is not a shape a refusal
disposes of.  `TypeTable::real_type` keyed such a value by
`(unsigned long long)std::ldexp(std::frexp(value, &exponent), 64)`, and `frexp`
takes no significand out of an infinity - the cast is undefined and the key it
lands on is one another value may share.  `spell_floating` then took the `f` of
`inf` for 2.14.4p1's floating-suffix and wrote `global @d : f64 ... = in` into
the image, which is no LowIR operand at all, where the reference writes `= inf`.
And 4.9p1's conversion of a floating value to an integral type was gated on
`value.real > -9.3e18 && value.real < 1.85e19`, whose two bounds are each on the
*wrong side* of the cast they guard: `1.85e19` is above `2^64` and `-9.3e18` is
below `-(2^63)`, so a value inside the gate could still be a cast with no
answer.  4.9p1 is now one pair of readings - `floating_fits_integral` and
`integral_of_floating`, stated once and asked by the fold and by the lowering's
own second fold - the key is a place of its own for each of the four non-finite
values, and the suffix is stripped only where 2.14.4p1 puts one, after a digit
or a `.`.  `inf`, `-inf`, `inff` and `infL` are now what the image holds, which
is what the reference holds for the same six shapes.

### What the review confirmed rather than found

**The complexity is what the plan claims, re-measured on this build against the
build the checkpoint landed.**  A `for` of 1e3 / 1e4 / 1e5 passes over floating
arithmetic is 0.00 / 0.03 / 0.27 s at a flat 6 MB; 2000 / 8000 / 32000 declared
*distinct* floating constants each read back are 0.04 / 0.20 / 0.88 s; 1000 /
4000 / 16000 array elements are 0.00 / 0.02 / 0.07 s at 6 / 9 / 18 MB.  Every
one of those is within a hundredth of a second of the same input on the
pre-audit binary, so this review's readings move none of them.  An array whose
bound names a million elements and whose clauses name one folds in 0.10 s and 13
MB - 8.5p7's tail is one interned entry repeated and not a million folds.

**A floating aggregate costs more than an integral one and costs it linearly.**
2000 / 8000 / 32000 clauses of a `double` array member, each an operator: 0.05 /
0.21 / **0.81 s** at 22 / 66 / **243 MB**, against 0.02 / 0.09 / **0.37 s** at
10 / 22 / **72 MB** for the same count of `int` clauses.  The reference shows the
same ratio at 2.7x the time and 1.4x the memory - 0.70 / 1.20 / **2.90 s** at
34 / 95 / **339 MB** against 0.70 / 0.90 / **1.90 s** at 21 / 43 / **135 MB** -
so the width is what the shape costs and not a fold this milestone repeats.
Making those clauses `float`, which is the one shape 8.5.4p7's second bullet
asks the fold about, adds 16% to both: 0.94 s and 274 MB at 32000.

**Two units reading one pool agree.**  Two translation units in one invocation,
each declaring floating constants, folded calls and arrays of `long double`,
write exactly the image the reference writes for the same invocation - the pool
`real_type` interns into belongs to the invocation and its indices are a key
both units read the same way.

**Nothing in the floating fold is exponential.**  A chain of 100 / 200 / 400
nested conditionals over floating operands is 0.01 s at every size, and 800
nested arithmetic operators is refused by the parser's own depth rather than
run.
2000 / 8000 / 32000 separate `constexpr` arrays and 4000 / 16000 / 64000
elements in one are each linear.

**Thirty-three shapes were swept for exit status through this compiler,
`pa21/cppgm++-ref` and g++**: eighteen across 8.5.4p7's bullets, six over 5p4's
overflow and 4.9p1's out-of-range conversion, two over an aggregate whose
element or member type a clause must be converted to, and seven over the array
constant, the arithmetic places a floating value may and may not reach, and a
loop whose condition is one.  Every exit status agrees with g++ but seven: the
four that are 5p4's overflow and 4.9p1's conversion, the two that are an element
and a member of class type built from a clause of another, and a `for` inside a
`constexpr` body, which C++11 has not got and checkpoint S runs.  The first two
are what this section records below.  Where the *reference* disagrees with g++ -
a floating enumerator and a floating array bound - this build refuses with g++.

**Valgrind is clean over 213 inputs** - all 130 pa21 fixtures and every probe
this review wrote, including the one that segfaulted before finding 1.

### Recorded, not landed

**5p4's overflow is accepted where g++ refuses it.**  `constexpr double d =
1e308 * 10.0;` and `constexpr int n = 1e18;` are undefined behaviour, which
5.19p2 leaves outside a constant expression, and g++ makes both ill-formed.
This build folds them and `pa21/cppgm++-ref` folds them to the same values -
the reference writes `inf` and `-1486618624` for exactly these two - so the
image is now the reference's and the refusal is a judgment the `.ref` files do
not ask for.  What the review fixed is the part that was neither oracle's: the
undefined cast, the malformed spelling and the two bounds on the wrong side of
`2^64`.

**An element or member of class type is not built from a clause of another
type.**  `constexpr P ps[1] = { 1 };` with `constexpr P(int)` is 8.5.1p2's
copy-initialization of the element, which is a constructor call, and `array_of`
hands the clause to `convert` rather than to the reading that runs one.  Both
oracles fold it.  Finding 1 turned it from a segfault into a refusal; building
it is the checkpoint work `object_of` and `array_of` share, and it is the same
gap behind a member of class type reached by a converting constructor.

**A floating literal in a flattened template-argument spelling is unreadable.**
`A<(int)1.5>` is refused with "a member access written as a template argument
names no member": `split_value_expression` reads a word that opens with a digit
as a name and stops at the `.`, so `1.5` is three words and the middle one is
5.2.5's operator.  It is PA20's reader and F did not touch it, but F is what
gave `literal_constant` a floating value to hand back, so the gap is now one
scanner rule away rather than two.

**A floating value bound to an integral non-type parameter is truncated rather
than refused.**  `A<d>` off a `constexpr double d = 2.5;` binds `A<2>`, where
14.3.2p5's converted constant expression leaves 5.19p3 no floating-integral
conversion and g++ refuses.  The reference binds `A<2>` as this build does, and
the parameter *type* rule F added is the one 14.1p4 states; this is the argument
side of it.

## Changes

- **`sema_constant.cpp` — `convert` refuses a destination of no arithmetic
  type** that the value does not already have, which is the door `array_of`,
  `object_of` and `object_from_constructor` all fell through into handing back a
  number as a list identifier.
- **`sema_init_list.cpp`, `sema_analyzer.h` — `floating_fits`** is 8.5.4p7's
  second bullet asked of the value the fold arrived at and asked as a *range*,
  and `require_no_narrowing` folds the clause where that bullet needs the answer
  and nowhere else.
- **`token_model.h` — `floating_fits_integral` and `integral_of_floating`** are
  4.9p1 stated once, with the two bounds the casts under them are defined
  between, asked by `sema_constant.cpp`'s fold and by `lowir_lower.cpp`'s.
- **`type_model.cpp` — `real_type` keys a non-finite value by a place of its
  own** rather than by an undefined cast of what `frexp` hands back for one.
- **`lowir_lower.cpp` — `spell_floating` strips 2.14.4p1's suffix only where
  one stands**, after a digit or a `.`, so the `f` of `inf` is part of the value.

## Performance Evidence

Best of three per shape, alternating between the two binaries:

| shape | this build | pre-audit build | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| 2000 / 8000 / 32000 distinct floating constants read back | 0.04 / 0.20 / **0.88 s** at 15 / 42 / **153 MB** | 0.04 / 0.20 / **0.89 s** | — |
| `for` of 1e3 / 1e4 / 1e5 passes of floating arithmetic | 0.00 / 0.03 / **0.27 s** at a flat **6 MB** | 0.00 / 0.03 / **0.27 s** | — |
| 1000 / 4000 / 16000 array elements read back | 0.00 / 0.02 / **0.07 s** at 6 / 9 / **18 MB** | 0.00 / 0.02 / **0.07 s** | — |
| 2000 / 8000 / 32000 `double` clauses of an array member | 0.05 / 0.21 / **0.81 s** at 22 / 66 / **243 MB** | — | 0.70 / 1.20 / **2.90 s** at 34 / 95 / **339 MB** |
| the same count of `int` clauses | 0.02 / 0.09 / **0.37 s** at 10 / 22 / **72 MB** | — | 0.70 / 0.90 / **1.90 s** at 21 / 43 / **135 MB** |
| the same count of `float` clauses, which 8.5.4p7 folds | 0.05 / 0.23 / **0.94 s** at 23 / 71 / **274 MB** | refused | — |
| `constexpr int a[1000000] = {1};` | **0.10 s** at **13 MB** | 0.10 s | — |

The pre-audit column is the same three inputs on `6cdd7e1d`'s binary, built and
measured on this machine in the same session: the three readings this review
changed sit on paths that fold once per clause and per declaration, and they
move none of the rows the checkpoint owns.  The `float` clause row is the one
that changed at all, and it changed from a refusal to an answer.

## Validation

- `make test-report-through-pa20` — **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` — **59 / 130**, the same
  count the turn started at with a byte-identical failing list: no fixture that
  passed then fails now.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth;
  `sema_analyzer.h` holds at the 2400 lines that are its ceiling.
- 33 shapes swept for exit status against `pa21/cppgm++-ref` and g++, each
  compile-pass case self-checking through a `static_assert` so a wrong value is
  a failed compile.
- Six image shapes over 5p4's infinities and NaN compared against the reference
  byte for byte, at all three floating widths.
- Two translation units in one invocation compared against the reference.
- `valgrind --error-exitcode=99 -q` over 213 inputs - all 130 pa21 fixtures and
  every probe this review wrote: **clean**.
