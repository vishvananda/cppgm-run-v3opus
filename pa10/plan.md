# PA10 Plan - `cppgm++ --emit-ast`

## Stage Design

PA10 replaces the PA6 recognizer boundary with a tree-building parser for the
shared source grammar (`pa10.gram`) and a deterministic line dump of the tree.

Owners, all under `dev/src`:

| Owner | Responsibility |
| --- | --- |
| `ast_tokens.{h,cpp}` | phases 1-7 into a flat terminal array with interned spellings; `flatten(begin,end)` spells a token span back out |
| `ast_names.h` | `DeclaredNames`: scoped map from a name to `Type` / `Value` / `Template` |
| `ast_model.{h,cpp}` | `AstKind`, `AstNode`, `AstArena`, `write_ast` |
| `ast_parser.h` + 6 `.cpp` | recursive descent with ordered choice and full backtracking |
| `ast_emit.{h,cpp}` | driver: per translation unit, tokens -> parser -> dump |
| `dev/cppgm++.cpp` | `--emit-ast -o out src...` argument surface |

Data flow: `AstTokenStream` -> `AstParser` (cursor + `DeclaredNames` +
`qualified_` spelling map) -> `AstNode` tree in an `AstArena` -> `write_ast`.

Three facts the parser keeps as typed state, because the grammar leaves them
open and later PAs must not re-derive them from source text:

1. **Spelled names.** A template-id, qualified name, decltype-specifier,
   placement clause or lambda introducer is dumped as it was written. Rules
   record the token span they matched; `AstTokenStream::flatten` writes it back,
   inserting a separator only where two tokens would run into one word.
2. **Name kinds.** `foo(x);` is a declaration when `foo` names a type and a call
   when it names an object; `a < b > c` is a template-id when `a` names a
   template. `DeclaredNames` answers both, and `qualified_` answers the same for
   `ns::f` by spelling, since PA10 models no scopes to look into.
3. **Bracket state.** `angle_` says whether the innermost open pair is `<>`, so
   `>` closes it instead of comparing (14.2.3). `BracketGuard` restores it when
   the rule that opened the pair leaves, however it leaves.

## Current Failure Map

`make test-report ACTIVE_TEST_REPORT_PAS='pa10'`: **157 / 157**, and
`make test-report-through-pa9`: 447 / 447. No open failures.

The turn started at 0 / 157. Failures were worked in five groups:

| Group | Shared behaviour | Count |
| --- | --- | --- |
| tree shape and dump payloads | node kinds, indentation, `TYPE:spelling` vs bare text | 131 |
| declaration vs statement | function-definition needs a parameter clause; brace initializers | 6 |
| lexical corners | `""_suffix` as one token, attributes after a parameter, `noexcept(e)` child, pack `...` in a declarator | 4 |
| relational vs template-id | value names are not template-names; member names after `.`/`->` are not either | 10 |
| qualified-name lookup | `ns::f` known as a value blocks the type and cast readings | 6 |

## Active Checkpoint

**Whole-stage checkpoint: the PA10 parser, name table and dump.** Complete.

- Owner: `dev/src/ast_*` as above; `dev/cppgm++.cpp` only routes the mode.
- Data flow: as above; no rule reads source text after tokenization.
- Expected complexity: `O(n)` in tokens for real sources; each rule is a
  constant number of alternatives over a memoized descent (below).
- Validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa10'`,
  `make test-report-through-pa9`,
  `perl scripts/cppgm_file_audit.pl --stage pa10 --paths dev/src`.

Two readings the checked-in refs settle and the handout does not; both are
implemented as stated and are the places to revisit if a later PA disagrees:

- A `template-argument` that neither the type-id nor the expression reading
  completes is read once more with its outermost `<` forced to the relational
  operator (`template_id_veto_depth_`). That is how `C<a, b < c>` closes.
- A non-type template parameter with no declarator **and** a keyword-only type
  keeps its default argument as the terminal (`literal TT_LITERAL:0`); with a
  named type it keeps an expression tree (`literal 0`). One ref each way.

## Performance Model

Measured with `dev/cppgm++ --emit-ast` on generated sources (templates, class
bodies, lambdas, casts, ambiguous declarations), release build:

| Input | Result |
| --- | --- |
| 10.4k lines / 297 KB | 0.08 s, 17 MB |
| 20.8k lines / 594 KB | 0.18 s, 29 MB |
| 41.6k lines / 1.19 MB | 0.38 s, 55 MB |

Linear in input, about 3.5 MB/s. Memory is the arena: nodes are owned by the
parse, not the tree, so an abandoned alternative costs nothing to drop but is
kept until the unit ends.

One scaling hazard was found and fixed. A `template-argument` is read first as
a type-id and then as an expression, and both descend into a nested
template-id, so `TC1<TC2<...<int>...>>` cost `2^N`: depth 16 took 0.21 s,
depth 22 took 13 s. `skip_simple_template_id` is now memoized on
(position, angle bracket state), which collapses the two descents into one:
depth 80 is now under 5 ms. The memo is dropped whenever a declaration adds a
name, which is the only thing that can change a position's answer.

Other nestings were measured and are flat: 40 nested parenthesized declarators,
60 nested compound statements, 30 nested parenthesized expressions, 30 nested
pointer declarators - all under 5 ms.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Token stream with spellings, AST model and dump, full recursive-descent parser for `pa10.gram`, declared-name table for the 6.8 and 14.2 ambiguities, memoized template-id descent | 157 / 157 pa10, 447 / 447 through pa9, file audit clean |
