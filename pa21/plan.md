# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Two ownership lines carry the whole assignment:

- **Constant evaluation** (`sema_constant.cpp`, `sema_constexpr.cpp`,
  `sema_constexpr_object.cpp`, `sema_constexpr_statement.cpp`,
  `sema_address.cpp`, `sema_value_expression.cpp`). `SemaConstant` is
  `{TypeId, bits, real, object, valued}`: 3.9.1p8's two kinds of arithmetic
  value, and for an object of class or array type the bits are the identifier
  of the interned list its subobjects hold. PA21 needs typed constant values —
  floating and array are **done, checkpoint F**, class object **checkpoint O**,
  pointer and reference **checkpoint R** — statement execution inside a
  constexpr body (**checkpoint S**), and one engine shared by ordinary
  semantics, template arguments and `static_assert`. What a constant is worth where a place asks for a *number*
  is `ConstexprReading::at_arithmetic_place`, which is 5.19p3's converted
  constant expression and the one door every arithmetic reader comes through;
  `ConstexprReading::counted` is the narrower door the places that count
  objects use, because 14.1p4, 7.2p5, 8.3.4p1, 9.6p1 and 6.4.2p2 each ask for
  an *integral* constant expression and no floating value is one.
  `sema_constexpr_object.cpp` owns both directions of the list an object of
  class or array type is: `subobjects` says which subobjects there are and in
  what order — 12.6.2p10's, 10p1's base class subobjects before 9.2p13's
  members, which is the order 9.2p13 lays out, so one index is what a member
  access, a base conversion, an address path and an image all use — and
  `member_path` is 10.2's lookup read back against it. Which of 8.5's
  initializations a subobject takes is its *declared type*'s answer and not the
  walker's: an array member holds a list exactly as a class member does, at a
  mem-initializer as much as at a clause, and 8.5p16's one value filling a place
  of class type is `at_class_place` wherever it is written. **Done — checkpoint
  B and its audit.**
- **Static-storage lowering** (`sema_analyzer.cpp` `record_storage`,
  `sema_lifetime.cpp`, `lowir_local_static.cpp`, `lowir_image.cpp`).
  Block-scope `static` objects were refused outright; 6.7p4's guard, 3.6.2's
  constant initialization and 3.6.3p3's `__cxa_atexit` registration are PA21's.
  **Done — checkpoint L.** `lowir_image.cpp` is the other half's one owner:
  3.6.2p2's *image* — the fold of an initializer written outside every body,
  the items a structured image is laid out as, and the constant the analysis
  already folded the object to. Nothing in it writes an instruction; the
  initialization the program *runs* stays beside the definition that asked for
  it, and which definitions the image then owes is `owe_folded_construction`'s
  one question: whether working the image out went through the definition
  8.4.2p1 gives a constructor the class declared. **Done — checkpoint I and its
  audit.**

Beside them, one line that is deliberately *not* an owner: which declaration a
constant expression runs is 13.3's and 14.8.2's answer, and the fold asks for it
rather than keeping a ranking of its own — `ConstexprReading` builds one
`AnalyzedValue` per constant and hands it to the same `select_overload` for a
call and to the same `conversion_match` for 12.3.2p1's conversion function, and
`called_name` is the one reading both the tree and the flattened-spelling door
ask, and 13.3.1.2p3's set for an operator is `sema_operator.h`'s `OperatorCall`,
which the expression layer and the fold gather from alike. Whether the
declaration chosen is a constexpr one this unit defined is asked *after* 13.3
has chosen and never used to choose. Which of the two readings an operator
expression *has* is the question in front of that one, and it is settled by the
operand types alone — not by the values a fold came to, not by the shape the
operand was written in, and not over a set holding declarations 13.5 lets no
program write. **Done — checkpoints T and P and both their audits.**

Beside them, one owner that is neither: 7.1.5 asks two different questions and
only one of them is a fold. What an expression *comes to* is
`sema_constexpr.cpp`'s; whether the program was allowed to write the declaration
at all is `sema_constexpr_declaration.cpp`'s `ConstexprRequirement` — 7.1.5p3's
literal return and parameter types, p4's initialized members and base
subobjects, p9's literal type and constant initializer, over 3.9p10's answer,
which is a fact of the *class* settled where 9.2p2 completes it beside 12.1p5's
triviality and 8.5.1p1's aggregate. 7.1.5 is written about a *declarator*, so
the requirement stands at every walk that reads one: an ordinary function
definition, a constructor, a destructor, a conversion function and an object,
with 12.1p1's missing return type the only thing that differs between them. Two
facts of the class carry 3.9p10: `literal_class` is 3.9p10 itself, and
`valued_class` is that walk read again over the narrower set `SemaConstant`
actually holds — and 12.1p5's own question about the default constructor the
standard defines is the same walk once more with 7.1.5p4's last bullet added,
which is what gates the fold rather than the type. Whether a failed fold found
the program's error or ran out of this build's values is settled at the refusal
and not at the declared type: `NotConstant::covered` is 5.19's answer about the
program, and a refusal this reading makes for a value kind, an operator or a
dialect it has none of is not one, and `SemaEntity::covered_constant` carries
that answer to the next name that reaches the declaration. **Done — checkpoint
V and its audit.**

Beside them, one sentence 3.2p2 owns at the *name* rather than at either
layer: 9.4.2p3's static data member the class initialized and no definition laid
storage out for is a constant the program knows, so `SemaAnalyzer::named_value`
asks 5.3.1p3 before it reads one — a read is the value the fold came to and `&`
is the storage — and for 5.19p2's address, which no number holds, what the name
is worth is the brace-or-equal-initializer the class wrote, read again where the
name stands. Beside it, 14.6p8 is the same question for a *pattern*: a reading
that stood a value in for something an argument list settles has arrived at
nothing, so 8.3.4p1's bound, 7.2p1's enumerator and 7.1.5p9's declaration each
carry that answer rather than the value they computed from it — 7.2p1's
successor among them, which writes no constant-expression of its own and is
worth what this reading knew of the enumerator before it. 4.2p1 is the same
shape one layer down: a name of array type has no value for any operand to wait
on, so what it is worth is which object it is, asked at `entity_constant` and
converted by each reader, and 3.2p2 is one sentence in every dialect. **Done —
checkpoint A and its audit.**

Beside them, one owner the values gained last: 5.19p2's address constant is
`sema_address.cpp`'s, and it is the one kind of constant that is not a value at
all. `ConstantAddress` is *which object* — the declaration whose storage it is,
2.14.5p8's literal that no declaration named, or 12.2p1's temporary interned by
what it holds — plus the path of subobject indices down to the one designated,
which indexes exactly the interned list a constant of class or array type
already holds. `AddressTable` gives each of them one number so an address is one
interned constant, with zero 4.10p1's null pointer value; `SemaConstant::object`
is the same fact one step earlier, 3.10p1's glvalue the expression designated,
and `::valued` says whether that object has a value this reading holds — a
`static int n;` has an address and no value. The two questions never meet: no
conversion brings an address to a number, and the only place they touch is
4.12p1's `bool`. `SemaEntity::address` is where a *binding* names an object some
other declaration owns — 8.3.2p1's reference bound to an argument, 9.2p1's
member of the object a call was written on — and it memoises a declaration's own
address beside that. What an initializer-clause comes to where the place it
fills may want the object rather than the value is `operand_constant`, which is
8.5's one operand reading and stands at every place 8.5 fills one from — 4.2p1's
array, 4.3p1's function and 8.3.2p1's reference each read no value because there
need be none — and the lvalue walk it falls back to never re-runs the value
reading that has already refused. Whether a refusal about such an operand is
5.19's answer about the program is `covered_object`, which is checkpoint V's
`covered_constant` read through the object an address designates. **Done —
checkpoint R and its audit.**

Beside them, one small owner of its own: 15.4's exception-specification is a
typed fact of a declaration — `SemaEntity::nonthrowing`, settled by
`sema_noexcept.cpp`'s reading of a declarator and by the implicit walks in
`sema_class.cpp`. That file owns both halves of 15.4: the condition 15.4p1
folds, which for a member is folded where 9.2p2 completes the class rather than
where the declarator stands, and the reading of that fact 5.3.7 needs, which
asks it of the *lines that name a declaration* in the resolved tree. **Done —
checkpoint N.**

Beside them, one line the fold has no owner of at all: three questions the
*expression layer* already answers, which a reading that asked them a second
time answered differently. 3.3.10p2 hides a class or an enumeration behind a
variable, a data member, a function or an enumerator of that name declared in
the same region, in either order, and 3.4.4p2 leaves it reachable through an
elaborated-type-specifier alone — so what tells 5.2.3's cast from 5.2.2's call
is 3.4.1's ordinary lookup and `LookupKind::Type` is 3.4.4p2's question and no
other, asked at `elaborated` and nowhere else. 14p1 declares no function until
a template is instantiated, so a reading of a *pattern* holds the definition of
nothing the pattern declares — a member of the class it describes, or a
specialization made over a dependent argument — and `unsettled_callee` is that
one sentence, which 14.6p8 stands a value in for rather than calling the
missing body the program's error. And 5.1.1p13's third bullet is the one place
a non-static data member is named with no object at all: an unevaluated operand,
which `unevaluated_` is the depth of, taken at each of the three doors that open
one — 5.3.3p1's `sizeof`, 7.1.6.2p4's decltype-specifier and 5.3.7p1's
`noexcept` operand. Beside them, 14.6p8's stand-in is worth nothing however the
reading ends: a place that reads *through* it runs out, so every place that
decides something carries that answer on a refusal as much as on a value, and
`ConstexprReading::counted_where` is the one reading 8.3.4p1's bound, 7.2p1's
enumerator, 9.6p1's width and 7.6.2p1's alignment all count by. **Done —
checkpoint M, its groups H, T' and U, and its audit.**

Beside them, one shape of the fold's own walk: 8.5.1p2 is written about the
*subobjects* and not about the clauses, because 8.5.1p11 lets the braces around
a subaggregate's own clauses be left out — so how many clauses a subobject takes
is what its own walk arrives at, and the argument is a cursor rather than a list.
`aggregate_constant` is that walk, `subobject_constant` is one step of it, and
`list_constant` is where the two questions only the place the braces stand can
answer are asked: 8.5.2p1's string literal for a whole array of character type,
and 8.5.1p6's clause that reached no subobject. Every place a list stands asks
the one reading — a declaration's initializer, a clause of an enclosing list,
12.6.2p2's mem-initializer and 12.6.2p8's brace-or-equal-initializer — and
8.5.1p11's own question about a subobject of class type is `elides_its_braces`,
which is the analysis's, borrowed rather than restated. Beside it two sentences
about what a value *is not*: 5.2.9p4's cast to cv `void` is a discarded-value
expression, evaluated for its writes and holding nothing any reader may take, so
a cast of a `void` operand to anything else reaches no value at all; and 1.4p8's
`__builtin_expect` is the one reserved function whose definition the
implementation states here — `long (long, long)` handing back its first operand,
folded where `call` reads it and lowered with no call boundary, found by the one
`reserved_function` door both the expression layer and `callee_candidates` ask.
**Done — checkpoint E.**

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed — but it folds a
conversion function only where the place is an *initializer* or 4p3's contextual
`bool`, and refuses one written at an enumerator, an array bound or inside a
`sizeof`, where g++ and this build fold it; and it accepts a floating enumerator
and a floating array bound 7.2p5 and 8.3.4p1 refuse, where g++ and the standard
agree with this build. Its floating images
are `%.20g` at the object's own width with 2.14.4p1's suffix for a scalar, and
the digits the program wrote for a clause of an aggregate; both were probed and
both are reproduced, `inf`, `-inf` and `-nan` among them. Three shapes it has no
answer for at all, so a fixture cannot be written from it and g++ is the
oracle: an out-of-class
definition of a `static constexpr` data member of a *non-template* class
(`mismatched variable declaration`), a namespace-scope array of scalars whose
initializer needs a startup body (`unsupported global array initializer`, valid
program or not), and a static data member two units of one invocation both
define — which it emits twice into one module. Where 5p4's overflow
or 4.9p1's out-of-range conversion makes a program undefined, g++ refuses it and
the reference folds it — this build folds it to the reference's values, because
the `.ref` files are the oracle and no fixture asks for the refusal.

3.3.10p2 is a fourth place it answers a clause of its own: the reference reads
*every* name written where a type may stand as 3.4.4p2's lookup does, so a class
a function of that name hides is still the class to it — `enum tone { low };
constexpr int tone(int);` makes `tone(4)` a cast there and a call in g++ and
here, `S obj;`, `static_cast<S>(3)`, `new S`, `operator S()`, an enum-base and a
template argument each name the hidden class there and are refused by g++ and by
this build alike, and `sizeof(S)` is the class's size there where g++ refuses the
program and this build measures the function's return type. Only the first of
those is a fold, and no fixture reaches any of them; the sweep judged each
against g++ rather than copying the reference. Beside it the reference refuses
`decltype(S::keys)` outright (`failed to resolve member id-expression`), where
7.1.6.2p4's first bullet gives it the member's declared type and g++ and this
build agree — so 5.1.1p13's fixture is written over `sizeof` alone.

## Current Failure Map

**162/166**; 4 failures, and none of them refuses a program the assignment asks
it to translate — every remaining row is a LowIR difference. Groups N, T, I, P,
V, R, C, B, A, M and E are all closed with their audits. The four LowIR rows are
unchanged — two are known gap L's symbol naming, one is I' below, and one is the
row checkpoint P reached; the four course fixtures the E audit added pass.

| Group | Shape | Count |
|---|---|---|
| I'. a dead `@__strlit__` | `300-function-local-static-array-guard` differs by one global: for `static const char nested[1][2] = {"x"};` the ref emits the literal's own object beside the array that copied it. The boundary was probed and is the reference's own: it materializes the literal where 8.5.2 initializes an array that is an *element* of an enclosing array (`char two[2][2] = {"m","n"}` gets two), and not where the array is the whole object (`char flat[2] = "y"`) nor where it is a *member* of a class (`struct S { char a[2]; }; S s = {"q"}`). 2.14.5p8 makes the object exist in all four | 1 |
| misc | the two symbol-naming rows of gap L and the owed-constructor row of gap P | 3 |

Four groups beside them that no fixture fails on, all found by checkpoint E's
sweep and its audit's, and each one layer off the walk E landed. **E'. the array
a *declaration*
names elides no braces**: `array_from_clauses` takes one clause per element with
no 8.5.1p11 question, so `int g[2][3] = {1,2,3,4,5,6};`, `P a[2] = {1,2,3,4};`
and `A() : e{1,2,3,4}` over `int e[2][2]` are each `an array initializer has more
clauses than the array has elements` — the analysis's refusal and not the fold's,
which is why a class whose *member* is that array (`struct A { int e[2][2]; }; A
a = {1,2,3,4};`) is accepted here one line away. The reference refuses all three
itself (`too many array initializer elements`) where g++ folds them, so no `.ref`
can pin the acceptance, and the walk that would close it writes its elements
inline under one line rather than under `open_subobject` steps — a different
emission shape from the one `aggregate_subobject` already elides in. **E''. a
braced-init-list standing where an *operand* does**: 8.5.4p1 makes the list the
initialization of the place it fills, so its meaning is the place's type, and
`evaluate` has no node for one at all — `return {4,5,6};` from a constexpr
function, `f({4,5,6})`, `int f(A a = {8,9})` and `H<f({7,8})>` are each `a
constant expression holds a construct PA11 does not evaluate`, where `clause_of`
answers all four the moment the type is threaded to `operand_constant`. g++ folds
all four; the reference folds the return and the default-argument and refuses the
argument and the template argument. **I''''. a mem-initializer of a member of
*class* type takes no image**: `struct P { int x; int y; }; struct A { P p;
constexpr A() : p{1,2} {} }; constexpr A a;` folds — a `static_assert` reads
`a.p.y` — and lays out `zero 8` with a startup body where the reference and g++
both write `i32 1, i32 2`, while the same constructor over a member of *scalar*
type lays out. It is `lowir_image.cpp`'s walk of a mem-initializer and not the
fold's, which is the same owner the three V gaps below have. **U'. a union built
by a constructor is neither folded nor laid out** (found by the E audit): `union
U { int a; double b; constexpr U(double v) : b(v) {} }; constexpr U u(1.5);`
leaves `u.b` `an object it holds no value of` and lays out `zero 8` with a
startup body, where both oracles fold the object and write `f64 1.5` —
`object_from_constructor` reads every member of the class in turn and a union's
other members have no mem-initializer to read, which 9.5p1 says they may not
have. The fold's *aggregate* union is right in both directions since the E
audit: it holds the member 8.5.1p15 initialized and refuses a read of any other.

Beside them, seven shapes checkpoint E now folds that `pa21/cppgm++-ref` refuses
outright, each of them one g++ folds too, so the course fixtures are written
around them: 8.5.1p11's elision two and three levels down (`struct A { P p; int
t; }` over a `P` holding `int e[2]`), an elided member of union type, a
brace-or-equal-initializer of class or array type read for a `constexpr` object,
a mem-initializer whose braces list-initialize an aggregate member, an
out-of-class definition of a `static constexpr` member of array type, and a
mem-initializer holding a string literal. The reference calls all seven
`unsupported constexpr variable initializer`.

Two shapes beside them where this build and the reference write different LowIR
for the same bytes, both found by the E audit and neither one a fixture reaches.
**Z'. a value-initialized tail is one span**: `int t[3] = {7};` lays out `i32 7,
zero 8` where the reference writes two `i32 0`, and `char s[4] = "ab";` the same
one byte over — it is the image's own writing, in every dialect and for a plain
declaration, so it is neither the fold's nor a checkpoint's. **E'''. the branch
hint's operand keeps its own width**: 1.4p8's call computes nothing, so what a
body lowers is the operand's reading, and where the operand is not already
`long` this build converts it to the declared parameter type and the reference
does not — `pa21/cppgm++-ref` writes `binary sub i64` over a `u32` operand and
`return i64` of an `i32` value. g++'s answer for the value needs the conversion,
so the fixtures are written over `long` operands.

Three gaps beside them that no fixture fails on, each belonging to an earlier
group. **I'''. a pointer whose second fold over the dump stops takes no image**
(found by the A audit): checkpoint I's rule is that where the walk of the dump
lines cannot re-fold the initializer the analysis's own answer stands, and
`SemaEntity::value`/`::real` are numbers, so a *pointer* has nothing there to
stand — `constexpr int *p = true ? &one : &two;` and one initialized from
another constexpr pointer each take `zero` and a startup body where the
reference writes `addr @one`, and a null pointer read through a name is written
`= 0` where 4.10p1's own image is `= zero`. It is `global_address`'s question.
The two below it were found by the R audit. **I''. the definition of an implicitly
declared default constructor an image was folded through**: `struct held { int
inside = 4; }; constexpr held h;` lays out the reference's image and emits no
`@held__held`, which the reference does — `owe_folded_construction` reads
`implicit_declaration` as 8.5.1's aggregate, which a class 12.6.2p8 leaves a
constructor to run is not. **L''. 3.5p3's `extern` before a `const`
definition**: `extern const int k; const int k = 5;` is `binding=internal` and
`_ZL1k` here and `binding=strong` and `_Z1k` in the reference, which 3.5p3
agrees with; the same program with no `extern` is identical in both.

## Next Checkpoint — G: the clauses an array takes where it stands, and the list standing where an operand does

**Owner.** `sema_init_list.cpp`'s `array_from_clauses` and the count
`list_initialize_into` takes in front of it; `sema_constexpr.cpp`'s
`operand_constant` and `evaluate` for a `BracedInitList` node.

**Why it is next.** Checkpoint E made 8.5.1p11 one question the *fold* asks per
subobject, and the analysis still asks nothing: `array_from_clauses` takes one
clause per element, so `int g[2][3] = {1,2,3,4,5,6};`, `P a[2] = {1,2,3,4};` and
`A() : e{1,2,3,4}` over `int e[2][2]` are each refused where g++ translates them
and where a class whose *member* is that array is accepted one line away —
`aggregate_subobject` already elides for a member, so this is one rule with two
implementations and the wrong one is reached by the shape a program is most
likely to write. Beside it, 8.5.4p1 makes a braced-init-list the initialization
of the place it fills, so `return {4,5,6};` from a constexpr function, `f({4,5,6})`,
`int f(A a = {8,9})` and `H<f({7,8})>` are a construct the fold has no node for
at all, where `clause_of` answers every one of them the moment the place's type
is threaded to `operand_constant`. Both groups refuse programs the assignment
asks the compiler to translate, which the four remaining LowIR rows do not.

**Data flow.** The array walk takes a *run* of clauses per element rather than
one, which is the cursor `aggregate_elements` already walks with: an element of
array type with no braces takes what its own walk arrives at, an element of
class type takes `elides_its_braces`' answer, and 8.5.1p6's refusal moves from
the count in front of the walk to what the walk left — which is what
`list_constant` does for the fold. What to distrust: the analysis writes its
elements *inline* under one line where `aggregate_subobject` writes them under
`open_subobject` steps, so the emission shape of an elided element is the one
thing to settle first and the images of every array declaration are what would
move; 8.3.4p3's deduced bound is read before the walk and an elided list changes
what it deduces; and the operand door threading a type reaches `at_class_place`,
`array_of` and 8.5.4p7's narrowing, each of which already has an answer for a
list that arrived as a declaration's initializer.

**Expected complexity.** One cursor and no second pass: the walk is the one the
fold already makes, the elision question is one scratch-node probe per subobject
as it is there, and no clause is read twice.

**Validation.** `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` and
`make test-report-through-pa20` — the array images every earlier stage writes
are the regression surface here, not pa21's. `pa21/cppgm++-ref` refuses both
groups (`too many array initializer elements`, and an argument or a template
argument holding a list), so g++ is the oracle and the course fixtures have to
be written around what the reference will not translate; a multiplicity and
nesting sweep over the elided shapes, and a valgrind pass.

## Performance Model

| Path | Shape | Measured |
|---|---|---|
| constexpr loop | one pass per iteration; a block's region, the objects it declares and the names it introduces are each made once per fold and reused, so n iterations cost O(n) statements and O(1) declarations | `for` of 1e3 / 1e4 / 1e5 passes with a body-local declaration: 0.00 / 0.02 / 0.18s, peak RSS 7.11 / 7.00 / 7.05 MB (flat). A body-local `typedef` beside it at 12800 / 51200 / 102400 passes: 0.03 / 0.09 / 0.19s at 6.78 / 7.20 / 6.81 MB. Ref: 0.54 / 0.60 / 4.01s |
| constant object | one fold per declaration, and one interned list per distinct object; a constructor called twice with one argument list is one walk | 500 / 2000 / 8000 `constexpr` two-member objects, each read back by a `static_assert`: 0.03 / 0.10 / 0.45s (ref 0.64 / 0.99 / 3.77s). A chain of 20 / 40 / 80 nested class members: 0.00 / 0.00 / 0.01s — linear in depth, not 2^depth |
| arithmetic place | one type-kind test per reading, and where the constant stands for an object one ranking of the class's conversion functions - 13.3.3.1.2's, over the set `gather_conversions` walks once per class that declares any | 1e3 / 4e3 / 16e3 folds reaching an `int` place through a conversion function: 0.02 / 0.10 / 0.44s at 13 / 34 / 117 MB. The same with 17 conversion functions declared and one of them viable: 0.03 / 0.12 / 0.52s at the same memory - 18% for 17x the candidates, which is 13.3's own walk. The reference refuses both |
| a call written as a template argument | one lookup of the flattened word per argument - 14.2's specializations, then the ordinary lookup, then 3.4.2p2's - and one 13.3 ranking, the same reading a call written as a tree asks | 500 / 2000 / 8000 arguments each holding a call: 0.04 / 0.20 / 0.93s at 17 / 49 / 177 MB, of which asking 14.2 first is 7% (ref 16.59s at 216 MB). The same where 3.4.2 names the callee: 0.05 / 0.23 / 1.05s at 19 / 55 / 201 MB (ref 45.43s at 225 MB) |
| a callee 3.4.2 names | one walk of the argument types' associated regions per call, by reached region, with no set kept between calls | a fold loop calling one such function 1e3 / 4e3 / 16e3 times: 0.02 / 0.08 / 0.37s at 11 / 26 / 85 MB, against 0.01 / 0.03 / 0.15s at 8 / 16 / 43 MB for one the ordinary lookup names - 2.4x one lookup, linear in the calls and with no term in the size of the program |
| local-static symbol | one flatten per declaration, memoised in `entity_symbols_`; a name used *n* times costs one flatten and *n* lookups | 400 / 1600 / 6400 image-initialized statics in one body: 0.01 / 0.05 / 0.22s (ref 0.584 / 0.743 / 1.403s) |
| local-static guard | one image read per declaration, one guard global per object | 400 / 1600 / 6400 guarded statics: 0.026 / 0.096 / 0.423s (ref 0.632 / 0.933 / 2.229s) |
| array destruction | one `__cxa_atexit` per array, handed a generated body that walks the elements — written out below `kArrayLoopLimit` and a loop above it | 100000-element array of class type: 0.005s, 11 instructions |
| floating value | one `long double` per constant, and one pool entry per *distinct* value - a value met twice is one index, so a memo key stays stable without the pool growing, and each of the four non-finite values keys to a place of its own | a constexpr loop of floating arithmetic at 1e3 / 1e4 / 1e5 passes: 0.00 / 0.03 / 0.27s, peak RSS a flat 6 MB. 2000 / 8000 / 32000 declarations of *distinct* floating constants each read back by a `static_assert`: 0.04 / 0.20 / 0.88s at 15 / 42 / 153 MB |
| array constant | one interned list per array, one entry per element, and 8.5p7's value-initialized tail interned once and repeated | 1000 / 4000 / 16000 elements read back by a `static_assert`: 0.00 / 0.02 / 0.07s at 6 / 9 / 18 MB - linear in elements. `constexpr int a[1000000] = {1};` is 0.10s and 13 MB, because the tail is one entry and not a million folds |
| chosen callee | one lookup and one 13.3 ranking per call the fold evaluates, over an argument list built once - the same pass the expression layer pays, and no `open_overloads` set kept for the model's lifetime | a `for` calling one constexpr function 1e3 / 4e3 / 16e3 times: 0.01 / 0.04 / 0.16s against 0.01 / 0.04 / 0.15s for the arity ranking it replaced (ref 0.78s at 16e3), and the same loop with a template-id callee 0.01 / 0.04 / 0.18s and a deduced one 0.01 / 0.04 / 0.16s (ref 1.64s). A call site with 8 / 32 / 128 candidates at 4e3 calls: 0.04 / 0.05 / 0.10s - linear in candidates, which is 13.3's own cost, and a shape the arity ranking refused outright. A member call on a constant object 16e3 times is 0.05s at a flat 7 MB |
| function parameter pack | one place per element of the run, named the way 14.5.3p4's own reading looks them up, so a `pattern...` in the body costs one reading per element and no scan | `plus(1,...,1)` at 16 / 32 / 64 arguments: 0.00 / 0.01 / 0.03s at 7 / 9 / 18 MB (ref 0.62s at 27 MB at 64). The n^2 is the idiom's - n specializations of n places - and not a repeated walk |
| `noexcept` operator | one reading of the operand per operator and one walk of the resolved tree it left, asked of the lines that name a declaration; a `noexcept` written inside another is read by the reading of the outer operand and not again, so nesting is linear in depth | 500 / 2000 / 8000 declarations each holding three `noexcept` operators over a member call: 0.04 / 0.19 / 0.78s at 17 / 50 / 183 MB (ref 0.62 / 0.92 / 2.97s at 20 / 38 / 109 MB). One operand of 500 / 2000 / 8000 calls: 0.01 / 0.03 / 0.13s at 8 / 14 / 36 MB (ref 0.61 / 1.59 / 0.86s). 50 / 100 / 200 nested `noexcept`: 0.00s at 6.3 / 6.5 / 7.0 MB (ref 0.53s at 14-16 MB) - 800 deep is refused by the parser's own depth limit, which the reference has not got |
| exception-specification condition | one fold per declaration that wrote one, and a second only where 9.2p2's complete-class context left the first unanswered - kept on the class's region and folded at the close of the class-specifier, in every dialect | 400 / 1600 / 6400 members whose condition names a member declared *below* it: 0.01 / 0.04 / 0.21s at 9 / 16 / 44 MB, against 0.00 / 0.02 / 0.08s at 7 / 11 / 25 MB for the same count writing `noexcept(true)`, which defers none (ref 0.63 / 1.79 / 26.78s at 19 / 33 / 90 MB) |
| a static data member's image | one reading of the class's brace-or-equal-initializer per definition and one item per scalar subobject, for every declared type and not only the ones an object is built in; the definition takes the initializer rather than folding a second time | 1000 / 4000 / 16000 two-member class elements of one `static constexpr` array member, read back by a `static_assert`: 0.01 / 0.05 / 0.20s at 8 / 15 / 42 MB (ref 0.60 / 0.74 / 1.32s at 21 / 39 / 110 MB) — linear in elements |
| an image a constructor call leaves | one read of the constructor's definition per call, and per *place* it binds one walk of the argument: whether 5.2.2p1 makes it work the program runs, and what it is worth at each type a member reads it at. A place carried into n members is one walk and not n, which is what keeps the cost off the product of the member count and the argument's size | `constexpr P p(<expr>)` over a class of n members each initialized from that one place: 6400-term expression at n = 3200 is 0.14s at 44 MB, against 4.55s at `1301e41b` and 0.94s in the reference; 1600 terms 0.10s (was 1.05s), 400 terms 0.08s (was 0.29s). Output byte-identical |
| a clause that holds a call | one walk of the clause per clause, at each of the three walkers an image is laid out by — a class's subobjects, a constructor's member initializations, an array's elements — so an object 5.2.2p1 builds is one answer wherever it is written | 2000 / 8000 / 32000 clauses of an array member: 0.05 / 0.22 / 0.90s at 22 / 66 / 243 MB against 0.04 / 0.16 / 0.63s at 13 / 33 / 113 MB for the same clauses holding no call (ref 3.96s and 2.34s). The same count as a whole array of scalars, which the third walker now refuses too: 0.05 / 0.20 / 0.80s at 19 / 56 / 204 MB against 0.01 / 0.03 / 0.11s laid out as data — the startup body's n stores, the same n the class's clauses have always paid, and linear either way. The reference refuses to lower an array of scalars that needs one at all |
| a folded list of clauses | one reading per clause down the object, so a list is linear in the clauses it holds and carries no term in how deep the nesting is | a class nested 8 / 10 / 12 deep with two members at each level, written out — 256 / 1024 / 4096 leaf clauses: 0.00 / 0.01 / 0.03s at 6.8 / 8.3 / 13 MB (ref 0.77s at 39 MB at depth 12) |
| 8.5p7's value-initialized object | one walk per *class*, held on `SemaModel::value_initialized`, so a class whose two members are themselves such classes costs one walk per level and not one per path | measured against the shape that shows it: a class nested 12 / 16 / 20 / 24 deep with two members at each level. The fold is now O(depth); what is left is 2^depth and is **not** this layer's — `n20 deep = {};` costs the same 2.19s and 833 MB *without* `constexpr`, because 8.5.1's dump writes one `subobject-initialization` node per scalar subobject. Depth 24 is 41s and 13 GB. That is PA15-PA20's description of an aggregate initialization, and the type itself is O(depth) either way; `n20 deep;` with no initializer is 0.00s and 6.5 MB |
| aggregate of floating clauses | one dump node per clause, and one fold per clause 8.5.4p7's second bullet asks about - which is a `float` member off a wider source and nothing else | 2000 / 8000 / 32000 `double` clauses of an array member: 0.05 / 0.21 / 0.81s at 22 / 66 / 243 MB, against 0.02 / 0.09 / 0.37s at 10 / 22 / 72 MB for the same count of `int` clauses (ref 0.70 / 1.20 / 2.90s at 34 / 95 / 339 MB and 0.70 / 0.90 / 1.90s at 21 / 43 / 135 MB). Making them `float`, the one shape the bullet folds, adds 16%: 0.94s and 274 MB at 32000 |

| an operator that names a declaration | 13.3.1.2p2's one test per operand - is it of class or enumeration type - before anything is gathered, so an arithmetic fold pays two type-kind reads and no lookup; where the test says yes, one candidate gathering into a local of the caller's and one 13.3 ranking, the same pair a call written with parentheses pays | a constexpr loop of 1e3 / 4e3 / 16e3 overloaded `operator+` calls on a class: 0.02 / 0.10 / 0.45s at 12 / 29 / 98 MB - linear, and the same loop over built-in `+` is 0.00 / 0.01 / 0.04s at a flat 6.0 MB, unchanged. 4e3 calls with 0 / 8 / 32 / 128 declarations of the operator in scope: 0.10 / 0.11 / 0.15 / 0.32s - linear in candidates, which is 13.3's own walk. The reference refuses the loop outright (`static_assert unevaluated`) at 0.60s |
| an operator that leaves an operand unread | 13.3.1.2p3's set gathered from the operand a fold *has* read, asked before the one it has not is read — an operand of class type before its truth is taken, any other where its truth would end the reading, which is the one place the answer changes what is read | a 1e5-pass fold loop over built-in `+` and `<` is 0.21s, unchanged; the same with a class-operand `&&` in the condition 0.31s; a `\|\|` that short-circuits on all 1e5 passes 0.36s against 0.33s for asking nothing there — one lookup per short-circuit and none on a fold that reads the right operand anyway. A chain of 100 / 200 / 400 nested `&&` over a class operand: 0.00s at 6.25 / 6.34 / 6.47 MB. 4000 short-circuits with 0 / 8 / 32 / 128 declarations of `operator&&` in scope: 0.01 / 0.01 / 0.02 / 0.02s |
| an assignment or increment that names a declaration | one gather and one 13.3 ranking per operator, the same pair a call written with parentheses pays, over an operand a name's declaration carries rather than one evaluating it comes to | 1000 / 4000 / 16000 folded `operator+=` calls on named class operands: 0.01 / 0.05 / 0.26s at 9.8 / 19.6 / 58.9 MB, against 0.01 / 0.05 / 0.24s at 9.8 / 19.7 / 58.7 MB for the identical calls written `plus(a, b)` and 0.00 / 0.02 / 0.07s at 6.6 / 7.3 / 9.7 MB for the same loop calling nothing — linear, and the memory is the argument lists the fold interns either way |
| a class template's aggregate clause | one `type_owner` probe per clause of class type and, past the first, one integer test in `require_complete_type` - the instantiation 14.7.1p1 demands is made once and every later clause finds it | 500 / 2000 / 8000 two-member class clauses of an array member of `Holder<int>`, read back by a `static_assert`: 0.00 / 0.02 / 0.10s at 7 / 11 / 27 MB - linear in clauses, with no term in the number of specializations |
| 3.9p10's answer for a class | two readings of one walk of the class's bases and members where 9.2p2 completes it — 12.1p5's, which adds 7.1.5p4's last bullet and gates the fold, and 3.9p10's, which does not — each reading the answer the subobject's own class already carries, so a hierarchy or a nesting *n* deep costs *n* walks and not 2^n; every later reader is one `unsigned char` read | 500 / 2000 / 8000 distinct two-member classes each with a `constexpr` constructor and a `constexpr` object read back by a `static_assert`: 0.07 / 0.33 / 1.41s at 23 / 75 / 283 MB (ref 0.90 / 1.90 / 9.31s at 33 / 93 / 336 MB). 500 / 2000 / 8000 *non-aggregate* classes, each with a base and a `constexpr` object, which is the shape the second reading exists for: 0.06 / 0.26 / 1.07s at 19 / 59 / 220 MB, and refused outright at `b8bd105a`. One class of 400 / 1600 / 6400 members with brace-or-equal-initializers, folded through 12.1p5's own constructor: 0.01 / 0.03 / 0.15s at 9 / 17 / 50 MB (ref 0.60 / 0.70 / 1.10s), and the same count of members of *class* type 0.02 / 0.07 / 0.15s |
| a declaration whose fold runs out | one `NotConstant::covered` read per refused fold and one `bool` on the declaration, so a name that reaches such a declaration answers from the field rather than folding again — and the object takes 3.6.2p2's dynamic initialization, which is the whole of what it then costs | 500 / 2000 / 8000 `constexpr` declarations whose initializer reads through an address: 0.03 / 0.10 / 0.40s at 11 / 27 / 92 MB (ref 0.57 / 0.72 / 2.00s at 16 / 26 / 66 MB), against a hard refusal at the first declaration at `b8bd105a`. 500 / 2000 / 8000 `const` class objects that wrote no initializer, which is the fold 8.5p6 added: 0.03 / 0.09 / 0.17s at 8 / 15 / 43 MB, unchanged |
| 8.5p6's default-initialization | one `default_constructor` read per const object that wrote no initializer, and one fold per (constructor, argument list) - so *n* declarations of one class are one walk of its members and *n* memo hits | 2000 / 8000 / 32000 `constexpr` objects of one class with a written `constexpr` default constructor, each read back by a `static_assert`: 0.05 / 0.22 / 0.97s at 18 / 53 / 195 MB (ref 0.90 / 2.70 / 24.53s at 30 / 77 / 270 MB) |
| an address constant | one `ConstantAddress` interned per distinct (object, path), memoised on the declaration for its own address - so a name read *n* times interns once and *n* map probes cost nothing; 5.7p5's walk interns one entry per step, and the addresses of one array are the same handful however many passes reach them, so a loop is flat | a fold loop walking a pointer over 1000 / 4000 / 16000 elements: 0.01 / 0.03 / 0.11s at 7 / 10 / 21 MB (ref 1.00s at 1000 and **189.37s** at 16000). A `while` walking a 4-element array to 5.7p4's bound 1e3 / 4e3 / 16e3 times over: 0.02 / 0.07 / 0.29s at a flat 6 MB (ref 1.20s at 14 MB). 500 / 2000 / 8000 declarations each keeping an object's address: 0.01 / 0.07 / 0.29s at 10 / 22 / 70 MB (ref 9.92s at 111 MB at 8000). 500 / 2000 / 8000 declarations each folding `&x` through a `const int &` place: 0.03 / 0.13 / 0.57s at 14 / 36 / 125 MB (ref 0.80 / - / 11.92s at 24 / - / 188 MB), and 500 / 2000 / 8000 folds each binding a reference place to a *different* object 0.02 / 0.11 / 0.46s at 12 / 29 / 99 MB (ref 6.61s at 117 MB) |
| 8.5's operand | one reading per initializer-clause, whichever of 8.5's places it fills: the value reading, and where that refuses one lvalue walk of the *same* node with no second value reading in it — so an operand costs one walk when it folds and one and a bit when it does not, and a call written on a call is linear in the nesting and not 2^depth | `f(f(...(nonconst)))` 12 / 16 / 18 calls deep, refused: 0.00s at 6 MB throughout, against 0.05 / 0.67 / **2.67s** at `6d975910`. The same shape 8 / 16 / 32 / 64 deep and folding: 0.00s at 7 MB throughout, unchanged. The ten initializations of a pointer place the audit reopened, in one unit: 0.00s at 7 MB (ref 0.54s at 15 MB), refused at the first one before it |
| a subobject an expression designates | one interning per member or element *read*, beside the value the list already held - so an aggregate fold pays one map probe per subobject and no walk | 1000 / 4000 / 16000 two-member class elements of an array, read back by a `static_assert`: 0.01 / 0.05 / 0.21s at 9 / 17 / 50 MB, against 0.01 / 0.05 / 0.20s before the checkpoint. 500 / 2000 / 8000 `constexpr` two-member objects: 0.02 / 0.07 / 0.33s at 10 / 21 / 64 MB (was 0.03 / 0.10 / 0.45s). A pointer into an array nested 8 / 12 / 16 / 20 deep, whose path is that long: 0.00s at a flat 6.1-6.4 MB - linear in depth. The 2^depth at depth 20 of a *class* nested that far is 8.5.1's dump and not this: `L20 deep = {};` costs 11.15s and 1.12 GB with no `constexpr` at all |
| a base class subobject | one entry of the enclosing object's interned list per direct base, and 10.2's lookup read back as one step per class on the path — the class's own members asked before its bases, so an access costs one walk of the derivation and no search of it | a derivation 20 / 40 / 80 deep, each level with a member and a `constexpr` constructor, read at both ends: 0.00 / 0.00 / 0.01s at 6.9 / 7.4 / 8.7 MB. 500 / 2000 / 8000 objects of a class with a base, each member read back by a `static_assert`: 0.04 / 0.16 / 0.69s at 14 / 38 / 133 MB (ref 1.52s at 45 MB at 2000). 1000 / 4000 / 16000 reads of *one* base member: 0.01 / 0.03 / 0.13s at 7 / 10 / 23 MB (ref 0.85s at 53 MB). The cross product, 2000 reads of the deepest base member at depth 10 / 20 / 40: 0.02 / 0.03 / 0.06s — linear in depth, not 2^depth (ref 0.93s at depth 40). Depth 20 / 40 / 80 / 160 read at both ends: 0.00 / 0.00 / 0.01 / 0.03s at 6.9 / 7.9 / 9.7 / 12.8 MB, and 25 / 50 / 100 / 200 *direct* bases 0.00 / 0.01 / 0.02 / 0.04s. A member call on an object at depth 5 / 10 / 20 / 40, folded 2000 times with a distinct argument each pass so no memo answers: 0.05 / 0.08 / 0.14 / 0.29s — `bind_subobjects` walks the derivation once per call and not once per name |
| a subobject of array type | one interned list per array wherever 8.5 fills one — a clause, an element, a declaration or a mem-initializer — so the reading is the declared type's and not the walker's, and 8.5.1p7's tail is one entry however long it is | 1000 / 4000 / 16000 elements of one array mem-initializer, read back by a `static_assert`: 0.01 / 0.05 / 0.23s at 10 / 23 / 76 MB (ref 0.59 / 0.80 / 1.70s at 25 / 55 / 178 MB), against a refusal at the mem-initializer before the B audit. 125 / 500 / 2000 array members each writing `{1, 2}`: 0.00 / 0.02 / 0.09s at 7.7 / 13 / 33 MB |
| a place of class type filled from one value | 13.3.1.4p1's two halves asked in `match_by_value`'s order — one `converting_constructor` walk of the place's class, and only where none takes the value one `conversion_match` over the value's — so a value that is *not* of class type asks nothing new and one that is pays one extra pass over a set 13.3 has to walk anyway | 16000 folds of an `int` at a class place: 0.43s, unchanged. 16000 of a *class* value a converting constructor takes: 0.69s against 0.66s at `afbfc093` — 4%, and 4000 with 0 / 8 / 32 constructors declared 0.16 / 0.18 / 0.25s, linear in candidates. 1000 / 4000 / 16000 reaching the place through a conversion function: 0.04 / 0.19 / 0.83s at 17 / 49 / 178 MB, refused at the first one before it (the reference refuses the loop outright) |
| a name read through no storage | one reading of the member's brace-or-equal-initializer per *use* - the tree the use lowers, which is the tree the reference writes there too - and no re-fold of the declaration; the storage the member would have had is named by nothing, so the unit lays none out, and 3.2p2 reads the same way in every dialect | 1e3 / 4e3 / 16e3 reads of a `static constexpr const char *` member: 0.03 / 0.11 / 0.44s at 13 / 33 / 116 MB (ref 0.63 / 0.95 / 2.18s at 23 / 51 / 159 MB), against 0.01 / 0.06 / 0.24s at 10 / 22 / 67 MB for the same count of reads of an `int` member, which is one literal node. The same over `&arr[2]`, whose initializer is three nodes: 0.03 / 0.14 / 0.57s at 14 / 37 / 130 MB (ref 0.66 / 1.07 / 2.70s at 27 / 63 / 206 MB). A *chain* of 50 / 100 / 200 / 400 members each initialized from the one before it and each read once - n^2 substitutions, which is the idiom's own shape: 0.10s throughout at 7.1 / 7.7 / 9.6 / 15.4 MB, output identical to the reference (ref 0.60 / 0.60 / 0.70 / 1.10s) |
| a name of array type read as an operand | one `held_at` per name over the address its declaration memoises, and 4.2p1's conversion at the reader that asks for it - so an operand costs one map probe and no lookup of the spelling, and a subscript asks the declaration nothing of its own | 500 / 2000 / 8000 declarations each folding `numbers + k`: 0.01 / 0.04 / 0.19s at 8.6 / 15.5 / 43.9 MB (ref 0.58 / 0.75 / 2.99s at 18 / 33 / 96 MB). A constexpr `for` walking a pointer over 1e3 / 4e3 / 16e3 elements 0.00 / 0.02 / 0.07s and the same loop subscripting a constexpr array 0.00 / 0.02 / 0.08s, both at a flat 6-10 MB. A subscript nested 8 / 12 / 16 / 20 deep, read as an address and as a value: 0.00s at 5.8-6.2 MB throughout - linear in depth, not 2^depth |
| a bound a pattern cannot compute | one `stood_in_` read per bound, per enumerator and per declaration the pattern folds - a counter compare beside a walk the reading already made, so nothing is read twice and nothing is kept, and 7.2p1's successor reads the answer rather than the arithmetic | a variadic class folding `first_set(flags, flags + sizeof...(T))` over a pack of 100 / 400 / 1600: 0.00 / 0.00 / 0.01s at 7.1 / 8.2 / 12.5 MB, where the reference is 0.70 / 1.60s and **times out at 300s and 1.05 GB** at 1600 - the `0.10s throughout` this row carried was a per-run process spawn in the harness and not the compiler. 500 / 2000 / 8000 enumerators after one a value was stood in for: 0.00 / 0.01 / 0.03s at 6.8 / 9.1 / 18.0 MB. 100 / 400 / 1600 members of one pattern each with a dependent bound: 0.10s at 7.3 / 10.6 / 21.9 MB (ref 0.60 / 0.60 / 0.70s) |
| an array of arrays | one interned list per row and one per column, with 8.5.1p7's value-initialized tail interned once *per level* - so a partly written array costs the clauses it wrote plus one entry per level and not one per element | `constexpr int grid[n][n] = {{1}};` at n = 100 / 300 / 1000, read back by a `static_assert` at the far corner: 0.00 / 0.00 / 0.00s at a flat 6.1-6.4 MB (ref 0.60 / 1.00 / 5.41s at 24 MB / 113 MB / 1.13 GB). `int deep[2]...[2] = {}` at depth 8 / 12 / 16 / 20: 0.10s at 6.8-7.1 MB throughout - linear in depth, not 2^depth |
| a call the ordinary lookup answers | one lookup per call the fold reads, `LookupKind::Any` where the door asked `LookupKind::Type` - the same probe of one `Binding`, so telling 5.2.3's cast from 5.2.2's call costs nothing the ranking did not already pay | 500 / 2000 / 8000 folded calls of a function that hides a class of its name: 0.02 / 0.10 / 0.45s at 12 / 29 / 97 MB, against 0.02 / 0.10 / 0.44s at 12 / 29 / 97 MB for the identical calls with no class declared at all - within measurement either way (ref 0.60 / 0.90 / 2.50s at 18 / 32 / 86 MB) |
| a call a pattern cannot fold | one `unsettled_callee` read per *refused* call and none on a call that folds - one walk of the regions standing over the callee's declaration, then the callee's own argument list, both of them the nesting the program wrote | 500 / 2000 / 8000 template arguments in one pattern each holding a call of its own member: 0.01 / 0.05 / 0.24s at 8.6 / 14 / 36 MB (ref 0.55 / 0.62 / 0.92s at 18 / 27 / 61 MB). The same count with 8 parameters on the head rather than 1: 0.01 / 0.05 / 0.24s - the parameter scan is free. 500 such calls at class-template nesting depth 4 / 8 / 16 / 32: 0.01s at 8.2 / 8.3 / 8.3 / 8.6 MB, flat in depth |
| a member an unevaluated operand names | one depth taken at each of 5.3.3p1's, 7.1.6.2p4's and 5.3.7p1's own doors and one type read per operand; the member's declared type is the whole answer, so no object is built and no access is written, and a nest of operands is linear in depth | 500 / 2000 / 8000 `sizeof(S::keys)` folds: 0.02 / 0.07 / 0.32s at 10 / 20 / 62 MB (ref 0.70 / 0.90 / 2.70s at 19 / 34 / 90 MB). The same count reading `sizeof(<member> + 0)`, which the id-expression door does not answer: 0.01 / 0.06 / 0.26s at 9.3 / 18.6 / 55.0 MB (ref 0.60 / 0.83 / 2.44s at 18 / 30 / 75 MB), as `decltype(<member> + 0)` 0.02 / 0.08 / 0.38s at 10.9 / 24.8 / 80.3 MB and as `noexcept(<member> + n)` 0.01 / 0.06 / 0.25s at 9.6 / 18.7 / 56.6 MB — the reference refuses both of those shapes. 50 / 100 / 200 nested `sizeof` over one such operand: 0.00s at 6.4 / 6.5 / 6.9 MB |
| a place that counts what a pattern stood a value in for | one `counted_where` per bound, enumerator, bit-field width and alignment-specifier — 5.19p3's converted constant expression and 5.19's integral one, taken where the expression stands, so 14.6p8's answer travels with the count and a reading that ran out on its own stand-in costs one refusal at the place that asked and no second reading of anything | 500 / 2000 / 8000 enumerators reading through a stand-in: 0.01 / 0.04 / 0.19s at 8.5 / 13.7 / 34.7 MB against 0.01 / 0.03 / 0.14s at 8.1 / 13.1 / 32.5 MB for the same count folding a scalar call — 36%, and refused at the first one before the audit. The same count as template arguments 0.02 / 0.11 / 0.49s against 0.02 / 0.09 / 0.43s (14%), and as bit-field widths 0.01 / 0.04 / 0.18s at 9.0 / 15.0 / 39.9 MB. 2000 / 8000 / 32000 *ordinary* bit-fields, which the shared door added one `counted` call to: 0.02 / 0.08 / 0.39s at 11.4 / 27.2 / 90.3 MB — linear |

| a list read down the subobjects | one cursor per list however deep 8.5.1p11's elided braces go, so a subobject takes a *run* of clauses in one pass and no clause is read twice; the string units are one scan of a literal and one interned list, and `subobjects` is read once per aggregate the walk reaches | 500 / 2000 / 8000 elided two-member class members of one aggregate: 0.01 / 0.04 / 0.21s at 8.6 / 15.5 / 44.3 MB (ref **20.23s** at 95 MB at 8000). 2000 / 8000 / 32000 elided elements of one array member: 0.01 / 0.07 / 0.29s at 7.9 / 13.5 / 37.1 MB (ref 1.91s at 94 MB). 500 / 2000 / 8000 string-literal elements of an array of arrays: 0.02 / 0.05 / 0.19s at 7.4 / 11.8 / 30.7 MB (ref 0.80s at 39 MB). A class nested 8 / 12 / 16 / 20 deep whose every level elides: 0.00s at a flat 5.9-6.3 MB — linear in depth, not 2^depth (ref refuses it at 0.60s) |
| a clause 8.5.1p11 has to place | one `elides_its_braces` per level of a nesting, which reads the clause standing there with a scratch-node probe — so the cost is the depth times that one clause and carries no term in the clauses below it. The fold asks it beside the analysis rather than instead of it, so a `constexpr` object of such a nesting pays the product twice | (re-measured at the E audit) a class nested 10 / 40 / 80 deep whose first clause is a 1600-term expression: 0.09 / 0.33 / 0.65s at 26 / 78 / 148 MB, and at depth 40 with 400 / 1600 terms 0.08 / 0.33s — linear in each, not 2^depth. The same declaration with no `constexpr` is 0.33s at 78 MB at depth 80, which is the analysis's half of it; the reference refuses the shape |
| a cast, and the temporary a reference binds | one type test per cast at the two exits both spellings come through, and one conversion where 8.5.3p5's temporary is of a type the value is not already at — neither reads an operand twice | 400 / 1600 / 6400 casts to cv `void`: 0.00 / 0.02 / 0.10s at 7 / 11 / 28 MB; the same count in 5.2.3p1's functional notation: 0.01 / 0.04 / 0.17s at 8 / 15 / 44 MB |
| a union a constant expression holds | one `one_storage` test per object built, and a list that stops at the member 8.5.1p15 initialized — so a union costs less than the aggregate beside it rather than more, and a read of any other member is refused where it is read | 400 / 1600 / 6400 constant unions read back by a `static_assert`: 0.01 / 0.05 / 0.24s at 9 / 16 / 47 MB (ref 0.73s at 1600) |
| a branch hint | one `reserved_function` probe per callee name the ordinary lookup found nothing of, and one declaration for the unit however many calls name it; a call of it is its first operand read as 5.2.2p10's prvalue and no boundary at all, so nothing is declared, ranked or emitted twice | byte-identical to the reference at every lowering shape probed, one unit and two |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| E audit | `bc25598c`, 4 blockers: 5.2.3p1 makes `int((void)0)` the same construct as `(int)((void)0)` and the refusal stood at the second alone, so the functional notation **translated** it into `convert zext i32 void`, an instruction with no operand, and a const reference bound to it — the reading now stands at `cast_conversion` and `cast_to_reference`, the two exits both spellings share; 8.5.2p1's units are written by a reading that opens the list itself, so a mem-initializer written `: s{"ab"}`, whose caller had opened one already, left them under a node *inside* the list and the constructor stored `'a'` and three zeroes where both oracles store `'a', 'b'`; `object_of` padded a union's list back out to every member with 8.5.1p7's tail, which is the sentence `aggregate_constant` had just stopped for, so `constexpr U u = {5}; u.b` **folded to 0.0** where both oracles refuse the read; and 8.5.3p5's temporary was laid out at the operand's width rather than the referenced type's, so `const long &r = __builtin_expect(6, 0)` named four bytes and read eight. Four course fixtures pin them. See [audit.md](audit.md). | 158 → 162 |
| E | **The clauses a subaggregate takes out of the list.** 8.5.1p2 is written about the *subobjects*, so the fold's walk goes down the object with one cursor and not one clause per step: `aggregate_constant` is that walk, `subobject_constant` is one step of it, and `list_constant` is where the two questions only the braces' own place can answer stand — 8.5.2p1's string literal for a whole array of character type and 8.5.1p6's clause that reached no subobject. All four places a list stands ask it: a declaration's initializer, a clause of an enclosing list, 12.6.2p2's mem-initializer (braces there list-initialize an aggregate rather than passing 13.3.1.3's arguments) and 12.6.2p8's brace-or-equal-initializer, whose 8.5.2p1 door the *analysis* had not got either. 8.5.1p11's own question about a subobject of class type is `elides_its_braces` borrowed from the analysis rather than restated, and for an array it is the whole of the answer. Beside it two sentences about what is not a value: 5.2.9p4's cast to cv `void` is a discarded-value expression — evaluated, holding nothing — and a cast of a `void` operand to anything else is refused where both oracles refuse it and this build accepted it; and 1.4p8's `__builtin_expect` is `long (long, long)`, declared by the one `reserved_function` door `callee_candidates` now asks too, folded to its first operand and lowered as that operand with no call boundary, byte-identical to the reference. Three course fixtures pin the three. | 154 → 158 (162) |
| M audit | `8a154542`, 2 blockers: 5.1.1p13's third bullet is about an *unevaluated operand* and the depth was taken at `sizeof`'s door alone, so `decltype(S::first + 0)` and `noexcept(S::first)` were each **refused** with a diagnostic about `this` — the sentence group U exists to say does not apply — where g++ translates both and `pa21/cppgm++-ref` refuses them itself, so g++ is the oracle and the fixture stays written over `sizeof`; and 14.6p8's stand-in is an `int` of 1 whatever the call returns, so a place that reads *through* it runs out — 7.2p1's enumerator, 7p4's `static_assert`, 14.3.2p5's template argument, 9.6p1's bit-field width and 7.6.2p1's alignment-specifier each **refused** `make().v` for a `make` the pattern declares, one line from a bound that already folded it, because checkpoint A's audit carried the answer on the value a reading arrived at and only `array_bound` also caught the refusal. `ConstexprReading::counted_where` is now the one reading all four places that count ask, taken where the expression stands, and 7p4 and 14.3.2p5 ask the same question at their own doors. Two course fixtures pin both sides. See [audit.md](audit.md). | 152 → 154 (159) |
| M | **The three questions the expression layer already answered, asked a second time and answered differently.** 3.3.10p2 hides a class or an enumeration behind a variable, a data member, a function or an enumerator of that name declared in the same region, in either order, and 3.4.4p2 leaves it reachable through an elaborated-type-specifier alone - so `LookupKind::Type` is 3.4.4p2's question and belongs at `elaborated`, and what tells 5.2.3's cast from 5.2.2's call at `call_or_cast` and at `SpelledTypeId::read` - the tree door and the flattened one - is 3.4.1's ordinary lookup with `names_a_type` after it, which is what the expression layer's own `call_expression` has always asked (group H). 14p1 declares no function until a template is instantiated, so a reading of a *pattern* holds the definition of nothing the pattern declares: `ConstexprReading::unsettled_callee` is that one sentence over both shapes - a member of the class the pattern describes, found by `dependent_reading` over the regions standing above it, and a specialization made over a dependent argument, found in its own argument list - and 14.6p8 stands a value in for the call rather than calling the missing body 7.1.5p2's error, leaving the instantiation to read the same call again and answer it (group T'). And 5.1.1p13's third bullet is the one place a non-static data member is named with no object at all: `SemaAnalyzer::unevaluated_` is the depth of a reading of an unevaluated operand, taken at 5.3.3p1's door, and `named_value` gives the id-expression the member's own declared type there instead of 9.3.1p3's `this` (group U). Three course fixtures pin the three. | 145 -> 152 (157) |
| A audit | `d9d9a8af`, 3 blockers: 14.6p8 was asked of the enumerator that *wrote* a constant-expression and 7.2p1 gives the one that writes none the value of the one before it plus one, so `enum { first = sizeof(T), second };` marked `second` a constant holding the stand-in's arithmetic and `char check[second == 5 ? 1 : -1]` beside it was **refused as a negative array bound** where both oracles translate the program, with `second = first + 1` one character away already right; 4.2p1 — the sentence that makes `&numbers[2]` an address constant — was asked by a `named_array` lookup of the spelling at the subscript's left operand and nowhere else, so `numbers + 1`, `1 + numbers`, `numbers == numbers`, `numbers != other`, `!numbers`, `numbers ? a : b` and `*numbers` were each `numbers is not a constant expression` where g++ folds all seven and the reference folds four; and 3.2p2's pointer half was gated on `lowering()` where the arithmetic half above it was not, so `--emit-semantics` wrote the member's name where `pa12/cppgm++-ref` writes the initializer read at the name. A name of array type is now worth *which object it is* at `entity_constant`, each reader applies the conversion — `truth` for 4.12p1 and `unary_constant` for 5.3.1p1, beside the 5.7/5.9 operands that already did — and `global_address` owes `1 + numbers` the image `numbers + 1` had. Two course fixtures pin them. See [audit.md](audit.md). | 143 → 145 (154) |
| A | **3.2p2's read of a name, and the bound a pattern cannot compute.** `named_value` is the one door every naming comes through - a qualified name, an unqualified one inside a member function, one after an object expression - and it now asks 5.3.1p3 there: `addressed` is `&`'s question, taken at the operand in `unary_expression` for every id-expression and not only the qualified one 5.3.1p3's pointer-to-member needs, and it is what tells a *read* of 9.4.2p3's member from a use of the object. A read of one of pointer type is 5.19p2's address, which no `bits` holds and no pool the lowering can reach names - so what it is worth is the class's own brace-or-equal-initializer read where the name stands, which gives `"x"` the literal's object, `&numbers[2]` the element, `&step` the function and `nullptr` 4.10p1's value, and leaves the member's storage named by nothing. Carried three siblings the door reached: `&T::n` and `&n` over an *arithmetic* member were refused as non-lvalues where both oracles fold; 14.6p8's stand-in is now carried by what it produced rather than lost at the next test, so 8.3.4p1's bound, 7.2p1's enumerator and 7.1.5p9's declaration each answer "the arguments'" instead of the arm a stood-in value chose; and 4.2p1's name of array type is no pointer to read, which is what made `&numbers[2]` an address constant at all. Two course fixtures pin both halves. | 138 → 143 (152) |
| B audit | `afbfc093`, 3 blockers: `subobject_initialized` — the one of the three walkers that lays a list out which B split off and rewrote — had no arm for a subobject of *array* type, so `pair{1, 2}`, `partial{5}` and 12.6p1's default-initialized array of class type were **refused** where both oracles fold them and `emptied{}` took the scalar arm's `bits = 0`, which under an array type is the *empty* interned list and made `one.emptied[0]` read outside its bounds instead of 8.5p7's zero; 13.3.1.4p1's second half — the conversion functions of the value's own class — was asked at no place of class type at all, and asked nowhere because `initialized_value`, `clause_of` and `at_class_place` each performed 8.5p16's initialization themselves, so `constexpr payload p = source(1);` over `constexpr operator payload() const` was refused at a declaration, an aggregate clause, an array element, 12.6.2p8's brace-or-equal-initializer, an argument and 6.6.3p2's return alike; and the checkpoint's own known gap was written as a fold's and is the ABI's — the reference emits the *complete-object* entry of a constructor the program declared `constexpr` wherever it writes that definition out for a base subobject, with no fold, image or `constexpr` object anywhere, so every single-file program with such a base differed by one whole function. 8.5p16 is now one reading every door asks, an array subobject is `array_of`'s at a mem-initializer as at a clause, and 7.1.5p2's implicitly inline constructor owes both of the ABI's entry points. Two course fixtures pin both. See [audit.md](audit.md). | 136 → 138 (150) |
| B | **10p1's base class subobject, as an entry of the list a constant of class type holds.** `sema_constexpr_object.cpp` split out of `sema_constexpr.cpp` to own both directions of that list, and `data_members` became `subobjects`: 12.6.2p10's order, the base subobjects before the members, which is the order 9.2p13 lays out and the one number a member access, a base conversion and an address path all index by. `Derivation::subobject_path` answers a fifth question of the one derivation walk - the *steps* rather than the byte. Every reading that already existed asks the widened list: `member_path` for a name 10.2's lookup finds in a base and `member_address` for its address, `subobject_initialized` for a mem-initializer that names a base by its type (14.5.3p4's `Base(v)...` keyed by `SemaAnalyzer::base_key` per element) and for 12.6.2p4's default-initialized one, `at_class_place` and `bound_object` for 4.10p3 down and 5.2.9p11 back up, and `constant_image` and `global_constructed` for the image - the second threading the enclosing frame's bindings, because a base's mem-initializer names the outer constructor's parameters. Carried four siblings the door reached: 12.8p15's implicitly-defined copy constructor is the argument's own list; 9.3.2p1's `this` is bound in the fold's region and 9.3.1p3 makes an unqualified member call one on it; `vpointer_image` asks 7.1.5p4 rather than who wrote the constructor; and 14.6p8 stands a value in for an object of a dependent type and for a call any operand stood in for. Two course fixtures pin both sides. | 126 → 136 (148) |
| R audit | `6d975910`, 5 blockers: 4.2p1's decay of an object with no value was asked at two of the eleven places 8.5 fills a pointer from, so `(buf)`, `{buf}`, an aggregate clause, an array element, 12.6.2p8's held initializer, a mem-initializer, 8.3.6p1's default-argument, `return buf;`, `static_cast<char *>(buf)` and a function name each **refused a program both oracles translate**; the lvalue walk behind that door ended in the value reading that had just refused, so a refused operand cost its subtree twice and eighteen nested calls were **2.67s**; `holds_address` asked the type alone, so `static int *held;` reached a pointer place as 4.10p1's **null pointer value** and `static_assert(identity(held) == 0, "")` passed where both oracles refuse the program; 5.19p3's user-defined conversion was asked at every arithmetic place and at no pointer place; and the refusal a valueless operand makes claimed 5.19's answer about the program, which turned `constexpr int r = f(d.x);` over group B's class with a base into a hard error where the checkpoint before R accepted it. `operand_constant` is now 8.5's one operand reading at every place one is filled, `designated` is told when a value reading is no longer worth doing, and `covered_object` carries checkpoint V's answer through the object an address designates. Two course fixtures pin both sides. See [audit.md](audit.md). | 124 → 126 (146) |
| R | **5.19p2's address constant, and the object one designates.** `sema_address.cpp` owns it: `ConstantAddress` is a base — a declaration, 2.14.5p8's literal, or 12.2p1's temporary interned by what it holds — and the path of subobject indices down it, `AddressTable` gives each one number so a constant of pointer type is interned like any other, and `SemaConstant` gains 3.10p1's `object` beside `::valued`, which is what a `static int n;` has an address and no value of. Every reading that already existed asks it: `designated` is the lvalue walk beside `evaluate`'s value one, `unary_constant` for `&` and `*`, `binary_value` for 5.10p1's equality and 5.7p5's arithmetic with its one-past-the-end bound, `accessed_object` for the `->` checkpoint P left unopened, `subscript_constant` for the three operands 5.2.1p1 has, `at_pointer_place` for 4.2p1 and 4.3p1's decay, and `at_reference_place` for 8.3.2p1's binding — which is what makes `&value` inside a body the *argument's* address and what keys a fold on which object it was written on. `call` hands a reference return back as the object it designates; `fold_declared_object` asks 5.19p2's own requirement that it be one with static storage duration. Carried the two siblings the door reached: 5.2.9's cast to a reference or a class type (group C), and `valued_subobject`, one sentence for the kinds a member and an element may be. Two course fixtures pin both sides. | 110 → 124 (144) |
| V audit | `b8bd105a`, 4 blockers: the requirement asked `valued_class` - a fact about the type of the object *declared* - where the question is whether the reading of the *initializer* ran out, so `constexpr int n = *(values + 1);`, `constexpr char first = text[0];`, a member read through a constexpr pointer, 8.5.1p11's brace-elided aggregate and `f(&x)` were each **refused** where both oracles fold them and where `4853971d` accepted them; the same requirement asked in `--emit-types`, which collects no conversion functions, refused a declaration PA12 and PA21 both fold; 7.1.5p3's literal return and parameter types stood at `function_definition` alone, so `constexpr T(nonliteral)` and `constexpr operator nonliteral() const` were accepted at the two declarators that walk never reaches; and 3.9p10's third bullet read only a constructor a declaration wrote, so every class 8.5.1p1 leaves no aggregate - a base, a virtual function, 11p1's access - was **refused as a non-literal type** where both oracles build the object. `NotConstant::covered` is now 5.19's answer about the program told apart from this reading's edge, carried to the next name by `SemaEntity::covered_constant`; `require_function` stands at all three declarators; and 12.1p5's walk and 3.9p10's are two readings of one walk. Three course fixtures pin them. See [audit.md](audit.md). | 107 → 110 (142) |
| V | **7.1.5's requirements on a declaration, and the initialization 8.5p6 gives one that wrote none.** `sema_constexpr_declaration.cpp`'s `ConstexprRequirement` asks 7.1.5 where the declaration *stands* rather than where a use does: p3's literal return and parameter types and non-virtual dispatch at the declarator, p4's initialized member and base subobject at the two places 12.6.2p10's walk already finds nothing to construct, p9's literal type and constant initializer beside the fold. 3.9p10 is a fact of the class - `literal_class` settled where 9.2p2 completes it, beside 12.1p5's triviality - and 12.1p5's own default constructor is made `constexpr_function` there, which is what 3.9p10's third bullet then reads. `valued_class` is the same walk over the set `SemaConstant` holds, and it is the whole answer to "is a failed fold the program's error or this build's gap": a class with a base or a union answered no, so groups B and R kept the acceptance they had - and checkpoint B has since made the base an entry of the list, leaving 9.5p1's union the one shape that parts the two answers. Carried the fold the requirement made answerable: 8.5p6 default-initializes an object that wrote no initializer, 12.6.2p8's brace-or-equal-initializer initializes a member no mem-initializer names - including every member of 12.1p5's own constructor - and 3.9p10's array of arrays is a list of lists. Two course fixtures pin both sides. | 97 → 107 (139) |
| P audit | `90faa85d`, 4 blockers: 5.14p1's short-circuit was read off the operand *values*, so a class operand with `operator bool` and no `operator&&` evaluated the right operand — `!(no && quotient(1, 0))` **refused** as a division by zero where both oracles fold it, and a guarded write run; 13.3.1.2p2's mirror half closed with it, where `0 && mark` over `operator&&(int, token)` answered the built-in `false`; the fold gathered the non-member half for the four operators 13.5.3p1, 13.5.4p1, 13.5.5p1 and 13.5.6p1 make members, so `c[0]` over a non-member `operator[]` **folded to 42**; and 5.17p1's write-back was asked before 13.3.1.2 wherever the operand was a name, so `one = two`, the ten compound assignments and `++one` were refused one exit away from the temporary P had already opened. Under them `is_operator_token` held neither `<<=` nor `>>=`. See [audit.md](audit.md). | 96 → 97 (137) |
| P | **13.3.1.2's operator names a declaration.** `sema_operator.h`'s `OperatorCall` takes 13.3.1.2p3's candidate set out of `SemaAnalyzer` - a reader beside `ArgumentLookup` - so the expression layer and `ConstexprReading::operator_constant` gather it once and rank it with the one `select_overload`, the first operand handed over twice as 13.3.1.2p4 asks. Every door reaches it: the unary and binary readings, `&&`/`||` (whose short-circuit is the built-in operator's alone), 5.17's assignment on an operand that is no name, 5.2.1's subscript, and a call written on a temporary. Carried three siblings the door then reached: `at_class_place` performs 5.2.2p4's and 6.6.3p2's initialization of a place of class type, `initialized_value` makes 8.5 one reading for a declaration inside a body as well as outside one, and `clause_of` asks 14.7.1p1 for a specialization's definition before reading 8.5.1p1's aggregate off it - without which every class template answered "no aggregate" and a nested clause was read as an expression. 3.9p10's array member is a subobject like any other. Course fixture pins the eight shapes 13.3.1.2 names. | 88 → 96 (136) |
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. | 29 → 38 |
| L audit | `af299cb9`, 3 blockers: the owner part of the name flattened two functions to one symbol; 3.5p3's internal linkage was missing from both readings of "a definition every unit may hold"; 12.4p8 over an array was handed to `__cxa_atexit` as one call. See [audit.md](audit.md). | 38 → 38 |
| S | **Statements inside a constant evaluation.** `sema_constexpr_statement.cpp` runs 6.1-6.6 over a constexpr body instead of matching 7.1.5p3's one-`return` shape: blocks, declaration-statements, `if`/`while`/`do`/`for` including 6.4p3's condition declarations, `break`/`continue`/`return`. `SemaEntity::fold_local` marks the one kind of object an evaluation may write, and 5.17's assignment, 5.17p7's compound assignment, 5.3.2p1/5.2.6p1's increment and 5.18p1's comma reach it through the shared `binary_value`. Carried the parser fix 6.5.3p1 needed: `parse_condition` took `)` as the token that ends the declaration arm. | 38 → 46 |
| O | **Objects of literal class type as declared constants.** `fold_constant_object` no longer stops at 5.19p3's arithmetic case: a const object of literal class type is folded through `ConstexprReading::object_of` (or taken as-is where 8.5p14's initializer is already a prvalue of its own class), so `constexpr Lit lit(42); static_assert(lit.value == 42);` reads. 3.6.2p2's other half followed: a namespace-scope object the analysis folded takes `global_constructed`'s image and no startup body. | 46 → 48 |
| S+O audit | `46d8b2f4`, 3 blockers: a constant of class type was read as a number by every arithmetic place, so an array bound, an enumerator, a `static_assert` and a conditional all took the interned list's identifier as the value — five diagnostics turned into five wrong answers, now one reading of 5.19p3 asked by all of them; a declaration statement the walk re-ran was declared again, so a `typedef` inside a loop cost 73 MB at 102400 passes and a class-specifier was defined twice; `fold_local` was never set on a place the call filled. See [audit.md](audit.md). | 48 → 49 |
| F | **3.9.1p8's other half, and 8.3.4p6's other aggregate.** `SemaConstant` gains `real`; `arithmetic_type` widens to the arithmetic types with `integral_type` beside it for the places that count; `TypeTable` interns a distinct floating value by its exact (sign, exponent, significand); 4.7-4.9's conversions, 5p10's usual arithmetic conversions and every operator over floating values land in `convert`/`common_type`/`binary_value`; `SemaEntity::real` and `SemaFact::real` carry the value to 3.6.2p2's image, which now holds what the initializer *came to* rather than the digits an operand was written with. An array is a constant object too: `array_of` and `element_value` give it a list and a subscript. | 49 → 59 |
| F audit | `6cdd7e1d`, 3 blockers: `convert` had no arm for a destination of no arithmetic type and handed back the operand's bits *under the object's type*, so `constexpr P ps[1] = { 999999 };` made 999999 the identifier of a member list and `ps[0].x` read `parameter_lists_[999999]` — a segfault, reachable through `array_of`, `object_of` and the mem-initializer alike, now one refusal at the door; 8.5.4p7's second bullet was asked of a literal *spelling* and asked as an exactness, so every `float` clause off a name, an operator or a folded call was refused as narrowing and `float a{0.1}` with them, where the clause is a range "even if it cannot be represented exactly"; and 5p4's overflow reached an undefined cast in `real_type`, a `= in` in the image where the reference writes `= inf`, and two 4.9p1 bounds each on the wrong side of the cast they guard. See [audit.md](audit.md). | 59 → 59 |
| N | **5.3.7's `noexcept` operator, over 15.4's specification.** `sema_noexcept.cpp` reads the operand once through `probe_expression` - 5.3.7p1 leaves it unevaluated, so the scratch node and the temporaries it made are dropped - and answers 5.3.7p3 by one walk of the resolved tree: a `call-expression` with no `callee` child is a call through a pointer, and one with a callee is worth its `SemaEntity::nonthrowing`. The answer is a `literal prvalue bool` in `dispatch_expression`, a `bool` constant in `evaluate`, and a kept tree in `sema_value_expression.cpp`, where 14.2 had flattened the operator into a name. 15.4p1's condition is now *folded* rather than matched against `true`, and 14.5.6.1 carries a template's specification to its specializations, and a course fixture pins the shapes 5.3.7p3 names. | 59 → 66 (131) |
| N audit | `76c1c8fd`, 3 blockers: 15.4p1's condition was folded at the declarator, where 9.2p2 has yet to complete the class, so `void f() noexcept(k)` beside a `static constexpr bool k` declared below it answered no in the class and yes on its out-of-class definition - a valid program **refused** as two declarations 15.4p1 does not make the same, and silently the wrong `boundary.unwind` where no definition was written; `nonthrowing_tree` answered a new- and a delete-expression from a `fact.entity` neither writer fills, so `noexcept(delete p)` was false for every operand; and 5.3.7p3's second bullet walked the operand for a node only the statement parser builds. The reading now asks the *lines that name a declaration* - `Callee` and `DestructorAction` - and the condition is folded where the class is complete. See [audit.md](audit.md). | 66 → 66 |
| T | **13.3 and 14.8.2 answer the fold's callee.** `ConstexprReading::chosen`'s arity ranking is gone: `callee_candidates` writes the lookup `call_expression` writes - 14.2's specializations for a template-id, 3.4.2's associated namespaces for an unqualified name - and `selected` hands the set to the analysis's own `select_overload`, over one `AnalyzedValue` per constant. 5.19's constant is a prvalue, which is what tells `read(T&)` from `read(T const&)`; 13.3.1p3's object argument carries the constant's cv, which is what leaves a non-`const` member no candidate for a call on a `constexpr` object; 13.5.4p1's `operator()` answers a name that reaches an object. Choosing is *naming*, so `SemaAnalyzer::named_function` - split out of `name_function` - asks 14.7.1p1 for the specialization's body. Carried two sibling fixes the ranking then reached: 8.3p1's declarator-id was read as "the first child is an identifier", so every reference parameter went unbound, and 14.5.3p4's function parameter pack now binds one place per element of the run the type settled. | 66 → 75 (132) |
| T audit | `33422f2f`, 3 blockers: 12.3.2p1's conversion function was still chosen by a ranking of the fold's own, over a set every declaration that is not a constexpr function was dropped from *before* ranking — so `struct C { operator int() const; constexpr operator bool() const; }` answered `enum E { e = c }` with **1** where 13.3 chooses the first and both oracles refuse the program, and 12.3.2p2's `explicit` reached an enumerator and an array bound; a call written as a template argument arrives at a door of its own that still resolved the word with the ordinary lookup alone, so `H<f(N2::S(3))>` and `H<twice<int>(3)>` were refused there; and `member_value` answered from the subobject list, which 9.4p2's static member is never in. `converted` now asks `conversion_match`, both doors ask `called_name`, and 5.3.1p9's `!` asks `truth`. See [audit.md](audit.md). | 75 → 75 |
| I2 | **14.7.3's definition, and what binds it.** A function template declared and never defined got no `TemplateInfo`, so `template<> int f<int>(){...}` was recorded nowhere and the unit emitted a `declare` for a definition it held — the record is now made for a declaration too and the definition read later fills in the pattern it had none of, rebuilding the head from *its* own parameters (14.5.6.1p5). 14.7.3p6 then binds it: an explicit specialization is `inline` only where its own decl-specifiers say so, so `instantiate_body` no longer forces it and `abi_instantiated` no longer calls it an instantiation. Course fixture pins both bindings against the pattern-read specialization beside them. | 82 → 84 (133) |
| I3 | **What a folded image still owes, and what it may not hold.** 8.4.2p1's defaulted definition of a constructor the *class* declared is one the unit owes wherever an initialization named it, however little of its work the image kept; one the standard declared as well is named by nothing the program wrote. And a clause holding a call is work the startup body does and no item a structured image lays out — which is where the reference draws the line too, and where a *scalar* object still takes the one value its initializer came to. | 84 → 86 (133) |
| I audit | `e797cd19`, 3 blockers: 9.4.2p3's held brace-or-equal-initializer was pulled only for a class or an array, so `static constexpr const char *text = "ab";` with its definition written out laid out `ptr = zero` with no literal and no startup body — a **null pointer** where both oracles write the address, and `&helper` and `&target` with it; 8.4.2p1 was read as a fact about who *declared* the constructor rather than about whether working the image out went *through* its definition, so three shapes wrote out a definition the reference does not and `owe_folded_construction` is now the one question all four exits ask; and 5.2.2p1's own sentence was asked at two of the three walkers that lay an image out, and asked once per member rather than once per place — 3200 members off one 6400-term argument cost 4.55s and cost 0.14s now. Two course fixtures pin the first two. See [audit.md](audit.md). | 86 → 88 (135) |
| I | **3.6.2p2's image is the declaration's, and 9.4.2p3 makes that one declaration.** A static data member's out-of-class definition takes the in-class brace-or-equal-initializer (`member_initializers_` now holds a static member's too) and the array bound that initializer deduced, so it is initialized rather than default-initialized; `ConstexprReading::clause_of` reads a clause against the *subobject* it initializes, which is what folds `{{1,2},{3,4}}` into an array of class type; a scalar whose second fold over the dump stops takes `SemaEntity::value`/`::real`, and 3.2p2 then makes nothing in that initializer a use. Carried 4.2: a subscript names no array, so `a[1][0]` decays once. `lowir_image.cpp` split out of `lowir_lower.cpp`; `fold_constant_object` became `ConstexprReading::fold_declared_object` and 8.5.1p1's `aggregate_class` moved to `sema_scope.cpp`. | 75 → 82 |


**Known gap (B).** One shape the differential sweep found, one layer past the
fold. `vpointer_image` asks 7.1.5p4 of the constructor and 9p6 of the storage, so
a class whose whole object *is* the vpointer takes the image and one with a
member beside it does not — `struct object : base { int held; constexpr object()
: base(), held(3) {} };` is `ptr addr @object__vtable + 16, i32 3` in the
reference and `zero 16` with a startup body here, because no walk here writes the
vpointer into a structured image beside the members.

**Known gaps (B audit).** Where the reference draws the base's complete-object
line is a *file position*: with both class definitions in the primary source file
it emits both of the ABI's entry points for a constructor the program declared
`constexpr`, and with the identical tokens reached through `#include`, or in a
two-unit invocation where both units use the base, it emits the base entry alone.
Phases 1-7 keep no position, so this build owes both wherever it writes the
definition out — which is what g++'s object file holds too, and which no fixture
reaches, because none of the 162 uses an `#include`. Beside it, `constexpr A()
= default;` on a base is a third answer again: the reference emits a
`trivial_lifecycle=yes` complete-object entry for it and this build emits nothing,
and nothing calls either. `arr()` as a mem-initializer of an array member was one
of two shapes recorded refused here and is folded since checkpoint E, which made
both spellings 8.5p7's one value-initialization; the reference still calls it an
`unsupported constexpr variable initializer` two characters away from the `arr{}`
it takes. The other stays refused with no oracle to pin the acceptance: the
*direct*-initialization
`constexpr payload p(origin);` where one user-defined conversion stands inside the
argument's sequence, which g++ folds and the reference **segfaults** on. And a
reference *declaration* folds nothing at all — `constexpr int const &r = n;` and
every reference to a class or a base of one is refused where both oracles fold,
because `fold_declared_object` gates on `const` standing on a type that is built,
addressed or arithmetic and a reference is none of the three. That one is
`fold_declared_object`'s and reaches no reading checkpoint B owns.

**Known gaps (R).** Three shapes the differential sweep found where
`pa21/cppgm++-ref` refuses what g++ and this build fold, so no `.ref` can pin
them and the course fixture leaves them out: `text[0]` where `text` is a
`constexpr const char *` bound to a string literal (`static_assert
unevaluated`, while `*(text + 2)` and `"ab"[0]` both fold there), 5.7p6's
pointer difference `(f + 3) - f`, and 5.9p2's relational `v + 1 > v`. Beside
them, a *pointer to member* is the value kind that is still missing: no address
here says which member of a class it names, so `valued_type` leaves it out and
14.3.2p1's non-type template parameter of pointer type is refused outright by
both oracles anyway. 5.19p2's requirement that an address constant designate an
object with static storage duration is asked where a *declaration* keeps the
pointer and not where a block-scope object's address is compared, which is what
`static_assert(values.data() == values.elems)` over an automatic object needs;
the storage duration of a program-declared block-scope object is not read there.
And 12.8p31's elided prvalue now stops the image of an object of class type -
`constexpr entry a = entry("one");` takes `zero 8` and a startup body as the
reference does, where `constexpr entry b("two");` is laid out by both - which is
the row `300-static-constexpr-member-string-pointer-initialization` pins and the
reversal of a difference the I audit had recorded as deliberately kept.

**Known gaps (V audit).** 12.6.2p8's held brace-or-equal-initializer is folded in
the complete-class context 9.2p2 gives it, which is not where the members
12.6.2p10 already settled are bound - so `struct S { int a = 1; int b = a + 1;
};` is refused with "`a` is not a constant expression". g++ folds it; the
reference refuses it exactly as this build does, so no `.ref` can pin the
acceptance, and closing it means one region that is the class *and* the settled
members. Beside it, a constexpr function *declared* and never defined is not
asked: the reference refuses a non-literal return or parameter type there,
N3485's 7.1.5p3 is written about "the definition of a constexpr function", and
g++ accepts - and `constexpr ~X()` is the mirror, which the reference and this
build accept and g++ makes a C++20 feature. 8.5p7's zero-initialization before a
default constructor that is neither user-provided nor constexpr is not laid
down, so such an object takes a dynamic initialization: byte-identical to the
reference where the class only hides a member behind 11p1, and the
`vpointer_image` row group B already holds where it has a virtual function. And
a class with a *reference* member is value-initialized here where both oracles
refuse it, because 8.3.2p1's binding is read at a place a call fills and not at
a member no initializer reaches.

**Known gaps (V).** Three shapes the sweep found where the reference lays out an
image and this build runs a startup body, all of them one layer past the fold
and all of them `lowir_image.cpp`'s. An *array* of class type whose elements the
fold answered — `constexpr written_constructor row[2];` — gets `zero 8` and two
constructor calls where the ref writes `i32 7, i32 7`: `global_constructed` is
asked only where the type is a class, and reaching an array means laying its
elements out from one construction node that names the whole array. 3.6.2p2's
constant initialization is not `const`'s to ask — the ref gives a plain
`standard_constructor spare;` at namespace scope the image its constexpr default
constructor folds to, where `fold_declared_object` folds nothing because
5.19p3's `const` is what it gates on. And the ref emits the *definition* of a
non-trivial defaulted constructor a folded image went through, where
`owe_folded_construction` owes one only for a constructor the class declared;
the fixture works around it by writing an object the program builds at run time.
Beside them one presentation difference that predates the checkpoint and is
deliberately kept: where an initializer leaves a tail of elements
value-initialized, this build writes one `zero n` run and the reference writes an
item per element, so `constexpr int a[5] = {9};` differs. Matching it would make
`constexpr int a[1000][1000] = {{1}};` cost a million items — 5.41s and 1.13 GB
in the reference against 0.00s and 6.4 MB here — so the run stays until a fixture
asks otherwise. Beside all of them, 7.1.5p8's requirement that the class of a
constexpr member function be a literal type is not asked, and neither is
7.1.5p4's first bullet about a virtual base class.

**Known gaps (P audit).** 3.4.2's namespaces for the *right* operand of `&&` and
`||` are the one source of 13.3.1.2p3's set a fold cannot ask, because naming
them means reading the operand 5.14p1 leaves unread — so an `operator&&` declared
only in a namespace associated with that operand's type, and reached by neither
the left operand's class nor the unqualified lookup nor 3.4.2 for the left
operand, is missed where the left operand decides. The expression layer has both
types and does not have the gap. Beside it, `pa21/cppgm++-ref` has no answer for
13.5.7p1's `++` and `--` over a class operand — `static_assert unevaluated`, with
the operand misprinted as `(++)` — where g++ and this build both name the
declaration, and it refuses a folded `,` over class operands, a free unary
`operator+` and a free `operator+` over two enumeration operands that g++ and
this build fold. And a compound assignment over a class that declares none hands
the object's bits to `binary_value`, which refuses them, so the answer is a
diagnostic and not a wrong number; 5.17p7's own sentence is what would say so.

**Known gaps (P).** The reference emits the *constructor* a hidden friend's body
names even where nothing calls the friend and nothing declares an object:
`struct C { constexpr C(int); friend constexpr C add(C, C) { return C(...); } };`
gets `@C__C` from it with no use at all, and the same class with `add` written
at namespace scope, or with the body in a member function, gets none — probed
eight ways and it is friend-ness alone, not the operator, not the call, not the
fold. `300-constexpr-hidden-friend-converting-argument` now lowers and differs by
that one definition. (13.5.6's `operator->` was
the one operator `operator_constant` was not asked from, and checkpoint R opened
it: `accessed_object` reads `->` as 5.2.5p2's `(*E).` over the pointer the
declaration hands back.)

**Known gaps (I audit).** A base or member subobject a folded image left with
nothing to do owes its constructor's definition too - `constexpr Derived d =
Derived();` over two `= default` classes gets `@Derived__Derived` here and
`@Base__Base` beside it from the reference - and the dump does not say those
steps exist, because `write_member_initializations` writes none for a member of
class type no initializer reaches whose construction is trivial. Reaching them
means asking the *class* for its default constructor from the lowering, and what
is missing is a weak definition no unit needs. Beside it, `constexpr P p =
P(7);` is folded to `i32 7` here and laid out as `zero 4` and a startup body
there, where `constexpr P p(7)` is folded by both: the difference is 12.8p31's
elided prvalue, which `fact.elided_prvalue` already marks, and reproducing it
would lose an image 3.6.2p2 asks for. And 8.5p7's value-initialization now reads a
brace-or-equal-initializer, which closed the row that stood here: `struct Zero
{ int x = 3; }; constexpr Zero z = Zero();` folds, because 12.1p5's own default
constructor is a constexpr one and 12.6.2p8 says what it initializes each member
with (checkpoint V).

**Known gap (T).** A `constexpr` static member function called from the *same*
class template's body — `typedef bool_constant<enabled()> type;` inside
`holder<T>` — is refused: 14.7.1p1's demand for the member's definition queues
it, and the fold needs the body where it stands. Closing it means reading that
one held definition on demand rather than at the end of the instantiation.
Making room for `named_function` took `sema_analyzer.h` and
`sema_expression.cpp` over the audit's ceilings, so 3.4.2's argument-dependent
lookup left `SemaAnalyzer` for `sema_argument_lookup.cpp` — a reader beside
`Deduction` and `PackReading` — and the naming pair moved to the resolution that
calls it.

**Known gaps (T audit).** 7.1.5p8 makes a constexpr member function that is not
a constructor a `const` member function, and this build takes the declarator's
word for it — as `pa21/cppgm++-ref` does, refusing `c.get()` over
`constexpr int get()` on a `constexpr` object exactly as this build's analysis
does. The fold's conversion reading used to be the one place that asked nothing
about the object at all and now asks 13.3.1p3's object argument like everyone
else, so the answer is one answer. Closing it means putting the `const` in the
member's *type*, which is what its object-file name is spelled from, and the
`.ref` files hold the reference's spelling. Beside it, a *deleted* conversion
function is dropped before ranking rather than made the candidate 8.4.3p2 then
refuses — all four of the analysis's readers of 12.3.2p1 do that, so the fold
agrees with the build; and `--emit-types` collects no conversion functions at
all, because `collect_conversions` stands behind `semantics()`, so a fold that
reaches one is refused in that dialect and folded in `--emit-lowir`. And a
static member read through an object expression that is *not* itself a constant
— `S s; s.value` — is refused, because the fold reaches the member lookup only
through a value it has already folded.

**Known gaps (N audit).** A name no declaration answers, written in a condition
- `void f() noexcept(bogus);` - is accepted, because the fold that reads the
condition swallows the lookup's diagnostic; g++ refuses it, the reference accepts
it, and narrowing the catch to the fold's own refusal would make a dependent
condition ill-formed. And a throw-expression is no part of the operand grammar:
both oracles fold `noexcept(throw 1)` to false and `noexcept(noexcept(throw 1))`
to true, where this build refuses both at the parse and refuses `throw 1;` as a
statement outside the PA12 subset. Closing the second is a parse rule and an arm
beside `Callee`, not a rule of 5.3.7.

**Known gap (N).** 15.4p13 instantiates a dependent exception-specification
when it is needed: `template<class T> void step(T) noexcept(noexcept(make<T>()));`
gives each specialization the condition read with its own arguments bound,
where this build gives every specialization the answer the *pattern's*
condition folded to - which for a dependent condition is "may throw". A member
of a class template is unaffected, because its declarator is read once per
instantiation with the arguments already bound. Closing it means keeping the
condition beside `TemplateInfo` and folding it in the bindings region where a
specialization is made. The reference also refuses `typeid` inside a `noexcept`
operand and answers 5.3.7p3's third bullet for the *pointer* form of
`dynamic_cast` as well as the reference form; this build reproduces the latter
and reads no `typeid` at all. And the reference does not count the destructor
of a temporary the operand created, where g++ does; this build follows the
reference, because the operand is read into a node whose temporaries are
dropped with it.

**Known gap (L).** The reference names a local static of an *inline* or
*instantiated* function by source position (` at file:line:col`, hex-encoded).
Phases 1-7 keep no position, so this uses a per-function occurrence index
(`local<n>`) beside an owner part that is the function's own object-file
identity wherever a spelling would name two functions. That is unique across
the program and agreed on by the units of one invocation, but spelled
differently from the reference, so
`300-nested-function-template-local-static-array` and
`300-class-template-static-reference-dynamic-initialization` differ by their
symbol names alone. `300-function-local-static-array-guard` now differs by one
thing only: the ref emits a dead `@__strlit__` for a literal 8.5.2 consumed.
(The second half of that row, one `unary decay` too many where a subscript
reached an array element that is itself an array, is closed by checkpoint I.)
Block-scope `thread_local` is still refused;
both oracles accept it, and the storage the ABI gives it is reached through a
wrapper the README's Assignment Boundary does not name.

**Known gaps (A).** 4.2p1's "a name of array type is no pointer to read" is
asked of a *name* and not of the two shapes that designate an array one step
further in: `&g.a[2]` over `struct L { int a[4]; }; L g;` and `&m[1][2]` over
`int m[2][3];` are each refused as `g is not a constant expression` where both
oracles fold them, because `array_object` still reads the operand as a value
first there. Closing it means asking the lvalue walk first for every shape
`designated` answers structurally and reading the value only where it does not,
which is one walk and not two - the ordering that keeps a nest of subscripts off
2^depth. Beside them, three shapes the differential sweep found where no `.ref`
can pin the acceptance: `constexpr int* q = &arr[2];` written at *namespace*
scope is an `unsupported constexpr variable initializer` in the reference and
folded by g++ and this build; `static constexpr int& r = g;` is a
`declare global @T__r : i32` there, loaded at the *referent's* width from the
reference's own symbol, where this build declares the `ptr` the object holds and
loads twice - 3.2p2's read is not given to a reference member by either build,
and only the reference's type for it disagrees; and `char arr[0]` written in a
pattern is accepted here (14.6p8's stand-in for a bound of zero) and refused by
g++, where a pattern's *non-dependent* non-constant bound - `char arr[g];` - is
refused here and by g++ and accepted by the reference.

**Known gaps (M audit).** Four shapes the sweep found beside the two blockers,
none of them the checkpoint's own. `sizeof` written over an *expression* as a
template argument is refused by the flattened reader — `H<sizeof(g)>` folds and
`H<sizeof(g + 0)>` is `written inside sizeof as a template argument and names no
type`, for a plain global as much as for a member, where g++ folds both; it is
`TemplateArgumentReader`'s own reading and predates the checkpoint. Neither of
finding 1's shapes can be pinned by a `.ref`: `pa21/cppgm++-ref` answers
`decltype(S::first + 0)` with `failed to resolve member id-expression` and
`noexcept(S::first)` with `static_assert unevaluated`, so the course fixture
stays written over `sizeof` alone and g++ is the oracle for the other two. A
pattern's *static data member* whose brace-or-equal-initializer is ill-formed is
diagnosed only where its value is needed — `template<class U> struct W { static
constexpr int n = S::a; };` over a non-static `S::a` is accepted until something
reads `n`, where the same declaration outside a template is refused at the class;
it is not `unevaluated_`'s, because the shape is accepted identically with the
depth never set. And `make().at()[0]` written as a *class member's* initializer
is `unsupported constexpr class member initializer` in the reference and folded
by g++ and this build, so the finding-2 fixture pins the member access, the
member call and the four counting places and leaves the subscript to g++.
