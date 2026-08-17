# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Six ownership lines carry the assignment; each is one
question asked at one door, and the audit below is the check that every place
that asks comes through it.

**1. What a constant is** — `sema_constant.cpp`, `sema_constexpr.cpp`,
`sema_constexpr_object.cpp`, `sema_constexpr_statement.cpp`,
`sema_value_expression.cpp`. `SemaConstant` is `{TypeId, bits, real, object,
valued}`: 3.9.1p8's two kinds of arithmetic value, and for an object of class or
array type the bits are the identifier of the interned list its subobjects hold.
`ConstexprReading::at_arithmetic_place` is 5.19p3's converted constant
expression and the one door every arithmetic reader comes through;
`ConstexprReading::counted_where` is the narrower door 14.1p4, 7.2p5, 8.3.4p1,
9.6p1, 7.6.2p1 and 6.4.2p2 use, because each asks for an *integral* constant
expression and no floating value is one, and because 14.6p8's stand-in has to
travel with the count. `sema_constexpr_object.cpp` owns both directions of the
list an object is: `subobjects` says which subobjects there are and in what
order — 10p1's base class subobjects before 9.2p13's members, which is the order
the layout uses, so one index is what a member access, a base conversion, an
address path and an image all use — and `member_path` is 10.2's lookup read back
against it. 9.5p1 is the one sentence that shortens that list: a union holds one
member at a time, so the list stops at the member whose lifetime an
initialization began, and an entry standing before it holds nothing at all.

**2. Which object a constant designates** — `sema_address.cpp`. 5.19p2's address
constant is the one kind of constant that is not a value: `ConstantAddress` is
*which object* — the declaration whose storage it is, 2.14.5p8's literal, or
12.2p1's temporary interned by what it holds — plus the path of subobject
indices, which indexes exactly the interned list a constant of class or array
type already holds. `AddressTable` gives each one number, with zero 4.10p1's
null pointer value. `SemaEntity::address` is where a *binding* names an object
some other declaration owns — 8.3.2p1's reference bound to an argument or
written as a declaration of its own, 9.2p1's member of the object a call was
written on — and it memoises a declaration's own address beside that.
`operand_constant` is 8.5's one operand reading, standing at every place 8.5
fills one from, and 8.5.4p1's braced-init-list is read there as the
initialization of the *place* wherever the reader knows what that place is.

**3. What a declaration written `constexpr` shall be** —
`sema_constexpr_declaration.cpp`. 7.1.5 asks two different questions and only
one of them is a fold: what an expression comes to is line 1's,
`ConstexprRequirement` is p3's literal return and parameter types, p4's
initialized members and base subobjects, p9's literal type and constant
initializer, over 3.9p10's answer — a fact of the *class* settled where 9.2p2
completes it. 7.1.5 is written about a *declarator*, so the requirement stands
at every walk that reads one. Whether a failed fold found the program's error or
ran out of this build's values is settled at the refusal: `NotConstant::covered`
is 5.19's answer about the program, and `SemaEntity::covered_constant` carries
it to the next name that reaches the declaration.

**4. Which declaration a constant expression runs** — deliberately *not* an
owner. 13.3 and 14.8.2 answer it: `ConstexprReading` builds one `AnalyzedValue`
per constant and hands it to the same `select_overload` and the same
`conversion_match` the expression layer uses, `called_name` is the one reading
both the tree door and the flattened-spelling door ask, and 13.3.1.2p3's set for
an operator is `sema_operator.h`'s `OperatorCall`. Whether the declaration
chosen is a constexpr one this unit defined is asked *after* 13.3 has chosen.
Three questions the expression layer already answers are asked once and not
twice: 3.4.1's ordinary lookup tells 5.2.3's cast from 5.2.2's call
(`LookupKind::Type` is 3.4.4p2's question and belongs at `elaborated` alone);
`unsettled_callee` is 14p1's sentence that a pattern declares no function; and
`unevaluated_` is the depth of 5.1.1p13's third bullet, taken at each of
5.3.3p1's, 7.1.6.2p4's and 5.3.7p1's doors. 9.3.2p1's `this` is the fourth:
8.3.5p10 puts the keyword's own name in a member function's declarator region,
so `folded_this` tells the binding a fold made from the declaration the analysis
wrote by *which object it names*.

**5. What an object holds before the program runs** — `lowir_image.cpp` is the
one owner of 3.6.2p2's image: the fold of an initializer written outside every
body, the items a structured image is laid out as, and the constant the analysis
already folded the object to. Nothing in it writes an instruction. Two lines
bound it, and both are 5.2.2p1's: an initializer that holds a *call* is work the
program runs however well 5.19 folded it, and 5.19 answers *which object* and
not which byte of it — a path down to a subobject is 9.2p13's and 8.3.4p6's,
answered where the dump lines spell the step. Which definitions the image then
owes is `owe_folded_construction`'s one question: whether working the image out
went through the definition 8.4.2p1 gives a constructor.

**6. Where an object lives and what ends it** — `sema_analyzer.cpp`
`record_storage`, `sema_lifetime.cpp`, `lowir_local_static.cpp`. 3.5p3's
linkage is a fact of the *variable* and not of the declaration that spelled the
`const`: internal only where the name is neither explicitly declared `extern`
nor previously declared to have external linkage, and only for a *non-volatile*
const-qualified type. 6.7p4's guard, 3.6.2's constant initialization and
3.6.3p3's `__cxa_atexit` registration are PA21's, and 3.7.1p3's block-scope
`static` of a definition every unit may hold is named by where in the source its
declarator-id stands — `AstTokenStream::positions()`, one byte offset per
terminal beside one region per `#include`.

Beside them, three small owners: 15.4's exception-specification is
`SemaEntity::nonthrowing`, settled by `sema_noexcept.cpp` and folded where 9.2p2
completes the class; 8.5.1p2's walk goes down the *subobjects* with a cursor, so
8.5.1p11's elided braces are one question per subobject and an array element or
a class member takes a *run* of the enclosing list — which is what 8.3.4p3's
deduced bound then counts, because the length of the list is not the number of
elements; and 1.4p8's `__builtin_expect` is the one reserved function whose
definition the implementation states here, found by the one `reserved_function`
door both the expression layer and `callee_candidates` ask.

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed — but it folds a
conversion function only where the place is an *initializer* or 4p3's contextual
`bool`; it accepts a floating enumerator and a floating array bound 7.2p5 and
8.3.4p1 refuse; it reads every name written where a type may stand as 3.4.4p2's
lookup does, so a class a function of that name hides is still the class to it;
it refuses `int g[2][3] = {1,2,3,4,5,6}` and every other 8.5.1p11 elision at a
declaration; and it emits a duplicate `alias object _ZN…C2E…` beside a base
entry it also defines. Where 5p4's overflow or 4.9p1's out-of-range conversion
makes a program undefined, g++ refuses it and the reference folds it — this
build folds it to the reference's values, because the `.ref` files are the
oracle. Its floating images are `%.20g` at the object's own width with 2.14.4p1's
suffix for a scalar, `inf`, `-inf` and `-nan` among them.

## Architecture Review

The final audit reconstructed the stage from the sources rather than from the
checkpoint conclusions, traced eight facts end to end — a clause, a subobject
index, an address identifier, a linkage, a `this`, an image item, a reference
binding and a deduced bound — and swept each against `pa21/cppgm++-ref`, g++
`-std=c++11 -pedantic-errors`, and the emitted program run through
`lowir2cy86`/`cy86`. Eight blockers were found and fixed; see
[audit.md](audit.md) for each.

What the review confirmed: 8.5.1p2's walk really is one walk with one cursor,
and the elided and fully-braced spellings of one nesting cost the same to the
byte; 5.19's refusal really does carry `covered` to every reader; 13.3 is asked
once per call and never ranked twice; the interned list and the address path
really do index the same order at a member access, a base conversion, an image
and a `&`; and `--emit-semantics`, `--emit-types` and `--emit-lowir` reach the
same answers through the same doors.

What it found is that four of the eight facts were written at one exit of their
own family, which is this stage's standing failure shape: 8.5.1p11's elision was
asked of a class *member* and not of an array *element* at a declaration; 8.5.4p1's
list was read at four of 8.5's places and not at 6.6.3p2's return or 8.3.6p1's
default-argument; 9.5p1's one member was asked of the walk that builds a union
from a list and not of the walk that builds one from a constructor; and 3.5p3's
linkage was asked of this declaration's specifiers and not of the variable.

### Boundary rows

**The assignment passes 169/169 and the suite 2568/2568.** Nothing below fails a
fixture; each row is a shape a sweep found, kept so a later assignment has the
boundary written down.

- **E″. a braced-init-list standing where a call's *argument* does.** `f({4,5})`
  and `H<f({7,8})>` inside a constant expression are `a constant expression holds
  a construct PA11 does not evaluate`: 13.3.3.1.5 ranks a list against a
  parameter, and the fold reads its arguments to values before 13.3 has chosen.
  6.6.3p2's return and 8.3.6p1's default-argument, where the place *is* known,
  now fold. The reference refuses the argument and the template argument too;
  g++ folds them.
- **Z′. a value-initialized tail is one span.** `int t[3] = {7};` lays out `i32
  7, zero 8` where the reference writes two `i32 0`, and `char s[4] = "ab";` the
  same one byte over.
- **E‴. the branch hint's operand keeps its own width.** Where 1.4p8's operand is
  not already `long` this build converts it to the declared parameter type and
  the reference does not; g++'s value needs the conversion, so the fixtures are
  written over `long` operands.
- **D′. an array member of an object of class type decays where the reference
  indexes.** One extra `unary decay` at each element step *inside* a member of
  array-of-array type. A member of a single array type and an array that is the
  whole object are both identical.
- **I″ (corrected). the definition of an implicitly declared default
  constructor.** `struct held { int inside = 4; }; constexpr held h;` emits no
  `@held__held` here and one in the reference — and **g++ emits none either**, so
  the row is the reference's and not this build's. The plan previously recorded
  it the other way round.
- **L‴. an explicit specialization is one unit's definition.** `template<> int
  tv<5>() { static int u = 9; }` names its local static at `binding=internal`
  here and `binding=weak` in the reference; 14.7.3p6 and g++ agree with this
  build. Seven shapes beside it that checkpoint E folds and the reference calls
  `unsupported constexpr variable initializer`.
- **M′. an element of an array of *class* type is built by 8.5.1's own
  constructor.** `struct P { int x; }; struct A { P e[2]; constexpr A() :
  e{{1},{2}} {} };` writes `call @P__P` per element where the reference stores in
  place. The image is identical either way.
- **R′. a namespace-scope reference bound to a temporary.** `constexpr const int
  &r = one + 1;` gives 12.2p5's lifetime-extended temporary a *slot of the
  startup body* and stores its address into `@r` — byte-identical to the
  reference, and a dangling pointer in both. g++ gives it static storage.
- **R″. a reference declared `constexpr` has external linkage here** (`_Z1r`) and
  internal linkage in the reference; a reference type carries no cv-qualification
  for 3.5p3 to read, and g++ agrees with this build.
- **C′. `lowir2cy86` truncates no floating conversion.** `double d = 1.5; (int)d`
  runs as `2`, with the LowIR byte-identical to the reference — so run evidence
  over floating values is that backend's and not this stage's.

## Final Architecture Review

The stage holds. The six ownership lines above are each one owner with one door,
and after this audit every reader of every one of them was swept: the walk of an
initializer list is asked at all five places a list stands (a declaration, a
clause, an element, 12.6.2p2's mem-initializer, 12.6.2p8's brace-or-equal-
initializer) and at the two 8.5.4p1 adds (6.6.3p2's return, 8.3.6p1's
default-argument); the subobject index is asked at all four readers (a member
access, a base conversion, an address path, an image); 5.19p2's address is asked
at both the dump walk and the fold's own answer, with the line between them
5.2.2p1's call; 3.5p3 is asked once, of the variable; and 9.3.2p1's `this` is
told from 8.3.5p10's declaration of the same name by the object it names.

No layer duplicates another. `lowir_image.cpp` writes no instruction and
`lowir_lower.cpp` writes no item; the fold ranks nothing and 13.3 folds nothing;
`sema_using.cpp` now owns 7.3.3 and 7.3.4, which `sema_analyzer.cpp` had been
carrying beside the declaration walk. Every dialect reaches the same rules: the
sweeps ran `--emit-semantics` and `--emit-types` beside `--emit-lowir` wherever a
fact could be gated on `lowering()`.

Performance is linear in every dimension measured, with no term in the size of
the program and no 2^depth this layer owns; the one 2^depth left is 8.5.1's dump
of an aggregate, which is PA15–PA20's description of the initialization and
costs the same without `constexpr`. The whole 169-file corpus compiles in 0.70 s,
unchanged from the pre-audit build to within measurement.

What PA22 inherits: a constant-evaluation layer whose engine is shared by
ordinary semantics, template arguments and `static_assert`; one interned list
per object and one interned address per (object, path); and the two doors a
template reading needs — `unsettled_callee` for 14p1 and `counted_where` for
14.6p8 — already carrying their answers to every place that decides something.

## Performance Model

| Path | Shape | Measured |
|---|---|---|
| constexpr loop | one pass per iteration; a block's region, objects and names are made once per fold | `for` of 1e3 / 1e4 / 1e5 passes: 0.00 / 0.02 / 0.18s at a flat 7 MB (ref 0.54 / 0.60 / 4.01s) |
| constant object | one fold per declaration, one interned list per distinct object | 500 / 2000 / 8000 two-member objects read back: 0.03 / 0.10 / 0.45s (ref 0.64 / 0.99 / 3.77s). 20 / 40 / 80 nested members: 0.00 / 0.00 / 0.01s |
| arithmetic place | one type-kind test per reading; where the constant is an object, one 13.3.3.1.2 ranking over a set gathered once per class | 1e3 / 4e3 / 16e3 folds through a conversion function: 0.02 / 0.10 / 0.44s at 13 / 34 / 117 MB; with 17 candidates 0.03 / 0.12 / 0.52s — 18% for 17x |
| chosen callee | one lookup and one 13.3 ranking per call, over an argument list built once | 1e3 / 4e3 / 16e3 calls: 0.01 / 0.04 / 0.16s; 8 / 32 / 128 candidates at 4e3 calls 0.04 / 0.05 / 0.10s — linear in candidates |
| an operator that names a declaration | 13.3.1.2p2's type test before anything is gathered | 1e3 / 4e3 / 16e3 overloaded `operator+`: 0.02 / 0.10 / 0.45s; built-in `+` 0.00 / 0.01 / 0.04s, unchanged |
| an image a constructor call leaves | one read of the definition per call; a place carried into n members is one walk | 6400-term expression at n = 3200 members: **0.12s** at 39 MB (was 4.55s at `1301e41b`; base build 0.11s). 1600 terms 0.05s, 400 terms 0.01s |
| a member of class type a mem-initializer reached | one walk of that member's own subobjects at 9.2p13's byte, per member | 500 / 2000 / 8000 `constexpr` objects whose member is a two-member class: 0.02 / 0.10 / 0.43s at 12 / 27 / 89 MB — linear, unchanged from the pre-audit build, and the image byte-identical to the reference |
| a list read down the subobjects | one cursor per list however deep 8.5.1p11's elided braces go | 500 / 2000 / 8000 elided two-member class members: 0.01 / 0.04 / 0.18s at 8.8 / 15 / 44 MB (ref **20.23s** at 8000; base build 0.01 / 0.04 / 0.17s) |
| an array element that elides its braces | the same one question per element, asked before the clause is read | `int g[n][2] = {…}` at n = 2000 / 8000 / 32000: 0.02 / 0.07 / 0.28s against 0.01 / 0.06 / 0.26s for the identical array written `{{…},…}` — within 8%, and `P a[n] = {…}` over a two-member class 0.02 / 0.08 / 0.33s |
| 8.3.4p3's deduced bound | one walk of the same list with nothing kept, asked only where an element can take more than one clause | `int g[][2] = {…}` at 2000 / 8000 / 32000: 0.03 / 0.10 / 0.42s against 0.02 / 0.07 / 0.28s bounded — one extra walk, linear, and not asked at all for a scalar or one-clause element |
| a nesting whose every level elides | linear in the clauses, with no term in the depth beyond the type's own | a class 2 members wide and 8 / 10 / 12 / 14 deep, written out: elided 0.00 / 0.01 / 0.05 / 0.20s and fully braced 0.00 / 0.01 / 0.05 / 0.20s — identical, at 7.5 / 9.5 / 18 / 54 MB against 7.0 / 9.0 / 17 / 50 MB |
| a union a constant expression holds | one `one_storage` test per object built; a list that stops at the member 8.5.1p15 or 12.6.2p2 initialized | 400 / 1600 / 6400 constant unions read back: 0.01 / 0.05 / 0.24s (ref 0.73s at 1600). A member call on a constant union folded 1e3 / 4e3 / 16e3 times: 0.01 / 0.03 / 0.11s at a flat 6.8 MB, against 0.01 / 0.03 / 0.12s for the same loop over a `struct` |
| an address constant | one `ConstantAddress` interned per distinct (object, path), memoised on the declaration | a fold loop walking a pointer over 1e3 / 4e3 / 16e3 elements: 0.01 / 0.03 / 0.11s (ref 1.00s at 1000 and **189.37s** at 16000) |
| a declaration of reference type | one `at_reference_place` per declaration and one address kept on it, so a name read n times is n map probes | 500 / 2000 / 8000 `constexpr` references each read back by a `static_assert`: 0.01 / 0.05 / 0.23s at 9 / 18 / 55 MB — linear, and refused at the first one before this audit |
| a folded address an image stands | one map probe per declaration, at the one door the second walk of the lines stopped at | byte-identical to the reference at every pointer-image shape probed; no measurable cost on the 169-file corpus |
| 3.9p10's answer for a class | two readings of one walk of the bases and members where 9.2p2 completes the class, each reading the answer the subobject's class already carries | 500 / 2000 / 8000 distinct classes with a constexpr constructor and object: 0.09 / 0.41 / 1.77s at 29 / 96 / 367 MB (base build 0.09 / 0.41 / 1.75s; ref 0.90 / 1.90 / 9.31s) — the row's earlier 1.41s was measured on other hardware |
| 8.5p7's value-initialized object | one walk per *class*, held on `SemaModel::value_initialized` | the fold is O(depth); the 2^depth left is 8.5.1's dump and not this layer's — `n20 deep = {};` costs the same 2.19s and 833 MB *without* `constexpr` |
| array constant | one interned list per array, 8.5p7's tail interned once and repeated | `constexpr int a[1000000] = {1};` is 0.10s and 13 MB |
| an array of arrays | one interned list per row and one per column, the tail interned once per level | `constexpr int grid[n][n] = {{1}};` at n = 100 / 300 / 1000: 0.00s at a flat 6.4 MB (ref 0.60 / 1.00 / 5.41s at up to 1.13 GB) |
| a base class subobject | one entry per direct base, 10.2's lookup read back as one step per class | derivation 20 / 40 / 80 / 160 deep read at both ends: 0.00 / 0.00 / 0.01 / 0.03s. 2000 reads at depth 10 / 20 / 40: 0.02 / 0.03 / 0.06s |
| `noexcept` operator | one reading of the operand per operator and one walk of the tree it left | 500 / 2000 / 8000 declarations with three operators each: 0.04 / 0.19 / 0.78s (ref 0.62 / 0.92 / 2.97s). 50 / 100 / 200 nested: 0.00s |
| exception-specification condition | one fold per declaration, a second only where 9.2p2's complete-class context left the first unanswered | 400 / 1600 / 6400 members naming a member declared below: 0.01 / 0.04 / 0.21s (ref 0.63 / 1.79 / **26.78s**) |
| local-static symbol and guard | one flatten per declaration memoised, one guard global per object | 400 / 1600 / 6400 guarded statics: 0.026 / 0.096 / 0.423s (ref 0.632 / 0.933 / 2.229s) |
| a terminal's place in the source | four bytes per terminal, one region per `#include`, line and column counted at the reader | a 1.25 MB unit of 20000 functions: 1.48s at 352.9 MB against 1.48s at 351.2 MB without the record — +1.9 MB and +2% |
| the whole corpus | — | the 169 PA21 fixtures compile in **0.70s** total, against 0.70s for the pre-audit build |

## Checkpoint Ledger

`audit.md` holds what each review found; this is the order they landed in.

| # | Checkpoint | Result |
|---|---|---|
| L | the place in the source a terminal stands at, and the three names that read it | 162 → 166 (169) |
| E audit | the second spelling one cast has, the braces a literal stands in, and the members a union does not hold | 158 → 162 |
| E | the clauses a subaggregate takes out of the list, the value a discard does not hold, the hint the implementation defines | 154 → 158 (162) |
| M audit | the operand an unevaluated reading is written about, and the stand-in a refusal did not carry | 152 → 154 (159) |
| M | the lookup that tells a cast from a call, the definition a pattern has not got, the member no object holds | 145 → 152 (157) |
| A audit | the enumerator 7.2p1 gives a value to, and the operands 4.2p1 is written about | 143 → 145 (154) |
| A | the address a name reads, and the bound a pattern cannot compute | 138 → 143 (152) |
| B audit | the declared type a subobject has, and the half of a set no place asked | 136 → 138 (150) |
| B | 10p1's base subobject is an entry of the list, not a reason to refuse | 126 → 136 (148) |
| R audit | the object an initializer designates, and the walk that read it twice | 124 → 126 (146) |
| R | 5.19p2's constant says which object, not what it is worth | 110 → 124 (144) |
| V audit | the fold that ran out, and the declarators 7.1.5 is written about | 107 → 110 (142) |
| V | 7.1.5 asks the declaration, not the fold | 97 → 107 (139) |
| P audit | the question in front of the set, and the operand it left unread | 96 → 97 (137) |
| P | 13.3.1.2's operator names a declaration, and a fold asks the same set | 88 → 96 (136) |
| I audit | the image a declaration holds, and whose the definition is | 86 → 88 (135) |
| I3 | which definitions a folded image still owes, and which calls it may not hold | 84 → 86 (133) |
| I2 | the definition an explicit specialization wrote out, and what binds it | 82 → 84 (133) |
| I | the image an object holds is the declaration's, and 9.4.2p3 makes that one declaration | 75 → 82 |
| T audit | the calls a fold still chose for itself | 75 → 75 |
| T | which declaration a constant expression calls, and who answers it | 66 → 75 (132) |
| N audit | where a class is complete for the condition it wrote | 66 → 66 |
| N | what a `noexcept` operator asks of a call, and what 15.4 answers | 59 → 66 (131) |
| F audit | the value a clause is judged by, and the destination a conversion has no value for | 59 → 59 |
| F | floating and array constants | 49 → 59 |
| S+O audit | what a class constant is worth where a number is asked for | 48 → 49 |
| O | an object of literal class type is a constant its declaration holds | 46 → 48 |
| S | statement execution inside a constexpr body | 38 → 46 |
| L audit | the function part of a name that has to say *which* function | 38 → 38 |
| L | block-scope `static`: 6.7p4's guard, 3.6.2's constant initialization, 3.6.3p3's registration | 29 → 38 |
| **stage audit** | **the clauses one element takes, the place a list stands at, the member a union began the lifetime of, the object a fold has none of, the image 12.6.2p2 built one level down, the object 5.19p2 answered, and the object a reference names** | **169 → 169; 8 blockers** |
