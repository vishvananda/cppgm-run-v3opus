# PA6 `recog` Plan

## Stage Design

`recog` answers one question per source file: does its phase-1..7 token
sequence match `translation-unit` of `pa6.gram`?  Output is `OK`/`BAD` only, so
the stage owns three new pieces of typed state and reuses everything else:

| Owner | Fact it owns |
| --- | --- |
| `dev/src/parse_token.h/.cpp` | `ParseToken`: one PA6 terminal plus the lexical facts the grammar asks about a token (mock-lookup name categories, `ST_OVERRIDE`/`ST_FINAL`, `ST_EMPTYSTR`, `ST_ZERO`), 4 bytes each.  Built once per file from `Preprocessor` + `PostTokenizer`, with `OP_RSHIFT` split into `ST_RSHIFT_1 ST_RSHIFT_2` and a trailing `ST_EOF`. |
| `dev/src/parse_cursor.h` | `ParseCursor`: a position in that sequence plus the one bit of bracket state 14.2.3 reads back, and the `Mark` that undoes a failed alternative. |
| `dev/src/recognizer.h` + `recognizer{,_name,_expression,_declarator,_statement,_member}.cpp` | The recursive-descent recognizer: one function per nonterminal and a `(rule, position, angle)` memo table. |
| PA1-PA5 modules | unchanged; phases 1 to 7 are reused as-is. |

Mock name lookup is a lexical fact of the spelling (`C`/`T`/`Y`/`E`/`N`), so it
is computed once per token when the stream is built and never re-derived while
parsing.

Three decisions drive the parser shape:

- **Angle commit.** An identifier that is a `template-name` followed by `OP_LT`
  *must* parse as a `simple-template-id`; no alternative falls back to reading
  it as a plain identifier.  That is what makes `int x = T1 < 2;` `BAD`, and it
  is what bounds the work deep template nesting can cost.
- **Angle guard.** While the innermost open bracket pair is `<>`, `OP_GT` is
  refused as a relational operator and `ST_RSHIFT_1 ST_RSHIFT_2` as a shift
  operator, so the first non-nested close-angle-bracket always closes the pair.
- **FOLLOW-checked alternatives.** Where two alternatives share an unbounded
  prefix (`template-argument`, `parameter-declaration`, `class-head`,
  `enum-head`, `exception-declaration`), the alternative is accepted only when
  it ends where its caller can continue.  That is what tells `TC1<C*>` from
  `TC1<C+1>` without backtracking into a rule that already succeeded.

## Current Failure Map

Turn-start baseline: 0/47 (`recog` was the PA6 stub).  Current: 47/47, and
`make test-report-through-pa6` is 313/313.  No known failures remain; the
groups below are what the checkpoint had to cover.

| Group | Tests | Shared behavior |
| --- | --- | --- |
| tool + stream | `100-empty`, `101-bad`, `300-invalid-token-*` | argv contract, per-file verdict, phase 1-7 failure is `BAD` not `EXIT_FAILURE` |
| expressions | `120`, `121`, `122`, `130*`, `131`, `140`, `150-assignment`, `150-pm`, `200*`, `201`, `500-operator-*` | primary/postfix/unary/cast/binary/conditional, ids, lambdas |
| statements | `150-statement`, `150-goto`, `150-jump`, `400-exceptions`, `300-try-*`, `150-bare-label-*`, `300-empty-case-*` | statement dispatch, conditions, handlers |
| declarations | `100-main`, `180`, `250*`, `260`, `270`, `300-enum`, `400-*`, `600`, `700` | decl-specifier-seq rule, declarators, initializers, classes, enums |
| templates + angles | `450`, `500-closing-angle-*`, `500-template-name-angle-commit`, `500-deep-*` | angle commit, angle guard, memoized failure |

## Active Checkpoint

None: CP1 completed the assignment.  Next work on this stage is only whatever
a later PA needs from the parse, which will replace the recognizer's `bool`
results with typed syntax facts.

## Performance Model

- **Token stream** - one pass, 4 bytes per token, no per-token allocation.
- **Memoization is load-bearing, not an optimisation.** `template-argument`
  has three alternatives that each re-descend, so `N` nested `TC1<` costs
  `3^N` without a memo.  Measured with the memo lookup disabled, the deep
  witness takes 0.10 s at N=6, 0.40 s at N=7, 2.9 s at N=8 and more than 30 s
  at N=10.  With the memo it is 0.00 s at N=1600.
- **Memo budget.** A memo entry is a fact about a position, so dropping one
  costs time and never correctness.  The table is released at a top-level
  declaration boundary once it passes 2^18 entries, which bounds it by the
  largest single declaration instead of by the translation unit.  On a 4.3 MB
  source this cut 2.56 s / 176 MB to 1.03 s / 26 MB.
- **Throughput** - 1.07 MB in 0.32 s / 17 MB, 2.1 MB in 0.66 s / 20 MB, 4.3 MB
  in 1.03 s / 26 MB: linear in tokens, near-flat in memory.
- **Depth** - the descent is bounded at 20000 memoized frames, about 3300
  levels of template nesting (measured: valid nesting parses to N=3000 and is
  refused at N=4000, and the unbounded descent crashes near N=12000).  Annex B
  recommends supporting 256.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| CP1 | Whole `recog`: PA6 terminal stream (`parse_token`), token cursor with the 14.2.3 angle state (`parse_cursor`), and the full `pa6.gram` recursive-descent recognizer over six modules, with angle commit, FOLLOW-checked alternatives and a `(rule, position, angle)` memo | 0/47 -> 47/47 pa6; 313/313 through pa6; file audit clean; scaling witness measured |
