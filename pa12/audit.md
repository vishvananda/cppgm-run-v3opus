# PA12 Audit — `cppgm++ --emit-semantics`

An independent review of the landed checkpoint, in the order a fact travels:
parse, name, region, type, value, dump.

## Current Checkpoint Review

**C1 — the `--emit-semantics` spine (groups A–E), landed at `066751b6` /
`fa4e6875`.**

The spine is a sound base for the class and template layers above it. One walk
of the PA10 tree serves both dumps, an expression is analysed bottom up in one
visit, and the three facts PA11 did not carry — the declarations of one function
name, the qualified spelling a declaration is dumped under, and the analysed
`Value` — are each held at one owner. What the review changed is below; nothing
in it moved a boundary.

### Value and dump

- **An argument that converted was written in the wrong place.** A reference
  parameter that binds a converted temporary writes the conversion as a
  `cast-expression` around the operand. It was opened as a new last line of the
  call and the operand was then erased from where it had been, so with
  `f(const long long&, int, int)` the call `f(u, 1, 2)` wrote `1`, `2` and only
  then the converted `u`. Every argument after the first converted one was out
  of order, and the erase cost a scan of the arguments already written.
  `SemaModel::wrap_node` now puts what a line said under a new line in the place
  that line already had, in constant time, which is what a conversion written
  around an operand needs. The three conversions the dump makes visible — the
  materialized temporary, the null pointer constant, and the resolved overload
  set — now all rewrite in place.

- **An overloaded name with no target was accepted and wrote a blank line.**
  13.4p1 gives an overloaded function name no type until something chooses
  between its declarations, and the name's line is written when it is chosen. In
  the positions that discard a value — an expression-statement, a `for`
  iteration expression, the left operand of a comma — and in the operands of
  `decltype` and `sizeof`, nothing chose and nothing refused, so `f;` and
  `(f, 1)` were accepted and wrote a line with no text into a dump the format
  calls deterministic. Each of those five now asks.

- **`&f` refused what 13.4p1 resolves.** Taking the address of an overloaded
  name is one of the contexts a target type chooses in, and it threw. The set
  now travels up through `&` as it does through the name itself, and the target
  writes both the pointer's line and the name's under it.

### Type

- **An enumeration could only be compared with itself.** `A == i` and `A < i`
  for an unscoped enumeration and an integer were rejected, as were two
  different unscoped enumerations, while two operands of one scoped enumeration
  type were rejected as well. 5.9p2 and 5.10p1 are now one rule: two operands of
  one enumeration type compare as they are, and otherwise the usual arithmetic
  conversions bring two arithmetic or unscoped enumeration operands together.

- **`--` on a `bool`** was written in the comment and not in the code (5.2.6p1,
  5.3.2p1).

- **The analyzer held its own copies of five facts about a type.**
  `is_arithmetic`, `is_integral`, `is_floating`, `is_object_pointer` and
  `contextually_bool` ask nothing of the analysis, and `is_scoped` was a second
  spelling of `TypeTable::is_scoped_enum`. They are now asked of the table that
  holds types, which is also what brought `sema_analyzer.h` back under the file
  audit's weight limit.

### Name and complexity

- **Declaring the nth overload of a name cost a walk of the n−1 before it.**
  13.1 tells two declarations apart by their parameter type list, and the
  question was asked by walking the chain the name heads, so a name with N
  declarations cost O(N²): 16000 declarations of one name took 3.05 s against
  0.35 s for 16000 distinct names. The chain is now indexed by that list on the
  entity the name is bound to, and holds its own last link, so declaring is a
  probe and a link: **3.05 s → 0.26 s** at 16000, **0.58 s → 0.13 s** at 8000.
  Resolution still walks the chain, which is the O(candidates) 13.3 asks for.

- **A call's callee name was looked up twice**, once to learn it did not name a
  type (5.2.3 makes `T(x)` a cast the grammar cannot tell from a call) and again
  by the expression layer. `named_value` is handed the answer.

- `select_overload` addressed its per-candidate match rows through
  `&matches[0]`, which for a call with no arguments indexes an empty vector.

### Confirmed intact

- 672 / 672 through PA11, and PA12 held at its 156 / 166 turn-start baseline
  with the same ten open tests. File audit clean, no warnings.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate or file-audit bypass. The two `catch` sites both catch `NotConstant`
  alone, which separates "this is not a constant" from a fact about the program.
- The `SemaDialect` gate on 8.3.5p5 parameter adjustment is a difference between
  two output contracts — PA11 spells a declarator as written — not a behavioural
  shortcut.
- Depth is bounded by the PA10 parse guard, so it costs stack rather than time:
  500 nested parenthesized operands in 0.00 s, 1000 refused; 800 nested unbraced
  `if` substatements ending in a declaration in 0.00 s, the new
  `parse_substatement` retry costing one failed statement parse per level rather
  than a doubling.
- The ten open tests are all group F, G, H and I features — implicit constructor
  synthesis and `member-expression`, a `MemberPointer` type category, template
  argument substitution, and one PA10 parse gap — not shortcuts in the spine.

### Durable architecture decisions

- One analyzer serves both dumps: a declaration is read once and written to
  whichever tree `SemaDialect` asks for.
- A name binds the head of the declaration chain of one function name in one
  region; the chain is indexed by parameter type list for declaring and walked
  in order for resolving.
- A `Value` carries one analysed expression up from operand to operator,
  including the dump line it wrote, so a conversion rewrites that line in place
  rather than the output being built in a second pass.
- A fact about a type alone belongs to `TypeTable`; a fact about a declaration
  belongs to `SemaEntity`, built where the declaration is read.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | argument conversions written out of order; an unresolved overload set accepted in five discarding contexts; `&f` refused; enumeration comparisons rejected; `--` on `bool` accepted; O(N²) overload declaration; callee looked up twice; two type-fact owners | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05 s → 0.26 s; file audit clean |
