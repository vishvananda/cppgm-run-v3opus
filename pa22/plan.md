# PA22 Plan — `cppgm++ --emit-lowir`, the template entity and specialization graph

## Stage Design

PA22 finishes the *declaration* half of templates: what template entities exist,
what specializations exist, which one a naming selects, and which declaration
owns it. The lowering surface is PA21's — nothing new reaches LowIR except more
of the source language reaching it at all.

The owners PA19–PA22 left standing, extended rather than replaced:

- `ast_parser_*.cpp` with `ast_names.h` — the syntax boundary, and the one fact
  the parse has about a name no scope it models declares. 14.2's `<` is settled
  from it; `keep_spelled` holds the tree of a `decltype`, a `noexcept` and a
  parenthesized `sizeof` operand under the spelling that flattens it. The arena
  owns the nodes of a unit, so it is also where the syntax the *analysis* builds
  (5.1.2p3's closure class) is owned.
- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. What a place *is* is settled once in 14.6.1p1's own region,
  opened by the first reading that needs it — 14.5.6.1p5's comparison of two
  heads is one of those.
- `sema_template.h/.cpp` — the entity graph: `TemplateInfo`, `instantiate_class`
  / `specialize`, and `substituted` as the one door a dependent type returns
  through. `record_template` is 14.5.4p1's tier for a friend head.
- `sema_template_signature.h/.cpp` — 14.5.6.1p5 alone: the one canonical form
  four tiers compare, and 13.1's own index of every declaration of a name.
- `sema_pattern.h/.cpp` — 14.6p8's reading of a definition for what it *says*,
  and `PatternReading::record`, which reads a member definition again for every
  specialization already made.
- `sema_lambda.h/.cpp` — 5.1.2: the closure class is *made* as a class-specifier
  and handed to the class reading, so the local class, its ABI name, its layout,
  its call operator and the demand-driven definition of that operator are the
  machinery already there. What is 5.1.2's own is which class one
  lambda-expression makes and what the expression is worth. One body is read
  *twice* - 5.1.2p4's return type in the declarator's own regions and the
  compound-statement at 9.2p2's closing brace - so `reading_region` is what
  `SemaModel::closure_of` and `closure_object_of` are keyed by beside the
  terminals: the outermost region one function declarator opens, walked through
  a closure class because `SemaEntity::closure_class` says that class is a
  region this reading made rather than one the program wrote.
- `sema_access.h/.cpp` — 11.2's reach and 11.3's grant as one reader.
- `sema_lifetime.cpp` with `lowir_lower_unwind.cpp` — 12.2p1's temporaries and
  15.2p2's regions. A region opens where the step that made a throwing call
  began and closes where the live set changes; `close_region_at_step` ends one
  where the step began only where its handler owes *nothing*, because a handler
  that owes an object is exactly what an exception out of that step needs.
  `SemaAnalyzer::vacuous_destruction` is 12.4p8's one door, and what it reads is
  a body this unit's own source wrote - `note_definition_body` settles that and
  14.7.2p10's own question from the node it found, because a class completes
  before a member defined below it is read.
- `lowir_abi.cpp` / `abi_mangle.cpp` — the ABI record for one entity.
- `lowir_lower_body.cpp` with `LowValue::storage_owed` — 3.2p3 at the naming.

## Current Failure Map

**375 / 377** — 307 of the 309 `tests/` fixtures plus all 68 `course/pa22`
ones. Both remaining failures are the reference's own:

| # | Group | Owner | Signature |
|---|-------|-------|-----------|
| 2 | the reference drops an initializer this build writes | **the reference's** | `T x = a + b;` over an *empty* class and an **operator-syntax** call: the reference emits no call at all, where `T x = operator+(a, b)`, `T x = a.m()` and the same program over a class with one member are three shapes it does write. Probed at 12 spellings |

Known gaps probed and deliberately left:

- **5.1.2p4 through a name the body declares.** The deduced return type is read
  where 8.3.5p2's trailing-return-type is read — before the body's own region
  exists — so any lambda whose returned expression names something its body
  declared is `no declaration of … is in scope` where both oracles read it.
  This is **four of twenty** ordinary shapes and not an exotic one:
  `[]{ int t = 0; …; return t; }`, `[]{ held h; return h.v; }`,
  `[]{ pair q; …; return q; }` and a `return` out of a handler are all refused.
  A body-first reading is what it wants: the return type would have to be
  settled where the `return` is read and the function's type rebuilt after it,
  which is a checkpoint of its own. `[](int a){ return a + 1; }`, a trailing
  return type, `if` around the `return`, and a nested lambda all read.
- **5.1.2p1's captures.** Anything but `[]` is refused by name: a capture is a
  member of the closure and reading one as captureless would let the body name
  the enclosing function's own storage from a class holding none of it.
- **5.1.2p6's conversion to a pointer to function.** `[]{…}()` and a namespace
  scope initializer are the two shapes the reference lowers *through* it — it
  emits the call operator with no `this` and calls a decayed address of it —
  where this build calls `operator()` on a temporary. Both run the same.
- **The closure's own ABI name.** The reference writes the ABI's
  `<closure-type-name>` (`UlvE_`) where the enclosing function is a template
  specialization and clang's `$_0` where it is not; this build writes the
  unnamed-local-type form (`Ut_`, `Ut0_`, …), which tells two closures apart but
  pairs with neither. Two closures of one shape in two specializations therefore
  pair by *order* in the relaxed comparison and can swap. `emit_closure_name`
  already writes the form; what it wants is a closure fact on the type carrying
  the call operator's parameter list.
- **An implicit copy constructor nothing calls.** Where a class holds or
  derives from one whose empty destructor an included file defined, the
  reference *defines* the held class's implicit copy constructor even though no
  call names it; this build writes the calls the same way and omits the unused
  definition. Both are weak, so no unit is short a symbol.
- **15p1's try-block** is in `pa22.gram` and the parse takes it, and the
  analysis refuses it as `a statement is outside the PA12 subset`. It is no part
  of 15.2p2's regions, which the lowering opens for the cleanup an unwind owes.
- 9.5p2's implicit deletion of a union's special members: `g++` refuses,
  `pa22/cppgm++-ref` and this build accept. No fixture pins it.
- 12.4p8 at a union whose variant member's class declares a destructor: the
  reference writes and calls an empty union destructor; this build writes
  neither; `g++` refuses both programs.
- 9.5p3's `static` at the storage object's name, 3.6.2p2's folded initializer of
  a namespace-scope anonymous union, 9.5p1's layout inside a union (the
  reference lays an anonymous struct's members at offset 0 where `g++` and this
  build lay them out), and 12.6.2p8 at two members of one union: four shapes
  where the reference alone differs. `g++` agrees with this build at each.
- A non-type place of pointer or reference to function is outside the PA20
  subset; `sizeof(&f<T>)` is refused by `g++` and folded by both builds.

## Active Checkpoint

**AA made 5.1.2's captureless lambda a class the program never wrote, and the
AA audit gave it the reading of a body it stands in — see the ledger.** What the
PA still holds is the 2 the reference itself drops, so the next one is **AB**: the closure's own ABI name, which is the last thing about a
closure this build spells differently from the reference and the one that makes
two closures of one shape pair by order.

- Owner. `type_model.h` for the fact a closure type carries — 5.1.2p3's class is
  no unnamed local type but a class the ABI has a name form of its own for — and
  `lowir_abi.cpp`, which turns a type into the record `abi_mangle.cpp` encodes.
  `sema_lambda.cpp` writes the fact where it declares the class.
- Data flow. One fact on the class's type node: that it is a closure, and the
  call operator's parameter type list, which `emit_closure_name` already takes.
  `local_occurrence` stays the discriminator. Nothing else reads it.
- Expected complexity. One entry per closure class, written once where the class
  is declared and read once per mangling of it; no walk of the declarations.
- Validation. The 15-shape sweep re-run through the real comparator, where the
  two that pair two identical closures by order currently fail; the whole pa22
  suite; and `abimangle` against `abimangle-ref` on the encoded names.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp`, against
`pa22/cppgm++-ref`. Traps recorded rather than re-measured: `timeout`/`date`
spawned per run invents a ~0.1 s floor; `cppgm++` run by hand needs `-o` or it
compiles nothing; the whole corpus handed to one process is one ill-formed unit
and times as 0.00 s; `/usr/bin/time` writes to stderr; `g++ file.t` treats a
`.t` as a linker input and `-x c++` after the file has no effect; `bc` is
absent; `make ref-test` needs `TEST=` spelled **relative**; a git worktree
cannot be added under `/home/vishvananda/work`; `cppgm++ … | head` reports the
*pipeline's* status, so a segfault reads as exit 0; the first pass over a corpus
measures the page cache (2.62 s cold against 1.53 s warm); and `CPPGM_APP_ARGS`
is read by the perl harness and not by the binary. Every generated input is
checked for exit 0 before it is timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| **AA** n captureless lambdas in one function body | 50 → 400 | 0.01 → 0.06 s | 0.57 → 1.03 s |
| **AA** n specializations of one body holding one lambda | 50 → 400 | 0.01 → 0.08 s | 0.60 → 1.23 s |
| **AA audit** lambdas nested d deep, each in the one before's returned expression | depth 8 → 64 | 0.00 → 0.26 s | 0.60 s at 8, 3.71 s at 16, killed at 60 s at 32 — the pre-audit build declared 2^d closure classes and was 11.20 s at 16 |
| **AA audit** n temporaries with destructors in one full-expression | 25 → 200 | 0.00 → 0.01 s | 0.60 → 0.80 s |
| **AA audit** n classes whose empty destructor a header defined | 50 → 400 | 0.02 → 0.05 s | 0.60 → 0.80 s |
| **U** a chain of d classes over a dependent base, each naming through itself | depth 100 → 3200 | 0.01 → 4.69 s | 0.55 → 1.03 s — the one path slower than the reference, and quadratic in the *chain*, which no fixture writes past depth 4 |
| **C** a `sizeof` of a specialization nested d deep in its own operand | depth 8 → 128 | 0.00 → 0.01 s | 0.60 s flat; the pre-C build was killed at 60 s at depth 24 |
| **B** 11.2p1's protected base chain of depth d, 200 accesses | depth 64 → 512 | 0.01 → 0.07 s | 0.55 → 0.69 s (0.97 s before the walk was one per class) |
| **O** a member class template nested d deep, each level defined out of class | depth 4 → 40 | 0.00 → 0.09 s | 17.78 s and **7.99 GB** at 24; killed at 32 |
| **F** a hidden friend chain of n declarations | 800 → 3200 | 0.79 s at 3200 | — (15.64 s before `Scope::hidden_index`) |
| the whole 377-test corpus, one process per file in a shell loop | — | 3.02 s warm (3.11 s at the pre-AA build; the earlier 1.53 s was measured another way) | — |

## Completed Checkpoints

| Checkpoint | What landed | Pass count |
|------------|-------------|-----------|
| **T, T2–T5, T audit** | 14.1p2's template place: a `template<…> class` parameter binds a template, `TypeKind::TemplateName` is interned per declaration, 14.3.3p1 matches two heads by kind and by a value place's signature, and 3.4.2p2 gives an argument the region declaring the template it named. | 156 / 308 |
| **A, A audit** | 14.5.7p1's alias template as a `Typedef` carrying a `TemplateInfo`, and the three regions a template-id is looked up in. | 193 / 308 |
| **P, P audit** | 14.2p4's optional keyword, answered from `DeclaredNames::names_a_template` and bounded to a list a `(` follows; 14.7.2p2 asked of what `extern template` wrote. | 200 / 308 |
| **M, M2, M audit** | 14.5.2's member template at its four definition exits, 3.4.1p8's head inside the region its declarator-id names, and 12.3.2p1's conversion function template through `Deduction::from_conversion`. | 229 / 318 |
| **F, F audit** | 14.5.4's friend templates: `befriended` asks the pair as spelled and then by `primary`; a friend definition is read twice and 3.4.1p10 reads it where it was written; `Scope::hidden_index` took a chain from 15.64 s to 0.79 s at n = 3200. | 249 / 326 |
| **D, D audit** | 8.3.5's function type as a template argument: `SpelledTypeId` learned 14.5.3p4, 8.3.5p7's trailing qualifiers and 8.3.5p5's adjustment, and a ref-qualifier became part of the name two overloads are told apart by. | 267 / 332 |
| **O, O audit** | 14.6.1p1's current specialization of a *partial* specialization, keyed in `TemplateInfo::patterns`, and 14.5.2p3's own tier for a member of one. | 285 / 336 |
| **X, X audit** | 14.7.3p1's explicit specialization of a member: `instantiated_definition` under an `instantiating_pattern_` depth, and `note_object` split from 5.19p2's own question. | 295 / 338 |
| **R, R audit** | 14.1p2's names an out-of-class definition wrote: `TemplateInfo::reading_region` and `Member::carried`, asked once at the door every queued body passes. | 306 / 343 |
| **C, C audit** | 5.19 read out of one spelling at the five operators the reader had no answer for: 5.18p1's comma, 5.2.9p4's discarded cast, 14.5.3p4's expansion found before its pattern is read, 14.2p4's keyword inside a component, and 5.3.3p1's `sizeof` over an expression — with `TypeTable::object_align`, `SemaAnalyzer::align_of` and `require_settled_type` under it. A `sizeof` nested 24 deep went from killed at 60 s to 0.01 s at depth 128. | **318 / 348** |
| **B, B module split, B audit** | 11.2p4 answered about a member *as a member of the naming class*: `Access::base_path` walks down asking each base-specifier, `resolve_prefix` asks it of every component, and five clauses no door enforced landed with it. The walk cost d² before it cost d. | **336 / 357** |
| **U, U audit** | 14.6.2.1p6's member of an unknown specialization at all three walks that look a component up; 7.1.6.2p4's dependent id-expression; and `LowValue::storage_owed`, which puts 9.4.2p3's storage in the program only where a use takes the address. `holds_written_definition` is 14.7.3p1 read in the other source order. | **342 / 361** |
| **N, N audit** | The four calls 13.3 had to resolve before it had a set: 8.1p1's type-id with no declarator-id, 3.4.2p3's deferred naming, 13.3.2p2's ellipsis-only clause, and 5.2.2p1's call on an object whose class this unit had not been asked for. `TemplateSignature` came out whole and is 13.1's index key. | **349 / 364** |
| **S, S audit** | 14.2 at the three readings 5.19 makes of one name and the fourth 7.1.6.2p4 makes: `folded_name` is the one door, `names_specialization` is 14.8.1p2 at one entry, and 5.2.1p1's subscript is one walk over the same words at both readings. | **354 / 367** |
| **L, L audit** | 14.7.2p9's `extern template` at its three tiers through `instantiation_suppressed`; what a discarded expression is worth at the comma and the array; and 5.2.3p3's `T{...}` over an array, which had run one of three calls. | **362 / 371** |
| **W, W audit** | 15.4p1 read as a fact of one call rather than of the step it stands in: `note_call` with `pending_calls_`, `throwing_since_mark_`; 9.3.1p3's object reached through the class the nested-name-specifier named; and `PendingDefinition::returned_object_chain`, bounded at both ends. | **367 / 372** |
| **Y, Y audit** | 8.3.4p1's bound as a bound *and* a place: `TypeTable::Node::bound_place`, written by both readers of the clause and read by `substituted_array`, `match_bound` and 14.5.5.2's ordering. `collect_packs` and 14.8.2.5p17 were the two walks the new edge had not reached. | 367 → **369 / 373** |
| **Z, Z audit** | 9.5p1's anonymous aggregate as the object no name reaches: `SemaEntity::anonymous_storage` and `collect_member_targets` with `one_of`, walked by 12.6.2p8, 12.4p8 and 12.6.2p2 alike. The audit made 12.6.2p2 and 8.5.1 answer *which subobject* alike, and 8.5.1p15's `one_per_union` fixed an array of aggregates that had run to the wrong value. | 372 → **374 / 377** |
| **AA, AA audit** 5.1.2's captureless lambda, 12.8p11's boundary and the step a region may not cover | A lambda-expression was `an expression is outside the PA12 subset`: this build had no closure type at all. 5.1.2p3's class is now *made* — `LambdaReading` builds the class-specifier the standard describes and hands it to the class reading, so the local class, its ABI name, its layout, its call operator's declaration, 9.2p2's reading of the body at the closing brace and the demand-driven definition are the machinery already there; `SemaModel::closure_of` keys one class per lambda-expression by the reading of the body it stands in and the terminals it was written from, which is what the parse now records on the node. p14's initialization is *nothing* for a lambda that captured nothing, so the temporary carries no constructor-action and `temporary_object` gives it storage and writes no call — where 5.1.2p19 leaves no default constructor for one to stand for. `AstKind::DeducedReturnType` is p4 read at 8.3.5p2's place. Under the fixture were two clauses of its own. 12.8p11's boundary asks 12.4p8 of a destructor, and this build answered from a body it had gone looking for: a destructor whose definition this unit's own source did not write is one no reading here may read the emptiness of, and one written *outside* its class is one the boundary in particular may not — `Q { int x; ~Q(){} }` written in a header is returned through a place and written in the unit's own source is returned as bytes, the reference's answer at every shape probed. And 15.2p2's region covered the step that *built* the object it was opened for: `close_region_at_step` ends it where that step began, moving the step's own instructions out, where the handler it carries owes nothing; `naming_storage_` keeps the naming of a new object's storage in front of the region, and `note_call`'s `building` keeps a nonthrowing construction of 12.2p1's own temporary from opening one. 19 shapes swept: 16 accepted (`g++ -pedantic-errors` accepts 19 and the 3 refusals are the recorded capture and return-type gaps), 14 run through `lowir2cy86` + `cy86` and every one returns exactly what `g++` returns, valgrind-clean; 15 of them compared against `pa22/cppgm++-ref` through the real comparator, 10 byte for byte and the 5 differences two recorded causes - 3 are 5.1.2p6's immediate invocation and 2 are the closure's ABI name pairing two identical closures by order.  The audit found the reading a class is held under, not the class: one body is read twice, so a lambda in the returned expression declared one class per reading and 8 nested lambdas came to **255** classes and 11.20 s at 16 - `reading_region` is the outermost region one function declarator opens, walked through a closure class, and depth 64 is now 0.26 s where the reference is killed at 60 s at 32.  Beside it three siblings the two new clauses had not reached: 12.4p8's provenance was written at 12.8p11's boundary and at no end of a lifetime, so a class whose empty destructor a *header* defined was destroyed by nobody at nine shapes the reference destroys; 15.2p2's region ended where a step began however much its handler owed, running a constructor outside the handler that owes the temporary in front of it; and 5.1.2p4's walk read a `return` written in a class 9.3p1 let the body declare, so a closure returning `int` came out `i8` and **ran to 1 where both oracles run 0**.  12.2p1's object is now held beside the class, and a temporary with nothing under it creates its object where the place asking owns storage. | 374 → **375 / 377** |
