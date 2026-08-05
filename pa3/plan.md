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
| `dev/src/token_model.{h,cpp}` (extended) | Which fundamental types are integral and which of those are signed, from the same list that already owns their spelling and size. |
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
- **The twelve left-recursive binary productions are one precedence table and
  one loop**, so an operator chain of any length costs one pass and no stack.

## Current Failure Map

`make test-report ACTIVE_TEST_REPORT_PAS='pa3'` -> **23 / 23** (20 fixtures at
turn start plus 3 added below). `make test-report-through-pa3` -> **111 / 111**.
No open failures.

Deliberate divergences from `ctrlexpr-ref`, all on input no fixture covers,
where `TESTING_AND_REFERENCES.md` says to resolve from the handout and the
standard. Counts are from the differential suites in the checkpoint ledger.

| # | Input | Reference | Ours | Basis |
| --- | --- | --- | --- | --- |
| 1 | `!0u`, `!5u`, `!(1u)` (93) | `1u`, `0u`, `0u` | `1`, `0`, `0` | 5.3.1/9: the operand of `!` is contextually converted to `bool` and the result is `bool`, which the handout lists as signed, so the operand's signedness does not survive. The reference copies it for `!` alone: it agrees with us that `5u < 3u` is `0` and `5u && 3u` is `1`, which yield `bool` the same way. |
| 2 | `(5/0) ? 1 : 2`, `(1 << 64) ? 7 : 0` (64) | `1`, `7` | `error` | 5.16 evaluates the first operand, and the handout course-defines a zero divisor and a shift count of 64 or more as an error. The reference reports those errors everywhere else, including in a branch it does evaluate, but drops them in the condition and then branches on the undefined value. |
| 3 | a file whose last line ends in a line splice, `"1+\\\n"` (483) | nothing for that line | `error` | The handout's algorithm is "split them by the `new-line` token ... for each non-empty sequence of `preprocessing-tokens` output one line". Phase 3 emits `1` and `+` for such a file, so that sequence is non-empty. 2.2/2 makes the input itself undefined, and both `pptoken` implementations already agree on its tokens. |

Divergences inherited from the phases underneath, which reach PA3 output only
as a wrong `error`: PA2 failure map class 2, binary integer-literals such as
`0b1` (11 probes), and PA1 failure map class 1, the reference not re-entering
its pipeline after a `?`, which makes two consecutive `??/` lines splice for us
and not for it (40 probes). The other PA1 and PA2 divergences turn one `error`
into another `error` here and are invisible.

## Performance Model

Cost is `O(bytes)` for phases 1 to 3, plus `O(1)` per token to convert and
`O(1)` amortized per token to parse and evaluate. Memory is the source file
plus 16 bytes per token of the *longest logical line* and that line's
identifier text; both buffers are grown once and reused, so a whole run costs
28 to 30 heap allocations whatever its shape or token count (`valgrind`, four
shapes; `memcheck` reports no error on any of them).

Measured at `-O3` with output to `/dev/null`, best of five:

| Input | Size | Time | MB/s | Rate | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| `300-triple` fixture | 11.48 MB | 0.589 s | 19.5 | 0.84 M lines/s | 19.7 MB |
| same, x4 | 45.93 MB | 2.333 s | 19.7 | 0.84 M lines/s | 67.7 MB |
| 400k `1 + 2 * 3` lines | 3.81 MB | 0.271 s | 14.1 | 1.48 M lines/s | 7.7 MB |
| 600k identifier terms | 5.38 MB | 0.190 s | 28.3 | | 11.7 MB |
| 600k `defined` operators | 9.38 MB | 0.325 s | 28.9 | | 19.9 MB |
| 800k character literals | 5.26 MB | 0.229 s | 23.0 | | 11.7 MB |
| 400k 19-digit integer literals | 8.32 MB | 0.274 s | 30.4 | | 19.9 MB |
| 400k hexadecimal literals | 7.93 MB | 0.259 s | 30.6 | | 11.7 MB |
| 400k lines that fail to parse | 3.43 MB | 0.204 s | 16.8 | 1.96 M lines/s | 7.7 MB |
| 100k lines rejected at token 1 | 23.17 MB | 0.852 s | 27.2 | | 35.7 MB |
| 5k 200-deep `?:` chains | 3.82 MB | 0.402 s | 9.5 | | 7.7 MB |
| 5k 400-deep parenthesizations | 3.82 MB | 0.334 s | 11.4 | | 7.7 MB |
| 300k comment-heavy lines | 17.45 MB | 0.325 s | 53.7 | | 35.9 MB |
| 3M blank lines | 2.86 MB | 0.065 s | 44.0 | 46 M lines/s | 7.7 MB |
| one 1M-term `+` chain | 1.91 MB | 0.257 s | 7.4 | 7.8 M tokens/s | 37.6 MB |
| mixed lines | 4.67 MB | 0.241 s | 19.4 | 2.49 M lines/s | 11.7 MB |

Throughput is flat from 11 MB to 46 MB, and the adversarial shapes span 7.4 to
53.7 MB/s, so none is more than 2.6x off the fixture in either direction.
Doubling each of the four shapes that can scale - terms in one line, lines in
the file, identifiers, identifier bytes - doubles the time: 71/133/259 ms,
141/272/531 ms, 111/214/420 ms and 85/160/316 ms, that is 1.87x to 1.97x per
doubling, with peak RSS linear in the same way.

Where the time goes, on the `300-triple` fixture: `SourceReader::fill` 16%,
`PPTokenLexer::scan_op_or_punc` 12%, `PPTokenLexer::next` 7%, PA2's
`scan_pp_number` 5%, `scan_identifier_text` 4%, `lookup_simple_token` 4%,
`CtrlExprEvaluator::parse_unary` 4%, `::add` 4%, `::evaluate` 2%. Phases 1 to 3
dominate, which is where PA1's audit left them. `pptoken`, which stops after
phase 3, runs the same fixture in 0.554 s against our 0.589 s, so everything
this assignment adds on top of the lexer is 7%. `ctrlexpr-ref` runs it in
0.995 s, the 400k-line file in 0.446 s, the identifier file in 0.307 s, the
1M-term chain in 0.519 s and the 400k failing lines in 2.574 s, so we are 1.6x
to 2.0x on the ordinary shapes and 12.6x where a line is rejected early.

Nesting is the one thing an input can grow without bound that recursion pays
for, so `parse_conditional`, `parse_binary` and `parse_unary` share a budget of
20000 live frames. The budget counts frames rather than nesting levels because
a parenthesis costs three to fourteen frames and a prefix operator costs one.
Minimum stack a shape actually needs, by binary search on `ulimit -s`:

| Shape | Depth | Stack | Bytes per frame |
| --- | --- | --- | --- |
| parentheses only | 6000 (accepted) | 1208 KB | 84 |
| `?:` chain | 10000 (accepted) | 1484 KB | 74 |
| parenthesis plus all twelve precedence levels | 1430 (accepted) | 1352 KB | 96 |
| prefix operators | 19000 (accepted) | 968 KB | 41 |
| parentheses | 20000 (rejected at the budget) | 1328 KB | |

So no shape reaches 1.5 MB, which is 19% of a default 8 MB stack, and past the
budget the line is an `error`. The reference segmentation faults on 100000
nested parentheses.

## Active Checkpoint

None open. PA3 is complete; next work is PA4 (`macro`), which reuses
`CtrlExprEvaluator` with a real macro table in place of
`PA3Mock_IsDefinedIdentifier`.

## Completed Checkpoints

| # | Checkpoint | Owner | Result |
| --- | --- | --- | --- |
| 1 | The controlling-expression stage: per-line splitting, preprocessing-token-context conversion, the grammar, and evaluation with liveness | `dev/src/ctrl_expr`, `ctrl_expr_stream`, extensions to `token_model` and `post_token`, `dev/ctrlexpr.cpp` | 0/20 -> 23/23, `through-pa3` 111/111. Differential probing over 89,095 probes in fifteen suites - operators, literals, `defined`, liveness, grammar errors, fuzz, and logical-line structure as single lines, pairs and triples - left three divergences from the reference, plus two inherited from PA1 and PA2, and no unclassified difference. Probing also established two course rules the handout leaves implicit: the type of `?:` comes from both branches even though only one is evaluated, and a shift error in a dead operand is not reported while a non-integral literal in one still fails the line. Packing the per-token record to 16 bytes cut the peak RSS of a one-million-term line from 71 MB to 38 MB and its time by 1.22x; making the recursion budget count frames rather than nesting levels raised the accepted depth 6x while holding the worst measured stack under 1.5 MB. Added `200-identifier-like-operators`, `250-liveness-nested` and `300-literal-signedness` under `cppgm.tests/course/pa3` for behaviour no fixture pinned. |
