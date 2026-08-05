# PA7 (`nsdecl`) Plan

## Stage Design

`nsdecl` runs translation phases 1-7 over each source file and describes the
namespace-scope entities the translation unit declares.  The pipeline reuses
PA1-PA5 unchanged and replaces the PA6 boolean recognizer with a semantic
parser for the much smaller `pa7.gram`, because PA7 needs an object model and
name lookup rather than a yes/no verdict.

Owners, innermost to outermost:

| Owner | File | Typed fact it keeps |
| --- | --- | --- |
| `NameTable` | `name_table.h` | identifier spelling <-> `NameId`, so scopes key on a word |
| `TypeTable` | `type_model.*` | every distinct type of a run, interned; 8.3 type building and the PA2 canonical description |
| `Namespace` / `Entity` | `entity_model.*` | 3.3.6 declarative regions, 7.3 members, declaration-order lists, using-directive edges |
| `TranslationUnitModel` | `entity_model.*` | namespace and entity storage, 3.4.1 and 3.4.3.2 lookup with the anchored-level cache |
| `DeclParser` | `decl_parser.*` | `pa7.gram` with semantic actions; owns the declarator tree only for as long as one declarator lasts |
| `build_sema_tokens` | `sema_token.*` | phase 7 token plus the two facts PA7 reads back: interned spelling and integral literal value |

Data flow: source file -> `Preprocessor`/`PostTokenizer` (PA1-PA5) ->
`SemaToken` array -> `DeclParser` -> `TranslationUnitModel` -> `write_namespace`.

## Current Failure Map

All 41 PA7 tests failed at turn start (the handout stub throws
`NotImplementedException`).  Grouped by the compiler behaviour each needs:

| Group | Tests | Behaviour |
| --- | --- | --- |
| namespaces | 100, 110, 120, 190, 230, 240, 280 | definition, reopening, unnamed, inline, output order |
| specifiers | 130, 150, 340 | 7.1.6.2 Table 10 canonical types, cv, typedef-name lookup |
| declarators | 140, 300, 310, 320, 330, 350, 360, 370, 600-deep | 8.3 meaning of declarators, 8.3.5p5 parameter adjustment, reference collapsing |
| lookup | 150, 190, 200, 220, 260, 270, 290, 600-chain | 3.4.1 unqualified with 7.3.4 anchoring, 3.4.3.2 qualified, filters |
| errors | 280-inline-namespace-reopen-bad | 7.3.1p8 rejected as `EXIT_FAILURE` |

Every group is one tool with one object model, so they are not separable into
independently shippable increments; the checkpoint is the whole tool.

## Active Checkpoint

**C1 - `nsdecl` full stage.**

- Owner: `DeclParser` drives; `TypeTable` owns types, `TranslationUnitModel`
  owns scopes and lookup.
- Data flow: as above.  Nothing flows back from the model into the token
  stream, so parsing is a single forward pass with local backtracking only.
- Expected complexity: `O(tokens)` parsing apart from the two bounded
  declarator retries; lookup is `O(levels)` per name with the anchored level
  list cached per namespace and invalidated by a using-directive epoch;
  type construction is `O(1)` amortised per node through interning.
- Validation: `make -C pa7 test`, then root
  `make test-report ACTIVE_TEST_REPORT_PAS='pa7'` and
  `make test-report-through-pa7`.

## Performance Model

| Path | Shape | Measurement |
| --- | --- | --- |
| declaration parsing | one forward pass; backtracking is bounded by one `(`-lookahead per declarator | 200k array-of-pointer declarations: 0.61 s, 63 MB |
| declarator descent | one arena `DeclaratorNode` per paren level, walked once, no reparse | 4000 nested parens: 0.00 s, 5.7 MB |
| unqualified lookup | anchored level list built once per namespace and reused until a using-directive moves the epoch | 2000-deep using-directive chain, 2000 lookups: 0.01 s, 5.7 MB |
| qualified lookup | 3.4.3.2 breadth-first walk, revisits cut by a stamp on the namespace | covered by the chain case above |
| type identity | interned by structural key, so equality and redeclaration matching are integer compares | whole PA7 suite: 41 tests in 0.5 s |

Known worst case: using-directives interleaved with lookups defeat the level
cache, because 7.3.4p2 anchoring genuinely changes with every directive - 4000
alternating directives and lookups take 1.48 s.  The first cut of the cache
also kept every stale level list, which made that case cost 331 MB; releasing
a list when it stops being true brought it to 10 MB and left the cached path
unchanged.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `nsdecl` full stage: token stream, type model, entity model, lookup, `pa7.gram` parser, output | 41/41 PA7 tests pass; PA1-PA7 report clean |
