# PA21 Audit — `cppgm++ --emit-lowir` with full `constexpr`

A review of each landed checkpoint, in the order a fact travels: the storage a
declaration asks the program for, the name the image gives it, the value the
image holds, and the initialization and destruction left for the program to run.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| L | `af299cb9` | 3 / 3 + 6 recorded | **the function part of a name that has to say *which* function, which two functions can spell the same way.**  3.7.1p3's object is named in the image by the body that declared it and where in that body it stands, and the where is a span of this unit's terminals only until 7.1.2p4 leaves a definition every unit may hold - then it is a counter per function, and the function part is a flattened qualified name that `f(int)` and `f(double)` share, and `value<1>` and `value<2>` with it.  So `template<int N> int value() { static int data = N; }` laid out *one* global for both instantiations, holding 2, which is what `300-nested-function-template-local-static-array` shows and what makes `value<1>()` answer 2.  Beside it, 3.5p3's internal linkage was left off both readings of "a definition every unit may hold", so `static inline int k() { static int t; }` gave the object a *weak* symbol and a counter place where the reference and 3.5p3 both give this unit's own object a `tokens` place and an internal one.  And 12.4p8 ends the lifetime of *each element* of an array where 3.6.3p3's runtime takes one function and one object, so a block-scope `static P p[2]` handed the runtime `~P` and the address of the array and ended one lifetime of the two - the same shortcut 3.7.2p2's hand-off already had, both now handing a body of the program's own that ends them all |
| S+O | `46d8b2f4` | 3 / 3 + 4 recorded | **what a constant of class type is worth where a place asks for a number, and what a statement costs the second time the walk runs it.**  O made an object of literal class type a constant its declaration holds, and such a constant's bits are the identifier of an interned list - so the readings that take a constant as a number had to be told, and only four of them were.  `promote` refused a class operand, `truth` converted it and `convert` and every reader that spelled `evaluate(...).bits` took the *identifier* as the number: `constexpr C c(7)` with `constexpr operator int` laid out `int a[c]` with 12 elements where g++ lays out 7, gave `enum E { e = c }` the value 12, passed `static_assert(c)` where the conversion is `false`, and wrote zero for `constexpr int n = c ? 2 : 3`.  Every one of those was *refused* before O landed, so the checkpoint turned five diagnostics into five wrong answers, and 5.19p3's one rule - a converted constant expression may reach its type through a user-defined conversion - is now asked once and by all of them.  Beside it, S's claim that a block's objects are made once per fold held for the objects and not for the names: 7p3's typedef, alias, using-declaration and class were handed to `SemaAnalyzer::declaration` on *every* pass, so a `typedef` inside a loop of 102400 cost 73 MB against the 7 MB the same loop costs without one, and `struct P { int a; } p;` inside a loop was 3.2p1's class defined twice on the second pass.  And `fold_local`, the flag the checkpoint added to say which object an evaluation may write, was never set on a place the call filled - so `constexpr int f(int n) { n = n + 1; return n; }` was refused by the rule that exists to allow it |

## Current Checkpoint Review

S and O are the two checkpoints that landed since the last review, and they are
one increment: S runs 6.1-6.6 over a constexpr body instead of matching
7.1.5p3's one-`return` shape, and O makes an object of literal class type a
constant its declaration holds.  Between them they widen what a `SemaConstant`
may be and widen what a fold may do to it, so the review followed the two
widenings out to their readers.

The shape of both is sound.  S's walk is one pass over the statements with three
facts on the frame - the region each block opened, the object each declaration
declared, and what each region declared - so a loop of n passes costs the
regions and entities the *body* has and n statements, which is what the plan
claims and what re-measures at 0.18 s and a flat 7.0 MB for 1e5 passes.  O's
`fold_constant_object` reads the clause list once and hands it to
`ConstexprReading::object_of`, which is the fold that already answered a member
of class type inside an aggregate, and 3.6.2p2's image follows from the same
answer.

What the review found is on the far side of each widening: the readers.

### Findings

**1. A constant of class type is a list identifier, and every reading that
wanted a number took it as one.**  `SemaConstant` is `{TypeId, bits}` and the
bits of an object are the identifier of the interned list its subobjects hold.
O made such constants reachable through `SemaEntity::constant`, and told four
readers - two PA12 dump lines, the static-data-member literal and
`LowirUnitLowering::folded`.  The readers that take a constant as a *value of
arithmetic type* were not told, and they answered three different ways:
`promote` threw, `truth` applied 12.3.2p1's conversion, and `convert` and every
reader that spelled `evaluate(...).bits` - the array bound, the enumerator, the
case label, the alignment, the bit-field width, the `new[]` count, the
conditional's condition, the subscript index and `static_assert` itself - read
the identifier as the number.  With

```cpp
struct C { constexpr C(int v) : value(v) {} constexpr operator int() const { return value; } int value; };
constexpr C c(7);
```

`int a[c]` laid out 12 elements where g++ lays out 7, `enum E { e = c }` gave
`e` the value 12, `static_assert(c)` passed for a conversion that answers
`false`, and `constexpr int n = c ? 2 : 3` wrote zero.  All five compiled and
exited `EXIT_SUCCESS`; all five were *refused* by the build one commit earlier,
so this is a diagnostic turned into a wrong answer rather than a gap left open.

5.19p3 is one rule - "the implicit conversion sequence contains only
user-defined conversions, lvalue-to-rvalue conversions, integral promotions, and
integral conversions" - and it is now asked once.
`ConstexprReading::at_arithmetic_place` is that clause: a constant that stands
for an object is what 12.3.2p1's conversion function hands back and every other
constant is itself.  `convert`, `promote` and `truth` ask it, and so does each
of the readers above that reaches neither.  `converted`'s own ranking needed
3.9.3p1 beside it: the place a `constexpr int n` asks for is `const int`, and
comparing that against the conversion function's `int` made the exact answer
look inexact, so a class declaring `operator int` and `operator long long` was
refused as ambiguous where 13.3.3p1 calls the first one the best there is.

**2. A declaration statement the walk re-runs is declared again.**  S's
`declared` splits 7p3 two ways: a declaration that declares an object is created
once and written again, and one that declares only *names* - a typedef, an
alias, a using-declaration, a class - was handed to `SemaAnalyzer::declaration`
where it stood, on every pass.  A name is introduced into a region once and the
regions here are opened once per fold, so the second pass was a second
declaration: `typedef int T; T v = i;` inside a loop of 12800 / 51200 / 102400
took 14.4 / 39.7 / 73.1 MB against the flat 7.0 MB the same loop costs with the
typedef removed, and `struct P { int a; } p;` inside a loop failed on the second
pass with "a class is defined twice" - because `declared_type` re-read the
decl-specifier-seq per pass too, and a decl-specifier-seq is what holds a
class-specifier.  Both are now read on the pass that first reaches them:
`introduce` is the name arm, keyed by the node in the frame, and the declarator
is read only where the object is created.  `static_assert` stays outside that,
because 7p4 checks its condition where the declaration stands.  The same walk
was missing 7p1's lone type definition entirely - the parser leaves
`struct P { int a; };` as the class-specifier itself, which reached no arm of
the statement switch and was refused as a statement the evaluation does not run.

**3. `fold_local` had one writer where the checkpoint's own rule has two.**  S
added the flag to mark "the one kind of object an evaluation may write - a place
the call filled or an object a statement of the body declared", and only
`local` set it.  So the parameters `bind_arguments` binds were left unwritable
and `constexpr int f(int n) { n = n + 1; return n; }` was refused by the very
rule that exists to allow it - where 5.19p2 asks whether the object's lifetime
began inside the evaluation, and 12.2p1 says a place the call filled is exactly
such an object.  `bind_constant` now takes that answer as an argument: true for
a place the call filled, false for a subobject of the object the call was
written *on* and for a member 12.6.2p10 has already initialized, both of which
are objects this evaluation did not create.

### What the review confirmed rather than found

**The complexity is what the plan claims, re-measured on this build.**  A `for`
of 1e3 / 1e4 / 1e5 passes with a body-local declaration is 0.00 / 0.02 / 0.18 s
at a flat 7.0 MB peak, against 0.54 / 0.60 / 4.01 s for `pa21/cppgm++-ref`; 500
/ 2000 / 8000 declared constant objects each read back by a `static_assert` are
0.03 / 0.10 / 0.45 s against 0.64 / 0.99 / 3.77 s.  The L row above them is
unchanged at 0.01 / 0.05 / 0.22 s.  The one rule this review added is a type-kind
test on a path that already looked the type up, and it moves none of them.

**The engine's own bounds hold.**  `kMaxConstexprDepth` is checked at both
entries to a fold, so a class whose conversion function reaches another class
whose conversion function reaches the first ends with a diagnostic rather than a
stack.  `kMaxConstexprSteps` bounds one frame's statements, which is the only
thing a loop whose condition its body never falsifies runs out of.

**Two units reading one body agree.**  A `constexpr` function with a loop and a
body-local typedef and a second with a `while`, compiled as two units in one
invocation, fold to the same values as either alone - the frame and the regions
it opens belong to the fold and nothing of them is shared.

**The values are right where the program runs.**  A program declaring an
enumerator, an array bound, a `constexpr int` and a conditional off one class
constant builds through `lowir2cy86` and `cy86` and returns 0, as g++'s build of
it does.  `pa21/cppgm++-ref` refuses to compile it at all.

**Eighteen shapes were swept for exit status through this compiler,
`pa21/cppgm++-ref` and g++**: a class constant at an array bound, an enumerator,
a bit-field width, an alignment, an initialization, a conditional, the logical
operators, `!`, arithmetic, `static_assert`, a template argument, a subscript
index and a condition inside a body; a class with no conversion function at two
of those places; a class with two conversions where one is exact, where neither
is, and where the place is reached by both.  Every exit status agrees with g++
but the three recorded below, and the reference refuses eight of the eighteen.

### Recorded, not landed

**A `constexpr` variable whose initializer the fold refuses is lowered as a
dynamic initialization rather than refused.**  7.1.5p9 makes such a declaration
ill-formed; this compiler writes 3.6.2p1's zero and a startup body.  It is the
shape behind most of the failure map's V group and is that group's work, not
this review's.

**The lowering re-folds from the dump rather than taking the analysis's
answer.**  `LowirUnitLowering::folded` reads a fact of kind `Id` or `Literal`,
so `constexpr int n = c + 1;` - which the analysis now folds to 8 - still writes
`= zero` and a startup body where the reference writes `= 8`.  Nothing is wrong
at runtime and it is one of the failure map's I group.

**An object declared with no initializer is not value-initialized by the
fold.**  `constexpr D two;` leaves `two` with no value, so `constexpr int n =
two;` is refused where 8.5p7 and g++ both give it `D()`.  It is the same clause
O left open for a namespace-scope object with no initializer.

**A class operand at a bit-field width or an alignment is accepted where g++
refuses it.**  9.6p1 and 7.6.2p3 ask for an *integral* constant expression,
whose first sentence in 5.19p3 wants an expression of integral or unscoped
enumeration type, and this reading gives all the arithmetic places the converted
constant expression's leave.  It is more permissive than the standard at two
places no fixture pins and at which the reference refuses everything; it writes
no wrong value.

## Changes

- **`sema_constexpr.cpp`, `sema_constexpr.h` — `at_arithmetic_place`** is
  5.19p3's clause written once, asked by `convert`, `promote` and `truth` and by
  the array bound, the enumerator, the case label, the alignment, the bit-field
  width, the `new[]` count, the conditional's condition and the subscript index.
- **`sema_constexpr.cpp` — `converted` compares the place without its
  cv-qualifiers**, which is what makes `operator int` the exact answer for the
  `const int` a `constexpr int` declaration asks for.
- **`sema_constexpr_statement.cpp` — `introduce`** reads a name-introducing
  declaration on the pass that first reaches it, and `declared` reads the
  declarator only where it creates the object.  7p1's lone type definition
  reaches that arm too.
- **`sema_constexpr.cpp` — `bind_constant` is told whether the binding is one
  the evaluation created**, which is 5.19p2's question and marks a place the
  call filled `fold_local`.

## Performance Evidence

Best of three per shape, alternating between the two binaries:

| shape | this build | `pa21/cppgm++-ref` |
| --- | --- | --- |
| `for` of 1e3 / 1e4 / 1e5 passes, body-local declaration | 0.00 / 0.02 / **0.18 s**, peak RSS 7.11 / 7.00 / **7.05 MB** | 0.54 / 0.60 / **4.01 s** |
| 500 / 2000 / 8000 declared constant objects read back | 0.03 / 0.10 / **0.45 s** | 0.64 / 0.99 / **3.77 s** |
| 400 / 1600 / 6400 image-initialized statics in one body | 0.01 / 0.05 / **0.22 s** | 0.584 / 0.743 / **1.403 s** |
| a `typedef` inside a loop of 12800 / 51200 / 102400 | 0.03 / 0.09 / **0.19 s**, peak RSS 6.78 / 7.20 / **6.81 MB** | — |

The last row is the finding: before it, the same three inputs took 0.04 / 0.16 /
0.31 s at 14.4 / 39.7 / **73.1 MB**, growing by about 660 bytes a pass, and now
cost what the same loop without a typedef costs.

## Validation

- `make test-report-through-pa20` — **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` — **49 / 129**, one above the
  48 the turn started at, with the full failing list a subset of the turn-start
  one: no fixture that passed then fails now, and
  `300-constexpr-contextual-bool-operators` newly passes.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth;
  `sema_analyzer.h` is unchanged at 2396 of its 2400 lines.
- 18 shapes swept for exit status against `pa21/cppgm++-ref` and g++, each one
  self-checking through a `static_assert` so a wrong value is a failed compile.
- One program built through `lowir2cy86` and `cy86` and run, returning 0 as
  g++'s build of it does.
- `valgrind --error-exitcode=99 -q` over 200 inputs - all 129 pa21 fixtures and
  the probe inputs: **clean**.
