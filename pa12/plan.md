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

The class layer adds three more, each at the declaration that established it:

- `SemaEntity::region` — the region a declaration was made in, which is what
  tells a data member from a variable without a second table of members.
- `SemaEntity::storage` — 9.5p1, the object an anonymous union's member is
  reached through, and `SemaEntity::constructor` — 12.1p5, the constructor a
  class has that no declaration wrote. Both are answers a use needs and the
  declaration already knew, so a use costs a load.
- `SemaAnalyzer::pending_` — the definitions the end of the translation unit
  writes: a synthesized constructor, and a member function body 9.2p2 reads
  where its class is complete. Each is appended once, and the walk of the list
  lets a body it reads append another.

## Current Failure Map

**160/166**. The class layer landed; every open test is member-pointer,
template or PA10 parse work.

| Group | Tests | State |
| --- | --- | --- |
| A. dump spine + declarations | ~25 | done |
| B. statements, conditions, jumps | ~20 | done |
| C. core expressions and operators | ~45 | done |
| D. calls, conversions, overloads | ~45 | done |
| E. diagnostics | ~20 | done |
| F. classes, members and anonymous unions | 4 | done |
| G. member-pointer types (`C::*`, `C::* ()const`) | 3 | open |
| H. templates (`&hello<stream>`, member template overload) | 2 | open |
| I. `decltype(x)(1)` functional cast (PA10 parse gap) | 1 | open |

Group G needs a `MemberPointer` type category in `TypeTable`, the `C::*`
ptr-operator in the declarator reader, and the cv-qualified function type
`function of () const returning T` the spelling of a member function pointer
uses.  Group H needs template-argument substitution deep enough to declare an
instantiation; one of its two tests also needs group G.

## Active Checkpoint

**C3 - member pointers (group G).**

- Owner: `TypeTable` for the `MemberPointer` category, its interning and its
  spelling; `SemaAnalyzer::apply_pointer` for the `C::*` ptr-operator.
- 8.3.3: a `nested-name-specifier *` in a declarator makes a pointer to member
  of the class that name reaches, which is one lookup where the operator is
  read.
- 5.3.1p3: `&C::f` is a pointer to member, which is what the qualified name of
  a member function under `&` denotes.

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
| members of one class | 4000 | 0.02s |
| block-scope objects of class type constructed | 4000 | 0.07s |
| unnamed local classes | 2000 | 0.06s |
| member-pointer aliases and functions | 2000 | 0.05s |
| member function declarations of one class | 2000 | 0.01s |

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
- A member is found by one probe in the region its class declares, and the
  constructor an object asks for is held on the class, so neither costs a
  search. A synthesized definition is appended once, under the flag on the
  declaration that says it has been asked for.

Two divergences from `cppgm++-ref` are recorded rather than matched, because no
fixture pins either: the order of several synthesized constructor definitions,
which we write in first-use order and the ref writes in an order that is
neither declaration nor use order, and member function calls, which the ref
resolves and which the README puts outside the PA12 slice.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | pa12 0 → 156/166; pa1–pa11 672/672; file audit clean |
| C1 audit | argument conversions in place, 13.4 asked wherever a value is discarded, `&f` resolved, 5.9 enumeration comparisons, indexed overload declaration | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05s → 0.26s; file audit clean |
| C2 | classes and members: member regions write no line, local class naming, anonymous-union object and injected members, 12.1p5 constructors and `constructor-action`, 9.3.1p3 object parameter and `this`, 5.2.5 member expressions, 4.4 reference binding | pa12 156 → 160/166; pa1–pa11 672/672; 4000 members 0.02s, 4000 constructed objects 0.07s; file audit clean |
| C3 | pointers to members: the `MemberPointer` category and one interning key per type, `C::*` declarators, the 8.3.5p7 cv-qualifier-seq, `&C::f`, and 14p1 templates writing no definition | pa12 160 → 164/166; pa1–pa11 672/672; 2000 member-pointer aliases 0.05s; file audit clean |