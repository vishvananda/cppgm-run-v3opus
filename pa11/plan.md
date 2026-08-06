# PA11 Plan - `cppgm++ --emit-types`

## Stage Design

PA11 adds the first semantic layer on top of the PA10 syntax boundary: scopes,
declarations, bindings, canonical types, and a deterministic dump of all four.

Owners, all under `dev/src`:

| Owner | Responsibility |
| --- | --- |
| `type_model.{h,cpp}` | canonical types, extended for PA11 with the user-defined categories - class, enumeration and template parameter, each interned by the entity that declared it - and now the single owner of Table 10 of 7.1.6.2 |
| `sema_name.{h,cpp}` | `QualifiedName`: the components of a name PA10 handed on as one spelling, and whether the last of them is a template-id |
| `sema_scope.{h,cpp}` | `SemaModel`: entity, scope and dump arenas, the binding table, the name-to-regions index, the using-directive reachability index, 3.4 unqualified and qualified lookup |
| `sema_analyzer.{h,cpp}` | the AST walk: namespaces, classes, enumerations, templates, using-declarations, functions, statements, and every dump line |
| `sema_declarator.cpp` | decl-specifier-seq and declarator to `TypeId` and name, parameter clauses, elaborated specifiers, nested-name-specifier resolution |
| `sema_constant.cpp` | the 5.19 subset: literals, enumerators, const objects, `sizeof`/`alignof`, casts, short-circuit operators, overflow, and `decltype` |
| `types_emit.{h,cpp}` | driver: one `SemaAnalyzer` per translation unit |
| `ast_emit.{h,cpp}` | the per-unit driver both dump modes share: phases 1-7, parse, wrapper lines |

Data flow: `AstTokenStream` -> `AstParser` (PA10) -> `AstNode` tree ->
`SemaAnalyzer` -> `SemaModel` (scopes, entities, types) -> dump tree -> output.
No pass re-reads source text: a name arrives as the spelling PA10 recorded, a
literal arrives as its spelling and is decoded once by the shared `literal_scan`
tokenizer, and a parameter clause is read once for the type it builds and the
names it binds.

One owner per fact:

- **A name's components.** PA10 spells a qualified-id from the terminals it
  matched, because that is what its dump names. `QualifiedName` is the only
  place that reads that spelling back as components, so "the name this
  declaration introduces", "is this name qualified", "which region does its
  prefix reach" and "is this a template-id" are one answer rather than four
  string probes with four different ideas of what a component may hold.
- **A type.** `TypeTable`, interned, so two declarations agree exactly when
  their type identifiers do.
- **A region.** `Scope`. The dump tree is deliberately separate, because its
  shape is not the scope tree's (below).
- **Whether a lookup reaches a declaration.** `SemaModel`, from the two indexes
  described under Performance Model; no caller walks scopes itself.

Four readings the refs settle that the handout leaves open, implemented as
stated and the first places to revisit if PA12 disagrees:

1. **A binding line is written where the declaration is, and spelled as the
   declaration spells it.** `struct C; class C {};` writes two `type C` lines,
   one per class-key. A *use* writes the entity's canonical spelling instead,
   so an enumeration defined out of class writes `type writer::state enum class
   writer::state` while a function returning it writes `enum class state`.
2. **A scope node per declaration, except a namespace.** A reopened namespace
   continues its own node; an enumeration writes one node per declaration, so
   an opaque member declaration and its out-of-class definition are two. An
   unscoped enumeration writes no node at all: 7.2p10 puts its enumerators in
   the region around it, and so does the dump.
3. **Bindings first, then child scopes**, each in declaration order.
4. **An unnamed class or enumeration is named by the first declarator of its
   declaration, before its body is read**, so every line the body writes spells
   it the way the program will. With no declarator, a class is named
   `__anonymous_<key>_type__<first>_<past-last>` after the token span of the
   declaration - the convention the PA12 and PA17 refs also use - and an
   enumeration is numbered `__anonymous_enum<n>`.

Two PA10 parser gaps PA11 needed and closed: an `enum-specifier` may name a
member enumeration with a nested-name-specifier (7.2p1), and an
opaque-enum-declaration may fix its underlying type with an enum-base (7.2p2).
`AstNode` now also carries the token span of each declaration, which is the one
fact about an anonymous union that is not a name.

## Performance Model

Dominant operation: one pre-order walk of the PA10 tree, with a lookup per name
written and an interning probe per type built. Nothing is deferred and no
construct is visited twice - a function definition's parameter clause included,
which `declarator_type` reads once and hands to the region it opens.

A lookup is the only operation that is not obviously constant, because 7.3.4p2
makes the declarations of a nominated namespace appear where the directive is,
and 3.4p1 makes a lookup that reaches two declarations of one name an error, so
a lookup cannot in general stop at its first hit. Two indexes make it constant
in the shapes that matter:

- `declarers_` maps a name to the regions that bind it, each once. A name no
  region declares is answered without touching the region chain. A name exactly
  one region declares - almost every name in almost every unit - has one
  possible answer, so the lookup asks only whether the declaring region is
  reached, and asks the cheap way first: is it one of the regions enclosing the
  lookup.
- `Scope::searchers` is the using-directive closure read from the region that
  *declares* rather than the region that asks: the regions a lookup written in
  which reaches this one. That way round is what makes it worth keeping, because
  a name is declared in one region and looked up from many, so one gathering
  answers every lookup of it. It is gathered only when a directive has to answer
  a lookup, and it grows with the directives rather than being gathered again:
  a directive written later costs the gathering the edges it added and nothing
  for the rest, and a run of directives longer than the set itself makes it
  gather from nothing instead, so a gathering never costs more than deriving the
  answer once.

For a name several regions declare, 3.4p1 needs the whole intersection of the
regions that declare it with the regions the lookup reaches. There are two ways
to compute it and they are lopsided in opposite directions, so the walk of the
regions reached runs on a budget of the number of regions that declare the name
and gives up when it is the larger, which makes the lookup cost the smaller of
the two.

Release build, `dev/cppgm++ --emit-types`, generated sources of namespaces,
classes, member enumerations, typedefs, arrays with constant bounds,
`static_assert`, `sizeof`, `decltype`, qualified names and one using-directive
per namespace:

| Input | Parse | Analyse | Total | Peak RSS |
| --- | --- | --- | --- | --- |
| 49 KB / 200 namespaces | 0.02 s | 0.00 s | 0.02 s | 11 MB |
| 199 KB / 800 | 0.07 s | 0.02 s | 0.08 s | 21 MB |
| 832 KB / 3200 | 0.30 s | 0.09 s | 0.39 s | 75 MB |
| 3.3 MB / 12800 | 1.34 s | 0.48 s | 1.82 s | 292 MB |
| 6.8 MB / 25600 | 2.87 s | 1.08 s | 3.95 s | 581 MB |

Linear over a 128x range; analysis is about a third of the parse it follows.
Memory is the PA10 arena plus the model and the dump; the dump is held rather
than streamed because a scope's lines keep growing after its children are added.

Six using-directive shapes, each generated so that one of the two search
directions is far more expensive than the other. All were superlinear before
the audit's `lookup` rewrite except the last two, which were already linear and
had to stay so:

| Workload | Before | After |
| --- | --- | --- |
| 4000 directives in one region, 4000 lookups of a name two regions declare | 0.78 s | 0.02 s |
| the same at 16000 / 16000 | 19.15 s | 0.14 s |
| 8000 directives, 8000 lookups of a name reached through the last of them | 4.92 s | 0.06 s |
| 8000 of those directives written one between each pair of lookups | 1.79 s | 0.06 s |
| 4000 directives in one namespace, 4000 namespaces nominating it and looking through it | 0.77 s | 0.04 s |
| 6400 namespaces, one directive each, looking up their own names | 0.054 s | 0.083 s |
| 20000 namespaces chained by directives, one lookup down the chain | 0.07 s | 0.07 s |

The one that got slower is the shape whose lookups are all answered by the
region they are written in; it pays one compare per enclosing region where it
used to pay one probe, which is 15 ms on 854 KB of source, or 4% of the run.

Nesting sweep, 11 shapes (namespaces, classes, blocks, parenthesized constant
expressions, nested declarators, array dimensions, pointer chains, function
typedef chains, qualified-name chains, template-parameter scopes,
using-directive chains) at depths 1024 to 20000. The parser's depth guard
refuses at about 4900 levels of namespace, class or block nesting and shallower
for a parenthesized expression; the shapes that cost it no frame per level run
to 20000 and beyond. Analysis time is flat or linear in depth everywhere, and
the superlinear part of the deep shapes is the PA10 parser's documented
name-spelling cost, not the analysis:

| Shape at its deepest accepted level | Parse | Analyse |
| --- | --- | --- |
| 4900 nested namespaces | 125 ms | 15 ms |
| 4900 nested classes | 147 ms | 29 ms |
| 4900 nested blocks | 31 ms | 28 ms |
| a 4900-component qualified name | 127 ms | 17 ms |
| 9000 nested declarators | 1636 ms | flat |
| 9000 template-parameter scopes | 752 ms | flat |
| 20000-long typedef chain | 122 ms | 11 ms |
| 20000-long using-directive chain | 90 ms | 67 ms |

Stack: every input the parser accepts, the analysis also completes; every input
it refuses is refused before the analysis runs, with an exit status and no
crash, on every axis at every depth measured.

## Architecture Review

The stage was reviewed end to end against the assignment and the refs, in the
order a fact travels: parse, name, region, type, value, dump.

- **Spelling to structure.** PA10 hands a qualified name on as one string
  because its own dump names it that way. Four places used to read that string
  back with `find("::")`, `rfind("::")`, a split into components, and a
  `find('<')` that took any `<` for a template-id and would have refused
  `using N::operator<;`. They are now one `QualifiedName`, which splits on the
  `::` a template-argument-list, a parenthesized decltype-specifier or a
  subscript does not enclose.
- **Two declaration models.** PA7 and PA8 (`decl_parser`, `entity_model`) build
  namespaces and entities from the PA6 recognizer, and PA11 builds them from
  the PA10 tree. They stay separate on purpose: they read different front ends,
  and their oracles disagree about 8.3.5p5 (below). Table 10 of 7.1.6.2 was the
  one fact both had an answer for and is now owned by `type_model` alone.
- **Recovery that hid errors.** A const object's initializer was evaluated
  inside a `catch (std::runtime_error&)`, so `const int u = undeclared;` was
  accepted in silence, as was an initializer whose lookup was ambiguous. The
  5.19 evaluator now throws `NotConstant` for what is only not a constant, and
  every other failure - a name declared nowhere, a type-id naming no type, an
  incomplete `sizeof` - is a fact about the program and is no longer caught.
- **Declaring twice.** Two places tested for an anonymous union and injected its
  members, with two different tests; a member enumeration defined outside its
  class bound its enumerators into the region the definition was written in
  rather than the region the enumeration was declared in, so `W::av` did not
  resolve. Both are one rule now, and the region is the enumeration's own.
- **Reading twice.** A function definition read its parameter clause once for
  the function type and again for the names its region binds. `declarator_type`
  now returns the parameters of the clause 8.4.1p1 makes the definition's own,
  and `read_parameters` has one caller.
- **Statements.** Declarations reached through a `for`-init-statement were
  modelled and declarations in an `if`, `while` or `switch` condition were
  dropped. 6.4p3 puts both in the region around the substatement, and both are
  there now.
- **Dead surface.** `TypeTable::set_underlying` and `TypeTable::user_name` were
  written for PA11 and never called; removed.

## Final Architecture Review

Traced once more after the changes:

- **Names.** `QualifiedName` is the only reader of a name's spelling.
  `resolve`, `resolve_prefix`, and every declaration that introduces a name go
  through it, and an unqualified name - the common case - costs the one scan
  the split needs and no allocation.
- **Regions and lookup.** `SemaModel` is the only owner of "what does this name
  denote here". `SemaAnalyzer` never walks a scope chain and never inspects
  `Scope::names`; `sema_declarator` resolves a nested-name-specifier one
  component at a time through `lookup_in`, one lookup per component with no
  fallback attempt of a second kind.
- **Types.** `TypeTable` is the only owner of type identity, size, alignment
  and spelling. `SemaAnalyzer::lay_out_class` is the only writer of a class
  layout, and it writes it once, at the end of the member specification.
- **Values.** `sema_constant` is the only evaluator, `literal_scan` the only
  decoder of a literal's spelling, and the two failure kinds are now distinct
  types rather than one message.
- **Serialization.** `DumpScope` holds the output; `write_dump` is the only
  writer; `ast_emit` owns the wrapper lines both dump modes share. Nothing in
  the model is reconstructed from the dump, and nothing in the dump is
  reconstructed from source text.

## Boundaries

- **8.3.5p5 is not applied.** `typedef int Y[3]; int f(Y y);` writes
  `function of (array of 3 int) returning int` and `parameter y array of 3 int`,
  because `spec/100-namespace.ref` says so. The PA7 oracle says the opposite
  (`pa7/tests/350-function-adjust.ref` adjusts an array parameter to a pointer),
  which is why the two declaration models cannot share one declarator reader
  even where they could share a front end. If PA12 wants the adjusted form,
  this is the one line to change.
- **Overload sets.** A second function of one name with a different type
  replaces the binding rather than joining a set. PA11 puts overloads out of
  scope and no ref writes one; PA12 needs the set.
- **The complete-class context of 3.3.7p1.** The walk is in source order, so a
  member function body or a member declaration that names a member declared
  later in the same class is refused. No ref exercises it; PA12, which has to
  read a function body for its expressions, is where deferring bodies belongs.
- **7.3.4p2 is approximated:** a using-directive is searched from every
  enclosing region rather than from the nearest one enclosing both it and the
  lookup. That can find a name one level earlier than the standard says; no ref
  distinguishes.
- An unscoped enumeration's underlying type is `int` rather than the smallest
  type 7.2p7 allows, which is what the refs' sizes want and what PA13 layout
  will have to revisit.
- A class layout is the plain course-ABI one: members in order, each at its
  alignment, no bases and no bit-fields.

## Checkpoint Ledger

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | The whole PA11 semantic layer: type model extended with class, enum and template-parameter types; scope, entity, binding and dump model with 3.4 lookup; the AST walk over declarations, classes, enumerations, templates, functions and statements; declarator-derived types; the 5.19 constant subset and `decltype`; two PA10 enum-syntax gaps closed; declaration token spans | 68 / 68 pa11, 672 / 672 through pa11, file audit clean |
| 2 | Stage audit: one name-structure owner, an ill-formed initializer no longer swallowed, enumerator and anonymous-union ownership, condition declarations, the parameter clause read once, and the lookup rewritten around two indexes | 68 / 68 pa11, 672 / 672 through pa11, file audit clean |

## Audit

### Findings

| # | Finding | Kind |
| --- | --- | --- |
| 1 | A const object's initializer was evaluated inside `catch (std::runtime_error&)`, so an ill-formed initializer - an undeclared name, an ambiguous lookup, `sizeof` of an incomplete class - left the object non-constant and the program accepted | correctness |
| 2 | A lookup of a name several regions declare walked the whole using-directive closure of every enclosing region, and a lookup whose one declaration was not reachable through a directive walked it too, so both were quadratic in directives x lookups: 19 s on 1.1 MB of source | performance |
| 3 | Four places read a name's spelling back into components with three different string probes, and a `using-declaration` took any `<` for a template-id | architecture |
| 4 | An unscoped member enumeration defined outside its class bound its enumerators into the region the definition was written in, so `W::av` did not resolve and `av` resolved at namespace scope | correctness |
| 5 | A function definition read its parameter clause twice, once for the type and once for the names | repeated work |
| 6 | A declaration in an `if`, `while` or `switch` condition declared nothing, while one in a `for`-init-statement did | correctness |
| 7 | Two places decided whether a class specifier was an anonymous union, with two different tests | duplicate ownership |
| 8 | `TypeTable::set_underlying` and `TypeTable::user_name` were never called | dead code |

Not defects, recorded as boundaries above: 8.3.5p5, the complete-class context,
and the two declaration models.

### Changes

- `sema_name.{h,cpp}` (new): `QualifiedName`, the one reader of a name's
  spelling. Registered in `dev/frontend_source_sets.mk`.
- `sema_analyzer.h`: `NotConstant`, thrown by the 5.19 evaluator for what is
  only not a constant; `init_declarator` catches that and nothing else.
- `sema_constant.cpp`: 16 of the 21 throw sites are now `NotConstant`; the five
  that are facts about the program - an incomplete `sizeof`, a negative or zero
  array bound, and the two `decltype` refusals - stay fatal.
- `sema_scope.{h,cpp}`: `lookup` and `lookup_in` rewritten around
  `declarers_` and `Scope::searchers`; `bind` keeps `declarers_` exact by
  asking the binding table whether the name is new to the region; `nominate`
  records the reverse edge and the directive's target.
- `sema_analyzer.{h,cpp}`: enumerators declared in the enumeration's own
  region; one anonymous-union rule; `condition_declaration`; `init_declarator`
  takes its declarator and initializer rather than digging for them.
- `sema_declarator.cpp`: `declarator_type` carries out the parameters of the
  outermost parameter-clause; `resolve` and `resolve_prefix` take components
  from `QualifiedName`; `split_name` and `last_component` deleted.
- `type_model.{h,cpp}`: the two unused entry points removed.

### Performance Evidence

The two tables under Performance Model. In summary: linear to 6.8 MB of source
at 3.95 s and 581 MB; four using-directive quadratics measured against the
committed PA11 binary and removed, the worst 19.15 s -> 0.14 s; one shape 15 ms
slower on 854 KB; every nesting axis flat or linear in depth to the parser's
depth guard, with no crash at any depth measured.

### Validation

- `perl scripts/cppgm_file_audit.pl --stage pa11 --paths dev/src`: passed, 101
  files checked.
- `make test-report-through-pa11`: 672 / 672, 11 / 11 stages.
- Behaviour probed by hand beyond the suite: out-of-class member and namespace
  definitions, class and enum redeclaration, anonymous class, union and enum
  naming, enumerator injection, using-directive transitivity and ambiguity,
  namespace aliases and inline namespaces, block and condition scopes, local
  classes and enumerations, the constant subset and its overflow rules,
  `decltype` forms, multi-file runs, and the diagnosed forms of each.
