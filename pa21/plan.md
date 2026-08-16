# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Two ownership lines carry the whole assignment:

- **Constant evaluation** (`sema_constant.cpp`, `sema_constexpr.cpp`,
  `sema_constexpr_statement.cpp`, `sema_value_expression.cpp`). `SemaConstant`
  is `{TypeId, bits, real}`: 3.9.1p8's two kinds of arithmetic value, and for
  an object of class or array type the bits are the identifier of the interned
  list its subobjects hold. PA21 needs typed constant values — floating and
  array are **done, checkpoint F**, class object **checkpoint O**, pointer and
  reference remain — statement execution inside a constexpr body (**checkpoint
  S**), and one engine shared by ordinary semantics, template arguments and
  `static_assert`. What a constant is worth where a place asks for a *number*
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
  it. **Done — checkpoint I.**

Beside them, one line that is deliberately *not* an owner: which declaration a
constant expression runs is 13.3's and 14.8.2's answer, and the fold asks for it
rather than keeping a ranking of its own — `ConstexprReading` builds one
`AnalyzedValue` per constant and hands it to the same `select_overload` for a
call and to the same `conversion_match` for 12.3.2p1's conversion function, and
`called_name` is the one reading both the tree and the flattened-spelling door
ask. Whether the declaration chosen is a constexpr one this unit defined is
asked *after* 13.3 has chosen and never used to choose. **Done — checkpoint T
and its audit.**

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
both are reproduced, `inf`, `-inf` and `-nan` among them. Where 5p4's overflow
or 4.9p1's out-of-range conversion makes a program undefined, g++ refuses it and
the reference folds it — this build folds it to the reference's values, because
the `.ref` files are the oracle and no fixture asks for the refusal.

## Current Failure Map

**86/133**; 47 failures remain. 5 are a LowIR mismatch and 9 are a `-bad` case
this compiler accepts; the rest refuse a program the assignment asks it to
translate. Groups N, T and I are closed: of the five LowIR rows left, two are
known gap L's symbol naming, two are group P's pointer-valued image, and one is
the row below.

| Group | Shape | Count |
|---|---|---|
| O2. a name whose declaration the fold refused | `X is not a constant expression` where `X` *is* a `constexpr` variable — the initializer is a call or an operator one of the groups below refuses | 13 |
| V. `constexpr` validation | a `-bad` case this compiler accepts (exit status inverted), mostly a refused initializer lowered as a dynamic one | 9 |
| B. objects with bases, and a class no conversion of reaches the place | `... is not a class a constant expression builds an object of` — 10p1's base subobject is one this object does not hold; `declares no one conversion function a constant expression reaches this place through` | 6 |
| P. pointer- and reference-valued constants | `&x` refused as an operator the fold does not evaluate; a pointer read for truth, compared, subscripted through, or written `->` on. Two of the LowIR rows are the same gap in the image: `ptr addr @object__vtable + 16` and `addr @__strlit__1` | 7 |
| C. a cast to a class or reference type | `casts to a type that is not arithmetic`, where the type-id names `const D&`, `E` or `X&&` | 3 |
| I'. a dead `@__strlit__` | `300-function-local-static-array-guard` differs by one global: for `static const char nested[1][2] = {"x"};` the ref emits the literal's own object beside the array that copied it. The boundary was probed and is the reference's own: it materializes the literal where 8.5.2 initializes an array that is an *element* of an enclosing array (`char two[2][2] = {"m","n"}` gets two), and not where the array is the whole object (`char flat[2] = "y"`) nor where it is a *member* of a class (`struct S { char a[2]; }; S s = {"q"}`). 2.14.5p8 makes the object exist in all four | 1 |
| misc | `this`, a subscript of a class object, a member call on a temporary, a write through an overloaded assignment, a member of a class *being* instantiated whose definition the demand only queued, and the two symbol-naming rows of known gap L | 8 |

**The next checkpoint is one door and not a list of rows.** O2 and most of
`misc` are 13.3.1.2: an operator expression names a declaration exactly as a
call does — a member `operator+`, a hidden friend 3.4.2 reaches, an
`operator==` that takes its operands by converting constructor — and the fold
answers only 13.6's built-in candidates, so `(C(1) + 2).n` is refused where the
declaration is there to be chosen. T made the call door ask `select_overload`
and T's audit made the conversion door ask `conversion_match`; this is the third
door and the same sentence.

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
| a static data member's image | one reading of the class's brace-or-equal-initializer per definition and one item per scalar subobject; the definition takes the initializer rather than folding a second time | 1000 / 4000 / 16000 two-member class elements of one `static constexpr` array member, read back by a `static_assert`: 0.01 / 0.06 / 0.24s at 9 / 17 / 49 MB (ref 0.60 / 0.74 / 1.38s at 21 / 39 / 110 MB) — linear in elements |
| 8.5p7's value-initialized object | one walk per *class*, held on `SemaModel::value_initialized`, so a class whose two members are themselves such classes costs one walk per level and not one per path | measured against the shape that shows it: a class nested 12 / 16 / 20 / 24 deep with two members at each level. The fold is now O(depth); what is left is 2^depth and is **not** this layer's — `n20 deep = {};` costs the same 2.19s and 833 MB *without* `constexpr`, because 8.5.1's dump writes one `subobject-initialization` node per scalar subobject. Depth 24 is 41s and 13 GB. That is PA15-PA20's description of an aggregate initialization, and the type itself is O(depth) either way; `n20 deep;` with no initializer is 0.00s and 6.5 MB |
| aggregate of floating clauses | one dump node per clause, and one fold per clause 8.5.4p7's second bullet asks about - which is a `float` member off a wider source and nothing else | 2000 / 8000 / 32000 `double` clauses of an array member: 0.05 / 0.21 / 0.81s at 22 / 66 / 243 MB, against 0.02 / 0.09 / 0.37s at 10 / 22 / 72 MB for the same count of `int` clauses (ref 0.70 / 1.20 / 2.90s at 34 / 95 / 339 MB and 0.70 / 0.90 / 1.90s at 21 / 43 / 135 MB). Making them `float`, the one shape the bullet folds, adds 16%: 0.94s and 274 MB at 32000 |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
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
| I | **3.6.2p2's image is the declaration's, and 9.4.2p3 makes that one declaration.** A static data member's out-of-class definition takes the in-class brace-or-equal-initializer (`member_initializers_` now holds a static member's too) and the array bound that initializer deduced, so it is initialized rather than default-initialized; `ConstexprReading::clause_of` reads a clause against the *subobject* it initializes, which is what folds `{{1,2},{3,4}}` into an array of class type; a scalar whose second fold over the dump stops takes `SemaEntity::value`/`::real`, and 3.2p2 then makes nothing in that initializer a use. Carried 4.2: a subscript names no array, so `a[1][0]` decays once. `lowir_image.cpp` split out of `lowir_lower.cpp`; `fold_constant_object` became `ConstexprReading::fold_declared_object` and 8.5.1p1's `aggregate_class` moved to `sema_scope.cpp`. | 75 → 82 |

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
symbol names alone. `300-function-local-static-array-guard` now differs by one thing
only: the ref emits a dead `@__strlit__` for a literal 8.5.2 consumed. (The
second half of that row, one `unary decay` too many where a subscript reached an
array element that is itself an array, is closed by checkpoint I.) Block-scope `thread_local` is still refused;
both oracles accept it, and the storage the ABI gives it is reached through a
wrapper the README's Assignment Boundary does not name.
