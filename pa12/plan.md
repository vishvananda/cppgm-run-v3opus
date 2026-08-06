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
| what a name in scope is (type, value, template) | `DeclaredNames` (`ast_names.h`) |
| the components of a name's spelling | `QualifiedName` / `TemplateId` (`sema_name.*`) |
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
  from operand to operator, so no subtree is read twice. It also carries the two
  parts of the line it wrote that are not the category and the type — the node
  kind and the payload — so `spell` writes every line of the expression output
  and `respell` writes one again from the category and type the value now has.
  That is what lets each conversion the output makes visible — the materialized
  temporary, the null pointer constant, the resolved overload set, a cast to a
  reference — rewrite the line the operand wrote, in the place it wrote it.

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
  name and held by no region's chain: it is reached only from the set the
  template-id that wrote its arguments found or the call that deduced it, so
  ordinary lookup never finds a declaration the program did not write.
- `TypeTable::substitute` — 14.3, a type rebuilt with its parameters replaced,
  memoized within one substitution so a shared subterm is rebuilt once and a
  type holding no parameter is itself.

The final audit added one more, which is where 3.4 and 13.1 disagree about who
owns a set of declarations:

- `SemaModel::open_overloads` — 3.4p2 and 7.3.4p3, the declarations one lookup
  associated with a function name. One region's are the chain the name heads,
  and a lookup a using-directive extended can reach the chains of several, so
  the set is a list of heads that belongs to the lookup rather than to any
  region: a chain is a fact about the region that declared it and no lookup may
  relink it. Two declarations that are not both functions stay 3.4p1's error,
  and so do two for a caller that asked for a single declaration.

Two facts of the PA10 layer that the PA12 slice turned out to rest on:

- `NameKind::FunctionTemplate` — 14.2p3 makes a template-id of a class or alias
  template a type and one of a function template an overload set, so 8.2p1's
  `f(x)` is a declaration of `x` for the first and a call for the second. One
  kind for every template-name made every call of a function template written
  with a named argument a declaration.
- `DeclaredNames::alias` and `DeclaredNames::nominate` — 7.3.2, 7.3.3 and 7.3.4
  each make a declaration reachable under a spelling other than the one it was
  written with, and the same ambiguity turns on what that spelling names. A
  using-declaration binds its name to the kind its target names, an alias
  records the namespace it stands for, and a scope records the namespaces its
  directives reach. The directives are asked only after every scope has been
  asked the cheap question, so a name that is declared costs no probe of one
  however many are written.

## Current Failure Map

**176/176**, with no group open.  The six added by the final audit are the
PA-local regressions of what it fixed, under `cppgm.tests/course/pa12/`.

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
| K. template and 7.3 name kinds, reference casts, a static member defined outside its class, cross-namespace overloads, 5.16p3 | 6 | done |

Refused rather than described, each named in its diagnostic and none of them a
fixture: instantiating a function template that has a definition (14.7.1 reads
the body again against the arguments and PA12 has no rule that does); a
template-id whose argument list is shorter than the template's parameter list,
which 14.8.1 leaves the rest of to deduction; a non-type template parameter,
which PA11 already declared nothing for; a class that declares a constructor,
destructor or conversion function (12.1 chooses among them and PA12 has no rule
that does); a call of a member function (13.3.1.1.1, which the README also puts
outside the slice); an object of an incomplete class; a class with a base clause
(10p1 makes a base a subobject that the layout counts, a member name reaches
through and the constructor initializes, and PA12 models none of the three); a
bit-field member (9.6p1's width, which the layout and every use of it read); and
an expression that nests deeper than the walk reads.  One name resolution the
class layer sits on is PA11's and predates it: a class-head with a
nested-name-specifier (`struct N::C { };`) declares a second class rather than
defining the one it names, so the forward-declared one stays incomplete and an
object of it is now refused where it used to be accepted quietly.

## Active Checkpoint

**None open — PA12 passes in full (176/176) and its final audit is closed.**
What a later assignment picks up from here is listed under Current Failure Map
as refused, and in the divergences below.

## Performance Model

The dominant operations are: one visit per syntax node; one lookup per name
written; one probe per declaration made; one walk of the candidates per call;
one substitution per template and argument list; and one line written per dump
node.  Every one of them is O(1) or O(what it is about), so the whole pass is
linear in the source and in the output — except where the output format itself
is not, which is a tree as deep as it is wide.

Measured with `cppgm++ --emit-semantics` on synthesized inputs (this host), each
size against its half:

| Case | 8000 | 16000 |
| --- | --- | --- |
| block-scope declarations and initializers | 0.11s | 0.22s |
| calls of one function | 0.06s | 0.13s |
| functions declared and called once each | 0.15s | 0.31s |
| overloads of one name | 0.22s | 0.47s |
| members of one class | 0.05s | 0.10s |
| block-scope objects of class type constructed | 0.09s | 0.17s |
| classes, each defined and constructed once | 0.26s | 0.57s |
| member functions defined in one class | 0.12s | 0.26s |
| members read through `this` in one body | 0.06s | 0.11s |
| `&C::m` over the members of one class | 0.18s | 0.36s |
| block-scope anonymous unions | 0.24s | 0.53s |
| unnamed local classes | 0.23s | 0.49s |
| specializations of one template, each named once | 0.27s | 0.64s |
| calls naming one specialization again | 0.08s | 0.17s |
| arguments converting to a reference parameter | 0.08s | 0.14s |
| enumerators of one enumeration | 0.04s | 0.09s |
| case labels of one switch | 0.05s | 0.11s |
| qualified lookups of one name | 0.06s | 0.11s |

The N x N axes, sized by the number of pairs rather than by one side:

| Case | 160000 pairs | 640000 pairs |
| --- | --- | --- |
| overloads of one name x calls of each | 0.03s | 0.07s |
| templates of one name x calls of each | 0.04s | 0.12s |
| using-directives x unqualified lookups | 0.04s | 0.12s |
| namespaces nominated x lookups of a nearer name | 0.01s | 0.01s |

Depth, at 640 levels: nested unbraced `if` substatements 0.01s, nested blocks
0.01s, nested class definitions 0.01s, nested `decltype(...)( )` casts 0.06s,
nested parenthesized binary expressions 0.01s, a 640-deep pointer type 0.01s,
deduction through a 640-deep pointer pattern 0.01s, a template-id whose argument
is one 0.01s.

- The walk is one visit per node; nothing is reparsed and no subtree is read
  twice, so depth costs stack rather than time.
- Lookup keeps the PA11 `declarers_` index and demand-gathered searchers.
  A block-scope using-directive is recorded in the namespace 7.3.4p2 puts its
  names in, so it adds no edge a block lookup has to walk. A call's callee name
  is looked up once, by the reader that had to know whether it named a type, and
  so is the qualified-id under a `&`, by the reader that had to know whether it
  named a member of a class.
- Overloads are one chain per name per region, indexed by parameter type list
  for declaring and walked in order for resolving, so neither costs a rescan.
  A lookup several using-directives extended costs one list of the chains it
  reached and no copy of a declaration.
- Dump nodes are arena-allocated and linked once; every rewrite a conversion
  performs is O(1) on the node it already wrote, in the place it wrote it. The
  writer carries its indent down the walk rather than building it per line, so a
  line costs the two characters it added.
- A member is found by one probe in the region its class declares, and the
  constructor an object asks for is held on the class, so neither costs a
  search. A synthesized definition is appended once, under the flag on the
  declaration that says it has been asked for.
- A `decltype`-specifier written where a call's callee stands is skipped by a
  balanced token scan and then read once as the expression it holds, so nesting
  costs one scan per level rather than a parse per level.
- A specialization is interned under its template and its argument list, so
  naming one again is a probe: 16000 calls of one specialization substitute once
  and write one declaration.  Deduction is one structural walk of the pattern
  against the argument and costs no interning, so templates of one name against
  calls of each stays the same N x N shape the non-template row has.
  Substitution memoizes within one call, so a type reached twice is rebuilt once
  and a type holding no parameter is returned as it is.
- The PA10 name table answers what a plain name is from the scopes it walks, and
  asks the using-directives in scope only once every scope has missed. 800
  directives against 8000 statements over a declared name is 0.09s, the same as
  200 against the same 8000, so a name that is declared costs no probe of a
  directive.
- Two shapes are quadratic in the *output* and linear in the work per byte of
  it, because the format indents two spaces per level: a left-leaning operator
  chain and a nest of unbraced substatements each write a tree as deep as it is
  wide.  9900 nested `if` substatements write 785 MB in 2.1s; the expression
  walk refuses past its depth guard, which 9000 chained operators are inside of
  and 16000 outside.

## Architecture Review

Where the seams are, and what each of them is for.  The PA12 pass is five layers
and one dump, and every layer above the parse asks the layer below rather than
re-reading the tree it came from.

- **Parse to name.** PA10 hands on a name as the spelling it was written with,
  because its own dump names it that way.  `QualifiedName` and `TemplateId` are
  the one place that turns a spelling back into components, and they know what a
  component may hold, so a `::` inside a template-argument-list belongs to the
  component around it.  A template-argument reaches the semantic layer inside a
  name rather than as a tree, so `sema_template.cpp` reads a type-id from
  terminals - the one text recovery the stage has, and it is bounded to the PA12
  argument subset and refuses anything else rather than guessing.
- **Name to region.** `DeclaredNames` answers what a name in scope is, for the
  two ambiguities the grammar cannot resolve alone; `SemaModel` answers which
  declaration a name reaches.  The first is a fact about spellings and the second
  about regions, and they are separate because the parse has no scopes to look
  into and the semantic pass has no need of the parse's guess.
- **Region to type.** A fact about a type alone is `TypeTable`'s; a fact about a
  declaration is `SemaEntity`'s.  Interning makes two types equal exactly when
  they are the same type, which is what makes 13.1's parameter-type-list a
  32-bit key and 14.7.1's argument list another.
- **Type to value.** A `Value` is one analysed expression, and it carries what it
  wrote as well as what it means, so a conversion the output makes visible
  rewrites one line in the place it stands.  There is no second pass.
- **Value to dump.** `DumpScope` is a tree of lines and `DumpNode` a tree of
  nodes, because PA11's output is declarations and PA12's is ordered, nested
  resolved expressions.  Keeping them apart lets each be what its own rules say.

Two seams are deliberately one-way.  A declaration is read once for both dumps,
so nothing in the PA12 layer may change what PA11 describes: every place the two
part company asks `semantics()` at that place rather than branching earlier.  And
what the assignment does not model is refused where it is read, not described as
the program it would be without the construct — which is what makes each of the
refusals the Current Failure Map lists a fact about the boundary rather than a
gap in the output.

## Final Architecture Review

The stage is a sound base for the class and template assignments above it.  One
walk, one owner per fact, no construct read twice, and every conversion the
output shows written in the place the operand wrote its line.  The eight findings
of the final audit were all the same shape: a seam drawn one question too
narrowly, where an owner answered the question it was built for and not the
neighbouring one the standard asks in the same breath.  None of them moved a
boundary; each of them widened one owner's question and removed the second answer
that had grown up beside it.

Three of the eight were in the PA10 layer PA12 rests on, which is where a
whole-stage review pays for itself: the name table answered "is this a type" with
one kind for three kinds of template-name and with nothing at all for the three
ways 7.3 makes a declaration reachable, and the PA12 slice needs both.  The
remaining five were in the PA12 layer: a value that did not carry the line it
wrote, a definition whose line went to the region it named rather than the region
it stood in, a walk with no bound on a shape the parse does not bound, a set of
declarations with no owner, and a conditional expression read at one of its two
paragraphs.

What is left for the assignment above this one, listed as refused in the Current
Failure Map, is exactly the class-aware and template-aware machinery the README
puts outside PA12: choosing among a class's special member functions, resolving a
call of a member function, reading a template's definition against its arguments,
and the base-class subobject.  Each is refused at the declaration or the use that
needs it, so the next assignment adds a rule where a diagnostic now stands rather
than having to find where an assumption was made.

## Divergences from `cppgm++-ref`

Recorded rather than matched, because no fixture pins any of them and each is
the reading the handout and the standard give:

- The order of several synthesized constructor definitions, which we write in
  first-use order and the ref writes in an order that is neither declaration nor
  use order.
- Member function calls, which the ref resolves and which the README puts
  outside the PA12 slice.
- The literal type the output gives the operand of `static_cast<T*>(0)`, which
  the ref writes as the pointer and we write as the `int` it was.
- A namespace-scope object whose initializer needs a conversion: the ref folds
  the initializer to the object's type (`long a = 1;` writes `literal prvalue
  long int 1`), while at block scope it writes the literal's own type, which is
  what the fixtures pin.  We write the literal's own type at both.
- A member of an anonymous union reached through an object expression: we write
  the access through the object 9.5p1 says the union declared, which is the one
  rule and the one the block-scope fixture pins; the ref skips that object when
  an object expression was written.
- A member function declared in its class and defined outside it: the ref writes
  a `function-declaration` line for the declaration, where we write no line for
  any member declaration - a data member, an alias, a function alike - because
  9.2p1 makes what a member declares part of an object of its class.
- The name a use of a static data member is written with: we write the name as
  the program spelled it, which `300-overloaded-name-written-as-found` pins for
  a function, and the ref writes the last component of it.

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
| Final audit | a template-name that names no type, the three 7.3 spellings, a base clause and a bit-field refused, one spelling for a value's line, casts to a reference type, a bounded expression walk, one set for the declarations a lookup found, 5.16p3's lvalue | pa1–pa12 842/842 → 848/848 (6 tests added); valgrind clean; every axis linear, N x N to 640000 pairs; file audit passes with one header-weight warning |
