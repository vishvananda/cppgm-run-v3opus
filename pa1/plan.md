# PA1 (pptoken) Plan

## Stage Design

PA1 owns translation phases 1-3. The implementation lives in `dev/src` so that
every later assignment drives the same lexer; `dev/pptoken.cpp` is only a
driver that reads stdin and prints `DebugPPTokenStream` output.

| Module | Owns |
| --- | --- |
| `dev/src/source_charset.{h,cpp}` | UTF-8 transcoding, Annex E.1/E.2 identifier classes, digit/hex/whitespace classes. Range membership is a binary search over sorted static tables. |
| `dev/src/source_reader.{h,cpp}` | Phases 1-2 as a character source: trigraphs, line splicing, universal-character-names, the phase-2 terminating new-line, BOM stripping, raw mode, `mark`/`rewind`, positions. |
| `dev/src/pptoken_lexer.{h,cpp}` | Phase 3: `PPToken`/`PPTokenType`, the pull lexer, and `emit_pp_token` onto `IPPTokenStream`. |

Data flow: bytes -> `SourceReader` (code points, translated) -> `PPTokenLexer`
(typed `PPToken`) -> `IPPTokenStream`. The lexer keeps exactly two pieces of
cross-token state: the reader's consumption point and `DirectiveState`, which
is what makes a header-name recognizable after a line-leading `#` `include`.

Key invariants:

- The consumption point is a **byte offset into the original file**, so raw-mode
  switching and rewinding are exact and O(1); lookahead is simply recomputed.
- A `\` is tested for a line splice first, then for a universal-character-name.
  A failed name contributes its spelled characters as final code points, which
  keeps `"\\u0041"` an escaped backslash and stops the trailing `\` of
  `\u03\`+newline from splicing.
- An invalid universal-character-name **value** is reported on consumption, not
  on lookahead, because lookahead can reach past the start of an untranslated
  raw-string body.

## Current Failure Map

`make test-report ACTIVE_TEST_REPORT_PAS='pa1'` -> **58 / 58**
(53 checked-in gating tests + 5 regression tests added this turn). No open
failures.

Known, deliberate divergences from `pptoken-ref`, all on ill-formed input where
`TESTING_AND_REFERENCES.md` says to prefer the handout and the standard:

1. When a failed universal-character-name is flushed, the reference re-emits the
   *untranslated* next character, undoing phase-1 trigraph replacement
   (`??/??/` -> ref `\ ? ? /`, ours `\ \`). Phase 1 is not reversible.
2. `?\uD800` is accepted by the reference as a literal escape although a bare
   `\uD800` is rejected by it; we reject both.
3. Our UTF-8 decoder rejects overlong forms and surrogate encodings per
   RFC 3629, which the handout cites; the reference accepts them.

Everything else agrees: 45 curated edge cases, 137 repository files, and 1500
random inputs drawn from a grammar-shaped alphabet without `?` all match the
reference byte for byte, including exit status.

## Active Checkpoint

None open. Next work is PA2 (`posttoken`), which consumes `PPToken` directly.

## Performance Model

Cost is O(bytes). Every character is fetched once into a fixed 18-slot ring
buffer and re-fetched only after a `rewind`, which happens once per raw-string
literal. Per-token work is O(token length): the punctuator table is a hash set
keyed by at most 4 ASCII characters, Annex E lookup is a binary search over 45
ranges, and a raw-string terminator is a suffix compare of at most 18 bytes done
only at a `"`.

Measured (`-O3`, this machine, output to `/dev/null`):

| Input | Size | Time | Peak RSS |
| --- | --- | --- | --- |
| repeated real-world + compiler sources | 3.16 MB | 0.36 s | 7.9 MB |
| same, x4 | 12.66 MB | 1.46 s | 20.1 MB |
| 400k `<::` punctuators | 1.20 MB | 0.33 s | 5.8 MB |
| 400k-quote raw-string body | 0.80 MB | 0.04 s | 5.6 MB |
| 400k line splices in one token | 0.80 MB | 0.01 s | 4.5 MB |
| 400k universal-character-names | 2.40 MB | 0.07 s | 7.9 MB |

Scaling is linear (0.36 -> 1.46 s for 1x -> 4x) and no pathological input is
super-linear. Two regressions were found and fixed by measurement:

- `endl` per token flushed once per line; buffered output plus
  `sync_with_stdio(false)` cut 3.16 MB from 0.46 s to 0.39 s.
- An out-parameter run array on the fetch path blocked inlining and cost 1.8x
  (0.36 -> 0.68 s); the hot path now pushes single characters and only the
  backslash path builds a run.

Reading stdin straight into the buffer the reader takes ownership of, rather
than through `ostringstream::str()`, removed a full copy of the translation unit
(28 MB -> 20 MB peak on the 12.66 MB input).

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | Phases 1-3: source character set, translation reader, pp-token lexer | `dev/src/source_charset`, `dev/src/source_reader`, `dev/src/pptoken_lexer` | 0/53 -> 58/58. Reference-differential testing corrected five semantics: hexadecimal pp-numbers take their exponent sign after `p` not `e`; a header-name is only the single token after `#` `include` and an unclosed one is an error; a failed universal-character-name is final and does not splice; an empty `''` is left for phase 7; a line comment spliced past end of file is ill-formed. Regression tests for all five added under `cppgm.tests/course/pa1`. |
