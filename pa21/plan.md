# PA21 Plan — `cppgm++ --emit-lowir` with full `constexpr`

## Stage Design

PA21 turns PA20's pragmatic constant-expression subset into a real
constant-evaluation layer and finishes the object-model work the earlier LowIR
milestones deferred. Two ownership lines carry the whole assignment:

- **Constant evaluation** (`sema_constant.cpp`, `sema_constexpr.cpp`,
  `sema_value_expression.cpp`). Today `SemaConstant` is `{TypeId, bits}` and a
  constexpr call is a re-read of a body 7.1.5p3 restricts to one `return`.
  PA21 needs typed constant values (floating, pointer, reference, array, class
  object), statement execution inside a constexpr body, and one engine shared
  by ordinary semantics, template arguments and `static_assert`.
- **Static-storage lowering** (`sema_analyzer.cpp` `record_storage`,
  `sema_lifetime.cpp`, `lowir_local_static.cpp`). Block-scope `static` objects
  were refused outright; 6.7p4's guard, 3.6.2's constant initialization and
  3.6.3p3's `__cxa_atexit` registration are PA21's. **Done — checkpoint L.**

Reference-binary note: `pa21/cppgm++-ref` exists and answers PA21 inputs, so
naming and lowering shapes are probed rather than guessed.

## Current Failure Map

Baseline at turn start 29/129; now **38/129**. 91 failures remain.

| Group | Shape | Count |
|---|---|---|
| O. object/pointer/reference constants | `X is not a constant expression`, `not a class a constant expression builds an object of` | ~32 |
| V. `constexpr` validation | a `-bad` case this compiler accepts (exit status inverted) | 10 |
| I. LowIR mismatch, not exit status | canonical diff only | 10 |
| N. `noexcept` operator | `a constant expression holds an operator PA11 does not evaluate` | 6 |
| F. floating constants | `a literal that has no integral value`, `a type that is not integral` | ~10 |
| S. constexpr statements | `body is outside what 7.1.5p3 leaves a constant expression to read`; one parse failure (`for` condition declaration) | 5 |
| T. template-id callees | `X<...> is written where a constant expression calls and names no function` | 6 |
| misc | lookup/overload gaps reached only through constant evaluation | ~12 |

Next checkpoint should be **S+F**: a statement-executing constant evaluator over
typed values. It is the common root of O, F and much of the misc group, and the
README's design notes name it explicitly.

## Active Checkpoint — none open

## Performance Model

| Path | Shape | Measured |
|---|---|---|
| local-static symbol | one flatten per declaration, memoised in `entity_symbols_`; a name used *n* times costs one flatten and *n* lookups | 400 image-initialized statics in one body: 0.016s |
| local-static guard | one image read per declaration (`global_image` called once, its result reused for the guard decision), one guard global per object | 400 guarded statics: 0.025s, 800 globals |
| shared-definition naming | `local_static_places_` keyed by function symbol; two units reading one inline body agree without comparing text | two units, one `@__local_static__shared__v__local0` |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. Carried two general fixes it needed: 8.5.2p1 for a string literal initializing an array *element* (`char a[1][2] = {"x"}`), and 3.6.2's fold of a call in a block-scope static's initializer not odr-using the callee. | 29 → 38 |

**Known gap (L).** The reference names a local static of an *inline* or
*instantiated* function by source position (` at file:line:col`, hex-encoded),
because token indices are not stable across translation units. Phases 1-7 drop
positions before `SemaToken`, so this uses a translation-unit-stable occurrence
index (`local<n>`) instead — correct and stable, but spelled differently, so
`300-nested-function-template-local-static-array` and
`300-class-template-static-reference-dynamic-initialization` still differ.
`300-function-local-static-array-guard` needs two unrelated things: the ref
emits a dead `@__strlit__` for a literal consumed by 8.5.2, and our subscript of
a multi-dimensional array emits one `unary decay` the ref does not.
