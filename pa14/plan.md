# PA14 `abimangle` Plan

Status: **PA14 complete** — `pa14/tests/abi` 111/111,
`make test-report ACTIVE_TEST_REPORT_PAS='pa14'` 111/111,
`make test-report-through-pa13` 954/954, file audit clean.

## Stage Design

PA14 is a standalone ABI-name constructor: read line-oriented ABI fact files,
build a typed ABI entity graph, encode Itanium C++ ABI names from it.

| Owner | Responsibility |
| --- | --- |
| `dev/src/abi_mangle.h` | typed ABI fact model + encoder API (shared with later PAs) |
| `dev/src/abi_mangle.cpp` | the Itanium encoder: substitution table, types, names, template args, dependent expressions, special names |
| `dev/abimangle.cpp` | line-oriented fact reader, canonical fact serializer, driver |

Fact text never reaches the encoder. `parse_fact_text` produces `AbiFactCase`
records and `mangle_target(target, records, definitions)` consumes only typed
facts, so PA15+ can build `AbiTargetRecord`s from semantic state and call the
same encoder. `abimangle --emit-facts` re-serializes the typed model as
canonical fact text; it is a diagnostic, not a path the encoder reads back.

### Substitution model (the core design decision)

Itanium substitution is structural, but the fact language deliberately spells
equivalent structures in different ways (`C` vs `named:C`, two `let-arg`s with
the same value, `const:volatile:X` vs `volatile:const:X`). The encoder keys the
table on a **canonical expansion** of each candidate, not on node identity or
on emitted text:

* two buffers, `out` (real output) and `canon`;
* `put()` appends to both, `put_out()` only to `out`;
* emitting `S<n>_` appends `S<n>_` to `out` but the referenced entry's key to
  `canon`, so `canon` is always the fully expanded form;
* a candidate is encoded **speculatively**: mark `out`/`canon`/table, encode,
  take `canon.substr(mark)` as the key, then either register it or roll all
  three back and emit `S<n>_`.

Two deliberate asymmetries fall out of the reference names:

* a class-like name emits `N`/`E` through `put_out` only, so the *type* entry
  and the *prefix* entry of one class share a slot (`_ZN1CcvS_Ev`,
  `_ZN12AbiTagBufferC1B9nqe220100ERKS_`);
* `<template-param>` is a candidate only when the fact says
  `template-param-subst`. Plain `template-param` is not, which is why
  `_ZSt7getlineIT_ER...` repeats `T_` while
  `_ZSt9addressofIU7_AtomicmEPT_RS1_` reuses it.

### Reference behaviours deliberately reproduced

* A `member-template-entity` template argument registers the owner's *internal*
  prefixes and its own member name, but neither the owner as a whole nor the
  entity as a whole. That is what makes the second use of
  `quote_trait<identity>::fn` come back as `NS0_IS1_ES2_E` instead of one slot
  (`300-member-template-template-function-argument`).
* Standard substitutions (`St`, `Sa`, `So`, ...) are emitted but never occupy a
  table slot, so they do not shift `S<n>_` numbering.
* `sr<owner>E<name>` keeps the `E` that the facts request for a dependent
  alias (`400-dependent-alias-type-id`) and omits it for a dependent member
  value (`500-distinct-integral-decltype-substitution`).
* A path operand is a template argument exactly when it names a `let-arg`
  binder, and a parameter type otherwise; that is the only thing separating
  `function path std::getline Char_arg` from `function path host int`.
* A local or lambda context with no terminal record names its call operator.

## Current Failure Map

Empty: `pa14/tests/abi` is 111/111 and the through-pa13 report is 954/954.

## Active Checkpoint

None. PA14 is complete; PA15 (`cppgm++ --emit-lowir` compiler-owned symbols)
is the next stage and reuses `mangle_target` directly.

## Performance Model

Measured with `dev/abimangle` on synthesized fact files (times are wall clock,
single run, release build):

| Shape | Size | Time |
| --- | --- | --- |
| pointer chain through binders | 1000 / 3000 / 3900 deep | 0.020s / 0.066s / 0.081s |
| distinct class parameters | 1000 / 8000 / 32000 | 0.018s / 0.132s / 0.552s |
| one 2000-deep type repeated | 200 / 2000 / 8000 uses | 0.042s / 0.056s / 0.103s |

* Speculative candidate encoding is linear because a duplicate's children are
  themselves already-registered duplicates, so the speculative walk stops at
  the first level.
* The repeated-deep-type shape was the one super-linear path: detecting the
  repeat still required walking the whole structure, so 2000 uses of a
  2000-deep type cost **4.255s**. `Encoder::known_keys` now records each
  definition binder's canonical key on first encode, so a later use of the same
  binder is one table lookup: the same case is **0.056s** (76x). The memo is
  keyed on the binder rather than the `AbiType` address because `resolve_type`
  and `emit_cv_type` hand back stack temporaries whose addresses are reused.
* `MAX_ENCODE_DEPTH` is 4000. Cyclic facts (`let-type A ptr A`,
  `let-type A B` / `let-type B A`) fail cleanly instead of overflowing the
  stack; 3900-deep valid input still encodes.
* Sweeps over all 111 fact files: valgrind clean (no errors, no definite
  leaks); `--emit-facts` is idempotent and re-mangling the serialized facts
  reproduces every reference name.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Whole `abimangle` encoder: typed fact reader, canonical-expansion substitution table, types/names/template args/dependent expressions/special names, canonical serializer | 2/111 -> 111/111; through-pa13 954/954; audit clean; repeated-deep-type path 4.255s -> 0.056s |
