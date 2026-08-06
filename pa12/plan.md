# PA12 Plan — `cppgm++ --emit-semantics`

## Stage Design

PA12 adds a third dump mode to the long-lived `cppgm++` frontend. It reuses the
PA10 syntax tree and the PA11 scope/type model and adds the layer above them:
expression typing, value categories, the standard-conversion subset, and
non-template overload resolution.

Owners:

| Fact | Owner |
| --- | --- |
| syntax | `AstNode` / `ast_parser*` (PA10) |
| scopes, bindings, lookup | `SemaModel` / `Scope` (`sema_scope.*`) |
| types | `TypeTable` (`type_model.*`) |
| declarations, declarators, constants | `SemaAnalyzer` (`sema_analyzer.cpp`, `sema_declarator.cpp`, `sema_constant.cpp`) |
| PA11 dump | `DumpScope` |
| PA12 dump | `DumpNode` (`sema_scope.*`) |
| statements | `sema_statement.cpp` |
| expressions and operators | `sema_expression.cpp` |
| calls, conversions, overloads | `sema_overload.cpp` |
| driver | `semantics_emit.*`, `dev/cppgm++.cpp` |

One analyzer serves both dumps: `SemaAnalyzer` takes a `SemaDialect`, so a
declaration is read once and written to whichever tree the mode asks for. That
keeps a single owner for "what does this declaration declare" rather than a
second declaration reader that has to be kept in step.

Three facts the PA11 model did not carry are held at their owner:

- `SemaEntity::next` — the declarations of one function name in one region, in
  declaration order. A name binds the head; overload resolution walks the
  chain. O(1) to declare, O(candidates) to resolve, no per-lookup rescan.
- `SemaEntity::dump_name` and `Scope::prefix` — the qualified spelling the dump
  gives a function, built once where the declaration is read.
- `Value` — one analysed expression: type, value category, node, the overload
  set an unresolved name denotes, and whether it is a constant. It travels up
  from operand to operator, so no subtree is read twice and the one place a
  conversion is visible in the output rewrites the line the operand wrote.

## Current Failure Map

Turn-start baseline: 0/166. Now: **156/166**.

| Group | Tests | State |
| --- | --- | --- |
| A. dump spine + declarations | ~25 | done |
| B. statements, conditions, jumps | ~20 | done |
| C. core expressions and operators | ~45 | done |
| D. calls, conversions, overloads | ~45 | done |
| E. diagnostics | ~20 | done |
| F. classes and member access | 4 | open |
| G. member-pointer types | 3 | open |
| H. templates | 2 | open |
| I. `decltype(x)(1)` functional cast (PA10 parse gap) | 1 | open |

Group F needs implicit constructor synthesis (`constructor-action`, an implicit
`this` parameter, and a synthesized `__local_typeN::__local_typeN` definition)
plus `member-expression`. Group G needs a `MemberPointer` type category in
`TypeTable` and its `member-pointer of C to T` spelling. Group H needs
template-argument substitution deep enough to declare an instantiation.

## Active Checkpoint

**C1 — the `--emit-semantics` spine (groups A–E). Complete.**

- Owner: `SemaAnalyzer` in `SemaDialect::Semantics`, with the expression layer
  in `sema_expression.cpp`, calls and conversions in `sema_overload.cpp`, and
  statements in `sema_statement.cpp`.
- Data flow: source → PA10 `AstNode` → one source-order walk that declares into
  `Scope` and appends to `DumpNode` → `write_nodes`.
- Complexity: one visit per syntax node; expression analysis is bottom-up with
  no re-walks; overload resolution is O(candidates x arguments) over the
  declaration chain of one name.
- Validation: `make -C pa12 test`, root `make test-report-through-pa11`, root
  `make test-report ACTIVE_TEST_REPORT_PAS='pa12'`, and the file audit.

**C2 — next: classes in the PA12 subset (groups F and G).**

## Performance Model

Measured with `cppgm++ --emit-semantics` on synthesized inputs (this host):

| Case | Size | Time |
| --- | --- | --- |
| nested parenthesized binary expression | depth 400 | 0.00s |
| block-scope declarations and initializers | 20000 statements | 0.28s |
| using-directives and unqualified lookups | 600 x 600 | 0.01s |
| overloads of one name x calls | 300 x 300 | 0.01s |

- The walk is one visit per node; nothing is reparsed and no subtree is read
  twice, so depth costs stack rather than time.
- Lookup keeps the PA11 `declarers_` index and demand-gathered searchers.
  A block-scope using-directive is recorded in the namespace 7.3.4p2 puts its
  names in, so it adds no edge a block lookup has to walk.
- Overloads are one chain per name per region, appended at declaration;
  resolution never rescans a scope.
- Dump nodes are arena-allocated and linked once; the one rewrite a conversion
  performs is O(1) on the node it already wrote.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | pa12 0 → 156/166; pa1–pa11 672/672; file audit clean |
