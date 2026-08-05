# PA3 (ctrlexpr) Plan

## Stage Design

PA3 owns 16.1 Conditional Inclusion: the controlling expression of `#if` and
`#elif`. The stage drives the PA1 lexer, splits preprocessing-tokens at
`new-line`, converts each one in *preprocessing-token context* (so a keyword is
still an identifier), then parses and evaluates the line by the assignment's
grammar. The implementation lives in `dev/src` because PA4 and PA5 (`macro`,
`preproc`) evaluate the same expressions with a real macro table in place of
PA3's mock; `dev/ctrlexpr.cpp` is only a driver.

| Module | Owns |
| --- | --- |
| `dev/src/ctrl_expr.{h,cpp}` | The controlling-expression stage: the token form one logical line is accumulated in, the conversion of a preprocessing-token into it, the grammar, and the behaviour of every operator in it. |
| `dev/src/ctrl_expr_stream.h` | The output format: a decimal, a decimal with `u`, `error`, `eof`. |
| `dev/src/token_model.{h,cpp}` (extended) | The 2.13 operators and punctuators as one list per spelling shape, shared with phase 3; which fundamental types are integral and which of those are signed, from the same list that already owns their spelling and size. |
| `dev/src/post_token.h` (extended) | `PostToken::integer_value`, the inverse of `set_integer_value`: an integral literal's value sign extended to 64 bits. |

Data flow: bytes -> `PPTokenLexer` (PA1) -> `CtrlExprEvaluator::add` per
preprocessing-token, using `scan_pp_number` and `scan_character_literal` (PA2)
-> one line of `CtrlExprEvaluator::Token` -> `evaluate` -> `CtrlExprValue` ->
`CtrlExprResultStream`.

Key facts this stage establishes, each with one owner:

- **A controlling expression sees phase 3 identifiers, not phase 7 tokens.**
  `auto` is an identifier and evaluates as `0`; `and`, `not` and `bitand` are
  operators because phase 3 already classifies them as preprocessing-op-or-punc;
  `new` and `delete` are in that same 2.13 list, so they are operators with no
  production in the grammar and their line is an `error`.
- **A value is a 64-bit pattern plus a signedness** (`CtrlExprValue`), because
  16.2.4 makes every signed type act as `intmax_t` and every unsigned type as
  `uintmax_t`. Nothing after the literal needs the literal's own type.
- **Liveness is a parameter of evaluation, not a second pass.** `?:`, `&&` and
  `||` evaluate one operand region; the other is still parsed and still typed,
  but its division, modulus and shift errors are not reported. That is what
  makes `true?5:5/0` be `5` and `false?5/0u:-5` be `18446744073709551611u`.
- **An invalid or non-integral token fails the line before it is parsed**, so a
  floating literal or a string literal in a dead branch is still an `error`,
  and the rest of such a line is never analysed.
- **Nesting is data, not call frames.** The four things the grammar nests
  without bound - `(`, a prefix operator, and the two operands of `?:` - are one
  heap stack of 16-byte `Pending` records, and the twelve left-recursive binary
  productions are one precedence table over that same stack. An operator chain
  or a nest of any depth costs one pass and linear heap, and the parse needs no
  depth limit of its own.

## Current Failure Map

`make test-report ACTIVE_TEST_REPORT_PAS='pa3'` -> **24 / 24**.
`make test-report-through-pa3` -> **112 / 112**. No open failures.

Deliberate divergences from `ctrlexpr-ref`, all on input no fixture covers,
where `TESTING_AND_REFERENCES.md` says to resolve from the handout and the
standard. Counts are from the differential suites in the checkpoint ledger.

| # | Input | Reference | Ours | Basis |
| --- | --- | --- | --- | --- |
| 1 | `!0u`, `!5u`, `!(1u)`, and anything downstream of one such as `-!0u >> 1` (448) | `1u`, `0u`, `0u`, `9223372036854775807u` | `1`, `0`, `0`, `-1` | 5.3.1/9: the operand of `!` is contextually converted to `bool` and the result is `bool`, which the handout lists as signed, so the operand's signedness does not survive. The reference copies it for `!` alone: it agrees with us that `5u < 3u` is `0` and `5u && 3u` is `1`, which yield `bool` the same way. |
| 2 | a course-defined error anywhere in the first operand of `?:`, `(5/0) ? 1 : 2`, `(1 ? 1/0 : 2) ? 3 : 4` (704) | `1`, `3` | `error` | 5.16 evaluates the first operand, and the handout course-defines a zero divisor and a shift count of 64 or more as an error. The reference reports those errors everywhere else, including in a branch it does evaluate; in a first operand it drops the report, yields the left operand of the failed operator, and branches on that. |
| 3 | a file whose last line ends in a line splice, `"1+\\\n"` (1) | nothing for that line | `error` | The handout's algorithm is "split them by the `new-line` token ... for each non-empty sequence of `preprocessing-tokens` output one line". Phase 3 emits `1` and `+` for such a file, so that sequence is non-empty. 2.2/2 makes the input itself undefined, and both `pptoken` implementations already agree on its tokens. |

Divergences inherited from the phases underneath, which reach PA3 output only
as a wrong `error`: PA2 failure map class 2, binary integer-literals such as
`0b1` (24 probes), and PA1 failure map class 1, the reference not re-entering
its pipeline after a `?`, which makes two consecutive `??/` lines splice for us
and not for it (1 probe). The other PA1 and PA2 divergences turn one `error`
into another `error` here and are invisible.

We accept input the reference cannot: it segmentation faults on 100000 nested
parentheses and on 500000 prefix operators, where we evaluate both.

## Performance Model

Cost is `O(bytes)` for phases 1 to 3, plus `O(1)` per token to convert and
`O(1)` amortized per token to parse and evaluate. Memory is the source file
plus 16 bytes per token of the *longest logical line*, that line's identifier
text, and 16 bytes per live nesting level; every buffer is grown once and
reused, so a whole run costs 14 to 45 heap allocations whatever its shape,
token count or depth (`valgrind`, four shapes; `memcheck` reports no error on
any of them).

Measured at `-O3` with output to `/dev/null`, best of five:

| Input | Size | Time | MB/s | Rate | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| `300-triple` fixture | 11.48 MB | 0.590 s | 19.5 | 0.84 M lines/s | 19.7 MB |
| same, x4 | 45.93 MB | 2.370 s | 19.4 | 0.84 M lines/s | 67.7 MB |
| 400k `1 + 2 * 3` lines | 3.81 MB | 0.270 s | 14.1 | 1.48 M lines/s | 7.7 MB |
| 600k identifier terms | 5.72 MB | 0.160 s | 35.8 | | 11.7 MB |
| 600k `defined` operators | 8.58 MB | 0.320 s | 26.8 | | 19.6 MB |
| 800k character literals | 3.81 MB | 0.170 s | 22.4 | | 7.7 MB |
| 400k 19-digit integer literals | 8.01 MB | 0.260 s | 30.8 | | 19.4 MB |
| 400k hexadecimal literals | 7.82 MB | 0.240 s | 32.6 | | 11.7 MB |
| 400k lines that fail to parse | 3.05 MB | 0.190 s | 16.1 | 2.11 M lines/s | 7.7 MB |
| 100k lines rejected at token 1 | 8.11 MB | 0.230 s | 35.2 | | 19.7 MB |
| 5k 200-deep `?:` chains | 7.64 MB | 0.480 s | 15.9 | | 11.7 MB |
| 5k 400-deep parenthesizations | 3.82 MB | 0.310 s | 12.3 | | 7.7 MB |
| one 1M-deep parenthesization | 1.91 MB | 0.250 s | 7.6 | 8.0 M tokens/s | 67.1 MB |
| 300k comment-heavy lines | 15.64 MB | 0.270 s | 57.9 | | 19.7 MB |
| 3M blank lines | 2.86 MB | 0.060 s | 47.7 | 50 M lines/s | 7.7 MB |
| one 1M-term `+` chain | 1.91 MB | 0.240 s | 7.9 | 8.3 M tokens/s | 37.6 MB |
| mixed lines | 6.77 MB | 0.350 s | 19.3 | 2.29 M lines/s | 11.7 MB |

Throughput is flat from 11 MB to 46 MB, and the adversarial shapes span 7.6 to
57.9 MB/s, so none is more than 3x off the fixture in either direction. Each of
the four things an input can grow without bound doubles the time and the peak
RSS when it doubles:

| Doubled | Time | Peak RSS |
| --- | --- | --- |
| terms in one line, 250k/500k/1M | 60/120/240 ms | 12/20/37 MB |
| lines in the file, 100k/200k/400k | 70/130/270 ms | 4/5/7 MB |
| identifiers, 150k/300k/600k | 40/80/160 ms | 5/7/11 MB |
| nesting depth, 250k/500k/1M | 60/120/250 ms | 19/35/66 MB |

Where the time goes, on the `300-triple` fixture: `SourceReader::fill` 17%,
`PPTokenLexer::scan_op_or_punc` 13%, `PPTokenLexer::next` 7%, PA2's
`scan_pp_number` 6%, `scan_identifier_text` 4%, `lookup_simple_token` 4%,
`CtrlExprEvaluator::evaluate` 4% (the whole parse, which the compiler inlines
into it), `::add` 3%. Phases 1 to 3 dominate, which is where PA1's audit left
them. `pptoken`, which stops after phase 3, runs the same fixture in 0.540 s
against our 0.590 s, so everything this assignment adds on top of the lexer is
9%. `lookup_simple_token` re-derives from an operator's spelling what phase 3's
maximal munch already knew, but the two ask different questions of 2.13 - a
membership test with the four preprocessing-only spellings in it, and a type
lookup with them out - so they stay two lookups over one shared list rather
than one lookup that leaks phase 7 vocabulary into phase 3.

`ctrlexpr-ref` runs the fixture in 0.980 s, the 400k-line file in 0.430 s, the
identifier file in 0.290 s, the 1M-term chain in 0.500 s at 103 MB and the 400k
failing lines in 2.560 s, so we are 1.5x to 2.3x on the ordinary shapes and
13.5x where a line is rejected early.

## Architecture Review

The stage is four owners deep and each fact is stated once.

- **Phase 3 and phase 7 share the 2.13 list.** `token_model.h` states the
  identifier-like operators, the punctuation operators and the four spellings
  reserved for preprocessing. `pptoken_lexer.cpp` builds its maximal-munch
  membership test from the last two, `token_model.cpp` builds the phase 7
  spelling table from the first two, and a `static_assert` per punctuation
  spelling proves each fits the four-character munch window.
- **PA3 converts a preprocessing-token itself rather than reusing
  `PostTokenizer`.** The two switches differ in exactly the rule the assignment
  names - an identifier stays an identifier here, so a keyword is one too - and
  everything under that rule (`lookup_simple_token`, `scan_pp_number`,
  `scan_character_literal`) is the same code. `ctrlexpr` therefore links neither
  `posttokenizer` nor `string_literal`.
- **The typed fact never becomes text again.** A literal's value crosses from
  PA2 as `PostToken::integer_value`, an accessor on the object representation
  `set_integer_value` wrote, and reaches output as `CtrlExprValue`, which
  `CtrlExprResultStream` formats by appending digits to a block buffer. No stage
  re-lexes, re-parses or re-formats anything.
- **One line's state is three reused buffers.** `tokens_`, `names_` and
  `pending_` are cleared, not freed, between lines.

## Final Architecture Review

### Findings

1. **The parse had a depth limit that rejected valid input.**
   `kMaxParseDepth = 20000` bounded recursion in `parse_conditional`,
   `parse_binary` and `parse_unary` so that a deep nest could not overflow the
   stack. A parenthesis cost three frames, so 6666 nested parentheses became
   `error` where the reference evaluates them, and so did 20000 `?:` levels and
   20000 prefix operators. This was the one thing an input could grow that the
   stage did not scale in, and it was an unrecorded divergence.
2. **The 2.13 punctuation list was stated twice.** `is_operator_or_punctuator`
   in `pptoken_lexer.cpp` listed 57 spellings for phase 3's maximal munch and
   `CPPGM_SIMPLE_TOKEN_SPELLINGS` in `token_model.cpp` listed the same 53 plus
   keywords for phase 7. The two agreed, but nothing made them: the PA2 audit
   had consolidated the identifier-like half of 2.13 and left the punctuation
   half duplicated.
3. **Divergence 2 was recorded too narrowly.** The failure map described the
   reference as dropping an error "in the condition"; it drops it anywhere in
   the first operand's subtree, including inside a `?:` or a `&&` nested in it,
   and then branches on the left operand of the operator that failed.
4. **Divergence 1 was recorded without its downstream cases.** `-!0u` and
   `-!0u >> 1` differ in the printed value, not only in the suffix, because the
   reference's unsigned `!` result changes what `-` and `>>` do next.

Nothing else: no correctness regression, no skipped phase, no dummy or embedded
output, no interpreter or trampoline substitute, no fixture-name gate, no
timeout workaround, no weakened check, and no file-audit bypass. The largest
file in the stage is 776 lines against a 1500-line limit and the largest
function 89 lines against 120.

### Changes

1. `dev/src/ctrl_expr.{h,cpp}`: replaced the recursive descent with the same
   grammar over an explicit `std::vector<Pending>`. `parse_operand` stacks `(`
   and prefix operators, `after_operand` decides what a completed operand
   belongs to, and `complete_pending` finishes every pending operator the next
   token does not bind tighter than. Liveness became the value each frame
   restores when it completes, so `&&`, `||` and `?:` suppress the errors of a
   dead operand exactly as before. `kMaxParseDepth`, `depth_`,
   `parse_conditional`, `parse_binary` and `parse_unary` are gone; the stage now
   has no depth limit at all, and nesting costs the same linear heap the token
   vector costs.
2. `dev/src/token_model.h`, `token_model.cpp`, `pptoken_lexer.cpp`: moved the
   2.13 punctuation spellings into `CPPGM_PUNCTUATION_OPERATORS` and the four
   preprocessing-only spellings into `CPPGM_PREPROCESSING_ONLY_OPERATORS`, both
   in the header phase 3 and phase 7 already share. `is_operator_or_punctuator`
   is now generated from those two lists through a `constexpr spelled_text`, and
   a `static_assert` per spelling proves the munch window holds it.
3. `cppgm.tests/course/pa3/300-deep-nesting.t`: seven lines the depth limit
   rejected and the reference accepts - 7000 nested parentheses, 21001 prefix
   operators, 7000 unmatched parentheses, a dead `1 / 0` under 7000 of them, a
   deep `?:` branch, a 4000-level `?:` chain and 7000 `!` operators.
4. `pa3/plan.md`: this review, and divergences 1 and 2 restated.

### Performance Evidence

The rewrite is throughput-neutral on every shape and removes a limit. Best of
five, before and after: `300-triple` 0.580/0.590 s, 400k lines 0.260/0.270 s,
1M-term chain 0.240/0.240 s, 5k 400-deep parenthesizations 0.320/0.310 s, 5k
200-deep `?:` chains 0.460/0.480 s, 100k lines rejected at token 1 0.230/0.230 s.
The profile is unchanged: the parse is 4-5% of a run and phases 1 to 3 are the
rest. Consolidating the 2.13 list changed no time and no byte of output;
`is_operator_or_punctuator` compiles to the same switch.

What the rewrite buys is the fourth scaling dimension. Nesting depth is now
linear rather than capped: 250k/500k/1M nested parentheses take 60/120/250 ms at
19/35/66 MB, 1M levels evaluate where 6666 used to fail, and 500k prefix
operators evaluate where the reference segmentation faults. Allocation counts
stay flat - 14 for 3M blank lines, 34 for mixed lines, 36 for a 1M-term chain,
45 for 400-deep parenthesizations - and `memcheck` is clean on all four.

### Validation

- `make test-report-through-pa3` -> 112/112, `pa1` 60/60, `pa2` 28/28,
  `pa3` 24/24.
- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src` -> 29 files,
  pass.
- 102,520 independent differential probes against `ctrlexpr-ref` in twelve
  suites: 14,112 binary-operator pairs over 28 extreme values, 344 unary, 1,888
  literal spellings including every escape and encoding form, 170 `defined`
  forms, 150 liveness cases, 6,000 random token sequences, 12,000 random
  precedence shapes, 7,827 byte fuzz, 60,000 structural fuzz, and 29 structural
  and depth cases; plus every 2.13 spelling in 6,674 contexts through `pptoken`
  and `posttoken`, which are byte-identical to their references. Every
  difference falls in the failure map above - 448, 704, 1, 24 and 1 probe in its
  five classes - and the four suites that avoid the diverging constructs, 34,109
  probes, are byte-identical.
- The 60,000 structural probes were also checked against an independent model of
  the handout's semantics written from the standard rather than from this code.
  The model reproduces `ctrlexpr` byte for byte with the reference's two quirks
  off, and `ctrlexpr-ref` byte for byte with them on, which is what pins the
  divergence set to exactly those two.
- Every probe corpus was re-run after each change and after the whole audit with
  identical output.
- `valgrind memcheck` on four shapes: no error, no leak.

## Active Checkpoint

None open. PA3 is complete; next work is PA4 (`macro`), which reuses
`CtrlExprEvaluator` with a real macro table in place of
`PA3Mock_IsDefinedIdentifier`.

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | The controlling-expression stage: per-line splitting, preprocessing-token-context conversion, the grammar, and evaluation with liveness | `dev/src/ctrl_expr`, `ctrl_expr_stream`, extensions to `token_model` and `post_token`, `dev/ctrlexpr.cpp` | 0/20 -> 23/23, `through-pa3` 111/111. Differential probing over 89,095 probes in fifteen suites - operators, literals, `defined`, liveness, grammar errors, fuzz, and logical-line structure as single lines, pairs and triples - left three divergences from the reference, plus two inherited from PA1 and PA2, and no unclassified difference. Probing also established two course rules the handout leaves implicit: the type of `?:` comes from both branches even though only one is evaluated, and a shift error in a dead operand is not reported while a non-integral literal in one still fails the line. Packing the per-token record to 16 bytes cut the peak RSS of a one-million-term line from 71 MB to 38 MB and its time by 1.22x. Added `200-identifier-like-operators`, `250-liveness-nested` and `300-literal-signedness` under `cppgm.tests/course/pa3` for behaviour no fixture pinned. |
| 2 | PA-wide audit: architecture, correctness and performance | all of `dev/src` for PA3 | 111/111 -> 112/112. Four findings fixed: a parse depth limit that rejected valid input, the last duplicate statement of the 2.13 list, and two understated divergence records. The parser became an explicit stack, which removed the limit entirely and made nesting depth scale linearly in time and heap like the other three dimensions - 1M nested parentheses in 250 ms at 66 MB where 6666 used to be an `error` - at unchanged throughput on every other shape. 102,520 independent differential probes over twelve suites, plus an independent model of the handout's semantics that reproduces both implementations, left no unclassified difference. Added `300-deep-nesting` under `cppgm.tests/course/pa3`. Full findings, changes, evidence and validation in Final Architecture Review. |
