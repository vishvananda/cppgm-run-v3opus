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
| `LiteralValue` pool | `sema_token.*` | a literal token's type and object representation, built only when a tool asks for it |
| `ConstValue` / `ExprValue` | `value_model.h` | 5.19 what is known of a value, and 3.10 the category and type of an expression |
| `TypeTable::object_size/align` | `type_model.*` | the course ABI size and alignment of a type, and 3.9p6 completeness |
| `Symbol` / `ProgramImage` | `program_model.*` | one object of the image: 3.5 identity across units, its constant initializer, its place in the file |
| `InitSemantics` | `init_semantics.*` | clause 4 standard conversions, 8.5/8.5.2/8.5.3 initialization, 5.19 constant evaluation |
| `DeclParser` (extended) | `decl_parser*.cpp` | `pa8.gram`: expressions, initializers, `static_assert`, function definitions, 7.1.1 storage class, 3.4.3p3 scope change |

Data flow: source file -> `SemaToken` array + literal pool -> `DeclParser` ->
(`TranslationUnitModel` for one unit, `ProgramImage` for the whole program) ->
layout -> image bytes.  Nothing flows back into the token stream, and the only
state shared between units is the name table, the type table and the image.

Four decisions drive the shape:

- **The image is the program-wide owner, the model stays per unit.**  A
  namespace is a declarative region of one translation unit, so the models stay
  separate; `ProgramImage` keys an external-linkage declaration on (interned
  namespace path, name, signature) and hands back the same `Symbol`, which is
  what makes `extern int x;` in one unit and `int x = 3;` in another one
  object.  Symbols are appended when first declared, so block 1 is already in
  "order of first declaration within the program".
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

## Current Failure Map

PA8 passes 60/60; nothing in the stage's own suite fails.  What the stage
answers, by the compiler behaviour each group needed:

| # | Group | Behaviour | State |
| --- | --- | --- | --- |
| G1 | image and layout | ABI size/alignment, block order, zero padding, `fun` stubs | done |
| G2 | expressions and initialization | clause 4 conversions, 8.5, Table 10 validity | done |
| G3 | strings and arrays | 8.5.2, block 3, 8.5p6 | done |
| G4 | references and temporaries | 8.3.2, 8.5.3, block 2 | done |
| G5 | linkage and definitions | 3.5, 3.2, 7.1.1, 7.1.5 | done |
| G6 | constant expressions | 5.19, 3.4.3p3 | done |
| G7 | namespace diagnostics | 7.3.1, 7.3.2p3, 7.3.3p8, 7.3.1.2p2 | done |
| G8 | scale | 723 nested namespaces, 600 initialized variables | done |

## Active Checkpoint

None.  PA8 is complete; C1 below records what it covered.  The next work on
this stage is what a later PA needs from the image - real machine code instead
of the `fun` stub, and the rest of clause 5 in the expression analyser, which
is where `InitSemantics` and `decl_parser_expression.cpp` will grow.

## Performance Model

Dominant operations, in the order they cost:

| Path | Shape | Complexity |
| --- | --- | --- |
| phases 1-7 | PA1-PA5 lexing, macro expansion, spelling interning | linear in bytes |
| declaration parsing | one forward pass; backtracking is one `(`-lookahead per declarator | linear in tokens |
| 3.5 linkage | one hash probe on an interned namespace path per declaration; the path is interned once per namespace and cached | O(1) amortised |
| function redeclaration | one probe on (namespace, name, signature), not a walk of the overload set | O(1) amortised |
| initialization | clause 4 walks the pointer chain of the two types once | O(pointer depth) |
| constant evaluation | an lvalue-to-rvalue read is one indexed lookup of the object's kept value | O(1) |
| layout and output | one pass over the three blocks, then one pass writing them | O(objects + image bytes) |

Measured (`-O3`, one core):

| Workload | Time | Peak RSS |
| --- | --- | --- |
| 50k / 100k / 200k initialized variables | 0.18 / 0.42 / 0.97 s | 29 / 55 / 105 MB |
| 25k / 50k / 100k string literals behind pointers | 0.14 / 0.29 / 0.65 s | 21 / 38 / 73 MB |
| 25k / 50k / 100k function declarations | 0.06 / 0.16 / 0.38 s | 15 / 27 / 50 MB |
| 2k / 4k / 8k / 32k overloads of one name | 0.01 / 0.02 / 0.06 / 0.29 s | 6 / 8 / 12 / 35 MB |
| 2k / 4k / 8k nested namespaces + 200 declarations | 0.00 / 0.01 / 0.02 s | 6 / 7 / 10 MB |
| 20k / 40k array bounds read from one const | 0.06 / 0.14 s | 13 / 22 MB |
| 200 units x 500 external names (100k declarations) | 0.14 s | 6 MB |
| 80 MB image from one array | 0.10 s | 4 MB |

Every shape is linear in its input.  The overload set was the one exception:
`declare` used to scan the whole set to find a matching signature, which is
quadratic in the functions one name reaches - 8000 overloads took 0.22 s and
32000 would have taken about 3.5 s.  `TranslationUnitModel` now keeps the
overload index above, and the same 32000 take 0.29 s.  The list the entities
still form is only so that an expression can see that a name reaches more than
one function, so a new member goes in behind the binding rather than at the end
of it.

Memory is flat in the number of translation units: 200 units declaring the same
500 external names hold 500 objects, because linkage is a probe into one table
rather than a copy per unit.  The per-unit namespace cache in `ProgramImage` is
dropped at each `begin_unit`, since a `Namespace` dies with its model and its
address would otherwise be reused by the next one.

Depth is bounded at 10000 open frames, shared with PA6 and PA7, and now also
counts a parenthesized expression: 9999 nested parentheses parse in 1-2 MB of
the 8 MB default stack, and 10001 are refused.  Every other unbounded chain -
the type an object's size is read off, the namespace path of a declaration, the
pointer chain a qualification conversion walks - is a loop.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa8'`: 60/60.
- `make test-report-through-pa7`: 361/361.
- `perl scripts/cppgm_file_audit.pl --stage pa8 --paths dev/src`: pass, and a
  clean `-Wall` rebuild with no warnings.
- **265 generated well-formed programs** - every fundamental type against every
  literal form, pointers and null pointer constants, arrays and string
  literals, references, storage classes and linkage across two units, function
  and declarator shapes, `static_assert`, cv and typedef interactions -
  compared byte-for-byte against `nsinit-ref`.  251 match exactly; none is
  rejected that `g++ -fsyntax-only` accepts.
- **56 ill-formed programs** that match `pa8.gram`: every one that
  `g++ -fsyntax-only` rejects, `nsinit` rejects.
- Depth witnesses at the bound for parenthesized expressions, parenthesized
  declarators and nested namespaces.

### Where the reference is not followed

`nsinit-ref` is not an oracle outside the checked-in fixtures, and the sweep
found four places it answers differently.  Each was adjudicated against the
handout and N3485, cross-checked with g++:

| Witness | Reference | Here |
| --- | --- | --- |
| `long double v = 1;` | writes uninitialized bytes in the six padding bytes of the object, differently on every run | writes zeros |
| `const char* p = "y"; char a[] = "x";` | block 3 holds `x` then `y` | the handout's "in order of their tokens", so `y` then `x` |
| `typedef char CA[4]; CA a = "abc";` | no string literal object, though `char a[4] = "abc";` gets one | a string literal object either way |
| `namespace N { void h(); } void N::h() {}` | two function objects | one: 7.3.1.2p1 defines the member, and PA7's own `250-outside-def` fixture agrees |
| `const int& r = 2;` with a converted initializer, and `int&& r = 3;` | refuses | 8.5.3p5 creates the temporary, as g++ does |
| `const char (&r)[4] = "abc";` | binds to the literal and emits a second copy | binds to the literal, one object |

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| C1 | `nsinit` full stage: literal pool, value model, ABI sizes, program image with 3.5 linkage and layout, clause 4 and 8.5 initialization, 5.19 constant evaluation, and `pa8.gram` expressions, initializers, `static_assert` and function definitions | 60/60 PA8; 361/361 through PA7; file audit clean; overload redeclaration made O(1); 265 generated programs compared against the reference and 56 ill-formed ones against g++ |
