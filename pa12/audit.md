# PA12 Audit — `cppgm++ --emit-semantics`

An independent review of the landed stage, in the order a fact travels: parse,
name, region, type, value, dump.

## Findings

**The whole stage, reviewed at `60cc89a7`.** The architecture holds: one analyzer
reads a declaration once and writes it to whichever of the two dumps the mode
asks for; a `Value` carries one analysed expression up from operand to operator
so no subtree is read twice; a fact about a type belongs to `TypeTable` and a
fact about a declaration to `SemaEntity`, built where the declaration is read.
The three layers above PA11 — expressions and conversions, classes and members,
function templates — each added the facts they needed at the declaration that
established them rather than by re-reading the tree.

What the review found is eight seams between owners drawn one question too
narrowly.  Six let the output describe a program the source does not have; one
refused a program the slice requires; one overran the machine stack.

**1. One kind for every template-name, so a call of a function template was a
declaration.** 8.2p1 resolves `f(x)` against what `f` names, and PA10's name
table answered that with a single `NameKind::Template` for a class template, an
alias template and a function template alike. 14.2p3 makes the first two a type
and the third an overload set, so `id(a)` and `id<int>(a)` are declarations of
`a` for a class template and calls for a function template. Every call of a
function template written with a named argument was read as a declaration:
`template<class T> T id(T); void f() { int p = 0; id(p); }` failed with "no
declaration of id is in scope", which the checkpoint's own group H had not caught
because its fixtures pass literals. A function-template name is now a
template-name for `a < b > c` and no type-specifier at all, and the name a
template-id was written on is what the reading asks about, so the argument list
does not hide it.

**2. The name table followed none of the three ways 7.3 makes a declaration
reachable under another spelling.** A using-declaration recorded nothing, a
namespace alias recorded only that it was a name before `::`, and a
using-directive recorded nothing at all — so the same 8.2p1 ambiguity resolved
the same wrong way for `using ns::g; g(p);`, for `using namespace ns; g(p);` and
for `namespace al = ns; al::g(p);`. Each is a required feature of the slice: the
README asks for unqualified lookup extended by using-declarations,
using-directives and namespace aliases, and for calls through function names. A
using-declaration now binds its name to the kind its target names, an alias
records the namespace it stands for, and a scope records the namespaces its
directives reach. The directives are asked only after every scope has been asked
the cheap question, which is what keeps the addition free: 800 directives against
8000 statements over a declared name is 0.09s, the same as 200 against the same
8000.

**3. A base clause and a bit-field were read as though they were not there.**
10p1 makes a base class a subobject of every object of the derived class, which
its layout counts, a member name reaches through and its constructor initializes;
9.6p1 makes a bit-field a member whose width its declaration writes. PA12 models
none of that, and both were silently dropped: `struct A { int x; }; struct B : A
{ int y; }; constexpr unsigned long s = sizeof(B);` wrote 4 where the program has
8, and constructed no base. Each is now refused where the semantics dump reads
it, named in its diagnostic. PA11 only spells the declaration and is unchanged.

**4. A cast to a reference type was written three different ways, depending on
what wrote its operand.** 5.2.9p1 gives the cast and the operand one node
wherever the reference binds the operand itself, so the operand's own line is
respelled with what the cast made of it — but the two parts of that line the
format puts either side of the type were carried only by an id-expression and a
literal. For every other operand the cast wrote nothing at all: `(const int&)f()`
described the call as the `int` prvalue it was, `(const int&)(a + b)` the sum,
`(int&)arr[1]` the subscript. Where a conversion was needed the operand's line
was respelled anyway, which said the `long` object `a` had type `lvalue-reference
to const int`. And nothing checked that the reference bound at all, so
`(int&)f()` was accepted where the ref refuses it.

The fix is the missing owner: a `Value` now carries the node kind and the payload
of the line it wrote, one `spell` writes every line of the expression output from
them, and `respell` writes one again from the category and type the value now
has. 8.5.3p4's reference-related is what decides which of the two shapes a
reference cast is — related binds the value it was given, so the operand's line
stands; unrelated binds a temporary the conversion made, which is a node of its
own — and an lvalue reference that is not to a non-volatile const binds no
operand naming no object.

**5. `int C::s;` wrote a line into the class it names.** 9.4.2p2 makes a
definition written with a nested-name-specifier define the object its class
already declares, so it declares nothing there. The C2–C4 audit had stopped the
entity being duplicated but not the line: `--emit-types` wrote two `variable s
int` lines in `scope class C` for one object, and `--emit-semantics` wrote a
top-level line spelled without the class. The line now stands where the
definition is written, spelled the way it wrote it, which is what the ref writes
in both modes.

**6. The expression walk was unbounded.** 5.6 makes `a + b + c + ...` a tree as
deep as it is long while nesting no parse rule, so the PA10 depth guard — which
refuses about a thousand levels of nesting — did not bound it at all. 24000 terms
in 468 KB of source overran the machine stack, where `--emit-ast` and
`--emit-types` read the same file without trouble. The same counter now bounds
the walk, and a file that goes deeper is refused rather than crashing. Every
other shape the walk recurses on nests the parse as well, and so is already
bounded where it is read: 9900 nested unbraced `if` substatements are accepted
and 8000 nested blocks refused, both by the parser.

**7. Two namespaces could not contribute one overload set.** 3.4p2 lets a lookup
associate more than one declaration with a function name and 7.3.4p3 lets it
reach the declarations of several namespaces at once, so two namespaces each
declaring an overload of one name make one set for the call to choose from. The
model treated any name two regions declared as 3.4p1's error and refused the
program, where the README puts using-directive lookup and overload resolution
both inside the slice.

What the layer was missing is a place for that set to live. One region's
declarations of a name are the chain the name heads, and a lookup may not relink
those — a chain is a fact about the region that declared them. So the set a
lookup found is now the lookup's own: a list of the chains it reached, held by
the model that built it and carried by the value the name denotes. Two
declarations that are not both functions are still 3.4p1's error, and so are two
for a caller that asked for a single declaration. The template layer stops
threading `next` through the specializations a template-id makes, because a
specialization no chain holds needs no link.

**8. 5.16p3 was read as 5.16p4 alone.** A conditional expression whose operands
are two lvalues was given their common type only when the two types were equal,
so `choose ? plain : qualified` over an `int` lvalue and a `const int` lvalue gave
a prvalue `int` where the program has an lvalue `const int`. 5.16p3 converts each
operand to an lvalue reference to the other's type, and the one conversion that
binds says what the result denotes — which is a question 8.5.3 already answers,
so it is asked rather than restated.

## Changes

Landed as three commits: `0bd2e051` for findings 1 to 3, `e199e170` for 4 to 6,
`623d3ab5` for 7 and 8.  Each fix is at the owner the finding names above, and
none of them moved a boundary the plan's Stage Design does not now record.  What
the review changed beyond the eight findings:

- The one lookup that says whether `&` was written on a qualified-id naming a
  member of a class is spent once and handed on, rather than spent again by the
  expression layer when the answer is no. That is the same rule the callee of a
  call already followed.
- Eight places built an expression line by hand, in three different orders. One
  `spell` writes them all, and `respell` is what a conversion calls.
- The dump carries its indent down the walk instead of building it from the depth
  per line, which for a tree as deep as it is wide was the output written twice.
- `member_address` no longer returns a value of no type to mean "not
  applicable"; the caller that resolved the name decides, and passes the member.

### Confirmed intact

- 848 / 848 through PA12, with six tests added under
  `cppgm.tests/course/pa12/` for what this audit fixed. Every refusal it
  introduced is a program the ref accepts and no fixture pins, which is why none
  of them is a test; they are listed in the plan.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output, interpreter substitute or file-audit bypass. What PA12 does
  not model is refused where it is read and named in the diagnostic.
- Valgrind clean over the 166 PA12 fixtures, the 10 course tests and the
  synthesized probes, including the deep and multiple-item ones.
- The file audit passes; its one warning, that `sema_analyzer.h` carries more
  than 180 body lines, is the heuristic counting declarations. The header holds
  the class's private API and exactly two function bodies, `Constant()` and
  `semantics()`, totalling seven lines; the eight nested fact types and about 180
  method declarations are the rest. The methods are one semantic pass over one
  shared state — `types_`, `model_`, `self_`, `returns_` and the `Context` and
  `Value` vocabulary — so splitting the class would mean passing that state
  between the halves rather than dividing anything. Left as it is, with the count
  recorded rather than worked around.
- Scaling is linear in every axis, measured on this host and recorded in the
  plan: 16000 of each of eighteen kinds of declaration, expression and call, and
  640000 pairs on each of the four N x N axes. Depth costs the parse guard or the
  new expression guard rather than time. The two shapes that are quadratic are
  quadratic in the bytes the format asks for, not in the work per byte.

## Durable architecture decisions

- One analyzer serves both dumps: a declaration is read once and written to
  whichever tree `SemaDialect` asks for.
- A name binds the head of the declaration chain of one function name in one
  region; the chain is indexed by parameter type list for declaring and walked in
  order for resolving. The set of chains one lookup found belongs to the lookup.
- A `Value` carries one analysed expression up from operand to operator,
  including the line it wrote, so a conversion rewrites that line in place rather
  than the output being built in a second pass. One place spells a line.
- A fact about a type alone belongs to `TypeTable`; a fact about a declaration
  belongs to `SemaEntity`, built where the declaration is read. One of them
  answers one question.
- 9.3.1p3's object parameter lives in the function's type, so everything above
  reads a member function as the function it is.
- A definition the place it is written cannot hold is appended once to
  `pending_`, which the end of the unit walks and which a body it reads may
  append to, so it holds its elements still.
- What the assignment does not model is refused where it is read, not described
  as the program it would be without the construct.
- A recursive walk over a shape the source can make arbitrarily deep is bounded
  by a counter, and a file that goes deeper is refused rather than crashing.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | argument conversions written out of order; an unresolved overload set accepted in five discarding contexts; `&f` refused; enumeration comparisons rejected; `--` on `bool` accepted; O(N²) overload declaration; callee looked up twice; two type-fact owners | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05 s → 0.26 s; file audit clean |
| C2–C4 | classes and members, pointers to members, `decltype(x)(1)` | a pending definition written through a reference into a reallocating list; a null node dereferenced by an anonymous union and by an alias in a class; a static data member read through `this` and counted in the layout; `int C::s;` declaring a second object; `&C::x` refused; an anonymous union member written two ways; a declared constructor and an incomplete class silently accepted | pa12 165/166 held; pa1–pa11 672/672; valgrind clean; linear to 8000 members, classes, unions and `&C::m`; file audit passes, one header-weight warning |
| Final | the whole stage, independently of the checkpoints above | one name kind for a class and a function template, so every call of a function template written with a named argument was a declaration; the three 7.3 spellings followed by none of the name table; a base clause and a bit-field read as absent, giving a wrong layout and no base construction; a cast to a reference type written three ways and validated in none; `int C::s;` writing a duplicate line into its class; an unbounded expression walk that overran the stack where PA10 and PA11 did not; two namespaces unable to contribute one overload set; 5.16p3 read as 5.16p4 alone | pa1–pa12 842/842 → 848/848, six tests added; valgrind clean; every axis linear and 640000 pairs on each N x N axis; file audit passes, one header-weight warning recorded rather than worked around |

## Performance Evidence

Measured with `cppgm++ --emit-semantics` on synthesized inputs (this host); the
full table is in the plan's Performance Model, which this turn re-measured from
scratch rather than carrying forward.

- Eighteen single-axis cases at 8000 and 16000 items: every one about 2.1x its
  half.  The largest are 16000 classes each constructed once at 0.57s, 16000
  specializations of one template at 0.64s, 16000 block-scope anonymous unions at
  0.53s, and 16000 overloads of one name at 0.47s.
- Four N x N axes, sized by pairs: overloads of one name x calls of each, 640000
  pairs at 0.07s; templates of one name x calls of each at 0.12s;
  using-directives x unqualified lookups at 0.12s; namespaces nominated x lookups
  of a nearer name at 0.01s.  All four are linear in the pair count, which is what
  says no axis of the stage is quadratic.
- The one thing the audit added to the hot path, the PA10 name table's probe of
  the using-directives in scope, is asked only after every scope has missed: 800
  directives against 8000 statements over a declared name is 0.090s, against
  0.084s for 200 directives and the same 8000 statements.
- Eight depth cases at 640 levels, all at or under 0.06s, the slowest being
  nested `decltype(...)( )` casts where the specifier is skipped by a balanced
  token scan rather than parsed per level.
- The two shapes the output format makes quadratic are linear in the work per byte
  it asks for: 9900 nested unbraced `if` substatements write 785 MB in 2.1s, and a
  left-leaning operator chain writes the same shape until the expression guard
  refuses it.  Neither is quadratic in the analysis.
- Nothing this audit changed is slower than before it: the sixteen axes measured
  at the turn's start and again at its end differ by less than the run-to-run
  spread, and 16000 overloads went 0.51s to 0.47s.

## Validation

- `make test-report-through-pa12` — 848 / 848, from a clean tree at each of the
  three commits this audit landed.
- `perl scripts/cppgm_file_audit.pl --stage pa12 --paths dev/src` — passes, with
  the one `bad-division` warning on `sema_analyzer.h` accounted for above.
- `valgrind -q --error-exitcode=99` over 166 PA12 fixtures, 10 course tests and
  ~25 synthesized probes — no error.
- Scaling: 18 single-axis cases at 8000 and 16000, 4 N x N axes at 160000 and
  640000 pairs, 8 depth cases at 640 levels, and the two guards checked at the
  boundary (9000 chained operators accepted, 16000 refused; 9900 nested `if`
  substatements accepted, 8000 nested blocks refused by the parser).
- Differential observation against `cppgm++-ref` over 80 synthesized inputs
  covering overload ranking, the conversion subset, the 7.3 lookup forms,
  reference binding and casts, member pointers, anonymous unions, templates and
  each diagnostic the README names. Every difference that remains is one of the
  seven recorded in the plan.
