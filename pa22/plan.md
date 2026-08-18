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
  parenthesized `sizeof` operand under the spelling that flattens it. `declared_`
  answers what the unit declared of a name written in a scope and `reachable_`
  what a *prefix* could reach, which is what keeps 10.2p2's walk of a base chain
  off every name that is going to miss. The arena owns the nodes of a unit, so it
  is also where the syntax the *analysis* builds (5.1.2p3's closure class) lives.
- `sema_template_head.h/.cpp` — `TemplateHead`, 14.1p2's head and 14.3p1's
  argument list. What a place *is* is settled once in 14.6.1p1's own region,
  opened by the first reading that needs it — 14.5.6.1p5's comparison of two
  heads is one of those.
- `sema_template.h/.cpp` — the entity graph: `TemplateInfo`, `instantiate_class`
  / `specialize`, and `substituted` as the one door a dependent type returns
  through. `record_template` is 14.5.4p1's tier for a friend head.
- `sema_explicit.cpp` — 14.7.2 and 14.7.3, the two clauses that write one
  argument list *out*: what a specialization is (`explicit_classes`,
  `explicit_functions`, `explicit_variables`, `explicit_member_classes`) against
  what the pattern would have said, and which unit's object file owes its
  definitions. Both are keyed by the interned argument list the specialization is
  already found by, so a reading asks one hash lookup on a number it holds.
- `sema_specialize.h/.cpp` — 14.5.5.1p1's choice among the patterns, and
  14.7.3p1 read in both source orders: `supersede`, `note_object`,
  `require_replaceable` and `holds_written_definition`.
- `sema_template_signature.h/.cpp` — 14.5.6.1p5 alone: the one canonical form
  four tiers compare, and 13.1's own index of every declaration of a name.
- `sema_pattern.h/.cpp` — 14.6p8's reading of a definition for what it *says*,
  and `PatternReading::record`, which reads a member definition again for every
  specialization already made.
- `sema_type_id.cpp` and `sema_value_expression.cpp` — the two readings that
  recover terminals from a flattened spelling, because 14.2 writes an argument
  list *inside a name*. 8.1p1's type-id is one and 5.19's constant expression the
  other; what they share is `pp_number_end`, 2.9p1's run, and the bound one hands
  the other is passed whole rather than split and rejoined.
- `sema_lambda.h/.cpp` — 5.1.2: the closure class is *made* as a class-specifier
  and handed to the class reading, so the local class, its ABI name, its layout,
  its call operator and the demand-driven definition of that operator are the
  machinery already there. One body is read *twice* — 5.1.2p4's return type in the
  declarator's own regions and the compound-statement at 9.2p2's closing brace —
  so `reading_region` is what `closure_of` and `closure_object_of` are keyed by.
- `sema_access.h/.cpp` — 11.2's reach and 11.3's grant as one reader.
- `sema_elision.h/.cpp` — 12.8p31's copy that is not made, 12.8p15's copy that
  carries no byte, and 8.5p14's reading of the form the initializer was written
  in.
- `sema_lifetime.cpp` with `lowir_lower_unwind.cpp` — 12.2p1's temporaries and
  15.2p2's regions; `SemaAnalyzer::vacuous_destruction` is 12.4p8's one door.
- `lowir_abi.cpp` / `abi_mangle.cpp` — the ABI record for one entity.
- `lowir_lower_body.cpp` with `LowValue::storage_owed` — 3.2p3 at the naming.

## Current Failure Map

**380 / 380 — PA22 passes.** 309 `tests/` fixtures and 71 `course/pa22` ones.
No failure group is open. `make test-report-through-pa22` is 2948 / 2948.

Known gaps probed as programs at the final audit and deliberately left:

- **PA10 flattens the space out of a template-argument-list.** `box<k >= 3>`
  reaches every semantic reading as `box<k>=3>`, so the `>` that was 5.9's
  operator is 14.2's delimiter and `hold<box<k < 4> >` is refused where both
  oracles read it. The four remaining shapes of the audit's 23-expression sweep
  are this one cause. It is not fixable below the AST: which `<` of a flattened
  name opens a list needs a lookup, and balancing says only *how many* are the
  operator. The unnested spellings — `box<k << 1>::v`, `box<k < 4>::v` — all read.
- **14.7.1p1's member class definition is instantiated eagerly.** `struct in {
  typename T::nope x; };` inside a class template makes `outer<int> a;` refuse
  where p1 instantiates the *declarations* of member classes and both oracles
  accept. The README puts "the remaining no-eager-instantiation … work" out of
  PA22's scope, and a member function's body already waits for its use.
- **5.1.2p4 through a name the body declares.** The deduced return type is read
  where 8.3.5p2's trailing-return-type is — before the body's own region exists —
  so `[]{ int t = 0; return t; }` is `no declaration of t is in scope`. 9.2p2
  holds an in-class body until the end of the unit, so settling the type at the
  body's own reading needs the closure's operator read eagerly and its type
  rebuilt after: a checkpoint of its own, and 5.1.2 is no part of PA22's boundary.
- **5.1.2p1's captures.** Anything but `[]` is refused by name: a capture is a
  member of the closure, and reading one as captureless would let the body name
  the enclosing function's storage from a class holding none of it.
- **5.1.2p6's conversion to a pointer to function**, and **the closure's own ABI
  name**, which writes the unnamed-local-type form (`Ut_`) where the reference
  writes `<closure-type-name>`, so two identical closures pair by order.
- **15p1's try-block** is in `pa22.gram` and the parse takes it; the analysis
  refuses it as outside the PA12 subset. It is no part of 15.2p2's regions.
- Shapes where `pa22/cppgm++-ref` alone differs and `g++` agrees with this
  build: an implicit copy constructor nothing calls; the ABI of a class with a
  user-provided copy constructor returned by value; `new T()` over an empty
  class; 9.5p2's implicit deletion of a union's special members; 12.4p8 at a
  union; 9.5p3's `static`; 3.6.2p2 at a namespace-scope anonymous union; 9.5p1's
  layout inside a union; 12.6.2p8 at two members of one union; the parameter type
  of a member template specialized for one class specialization (the reference
  writes `_ZN5ownerIcE1mIiEEiT_` and gives it an `i8` parameter). No fixture pins
  any of them.
- 12.8p31's side effects at an empty class: an initializer the copy never reads
  is never evaluated, so a dropped operator call's side effects are lost. This
  build and the reference both run to 1 where `g++` runs 2, and the `.ref` files
  pin the reference's reading.
- A non-type place of pointer or reference to function is outside the PA20
  subset; `sizeof(&f<T>)` is refused by `g++` and folded by both builds.

## Performance Model

Best of three with `/usr/bin/time` on generated inputs under `/tmp`, measured on
the audited build against `pa22/cppgm++-ref`. Traps recorded rather than
re-measured: `timeout`/`date` spawned per run invents a ~0.1 s floor; `cppgm++`
run by hand needs `-o` or it compiles nothing; the whole corpus handed to one
process is one ill-formed unit and times as 0.00 s; `/usr/bin/time` writes to
stderr and needs its stdout thrown away inside `$(...)`; `g++ file.t` treats a
`.t` as a linker input; `make ref-test` needs `TEST=` spelled **relative**; a git
worktree cannot be added under `/home/vishvananda/work`; `cppgm++ … | head`
reports the *pipeline's* status; and the first pass over a corpus measures the
page cache. Every generated input is checked for exit 0 before it is timed.

| Path | Sweep | This build | `pa22/cppgm++-ref` |
|------|-------|-----------|-------------------|
| **audit** d classes over a dependent base, each naming through itself | 100 → 3200 | 0.01 → 0.49 s, linear — **3.21 s before `reachable_`, and quadratic** | 0.60 → 1.80 s |
| n partial specializations, one naming each | 100 → 800 | 0.01 → 0.17 s | 0.70 → 2.70 s |
| n partial specializations, two namings choosing among each | 100 → 800 | 0.02 → 0.32 s | 0.80 → 7.91 s |
| n static data member templates named once each | 100 → 800 | 0.01 → 0.13 s | 0.60 → 1.00 s |
| n member templates, each called at two specializations | 100 → 800 | 0.02 → 0.19 s | 0.70 → 1.80 s |
| n template-template arguments at one parameter | 100 → 800 | 0.02 → 0.17 s | 0.70 → 1.10 s |
| n explicit instantiation definitions | 100 → 800 | 0.01 → 0.08 s | 0.60 → 0.70 s |
| n hidden friend templates | 100 → 800 | 0.03 → 0.25 s | 0.70 → 2.00 s |
| n `template<>` definitions of one member | 100 → 800 | 0.02 → 0.17 s | 0.70 → 1.30 s |
| n captureless lambdas in one body | 100 → 800 | 0.01 → 0.13 s | 0.70 → 2.20 s |
| a chain of n alias templates | 200 → 1600 | 0.00 → 0.04 s | 0.60 → 1.10 s, **segfault at 1600** |
| one template-id nested d deep | 8 → 256 | 0.00 → 0.01 s | 30.04 s at 16 |
| **cross product** n template-template arguments × m types | 200×32 → 400×64 | 1.01 → 4.42 s, linear in n·m | 9.62 → 120.17 s |
| **cross product** n specializations × m out-of-class members | 50×8 → 200×32 | 0.03 → 0.43 s, linear in n·m | — |
| n partial specializations *interleaved* with n namings | 100 → 800 | 0.01 → 0.16 s | — |
| lambdas nested d deep, each in the one before's returned expression | 8 → 64 | 0.00 → 0.26 s | 0.60 s at 8, killed at 60 s at 32 |
| n specializations of one body holding one lambda | 50 → 400 | 0.01 → 0.09 s | 0.60 → 1.23 s |
| n temporaries with destructors in one full-expression | 25 → 200 | 0.00 → 0.01 s | 0.60 → 0.80 s |
| 11.2p1's protected base chain of depth d, 200 accesses | 64 → 512 | 0.01 → 0.07 s | 0.55 → 0.69 s |
| a member class template nested d deep, each level defined out of class | 10 → 60 | 0.01 → 2.76 s | 0.60 s → 127.95 s, OOM-killed at 32 in the pre-O build |
| the whole 380-test corpus, one process per file | — | **1.68 s** warm | — |

The dominant operations are: interning a type in `TypeTable` (a hash lookup on a
key of a kind and its operands), looking a name up in a region (`SemaModel::find`
/ `lookup_in`, one probe per region), and 14.5.5.1p1's choice among the patterns
(memoized per interned argument list in `TemplateInfo::chosen`). Every sweep
above is linear in what it scales except the last two: the protected chain is
linear in the derivation it walks, and the nested out-of-class nest writes an
input that is itself O(d²) — d heads and a d-component declarator-id per level —
which a profile confirms is spread over name lookup and interning with no
dominant term. Nothing scans a list of specializations per naming, and nothing
re-reads a body it has read.

## Architecture Review

The graph the README asks for is the one that is there. Every fact about an
argument list is keyed by the interned list (`std::uint32_t`), which is the same
key the specialization is found by: `TemplateInfo::patterns`, `chosen`,
`explicit_classes`, `explicit_functions`, `explicit_variables`,
`explicit_member_classes` and `reading`. A template-template argument binds a
`TemplateInfo*` and not a spelling. A partial specialization is a template of its
own — its own head, its own current instantiation, its own out-of-class members —
so 14.5.5.1p1's choice is a comparison of entities and never of text.

Three readings recover terminals from a spelling, because 14.2 writes an
argument list inside a name and PA10 hands names on flattened: `QualifiedName`
and `TemplateId` for the components, `split_type_id` for 8.1p1, and
`split_value_expression` for 5.19. That is duplication the AST boundary forces
rather than a fallback: none of the three is a retry of another, they are asked
at different places, and the rule they share — 2.9p1's number — is now written
once. The audit swept 34 type-id forms and 23 constant-expression spellings
through both the declarator reading and the spelled one and found them equal on
every one the flattening does not destroy.

Nothing in the stage is a parallel fallback. There is no retry whose fallback
re-reads, no text recovery standing beside a tree that is still available, no
second owner of one fact: `folded_name` is the one door 5.19's four readings ask,
`TemplateSignature` the one form 13.1 and 7.3.3p14 key by, `Access::base_path`
the one walk 11.2p4 makes, `vacuous_destruction` the one door 12.4p8 answers at,
and `reading_region` the one key a closure class is held under.

## Final Architecture Review

The audit reconstructed the stage from its sources rather than from the ledger
and found three defects, all of them a rule landed at one exit and left at its
siblings, and all now fixed and swept:

1. The parse's unit-wide name index answered for a name written *in a scope* and
   had nothing for the form a probe of a prefix takes, so 10.2p2's chain was
   walked once per name that was going to miss — quadratic in the derivation.
2. 14.7.3p1's `template<>` over a member **class** fell through to the
   class-template tier and was refused, where the member class *template* form
   the fixture pins had been landed by checkpoint X.
3. 2.9p1's preprocessing number was split into three words by both readings that
   recover a spelling, and 8.3.4p1's bound was split by one and rejoined with
   spaces by the other.

What the audit confirmed rather than found: the ABI names of six PA22 entity
kinds agree with `g++` byte for byte; 31 of 32 PA22-shaped programs are byte for
byte the reference's through the assignment's own comparator, the one difference
being the reference's own; `valgrind -q --error-exitcode=9` is clean over 78
inputs; no `getenv`, fixture name, timeout, dialect switch keyed on anything but
a dialect, or caught exception standing for a success appears anywhere in the
stage's diff; and every phase runs — the driver reads the source files, runs
phases 1–7, parses, analyses and lowers, and the LowIR the fixtures compare is
the one `lowir2cy86` + `cy86` then runs to the value `g++` gives.

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
| **C, C audit** | 5.19 read out of one spelling at the five operators the reader had no answer for, with `TypeTable::object_align`, `align_of` and `require_settled_type` under it. A `sizeof` nested 24 deep went from killed at 60 s to 0.01 s at depth 128. | 318 / 348 |
| **B, B module split, B audit** | 11.2p4 answered about a member *as a member of the naming class*: `Access::base_path`, `resolve_prefix` per component, and five clauses no door enforced. The walk cost d² before it cost d. | 336 / 357 |
| **U, U audit** | 14.6.2.1p6's member of an unknown specialization at all three walks; 7.1.6.2p4's dependent id-expression; `LowValue::storage_owed`; and `holds_written_definition`, 14.7.3p1 read in the other source order. | 342 / 361 |
| **N, N audit** | The four calls 13.3 had to resolve before it had a set, and `TemplateSignature` as 13.1's index key. | 349 / 364 |
| **S, S audit** | 14.2 at the three readings 5.19 makes of one name and the fourth 7.1.6.2p4 makes: `folded_name` is the one door and `names_specialization` 14.8.1p2 at one entry. | 354 / 367 |
| **L, L audit** | 14.7.2p9's `extern template` at its three tiers through `instantiation_suppressed`; what a discarded expression is worth; and 5.2.3p3's `T{...}` over an array. | 362 / 371 |
| **W, W audit** | 15.4p1 as a fact of one call; 9.3.1p3's object reached through the naming class; and `PendingDefinition::returned_object_chain`, bounded at both ends by `held_by_class`. | 367 / 372 |
| **Y, Y audit** | 8.3.4p1's bound as a bound *and* a place: `TypeTable::Node::bound_place`, read by `substituted_array`, `match_bound` and 14.5.5.2's ordering; `collect_packs` and 14.8.2.5p17 were the two walks the new edge had not reached. | 369 / 373 |
| **Z, Z audit** | 9.5p1's anonymous aggregate as the object no name reaches: `anonymous_storage` and `collect_member_targets` with `one_of`, walked by 12.6.2p8, 12.4p8 and 12.6.2p2 alike; 8.5.1p15's `one_per_union` fixed an array of aggregates that ran to the wrong value. | 374 / 377 |
| **AA, AA audit** | 5.1.2's closure class *made* as a class-specifier and handed to the class reading; `reading_region` as the key one class is held under, which took 8 nested lambdas from 255 classes and 11.20 s at 16 to 36 and 0.26 s at 64; 12.4p8's provenance at every end of a lifetime; 15.2p2's region at a step whose handler owes something; and 5.1.2p4's walk stopped at a nested `FunctionDefinition`. | 375 / 377 |
| **AB** | 12.8p15's copy of a class with no non-static data member and no base carries **no byte**, asked before the initializer is read: `copies_no_byte`, `SemaFact::written_call` as the form the program wrote, and `kept_string_objects` for 2.14.5p8's object the dropped subtree still owes. `sema_elision` came out of `sema_lifetime.cpp`. | **380 / 380** |
| **Final audit** | `DeclaredNames::reachable_`, the spelling index that makes 10.2p2's chain walk a miss's one probe; 14.7.3p1 over a member **class**, recorded against the argument list before the specialization is made and read in place of the pattern's body; `pp_number_end` shared by both readings that recover a spelling, and 8.3.4p1's bound handed on whole. `sema_explicit.cpp` came out of `sema_template.cpp`. | **380 / 380** |
