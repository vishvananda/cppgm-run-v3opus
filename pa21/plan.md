# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Two ownership lines carry the whole assignment:

- **Constant evaluation** (`sema_constant.cpp`, `sema_constexpr.cpp`,
  `sema_constexpr_statement.cpp`, `sema_address.cpp`,
  `sema_value_expression.cpp`). `SemaConstant` is
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

## Current Failure Map

**126/146**; 20 failures. 6 are a LowIR mismatch and the rest refuse a program
the assignment asks it to translate. Groups N, T, I, P, V, R and C are closed,
R with its audit. Of the six LowIR rows, two are known gap L's symbol naming,
one is I' below, one is the row checkpoint P reached, and two are group A below.

| Group | Shape | Count |
|---|---|---|
| B. objects with bases | `... is not a class a constant expression builds an object of` — 10p1's base subobject is one this object does not hold, so no class with a base folds at all and `__and_<int>`, `trait<int>`, `pair<left,right>`, `type_identity<T>` and CRTP's `derived` each stop there; `300-constexpr-base-copy-constructor-init` and `400-constexpr-derived-this-base-reference` are the same gap read one name further along. 3.9p10 calls every one of them a literal type, as both oracles do, and `valued_class` says the values do not cover them — which together are what leave the declaration accepted and lowered as 3.6.2p2's dynamic initialization rather than refused. `400-constexpr-polymorphic-global-vptr-init` is the same object read from the image: `vpointer_image` refuses a constructor 12.1p11 leaves nothing but the vpointer because the program wrote it | 8 |
| A. a constant the program reads through a name | `traits<int>::name` is `addr @__strlit__1` in the reference, which folds the lvalue-to-rvalue conversion 3.2p2 makes no odr-use and instantiates no storage for the member at all, and `load ptr @traits_int___name` here — the fold now *has* the address and the lowering of an id-expression does not ask it. Beside it `300-class-template-static-reference-dynamic-initialization`, whose difference is gap L's symbol spelling | 2 |
| T'. a member of the class being instantiated | `values is not a constant expression` and `enabled is not a constexpr function this unit has defined` — 14.7.1p1's demand for a member's definition queues it, and a fold written in a *later member* of the same instantiation needs it where it stands | 3 |
| I'. a dead `@__strlit__` | `300-function-local-static-array-guard` differs by one global: for `static const char nested[1][2] = {"x"};` the ref emits the literal's own object beside the array that copied it. The boundary was probed and is the reference's own: it materializes the literal where 8.5.2 initializes an array that is an *element* of an enclosing array (`char two[2][2] = {"m","n"}` gets two), and not where the array is the whole object (`char flat[2] = "y"`) nor where it is a *member* of a class (`struct S { char a[2]; }; S s = {"q"}`). 2.14.5p8 makes the object exist in all four | 1 |
| misc | `this` written outside a member function, 8.5.1p11's brace elision beside a declaration inside an *instantiated* function body, 13.3.1.4's constructor for a class whose one argument is of its own class (`no declaration of struct S accepts the arguments of a call`, twice), the one symbol-naming row of gap L, and the owed-constructor row of gap P | 6 |

Two gaps beside them that no fixture fails on, each found by the R audit and
each belonging to an earlier group. **I''. the definition of an implicitly
declared default constructor an image was folded through**: `struct held { int
inside = 4; }; constexpr held h;` lays out the reference's image and emits no
`@held__held`, which the reference does — `owe_folded_construction` reads
`implicit_declaration` as 8.5.1's aggregate, which a class 12.6.2p8 leaves a
constructor to run is not. **L''. 3.5p3's `extern` before a `const`
definition**: `extern const int k; const int k = 5;` is `binding=internal` and
`_ZL1k` here and `binding=strong` and `_Z1k` in the reference, which 3.5p3
agrees with; the same program with no `extern` is identical in both.

**The next substantial checkpoint is group B, and 10p1's base subobject is the
one thing it adds.** `object_of` refuses outright where `owner->bases` is not
empty, and every row of the group stops at that line: the object a constant
expression holds is the interned list of what its *members* hold, and a class
with a base holds one subobject more than that list has entries. What the group
needs is for the list to hold the base subobjects too — 12.6.2p10's order puts
them before the members, which is the order `data_members` would have to walk
and the order 9.2p13 lays out — and then the readings that already exist ask it:
`member_value` and `member_address` for a name 10.2's lookup finds in a base,
`object_from_constructor` for a mem-initializer that names one, `converted` for
4.10p3's conversion of a derived object to a base, and `subobject_address` for
the path down to one. `valued_class` is where the group announces itself, and
`vpointer_image` is the one row that is not the fold's at all: 12.1p11 leaves a
polymorphic object nothing but the vpointer, which the image can spell and the
constructor the program wrote is what stops it.

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
| an array of arrays | one interned list per row and one per column, with 8.5.1p7's value-initialized tail interned once *per level* - so a partly written array costs the clauses it wrote plus one entry per level and not one per element | `constexpr int grid[n][n] = {{1}};` at n = 100 / 300 / 1000, read back by a `static_assert` at the far corner: 0.00 / 0.00 / 0.00s at a flat 6.1-6.4 MB (ref 0.60 / 1.00 / 5.41s at 24 MB / 113 MB / 1.13 GB). `int deep[2]...[2] = {}` at depth 8 / 12 / 16 / 20: 0.10s at 6.8-7.1 MB throughout - linear in depth, not 2^depth |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| R audit | `6d975910`, 5 blockers: 4.2p1's decay of an object with no value was asked at two of the eleven places 8.5 fills a pointer from, so `(buf)`, `{buf}`, an aggregate clause, an array element, 12.6.2p8's held initializer, a mem-initializer, 8.3.6p1's default-argument, `return buf;`, `static_cast<char *>(buf)` and a function name each **refused a program both oracles translate**; the lvalue walk behind that door ended in the value reading that had just refused, so a refused operand cost its subtree twice and eighteen nested calls were **2.67s**; `holds_address` asked the type alone, so `static int *held;` reached a pointer place as 4.10p1's **null pointer value** and `static_assert(identity(held) == 0, "")` passed where both oracles refuse the program; 5.19p3's user-defined conversion was asked at every arithmetic place and at no pointer place; and the refusal a valueless operand makes claimed 5.19's answer about the program, which turned `constexpr int r = f(d.x);` over group B's class with a base into a hard error where the checkpoint before R accepted it. `operand_constant` is now 8.5's one operand reading at every place one is filled, `designated` is told when a value reading is no longer worth doing, and `covered_object` carries checkpoint V's answer through the object an address designates. Two course fixtures pin both sides. See [audit.md](audit.md). | 124 → 126 (146) |
| R | **5.19p2's address constant, and the object one designates.** `sema_address.cpp` owns it: `ConstantAddress` is a base — a declaration, 2.14.5p8's literal, or 12.2p1's temporary interned by what it holds — and the path of subobject indices down it, `AddressTable` gives each one number so a constant of pointer type is interned like any other, and `SemaConstant` gains 3.10p1's `object` beside `::valued`, which is what a `static int n;` has an address and no value of. Every reading that already existed asks it: `designated` is the lvalue walk beside `evaluate`'s value one, `unary_constant` for `&` and `*`, `binary_value` for 5.10p1's equality and 5.7p5's arithmetic with its one-past-the-end bound, `accessed_object` for the `->` checkpoint P left unopened, `subscript_constant` for the three operands 5.2.1p1 has, `at_pointer_place` for 4.2p1 and 4.3p1's decay, and `at_reference_place` for 8.3.2p1's binding — which is what makes `&value` inside a body the *argument's* address and what keys a fold on which object it was written on. `call` hands a reference return back as the object it designates; `fold_declared_object` asks 5.19p2's own requirement that it be one with static storage duration. Carried the two siblings the door reached: 5.2.9's cast to a reference or a class type (group C), and `valued_subobject`, one sentence for the kinds a member and an element may be. Two course fixtures pin both sides. | 110 → 124 (144) |
| V audit | `b8bd105a`, 4 blockers: the requirement asked `valued_class` - a fact about the type of the object *declared* - where the question is whether the reading of the *initializer* ran out, so `constexpr int n = *(values + 1);`, `constexpr char first = text[0];`, a member read through a constexpr pointer, 8.5.1p11's brace-elided aggregate and `f(&x)` were each **refused** where both oracles fold them and where `4853971d` accepted them; the same requirement asked in `--emit-types`, which collects no conversion functions, refused a declaration PA12 and PA21 both fold; 7.1.5p3's literal return and parameter types stood at `function_definition` alone, so `constexpr T(nonliteral)` and `constexpr operator nonliteral() const` were accepted at the two declarators that walk never reaches; and 3.9p10's third bullet read only a constructor a declaration wrote, so every class 8.5.1p1 leaves no aggregate - a base, a virtual function, 11p1's access - was **refused as a non-literal type** where both oracles build the object. `NotConstant::covered` is now 5.19's answer about the program told apart from this reading's edge, carried to the next name by `SemaEntity::covered_constant`; `require_function` stands at all three declarators; and 12.1p5's walk and 3.9p10's are two readings of one walk. Three course fixtures pin them. See [audit.md](audit.md). | 107 → 110 (142) |
| V | **7.1.5's requirements on a declaration, and the initialization 8.5p6 gives one that wrote none.** `sema_constexpr_declaration.cpp`'s `ConstexprRequirement` asks 7.1.5 where the declaration *stands* rather than where a use does: p3's literal return and parameter types and non-virtual dispatch at the declarator, p4's initialized member and base subobject at the two places 12.6.2p10's walk already finds nothing to construct, p9's literal type and constant initializer beside the fold. 3.9p10 is a fact of the class - `literal_class` settled where 9.2p2 completes it, beside 12.1p5's triviality - and 12.1p5's own default constructor is made `constexpr_function` there, which is what 3.9p10's third bullet then reads. `valued_class` is the same walk over the set `SemaConstant` holds, and it is the whole answer to "is a failed fold the program's error or this build's gap": a class with a base or a union answers no, so groups B and R keep the acceptance they had. Carried the fold the requirement made answerable: 8.5p6 default-initializes an object that wrote no initializer, 12.6.2p8's brace-or-equal-initializer initializes a member no mem-initializer names - including every member of 12.1p5's own constructor - and 3.9p10's array of arrays is a list of lists. Two course fixtures pin both sides. | 97 → 107 (139) |
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
that one definition. Beside it, 8.5.1p11's brace elision is not read by
`clause_of` — `constexpr A<int> v = {3, 5};` for `int e[2]` is refused where
`{{3, 5}}` folds — and a declaration inside an *instantiated function body*
(`template<class T> void check() { constexpr array<T,2> v = {{3,5}}; ... }`) is
refused where the same declaration at namespace scope folds; both are
`300-constexpr-template-aggregate-subscript-member`. (13.5.6's `operator->` was
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
