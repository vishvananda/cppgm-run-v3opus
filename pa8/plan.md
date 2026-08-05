# PA8 (`nsinit`) Plan

## Stage Design

`nsinit` runs phases 1-7 over every source file, parses `pa8.gram` with
semantic actions, links the translation units under 3.5 and writes the PA8
mock program image.  It extends PA7 rather than replacing it: phases 1-7, the
type table, the entity model and the declarator machinery are PA7's, and PA8
adds values, initialization, linkage and the image.

New owners, on top of PA7's:

| Owner | File | Typed fact it keeps |
| --- | --- | --- |
| `LiteralValue` pool | `sema_token.*` | 2.14 a literal token's type, object representation and the integer or floating value read back out of it |
| `ConstValue` / `ExprValue` | `value_model.h` | 5.19 what is known of a value, and 3.10 the category and type of an expression |
| `TypeTable::object_size/align/cv` | `type_model.*` | the course ABI size and alignment of a type, 3.9p6 completeness and 3.9.3p5 qualification |
| `Symbol` / `ProgramImage` | `program_model.*` | one object of the image: 3.5 identity and linkage across units, its constant initializer, its place in the file |
| `InitSemantics` | `init_semantics.*` | clause 4 standard conversions, 8.5/8.5.2/8.5.3 initialization, 5.19 constant evaluation and which objects it may read |
| `DeclParser` (extended) | `decl_parser*.cpp` | `pa8.gram`: expressions, initializers, `static_assert`, function definitions, 7.1.1 storage class, 3.4.3p3 scope change |

Data flow: source file -> `SemaToken` array + literal pool -> `DeclParser` ->
(`TranslationUnitModel` for one unit, `ProgramImage` for the whole program) ->
layout -> image bytes.  Nothing flows back into the token stream, and the only
state shared between units is the name table, the type table and the image.

Five decisions drive the shape:

- **The image is the program-wide owner, the model stays per unit.**  A
  namespace is a declarative region of one translation unit, so the models stay
  separate; `ProgramImage` keys an external-linkage declaration on (interned
  namespace path, name, signature) and hands back the same `Symbol`, which is
  what makes `extern int x;` in one unit and `int x = 3;` in another one
  object.  Symbols are appended when first declared, so block 1 is already in
  "order of first declaration within the program".
- **Declared and defined are different questions.**  The output format holds
  "each defined named variable and declared function", so block 1 is filtered
  at layout: a variable no unit ever defines keeps its place in the order but
  occupies no bytes, and a function needs only a declaration.
- **A value is a `ConstValue`, not bytes.**  5.19 needs an integral value, a
  floating value, a null pointer, or a symbol plus an addend whose absolute
  address is only known after layout.  Keeping that shape until the image is
  written makes relocation one pass and makes an lvalue-to-rvalue read of
  another object a lookup rather than a re-evaluation.
- **One parser, two grammars.**  `pa8.gram` is a superset of `pa7.gram`, so the
  PA7 parser is extended in place; the presence of a `ProgramImage` selects the
  PA8 productions and the PA8 diagnostics, because PA7 leaves an ill-formed
  program undefined and its fixtures pin that.
- **Declarator errors are decided where the type is built.**  8.3.1p4, 8.3.2p5
  and 8.3.4p1 forbid shapes the type table would otherwise happily intern, and
  8.3.2p6 collapses a reference only when the inner one came from a typedef, so
  `build_type` carries "this operand was made a reference by this declarator"
  along the chain it already walks.

## Performance Model

Dominant operations, in the order they cost:

| Path | Shape | Complexity |
| --- | --- | --- |
| phases 1-7 | PA1-PA5 lexing, macro expansion, spelling interning | linear in bytes |
| declaration parsing | one forward pass; backtracking is one `(`-lookahead per declarator | linear in tokens |
| unqualified lookup | the name's own namespace, then one level of 7.3.4p2 at a time; a namespace that nominates nothing answers from its own map | O(1) with no directive in scope, else O(namespaces the level reaches) |
| the 7.3.4p2 closure | one BFS per (namespace, using-directive epoch), kept until a new directive is written | O(closure), amortised to zero while no directive is written |
| 3.5 linkage | one hash probe on an interned namespace path per declaration; the path is interned once per namespace and cached | O(1) amortised |
| function redeclaration | one probe on (overload set, signature), qualified or not, never a walk of the set | O(1) amortised |
| initialization | clause 4 walks the pointer chain of the two types once | O(pointer depth) |
| constant evaluation | an lvalue-to-rvalue read is one indexed lookup of the object's kept value | O(1) |
| layout and output | one pass over the three blocks, then one pass writing what it placed | O(objects + image bytes) |

Measured (`-O3`, one core):

| Workload | Time | Peak RSS |
| --- | --- | --- |
| 50k / 100k / 200k initialized variables | 0.16 / 0.37 / 0.84 s | 27 / 51 / 98 MB |
| 25k / 50k / 100k string literals behind pointers | 0.12 / 0.26 / 0.60 s | 20 / 35 / 67 MB |
| 25k / 50k / 100k character arrays from string literals | 0.14 / 0.29 / 0.64 s | 24 / 43 / 82 MB |
| 8k / 16k / 32k / 64k function declarations | 0.04 / 0.08 / 0.18 / 0.38 s | 8 / 12 / 21 / 38 MB |
| 8k / 16k / 32k / 64k overloads of one name | 0.05 / 0.09 / 0.22 / 0.46 s | 9 / 14 / 25 / 46 MB |
| 2k / 4k / 8k out-of-line definitions of overloads of one name | 0.02 / 0.04 / 0.08 s | 6 / 7 / 10 MB |
| 20k / 40k / 80k array bounds read from one const | 0.06 / 0.12 / 0.27 s | 12 / 20 / 37 MB |
| 25k / 50k / 100k reference-bound temporaries | 0.12 / 0.24 / 0.56 s | 19 / 35 / 67 MB |
| 2k / 4k / 8k nested namespaces, one declaration in each | 0.02 / 0.03 / 0.05 s | 7 / 9 / 14 MB |
| 8k namespaces, each a using-directive and a lookup | 0.06 s | 14 MB |
| 100 / 200 / 400 units x 500 external names | 0.07 / 0.13 / 0.26 s | 5 / 7 / 9 MB |
| 200 units x a 500-deep namespace | 0.15 s | 7 MB |
| 200 MB image from one array | 0.24 s | 4 MB |
| 16 MB string literal | 0.47 s | 126 MB |
| 32 MB of declarators (pointer depth to 8000) | 7.9 s | 291 MB |

Every shape in the table is linear in its input.  Two shapes were not; the
first is now, and the second is a bound this stage keeps knowingly:

- Redeclaring one of the functions a name reaches through a **qualified**
  declarator-id walked the overload set, which is quadratic in the functions
  the name reaches; 8000 out-of-line definitions took 0.21 s against 0.10 s for
  8000 distinct names.  Both paths now probe the same signature index and the
  two are 0.080 s and 0.088 s.
- A translation unit that writes N using-directives **in one namespace** is
  O(N x closure), because each directive's own namespace-name is an unqualified
  lookup and the previous directive invalidated the closure: 2k/4k/8k
  directives take 0.05 / 0.16 / 0.72 s.  Rebuilding the 7.3.4p2 closure per
  lookup is what production compilers do as well, and the cache already makes
  the settled case - directives first, then lookups - free.  Adding N lookups
  that have to scan the whole level for 3.4p1 ambiguity makes 8k directives
  2.99 s.  The bound is recorded rather than removed: an incremental closure
  needs a per-namespace membership set and a re-anchoring rule for a namespace
  that a later directive reaches from a nearer scope, which is a lookup-order
  hazard out of proportion to a shape no real translation unit has.

Memory is flat in the number of translation units: 400 units declaring the same
500 external names hold 500 objects, because linkage is a probe into one table
rather than a copy per unit.  The per-unit namespace cache in `ProgramImage` is
dropped at each `begin_unit`, since a `Namespace` dies with its model and its
address would otherwise be reused by the next one.  A token costs one 8 byte
`SemaToken`; a literal costs one pool entry on top, which is what the 32 MB
declarator workload's 291 MB is (it is 32 M tokens).  The 16 MB string literal
workload is phases 1-7's: `preproc` alone takes 160 MB on the same input.

Depth is bounded at 10000 open frames, shared with PA6 and PA7, and also counts
a parenthesized expression: 9999 nested parentheses parse in 1-2 MB of the 8 MB
default stack, and 10001 are refused.  Every other unbounded chain - the type
an object's size is read off, the namespace path of a declaration, the pointer
chain a qualification conversion walks, the dimensions of an array - is a loop.

## Architecture Review

What the stage looks like after this audit, by the question each file answers:

- `sema_token.*` - what a phase 7 token is worth.  One token is one word; a
  literal's type, bytes, integer value and floating value all come from its one
  pool entry, so no later file decodes a literal a second way.
- `type_model.*` - what a type is.  Interning, 8.3's builders, and every fact
  the ABI states about a type: size, alignment, completeness, qualification.
  Nothing else states a size.
- `entity_model.*` - what a name denotes in one translation unit.  3.4 lookup,
  including 3.4p1 ambiguity, 7.3 namespaces and the overload set index.
- `value_model.h` - what is known of a value and of an expression.
- `program_model.*` - what the program contains.  3.5 identity and linkage,
  the three blocks, layout, relocation, output.
- `init_semantics.*` - clause 4, 8.5 and 5.19.  One place decides whether an
  lvalue-to-rvalue conversion may read an object.
- `decl_parser*.cpp` - the grammar and the semantic actions, split by what they
  are about: declarations, declarators, expressions, objects.

The one place two paths remain is the array bound, which `pa7.gram` spells as a
literal and `pa8.gram` as a constant-expression.  They are two grammars with
two error contracts - PA7 must describe what it can and PA8 must refuse - so
they are two paths on purpose, and they read the same pool entry.

## Final Architecture Review

### Findings

Ten defects, found by tracing each typed fact end to end and by 1500 generated
programs compared against `nsinit-ref` and `g++ -std=c++11`.

| # | Finding | Evidence |
| --- | --- | --- |
| F1 | A variable the program declares but never defines was appended to the image, shifting every later object.  The format holds "each **defined** named variable and **declared** function". | `extern int x; char c = 65;` gave 9 bytes against the reference's 5 |
| F2 | 5.19p2 needs the initialization that made an object constant to precede the use **in this translation unit**; a `const` object defined in another unit was read anyway | `extern const int n = 5;` / `extern const int n; int a[n];` accepted, `g++` and the reference refuse |
| F3 | The temporary 8.5.3p5 creates was never readable, so 5.19p2's third bullet was lost | `const int& r = 2; int a[r];` refused, `g++` and the reference accept |
| F4 | 3.4p1 and 7.3.4p3 ambiguity was not diagnosed: a lookup returned the first of two entities one level declares | six shapes, from two using-directives to an inline namespace against its parent, all accepted; `g++` and the reference refuse all six |
| F5 | 7.1.1p7 linkage agreement was not checked | `extern int x; static int x;` accepted, `g++` refuses |
| F6 | 7.1.1p4 lets `thread_local` name only a variable | `thread_local void f();` accepted, `g++` refuses |
| F7 | 7.1.5p3 asks a constexpr function for a literal return type | `constexpr void f();` accepted, the reference refuses |
| F8 | Redeclaring an overload through a qualified declarator-id walked the overload set | quadratic; 8000 out-of-line definitions cost 2.2x the same count of distinct names |
| F9 | The value of an integral literal was kept twice - in the token and in the pool - which made a token 16 bytes | 32 MB of declarators held 547 MB |
| F10 | Three statements of one fact: 5.19p2 readability in two files with a cached flag, 3.9.3p5 array qualification in a file-local helper, and the mock function size and alignment in both the type table and the image | read of the code |

Adjudicated against the handout and N3485 rather than followed, with `g++` as
the third opinion.  The reference is not an oracle outside the fixtures:

| Witness | Reference | Here |
| --- | --- | --- |
| `int x = 1; int y = x;`, `const double d = 1.5; double e = d;`, `const volatile int n = 5; int x = n;` | writes the folded value | 3.6.2 and 5.19: neither constant-initialized nor readable, so zero.  The reference refuses `int x; int y = x;` outright, which `g++` accepts, so its model is "every initializer must fold" rather than 5.19's |
| `constexpr int* p = nullptr; int* q = p;` | writes the address of `p` | the null pointer `p` holds |
| `bool b = 0.5;` | writes 0, and drops the object entirely for `bool b = 1e-2;` | 4.12: any non-zero value is `true` |
| `long double v = 1;` | writes uninitialized bytes in the six padding bytes of the object, differently on every run | writes zeros |
| `const char* p = "y"; char a[] = "x";` | block 3 holds `x` then `y`; a literal that initializes an array comes first whatever its token | the handout's "in order of their tokens", so `y` then `x` |
| `typedef char CA[4]; CA a = "abc";` | no string literal object, though `char a[4] = "abc";` gets one | a string literal object either way |
| `namespace N { void h(); } void N::h() {}` | two function objects, though its own answer for `const int A::x = 3;` is one variable | one: 7.3.1.2p1 defines the member, and PA8's `600-qualified-redeclaration` fixture agrees for a variable |
| `const int& r = 2;` with a converted initializer, and `int&& r = 3;` | refuses | 8.5.3p5 creates the temporary, as `g++` does |
| `const char (&r)[4] = "abc";` | binds to the literal and emits a second copy | binds to the literal, one object |
| `namespace C { using namespace A; const int x = 2; } C::x` | refuses as ambiguous | 3.4.3.2p2 asks the namespace itself first, as `g++` does |
| `int a[2.0];`, `typedef int A[2]; A f();`, `inline int x = 1;`, `typedef int T = 1;` | accepts | 8.3.4p1, 8.3.5p8, 7.1.2p1 and 7.1.3 refuse, as `g++` does |

Three boundaries the whole stage keeps, all shared with the reference: naming
an overloaded function needs 13.4, which no context of `pa8.gram` supplies;
`constexpr int f() {}` is left alone, because 7.1.5p3's return-statement
constraint cannot be met by the only function-body this grammar has; and a
name a type-specifier looks up is looked up filtered to typedef-names, which is
how the parser tells `T x;` from `int T;`, so `typedef int T;` in one namespace
against `int T;` in another at one level is taken as the typedef where `g++`
calls it ambiguous.

### Changes

| Finding | Change |
| --- | --- |
| F1 | `ProgramImage::layout` places what the image holds into `placed_` and passes over an undefined variable; `write` walks that one list instead of repeating the block loops |
| F2, F3, F10 | `Symbol::readable` and `readable_object` are gone: `InitSemantics::read_object` decides 5.19p2 on its own from what the symbol holds, requires the defining unit to be the current one, and so reads a temporary on the same terms as a named object.  `bind_reference` stamps the temporary's unit |
| F4 | `Namespace::level_starts_` records where each 7.3.4p2 level begins; both lookups collect a level before answering and `merge_found` applies 3.4p1 - one entity, all functions, or two typedef-names for one type, is not ambiguity.  A namespace that nominates nothing keeps the O(1) path |
| F5 | `Symbol::internal` records the 3.5p3 linkage the first declaration gave, and a later `static` that disagrees is refused |
| F6, F7 | `link_object` holds the specifier-applicability rules 7.1.1p4, 7.1.2p1 and 7.1.5p3 together |
| F8 | `overloads_` is keyed on the overload set's head entity rather than on (namespace, name), so `TranslationUnitModel::find_overload` answers both the unqualified and the qualified redeclaration |
| F9 | `SemaToken` is `{type, name}`; `LiteralValue::integral/integer/real` are the one way to read a literal, and `build_sema_tokens` always builds the pool, so `nsdecl` reads the same one |
| F10 | `TypeTable::object_cv` states 3.9.3p5 once; `ProgramImage::size_of/align_of` ask the type table, which already answers 4 for a function; `InitSemantics::to_bool` asks `convert` for 4.12 rather than repeating it; the floating literal decode moved beside the integral one |

### Performance Evidence

- The tables above are re-measured on this build.  Every workload doubles its
  time when its input doubles, except the two named as bounded.
- F8: out-of-line definitions of 2k/4k/8k overloads of one name went
  0.031 / 0.074 / 0.214 s to 0.023 / 0.042 / 0.080 s, which is now the same
  curve as 2k/4k/8k distinct names (0.024 / 0.045 / 0.088 s).
- F9: peak RSS fell 47% on the token-heaviest workload (32 MB of declarators,
  547 MB to 291 MB) and 13% on 64k function declarations (44 MB to 38 MB).
- F4 costs what 3.4p1 costs: a level with many nominated namespaces is now
  scanned to its end rather than to its first hit, which takes 8k directives
  and 8k lookups from 1.25 s to 2.99 s.  No workload without many
  using-directives in one namespace moved.
- 200 MB of image is written in 0.24 s in 4 MB of memory; the image is streamed
  from the symbols rather than assembled.

### Validation

- `make test-report-through-pa8`: 426/426, 8/8 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa8 --paths dev/src`: pass, 67
  files, and a clean `-Wall` rebuild with no warnings.
- **1700 randomly generated programs** of one and two translation units -
  every fundamental type against every literal form, character arrays, string
  literals, pointers, null pointer constants, references, typedefs, storage
  classes, nested and inline and unnamed namespaces, `static_assert`, array
  bounds read from const objects - run against `nsinit-ref` and against
  `g++ -std=c++11 -fsyntax-only`.  Not one disagrees with `g++` about whether
  the program is well formed, apart from `int a[0]`, where `g++` accepts a GNU
  extension that 8.3.4p1 and the reference both refuse.  With character arrays
  from string literals left out - the one block 3 ordering the reference does
  differently - 1100 of them produce a byte-identical image but for two, where
  the reference converts a small floating value to `bool` as zero.
- **467 hand-written witnesses** across linkage, conversions, initialization,
  declarator shapes, specifier combinations, namespaces and the output format,
  each compared against both.  Everything left is in the two tables above.
- Depth witnesses at the bound for parenthesized expressions, parenthesized
  declarators and nested namespaces: 9999 parses, 10001 is refused.
- Five regression fixtures added under `cppgm.tests/course/pa8/`, one per
  finding whose answer the reference agrees with: the image of a declared but
  undefined variable, a temporary read as a constant, a cross-unit `const`
  read, and unqualified and qualified ambiguity.

## Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `nsinit` full stage: literal pool, value model, ABI sizes, program image with 3.5 linkage and layout, clause 4 and 8.5 initialization, 5.19 constant evaluation, and `pa8.gram` expressions, initializers, `static_assert` and function definitions | 60/60 PA8; 361/361 through PA7; file audit clean; overload redeclaration made O(1); 265 generated programs compared against the reference and 56 ill-formed ones against g++ |
| C2 | PA-wide audit: the image holds only defined variables, 5.19p2 readability has one owner and one translation unit, 3.4p1 ambiguity is diagnosed, 7.1.1p7/7.1.1p4/7.1.5p3 are checked, qualified overload redeclaration indexed, the literal is one owner and a token one word | 426/426 through PA8; file audit clean; 1500 generated programs against the reference and g++; token memory down 47% on the heaviest workload; the last quadratic in a declaration's own path removed |

Nothing on this stage is open.  The next work is what a later PA needs from the
image - real machine code instead of the `fun` stub, and the rest of clause 5
in the expression analyser, which is where `InitSemantics` and
`decl_parser_expression.cpp` will grow.
