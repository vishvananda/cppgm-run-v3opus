# PA10 Plan - `cppgm++ --emit-ast`

## Stage Design

PA10 replaces the PA6 recognizer boundary with a tree-building parser for the
shared source grammar (`pa10.gram`) and a deterministic line dump of the tree.

Owners, all under `dev/src`:

| Owner | Responsibility |
| --- | --- |
| `ast_tokens.{h,cpp}` | phases 1-7 into a flat terminal array with interned spellings; `flatten(begin,end)` spells a token span back out; `string_value` hands back a string literal's decoded characters |
| `ast_names.h` + part of `ast_parser_name.cpp` | `DeclaredNames`: every name fact, by scope and by qualified spelling, with the scope and prefix guards that open and close them |
| `ast_model.{h,cpp}` | `AstKind`, `AstNode`, `AstArena`, `write_ast` |
| `ast_parser.h` + 6 `.cpp` | recursive descent with ordered choice, full backtracking and a depth-bounded stack |
| `ast_emit.{h,cpp}` | driver: per translation unit, tokens -> parser -> dump |
| `dev/cppgm++.cpp` | `--emit-ast -o out src...` argument surface |

Data flow: `AstTokenStream` -> `AstParser` (cursor + `DeclaredNames`) ->
`AstNode` tree in an `AstArena` -> `write_ast`. No rule reads source text after
tokenization: a rule that needs a name spells the token span it matched, and
the one rule whose payload is a literal's value reads the value phase 7
decoded.

Four facts the parser keeps as typed state, because the grammar leaves them
open and later PAs must not re-derive them from source text:

1. **Spelled names.** A template-id, qualified name, decltype-specifier,
   placement clause or lambda introducer is dumped as it was written. Rules
   record the token span they matched; `AstTokenStream::flatten` writes it back,
   inserting a separator exactly where the pair would otherwise read back as
   some other token. Two spellings close up on purpose: `>` and `>` (14.2.3
   splits `>>` into two terminals, and a name that closes two template-ids is
   spelled `>>`), and `<` before `::` (2.5.3 keeps the digraph `<:` from
   forming there).
2. **Name kinds.** `foo(x);` is a declaration when `foo` names a type and a
   call when it names an object; `a < b > c` is a template-id when `a` names a
   template. `DeclaredNames` answers both. It answers a qualified name the same
   way, by spelling, since PA10 models no scope to look into: a member declared
   under the prefix `ns::` is remembered as `ns::f` as well as `f`.
3. **Bracket state.** `angle_` says whether the innermost open pair is `<>`, so
   `>` closes it instead of comparing (14.2.3). `BracketGuard` restores it when
   the rule that opened the pair leaves, however it leaves.
4. **Descent depth.** `ParseDepth`, shared with the PA6 recognizer. Every rule
   that can re-enter itself opens a frame, so a file that nests deeper than the
   machine stack allows is refused with `EXIT_FAILURE` rather than crashing.

Two readings the checked-in refs settle and the handout does not; both are
implemented as stated and are the places to revisit if a later PA disagrees:

- A `template-argument` that neither the type-id nor the expression reading
  completes is read once more with its outermost `<` forced to the relational
  operator (`template_id_veto_depth_`). That is how `C<a, b < c>` closes.
- A non-type template parameter with no declarator **and** a keyword-only type
  keeps its default argument as the terminal (`literal TT_LITERAL:0`); with a
  named type it keeps an expression tree (`literal 0`). One ref each way.

One construct is in the refs and not in `pa10.gram`: `linkage-specification`.
The refs are the grading oracle, so it is parsed and dumped.

## Performance Model

Dominant operation: one ordered-choice descent per token, with three rules that
read the same span more than once - a template-argument (type-id, expression,
vetoed expression), a parenthesized operand (type-id, expression), and a
declaration (function definition, then simple declaration, then in a class a
bit-field). Every re-reading either bottoms out in a memoized
`simple-template-id` or covers a span with no nested re-reading in it, so the
expected cost is `O(n)` in tokens for real sources.

Release build, `dev/cppgm++ --emit-ast`, generated sources with templates,
class bodies, lambdas, casts, bit-fields, enums, try blocks and ambiguous
declarations:

| Input | Time | Throughput | Peak RSS |
| --- | --- | --- | --- |
| 2.5k lines / 87 KB | 0.033 s | 2.5 MB/s | 8 MB |
| 10k lines / 350 KB | 0.112 s | 3.0 MB/s | 19 MB |
| 40k lines / 1.41 MB | 0.459 s | 2.9 MB/s | 66 MB |
| 80k lines / 2.82 MB | 0.899 s | 3.0 MB/s | 128 MB |

Linear in input over a 32x range. Memory is the arena, and the arena is mostly
the tree the assignment asks for: the 1.41 MB input builds 652,823 nodes of
which 505,620 are dumped, so abandoned alternatives are 23% of it, not a
multiple of it. Nodes are owned by the parse rather than by the tree, so an
abandoned alternative costs nothing to drop but is kept until the unit ends.

Two costs are super-linear in *nesting depth* rather than in file size, and
both are bounded by the depth limit:

- Spelling nested names. Each nested template-argument type-id spells its own
  span, so `TC<TC<...<int>...>>` is quadratic in depth: 400 deep is 9 ms, 1000
  deep is 36 ms, 2000 deep is 126 ms. The depth limit refuses about 5000.
- Stack. The deepest accepted input on each nesting axis completes within 4 MB
  of the 8 MB default stack (measured under `ulimit -s`); one level deeper is
  refused.

## Architecture Review

Whole-stage checkpoint (checkpoint 1): the PA10 parser, name table and dump.
Owner `dev/src/ast_*`; `dev/cppgm++.cpp` only routes the mode. Reached 157/157
pa10 and 447/447 through pa9 with a clean file audit.

## Final Architecture Review

The stage was reconstructed from the handout, `pa10.gram`, the stage commit and
the sources, and then measured rather than reasoned about. The tree, the name
table and the dump hold up: one owner per concern, no rule reads source text
after tokenization, and the arena's ownership rule is sound. What the earlier
checkpoint had not established was the cost of the ordered choice and the
completeness of the memo's invalidation. Both were wrong, in ways only a
scaling measurement finds.

Six blockers were found and fixed; details in Findings. After them:

- The two re-reading points that nest - a template-argument and a class member
  - are `O(depth)` rather than `2^depth`.
- The one thing that can change a memoized answer, the name table, is the one
  thing that invalidates the memo.
- Every name fact has a single owner, `DeclaredNames`, including the qualified
  spellings and the prefix they are recorded against.
- A spelled name reads back as the tokens it was written as.
- A file that nests too deep is refused, as the PA6 recognizer already refused
  it.

One boundary is deliberate and documented rather than closed: the value veto on
a `<` applies to unqualified names only. `n::w < 1 > 2;` is refused where
`w < 1 > 2;` parses. The README puts "semantic resolution of `template-id`
versus `<`" out of scope, no ref exercises the qualified form outside a
template-argument, and inside one the vetoed reading already covers it. Closing
it needs the nested-name-specifier prefix threaded into the memo key, which is
the natural shape once PA11 has real lookup.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | Token stream with spellings, AST model and dump, full recursive-descent parser for `pa10.gram`, declared-name table for the 6.8 and 14.2 ambiguities, memoized template-id descent | 157 / 157 pa10, 447 / 447 through pa9, file audit clean |
| 2 | Final audit: two exponential blowups, memo invalidation, name-fact ownership, spelling round-trip, literal-value recovery, descent depth | 604 / 604 through pa10, file audit clean with no warnings |

## Findings

| # | Finding | Evidence |
| --- | --- | --- |
| 1 | A template-argument's vetoed reading turned the template-id memo off for its whole subtree, so a nested template-id under one cost `2^N` | 267 bytes of source took 27 s; +2 depth was 4x |
| 2 | A class member was parsed twice, once as a declaration and once as a bit-field, and both readings descended into a nested class body, so `N` nested classes cost `2^N` | 386 bytes of source timed out at 30 s; +4 depth was 16x |
| 3 | The memo was dropped only by `declare_name`, but four rules declared through `DeclaredNames` directly and every scope pop removed names without dropping it, so a remembered answer could outlive the names it was read against | Read from the sources; `declare_parameters`, `parse_exception_declaration`, `parse_type_parameter` and `parse_non_type_template_parameter` all bypassed it |
| 4 | `flatten` separated two tokens only when both were words, so a spelled name could read back as different tokens | `decltype(a / *p)` was dumped as `decltype(a/*p)`, which reads back as a comment; `decltype(a + +a)` as `decltype(a++a)` |
| 5 | The language of a `linkage-specification` was recovered by taking the quotes off the literal's source spelling rather than reading the value phase 7 had already decoded | `extern R"(C)"` was dumped as `linkage-specification "(C)` |
| 6 | The parser dropped the `ParseDepth` guard the PA6 recognizer it replaces still carries, so deep nesting crashed instead of exiting `EXIT_FAILURE` | 4000 nested parentheses segfaulted; `recog` refuses the same file |
| 7 | A lambda's parameters were not declared in its body, so the same parameter read one way in a function and another in a lambda | `void f(int x) { x<1>(2); }` compared, `[](int x) { x<1>(2); }` called through a template-id |

## Changes

- `ast_parser_name.cpp`, `ast_parser.h`: the template-id memo key carries
  everything besides the position that the answer turns on - the bracket state,
  whether the veto applies at this bracket depth, and whether the name is
  qualified - so the vetoed reading is memoized like the others (1). The memo
  is dropped when `DeclaredNames::version()` moves, and an answer reached after
  the depth limit or after the names changed under it is not remembered (3, 6).
- `ast_parser.cpp`, `ast_parser_class.cpp`: a bit-field is read against the
  decl-specifier-seq the declaration already read, so the specifiers - which
  may hold a whole class definition - are read once per member (2).
- `ast_names.h`, `ast_parser_name.cpp`: `DeclaredNames` owns every name fact -
  the scopes, the qualified spellings, the prefix they are recorded against,
  and the scope and prefix guards - so `AstParser::declare_name`,
  `kind_of_name`, `qualified_`, `prefix_`, `ScopeGuard` and `PrefixGuard` are
  gone from the parser (3).
- `ast_tokens.cpp`: `flatten` separates two tokens whenever phase 3 would munch
  them into one, against the shared punctuator table plus the comment
  introducers, with the two closings 14.2.3 and 2.5.3 call for (4).
- `ast_tokens.{h,cpp}`, `ast_parser.cpp`: `AstTokenStream::string_value` hands
  back a narrow string literal's decoded characters, and the
  linkage-specification rule reads that instead of the spelling (5).
- `ast_parser*.cpp`: a `ParseDepth::Frame` in each of the nine rules that can
  re-enter itself, and `run()` refuses a descent that overflowed (6).
- `ast_parser_expression.cpp`: a lambda declares its parameters into a scope
  around its body (7).
- `ast_model.h`: `AstArena::size()` had no caller; removed.

## Performance Evidence

Blowups, before and after, same inputs:

| Workload | Before | After |
| --- | --- | --- |
| vetoed template-argument over a nested template-id, depth 16 / 20 / 24 | 0.11 s / 1.9 s / 27 s | 3 ms / 3 ms / 4 ms |
| nested classes with a failing innermost member, depth 16 / 20 / 24 | 0.17 s / 2.8 s / >30 s | 3 ms / 3 ms / 3 ms |
| both, depth 80 | not reachable | 4 ms / 3 ms |

Nesting sweep: 24 nesting shapes (classes, namespaces, templates, member
functions, constructors, blocks, lambdas, `if`, `for`, `try`, parentheses,
calls, `sizeof`, `typeid`, template arguments, braced lists, casts, `new`,
default arguments, abstract declarators, subscripts, conditionals, template
parameters), each at depth 8 to 128, and all 121 pairwise alternations of 11
expression wrappers: every one flat, worst 9 ms.

Depth limit, deepest accepted before refusal, and the stack it used:

| Axis | Deepest accepted | Fits in |
| --- | --- | --- |
| parenthesized expression, nested `sizeof` | 713 | 2 MB |
| nested classes, namespaces, blocks, template-ids | ~5000 | 4 MB |
| nested declarators, braced lists, unary operators | ~9990 | 4 MB |

One level deeper is refused with `EXIT_FAILURE` on every axis; none crashes.

Throughput and memory are in the Performance Model above.

## Validation

- `make test-report-through-pa10`: 604 / 604, all ten stages.
- `perl scripts/cppgm_file_audit.pl --stage pa10 --paths dev/src`: passed, 91
  files, no warnings.
- Spelling round-trip, digraph and angle-close cases checked by hand against
  the refs' spellings: `TC<::N::T,::N::T>`, `W<W<W<int> > >` and
  `W<W<W<int>>>` both closing up, `decltype(p1% ::p1)`, `decltype(p1<::p1)`.
