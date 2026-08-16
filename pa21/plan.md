# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Two ownership lines carry the whole assignment:

- **Constant evaluation** (`sema_constant.cpp`, `sema_constexpr.cpp`,
  `sema_constexpr_statement.cpp`, `sema_value_expression.cpp`). `SemaConstant`
  is `{TypeId, bits}`, where the bits of an object of class type are the
  identifier of the interned list its subobjects hold. PA21 needs typed
  constant values (floating, pointer, reference, array, class object),
  statement execution inside a constexpr body — **done, checkpoint S** — and
  one engine shared by ordinary semantics, template arguments and
  `static_assert`. What a constant is worth where a place asks for a *number*
  is `ConstexprReading::at_arithmetic_place`, which is 5.19p3's converted
  constant expression and the one door every arithmetic reader comes through.
- **Static-storage lowering** (`sema_analyzer.cpp` `record_storage`,
  `sema_lifetime.cpp`, `lowir_local_static.cpp`). Block-scope `static` objects
  were refused outright; 6.7p4's guard, 3.6.2's constant initialization and
  3.6.3p3's `__cxa_atexit` registration are PA21's. **Done — checkpoint L.**

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed — but it refuses every
shape that reaches an arithmetic place through a conversion function, where g++
and 5.19p3 agree with this build.

## Current Failure Map

**49/129**; 80 failures remain.

| Group | Shape | Count |
|---|---|---|
| O2. a name whose declaration the fold refused | `X is not a constant expression` where `X` *is* a `constexpr` variable — the initializer is a call or operator below | 14 |
| I. LowIR mismatch, not exit status | canonical diff only; mostly the lowering re-folding from the dump instead of taking the analysis's answer | 13 |
| misc | subscript of a real array, `this`, lookup gaps reached only through evaluation, a class the fold cannot rank a conversion of | 13 |
| F. floating constants | `a literal that has no integral value` / `casts to a type that is not integral` — `SemaConstant` has no floating value at all | 11 |
| V. `constexpr` validation | a `-bad` case this compiler accepts (exit status inverted), mostly a refused initializer lowered as a dynamic one | 9 |
| T. template-id and overloaded callees | `X<...> ... names no function` / `names no one constexpr function ... with these arguments` | 9 |
| N. `noexcept` operator | `a constant expression holds an operator PA11 does not evaluate` | 7 |
| B. objects with bases / dependent classes | `... is not a class a constant expression builds an object of` — `object_of` refuses a class with a base | 4 |

**F is the next checkpoint.** `SemaConstant` is `{TypeId, bits}` with the bits
read as an integer everywhere, so a floating literal has nowhere to land; the
group is self-contained (one widening of the value type plus the arithmetic that
reads it) and it is the last typed value kind the README names that the engine
has none of. `at_arithmetic_place` is where the widening lands on the reader
side, because it is already the one rule that says what a constant is worth at
an arithmetic place. Part of O2 and V follow from it.

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

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. | 29 → 38 |
| L audit | `af299cb9`, 3 blockers: the owner part of the name flattened two functions to one symbol; 3.5p3's internal linkage was missing from both readings of "a definition every unit may hold"; 12.4p8 over an array was handed to `__cxa_atexit` as one call. See [audit.md](audit.md). | 38 → 38 |
| S | **Statements inside a constant evaluation.** `sema_constexpr_statement.cpp` runs 6.1-6.6 over a constexpr body instead of matching 7.1.5p3's one-`return` shape: blocks, declaration-statements, `if`/`while`/`do`/`for` including 6.4p3's condition declarations, `break`/`continue`/`return`. `SemaEntity::fold_local` marks the one kind of object an evaluation may write, and 5.17's assignment, 5.17p7's compound assignment, 5.3.2p1/5.2.6p1's increment and 5.18p1's comma reach it through the shared `binary_value`. Carried the parser fix 6.5.3p1 needed: `parse_condition` took `)` as the token that ends the declaration arm. | 38 → 46 |
| O | **Objects of literal class type as declared constants.** `fold_constant_object` no longer stops at 5.19p3's arithmetic case: a const object of literal class type is folded through `ConstexprReading::object_of` (or taken as-is where 8.5p14's initializer is already a prvalue of its own class), so `constexpr Lit lit(42); static_assert(lit.value == 42);` reads. 3.6.2p2's other half followed: a namespace-scope object the analysis folded takes `global_constructed`'s image and no startup body. | 46 → 48 |
| S+O audit | `46d8b2f4`, 3 blockers: a constant of class type was read as a number by every arithmetic place, so an array bound, an enumerator, a `static_assert` and a conditional all took the interned list's identifier as the value — five diagnostics turned into five wrong answers, now one reading of 5.19p3 asked by all of them; a declaration statement the walk re-ran was declared again, so a `typedef` inside a loop cost 73 MB at 102400 passes and a class-specifier was defined twice; `fold_local` was never set on a place the call filled. See [audit.md](audit.md). | 48 → 49 |

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
