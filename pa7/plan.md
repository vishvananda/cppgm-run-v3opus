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
| `Namespace` / `Entity` | `entity_model.*` | 3.3.6 declarative regions, 7.3 members, declaration-order lists, using-directive and inline-member edges |
| `TranslationUnitModel` | `entity_model.*` | namespace and entity storage, 3.4.1 and 3.4.3.2 lookup with the anchored-level cache |
| `DeclParser` | `decl_parser.*` | `pa7.gram` with semantic actions; owns the declarator tree only for as long as one declaration lasts |
| `ParseDepth` | `parse_depth.h` | how deep any re-entrant rule may descend, shared with PA6 |
| `build_sema_tokens` | `sema_token.*` | phase 7 token plus the two facts PA7 reads back: interned spelling and integral literal value |

Data flow: source file -> `Preprocessor`/`PostTokenizer` (PA1-PA5) ->
`SemaToken` array -> `DeclParser` -> `TranslationUnitModel` -> `write_namespace`.
Nothing flows back from the model into the token stream, so parsing is a single
forward pass with local backtracking only.

Four decisions drive the shape:

- **Lookup runs while parsing.**  Unlike PA6 the grammar alone cannot say what a
  name is: `simple-type-specifier` admits an identifier only when a scope says
  it is a typedef-name.  Lookup only reads, and a declaration enters the model
  only once its declarator has parsed, so the two places the grammar backtracks
  cost a cursor reset and nothing else.
- **The declarator is recorded as written, then walked once.**  8.3 derives a
  type from the outside in while a declarator is written from the inside out,
  so `DeclaratorNode` keeps the prefix and suffix operators per paren level and
  `build_type` makes one pass over them.  Deep nesting costs one pass, not one
  pass per level.
- **Two edge sets per namespace.**  `nominated()` is what a using-directive
  reaches, which 7.3.1p8 and 7.3.1.1p1 also write for an inline and for an
  unnamed member; `inlines()` is the inline members alone.  Unqualified lookup
  reads only the first, because for 3.4.1 an inline namespace *is* an implicit
  using-directive; qualified lookup reads both, because 3.4.3.2p2 asks what a
  namespace and its inline members declare before it asks any directive.
- **Bounded descent.**  Both re-entrant rules - a namespace body and a
  parenthesized declarator - open a `ParseDepth::Frame`, so one counter bounds
  the machine stack a file can ask for and a file that nests deeper is refused
  rather than crashed.  Everything else that could nest without bound (cv
  through array dimensions, the type description, the output walk) is a loop.

## Performance Model

Dominant operations, in the order they cost:

| Path | Shape | Complexity |
| --- | --- | --- |
| phases 1-7 | PA1-PA5 lexing, macro expansion, spelling interning | linear in bytes; ~90% of an ordinary run |
| declaration parsing | one forward pass; backtracking is one `(`-lookahead per declarator | linear in tokens |
| type construction | interned by structural key, so equality and redeclaration matching are integer compares | O(1) amortised per declarator node |
| unqualified lookup | asks the namespace itself first; only a name it does not declare needs the 3.4.1 search set | O(1), else O(L) over the search set |
| the 3.4.1 search set | scope chain plus the namespaces 7.3.4p2 anchors at each level, kept per namespace until a using-directive moves the epoch | O(C) per rebuild, C = namespaces reachable by directives |
| qualified lookup | 3.4.3.2p2 wave by wave, inline members before directives, revisits cut by a stamp | O(reached), stops at the first wave that has the name |
| output | one pass over the three member lists per namespace, on an explicit stack | linear in entities |

In the measured profile of 200k declarations the whole PA7 semantic layer -
parser, type table, entity model, output - is about 5%, and the rest is phases
1-7 and the C++ runtime they use, so on ordinary input the stage is front-end
bound.

The one super-linear shape is using-directives interleaved with lookups: a
directive genuinely changes what 3.4.1 searches, so a lookup after it must be
answered against a set that has grown.  16000 directives alternating with 16000
type-name lookups is 5.07 s; the same directives followed by the same lookups
is 2.24 s, because only the type-name lookups then miss the namespace they
start from.  Real translation units write tens of directives, not thousands.

Depth is bounded at 10000 open frames, shared with PA6.  Measured at that
limit, 10000 nested namespaces need 1.8 MB and 10000 nested parentheses in one
declarator need 3.3 MB of the 8 MB default stack.  Annex B recommends
supporting 256.

## Architecture Review

Three properties carry the design, and the audit checked each end to end:

- **One owner per fact.**  A spelling becomes a `NameId` once, in
  `sema_token.cpp`, and is text again only in `write_namespace`; no rule
  compares letters.  A type is built only through `TypeTable`, which is where
  8.3.2p6 reference collapsing, 8.3.4p1 cv-through-array and 8.3.5p5 parameter
  adjustment live, so every caller gets one answer to "what type is this".
  Table 10 of 7.1.6.2 is read off the counted specifiers in one function.  The
  descent bound lives only in `ParseDepth`.
- **Interface discipline.**  A parse routine that succeeds leaves the cursor one
  past its last token; one that fails leaves it where it found it, through
  `Mark`, and restores the `DeclaratorId` it was given.  The two grammar
  ambiguities are decided before descending rather than by trying and undoing:
  an `(` opens a parenthesized declarator unless what follows can start a
  parameter-declaration-clause (8.2p3), and an identifier is a
  `simple-type-specifier` only while the specifier sequence has no type yet.
- **Every unbounded nesting is either counted or a loop.**  The two re-entrant
  parse rules open a `Frame`.  `qualified`, `append_description` and
  `write_namespace` each walk a chain that a declarator or a namespace body can
  make as long as the file, and each is a loop over an explicit scratch.

## Final Architecture Review

The audit reconstructed the architecture from the sources rather than from the
checkpoint, and used three oracles the checkpoint did not:

- **Standard-driven probes.**  Hand-built witnesses for 3.4.3.2p2, 7.3.4p2/p4,
  7.3.1p8, 8.3.4p1 and 8.3.5p5, adjudicated against N3485 and cross-checked
  against `nsdecl-ref` and, where the two disagreed, against g++.
- **Differential generation.**  Two generators, one for declarator and type
  shapes and one for namespaces, using-directives, using-declarations, aliases
  and qualified names.  Every generated file was filtered for well-formedness
  with `g++ -fsyntax-only` before comparing, because an ill-formed program has
  undefined behaviour for PA7 and would compare nothing.  1347 well-formed
  programs - 246 declarator and type shapes, 1101 namespace and lookup shapes -
  compared byte-for-byte against `nsdecl-ref`.
- **Scaling and stack witnesses.**  Six shapes measured at three sizes each,
  plus a stack-limit bisection at the depth bound.

`nsdecl-ref` is **not** an oracle outside the checked-in fixtures.  It recovers
from errors, so it answers for programs the assignment leaves undefined, and it
crashes on inputs this tool handles: 5000 nested namespaces dump core.  It is
also wrong at least once where g++ and N3485 agree against it (F6).  Its
answers were used as evidence, never as the verdict.

One limitation remains, recorded rather than fixed: **the 3.4.1 search set is
rebuilt whenever any using-directive is written.**  Keeping it incrementally
would mean re-anchoring namespaces already in a kept list when a new directive
reaches them from a nearer scope, which is a larger change than the shape that
needs it justifies - a translation unit with thousands of using-directives.
The cheap half of it is done: a name the namespace itself declares never asks
the question, which is the common case and removes the rebuild from every
using-directive's own namespace-name lookup.

## Findings

| # | Finding | Verdict |
| --- | --- | --- |
| F1 | Qualified lookup treated an inline member as one more nominated namespace, so 3.4.3.2p2's rule that a namespace and its inline members are asked *before* any using-directive was lost to queue order.  `namespace A { namespace B { typedef double T; } using namespace B; inline namespace I { typedef int T; }; } A::T v;` described `v` as `double`; `nsdecl-ref` and 3.4.3.2p2 say `int`. | fixed |
| F2 | A namespace body was the one re-entrant parse rule that opened no `ParseDepth::Frame`, so nesting was bounded only by the machine stack: 50000 nested namespaces dumped core. | fixed |
| F3 | `TypeTable::qualified` recursed once per array dimension, so `typedef int A[2]...[2]; const A x;` with 100000 dimensions dumped core.  Array suffixes are a loop in the parser and are not depth-bounded, so the recursion had no bound either. | fixed |
| F4 | `TypeTable::append_description` recursed once per type-constructor level; only the tail call `-O3` happened to produce kept a 200000-deep pointer chain from overflowing. | fixed |
| F5 | `write_namespace` recursed once per namespace level, which was safe only because of a bound in another file that F2 shows did not exist. | fixed |
| F6 | `levels` marked the whole scope chain before walking, so a using-directive nominating an enclosing namespace could not bring that namespace's own directives in at the nearer anchor 7.3.4p4 gives them.  On the witness in `Final Architecture Review`, g++ resolves the name to `int` and the tool answered `long`.  (`nsdecl-ref` calls the witness ambiguous.) | fixed |
| F7 | `nominate` scanned everything a namespace already nominated to reject a repeat, which is O(n^2) in the directives written in one namespace. | fixed |
| F8 | `levels` allocated a fresh vector-of-vectors, one inner vector per scope-chain level, on every rebuild - and it rebuilds after every using-directive. | fixed |
| F9 | `lookup_unqualified` materialised the whole 3.4.1 search set even for a name the namespace it starts from declares, which no anchored namespace can beat.  That made every `using namespace N;` pay for a full closure rebuild just to resolve `N`. | fixed |
| F10 | `Entity::home` was written by every entity constructor and never read, and the header comment claimed it, rather than where the entity was declared, decided which member list the entity appears in. | fixed |
| L1 | The search set is rebuilt on every using-directive, so directives interleaved with lookups are O(directives x closure). | left, recorded above |

Two behaviours were checked and confirmed correct rather than changed: a
`bool` literal is refused as an array bound because `pa7.gram` spells
`constant-expression` as `TT_LITERAL` and `true` is `KW_TRUE`, not a literal
(`nsdecl-ref` accepts it); and a using-declaration adds a binding without
adding the entity to the namespace's member lists, which the checked-in `270`
fixture pins.

## Changes

- `entity_model.h/.cpp`: `Namespace::inlines()` beside `nominated()`, and
  `lookup_qualified` rewritten as 3.4.3.2p2's two waves - the inline closure of
  the wave, then what it declares, then the directives of the whole wave (F1).
  `levels` marks chain membership separately from reachedness, so a chain
  namespace reached through a directive is expanded at the inner anchor without
  being added twice (F6); its per-level buckets are scratch kept between walks
  (F8).  `nominate` keeps the directives written so far as `(where, space)`
  identifier pairs in one hash set (F7).  `lookup_unqualified` asks the
  namespace it starts from before asking what else is in scope (F9).
  `write_namespace` walks on an explicit stack, with the per-namespace header
  split out (F5).  `Entity::home` removed (F10).
- `type_model.h/.cpp`: `qualified` unwinds array dimensions onto a scratch and
  rebuilds them (F3); `append_description` is a loop over the type chain, with
  only a function's parameters branching (F4).
- `decl_parser.cpp`: `parse_namespace_definition` opens a `ParseDepth::Frame`
  (F2).
- `cppgm.tests/course/pa7/280-inline-namespace-lookup-order.*`: regression test
  for F1, covering both the inline set beating a using-directive and the inline
  set of a namespace a directive reaches, with the fixture regenerated through
  `make -C pa7 ref-test`.  It fails against the pre-audit binary.

## Performance Evidence

| Workload | Before | After |
| --- | --- | --- |
| 200k declarations, 2.8 MB | 0.69 s / 63 MB | 0.66 s / 62 MB |
| 2000 nested namespaces, 2000-segment qualified ids, 25 MB | 2.24 s / 193 MB | 2.23 s / 193 MB |
| 100k namespaces + 100k qualified declarator-ids, 5.8 MB | 0.92 s / 99 MB | 0.89 s / 102 MB |
| 2000-deep using-directive chain + 2000 lookups | 0.01 s / 5.7 MB | 0.02 s / 5.9 MB |
| 16000 using-directives then 16000 lookups | 5.15 s / 22 MB | 2.24 s / 24 MB |
| 16000 using-directives alternating with 16000 lookups | 5.79 s / 22 MB | 5.07 s / 23 MB |
| 100000 array dimensions through a cv-qualified typedef | segfault | 0.21 s / 33 MB |
| 50000 nested namespaces | segfault | refused |

The whole PA7 suite, 42 tests, runs in 0.33 s.

Scaling is linear in the shapes that matter: 200k declarations, 100k qualified
declarator-ids and 4M-token qualified name chains are all linear in input
bytes, and the profile of the first puts the PA7 semantic layer at about 5%
against phases 1-7.  The alternating-directive shape remains quadratic and is
recorded as L1; the same directives without interleaving are now 2.3x faster
because the lookup that resolves each directive's own namespace-name no longer
rebuilds a closure.

Depth: at the 10000-frame limit, nested namespaces use 1.8 MB and nested
declarator parentheses 3.3 MB of the 8 MB default stack, bisected with
`ulimit -s`.  Inputs past the limit are refused; before, two shapes dumped
core, and one of them (nested namespaces) dumps core in `nsdecl-ref` at 5000.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src`: pass.
- `make test-report-through-pa7`: 361/361, PA1 through PA7.
- `make -C pa7 test`: 25 local + 17 course tests.
- 1347 generated programs, filtered to well-formed with `g++ -fsyntax-only`,
  compared byte-for-byte against `nsdecl-ref`: no difference in output or exit
  status.
- Deep-nesting witnesses in four shapes (namespaces, parenthesized declarators,
  array dimensions, pointer chains): no crash, stack bisected at the bound.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `nsdecl` full stage: token stream, type model, entity model, lookup, `pa7.gram` parser, output | 41/41 PA7 tests pass; PA1-PA7 report clean |
| C2 | PA-wide audit: 3.4.3.2p2 inline-namespace set and 7.3.4p4 transitivity (F1, F6), three unbounded recursions and one uncounted descent (F2-F5), and the lookup path made demand-driven and allocation-free (F7-F9), plus dead ownership removed (F10) | 361/361 through pa7; file audit clean, no warnings; 16000 directives + lookups 2.3x faster; two segfault shapes now bounded; 1347 generated programs match the reference |

## Active Checkpoint

None: PA7 is complete and audited.  The next work on this stage is whatever a
later PA needs from the object model - class scope, linkage across translation
units, and overload sets, all of which land in `entity_model`, and which is
where L1 should be revisited if a real translation unit ever makes it matter.
