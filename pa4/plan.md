# PA4 `macro` Plan

## Stage Design

PA4 adds translation phase 4 between phase 3 and the PA2 phase 5-7 assembler:

```
PPTokenLexer (1-3) -> MacroExpander (4) -> PostTokenizer (5-7) -> DebugPostTokenStream
```

Ownership:

- `dev/src/pptoken_lexer.h` owns `PPTokenSource`, the pull interface both the
  lexer and the expander implement, so phases 5-7 no longer own a lexer and
  PA2 and PA4 drive the same analysis.  It also owns `CPPGM_PP_TOKEN_TYPES`,
  the one list the phase 3 and phase 4 token type enumerations are generated
  from, so the cast between them is checked rather than assumed.
- `dev/src/source_reader.h` owns `SourceForm`: whether bytes still need phases
  1 and 2.  Phase 4 lexes text those phases already produced.
- `dev/src/macro_model.*` owns the typed facts phase 4 carries: interned
  spellings, hash-consed hide sets, the well-known spellings, the analysed
  `#define` (parameters, a substitution program, and the replacement list kept
  for 16.3/2 equality) and the macro table.
- `dev/src/macro_expander.*` owns the directive/text-sequence split and the
  rescanning expansion engine.

Input is demand driven: one preprocessing-token of look-ahead is held, which
is what the rescan needs to see whether a `(` follows a function-like macro
name.  A `#` that starts a line ends the text-sequence and its line is read as
a whole, because the directive parse wants the line as a range.  Nothing else
is buffered, so a file written as one long line costs no more than a file
written as many.

### Hide-set model

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

This is 16.3.4 plus the one rule the fixtures add: 610's `#define G(x) 8 OBJ`
inside `#define OBJ 9 G` keeps `OBJ` hidden where it is produced, which is
what gcc and clang do and what the closing-paren intersection alone would not.

## Performance Model

Dominant operations, in the order they cost: interning a spelling (once per
source token and per `##` or `#` result), copying a fixed 16 byte `MacroToken`
(once per token per rescan level it passes through), and a hide-set `add` or
`intersect` (once per invocation, memoised on operand ids).  There is no
hashing of token text after interning and no re-parse of a replacement list.

- Throughput is **linear in the tokens produced**: 2.3 / 4.6 / 9.2 MB of the
  same source take 0.12 / 0.24 / 0.48 s, and 15k / 30k / 60k invocations take
  0.13 / 0.27 / 0.56 s.
- Memory is the source buffer plus the **vocabulary**, not the file: a
  spelling is stored once in a 64 KB block and indexed by an open addressed
  table of ids, about 48 bytes of index per distinct spelling.
- **Nested arguments are linear in memory** and quadratic in time: each level
  re-materialises its argument, as any rescanning preprocessor does, but the
  tokens an argument was written with are released as soon as the prescan that
  reads them is queued, unless the macro has a `#` or `##` that still wants
  them.  A macro that does keep them is quadratic in memory too, which is
  inherent: the written form of every open argument is still needed.

Measured against `dev/macro-ref`, output compared byte for byte in each case:

| input | mine | reference |
| --- | --- | --- |
| 2.3 MB of real C++ source, 361k tokens | 0.12 s, 7.7 MB | 0.31 s, 8.1 MB |
| 3.5 MB, 60k invocations of 6 macros | 0.56 s, 14.5 MB | 1.78 s, 8.1 MB |
| 14.9 MB, no macros, 600k distinct spellings | 0.58 s, 51 MB | 0.62 s, 8.1 MB |
| 4.9 MB as one line, 1M tokens | 0.27 s, 11.9 MB | 0.59 s, 8.1 MB |
| 2000 object-like macros chained, 200 times | 0.11 s, 21.7 MB | 5.37 s, 8.1 MB |
| `f(` x 2000 nested arguments | 0.07 s, 4.2 MB | 1.96 s, 1304 MB |
| `f(` x 8000 nested arguments | 1.10 s, 4.8 MB | segfaults at 15.4 GB |
| `s(` x 2000 nested, `s` stringizes | 0.46 s, 145 MB | 4.27 s, 1541 MB |

## Architecture Review

Read end to end against the assignment and 16.3, independently of the
implementation checkpoint.  The phase boundary is clean: phase 4 is a
`PPTokenSource` between two others, the expander never reaches into phase 3
state, and the only text-level work it does is the `##` re-lex and `#`
stringize that 16.3.2 and 16.3.3 define.  There is no second expansion path,
no interpreter fallback, no fixture-shaped special case, and no place where a
phase is skipped.  Three things did not hold up and were fixed; see Findings.

## Final Architecture Review

- One owner per fact.  `MacroSpellings` holds the well-known spellings that
  the directive parse and the rescan both look for; they used to be interned
  and stored twice.  `CPPGM_PP_TOKEN_TYPES` is the one list the phase 3 and
  phase 4 token types come from, with a static assertion per shared value.
  `push_replacement` is the only place that decides a placemarker's fate; the
  second, unreachable placemarker drop in `emit` is gone.
- One input path.  The line buffer and the rescan stack were two ways to hold
  pending input; the text-sequence is now a one-token look-ahead and only a
  directive line is buffered.
- One reader.  Re-lexing a `##` result went back through phases 1 and 2; the
  reader now takes the form of its input, so re-lexing is phase 3 alone.
- Facts stay typed.  A spelling is an id, a hide set is an id, an argument is a
  range, and a `#define` is analysed once into a substitution program.  Nothing
  is recovered from text after phase 3.

## Findings

1. **`##` re-lexed through phases 1 and 2** (correctness, fixed).  The joined
   spelling was handed to a fresh `PPTokenLexer` over physical source, so
   phase 2 spliced a trailing `\`: `#define cat(a,b) a##b` with `cat(x,\)`
   silently produced the identifier `x` and exited 0, where the join is not
   one preprocessing-token.  gcc, clang and `macro-ref` all reject it.  The
   reader now takes a `SourceForm`; a universal-character-name is still
   decoded, because after phase 1 that is how a character outside the basic
   source character set is spelled, so `cat(\,u0041)` remains `A` as in gcc
   and the reference.
2. **Nested arguments were quadratic in memory** (performance, fixed).  Every
   open invocation held the tokens its argument was written with for as long
   as the innermost replacement ran, so `f(f(f(...)))` cost O(depth^2): 2.0 GB
   at depth 8000.  The written form has no reader after the prescan unless the
   macro has a `#` or `##`, so it is released there: 4.8 MB at depth 8000, and
   1.10 s instead of 4.32 s.
3. **Interning was the dominant cost** (performance, fixed).  A
   `unordered_map<string, id>` copied the key on every lookup and allocated a
   node per spelling: 34% of the time and 90 MB on a 600k-spelling file.  A
   block-packed pool with an open addressed id index does it in 9% and 51 MB,
   and cuts the macro-heavy workload from 0.69 s to 0.56 s.
4. **A whole logical line was buffered** (performance, fixed).  Only the first
   token decides whether a line is a directive, so the text-sequence now
   streams: 24.4 MB to 11.9 MB on a 4.9 MB single-line file, and the O(longest
   line) term is gone.
5. **The reference diverges from the standard on one recursion shape**
   (recorded, not followed).  Where a **function-like** macro's replacement
   reaches an **object-like** macro that names the function-like macro again,
   `macro-ref` expands it a second time: `#define A(x) B`, `#define B A(1)`,
   `A(0)` gives `B` from the reference and `A ( 1 )` here.  gcc, clang and
   16.3.4 all agree with this implementation, no fixture covers the shape, and
   TESTING_AND_REFERENCES.md prefers the standard on non-test inputs, so the
   implementation is left alone.  The previous plan's claim that the model
   "is the only model that reproduces every reference trace observed" was too
   strong and is withdrawn.
6. **Stringizing a lone `\` is undefined and differs** (recorded, no change).
   `#define S(x) #x` with `S(\)` builds `"\"`, which is not a valid
   string-literal; 16.3.2 leaves the result undefined.  Phase 6 here reads one
   `"` character out of it and the reference reads none.  No fixture covers
   it and neither answer is more correct.

## Changes

- `source_reader.*`, `pptoken_lexer.*`: `SourceForm`, so phase 3 can be run
  over text phases 1 and 2 already produced.  `CPPGM_PP_TOKEN_TYPES`.
- `macro_model.*`: block-packed `SpellingPool` with an open addressed index;
  `MacroSpellings`; `MacroDefinition::keeps_raw_arguments`;
  `MacroTokenType` generated from the shared list with a static assertion per
  value.
- `macro_expander.*`: demand-driven input with one token of look-ahead;
  `drop_raw_arguments`; `Argument::raw_empty` so the `, ## __VA_ARGS__`
  extension survives the release; the unreachable placemarker drop removed.
- `cppgm.tests/course/pa4/310-token-paste-trailing-backslash.t` and
  `310-token-paste-universal-character-name.t`, with references from
  `make -C pa4 ref-test TEST=...`.

## Validation

- `make test-report-through-pa4`: **190 / 190**, pa1 60, pa2 28, pa3 24,
  pa4 78 (76 plus the two added above).
- `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src`: pass.
- 1110 generated macro programs (object-like, function-like, variadic, `#`,
  `##`, mutual recursion) agree with **gcc** on every produced token; the two
  reported differences were `gcc -E` avoid-paste spacing, confirmed identical
  when re-checked in isolation.  382 more agree with **clang** on the same
  terms.
- 1200 more agree with `macro-ref` on exit status and every output byte apart
  from finding 5's recursion shape, which gcc and clang decide as here.
- Every benchmark above compared byte for byte with `macro-ref`, except the
  two nesting depths where the reference segfaults.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| 1 | phase 4 as one stage: `PPTokenSource` split, macro table and directive parse, rescanning expander with argument prescan, `#`, `##` and hide sets | 76/76 pa4, 188/188 through pa4 |
| 2 | PA-wide audit: `##` re-lex through phases 1-2, quadratic nested-argument memory, interning cost, line buffering, single-owner spellings and token types | 78/78 pa4, 190/190 through pa4 |

## Active Checkpoint

None: the stage is complete and audited.  Next assignment is PA5 `preproc`,
which reuses `MacroExpander` with `#include`, `#if` (through the PA3
`CtrlExprEvaluator`), predefined macros and the pragma operator.
