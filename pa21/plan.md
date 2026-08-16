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

**38/129**; 91 failures remain, unchanged by the checkpoint L audit.

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

Re-measured at the checkpoint L audit, best of three against `pa21/cppgm++-ref`.

| Path | Shape | Measured |
|---|---|---|
| local-static symbol | one flatten per declaration, memoised in `entity_symbols_`; a name used *n* times costs one flatten and *n* lookups | 400 / 1600 / 6400 image-initialized statics in one body: 0.018 / 0.062 / 0.247s (ref 0.584 / 0.743 / 1.403s) |
| local-static guard | one image read per declaration (`global_image` called once, its result reused for the guard decision), one guard global per object | 400 / 1600 / 6400 guarded statics: 0.026 / 0.096 / 0.423s (ref 0.632 / 0.933 / 2.229s) |
| shared-definition naming | `local_static_places_` keyed by function symbol, and the owner part is the function's own symbol wherever a spelling would name two functions | two units over one header: five globals, both units reaching the same one |
| array destruction | one `__cxa_atexit` per array, handed a generated body that walks the elements - written out below `kArrayLoopLimit` and a loop above it | 100000-element array of class type: 0.005s, 11 instructions |

## Completed Checkpoints

| # | Checkpoint | Result |
|---|---|---|
| L | Function-local `static` objects: `record_storage` records the storage duration instead of refusing it; `record_lifetime` puts 12.4p11's destruction under the declaration; `global_symbol` answers `__local_static__<function>__<name>__tokens<b>_<e>`; `lowir_local_static.cpp` writes the image half, 6.7p4's guard and 3.6.3p3's `__cxa_atexit`. Carried two general fixes it needed: 8.5.2p1 for a string literal initializing an array *element* (`char a[1][2] = {"x"}`), and 3.6.2's fold of a call in a block-scope static's initializer not odr-using the callee. | 29 → 38 |
| L audit | `af299cb9`, 3 blockers: the owner part of the name flattened two functions to one symbol, so two instantiations of one function template shared a global; 3.5p3's internal linkage was missing from both readings of "a definition every unit may hold"; and 12.4p8 over an array was handed to `__cxa_atexit` as one call, ending one lifetime of *n*. See [audit.md](audit.md). | 38 → 38 |

**Known gap (L).** The reference names a local static of an *inline* or
*instantiated* function by source position (` at file:line:col`, hex-encoded).
Phases 1-7 keep no position — `IncludeTable` records only whether a token was
read from this unit's own file — so this uses a per-function occurrence index
(`local<n>`) beside an owner part that is the function's own object-file
identity wherever a spelling would name two functions. That is unique across the
program and agreed on by the units of one invocation, but spelled differently
from the reference, so `300-nested-function-template-local-static-array` and
`300-class-template-static-reference-dynamic-initialization` differ by their
symbol names alone. `300-function-local-static-array-guard` needs two unrelated
things: the ref emits a dead `@__strlit__` for a literal consumed by 8.5.2, and
our subscript of an array *element* that is itself an array emits one
`unary decay` the ref does not — which is general, reproducing at namespace
scope and over an automatic array.
