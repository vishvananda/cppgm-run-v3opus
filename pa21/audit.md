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

## Current Checkpoint Review

Checkpoint P is where 13.3.1.2 became one owner.  `sema_operator.h`'s
`OperatorCall` took the candidate set out of `SemaAnalyzer` - a reader beside
`ArgumentLookup` and `Deduction` - so the expression layer and
`ConstexprReading::operator_constant` gather it once and rank it with the one
`select_overload`, the first operand handed over twice as 13.3.1.2p4 asks.  It
carried three siblings: `at_class_place` performs 5.2.2p4's and 6.6.3p2's
initialization of a place of class type, `initialized_value` makes 8.5 one
reading wherever a declaration stands, and `clause_of` asks 14.7.1p1 for a
specialization's definition before reading 8.5.1p1's aggregate off it.

That rule is right, and the three siblings are right: 5.2.2p4's argument, 6.6.3p2's
returned value and a call written on a temporary each fold byte-identically to
`pa21/cppgm++-ref`, and so does a nested aggregate clause in a class template's
declaration statement.  Seventy operator shapes - a member and a non-member
declaration of each of 13.5p1's binary and unary operators, the subscript, the
call, an operand of enumeration type and one 3.4.2 alone reaches - were swept
through both `pa21/cppgm++-ref` and g++.

What the review found is that P landed the *answer* and left the **question**.
Whether an operator expression is a call or 13.6's built-in operator is settled
by the types of its operands and by nothing else - and in front of the new set
that question was being asked of the values a fold had already computed, of the
shape the operand happened to be written in, and of a candidate set that held
declarations no program may write.  Each of the four findings is that one
sentence read from a different side.

### Findings

**1. 5.14p1's short-circuit was read off the operand values, so a class operand
lost it.**  `binary_constant` evaluated the left operand, asked
`overloadable_operand` of the *constant it came to*, and evaluated the right one
whenever the answer was yes:

```cpp
const bool overloaded = overloadable_operand(operands[0]);
if (!overloaded && truth(operands[0]) == (node.token == OP_LOR)) { ... }
operands.push_back(analyzer_.evaluate(*node.children[1], ctx));
```

A class that declares `operator bool` and no `operator&&` reaches the *built-in*
`&&`, whose right operand 5.14p1 leaves unevaluated - so

```cpp
constexpr flag no = {false};
static_assert(!(no && quotient(1, 0)), "");
```

was **refused** as a division by zero, and `yes || quotient(1, 0)` with it, where
`pa21/cppgm++-ref` and g++ both fold them.  Evaluating what 5.14p1 says not to
evaluate is not a cost: it refuses valid programs, and it runs the writes a
program guarded behind the operator.  Pre-P (`afb06571`) the built-in reading
short-circuited and both folded, so the checkpoint is where the disagreement
became reachable.  The mirror half was there before P and is closed with it:
13.3.1.2p2's test is over *every* operand, and a fold that has read only the left
one cannot ask it - so `0 && mark` with `constexpr int operator&&(int, token)` in
scope answered **false** where 13.3 chooses the declaration.  The question is now
asked of 13.3.1.2p3's set gathered from the operand the fold has read, before the
one it has not is read: an operand of class type asks before its truth is taken,
because a class that declares `operator&&` and no conversion to `bool` has no
truth to take; any other operand asks where its truth would end the reading,
which is the one place the answer changes what is read.

**2. The fold gathered the non-member half for the four operators only a member
may declare.**  `operator_constant` passed `false` for every token, where the
expression layer's own doors pass `true` at `[]`, at `()` and at `=` on a class
operand.  13.5.3p1, 13.5.4p1, 13.5.5p1 and 13.5.6p1 leave a program no way to
write a non-member one, so the set held candidates no declaration may make:

```cpp
constexpr int operator[](C c, int i) { return 42; }   // g++: must be a member
static_assert(c[0] == 42, "");                        // folded to 42 here
```

That is one rule with two implementations disagreeing, and the fact is the
operator's alone - so it is now `OperatorCall::member_only`, which both readers
ask and which every one of the eight doors that used to spell a literal now
answers from.

**3. 5.17p1's write-back was asked before 13.3.1.2 wherever the operand was a
name.**  P opened the operator door at the exit where the left operand is *not* a
name and left the sibling exit asking `assignable` first, which throws for any
name the evaluation did not itself declare.  So `one = two`, `one += two` and the
eight other compound assignments, and `++one` and `one++`, over a class that
declares each of them, were refused as "written by a constant expression that did
not declare it" - while `counted{4} += two`, the identical call one exit over,
folded.  The shape an operand was written in is no part of 13.5p1, and 5.17p1's
write-back and 5.3.2p1's step are the *built-in* operators' own rules: the object
they need is theirs alone.  `written_operator` is now the one question both doors
ask first, over the operand a name's declaration carries - read off the
declaration rather than evaluated, because 8.5p11 leaves an object unset and it
is still a left operand the built-in operator takes - and `written_object` is
5.19p2's, asked only after 13.3 has named nothing.

**4. Two of 13.5p1's operators could not be declared at all.**
`ast_parser_name.cpp`'s `is_operator_token` held neither `<<=` nor `>>=`, which
`Recognizer::parse_operator_function_id` holds and which
`OperatorCall::overloadable` and `OperatorCall::spelling` - checkpoint P's own
tables - both name.  So the recognizer read the translation unit and the parser
did not, and `int operator<<=(C, C)` was `is not a translation unit`: a set the
checkpoint gathers for 38 operators had 36 that could reach a declaration.  Both
oracles accept the declaration.

### What the review confirmed rather than found

**The three carried siblings hold, and hold byte-for-byte.**  `reads(7)` into a
place of class type, `makes(9)` out of one, a call written on a temporary, and a
class template's nested aggregate clause read in a declaration statement each
fold to LowIR identical to `pa21/cppgm++-ref`'s.

**The set is still gathered per reading and kept nowhere.**  P took the two
lookups off `model_.open_overloads()` and onto locals of the caller's; a loop of
16000 operator calls holds no set the model keeps, and its peak RSS matches the
same calls written with parentheses to within 0.2 MB.

**13.3.1.2p2's cheap test is still cheap.**  A fold of built-in arithmetic pays
two type-kind reads and no lookup: a 1e5-pass loop over `+` and `<` is unchanged
at 0.21 s, and the lookup finding 1 adds is paid once per short-circuit and by
nothing else.

**Valgrind is clean** over the group's course fixture, the four probe programs
each finding was found by, and every scaling probe this review wrote.

### Recorded, not landed

**13.5.7p1's `++` and `--` over a class operand have no reference answer.**  With
finding 3 landed, `(++one)` and `(one++)` name the declaration here and in g++;
`pa21/cppgm++-ref` refuses both with `static_assert unevaluated` and misprints the
operand as `(++)`.  So no `.ref` can pin them and the course fixture declares them
and names them from nothing.  Beside them the reference refuses a folded `,` over
class operands, a free unary `operator+`, and a free `operator+` over two
enumeration operands, all three of which g++ and this build fold - the same
`static_assert unevaluated` limitation the plan already records for a loop of
overloaded operators.

**3.4.2's namespaces for the right operand of `&&` and `||` are the one candidate
source a fold cannot ask.**  Naming them means reading the operand 5.14p1 leaves
unread, so an `operator&&` declared only in a namespace associated with the right
operand's type - and reachable by neither the left operand's class, the
unqualified lookup, nor 3.4.2 for the left operand - is missed where the left
operand decides.  Every other shape of 13.3.1.2p3 is reached.  The expression
layer has both operands' types and does not have this gap.

**A compound assignment over a class that declares none still reads its operand
as a number.**  With no `operator+=` chosen, `binary_value` is handed the object's
bits, which for a class constant are the identifier of an interned list.  It
refuses them - `promote` has had no class arm since the S+O audit - so the answer
is a diagnostic and not a wrong number, and closing it properly is 5.17p7's own
sentence rather than this group's.

## Changes

- **`sema_operator.h`, `sema_operator.cpp` - `OperatorCall::member_only`**:
  13.5.3p1, 13.5.4p1, 13.5.5p1 and 13.5.6p1 as one answer about the operator,
  which the expression layer's eight doors and the fold's one now all ask
  instead of spelling a literal each.
- **`sema_constexpr.cpp` - `reaches_operator`**: 13.3.1.2p3 asked of one operand
  and asked only whether it reaches a declaration, which is what lets 5.14p1
  leave the other unread; `binary_constant` asks it before evaluating the right
  operand of `&&` and `||`, and `overloadable_place` is 13.3.1.2p2 asked of a
  declared type rather than of a value.
- **`sema_constexpr_statement.cpp` - `written_operator`, `named_object`,
  `written_object`, `held_constant`**: 13.5p1 asked before 5.17p1 and 5.3.2p1
  at both doors, so an assignment, a compound assignment and an increment name
  the same declaration whether the operand is a name or a temporary; the
  built-in operators' object is looked for only after 13.3 has named none.
- **`ast_parser_name.cpp` - `is_operator_token`**: `<<=` and `>>=` read as
  `<<` and `>>` already were, which is what the recognizer's own list has held
  all along.

## Performance Evidence

Best of three per shape, alternating between the binaries.  The gated build is
this review's own first cut, which asked the lookup only for an operand of class
type and so left finding 1's mirror half open.

| shape | this build | pre-P `afb06571` | `pa21/cppgm++-ref` |
| --- | --- | --- | --- |
| a fold loop of 1e5 passes over built-in `+` and `<` | **0.21 s** | 0.21 s | - |
| the same with a class-operand `&&` in the condition | **0.31 s** | refuses the fold | - |
| a `\|\|` that short-circuits on all 1e5 passes | **0.36 s** (gated: 0.33 s) | refuses the fold | - |
| 1000 / 4000 / 16000 folded `operator+=` calls on named class operands | **0.01 / 0.05 / 0.26 s** at 9.8 / 19.6 / **58.9 MB** | refused outright | - |
| the same calls written with parentheses | 0.01 / 0.05 / **0.24 s** at 9.8 / 19.7 / **58.7 MB** | - | - |
| the same loop declaring the operands and calling nothing | 0.00 / 0.02 / **0.07 s** at 6.6 / 7.3 / **9.7 MB** | - | - |
| a chain of 100 / 200 / 400 nested `&&` over a class operand | **0.00 s** at 6.25 / 6.34 / 6.47 MB | refuses the fold | - |
| 4000 short-circuits with 0 / 8 / 32 / 128 declarations of `operator&&` in scope | **0.01 / 0.01 / 0.02 / 0.02 s** at 5.9 / 6.2 / 7.0 / 8.6 MB | - | - |

The second and third rows are what finding 1 costs: one lookup per short-circuit,
0.03 s across 1e5 of them, and nothing at all on a fold that goes on to read the
right operand - which is why row one is unchanged.  Row four against rows five
and six is finding 3: the write-back exit now pays the call path's own cost and
not a byte more, and the memory is the argument lists the fold interns, which the
same call written with parentheses interns too.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **97 / 137**: the 40
  failures the turn started with, byte-identical, and the course fixture this
  review added.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- One course fixture added, byte-identical to `pa21/cppgm++-ref`: it pins
  5.14p1 and 5.15p1 over a class operand, an `operator&&` an `int` left operand
  reaches, `=` and `+=` written on a name and on a temporary alike, and
  `operator<<=` and `operator>>=` declared and named.  All seven course fixtures
  pass.
- Seventy operator shapes - a member and a non-member declaration of each of
  13.5p1's binary operators, the four unary ones, the subscript, the call, an
  enumeration operand and one 3.4.2 alone reaches - swept through
  `pa21/cppgm++-ref` and g++.  Every one now agrees with g++; the four the
  reference alone refuses are its own `static_assert unevaluated` limitation.
- `valgrind -q --error-exitcode=9` over the course fixture, each finding's probe
  and every scaling probe: **clean**.
