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
| `dev/src/token_model.{h,cpp}` | The phase 7 vocabulary: `EFundamentalType` with its course ABI size, `ETokenType`, their names, and the spelling -> `ETokenType` lookup for keywords, operators and alternative tokens. Each enumeration and its name table come from one list, so a type and its spelling cannot drift apart. |
| `dev/src/post_token.h` | `PostToken`: one analysed token as a typed fact - kind, source, token type, fundamental type, object representation, ud-suffix. Its buffers are reused across tokens. |
| `dev/src/literal_scan.{h,cpp}` | 2.14.2, 2.14.3, 2.14.4 and 2.14.8 for one spelling: the c-char/s-char element decoder, pp-number classification and typing, character-literal typing. |
| `dev/src/string_literal.{h,cpp}` | Phase 6: accumulates a maximal string-literal sequence, resolves its encoding-prefix and ud-suffix, and encodes the bodies into code units. |
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
where the handout points at the C++11 grammar ("If it doesn't match any of
these grammars, output it as `invalid`"). Counts are from the differential
suites in Validation.

| # | Input | Reference | Ours | Basis |
| --- | --- | --- | --- | --- |
| 1 | `0_.`, `9_fl8.e.e` (561) | user-defined-literal with suffix `_.` | `invalid` | 2.14.8: a ud-suffix is an identifier. The reference applies that rule to a floating prefix - fixture `300-invalid-floating-literal-shapes` pins `1.0_foo.bar` as invalid - but not to an integer one. |
| 2 | `0b101`, `0B11110000_buf` (11) | binary integer-literal | `invalid` | Binary literals are C++14. `pa34/tests/preproc/400-host-binary-integer-ud-literal` shows they belong to the hosted GNU-extension assignment. |
| 3 | `0x1p3`, `0XbEp0` (1) | `double` | `invalid` | Hexadecimal floating literals are C99. `pa34/tests/preproc/400-host-gnu-hex-float-pp-number` shows the same. |
| 4 | `.0u` (10) | `unsigned int 0` | `invalid` | A fractional-constant with an integer-suffix matches neither 2.14.2 nor 2.14.4. |

## Performance Model

Cost is `O(bytes)` for phases 1-3 plus `O(spelling)` per token to analyse, with
no input super-linear in either and no per-token heap allocation once the
buffers have grown. Measured at `-O3` with output to `/dev/null`, best of
three: **25.8 MB/s** on real source, where PA1 alone runs at 34.3 MB/s, so
phase 7 costs about a third again of the lexing it sits on.

Dominant operations, and why each is constant per unit:

- Keyword and operator lookup: one FNV-1a pass over the spelling and one probe
  into a 512-slot open-addressed table built once. An identifier that is not a
  keyword is rejected on that first probe, and anything longer than
  `reinterpret_cast` never reaches the table.
- A string sequence: each spelling is appended to one reused buffer that is
  also the token's source, with a `(body, suffix, encoding)` record per part.
  Nothing is copied twice, and the buffer is swapped into the token rather than
  copied. `build` walks the records once to resolve the encoding and the bodies
  once to encode them, so a 200k-part sequence costs the same per part as 200k
  two-part sequences.
- Encoding an ordinary or `u8` body: `memchr` to the next `\`, then a block
  append, because the execution character set is UTF-8 and the body already is.
  A raw body is one append. Only `char16_t`, `char32_t` and `wchar_t` bodies
  decode UTF-8 per character.
- An integer literal: one pass over the digits with a pre-multiply overflow
  test, then a first-fit walk over at most six candidate types.
- A floating literal: 200 ns each end to end, of which the conversion is 55 ns.
  The conversion is `strtof`/`strtod`/`strtold`, which is what the starter
  code's `istringstream >> x` calls internally; going through the stream costs
  310 ns instead, so the direct call is 1.7x on floating-dense input as well as
  being the form that still matches the reference out of range (see Validation).
- Output: lines are packed into a 64 KB buffer and written a block at a time,
  and a hexdump is written through a byte-pair table, since a literal's object
  representation can be as large as the literal.

| Input | Size | Time | MB/s | Rate | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| real-world + compiler sources | 1.03 MB | 0.040 s | 25.8 | | 5.0 MB |
| same, x4 | 4.12 MB | 0.150 s | 27.5 | | 7.7 MB |
| same, x16 | 16.48 MB | 0.620 s | 26.6 | | 19.7 MB |
| mixed operators and keywords | 1.56 MB | 0.060 s | 26.0 | | 5.7 MB |
| 1.5M one-character tokens | 3.00 MB | 0.220 s | 13.6 | 6.8 M tokens/s | 7.7 MB |
| 300k integer literals | 2.10 MB | 0.100 s | 21.0 | 3.0 M literals/s | 7.4 MB |
| 200k floating literals | 1.20 MB | 0.070 s | 17.1 | 2.9 M literals/s | 5.7 MB |
| 300k character literals | 2.10 MB | 0.080 s | 26.2 | 3.8 M literals/s | 7.5 MB |
| one 200k-part concatenation | 1.40 MB | 0.060 s | 23.3 | 3.3 M parts/s | 21.2 MB |
| one 4 MB ordinary string body | 4.00 MB | 0.110 s | 36.4 | | 33.9 MB |
| one 4 MB `u""` string body | 4.00 MB | 0.190 s | 21.1 | | 64.5 MB |
| one 4 MB raw string body | 4.00 MB | 0.090 s | 44.4 | | 33.9 MB |

Throughput is flat from 1 MB to 16 MB (25.8 -> 26.6 MB/s). Real source is
183,806 tokens in 1.03 MB, or 4.6 M tokens/s, and the adversarial shapes sit
between 2.9 and 6.8 M tokens/s, so none is more than 1.6x off the real-source
token rate: the floating literals are the slow end and the one-character
tokens the fast end, where PA1 alone reaches 8.8 M/s. Peak RSS is the source
plus, for a string literal, its encoded data and its hexdump, each proportional to the
literal; the 200k-part case adds 48 bytes per part for the sequence records.

## Validation

Differential against `posttoken-ref`, byte for byte on stdout and on exit
status. Probes are packed into one file separated by a marker identifier and
the outputs split on it, so a batch covers hundreds of probes and still
attributes a difference to one probe.

- 41,086 pp-numbers, exhaustive to length 3 over `018.eExX+-_ulLfFbp` plus 40k
  random length 5-10: 586 differences, every one in the four classes of the
  failure map and no other.
- 912 integer literals at the type boundaries (0, 2^7, 2^8, 2^15, 2^16,
  2^31+-1, 2^32+-1, 2^63+-1, 2^64+-1, 10^25) in all three bases against all 19
  suffix spellings: identical.
- 1,352 floating literals over 13 mantissas, 13 exponents including overflow
  and underflow, and 8 suffixes: identical. This is what showed that
  `istringstream` no longer reproduces the reference out of range - it stores
  the largest representable value where the reference stores an infinity - and
  that `strtod` does; 400k random in-range literals of each of `float`,
  `double` and `long double` confirmed the two agree bit for bit in range.
- 612 character literals: every escape form, the code point boundaries
  (0x7F, 0x80, 0xD7FF, 0xD800, 0xDFFF, 0xE000, 0xFFFF, 0x10000, 0x10FFFF,
  0x110000, 0xFFFFFFFF), multi-byte source characters and
  universal-character-names, all four prefixes, with and without a ud-suffix:
  identical.
- 825 string literals over the same bodies and all five prefixes, raw and
  non-raw: identical.
- 3,225 concatenations: all ordered pairs of 15 parts covering every prefix,
  reserved and unreserved ud-suffixes, and escapes that fit only some
  encodings, plus 3k random 3-to-5 part sequences: identical.
- 6,000 random token sequences over a 37-entry pool including `operator`,
  `""sv`, digraphs, the preprocessing-only operators and non-whitespace
  characters: identical.
- 42 real source files: identical. 183 checked-in PA1 and PA2 test inputs,
  which the `pa2/course` symlink reaches twice: identical except the two
  distinct files that contain `0x1p+2` (failure map 3).
- `make test-report-through-pa2`: 88/88.
- `perl scripts/cppgm_file_audit.pl --stage pa2 --paths dev/src`: passed,
  26 files.

Two regression tests were added under `cppgm.tests/course/pa2` for behaviour
no fixture pinned: `200-long-double-floating-suffix` (the 80-bit `long double`
representation with its six zero padding bytes, and the out-of-range results
that the conversion change fixed) and `300-integer-shape-and-escape-range`
(`08`/`0x` shapes, and octal and hexadecimal escapes at the Unicode range
boundary in a character literal).

## Active Checkpoint

None open. PA2 is complete; next work is PA3 (`ctrlexpr`), which reuses
`token_model` and `literal_scan` for controlling expressions.

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | Phases 4-6 and phase 7 tokenization: token vocabulary, literal analysis, string concatenation, tokenizer, output | `dev/src/token_model`, `post_token`, `literal_scan`, `string_literal`, `posttokenizer`, `DebugPostTokenStream` | 0/26 -> 28/28, `through-pa2` 88/88. Reference-differential probing established four course rules the handout leaves implicit: a ud-suffix must start with `_`; a numeric escape is a code unit in a string but a code point in a character literal, so its range check belongs to the resolved sequence encoding; `operator ""` plus a reserved suffix splits into two tokens; an ill-formed file still reports the tokens already produced. It also showed the starter `PA2Decode_*` stream conversions no longer match the reference outside a type's range, fixed by calling the conversion they wrap. |
