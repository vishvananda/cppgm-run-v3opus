# PA15 `cppgm++ --emit-lowir` Plan

Status: **PA15 complete and audited** — `pa15/tests` 108/108,
`make test-report-through-pa15` 1173/1173, file audit clean.

## Stage Design

PA15 lowers the resolved PA12 procedural program into PA13 LowIR. The one
architectural decision is where the resolved program lives: **the PA12 dump tree
becomes the resolved procedural tree**, and each of its nodes carries typed
facts.

| Owner | Responsibility |
| --- | --- |
| `dev/src/sema_facts.h` | `FactKind` / `SemaFact`: what a resolved node is, typed |
| `dev/src/sema_*.cpp` | records those facts while PA11/PA12 already walk the tree |
| `dev/src/lowir_abi.{h,cpp}` | 3.5p9: the name the object file gives a declaration, built for PA14's encoder |
| `dev/src/lowir_lower.{h,cpp}` | program layer: symbols, globals, function shells, 3.6.2 startup |
| `dev/src/lowir_lower_body.cpp` | function layer: blocks, statements, expressions |
| `dev/src/lowir_write.cpp` | the only place LowIR text is spelled |
| `dev/src/lowir_emit.{h,cpp}` | the `--emit-lowir` driver mode |

`SemaDialect::Lowering` is the PA12 walk with `record()` enabled. `record()` runs
from `respell()` and from the one dispatch point in `expression()`, so a line's
text and its facts are one description written once. Nothing downstream parses
dump text or re-reads syntax: the lowering sees `FactKind`, `TypeId`,
`ValueCategory`, `SemaEntity*`, the operator token, the folded constant, and the
one spelling no fact can hold (a floating literal's tokens, a string literal's
code units, a label's identifier).

Facts added to the semantic layer, each because only the analysis can know it
and only a later stage needs it:

* `SemaFact::operands` — 5p9/5.9p2, the one type a built-in binary operator
  brings both operands to. A comparison's own type (`bool`) does not say it.
* `SemaEntity::object_definition` — 3.1p2, whether a namespace-scope declaration
  defines the object or only declares it (`global` vs `declare global`).
* `SemaEntity::promotion` — 4.5p3/7.2p5, the type an unscoped enumeration is
  promoted to, known only once its enumerators have been read.
* `SemaEntity::c_linkage` — 7.5p1/7.5p4, the innermost linkage-specification a
  declaration is written in, which no type says and which 3.5p9 makes the object
  name of an external `"C"` entity.
* `SemaEntity::internal_linkage` — 3.5p3, whether a namespace-scope name belongs
  to this translation unit alone: `static`, or `const` and not `extern`.
* `TypeTable::user_qualified_name` — 3.4.3, the same class or enumeration named
  from outside every region around it. The dump spells a type as its declaration
  wrote it, and an object-file name cannot.
* `SemaAnalyzer::Default` — 8.3.6p4/p9, each declaration's default-arguments
  *and*, per parameter, the region that introduced it, because that is where the
  default is read.

### The object-file boundary

3.5p9 makes the object file, not the language, name an entity. PA14 owns that
encoding; `lowir_abi.cpp` is the one place that builds its typed target - the
regions around a declaration, its language linkage, its internal linkage, its
parameter types, 13.5's operator terminal - and hands it to the shared
`abi_mangle::mangle_target`. The fact-file parser stays in `dev/abimangle.cpp`,
a reader for the standalone tool and not part of this path. Only the difference
is carried: a symbol whose object name is already its LowIR name (`main`, an
external `"C"` function) writes no `object=`.

### Reference lowering conventions reproduced

Deduced from the checked-in `.ref` files and held to deliberately:

* an lvalue's `LowValue` operand *is* its storage (`$slot`, `@global`, or a
  `ptr` temporary), so reading is a `load` and `&` is at most an `addr`;
* an assignment and a prefix `++` hand back the object they wrote *and* the
  value they wrote (`LowValue::held`), so reading the result costs no reload;
* a built-in binary operator reads **both** operands to values before either is
  converted (`as_value` then `converted`), which is what orders
  `const i64 4` before `convert sext i64 i32 2`;
* a simple assignment evaluates its right operand first; a compound assignment
  reads its left operand first; a call evaluates its arguments before its callee;
* branch context tests an integer directly and compares a pointer or a floating
  value with zero; the value context of `&&`/`||` materializes truth at `i64`
  and keeps calling the result `bool`, so a use of it widens from `u8`;
  a short circuit the left operand alone decides omits the right one entirely;
* an integral immediate is respelled at its destination type when the
  destination is signed or the same width, and goes through `convert`
  otherwise; a same-width signedness change, an integer read as an address, and
  `nullptr` read as a pointer are each a `copy`;
* pointer arithmetic scales through `binary mul` into a byte `index i8`, and an
  element one byte wide needs no scale at all;
* `unary decay` marks where a *declared* entity becomes a pointer view of
  itself, so a string literal - which no declaration names - gets only `addr`;
* a conditional chooses an object (`condaddr__`) when its use needs one and a
  value (`cond__`) otherwise, and neither when the value is discarded
  (`discard_cond_*`);
* block labels are reserved where their construct starts; a switch reserves
  every label its substatement writes before reading it, and a `goto` label is
  reserved at whichever of its mentions comes first;
* generated slots (`cond__`, `condaddr__`, `land__`, `lor__`, `refarg__`) share
  one counter that keeps rising past any source name; a shadowed local slot gets
  a `__shadowN` suffix; and a temporary skips any name a parameter already has.

### Fixes this milestone made outside the lowering

Each is a rule the earlier layers had not needed yet, found by a PA15 test or by
the audit sweeps, and fixed where the rule lives rather than where it was seen:

* 5.2p1 — a named cast is a postfix-expression, so a call may be written on it.
* 5.2.9p1/5.4p4 — a cast to a reference type makes the same thing of its
  operand whether it was spelled `(T)x` or `T(x)`; both forms now ask one place.
* 6.6.3p3 — a function returning `void` may return a `void` expression.
* 6.1/6.6.4 — labeled statements and `goto`, with labels matched against the
  gotos that name them once the body has been read.
* 4p1/4.12 — clause 4 is a sequence, so 4.2 and 4.3 stand before the conversion
  to `bool`: an array or a function is the pointer it converts to, and that
  pointer is what `!`, a condition and `?:` compare with zero.
* 8.3.6p4/p9 — default-arguments accumulate over a function's declarations, and
  each is read in the region that introduced it.
* 8.5.1p3/p6 — a braced list initializing an aggregate whose element is itself
  an aggregate, and a list with more clauses than the array has elements.

## Performance Model

**Dominant operation.** One walk of the resolved nodes of a translation unit,
emitting LowIR instructions. Everything the walk asks is a probe: one
`unordered_map` lookup per named entity, one `unordered_set` probe per slot
name, one per symbol declared or defined, one per top-level entry emitted, and
one per (name, signature) pair. A namespace-scope name is flattened - and a
function's signature described and signed, and its ABI name encoded - **once per
declaration**, not once per use. Expected cost is linear in resolved nodes, and
output is linear in them too.

The one super-linear shape left is `LowirFunctionLowering::holds_label`,
consulted only when a statement follows a terminator; it is `O(subtree)` and is
asked at most once per unreachable statement. Sibling statements hold disjoint
subtrees, so a body of n nodes costs `O(n)` reachable and `O(n * depth)` only
where a switch label is buried inside nested dead code. PA10's parse-depth guard
bounds that depth, so it cannot run away.

Measured on this machine, one process each. Each series doubles its input:

| Series | Input | Wall | Lines |
| --- | --- | --- | --- |
| top-level functions | 4000 / 8000 / 16000 | 0.07 / 0.16 / 0.35s | 64004 |
| top-level globals | 4000 / 8000 / 16000 | 0.05 / 0.10 / 0.20s | 16005 |
| calls in one body | 10000 / 20000 / 40000 | 0.19 / 0.40 / 0.79s | 160010 |
| overloads of one name | 500 / 1000 / 2000 | 0.01 / 0.03 / 0.06s | 14004 |
| expression depth (`x+x+...`) | 8000 | 0.06s | 16006 |
| short-circuit chain | 1600 terms | 0.03s | 23994 |
| nested `if`/`else` | 240 deep | 0.01s | 2656 |
| value-initialized array | 400000 elements | 0.00s | 12 |
| whole suite | 108 translation units | 0.48s total | — |

Every series doubles when its input doubles. For comparison, the same inputs
through `--emit-semantics` alone cost 0.05 / 0.11 / 0.23s and 0.03 / 0.07 /
0.14s, so the lowering is a constant factor on a linear frontend rather than a
curve of its own.

Re-measure when PA16 aggregates raise the per-object work of initialization.

## Architecture Review

What the audit checked, beyond the checkpoint conclusions:

* **Typed fact flow, end to end.** No file under the lowering reads
  `DumpNode::text`; every decision comes from `SemaFact`, `TypeId` or
  `SemaEntity`. The one text the lowering does read is `LowType::text`, which is
  the LowIR spelling it is producing.
* **Ownership.** One place says what a type is worth as bits (`narrowed`), one
  says what an array or function is worth as a value (`as_value`), one says what
  a symbol is called internally (`LowirSymbolTable`) and one what the object
  file calls it (`lowir_abi.cpp`), one reads a braced list (`list_initialize`),
  one closes the program's initialization (`LowirProgramBuilder::finish`).
* **Parallel fallbacks.** The two readings of a braced list - one for a
  declaration's initializer and one for a list standing where an expression does
  - were the only pair, and are now one.
* **Differential sweep.** 109 synthesized programs across the PA15 boundary were
  run through both `cppgm++ --emit-semantics` and the reference binary; every
  disagreement was judged and is now fixed or is a case both reject.
* **Behaviour.** 28 programs were compiled, lowered through
  `lowir2cy86` and `cy86`, and run. 26 return 0; one returns its own value by
  design, and one exercises a negative immediate, which PA13's own reference
  CY86 spells as `move64 x64 -42;` - a form PA9's grammar does not admit. That
  is a property of the PA13 text oracle, not of this stage's LowIR.
* **Valgrind.** All 108 suite inputs and the audit probes run clean under
  `valgrind -q --error-exitcode=9`.

## Final Architecture Review

What a later stage should know before extending this:

* `initialize_array` walks bounded arrays element by element while there are few
  enough elements for that to be a description of them, and emits one `zeroinit`
  past 64 bytes or for an element no single store can hold. PA16's class members
  and larger aggregates should reach for `copyobj` at the same seam.
* `LowirProgramBuilder` holds one `__cppgm_init`, one string-literal map, and
  one set of emitted top-level entries. All three belong to the program, so a
  second translation unit adds to them rather than restarting them.
* `object=` and `linkage=c` are ignored by the relaxed LowIR comparison. They
  are produced from typed facts anyway, and the 76 checked-in references that
  spell `object=` are reproduced byte for byte, so the encoder has an oracle
  even though the harness does not enforce one.
* `unwind=no` is the one metadata key the references carry that this stage does
  not: it comes from 15.4 `noexcept`, and exceptions are out of the PA15
  boundary. It belongs to the exception-aware milestone.

## Audit

### Findings

| # | Finding | Where |
| --- | --- | --- |
| 1 | Externally meaningful symbols were spelled by flattening the qualified name; no `object=` and no `linkage=c` were produced at all, and every `static` function was published `binding=strong`. The relaxed comparison drops all three keys, so nothing had asked. | `lowir_lower.cpp`, `sema_analyzer.cpp` |
| 2 | A second translation unit with 3.6.2p2 dynamic initialization opened a second `^entry` in `__cppgm_init` and restarted its temporaries - duplicate block label, duplicate `%t1`. | `lowir_lower.cpp` |
| 3 | String-literal globals were numbered per translation unit, so two units both defined `@__strlit__1` and the second unit's pointers named the first unit's data. | `lowir_lower.cpp` |
| 4 | `!a`, `if (a)`, `while (a)`, `for (; a;)`, `do…while (a)` and `a ? :` over an array or a function were refused, though 4.2/4.3 precede 4.12 and the reference accepts them. | `type_model.cpp` |
| 5 | Where such an operand did reach a conversion, the bare address came back for *any* target: `bool b = a;` stored a `ptr` into a `u8`, `(long)a` stored one into an `i64`, a branch tested a raw pointer, and `!` compared at `obj<…>`. | `lowir_lower_body.cpp` |
| 6 | Default-arguments were recorded only by a function *definition*, so `int f(int x = 3); int f(int x){…} f();` was refused; and 8.3.6p9's region was the last declaration read rather than the one that introduced each default. | `sema_analyzer.cpp` |
| 7 | An array initializer with more clauses than elements was accepted by `--emit-semantics` and only refused by the lowering. | `sema_analyzer.cpp` |
| 8 | `int a[2][3] = {{1,2,3}}` was refused: a braced list standing where an expression does insisted on a scalar. Where a nested list did reach the global path it was folded to its first element, so `{{1,2,3},{4,5,6}}` wrote two items instead of six. | `sema_overload.cpp`, `lowir_lower.cpp` |
| 9 | 8.5p7 value-initialization was one store per element, which cannot describe an element no single store holds (`store obj<12x4> 0`) and grows with a bound the source only wrote a number for: `int a[400000] = {}` was 800010 lines and 1.6s. | `lowir_lower_body.cpp` |
| 10 | A cast folded to a constant kept the operand's value, so `int gi = (char)200 + 1;` held 201 and `c == (char)200` compared against 200. | `lowir_lower.cpp`, `lowir_lower_body.cpp` |
| 11 | `function_definition` and `global_variable` scanned every entry already emitted before adding one, and `function_symbol` rebuilt a full type description and rescanned the overload list at every use. 16000 functions cost 0.85s against 0.23s to analyse them. | `lowir_lower.cpp` |
| 12 | Which signature a name's symbol belonged to was a walk of the ones already named. | `lowir_lower.cpp` |

### Changes

* **`object=`, `linkage=c` and `binding`** — new `dev/src/lowir_abi.{h,cpp}`
  builds PA14's typed encoder target from resolved declarations and types, with
  13.5's operator terminal and 8.3.5's parameter types; `AbiTargetRecord` gained
  the internal-linkage marker a data name carries, and `dev/abimangle.cpp` gained
  the `internal-variable` spelling for it. 7.5p1 language linkage and 3.5p3
  internal linkage became facts on `SemaEntity`, and a class or enumeration
  carries its qualified name beside the name the dump spells.
* **One program-owned initialization** — the startup body's counters and the
  string-literal map moved to `LowirProgramBuilder`; a unit resumes the block
  the last one left open and `finish()` closes it.
* **Array and function as truth values** — `contextually_bool` admits them, and
  `converted`, `truth_for_branch`, `truth_value` and `!` all bring the operand
  to a value first, so one rule decides what the pointer is worth.
* **Default arguments** — recorded by every function declaration, accumulated,
  with 8.3.6p9's region held per parameter.
* **One reading of a braced list** — `write_initializer` delegates to
  `list_initialize`, which walks an aggregate's elements and admits a nested
  list; the global path writes a nested list's items in place.
* **`zeroinit`** — value-initialization past 64 bytes, or of an element with no
  scalar store form, is the span it is.
* **One rule for what a type holds** — `LowirUnitLowering::narrowed`, used by
  both constant folds and by every literal spelled.
* **Indexed** — the program's emitted entries, each entity's symbol, and each
  name's signatures.

### Performance Evidence

The table under **Performance Model** is the evidence: four doubling series and
five shape probes, each measured after the changes. Before them, top-level
functions cost 0.09 / 0.29 / 0.85s for 4000 / 8000 / 16000 (3.2x and 2.9x per
doubling) and globals 0.06 / 0.28 / 0.74s, against a linear
`--emit-semantics` on the same inputs; both now double when their input doubles.
`int a[400000] = {}` went from 800010 lines and 1.6s to 12 lines and 0.00s.

### Validation

* `perl scripts/cppgm_file_audit.pl --stage pa15 --paths dev/src` — pass.
* `make test-report-through-pa15` — 1173/1173.
* `pa15/tests` — 108/108, and the 76 references that spell `object=` match the
  emitted metadata byte for byte.
* Differential sweep of 109 synthesized programs against the reference
  `cppgm++ --emit-semantics` — no disagreement remains.
* Behavioural sweep of 28 programs run through `lowir2cy86` and `cy86` — all
  accounted for.
* `valgrind -q --error-exitcode=9` over all 108 suite inputs — clean.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Source-to-LowIR spine: typed facts on the resolved tree, LowIR builder, text writer, `--emit-lowir` driver; functions, globals, every statement incl. `switch`, and the scalar/pointer/array/reference expression subset | pa15 0 → 87/108 |
| 2 | 3.6.2 startup initialization; `void` return expressions; discarded conditionals; literal short-circuit folding; 4.5p3 wide-enum promotion | pa15 87 → 94/108 |
| 3 | String literals as array objects with their own globals; braced-init-list as an expression; `nullptr` as its own type | pa15 94 → 101/108 |
| 4 | 8.3.6 default arguments read in the declaring region; shared reference-cast rules; block-scope type aliases; 6.1/6.6.4 labels and `goto`; 5.2p1 postfix suffixes after a named cast | pa15 101 → 108/108, through-pa15 1173/1173 |
| 5 | Audit: findings 1-12 above — the object-file boundary through PA14's encoder, one program-owned initialization, 4.2/4.3 before 4.12, 8.3.6p4 accumulation, one reading of a braced list, `zeroinit`, one narrowing rule, and the indexes that made the stage linear | pa15 108/108, through-pa15 1173/1173 |

## Current Failure Map

Empty: `pa15/tests` is 108/108 and the through-pa15 report is 1173/1173.

## Active Checkpoint

None open. The PA15 contract is met and audited; PA16 extends this same
procedural lowering path into the non-virtual object model.
