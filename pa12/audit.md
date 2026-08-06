# PA12 Audit — `cppgm++ --emit-semantics`

An independent review of the landed checkpoint, in the order a fact travels:
parse, name, region, type, value, dump.

## Current Checkpoint Review

**C2–C4 — the class layer: classes and members at `c6c3694a`, pointers to
members at `2ecd225e`, the `decltype(x)(1)` functional cast at `d90ed4f3`.**

The layer is a sound base for the template checkpoint above it. A member
function is the function 9.3.1p3 says it is — its declaration, its definition
and a pointer to it all read the same object parameter out of one type — a
member is found by one probe in the region its class declares, and a definition
the place it is written cannot hold is appended once to a list the end of the
unit walks. What the review changed is below; nothing in it moved a boundary.

### Ownership and lifetime

- **A definition was written through a reference into a list that grew under
  it.** `write_pending_definitions` walked `pending_` by index but handed
  `write_definition` a reference to the element, and reading that body may
  default-initialize an object and append the constructor it asks for. On
  `struct A{}; struct B{ void g(){ A a; } }; void h(){ B b; }` the append
  reallocated the vector and the loop then read `pending.body->children` out of
  freed memory — valgrind reports the invalid read at the reallocating
  `push_back`. The list is now a `std::deque`, which is what the model already
  uses for the entities and dump nodes it hands out references to while they
  grow. Valgrind is clean over the class fixtures and the probes.

- **`Context::node` says a member declaration writes no line, and two writers
  did not ask.** `struct C { union { int a; }; };` and
  `struct C { using X = int; };` both dereferenced a null node and crashed.
  9.5p1's injection now declares the union's object as the member it is and
  writes no line for it, and an alias-declaration writes none either. Every
  writer of a top-level line now honours the invariant its own comment states.

### Name and region

- **9.4p2: a static data member was read as a member of an object.** `s` in a
  member function of `struct C { static int s; }` wrote `member-expression` over
  `this`, and `sizeof(C)` counted the member's storage. Whether a declaration is
  reached through an object of its class is now one fact, `object_member`, set
  where the declaration is read and asked by the name that uses it, by the class
  layout, and by 5.3.1p3; 9.5p1's injected members are counted where the union's
  object is, not twice.

- **9.4.2p2: `int C::s;` declared a second variable in the class** rather than
  defining the one it names, which left two entities for one object and rebound
  the name to the one that knew nothing about itself. A declarator-id with a
  nested-name-specifier now defines the object that region already declares,
  as a function declarator already did.

- **5.3.1p3: `&C::x` threw `this` is written outside a member function.** The
  type existed and `&C::f` formed one, but the data member half of the same
  clause went through the implicit object access and failed there. `&` written
  on a qualified-id now names the member of the class, which is what forms a
  pointer to a data member, and writes the operand's line the way the fixture
  pins `&A::f`.

- **9.5p1 disagreed with itself.** A member of an anonymous union reached
  through an object expression was written directly on that object, while the
  same member named with no object expression was written through the object the
  union declared. Both now go through it, which is the one rule and the one the
  block-scope fixture pins.

### Boundaries the output used to keep quiet about

- **12.1p5: a class that declared its own constructor lost it.** The constructor
  was neither declared nor written, and an object of the class was default
  initialized by nothing, so the dump silently described a program the source
  does not have. PA12 chooses no constructor, so a class that declares a special
  member function is now refused where the semantics dump reads it, and PA11,
  which only spells the declaration, is unchanged.

- **8.5p6 and 3.9p6: an object of an incomplete class was accepted** and
  constructed by nothing. Default-initializing an object of class type now
  requires the constructor 12.1p5 gives a complete class.

### Confirmed intact

- 672 / 672 through PA11, and PA12 held at its 165 / 166 turn-start baseline
  with the same one open test. The file audit passes; its one warning, that
  `sema_analyzer.h` carries 201 body lines against a limit of 180, arrived with
  C2–C4 (199 at the checkpoint) and is the class's whole private API rather than
  implementation in a header. Splitting the analyzer belongs to the assignment
  that next grows it, not to this audit.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate or file-audit bypass. What PA12 does not model — a declared constructor,
  a member function call, a class-scope name a later assignment resolves — is
  refused where it is read and named in the diagnostic.
- Two spellings the refs pin and the code follows rather than derives: a class
  type is written with the regions around it (`struct n::S`) and an enumeration
  is not (`enum role`); and a cast to the member-pointer type an operand already
  has writes no `cast-expression` line while the same cast to a function pointer
  does.
- Scaling is linear in every axis the class layer added, measured on this host:
  8000 member functions defined in one class 0.11 s, 8000 classes each
  constructed 0.25 s, 8000 anonymous unions 0.31 s, 8000 member accesses through
  `this` 0.13 s, 8000 `&C::m` 0.15 s, each about 2.2x its half. Depth costs the
  PA10 parse guard rather than time: 800 nested class definitions 0.02 s, 640
  nested `decltype(...)( )` casts 0.06 s, where the specifier is skipped by a
  balanced token scan rather than a parse.

### Durable architecture decisions

- One analyzer serves both dumps: a declaration is read once and written to
  whichever tree `SemaDialect` asks for.
- A name binds the head of the declaration chain of one function name in one
  region; the chain is indexed by parameter type list for declaring and walked
  in order for resolving.
- A `Value` carries one analysed expression up from operand to operator,
  including the dump line it wrote, so a conversion rewrites that line in place
  rather than the output being built in a second pass.
- A fact about a type alone belongs to `TypeTable`; a fact about a declaration
  belongs to `SemaEntity`, built where the declaration is read. One of them
  answers one question: `object_member` is asked by the name, the layout and the
  pointer to member alike.
- 9.3.1p3's object parameter lives in the function's type, so everything above
  reads a member function as the function it is.
- A definition the place it is written cannot hold is appended once to
  `pending_`, which the end of the unit walks and which a body it reads may
  append to, so it holds its elements still.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1 | `--emit-semantics` spine: dump tree, declarations, statements, expressions, conversions, overload resolution, diagnostics | argument conversions written out of order; an unresolved overload set accepted in five discarding contexts; `&f` refused; enumeration comparisons rejected; `--` on `bool` accepted; O(N²) overload declaration; callee looked up twice; two type-fact owners | pa12 156/166 held; pa1–pa11 672/672; 16000 overloads 3.05 s → 0.26 s; file audit clean |
| C2–C4 | classes and members, pointers to members, `decltype(x)(1)` | a pending definition written through a reference into a reallocating list; a null node dereferenced by an anonymous union and by an alias in a class; a static data member read through `this` and counted in the layout; `int C::s;` declaring a second object; `&C::x` refused; an anonymous union member written two ways; a declared constructor and an incomplete class silently accepted | pa12 165/166 held; pa1–pa11 672/672; valgrind clean; linear to 8000 members, classes, unions and `&C::m`; file audit passes, one header-weight warning |
