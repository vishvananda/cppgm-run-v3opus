# PA14 `abimangle` Plan

Status: **PA14 complete and audited** — `pa14/tests/abi` 111/111,
`make test-report-through-pa14` 1065/1065, file audit clean.

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
table on a **canonical form** of each candidate, not on node identity or on
emitted text:

* two buffers, `out` (real output) and `canon`;
* `put()` appends to both, `put_out()` only to `out`;
* a candidate is encoded **speculatively**: mark `out`/`canon`/table, encode,
  take `canon.substr(mark)` as the key, then either register it or roll all
  three back and emit `S<n>_`;
* a registered candidate's canonical span collapses to a **slot token**, and
  emitting `S<n>_` appends that same token, so one key names its registered
  children by slot instead of expanding them again. Equal structures still
  produce equal keys, because equal children collapse to equal tokens.

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
* A `let-entity` symbol is encoded in **its own** substitution scope. gcc shares
  one table across `L <mangled-name> E`
  (`_ZN2ns6HolderIXadL_ZN1C1mEEEE1fERS1_`), but
  `300-function-owner-member-pointer-nttp-data.ref` requires the isolated form
  (`...1fER1C`), and checked-in fixtures gate. A `member-external-address`
  member is *constructed in place* rather than named by a fixed symbol, so it
  does join the enclosing stream, which is what both gcc and the reference do.

## Current Failure Map

Empty: `pa14/tests/abi` is 111/111 and the through-pa14 report is 1065/1065.

## Performance Model

Dominant operation: one speculative encode per substitution candidate. Each
candidate's key is proportional to that node's own structure, because its
already-registered children collapse to slot tokens, so a whole name costs
O(number of components). Duplicate detection is a hash lookup, and a repeated
definition binder is one `known_keys` lookup with no structural walk at all.

Measured with `dev/abimangle` on synthesized fact files (wall clock, release
build; "before" is the pre-audit binary at commit `940b1e4a`):

| Shape | Size | Before | After |
| --- | --- | --- | --- |
| shared subterm named twice per level | depth 20 / 22 / 24 | 0.03s 29MB / 0.14s 100MB / 0.59s 398MB | 0.00s 4MB at every depth |
| shared subterm named twice per level | depth 3000 | out of memory | 0.05s |
| distinct member types over one owner chain | 2000 types, depth 400 / 1200 | 0.26s / 0.81s | 0.08s / 0.08s |
| pointer chain through binders | 1000 / 2000 / 3900 deep | — | 0.01s / 0.03s / 0.06s |
| distinct class parameters | 1000 / 8000 / 32000 | — | 0.01s / 0.11s / 0.47s |
| one 2000-deep type repeated | 200 / 2000 / 8000 uses | — | 0.04s / 0.05s / 0.17s |
| template arguments in one specialization | 1000 / 8000 / 32000 | — | 0.01s / 0.11s / 0.45s |
| cases per file | 1000 / 8000 | — | 0.03s / 0.26s |
| dependent expression chain | 500 / 2000 / 3900 deep | — | 0.00s / 0.02s / 0.04s |

* The exponential path was the canonical key. A key used to be the *fully
  expanded* form, so `let-type Tk member-pointer T(k-1) T(k-1)` doubled it at
  every level: 24 such lines (864 bytes) cost 0.59s and 398MB, and each further
  line doubled both. Interning a registered candidate as a slot token makes the
  key linear in the node's own structure; the same input is now 0.00s / 4MB and
  depth 3000 finishes in 0.05s.
* The second super-linear path was `class_component_count`, which re-resolved
  and re-walked a member type's whole owner chain on every occurrence just to
  decide whether to emit `N`/`E`. A member name always has more than one
  component, so `is_nested_name` answers without recursing: 2000 member types
  over a depth-1200 owner chain go from 0.81s to 0.08s, and the cost stops
  growing with owner depth.
* Speculative candidate encoding is linear because a duplicate's children are
  themselves already-registered duplicates, so the speculative walk stops at the
  first level and rolls back nothing.
* `MAX_ENCODE_DEPTH` is 4000 guard entries. Cyclic facts (`let-type A ptr A`,
  `let-type A B` / `let-type B A`) fail cleanly instead of overflowing the
  stack. The guard counts entries rather than structural levels, so the usable
  depth depends on shape: 3900 for a pointer chain, ~1995 for nested local
  contexts, ~1333 for a template-argument chain. Deeper input is rejected with a
  diagnostic, never a crash.
* Fact-file parsing costs about 4KB per record because `AbiFactRecord` holds all
  three record variants inline; it is linear in input lines and confined to the
  tool boundary, since PA15+ builds `AbiTargetRecord`s directly.

## Architecture Review

Traced end to end: fact text → `parse_fact_record_words` (tool) → typed
`AbiFactCase` → `build_definition_map` → `Encoder::encode_target` → mangled
name, plus `serialize_fact_file` back to canonical text.

* One owner per concern. Fact syntax lives only in `dev/abimangle.cpp`; every
  Itanium spelling decision lives only in `dev/src/abi_mangle.cpp`. The encoder
  never sees a fact word it has not been given a typed meaning for.
* One substitution mechanism. `close_candidate` is the only place a slot is
  created or reused, `put`/`put_out` the only places output is produced.
* One rule per shape. The "parameters or `v`, then `z`" rule, the local-name
  discriminator, the closure-type name, the `L <type> <value> E` literal and the
  member cv/ref qualifier order each have exactly one implementation, shared by
  every caller.
* The encoder consumes typed facts only. No path builds a name by splicing
  supplied mangled text except `let-entity <id> symbol <mangled-name>`, which is
  the fact language's explicit escape hatch for an entity that is already known
  by symbol.

## Final Architecture Review

The audit re-derived the stage independently, used `reference-binaries/abimangle`
as a differential oracle over 8586 synthesized fact files, and adjudicated every
disagreement against the handout and the Itanium ABI (checking the ABI with host
`g++`/`c++filt`, which is an audit tool here, never a test oracle).

Result: no input the reference accepts is rejected by `abimangle`. The output
divergences that remain are all cases where the reference contradicts the ABI,
each confirmed against a compiled example:

| Divergence | ABI evidence |
| --- | --- |
| a function's namespace prefix is a substitution candidate | `_ZN2ns1fERKNS_1CE` |
| a function-template prefix is a substitution candidate | `_Z2ftIiET_S0_`, `_ZN2n21gIiEET_S1_NS_1DE` |
| a standard substitution never occupies a slot | `_Z1fSsSs`, `_Z1fRSiS_`, `_Z1hSt1QIiES0_` |
| a construction vtable's two types share one table | `_ZTC1DI1BE0_1WIS0_E` |
| arrays, closures, vendor-qualified and member-pointer types are candidates | `_Z1fPA9_1SS1_`, `_Z2g2IZ4hostvEUl3BariE_S1_EvT_T0_` |
| `bit-and` is `an`; `ad` is unary `operator&` | `_ZN1KanEi`, ABI operator table |
| `plus`/`minus` choose unary or binary from the shape | handout, "the encoder chooses from the parameter count and member/non-member shape" |
| a decimal above `LLONG_MAX` keeps its value instead of saturating, so `value ulonglong 18446744073709551615` is that number and not `9223372036854775807` | the reference ignores its own `strtoll` range error |

Two further divergences are widenings, not disagreements: `abimangle` also spells
`wchar_t`, `char16_t`, `char32_t`, `auto`, `decltype-auto`, `half` and
`ellipsis` as their Itanium builtin codes, and accepts `pack:` as a compact type
constructor beside the multiword `pack` the fixtures use. The reference reads
those words as class names. No fixture uses either spelling, and nothing the
reference accepts is rejected.

The one place the fixtures win over the ABI is the `let-entity` substitution
scope, recorded under Stage Design above.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Whole `abimangle` encoder: typed fact reader, canonical substitution table, types/names/template args/dependent expressions/special names, canonical serializer | 2/111 -> 111/111; through-pa13 954/954; audit clean |
| 2 | PA14 audit: interned substitution keys, structural member entities, eight ABI corrections, one rule per shape | 111/111; through-pa14 1065/1065; audit clean; 8000-case differential sweep with no unexplained divergence |

## Audit Findings

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | Use-after-free folding cv qualifiers: the walk assigned a resolved node into the very node it had read it from, so `type const:volatile:memberptr:C:float` freed the source vector and then copied out of it. Only visible when the operand has more children than its parent, which no fixture has | the walk holds pointers and alternates two scratch nodes, so a resolve never writes into what it is reading; it also stops deep-copying the remaining chain at every hop |
| 2 | Canonical substitution keys expanded every shared subtree, so a fact graph naming one binder twice per level doubled the key per level: 864 bytes of input cost 0.59s and 398MB, and each added line doubled both | keys intern a registered candidate as a slot token |
| 3 | `member-external-address` spliced the supplied mangled symbol and dropped its typed owner, member name, cv/ref qualifiers and parameter types; a symbol that disagreed with the facts silently won | the member name is built from the typed facts, in the enclosing substitution stream |
| 4 | A variadic function with no named parameters emitted the empty-list marker first (`_Z1fvz`, `FvvzE`) | `emit_parameter_types` is the single rule; `_Z1fz`, `FvzE` |
| 5 | GNU complex types were treated as opaque builtin codes, so a repeated `complex-longdouble` never reused a slot | `C <type>` is composite and is now a candidate (`_Z1fCeS_`) |
| 6 | A local-name discriminator of ten or more used the short spelling (`1X_10`) | `__10_`, per `<discriminator>` |
| 7 | `bool` was masked to eight bits and unsigned values were masked even when non-negative, so `value bool -1` became `Lb255E` | the modulo rule applies only to a negative value of an unsigned builtin, and `bool` is not one |
| 8 | The comparison, shift and bitwise compound-assignment operator terminals the handout requires had no accepted spelling, so `operator-terminal less` failed outright | added `less`, `greater`, `less-equal`, `greater-equal`, `shift-left`, `shift-right`, `shift-left-assign`, `shift-right-assign`, `bit-and-assign`, `bit-or-assign`, `bit-xor-assign`; dropped invented aliases, the non-overloadable `conditional`/`sizeof`/`alignof`, and `constructor-inheriting` -> `C4`, which is not an Itanium encoding |
| 9 | `typeinfo-name` was accepted and encoded as `_ZTI`, the typeinfo symbol | encodes `_ZTS` |
| 10 | Array bounds were never validated, so `array:abc:int` emitted the malformed `Aabc_i`, and the dependent `expr:<reference>` bound was unimplemented | bounds parse as a nonnegative extent or a dependent expression (`AT__i`) |
| 11 | `class_component_count` re-resolved and re-walked a member type's whole owner chain per occurrence | `is_nested_name` answers in constant time for member names |
| 12 | Duplicated emission rules: the parameter-list marker in four places, the local discriminator and closure name in two each, the integral literal in four | one helper each |
| 13 | `ABI_TEMPLATE_ARGUMENT_EXTERNAL_ENTITY` was unreachable and duplicated the member-entity kind | removed |

## Audit Changes

* `dev/src/abi_mangle.cpp`: `emit_cv_type` walks with pointers over two scratch
  nodes instead of assigning a resolved node into its own source; slot-token
  interning in `close_candidate`, which now
  returns the settled slot so `known_keys` records a live key; `is_nested_name`
  replaces `class_component_count`; `emit_parameter_types`,
  `emit_closure_name`, `local_discriminator`, `emit_integral_literal` and
  `function_qualifier_codes` become the single implementations of their rules;
  `emit_member_entity` builds a member name from typed facts; complex builtins
  become substitution candidates; `_ZTS`; corrected operator and ctor/dtor
  tables; corrected unsigned-literal rule.
* `dev/src/abi_mangle.h`: `ABI_TARGET_FACT_TYPEINFO_NAME` added,
  `ABI_TEMPLATE_ARGUMENT_EXTERNAL_ENTITY` removed.
* `dev/abimangle.cpp`: `parse_array_bound` validates the extent and accepts
  `expr:<reference>`; `serialize_array_bound` round-trips it; `typeinfo-name`
  parses and serializes as its own target.

## Audit Performance Evidence

* Shared-subterm depth 24 (864 bytes of facts): 0.59s / 398MB -> 0.00s / 4MB;
  depth 3000, previously out of reach, is 0.05s.
* 2000 member types over a depth-1200 owner chain: 0.81s -> 0.08s, and now flat
  in owner depth (0.08s at depth 400 and at depth 1200).
* Linear scaling confirmed for pointer chains (3900 deep, 0.06s), 32000 distinct
  parameters (0.47s), 32000 template arguments (0.45s), 8000 uses of a 2000-deep
  type (0.17s), 8000 cases per file (0.26s) and 3900-deep expression chains
  (0.04s).

## Audit Validation

* `pa14/tests/abi` 111/111; `make test-report-through-pa14` 1065/1065.
* `perl scripts/cppgm_file_audit.pl --stage pa14 --paths dev/src` passes, with
  and without `--include-stage-tools`.
* Differential sweep against `reference-binaries/abimangle` over 8586 synthesized
  fact files (vocabulary cross-product, structural fuzz, context/lambda/thunk
  fuzz): no input the reference accepts is rejected, and every output divergence
  is one of the ABI deviations tabulated above.
* Every disagreement was adjudicated against the Itanium document and against
  host `g++`/`c++filt` output; the demangler was also used to confirm that each
  divergent name of ours resolves to exactly the type the facts describe.
* valgrind clean (no errors, no definite leaks) over all 111 fact files and over
  the deep, wide and fuzzed shapes. The sweep over synthesized inputs is what
  exposed the cv-folding use-after-free; the fixtures alone never reach it.
* Cyclic facts still fail cleanly with a diagnostic and a failure exit status
  (`let-type A const A`, `let-type A ptr A`, `let-type A B` / `let-type B A`).
* Multiplicity and command-line surface: several input files in order, several
  cases per file, `-o` before or after the inputs, a missing `-o` operand, no
  inputs, and an unreadable input all agree with the reference.
* No regression fixtures were added: the handout states that PA14's test inputs
  and references are part of the handout, and `pa14/Makefile` has no
  course-test root to extend. The evidence for each judgment is recorded above
  instead.
* `--emit-facts` is idempotent and re-mangling the serialized facts reproduces
  every reference name.
