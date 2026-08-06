# PA11 Plan - `cppgm++ --emit-types`

## Stage Design

PA11 adds the first semantic layer on top of the PA10 syntax boundary: scopes,
declarations, bindings, canonical types, and a deterministic dump of all four.

Owners, all under `dev/src`:

| Owner | Responsibility |
| --- | --- |
| `type_model.{h,cpp}` | canonical types, extended for PA11 with the user-defined categories - class, enumeration and template parameter, each interned by the entity that declared it - and now the single owner of Table 10 of 7.1.6.2 |
| `sema_scope.{h,cpp}` | `SemaModel`: entity, scope and dump arenas, the binding table, the name-to-regions index, 3.4 unqualified and qualified lookup |
| `sema_analyzer.{h,cpp}` | the AST walk: namespaces, classes, enumerations, templates, using-declarations, functions, statements, and every dump line |
| `sema_declarator.cpp` | decl-specifier-seq and declarator to `TypeId` and name, parameter clauses, elaborated specifiers, nested-name-specifier resolution |
| `sema_constant.cpp` | the 5.19 subset: literals, enumerators, const objects, `sizeof`/`alignof`, casts, short-circuit operators, overflow, and `decltype` |
| `types_emit.{h,cpp}` | driver: one `SemaAnalyzer` per translation unit |
| `ast_emit.{h,cpp}` | the per-unit driver both dump modes share: phases 1-7, parse, wrapper lines |

Data flow: `AstTokenStream` -> `AstParser` (PA10) -> `AstNode` tree ->
`SemaAnalyzer` -> `SemaModel` (scopes, entities, types) -> dump tree -> output.
No pass re-reads source text: a name arrives as the spelling PA10 recorded, and
a literal arrives as its spelling and is decoded once by the shared
`literal_scan` tokenizer.

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

## Current Failure Map

Turn-start baseline: 0 / 68 (the mode threw `NotImplementedException`).
Now: 68 / 68 local, 672 / 672 through PA11.

| Group | Tests | Shared behavior | State |
| --- | --- | --- | --- |
| scope tree and declarators | 100-* (12) | namespaces, classes, functions, blocks, typedefs, declarator-derived types | pass |
| enumerations and constants | 200-enum-*, 200-*static-assert, 200-sizeof-*, 300-scoped-enum-* (14) | enum scopes, enumerator values, the 5.19 subset, `sizeof`/`alignof` bounds | pass |
| lookup | 200-*-lookup, 200-using-*, 200-*-qualified-* (13) | using-directives, using-declarations, namespace aliases, qualified lookup through namespace, class and scoped-enum scopes | pass |
| anonymous and elaborated types | 200-anonymous-*, 200-elaborated-*, 200-namespace-anonymous-* (7) | naming an unnamed class, elaborated specifiers, member injection | pass |
| templates | 200-template-*, 300-template-* (3) | template-parameter scopes | pass |
| diagnosed errors | 100-bad-*, 200-bad-*, 300-*-bad (19) | every one exits `EXIT_FAILURE` | pass |

## Active Checkpoint

None: checkpoint 1 completed the stage. The next turn's work is the stage
audit - the boundaries below are what it should attack first.

## Performance Model

Dominant operation: one pre-order walk of the PA10 tree, with a hash probe per
enclosing region per name looked up, and an interning probe per type built.
Nothing is deferred and no construct is visited twice, apart from a function
definition's parameter clause, which is read once for the function type and
once for the names its scope binds - a factor of two, not a nesting factor.

Release build, `dev/cppgm++ --emit-types`, generated sources of namespaces,
classes, member enumerations, typedefs, arrays with constant bounds,
`static_assert`, `sizeof`, `decltype`, qualified names and one using-directive
per namespace:

| Input | Parse | Analyse | Total | Peak RSS |
| --- | --- | --- | --- | --- |
| 88 KB / 200 namespaces | 0.02 s | 0.01 s | 0.03 s | 11 MB |
| 362 KB / 800 | 0.09 s | 0.04 s | 0.13 s | 31 MB |
| 1.5 MB / 3200 | 0.49 s | 0.18 s | 0.67 s | 112 MB |
| 6.2 MB / 12800 | 2.15 s | 0.91 s | 3.06 s | 436 MB |

Linear over a 70x range; analysis is about 40% of the parse it follows. Memory
is the PA10 arena plus the model and the dump; the dump is held rather than
streamed because a scope's lines keep growing after its children are added.

Two scaling faults were found by measurement and fixed; both were quadratic in
the number of using-directives written in one namespace:

| Workload | Before | After |
| --- | --- | --- |
| 3200 namespaces, one using-directive each | 4.64 s | 0.67 s |
| 12800 namespaces, one using-directive each | 178 s | 3.06 s |

- A nested-name-specifier component was looked up twice, once as a namespace
  and again as a type. The failing first lookup walked the whole
  using-directive closure of every enclosing namespace. One `LookupKind::Region`
  lookup answers both (`sema_declarator.cpp`, `sema_scope.cpp`).
- 3.4p1 makes a lookup that reaches two declarations of a name an error, so a
  closure walk cannot stop at the first hit in general. `SemaModel::declarers_`
  indexes each name by the regions that bind it, so a name no region declares
  costs no walk at all and a name exactly one region declares stops at its
  first hit. Writing the same directive twice is a probe rather than a scan.

Residual cost, deliberate: a lookup for a name that several regions declare, in
a namespace that reaches N namespaces through using-directives, still probes up
to N of them, which is what the ambiguity rule asks for.

Nesting sweep, 11 shapes (namespaces, classes, blocks, parenthesized constant
expressions, nested declarators, array dimensions, pointer chains, function
typedef chains, qualified-name chains, template-parameter scopes,
using-directive chains) at depths 8 to 8192: analysis time is flat or linear in
depth everywhere, and the superlinear part of the deep shapes is the PA10
parser's documented name-spelling cost, not the analysis:

| Shape at depth 1024 / 2048 / 4096 | Parse | Analyse |
| --- | --- | --- |
| nested namespaces | 14 / 41 / 154 ms | 1 / 7 / 8 ms |
| nested classes | 16 / 45 / 160 ms | 2 / 6 / 13 ms |
| nested declarators | 20 / 68 / 254 ms | flat |
| qualified-name chain | 13 / 42 / 151 ms | 2 / 5 / 12 ms |
| template-parameter scopes | 37 / 129 / 402 ms | 10 / 20 ms |

Stack: every input the parser accepts, the analysis also completes; every input
it refuses is refused before the analysis runs. No depth on any axis crashed.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| 1 | The whole PA11 semantic layer: type model extended with class, enum and template-parameter types; scope, entity, binding and dump model with 3.4 lookup; the AST walk over declarations, classes, enumerations, templates, functions and statements; declarator-derived types; the 5.19 constant subset and `decltype`; two PA10 enum-syntax gaps closed; declaration token spans | 68 / 68 pa11, 672 / 672 through pa11, file audit clean |

## Boundaries For The Audit

- Overload sets: a second function of one name with a different type replaces
  the binding rather than joining a set. PA11 puts overloads out of scope and
  no ref writes one; PA12 needs the set.
- 7.3.4p2 is approximated: the using-directive closure is searched at every
  enclosing namespace rather than at the nearest one enclosing both. That can
  find a name one level earlier than the standard says; no ref distinguishes.
- An unscoped enumeration's underlying type is `int` rather than the smallest
  type 7.2p7 allows, which is what the refs' sizes want and what PA13 layout
  will have to revisit.
- A class layout is the plain course-ABI one: members in order, each at its
  alignment, no bases and no bit-fields.
