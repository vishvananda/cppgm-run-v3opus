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
| template-ids, deduction, specializations | `sema_template.cpp` |
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
  gives a declaration, built once where the declaration is read, which is where
  the regions around it are known.
- `Value` — one analysed expression: type, value category, node, the overload
  set an unresolved name denotes, and whether it is a constant. It travels up
  from operand to operator, so no subtree is read twice, and each of the three
  conversions the output makes visible — the materialized temporary, the null
  pointer constant, the resolved overload set — rewrites the line the operand
  wrote, in the place it wrote it.

The class layer adds three more, each at the declaration that established it:

- `SemaEntity::region` — the region a declaration was made in, and
  `SemaEntity::object_member` — whether it is reached through an object of the
  class that made it, which 9.4p2 makes untrue of a `static` member. The name
  that uses it, the class layout and 5.3.1p3's pointer to member all ask that
  one fact rather than the region's kind.
- `SemaEntity::storage` — 9.5p1, the object an anonymous union's member is
  reached through, and `SemaEntity::constructor` — 12.1p5, the constructor a
  class has that no declaration wrote. Both are answers a use needs and the
  declaration already knew, so a use costs a load.
- `SemaAnalyzer::pending_` — the definitions the end of the translation unit
  writes: a synthesized constructor, a member function body 9.2p2 reads where
  its class is complete, and the declaration an instantiation stands for. Each
  is appended once, and the walk of the list lets a body it reads append
  another, so the list holds its elements still.

The template layer adds three, each split so the owner that already answers the
question answers this one too:

- `SemaEntity::template_parameters` — 14.1p1, the region a declaration's
  parameters were declared in, which is what says the declaration is a pattern
  rather than a function and what substitution binds arguments to. The name
  itself is declared by `declaring_region` in the region around that one, which
  is where 3.4 looks for it, so a template is an ordinary overload candidate.
- `SemaEntity::primary` and `SemaModel::specializations_` — 14.7.1p1, one
  declaration per template and argument list, interned under the argument list
  `TypeTable::type_list` gives an identifier to. A specialization is bound to no
  name: it is reached only from the template-id that wrote its arguments or the
  call that deduced them, so ordinary lookup never finds a declaration the
  program did not write.
- `TypeTable::substitute` — 14.3, a type rebuilt with its parameters replaced,
  memoized within one substitution so a shared subterm is rebuilt once and a
  type holding no parameter is itself.

## Current Failure Map

**170/170**, with no group open.  The four added this turn are the PA-local
regressions of the template layer, under `cppgm.tests/course/pa12/`.

| Group | Tests | State |
| --- | --- | --- |
| A. dump spine + declarations | ~25 | done |
| B. statements, conditions, jumps | ~20 | done |
| C. core expressions and operators | ~45 | done |
| D. calls, conversions, overloads | ~45 | done |
| E. diagnostics | ~20 | done |
| F. classes, members and anonymous unions | 4 | done |
| G. member-pointer types | 3 | done |
| H. function templates | 1 + 3 | done |
| I. `decltype(x)(1)` functional cast | 1 | done |
| J. an overloaded name written as it was found | 1 | done |

Refused rather than described, each named in its diagnostic and none of them a
fixture: instantiating a function template that has a definition (14.7.1 reads
the body again against the arguments and PA12 has no rule that does); a
template-id whose argument list is shorter than the template's parameter list,
which 14.8.1 leaves the rest of to deduction; a non-type template parameter,
which PA11 already declared nothing for; a class that declares a constructor,
destructor or conversion function
(12.1 chooses among them and PA12 has no rule that does); a call of a member
function (13.3.1.1.1, which the README also puts outside the slice); and an
object of an incomplete class.  One name resolution the class layer sits on is
PA11's and predates it: a class-head with a nested-name-specifier
(`struct N::C { };`) declares a second class rather than defining the one it
names, so the forward-declared one stays incomplete and an object of it is now
refused where it used to be accepted quietly.

## Active Checkpoint

**None open — PA12 passes in full (170/170).**  C5 closed the last group; what
a later assignment picks up from here is listed under Current Failure Map as
refused, and the three recorded divergences below.

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
| block-scope objects of class type constructed | 4000 | 0.03s |
| classes, each defined and constructed once | 8000 | 0.25s |
| member functions defined in one class | 8000 | 0.11s |
| members read through `this` in one body | 8000 | 0.13s |
| `&C::m` over the members of one class | 8000 | 0.15s |
| block-scope anonymous unions | 8000 | 0.31s |
| unnamed local classes | 2000 | 0.06s |
| member-pointer aliases and functions | 2000 | 0.05s |
| nested class definitions | depth 800 | 0.02s |
| nested `decltype(...)( )` casts | depth 640 | 0.06s |
| specializations of one template, each named once | 8000 | 0.33s |
| calls naming one specialization again | 8000 | 0.07s |
| templates of one name x calls of each | 600 x 600 | 0.08s |
| deduction and substitution through a pointer pattern | depth 800 | 0.00s |
| a template-id whose argument is a deep pointer type | depth 800 | 0.00s |

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
  declaration that says it has been asked for. Every case above is linear:
  each doubling costs about 2.2x, and no axis of the class layer is quadratic.
- A `decltype`-specifier written where a call's callee stands is skipped by a
  balanced token scan and then read once as the expression it holds, so nesting
  costs one scan per level rather than a parse per level.
- A specialization is interned under its template and its argument list, so
  naming one again is a probe: 8000 calls of one specialization substitute once
  and write one declaration.  Deduction is one structural walk of the pattern
  against the argument and costs no interning, so templates of one name against
  calls of each stays the same N x N shape the non-template row has, at about
  twice its constant.  Substitution memoizes within one call, so a type reached
  twice is rebuilt once and a type holding no parameter is returned as it is.

Three divergences from `cppgm++-ref` are recorded rather than matched, because
no fixture pins any of them: the order of several synthesized constructor
definitions, which we write in first-use order and the ref writes in an order
that is neither declaration nor use order; member function calls, which the ref
resolves and which the README puts outside the PA12 slice; and the literal type
the output gives the operand of `static_cast<T*>(0)`, which the ref writes as
the pointer and we write as the `int` it was.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | pa12 0 → 156/166; pa1–pa11 672/672; file audit clean |
| C1 audit | argument conversions in place, 13.4 asked wherever a value is discarded, `&f` resolved, 5.9 enumeration comparisons, indexed overload declaration | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05s → 0.26s; file audit clean |
| C2 | classes and members: member regions write no line, local class naming, anonymous-union object and injected members, 12.1p5 constructors and `constructor-action`, 9.3.1p3 object parameter and `this`, 5.2.5 member expressions, 4.4 reference binding | pa12 156 → 160/166; pa1–pa11 672/672; 4000 members 0.02s, 4000 constructed objects 0.07s; file audit clean |
| C3 | pointers to members: the `MemberPointer` category and one interning key per type, `C::*` declarators, the 8.3.5p7 cv-qualifier-seq, `&C::f`, and 14p1 templates writing no definition | pa12 160 → 164/166; pa1–pa11 672/672; 2000 member-pointer aliases 0.05s; file audit clean |
| C4 | `decltype(x)(1)`: 7.1.6.2 makes a decltype-specifier a simple-type-specifier, so 5.2.3 reads a call written on one as an explicit type conversion | pa12 164 → 165/166; pa1–pa11 672/672; file audit clean |
| C2–C4 audit | a pending list that holds its elements still, one fact for what is reached through an object, no line and no crash for a member declaration, `&C::x`, and a diagnostic where a declared constructor or an incomplete class was quietly accepted | pa12 165/166 held; pa1–pa11 672/672; valgrind clean; linear to 8000 members, classes, unions and `&C::m`; file audit passes with one header-weight warning |
| C5 | function templates: 14.1p1 declares the name in the region around its parameters, `TemplateId` and `TypeTable::substitute` make one specialization per argument list, 14.8.2.1 deduces one from a call, 13.3.3p1 prefers the declaration the program wrote, and an id-expression is written as the program spelled it | pa12 165 → 170/170 (4 tests added); pa1–pa11 672/672; valgrind clean; 8000 specializations 0.33s, 8000 reuses 0.07s, 600 x 600 templates x calls 0.08s; file audit passes with one header-weight warning |
