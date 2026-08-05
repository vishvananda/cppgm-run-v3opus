# PA1 (pptoken) Plan

## Stage Design

PA1 owns translation phases 1-3. The implementation lives in `dev/src` so that
every later assignment drives the same lexer; `dev/pptoken.cpp` is only a
driver that reads stdin and prints `DebugPPTokenStream` output.

| Module | Owns |
| --- | --- |
| `dev/src/source_charset.{h,cpp}` | UTF-8 transcoding and the code point classes of [lex.charset], [lex.name] and [lex.ppnumber]. The per-character questions answer inline in the header; only the Annex E.1/E.2 range search, a binary search over sorted tables, is out of line. |
| `dev/src/source_reader.{h,cpp}` | Phases 1-2 as a character source: trigraphs, line splicing, universal-character-names, the phase-2 terminating new-line, BOM skipping, raw mode, `mark`/`rewind`, positions. `peek`/`advance`/`mark` are inline buffer hits; refilling is the only call. |
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
- A token's spelling has one owner, `PPToken::spelling`. Every scanner appends
  to it in place, so an encoding-prefix is already there when the identifier it
  was scanned as turns out to introduce a literal, and the buffer is reused
  across tokens.

## Current Failure Map

`make test-report-through-pa1` -> **60 / 60**. No open failures.

Known, deliberate divergences from `pptoken-ref`, all on input the handout and
`TESTING_AND_REFERENCES.md` say to resolve from the handout and the standard:

1. The reference does not re-enter its translation pipeline after a `?`. It
   undoes phase-1 trigraph replacement when it flushes a failed universal-
   character-name (`\??<` -> ref `\ ? ? <`, ours `\ {`), and it does not
   recognize a name at all when a `?` immediately precedes it: the reference
   emits the `?`, then the backslash as a non-whitespace-character, then the
   rest of the name as an identifier, where we emit the `?` and the code point
   the name designates. Phase 1 runs before phase 2 and is not reversible.
2. `?\` at end of file: phase 2 appends the terminating new-line and then
   splices it away, so the file is just `?`. The reference emits `? \ new-line`,
   although it splices the same appended new-line for `a\` and `//x\`.
3. Our UTF-8 decoder rejects overlong forms, surrogate encodings and lone
   continuation bytes per RFC 3629, which the handout cites; the reference
   accepts them.

Everything else agrees with the reference (see Validation).

## Performance Model

Cost is `O(bytes)` to scan plus `O(tokens)` to emit, with no input super-linear
in either. Measured on this machine at `-O3` with output to `/dev/null`:
**28 MB/s** on real source and **9-13 M tokens/s**; peak RSS is the source file
plus the longest single token plus about 4 MB of process.

Dominant operations, and why each is constant per unit:

- Fetching one code point: a mask-indexed slot in a 32-entry ring buffer.
  Plain single-unit characters that no phase-1 or phase-2 rule looks at are
  stored without decoding; only `?`, `\` and non-ASCII take the general path.
- Consuming one code point: an inline buffer hit. Each character is fetched
  once, and re-fetched only after a `rewind`, which happens twice per
  raw-string literal (in and out of raw mode).
- A punctuator: up to four ASCII characters packed into an integer and matched
  by a switch; maximal munch is one shift per attempt.
- An identifier: a length and first-character test, and only then a set lookup
  for the identifier-like operators.
- Annex E lookup: a binary search over 45 ranges, and never reached for ASCII.
- A raw-string terminator: a suffix compare of at most 18 bytes, done only at
  a `"` in the body.
- Output: lines are packed into a 64 KB buffer and written a block at a time,
  so no token costs a formatted stream insertion.

| Input | Size | Time | MB/s | Peak RSS |
| --- | --- | --- | --- | --- |
| real-world + compiler sources | 1.03 MB | 0.037 s | 26.6 | 4.5 MB |
| same, x4 | 4.11 MB | 0.139 s | 28.2 | 7.9 MB |
| same, x16 | 16.45 MB | 0.550 s | 28.5 | 20.2 MB |
| 1.6M `<::` punctuators | 4.80 MB | 0.261 s | 17.5 | 12.0 MB |
| 1.6M-quote raw-string body | 1.60 MB | 0.041 s | 37.6 | 10.2 MB |
| 1.6M line splices in one token | 3.20 MB | 0.045 s | 68.5 | 7.8 MB |
| 1.6M universal-character-names | 9.60 MB | 0.196 s | 46.7 | 22.9 MB |
| 1.6M trigraphs | 4.80 MB | 0.100 s | 45.8 | 12.0 MB |
| 4M non-whitespace-characters | 4.00 MB | 0.315 s | 12.1 | 7.8 MB |
| 1.3M failed `\uZ` names | 3.90 MB | 0.233 s | 16.0 | 7.8 MB |
| one 4 MB string literal | 4.00 MB | 0.082 s | 46.3 | 19.0 MB |
| raw string, 250k near-terminators | 4.00 MB | 0.073 s | 52.4 | 19.0 MB |
| 220k `#include <a/b/c.h>` lines | 4.18 MB | 0.111 s | 35.8 | 7.9 MB |

Throughput is flat from 1 MB to 16 MB (26.6 -> 28.5 MB/s) and the adversarial
inputs sit on the same two lines: byte-bound inputs at 37-68 MB/s and
token-bound inputs at 9-13 M tokens/s. `nwc` is the token-rate floor: 4M
one-character tokens at 12.7 M tokens/s.

## Architecture Review

Reconstructed from the source, independently of the checkpoint notes.

- **Ownership.** Each fact has one owner. Code point classes are only in
  `source_charset`; phase 1-2 rewriting is only in `SourceReader`; the token
  grammar is only in `PPTokenLexer`; the output format is only in
  `DebugPPTokenStream`. No fact is recomputed downstream: the lexer never looks
  at bytes and the reader never looks at tokens.
- **No parallel fallback.** There is one path from bytes to code points and one
  from code points to tokens. Raw mode is a mode of the single reader, not a
  second reader, which is why entering it is a `rewind` to the same byte offset.
- **No text recovery.** `PPToken` carries its spelling and its byte offset, so
  later phases never re-lex or re-parse a spelling to recover what phase 3
  already knew.
- **Look-ahead is bounded.** Every scanner peeks at most 4 ahead of an 8-deep
  window, and one translation step pushes at most 10 slots, which the ring
  buffer's `static_assert` ties to its capacity.

## Final Architecture Review

Findings, changes, evidence and validation for this audit are below. Nothing is
left open: the remaining reference divergences are the three listed in the
failure map, each with a stated standard or handout basis.

### Findings

1. **pp-number hexadecimal entry was an approximation.** `is_pp_number_exponent`
   decided a pp-number was hexadecimal from a literal `0x`/`0X` prefix. The rule
   the whole reference toolchain uses is different: an `x` or `X` introduces
   hexadecimal digits while the pp-number is still its *leading digit-sequence*,
   which a `.` ends and so does an `e` that is itself an exponent, one followed
   by a sign or a digit. So `1xe+2`, `12x3e+4` and `0Exe+2` are hexadecimal and
   stop before the sign, while `1e2x3e+4` is not and is one token. The old rule
   disagreed with the reference on 1435 of 189614 exhaustive pp-numbers, and
   `posttoken-ref` shows the same split (`0x1e+2` -> `literal 0x1e`, `+`, `2`),
   so this is a fact PA2 fixtures depend on, not a lexer detail.
2. **A byte-order-mark-only file lost its terminating new-line.** The BOM was
   erased from the buffer, after which the file tested as empty and phase 2 added
   no new-line. The BOM is an encoding signature, but the *file* is not empty.
   Erasing also moved the whole translation unit once.
3. **The per-character primitives were out-of-line cross-translation-unit
   calls.** `decode_utf8`, `append_utf8`, the character classes, and
   `SourceReader::peek`/`advance`/`mark` were each a call per character. That,
   plus a `%` by a non-power-of-two ring capacity, a `std::string` construction
   and hash per punctuator munch attempt, a `std::string` temporary per emitted
   token type, and a formatted `std::cout` insertion per token, held real source
   to 7.9 MB/s.
4. **The token spelling had two owners.** `scan_identifier_or_literal` built a
   local string and then either moved it into the token or passed it back down
   as a `prefix` for a literal scanner to assign over the token's own spelling.
   That is a copy per literal and a fresh allocation per identifier.

### Changes

- `pptoken_lexer.cpp`: `scan_pp_number` tracks `leading_digits` and
  `hexadecimal` as it scans, replacing the prefix test (finding 1).
- `source_reader.{h,cpp}`: the BOM is skipped with an `origin_` offset instead
  of erased, so the file stays non-empty and is never moved (finding 2).
- `source_charset.h`: the single-code-unit decode, the encode, and the code
  point classes are inline; only the Annex E range search stays out of line
  (finding 3).
- `source_reader.h`: `peek`, `advance`, `mark` and `raw_mode` are inline buffer
  hits; the ring capacity is a power of two indexed with a mask, `static_assert`
  ed against the lookahead and run-length bounds; `Fetched` is packed to 24
  bytes; `fill` stores untranslatable single-unit characters directly
  (finding 3).
- `pptoken_lexer.cpp`: punctuator, encoding-prefix and raw-prefix spellings are
  packed into an integer and matched by switch; the identifier-like operators
  are rejected on length and first character before any hashing (finding 3).
- `DebugPPTokenStream.h`: lines are built in a 64 KB buffer written a block at a
  time, with the token type taken as a literal so its length is known and the
  byte count formatted without a stream (finding 3).
- `pptoken_lexer.{h,cpp}`: every scanner appends to `PPToken::spelling`; the
  `prefix` parameters are gone (finding 4).
- New regression tests `250-pp-number-hexadecimal-entry` and
  `100-utf8-bom-only` under `cppgm.tests/course/pa1`, with `.ref` files
  generated by `make -C pa1 ref-test TEST=...`.

### Performance Evidence

Before -> after this audit, same machine, same inputs, best of three:

| Input | Before | After | Ratio |
| --- | --- | --- | --- |
| real-world sources, 1.03 MB | 7.9 MB/s | 26.6 MB/s | 3.4x |
| real-world sources, 4.11 MB | 8.0 MB/s | 28.2 MB/s | 3.5x |
| `<::` punctuators | 4.0 MB/s | 16.7 MB/s | 4.2x |
| mixed operators | 5.4 MB/s | 21.2 MB/s | 3.9x |
| identifiers | 11.8 MB/s | 37.7 MB/s | 3.2x |
| string literals | 17.0 MB/s | 46.0 MB/s | 2.7x |
| trigraphs | 13.6 MB/s | 42.1 MB/s | 3.1x |
| universal-character-names | 33.2 MB/s | 45.5 MB/s | 1.4x |
| raw-string body | 16.4 MB/s | 29.7 MB/s | 1.8x |
| line splices | 46.6 MB/s | 59.1 MB/s | 1.3x |

Each step was taken because `perf record` on 4 MB of real source pointed at it,
and re-profiled after. The profile started as `fill` 26%, `scan_op_or_punc`
12%, `peek` 11%, `std::cout` formatting 22%; it now has no single frame over
21% and the stream formatting is gone. The scaling table above is the check
that nothing was traded for a constant factor.

### Validation

Differential against `pptoken-ref`, byte for byte on stdout and on exit status:

- 189,614 pp-numbers, exhaustive to length 4 over `0189.eExXpP+-_az` plus random
  length 5-9: identical. 243,382 random pp-numbers over a 32-character alphabet
  including `bBoO` and the hex letters: identical. 4,920 targeted
  `<prefix>e+1`/`<prefix>p+1` probes: identical.
- 492,560 punctuator strings, exhaustive to length 4 over 23 punctuation
  characters including `\`, plus 200k random length 5-8: identical.
- Every Unicode code point from 0x80 to 0x10FFFF outside the surrogates, alone,
  after `a`, and after `1`: identical. This checks the decoder, the Annex E.1
  table and the Annex E.2 initial restriction.
- Every `\uXXXX` value and the boundary `\UXXXXXXXX` ranges, alone and after
  `a`: identical, including which values are rejected.
- Random-input fuzzing, minimized and classified: 3,000 raw-string-shaped,
  2,500 `#include`-shaped, 2,500 literal-shaped and 4,000 general inputs are
  all identical. Of 2,500 byte-level UTF-8 inputs, 307 differ and every one is
  invalid UTF-8; of 1,200 general inputs drawn from an alphabet that includes
  `?`, 18 differ and every one contains a `?`. Those are exactly the divergences
  listed in the failure map, and no other class remains.
- `make test-report-through-pa1`: 60/60.
- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`: passed,
  16 files.

## Active Checkpoint

None open. Next work is PA2 (`posttoken`), which consumes `PPToken` directly.

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | Phases 1-3: source character set, translation reader, pp-token lexer | `dev/src/source_charset`, `dev/src/source_reader`, `dev/src/pptoken_lexer` | 0/53 -> 58/58. Reference-differential testing corrected five semantics: hexadecimal pp-numbers take their exponent sign after `p` not `e`; a header-name is only the single token after `#` `include` and an unclosed one is an error; a failed universal-character-name is final and does not splice; an empty `''` is left for phase 7; a line comment spliced past end of file is ill-formed. Regression tests for all five added under `cppgm.tests/course/pa1`. |
| 2 | PA-wide audit: architecture, correctness and performance | all of `dev/src` for PA1 | 58/58 -> 60/60. Two correctness findings fixed (pp-number hexadecimal entry, byte-order-mark-only file) and two structural ones (per-character out-of-line calls, duplicate spelling ownership), for 3.5x on real source. Full findings, changes, evidence and validation in Final Architecture Review. |
