# PA12 Plan — `cppgm++ --emit-semantics`

## Stage Design

PA12 adds a third dump mode to the long-lived `cppgm++` frontend. It reuses the
PA10 syntax tree and the PA11 scope/type model unchanged and adds the layer
above them: expression typing, value categories, the standard-conversion subset,
and non-template overload resolution.

Owners:

| Fact | Owner |
| --- | --- |
| syntax | `AstNode` / `ast_parser*` (PA10) |
| scopes, bindings, lookup | `SemaModel` / `Scope` (`sema_scope.*`) |
| types | `TypeTable` (`type_model.*`) |
| declarations, declarators, constants | `SemaAnalyzer` (`sema_analyzer.*`, `sema_declarator.cpp`, `sema_constant.cpp`) |
| PA11 dump | `DumpScope` |
| PA12 dump | `DumpNode` (`sema_scope.*`) |
| statements | `sema_statement.cpp` |
| expressions, conversions, overloads | `sema_expression.cpp` |
| driver | `semantics_emit.*`, `dev/cppgm++.cpp` |

One analyzer serves both dumps: `SemaAnalyzer` takes a `SemaDialect`, so a
declaration is read once and written to whichever tree the mode asks for. That
keeps a single owner for "what does this declaration declare" rather than a
second declaration reader that has to be kept in step.

Two facts the PA11 model did not carry are added at their natural owner:

- `SemaEntity::next` — the declarations of one function name in one region, in
  declaration order. A name binds to the head; overload resolution walks the
  chain. O(1) to declare, O(candidates) to resolve, no per-lookup rescan.
- `SemaEntity::dump_name` and `Scope::prefix` — the qualified spelling the PA12
  dump uses, computed once when the declaration is read rather than by walking
  enclosing scopes at every use.

## Current Failure Map

Turn-start baseline: 0/166 (`--emit-semantics` exited `EXIT_NOT_IMPLEMENTED`).

| Group | Tests | Needs |
| --- | --- | --- |
| A. dump spine + declarations | ~25 | mode, `DumpNode`, qualified names, namespace/alias/variable/function nodes |
| B. statements | ~20 | compound/expression/return/if/switch/while/do/for/break/continue/case/default, condition scopes |
| C. core expressions | ~45 | literals, id-expressions, unary/binary/assignment/conditional/subscript/sizeof/cast |
| D. calls, conversions, overloads | ~45 | ICS subset, candidate collection, ranking, target-directed resolution |
| E. diagnostics | ~20 | arity, type, control-flow and redeclaration rejections |
| F. classes, member access, member pointers, templates | ~11 | later checkpoint |

Known PA10 parse gaps that PA12 tests reach (later checkpoint):
`unsigned long(e)` and `decltype(x)(1)` functional casts.

## Active Checkpoint

**C1 — the `--emit-semantics` spine (groups A–E).**

- Owner: `SemaAnalyzer` in `SemaDialect::Semantics`, with the expression layer
  in `sema_expression.cpp` and the statement layer in `sema_statement.cpp`.
- Data flow: source → PA10 `AstNode` → one source-order walk that declares into
  `Scope` and appends to `DumpNode` → `write_nodes`.
- Complexity: one visit per syntax node; expression analysis is bottom-up with
  no re-walks; overload resolution is O(candidates x arguments) over the
  declaration chain of one name.
- Validation: `make -C pa12 test`, then root `make test-report-through-pa11`
  and `make test-report ACTIVE_TEST_REPORT_PAS='pa12'`.

## Performance Model

- Lookup: unchanged from PA11 (`declarers_` index, demand-gathered searchers).
- Overloads: one chain per name per region, appended at declaration; resolution
  never rescans a scope.
- Dump: nodes are arena-allocated and linked once; no string is rebuilt.
- Qualified names: `Scope::prefix` is built once per namespace definition, so a
  declaration's dump name costs one concatenation rather than a walk.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| — | — | — |
