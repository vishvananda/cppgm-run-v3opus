# PA4 `macro` Plan

## Stage Design

PA4 adds translation phase 4 between phase 3 and the PA2 phase 5-7 assembler:

```
PPTokenLexer (1-3) -> MacroExpander (4) -> PostTokenizer (5-7) -> DebugPostTokenStream
```

Ownership:

- `dev/src/pptoken_lexer.h` owns `PPTokenSource`, the pull interface both the
  lexer and the expander implement, so phases 5-7 no longer own a lexer and
  PA2 and PA4 drive the same analysis.
- `dev/src/macro_model.*` owns the typed facts phase 4 carries: interned
  spellings, hash-consed hide sets, the analysed `#define` (parameters, a
  substitution program, and the replacement list kept for 16.3/2 equality) and
  the macro table.
- `dev/src/macro_expander.*` owns the directive/text-sequence split and the
  rescanning expansion engine.

### Hide-set model (measured against `dev/macro-ref`)

Each token carries a hide set `H` and a sticky `unavailable` flag.  For an
invocation of `M` with head `h` and, for a function-like macro, closing paren
`r`:

- `acc = H(h) + {M}`, and `H(produced) = ((H(h) & H(r)) + {M})` for a
  function-like macro, `= acc` for an object-like one.
- a produced token naming an **object-like** macro in `acc` becomes
  unavailable where it is produced, because no later point decides it;
- a produced token naming a **function-like** macro is decided when a `(`
  follows it, against its own `H` -- except that a token substituted from an
  **argument** is also decided against `acc`, which is the nesting relationship
  16.3 keeps across parameter substitution.

This is the only model that reproduces every reference trace observed:
`f(a)(b)(c)(d)` chaining without limit, `OBJ -> G -> OBJ` blocking while
`OBJ -> G -> H -> OBJ` and `OBJ -> G -> OBJ2 -> OBJ` do not, `g(f)(g)(3)`
blocking `g` while `f(g)(3)` does not, and 920's deferred-expression idiom
running to the end of its input.

## Current Failure Map

Turn start: 0/72.  Turn end: **76/76** (72 checked-in plus 4 added regression
tests).  The whole PA was one behavior group, all of it now passing:

| group | tests | behavior |
| --- | --- | --- |
| directives | 100-*, 300-*, 700-redef* | `#define`/`#undef` parse, 16.3/2 redefinition, `__VA_ARGS__` placement |
| replacement | 150-*, 200-*, 250-*, 400-* | invocation boundaries, argument collection, empty and variadic arguments |
| `#` and `##` | 250-join, 300-double-hash, 310-*, 500-*, 800-* | stringize, paste and re-lex, placemarkers, GNU `, ##__VA_ARGS__` |
| rescan | 600-*, 610-*, 650-*, 900-*, 910-*, 920-* | hide sets, argument prescan, deferred expansion |

## Active Checkpoint

None: the stage is complete.  Next assignment is PA5 `preproc`, which reuses
`MacroExpander` with `#include`, `#if` (through the PA3 `CtrlExprEvaluator`),
predefined macros and the pragma operator.

## Performance Model

Measured with `dev/macro` against `dev/macro-ref`, output compared byte for
byte in each case:

| input | mine | reference |
| --- | --- | --- |
| 2.3 MB of real C++ source, 361k tokens | 0.12 s, 7.9 MB | 0.26 s, 8.3 MB |
| 4.6 MB, 60k invocations of 6 macros, 2.6M tokens | 0.85 s, 24 MB | 1.90 s, 8.2 MB |
| 6.1 MB, no macros, 600k distinct spellings | 0.87 s, 57 MB | 0.80 s, 8.4 MB |
| `f(` x 8000 nested arguments | 4.5 s | segfaults (native stack) |

- Throughput is linear in the tokens produced.  A spelling is interned once, a
  token is 16 bytes, and hide-set `add`/`intersect` are memoised on their
  operand ids, so an invocation costs copies of fixed-size records and no
  hashing of text.
- Input is streamed a logical line at a time; only replacement tokens and open
  arguments are held, so a one text-sequence file does not buffer the file.
- Memory is proportional to the *vocabulary*, not the file: the third row is a
  worst case of 600k unique identifiers.  Real source reuses spellings, which
  is the first row.
- Deeply nested arguments are quadratic in the nesting depth: each level
  materialises its argument's replacement, as any rescanning preprocessor
  does.  The reference is quadratic too, about 5x slower, and overflows its
  native stack past 4000 levels; the expander's markers keep nesting on the
  heap, so depth costs memory only.
- Validation beyond the fixtures: 2600 generated macro programs (object-like,
  function-like, variadic, `#`, `##`, mutual recursion) agree with the
  reference on exit status and on every output byte.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| 1 | phase 4 as one stage: `PPTokenSource` split, macro table and directive parse, rescanning expander with argument prescan, `#`, `##` and hide sets | 76/76 pa4, 188/188 through pa4 |
