# PA2 (posttoken) Plan

## Stage Design

PA2 owns translation phases 4-6 and the tokenization part of phase 7. Phase 4
is a no-op for this assignment, so the stage is: drive the PA1 lexer, join
maximal string-literal sequences (phase 6), and analyse every other
preprocessing-token into a typed `token` (phase 7). The implementation lives in
`dev/src` so that PA4 and PA5 (`macro`, `preproc`), which emit the same token
format, drive the same code; `dev/posttoken.cpp` is only a driver.

| Module | Owns |
| --- | --- |
| `dev/src/token_model.{h,cpp}` | The token vocabulary: `EFundamentalType` with its course ABI size, `ETokenType`, their names, the identifier-like operators of 2.13, and the spelling -> `ETokenType` lookup for keywords, operators and alternative tokens. Each enumeration and its name table come from one list, so a type and its spelling cannot drift apart. |
| `dev/src/post_token.h` | `PostToken`: one analysed token as a typed fact - kind, source, token type, fundamental type, object representation, ud-suffix. Its buffers are reused across tokens. |
| `dev/src/literal_scan.{h,cpp}` | 2.14.2, 2.14.3, 2.14.4 and 2.14.8 for one spelling: the c-char/s-char element decoder, pp-number classification and typing, character-literal typing. |
| `dev/src/string_literal.{h,cpp}` | Phase 6: accumulates a maximal string-literal sequence, resolves its encoding-prefix and ud-suffix as its parts arrive, and encodes the bodies into code units. |
| `dev/src/posttokenizer.{h,cpp}` | The pull interface `next(PostToken&)`: the sequence state machine over `PPToken`s, and the conversion of every other preprocessing-token. |
| `dev/src/DebugPostTokenStream.h` | The output format, on any `std::ostream`, so PA5 can write to its outfile. |

Data flow: bytes -> `PPTokenLexer` (PA1) -> `PostTokenizer` -> `PostToken` ->
`DebugPostTokenStream`. The tokenizer holds exactly three pieces of cross-token
state: the held-back `PPToken` that ended a string sequence, the open
`StringLiteralSequence`, and whether the last token emitted was `KW_OPERATOR`.

Key facts this stage establishes, each with one owner:

- A **ud-suffix is course-restricted to start with `_`**. `1_ud`, `'a'_ud` and
  `"a"_ud` are user-defined-literals; `1sv`, `'a'da` and `"a"da` are `invalid`.
  This is what makes `0_ud1l` a suffix while `0l_ud1` is invalid.
- A **numeric escape in a string is a code unit, not a code point**, so its
  range check depends on the encoding of the *whole* concatenated sequence:
  `"\x3C0" u""` is valid and `"\x3C0"` alone is not. The sequence therefore
  resolves its encoding before any body is decoded.
- A **numeric escape in a character literal is a code point** and must be a
  Unicode scalar value: `'\x00aa'` is `int`, `'\xD800'` is invalid.
- `operator ""` plus a reserved suffix is the one place a preprocessing-token
  becomes two tokens: after `KW_OPERATOR`, a lone `""`*suffix* whose suffix is
  reserved splits into `literal ""` and `identifier <suffix>`, which is what a
  phase 7 `literal-operator-id` needs.
- An **ill-formed phase 1-3 file still reports the tokens already produced**,
  then fails without an `eof`, so the output stream flushes from its destructor.

## Current Failure Map

`make test-report ACTIVE_TEST_REPORT_PAS='pa2'` -> **28 / 28**.
`make test-report-through-pa2` -> **88 / 88**. No open failures.

Deliberate divergences from `posttoken-ref`, all on input no fixture covers,
where the handout and `TESTING_AND_REFERENCES.md` say to resolve from the
handout and the standard. Counts are from the differential suites in Validation.

| # | Input | Reference | Ours | Basis |
| --- | --- | --- | --- | --- |
| 1 | `0_.`, `9_fl8.e.e` (99) | user-defined-literal with suffix `_.` | `invalid` | 2.14.8: a ud-suffix is an identifier. The reference applies that rule to a floating prefix - fixture `300-invalid-floating-literal-shapes` pins `1.0_foo.bar` as invalid, and its stderr says `invalid character in ud-suffix` - but not to an integer one. |
| 2 | `0b101`, `0B11110000_buf` (40) | binary integer-literal | `invalid` | Binary literals are C++14. `pa34/tests/preproc/400-host-binary-integer-ud-literal` shows they belong to the hosted GNU-extension assignment. |
| 3 | `0x1p3`, `0XbEp0` (212) | `double` | `invalid` | Hexadecimal floating literals are C99. `pa34/tests/preproc/400-host-gnu-hex-float-pp-number` shows the same. |
| 4 | `.0u`, `.1ll` (25) | `unsigned int 0` | `invalid` | A fractional-constant with an integer-suffix matches neither 2.14.2 nor 2.14.4. |
| 5 | `$ #include "foo"` (1) | `invalid "foo"`, a header-name | `literal "foo"` | PA1 README: a header-name is only tokenized "after a sequence of (start of file or `new-line`) (`#` or `%:`) `include`". The reference recognizes one after a `#` `include` anywhere in a line. 16.2 requires the `#` to begin the line, and PA5 needs that same rule. |
| 6 | `"\??/"` (240) | `array of 4 char 3F3F2F00` | `array of 2 char 5C00` | Phase 1 replaces `??/` with `\`, so the body is an escaped backslash. This is the PA1 failure map's class 1 - the reference does not re-enter its translation pipeline after a `?` - now visible in a literal's object representation. |

## Performance Model

Cost is `O(bytes)` for phases 1-3 plus `O(spelling)` per token to analyse. No
input is super-linear in either time or memory, and no token costs a heap
allocation once the buffers have grown. Measured at `-O3` with output to
`/dev/null`, best of seven: **23.9 MB/s** on 1.36 MB of real source, rising to
**28.3 MB/s** once the file is large enough to amortize start-up. `pptoken`,
which stops after phase 3, runs the same file at 25.9 MB/s while printing 67%
more lines, so phases 4 to 7 are a small addition to the lexing they sit on.

Dominant operations, and why each is constant per unit:

- Phase 1-3 per character: `SourceReader::fill` and the lexer's identifier and
  operator scanners, which are 21%, 14% and 9% of a real-source profile.
  Everything below is smaller.
- Keyword and operator lookup: one FNV-1a pass over the spelling and one probe
  into a 512-slot open-addressed table built once. An identifier that is not a
  keyword is rejected on that first probe, and anything longer than
  `reinterpret_cast` never reaches the table.
- A string sequence: each spelling is appended to one reused buffer that is
  also the token's source, with a `(body, raw)` record per part. The
  sequence-level facts - its encoding-prefix, its ud-suffix and whether the
  parts disagree - are folded in as each part arrives, so building the token is
  the single pass that encodes the bodies. Nothing is copied twice and the
  buffer is swapped into the token rather than copied, so a 200k-part sequence
  costs the same per part as 200k two-part sequences.
- Encoding an ordinary or `u8` body: `memchr` to the next `\`, then a block
  append, because the execution character set is UTF-8 and the body already is.
  A raw body is one append. Only `char16_t`, `char32_t` and `wchar_t` bodies
  decode UTF-8 per character.
- An integer literal: one pass over the digits, whose overflow test is two
  comparisons against limits computed once for the base, then a first-fit walk
  over at most six candidate types.
- A floating literal: `strtof`/`strtod`/`strtold` on the spelling itself. That
  is the conversion the starter code's `istringstream >> x` calls internally,
  and it is 171 ns per 16-character literal against 503 ns through the stream,
  so the direct call is 2.9x on the conversion and about 1.5x on floating-dense
  input end to end, as well as being the form that still matches the reference
  out of range (see Validation). The suffix is left on the spelling because none
  of `f`, `F`, `l` and `L` belongs to a C floating constant, so the conversion
  stops on it and no copy is needed.
- Output: lines are packed into a 64 KB buffer and written a block at a time,
  and the buffer is sized for a whole hexdump once, since a literal's object
  representation can be as large as the literal.

Memory is the source file plus, for a string literal, its encoded data and its
hexdump, each proportional to the literal, plus 24 bytes per part of an open
sequence. The shortest a part can be is `""` and the space that separates it
from the next, three bytes, so those records are at most eight times the source
they describe and memory stays linear in the file whatever its shape.

| Input | Size | Time | MB/s | Rate | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| compiler sources + C++ standard library headers | 1.36 MB | 0.057 s | 23.9 | 3.8 M tokens/s | 5.7 MB |
| same, x4 | 5.45 MB | 0.198 s | 27.4 | | 11.9 MB |
| same, x16 | 21.78 MB | 0.769 s | 28.3 | | 35.7 MB |
| 1.5M one-character tokens | 2.86 MB | 0.225 s | 12.7 | 6.7 M tokens/s | 7.7 MB |
| 400k operators and digraphs | 1.31 MB | 0.088 s | 15.0 | 4.5 M tokens/s | 5.7 MB |
| 300k identifiers | 3.61 MB | 0.100 s | 36.1 | 3.0 M tokens/s | 7.6 MB |
| 300k keywords | 2.61 MB | 0.086 s | 30.5 | 3.5 M tokens/s | 7.7 MB |
| 300k 19-digit integer literals | 5.69 MB | 0.210 s | 27.1 | 1.4 M literals/s | 11.9 MB |
| 300k 16-digit hexadecimal literals | 5.42 MB | 0.218 s | 24.9 | 1.4 M literals/s | 11.7 MB |
| 200k 16-character floating literals | 3.22 MB | 0.142 s | 22.6 | 1.4 M literals/s | 7.7 MB |
| 300k character literals | 2.00 MB | 0.091 s | 22.0 | 3.3 M literals/s | 7.4 MB |
| 300k user-defined-literals | 2.76 MB | 0.106 s | 26.1 | 2.8 M literals/s | 7.7 MB |
| one 200k-part concatenation | 1.53 MB | 0.067 s | 22.7 | 3.0 M parts/s | 17.3 MB |
| 150k two-part concatenations | 1.43 MB | 0.077 s | 18.5 | 3.9 M parts/s | 20.0 MB |
| one 4 MB ordinary string body | 3.81 MB | 0.119 s | 32.2 | | 33.9 MB |
| one 4 MB `u""` string body | 3.81 MB | 0.204 s | 18.7 | | 64.5 MB |
| one 4 MB raw string body | 3.81 MB | 0.105 s | 36.3 | | 33.9 MB |
| 1M narrow `\x41` escapes | 3.81 MB | 0.144 s | 26.6 | | 25.3 MB |
| 1M wide `\x41` escapes | 3.81 MB | 0.162 s | 23.5 | | 30.2 MB |
| comment-dense | 2.80 MB | 0.050 s | 56.1 | | 7.7 MB |

Throughput is flat from 5 MB to 22 MB (27.4 -> 28.3 MB/s), and the adversarial
shapes span 12.7 to 56.1 MB/s, so none is more than 1.9x off real source in
either direction. Doubling any of nine scaling shapes - concatenation parts,
ordinary and raw string bodies, line splices, quote-dense bodies, raw
delimiters, one huge identifier, one huge pp-number, one huge ud-suffix -
doubles the time, from 1x to 8x in every case.

## Architecture Review

Reconstructed from the source, independently of the checkpoint notes.

- **Ownership.** Each fact has one owner. The token vocabulary is only in
  `token_model`; how one spelling becomes a value is only in `literal_scan`;
  phase 6 is only in `StringLiteralSequence`; which preprocessing-token becomes
  which token kind is only in `PostTokenizer::convert`; the output format is
  only in `DebugPostTokenStream`. The identifier-like operators of 2.13 are the
  one fact phase 3 and phase 7 share, and after this audit they are stated once,
  in `token_model.h`, and used by both.
- **No parallel fallback.** There is one path from a preprocessing-token to a
  token. A string-literal sequence is not a second path but the same one with
  its terminator held back, which is why the held `PPToken` is the tokenizer's
  only look-ahead. `convert` has no per-token special case: `#`, `##`, `%:` and
  `%:%:` are `invalid` because they are the only operators the phase 7 table
  does not name, not because they are listed a second time.
- **No text recovery.** `PostToken` carries the typed fact, never text to be
  re-parsed: a user-defined integer or floating literal keeps its prefix as a
  length into `source`, and a string sequence keeps its ud-suffix as a copy made
  while the sequence was still open. The output stream formats and never scans.
- **Demand.** Nothing is computed that the token does not need. A body is
  decoded once, after the encoding that decides its code-unit width is known; a
  pp-number's value is computed only once its shape and suffix have been
  accepted; a spelling that is not a candidate keyword never reaches the table.

## Final Architecture Review

Findings, changes, evidence and validation for this audit are below. Nothing is
left open: the remaining reference divergences are the six in the failure map,
each with a stated standard or handout basis.

### Findings

1. **The identifier-like operators of 2.13 were stated twice.** `and`, `or`,
   `not_eq`, `new`, `delete` and their eight companions appeared once in
   `pptoken_lexer.cpp`, so that phase 3 calls them `preprocessing-op-or-punc`,
   and once in `token_model.cpp`, so that phase 7 gives them a token type. The
   two lists are the same fact and could drift: dropping a spelling from the
   lexer's list alone would silently change `pptoken` output while leaving
   `posttoken` output correct, because `convert` looks identifiers up in the
   same table.
2. **An open string sequence carried the sequence's facts once per part.**
   Every `Part` held its own encoding-prefix and the span of its own ud-suffix,
   and `build` then re-scanned all the parts to reduce them to one encoding and
   one suffix. Those are facts about the sequence, not about a part; holding
   them per part made the record 40 bytes where 24 suffice, and added a pass
   over the sequence that a concatenation-dense file pays once per part.
3. **The integer overflow test divided once per digit.** `value > (ULLONG_MAX -
   digit) / base` is a 64-bit division inside the digit loop, when the two
   limits it needs depend only on the base.
4. **Every floating literal copied its own prefix.** `spelling.substr(0,
   shape.suffix_begin)` allocated to strip a suffix that `strtod` stops on
   anyway, since none of `f`, `F`, `l` and `L` belongs to a C floating constant.
   It was the stage's only per-token heap allocation: 200k floating literals
   cost 180,104 allocations where every other shape costs a few dozen.
5. **The source-character decoder was written twice.** `decode_literal_element`
   in `literal_scan` and `decode_raw_element` in `string_literal` were the same
   eight lines: how a character in a literal body maps to a code point.
6. **Two reference divergences were unrecorded.** A header-name after a `#`
   `include` that does not begin the line, and a trigraph inside a literal, both
   reach PA2 output and neither was in the failure map. Both are decided by the
   PA1 handout: it requires "(start of file or `new-line`) (`#` or `%:`)
   `include`" before a header-name, and phase 1 precedes phase 3.
7. **The output stream's comment overstated what it does.** It claimed a
   byte-pair hexdump table; the code has a nibble table. Building the byte-pair
   table measured within noise of the nibble one on every hexdump-heavy shape,
   because the loop is not the bottleneck, so the comment was corrected instead.

### Changes

- `token_model.h` gains `CPPGM_IDENTIFIER_LIKE_OPERATORS`, the thirteen 2.13
  operators spelled as identifiers with the token type each takes.
  `CPPGM_SIMPLE_TOKEN_SPELLINGS` is built on top of it and
  `pptoken_lexer.cpp` builds its phase 3 set from it, so the list is stated
  once. The header is used for the macro alone, so `pptoken` gains no object.
- `StringLiteralSequence` folds the encoding-prefix, the ud-suffix and the
  conflict flag in as each part arrives, in `note_encoding` and `note_suffix`.
  `Part` is now `(body_begin, body_end, raw)`, `resolve` and `same_suffix` are
  gone, and `build` is the one pass that encodes.
- `compute_integer_value` hoists `ULLONG_MAX / base` and `ULLONG_MAX % base` out
  of the digit loop, leaving two comparisons per digit.
- `PA2Decode_float`, `PA2Decode_double` and `PA2Decode_long_double` take a
  `const char*` and are handed the spelling itself, so a floating literal costs
  no allocation.
- `decode_source_character` moves to `literal_scan.h` as an inline function and
  both callers use it; `decode_raw_element` is gone.
- `DebugPostTokenStream::append_hex` writes through a pointer into the sized
  buffer rather than indexing it, and its comment now describes what it does.
- The failure map gains classes 5 and 6.

### Performance Evidence

Interleaved A/B against the pre-audit binary, best of seven, on an idle machine
so both see the same conditions.

| Input | Before | After | |
| --- | --- | --- | --- |
| 300k 19-digit integer literals | 224.2 ms | 210.6 ms | 1.06x |
| 300k hexadecimal literals | 254.3 ms | 218.9 ms | 1.16x |
| 200k floating literals | 148.9 ms | 142.3 ms | 1.05x |
| one 200k-part concatenation | 74.6 ms | 67.4 ms | 1.11x |
| 150k two-part concatenations | 92.1 ms | 77.9 ms | 1.18x |
| 800k-part `"ab"` concatenation | 219.2 ms | 187.6 ms | 1.17x |
| 2.4M-part `"a"b` concatenation | 333.6 ms | 287.2 ms | 1.16x |
| 150k `"abcd"` parts | 57.6 ms | 51.5 ms | 1.12x |
| real source, 1.36 MB and 21.8 MB | 57.4 / 768.1 ms | 57.8 / 766.1 ms | unchanged |

Peak RSS on the same concatenation shapes: 21.9 -> 17.2 MB, 59.6 -> 47.8 MB,
104.9 -> 72.9 MB, 17.1 -> 15.0 MB, 28.1 -> 20.0 MB, that is 69% to 88% of what
it was. Real source is unchanged in both time and memory, as expected: it has
few string literals and few integer literals per byte.

Under `valgrind`, 200k floating literals went from 180,104 heap allocations to
29, and every other shape measured - 150k two-part concatenations, 100k
concatenation parts, real source, character literals, integer literals - now
allocates at most 71 times whatever the token count, so the allocation count is
a function of how far the buffers had to grow and not of the input. `memcheck`
reports no error on any of them.

The profile after the changes, on 10.9 MB of real source, is `SourceReader::
fill` 21%, `scan_identifier_text` 14%, `scan_op_or_punc` 9%,
`lookup_simple_token` 7%, the tokenizer and its conversion 5%. Phases 1 to 3
dominate, which is where PA1's audit left them; nothing in PA2's own work is
above `lookup_simple_token`.

### Validation

Differential against `posttoken-ref`, byte for byte on stdout and on exit
status, one process per probe so a difference is attributed to one probe. The
nine suites below, 38,987 probes in all, were written for this audit and are
independent of the ones the implementation checkpoint used; all were re-run
after every change.

- 7,714 pp-numbers, exhaustive to length 4 over `01.eE+-_ulLfFxX`: 84
  differences, 60 in failure map class 1 and 24 in class 4.
- 8,956 more pp-numbers - 6,000 random length 5-12 over a 38-character
  alphabet, the type boundaries (0, 2^7, 2^8, 2^15, 2^16, 2^31, 2^32, 2^63,
  2^64, 10^25) in all three bases against 31 suffix spellings, and 8 mantissas
  against 15 exponents including overflow and underflow against 5 floating
  suffixes: 38 differences, every one in class 1. No difference at any integer
  type boundary and none in a floating conversion.
- 476 character literals: 38 escape forms including the code point boundaries
  (0x7F, 0x80, 0xD7FF, 0xD800, 0xDFFF, 0xE000, 0xFFFF, 0x10000, 0x10FFFF,
  0x110000, 0xFFFFFFFF), universal-character-names, multi-byte source
  characters, empty and multi-character literals, all four prefixes, with and
  without a ud-suffix: identical.
- 3,736 string sequences: all ordered pairs of a 26-part pool covering every
  prefix, raw and non-raw, reserved and unreserved ud-suffixes and escapes that
  fit only some encodings, 3,000 random 3-to-5 part sequences, and 60
  `operator`-prefixed forms: identical.
- 1,312 raw strings, trigraphs, line splices and ill-formed phase 1-3 inputs -
  8 delimiters against 16 bodies against 5 prefixes, plus unterminated literals
  and comments and invalid UTF-8: one difference, failure map class 6.
- 4,208 operator, keyword and identifier sequences over a pool of 73 operator
  and punctuator spellings including the digraphs, `<::` and the
  preprocessing-only forms, 75 keyword and identifier spellings, and 21 other
  shapes, plus 4,000 random sequences: one difference, failure map class 5.
- 366 binary integer-literals and hexadecimal floating-literals: 6 bodies and 7
  exponents against 6 suffixes each, 250 differences, 40 in failure map class 2
  and 210 in class 3.
- 12,000 random fuzz inputs over a 60-element alphabet including `?`, the
  trigraph sequences and the literal delimiters: 241 differences, 239 in class 6
  and 2 in class 4. No unclassified difference in any suite.
- 219 real source files - every `.cpp` and `.h` of the compiler itself and
  every checked-in PA1 and PA2 test input: identical except the two files
  containing `0x1p+2`, failure map class 3.
- `make test-report-through-pa2`: 88/88, and `pa1` 60/60 within it, so the
  shared lexer change is covered.
- `perl scripts/cppgm_file_audit.pl --stage pa2 --paths dev/src`: passed,
  26 files. The same for `--stage pa1`.
- `valgrind memcheck` on real source and on the integer, character, floating
  and concatenation shapes: no error on any of them.

The existing fixtures already pin the code the changes touch: `300-integer-
limits`, `300-hex-limits` and `200-octal-limits` pin the 2^64 boundary in all
three bases against every suffix spelling, which is what the rewritten overflow
test decides; `200-long-double-floating-suffix` pins `1e40f`, `1e400` and
`1e5000L`, which is what the removed `substr` fed; and `700-hard-string-concat`
is exhaustive over prefixes and ud-suffixes for two- and three-part sequences,
which is what `note_encoding` and `note_suffix` decide. No new fixture was
needed and none was added.

Two regression tests added by the implementation checkpoint remain under
`cppgm.tests/course/pa2` for behaviour no fixture pinned:
`200-long-double-floating-suffix` (the 80-bit `long double` representation with
its six zero padding bytes, and the out-of-range results the conversion change
fixed) and `300-integer-shape-and-escape-range` (`08`/`0x` shapes, and octal and
hexadecimal escapes at the Unicode range boundary in a character literal).

## Active Checkpoint

None open. PA2 is complete; next work is PA3 (`ctrlexpr`), which reuses
`token_model` and `literal_scan` for controlling expressions.

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | Phases 4-6 and phase 7 tokenization: token vocabulary, literal analysis, string concatenation, tokenizer, output | `dev/src/token_model`, `post_token`, `literal_scan`, `string_literal`, `posttokenizer`, `DebugPostTokenStream` | 0/26 -> 28/28, `through-pa2` 88/88. Reference-differential probing established four course rules the handout leaves implicit: a ud-suffix must start with `_`; a numeric escape is a code unit in a string but a code point in a character literal, so its range check belongs to the resolved sequence encoding; `operator ""` plus a reserved suffix splits into two tokens; an ill-formed file still reports the tokens already produced. It also showed the starter `PA2Decode_*` stream conversions no longer match the reference outside a type's range, fixed by calling the conversion they wrap. |
| 2 | PA-wide audit: architecture, correctness and performance | all of `dev/src` for PA2 | 88/88 held. Seven findings fixed: two duplicate ownerships (the 2.13 identifier-like operators, the source-character decoder), one that held a sequence's facts per part, two per-token costs in the literal path, two unrecorded reference divergences, and one overstated comment. 1.05x to 1.18x on literal- and concatenation-dense input and 69% to 88% of the peak RSS on concatenation-dense input, with real source unchanged. 38,987 independent differential probes over nine suites left no unclassified difference. Full findings, changes, evidence and validation in Final Architecture Review. |
