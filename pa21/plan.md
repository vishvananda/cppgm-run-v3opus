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
  `sema_lifetime.cpp`, `lowir_local_static.cpp`). Block-scope `static` objects
  were refused outright; 6.7p4's guard, 3.6.2's constant initialization and
  3.6.3p3's `__cxa_atexit` registration are PA21's. **Done — checkpoint L.**

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed — but it refuses every
shape that reaches an arithmetic place through a conversion function, and it
accepts a floating enumerator 7.2p5 refuses, where g++ and the standard agree
with this build. Its floating images are `%.20g` at the object's own width with
2.14.4p1's suffix for a scalar, and the digits the program wrote for a clause of
an aggregate; both were probed and both are reproduced.

## Current Failure Map

**59/130** (58 of the 129 checked-in cases, plus the one added this turn); 71
failures remain. 13 are a LowIR mismatch and 9 are a `-bad` case this compiler
accepts; the rest refuse a program the assignment asks it to translate.

| Group | Shape | Count |
|---|---|---|
| O2. a name whose declaration the fold refused | `X is not a constant expression` where `X` *is* a `constexpr` variable — the initializer is a call or an operator one of the groups below refuses | 13 |
| I. LowIR mismatch, not exit status | canonical diff only; mostly the lowering re-folding from the dump instead of taking the analysis's answer, plus the two symbol-naming gaps below | 13 |
| T. template-id and overloaded callees | `X<...> is written where a constant expression calls and names no function` / `names no one constexpr function ... with these arguments` | 9 |
| V. `constexpr` validation | a `-bad` case this compiler accepts (exit status inverted), mostly a refused initializer lowered as a dynamic one | 9 |
| N. `noexcept` operator | `a constant expression holds an operator PA11 does not evaluate` | 7 |
| B. objects with bases, and a class the fold cannot rank a conversion of | `... is not a class a constant expression builds an object of`; `declares no constexpr conversion function` | 6 |
| C. a cast to a class or reference type | `casts to a type that is not arithmetic`, where the type-id names `const D&`, `E` or `X&&` | 3 |
| misc | `this`, lookup gaps reached only through evaluation, a member call on a temporary, a nested braced clause | 11 |

**N is the next checkpoint.** It is the largest single-message group, it is one
operator with one rule (15.4p13's exception-specification of the function a call
names), and it is the last operator the README's Assignment Boundary names that
the engine has none of. Several O2 rows are `constexpr bool` declarations whose
initializer is a `noexcept`, so part of O2 follows from it.

**Known gaps checkpoint F left standing.** A clause of an aggregate written as a
nested braced-init-list (`constexpr P ps[2] = {{1,2},{3,4}};`) is not folded:
`evaluate` reads an expression and a list is not one, so a target-typed clause
reader is what that needs. And a scalar whose initializer the *analysis* folded
but the *lowering's* own second fold cannot follow — `constexpr int n = arr[1];`,
`constexpr bool b = 0.5;` — is given a dynamic initializer; that is group I's
shape, and its fix is for the lowering to take `SemaEntity::value` and `::real`
rather than re-fold the dump.

## Active Checkpoint — none open

## Performance Model

| Path | Shape | Measured |
|---|---|---|
| constexpr loop | one pass per iteration; a block's region, the objects it declares and the names it introduces are each made once per fold and reused, so n iterations cost O(n) statements and O(1) declarations | `for` of 1e3 / 1e4 / 1e5 passes with a body-local declaration: 0.00 / 0.02 / 0.18s, peak RSS 7.11 / 7.00 / 7.05 MB (flat). A body-local `typedef` beside it at 12800 / 51200 / 102400 passes: 0.03 / 0.09 / 0.19s at 6.78 / 7.20 / 6.81 MB. Ref: 0.54 / 0.60 / 4.01s |
| constant object | one fold per declaration, and one interned list per distinct object; a constructor called twice with one argument list is one walk | 500 / 2000 / 8000 `constexpr` two-member objects, each read back by a `static_assert`: 0.03 / 0.10 / 0.45s (ref 0.64 / 0.99 / 3.77s). A chain of 20 / 40 / 80 nested class members: 0.00 / 0.00 / 0.01s — linear in depth, not 2^depth |
| arithmetic place | one type-kind test per reading, and one fold of a conversion function where the constant stands for an object | the rows above are unchanged by it |
| local-static symbol | one flatten per declaration, memoised in `entity_symbols_`; a name used *n* times costs one flatten and *n* lookups | 400 / 1600 / 6400 image-initialized statics in one body: 0.01 / 0.05 / 0.22s (ref 0.584 / 0.743 / 1.403s) |
| local-static guard | one image read per declaration, one guard global per object | 400 / 1600 / 6400 guarded statics: 0.026 / 0.096 / 0.423s (ref 0.632 / 0.933 / 2.229s) |
| array destruction | one `__cxa_atexit` per array, handed a generated body that walks the elements — written out below `kArrayLoopLimit` and a loop above it | 100000-element array of class type: 0.005s, 11 instructions |
| floating value | one `long double` per constant, and one pool entry per *distinct* value - a value met twice is one index, so a memo key stays stable without the pool growing | a constexpr loop of floating arithmetic at 1e3 / 1e4 / 1e5 passes: 0.00 / 0.02 / 0.21s, peak RSS 6.37 / 6.50 / 6.40 MB (flat). 2000 / 8000 / 32000 declarations of *distinct* floating constants each read back by a `static_assert`: 0.05 / 0.20 / 0.87s; the same count all of one value: 0.04 / 0.18 / 0.77s - the pool costs about 5% and does not scale with repeats |
| array constant | one interned list per array, one entry per element, and 8.5p7's value-initialized tail interned once and repeated | 1000 / 4000 / 16000 elements read back by a `static_assert`: 0.00 / 0.01 / 0.06s at 6.79 / 8.19 / 15.97 MB - linear in elements |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. | 29 → 38 |
| L audit | `af299cb9`, 3 blockers: the owner part of the name flattened two functions to one symbol; 3.5p3's internal linkage was missing from both readings of "a definition every unit may hold"; 12.4p8 over an array was handed to `__cxa_atexit` as one call. See [audit.md](audit.md). | 38 → 38 |
| S | **Statements inside a constant evaluation.** `sema_constexpr_statement.cpp` runs 6.1-6.6 over a constexpr body instead of matching 7.1.5p3's one-`return` shape: blocks, declaration-statements, `if`/`while`/`do`/`for` including 6.4p3's condition declarations, `break`/`continue`/`return`. `SemaEntity::fold_local` marks the one kind of object an evaluation may write, and 5.17's assignment, 5.17p7's compound assignment, 5.3.2p1/5.2.6p1's increment and 5.18p1's comma reach it through the shared `binary_value`. Carried the parser fix 6.5.3p1 needed: `parse_condition` took `)` as the token that ends the declaration arm. | 38 → 46 |
| O | **Objects of literal class type as declared constants.** `fold_constant_object` no longer stops at 5.19p3's arithmetic case: a const object of literal class type is folded through `ConstexprReading::object_of` (or taken as-is where 8.5p14's initializer is already a prvalue of its own class), so `constexpr Lit lit(42); static_assert(lit.value == 42);` reads. 3.6.2p2's other half followed: a namespace-scope object the analysis folded takes `global_constructed`'s image and no startup body. | 46 → 48 |
| S+O audit | `46d8b2f4`, 3 blockers: a constant of class type was read as a number by every arithmetic place, so an array bound, an enumerator, a `static_assert` and a conditional all took the interned list's identifier as the value — five diagnostics turned into five wrong answers, now one reading of 5.19p3 asked by all of them; a declaration statement the walk re-ran was declared again, so a `typedef` inside a loop cost 73 MB at 102400 passes and a class-specifier was defined twice; `fold_local` was never set on a place the call filled. See [audit.md](audit.md). | 48 → 49 |
| F | **3.9.1p8's other half, and 8.3.4p6's other aggregate.** `SemaConstant` gains `real`; `arithmetic_type` widens to the arithmetic types with `integral_type` beside it for the places that count; `TypeTable` interns a distinct floating value by its exact (sign, exponent, significand); 4.7-4.9's conversions, 5p10's usual arithmetic conversions and every operator over floating values land in `convert`/`common_type`/`binary_value`; `SemaEntity::real` and `SemaFact::real` carry the value to 3.6.2p2's image, which now holds what the initializer *came to* rather than the digits an operand was written with. An array is a constant object too: `array_of` and `element_value` give it a list and a subscript. | 49 → 59 |

**Known gap (L).** The reference names a local static of an *inline* or
*instantiated* function by source position (` at file:line:col`, hex-encoded).
Phases 1-7 keep no position, so this uses a per-function occurrence index
(`local<n>`) beside an owner part that is the function's own object-file
identity wherever a spelling would name two functions. That is unique across
the program and agreed on by the units of one invocation, but spelled
differently from the reference, so
`300-nested-function-template-local-static-array` and
`300-class-template-static-reference-dynamic-initialization` differ by their
symbol names alone. `300-function-local-static-array-guard` needs two unrelated
things: the ref emits a dead `@__strlit__` for a literal consumed by 8.5.2, and
our subscript of an array *element* that is itself an array emits one
`unary decay` the ref does not — which is general, reproducing at namespace
scope and over an automatic array. Block-scope `thread_local` is still refused;
both oracles accept it, and the storage the ABI gives it is reached through a
wrapper the README's Assignment Boundary does not name.
