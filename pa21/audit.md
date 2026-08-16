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

## Current Checkpoint Review

N is the one checkpoint that landed since the last review, and it is a *reader*
rather than a new fact: 15.4 had already settled `SemaEntity::nonthrowing`, and
5.3.7's operator is one question asked of it.  The shape of the reading is
right, and it is the part of the checkpoint that is hardest to get right.  The
operand is read exactly once, through `probe_expression`, so 13.3's ranking
between overloads a using-declaration merged, 8.3.6p1's default argument,
13.3.1.2's member operator and 8.5's copy of a by-value argument all answer it
and none of them is re-decided by a walk of the syntax; 5.3.7p1 leaves the
operand unevaluated and the scratch node the reading writes into is dropped with
the temporaries it made, which is why a function template named only inside an
operand emits exactly the LowIR the reference emits and no definition beside it.
And the one answer serves all three places the operator is written.

What the review found is on the two sides that reading does *not* cover: where
15.4p1's own condition is folded, and which line of the resolved tree a call is
read off.

### Findings

**1. An exception-specification is 9.2p2's complete-class context, and the
condition was folded where the class was not yet complete.**  N's own change to
15.4p1 is that the condition is folded rather than matched against the spelling
`true` - which is right, and is what makes `noexcept(sizeof(int) == 4)` say what
it comes to.  It was folded at the declarator.  9.2p2 lists the
exception-specification among the contexts in which a class is regarded as
complete, so a member's condition may name a member declared below it:

```cpp
struct S { void f() noexcept(k); static constexpr bool k = true; };
void S::f() noexcept(k) {}
```

folded to *no* inside the class, where `k` had not been reached, and to *yes* on
the definition written after the closing brace - so
`require_matching_exception_specification` **refused a valid program**, with
"two declarations of f write exception-specifications 15.4p1 does not make the
same".  Both `pa21/cppgm++-ref` and g++ accept it.  With no out-of-class
definition written there is no diagnostic and the fact is simply wrong:
`noexcept(((S*)0)->f())` read false, and `boundary.unwind` in the emitted object
said an object of `S` may unwind where the reference says it may not.

Pre-N this shape could not arise - the reading matched the spelling and answered
no in the class and out of it alike - so the checkpoint is where a rule that
reads a class turned an agreement into a disagreement.  The fix is at the
declaration rather than at the reader: a condition the declarator's fold could
not answer is kept on the class's own region, and folded where the
class-specifier closes.  One fold per member that wrote such a condition, and
none for a member that wrote none.

Where it is folded is the second half of the finding.  `settle_class_answers` is
the obvious home - it is already where 15.4p14's implicit specifications are
settled, because it is where the class is complete - but it is reached through
`declare_special_members`, which stands behind `semantics()`.  15.4p1's answer is
not asked only where a class is given the members no declaration wrote:
15.4p3 compares the two declarations of one function wherever a definition is
read, in *every* dialect, and `--emit-types` refused the program above exactly as
`--emit-lowir` did.  So the fold stands at the close of the class-specifier
itself and answers the same in all three.

**2. The new- and delete-expression arm read a field neither writer fills.**
`nonthrowing_tree` asked `node.fact.entity` of a `NewExpression` and a
`DeleteExpression`.  Neither `new_expression` nor `delete_expression` sets it:
the allocation function 3.7.4.1 gave the storage and the deallocation function
5.3.5p9 paired with the delete are each named by a `callee` line of their own,
which is the same line a written call names its function by.  So the arm was
null at every reach and answered false for every operand of either kind -
*right by accident* for the throwing default `operator new`, which is the one
shape a fixture writes, and wrong for the rest.  `noexcept(delete p)` was false
over an `int*`, over a class with a trivial destructor and over one with a
user-written destructor, where both oracles answer true, and `new S` with a
`static void* operator new(size_t) noexcept` was false where both answer true.

The rule underneath it is the one the fix states: 5.3.7p3 asks about the
*declarations a call reaches*, and what names a declaration in the resolved tree
is a line and not an expression kind.  So `Callee` is one arm and
`DestructorAction` - 12.4p3's end of a lifetime, which a delete-expression
writes and which footnote 80 counts - is the other, and the `Call` arm is left
asking the one question only it can answer: a call-expression with no callee
under it reached no declaration, which is a call through a pointer to function.
Four expression kinds now read one fact through one arm, and no kind is left
reading a field its own writer never fills.

**3. 5.3.7p3's second bullet was a walk that could not fire.**  `holds_throw`
walked the whole operand tree for an `AstKind::ThrowStatement` before the
operand was read.  That node is built by `parse_statement` and by nothing else:
`noexcept(throw 1)` is refused by the *parse* - a throw-expression is no part of
the expression grammar this milestone reads - and `throw 1;` is refused by the
analysis as a statement outside the PA12 subset.  So the bullet's reader could
answer nothing, and cost a walk of the operand per operator to do it.  It is
removed and the gap recorded below.

### What the review confirmed rather than found

**The reading itself is single-pass, and the audit's readings move nothing.**
Every shape shared with the checkpoint binary times identically on this build:
500 / 2000 / 8000 declarations of three operators over a member call are 0.04 /
0.19 / 0.78 s on both, and one operand of 500 / 2000 / 8000 calls is 0.01 / 0.03
/ 0.13 s on both.  The reference is 0.62 / 0.92 / 2.97 s and 0.61 / 1.59 / 0.86 s
for the same two.

**The deferred condition is linear in the conditions and free where none is
written.**  400 / 1600 / 6400 members each writing `noexcept(k_i)` over a
constant declared *below* it - every one of them deferred - fold in 0.01 / 0.04 /
**0.21 s** at 9 / 16 / **44 MB**, against 0.00 / 0.02 / **0.08 s** at 7 / 11 /
**25 MB** for the same count writing `noexcept(true)`, which defers none.  The
difference is one fold per deferred condition and nothing else; the reference
takes 0.63 / 1.79 / **26.78 s** over the first of those, which is not linear.

**An unevaluated operand leaves nothing behind.**  A function template named
only inside a `noexcept` operand, and a destructor named only by a
`delete`-expression inside one, each emit LowIR byte-identical to the
reference's - the scratch node the reading writes into is dropped with the
temporaries it made, and no definition the program never uses is emitted.

**Two units reading one class agree.**  Two translation units in one invocation,
each holding the class of finding 1 and reading the operator over its member,
write exactly the image the reference writes; the checkpoint binary refuses the
same invocation.

**Thirty-three shapes were swept for exit status through this compiler,
`pa21/cppgm++-ref` and g++**: the four member kinds a deferred condition can be
written on and the two nestings it can be written in, 5.3.7p3's bullets over a
call through a pointer, a `dynamic_cast`, a new- and a delete-expression, an
explicit destructor call, a conversion function and an unevaluated `sizeof`
operand, the operator written as a template argument in two regions that spell
it the same, and 15.4p13's specialization.  All agree but the three recorded
below.

**Valgrind is clean over 164 inputs** - all 129 pa21 fixture sources and every
probe this review wrote.

### Recorded, not landed

**A name no declaration answers, written in a condition, is accepted.**  `void
f() noexcept(bogus);` is ill-formed and g++ says so; this build folds the
condition inside a `catch`, so the lookup's diagnostic is swallowed and the
declaration is left allowing every exception.  `pa21/cppgm++-ref` accepts it
too, and no fixture asks - narrowing the catch to the fold's own refusal would
make a dependent condition ill-formed, which is the shape 14.6p8 exists for.

**A throw-expression is no part of the operand grammar.**  Both oracles fold
`noexcept(throw 1)` to false and `noexcept(noexcept(throw 1))` to true; this
build refuses both at the parse, and refuses `throw 1;` as a statement.  It is
the expression subset and not 5.3.7, and closing it is a parse rule and an
arm beside `Callee`.

**5.3.7p3's third bullet is answered for the pointer form of `dynamic_cast`.**
The reference answers no there and g++ answers yes; this build follows the
reference, which is where the `.ref` files are.

## Changes

- **`sema_scope.h`, `sema_noexcept.cpp`, `sema_class.cpp`, `sema_analyzer.cpp` -
  `defer_specification` and `settle_specifications`** keep 15.4p1's condition on
  the class's region where the declarator's fold could not answer it, and fold
  it at the close of the class-specifier - in every dialect, because 15.4p3
  compares two declarations of one function in every dialect.
- **`sema_noexcept.cpp` - `nonthrowing_tree` asks the lines that name a
  declaration**: one arm for `Callee` and one for `DestructorAction`, with the
  `Call` arm left answering only 5.3.7p3's call through a pointer.
- **`sema_noexcept.cpp` - `holds_throw` is removed**, with 5.3.7p3's second
  bullet recorded as the expression-subset gap it is.
- **`sema_noexcept.cpp`, `sema_class.cpp` - 15.4's declarator reading moves to
  the file that owns 15.4**: `declarator_nonthrowing` and the fold under it are
  one walk of the declarator shared with the deferral, beside the reader 5.3.7
  needs, and `sema_class.cpp` is 59 lines shorter for it.

## Performance Evidence

Best of three per shape, alternating between the binaries:

| shape | this build | `76c1c8fd` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| 500 / 2000 / 8000 declarations of three operators over a member call | 0.04 / 0.19 / **0.78 s** at 17 / 50 / **183 MB** | 0.04 / 0.18 / **0.77 s** at 17 / 50 / **183 MB** | 0.62 / 0.92 / **2.97 s** at 20 / 38 / **109 MB** |
| one operand of 500 / 2000 / 8000 calls | 0.01 / 0.03 / **0.13 s** at 8 / 14 / **36 MB** | 0.01 / 0.03 / **0.13 s** | 0.61 / 1.59 / **0.86 s** at 17 / 27 / **31 MB** |
| 50 / 100 / 200 nested `noexcept` | 0.00 s at 6.3 / 6.5 / **7.0 MB** | 0.00 s | 0.53 s at 14 / 14 / **16 MB** |
| 400 / 1600 / 6400 members whose condition names a member below it | 0.01 / 0.04 / **0.21 s** at 9 / 16 / **44 MB** | refused | 0.63 / 1.79 / **26.78 s** at 19 / 33 / **90 MB** |
| the same count writing `noexcept(true)`, which defers none | 0.00 / 0.02 / **0.08 s** at 7 / 11 / **25 MB** | 0.00 / 0.02 / **0.08 s** | — |

The `76c1c8fd` column is the checkpoint's own binary, built and measured on this
machine in the same session.  The deferral row has no entry there because that
binary refuses the input - which is finding 1.

One row the checkpoint carried forward is corrected: 800 nested `noexcept` is
not 0.00 s on this build, it is a **refusal** - 800 nested parentheses overflow
the parser's own depth limit, which the reference has not got, and it folds the
same input in 0.54 s.  The measurable range is the row above.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **66 / 131**, the count the
  turn started at with a byte-identical failing list: no fixture that passed
  then fails now, and `300-what-a-noexcept-operator-asks-of-a-call` still passes
  through the fixed reading.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth;
  `sema_analyzer.h` holds at the 2400 lines that are its ceiling, because the
  three declarations this review adds are `ConstexprReading`'s and go in
  `sema_constexpr.h`.
- 33 shapes swept for exit status against `pa21/cppgm++-ref` and g++, each
  compile-pass case self-checking through a `static_assert`, and finding 1's
  shape swept through `--emit-types` and `--emit-semantics` as well.
- Three shapes compared as emitted LowIR against the reference, one of them a
  two-unit invocation.
- `valgrind --error-exitcode=99 -q` over 164 inputs - all 129 pa21 fixture
  sources and every probe this review wrote: **clean**.
