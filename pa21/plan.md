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
  `static_assert`.
- **Static-storage lowering** (`sema_analyzer.cpp` `record_storage`,
  `sema_lifetime.cpp`, `lowir_local_static.cpp`). Block-scope `static` objects
  were refused outright; 6.7p4's guard, 3.6.2's constant initialization and
  3.6.3p3's `__cxa_atexit` registration are PA21's. **Done — checkpoint L.**

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed.

## Current Failure Map

**46/129**; 83 failures remain.

| Group | Shape | Count |
|---|---|---|
| O. constant-valued declarations | `X is not a constant expression` where `X` *is* a `constexpr` variable — its value was never recorded | 20 |
| I. LowIR mismatch, not exit status | canonical diff only | 10 |
| V. `constexpr` validation | a `-bad` case this compiler accepts (exit status inverted) | 9 |
| F. floating constants | `a literal that has no integral value` | 8 |
| N. `noexcept` operator | `a constant expression holds an operator PA11 does not evaluate` | 6 |
| T. template-id callees | `X<...> is written where a constant expression calls and names no function` | 5 |
| B. objects with bases / dependent classes | `... is not a class a constant expression builds an object of` | 4 |
| C. class-typed operands | `casts to`/`has a type that is not integral` — a class value reaching an arithmetic place | 6 |
| misc | address-of, subscript of a real array, `this`, lookup gaps reached only through evaluation | 15 |

Group O is the next checkpoint: `sema_analyzer.cpp`'s object declaration
records `entity.constant` only for the integral case, so a `constexpr` variable
of class type — or one whose initializer is a call the fold *can* answer —
lands unmarked and every later read of it fails. It is the common root of most
of group C and part of V.

## Active Checkpoint — none open

## Performance Model

| Path | Shape | Measured |
|---|---|---|
| constexpr loop | one pass per iteration; the block's region and the objects it declares are opened/created once per fold and reused, so n iterations cost O(n) statements and O(1) regions | `for` of 1e3 / 1e4 / 1e5 passes with a body-local declaration: 0.00 / 0.02 / 0.19s, peak RSS 6.24 / 6.25 / 6.21 MB (flat). Ref: 0.61s at 1e4 and 4.09s at 1e5 |
| folded call | memoised on (callee, converted argument list), so a call written n times is one walk | unchanged by checkpoint S |
| local-static symbol | one flatten per declaration, memoised in `entity_symbols_`; a name used *n* times costs one flatten and *n* lookups | 400 / 1600 / 6400 image-initialized statics in one body: 0.018 / 0.062 / 0.247s (ref 0.584 / 0.743 / 1.403s) |
| local-static guard | one image read per declaration, one guard global per object | 400 / 1600 / 6400 guarded statics: 0.026 / 0.096 / 0.423s (ref 0.632 / 0.933 / 2.229s) |
| array destruction | one `__cxa_atexit` per array, handed a generated body that walks the elements — written out below `kArrayLoopLimit` and a loop above it | 100000-element array of class type: 0.005s, 11 instructions |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. Carried 8.5.2p1 for a string literal initializing an array *element*, and 3.6.2's fold of a call in a block-scope static's initializer not odr-using the callee. | 29 → 38 |
| L audit | `af299cb9`, 3 blockers: the owner part of the name flattened two functions to one symbol; 3.5p3's internal linkage was missing from both readings of "a definition every unit may hold"; 12.4p8 over an array was handed to `__cxa_atexit` as one call. See [audit.md](audit.md). | 38 → 38 |
| S | **Statements inside a constant evaluation.** `sema_constexpr_statement.cpp` runs 6.1-6.6 over a constexpr body instead of matching 7.1.5p3's one-`return` shape: blocks, declaration-statements, `if`/`while`/`do`/`for` including 6.4p3's condition declarations, `break`/`continue`/`return`. `SemaEntity::fold_local` marks the one kind of object an evaluation may write, and 5.17's assignment, 5.17p7's compound assignment, 5.3.2p1/5.2.6p1's increment and 5.18p1's comma reach it through the shared `binary_value`. Carried the parser fix 6.5.3p1 needed: `parse_condition` took `)` as the token that ends the declaration arm, so `for (int i = 3; int j = i; ...)` never parsed. | 38 → 46 |

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
scope and over an automatic array.
