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
| types, and what a type is | `TypeTable` (`type_model.*`) |
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
  declaration order. A name binds the head, which holds the last link and is
  what `SemaModel::overloads_` indexes the chain by parameter type list under.
  Declaring is a probe and a link; overload resolution walks the chain, which is
  the O(candidates) 13.3 asks for. No per-lookup rescan.
- `SemaEntity::dump_name` and `Scope::prefix` — the qualified spelling the dump
  gives a function, built once where the declaration is read.
- `Value` — one analysed expression: type, value category, node, the overload
  set an unresolved name denotes, and whether it is a constant. It travels up
  from operand to operator, so no subtree is read twice, and each of the three
  conversions the output makes visible — the materialized temporary, the null
  pointer constant, the resolved overload set — rewrites the line the operand
  wrote, in the place it wrote it.

## Current Failure Map

**156/166**, held across the C1 audit. All ten open tests are class, member
pointer, template or PA10 parse work; none is a gap in the spine.

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

**C2 — classes in the PA12 subset (groups F and G).**

- Owner: `SemaAnalyzer` for the synthesized constructor and `member-expression`,
  `TypeTable` for the `MemberPointer` category and its spelling.
- 12.1 and 12.8: a class with no written constructor gets one where the class is
  completed, declared into the class region like any other member function, so
  the declaration layer stays the one place a function is declared.
- 5.2.5: a member expression names a member of the class the object expression
  has, which is a lookup in that class's region rather than a second name owner.
- Scope: the dump for a local class definition, `constructor-action`, the
  implicit `this` parameter, member access on unions and anonymous unions.

## Performance Model

Measured with `cppgm++ --emit-semantics` on synthesized inputs (this host):

| Case | Size | Time |
| --- | --- | --- |
| nested parenthesized binary expression | depth 500 | 0.00s |
| nested unbraced `if` substatements | depth 800 | 0.00s |
| block-scope declarations and initializers | 20000 statements | 0.24s |
| using-directives and unqualified lookups | 600 x 600 | 0.01s |
| calls of one function | 20000 | 0.13s |
| overloads of one name | 16000 | 0.26s |
| overloads of one name x calls | 600 x 600 | 0.04s |
| arguments converting to a reference parameter | 4000 | 0.02s |

- The walk is one visit per node; nothing is reparsed and no subtree is read
  twice, so depth costs stack rather than time. Depth itself is bounded by the
  PA10 parse guard, which refuses about 1000 levels.
- Lookup keeps the PA11 `declarers_` index and demand-gathered searchers.
  A block-scope using-directive is recorded in the namespace 7.3.4p2 puts its
  names in, so it adds no edge a block lookup has to walk. A call's callee name
  is looked up once, by the reader that had to know whether it named a type.
- Overloads are one chain per name per region, indexed by parameter type list
  for declaring and walked in order for resolving, so neither costs a rescan.
- Dump nodes are arena-allocated and linked once; every rewrite a conversion
  performs is O(1) on the node it already wrote, in the place it wrote it.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | pa12 0 → 156/166; pa1–pa11 672/672; file audit clean |
| C1 audit | argument conversions in place, 13.4 asked wherever a value is discarded, `&f` resolved, 5.9 enumeration comparisons, indexed overload declaration | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05s → 0.26s; file audit clean |
