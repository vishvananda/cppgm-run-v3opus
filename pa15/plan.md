# PA15 `cppgm++ --emit-lowir` Plan

Status: **PA15 complete** — `pa15/tests` 108/108,
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
* `SemaAnalyzer::Defaults` — 8.3.6p9, each function's default-arguments *and*
  the region they were written in, because that region is where they are read.

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
  value with zero; the value context of `&&`/`||` materializes truth at `i64`;
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

Each is a rule the earlier layers had not needed yet, found by a PA15 test and
fixed where the rule lives rather than where it was noticed:

* 5.2p1 — a named cast is a postfix-expression, so a call may be written on it
  (`static_cast<bool(&)(char,char)>(fn)(':',':')`).
* 5.2.9p1/5.4p4 — a cast to a reference type makes the same thing of its
  operand whether it was spelled `(T)x` or `T(x)`; both forms now ask one place.
* 6.6.3p3 — a function returning `void` may return a `void` expression.
* 8.5.4 — a braced-init-list may stand where an expression initializes an
  object; over the scalar subset that is at most one clause, empty meaning zero.
* 6.1/6.6.4 — labeled statements and `goto`, with labels matched against the
  gotos that name them once the body has been read.

## Current Failure Map

Empty: `pa15/tests` is 108/108 and the through-pa15 report is 1173/1173.

## Active Checkpoint

None open. The PA15 contract is met; PA16 extends this same procedural lowering
path into the non-virtual object model.

What a later stage should know before extending it:

* `initialize_array` walks bounded arrays element by element. When PA16 brings
  class members and larger aggregates, `copyobj` / `zeroinit` are the LowIR
  forms that replace a long store sequence, and the element loop is the place
  to decide between them.
* `LowirProgramBuilder` holds one `__cppgm_init`; a second translation unit with
  dynamic initialization appends to the same body but restarts its temporary
  numbering, which is invisible today because no test links two such units.
* Externally meaningful symbols are still spelled from the flattened qualified
  name rather than through PA14's Itanium encoder. The relaxed comparison
  ignores `object=`, so nothing gates it yet; PA16's object model is where the
  encoder should be wired in, since that is where the names stop being
  derivable from the name alone.

## Performance Model

Nothing here rescans or rebuilds. Per function: one walk of its resolved nodes,
one `unordered_map` probe per named entity, one `unordered_set` probe per slot
name. Per program: one `unordered_set` probe per symbol declared or defined, and
one linear scan of `program_.functions`/`globals` per definition to reject a
repeat — bounded by the number of top-level entities, which is why it has not
been indexed yet.

The one super-linear shape is `LowirFunctionLowering::holds_label`, consulted
only when a statement follows a terminator; it is `O(subtree)` and is asked at
most once per unreachable statement, so a body of n nodes costs `O(n)` reachable
and `O(n * depth)` in the worst dead-code case. PA10's parse-depth guard bounds
that depth, so it cannot run away.

Measured (this machine, one process each):

| Case | Shape | Lowered | Wall |
| --- | --- | --- | --- |
| whole suite | 108 translation units | — | 0.35s total |
| multiplicity | 4000 locals + 4000 assignments | 20005 lines | 0.11s |
| depth | `x + x + ...` 20000 deep | 4008 lines | 0.05s |
| switch multiplicity | 4000 switches | 64008 lines | 0.14s |
| aggregate multiplicity | 3000-element array initializer | 6010 lines | 0.01s |
| dead code | `return` before 200-deep nested blocks | — | 0.00s |

Every curve is linear in emitted lines. Re-measure when PA16 aggregates raise
the per-object work of initialization.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Source-to-LowIR spine: typed facts on the resolved tree, LowIR builder, text writer, `--emit-lowir` driver; functions, globals, every statement incl. `switch`, and the scalar/pointer/array/reference expression subset | pa15 0 → 87/108 |
| 2 | 3.6.2 startup initialization; `void` return expressions; discarded conditionals; literal short-circuit folding; 4.5p3 wide-enum promotion | pa15 87 → 94/108 |
| 3 | String literals as array objects with their own globals; braced-init-list as an expression; `nullptr` as its own type | pa15 94 → 101/108 |
| 4 | 8.3.6 default arguments read in the declaring region; shared reference-cast rules; block-scope type aliases; 6.1/6.6.4 labels and `goto`; 5.2p1 postfix suffixes after a named cast | pa15 101 → 108/108, through-pa15 1173/1173 |
