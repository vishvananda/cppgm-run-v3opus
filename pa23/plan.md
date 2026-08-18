# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `ast_parser_class.cpp` — 14.1p3's abstract declarator and 10p1's
  `class-or-decltype`, whose operand tree the arena keeps beside its spelling.
- `sema_template_head.*` — 14.1p2's places, 14.1p9's defaults and the bound
  each was written under.
- `sema_deduce.{h,cpp}` — 14.8.2. `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, and `Instantiated` is the error that escapes it.
- `sema_specialize.cpp` — 14.5.5.1p1's match, 14.5.5.2p1's ordering, 14.5.1p1's
  variable template and 14.5.7p1's alias.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and `DependentReadings`: the constructs a pattern left
  standing, each with the region and the bound 14.7.1p1 reads it again under.
- `sema_scope.{h,cpp}` — 3.4's lookups, and 14.6.4.2p1's bound on them:
  `declared_serial` per namespace-bound declaration, `ReadingBound` per
  reading.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5 and
  14.5.6.2's ordering of two function templates.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused,
  gathers 13.3.1.4p1's converting constructors, and asks 14.6.4.2p1 of 3.4.1's
  half of a set and not of 3.4.2's.
- `sema_layout.cpp` / `type_model.cpp` — 5.2.2p4's boundary, which reads
  12.8p12's copy, 12.4p8's destruction and 3.9.1p8's floating storage.
- `lowir_abi.cpp` — 14.2's encoding of an argument list.

## Current Failure Map

416 tests (405 handout + 11 course), 341 passing (handout-only 330 / 405). The
75 left, by the compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a wrong answer survives to a false `static_assert` | 12 | the refusal a substitution should make is not made at all |
| a call or name no declaration answers | 13 | the candidate a deduction or a substitution should have kept is dropped |
| LowIR text mismatch | 18 | the program compiles; the emitted LowIR differs |
| a value place still outside the read subset | 3 | a qualified alias and `typename` written at one |
| a pack the reading does not find | 2 | `Args is expanded and names no parameter pack` |
| the rest | 27 | one-off clauses, one test each |

The `static_assert` group is one theme with twelve clauses under it: the
expression layer accepts what the standard refuses, so SFINAE has nothing to
fire on. Each needs its own rule — 5.7p1's pointer arithmetic over an
incomplete pointee, 8.5.4p3's narrowing, 8.3.2p5/8.3.4p1's underivable types,
14.1p2's template-template kind mismatch, 5.2.4p1's pseudo-destructor,
CWG 1558's substitution into an alias template's *unused* arguments. No two of
them share an owner.

Known gaps diagnosed but not landed:

- **A global with an initializer is written per element by the reference and as
  one `zero n` run by us.** The collapse is PA13's and 152 earlier `.ref` lines
  hold `zero 1`.
- **14.6p2 at a reading a dependent context defers.** Widening the clause to
  every dependent prefix costs
  `pa20/course/pa20/100-a-decltype-an-argument-list-wrote-is-a-tree.t`.
- **A pack pattern in a partial specialization's own argument list.**
  `D<void_t<typename T::m...>, T...>` leaves the primary unsupported.
- **Two partial specializations reached through a non-deduced array bound are
  not ordered against each other.**
- **8.3.4p1's bound over a place is read in a class template's member and in an
  alias and not in a function template's parameter.**
- **14.1p4's fourth bullet has no layer below it.** A pointer-to-member place
  is refused because `int S::*p = &S::m;` is.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain: n declarations of one name is 0.80 s at
  n = 3200. The walk is PA22's.
- **14.6.4.2p1's bound is not asked of a fold's own candidate set.**
  `ConstexprReading::selected` does not draw 3.4.1's line against 3.4.2's, so
  it passes `kAssociatedUnknown` and no candidate is asked. A later overload
  reached through a constant expression *inside* a second reading is still
  found. No fixture reaches it; the sweep's 30 shapes go through the
  expression tier.

## Active Checkpoint

**The second reading a substitution makes: what travels with it.** Complete;
ledger row 7 below is its record. Three constructs a pattern leaves behind were
each missing one of the two facts 14.7.1p1 needs to make the reading again -
the tree, the region, and (new) the bound 14.6.4.2p1 puts on what its names may
reach - and one boundary fact was missing a reader.

- *Owner.* `DependentDecltype` (`sema_declaration.h`) carries the third form:
  a tree with `evaluated`, which `SemaAnalyzer::dependent_default` makes and
  `substituted` reads. `SemaModel::bound()` counts declarations 3.3.6's
  namespace regions bind, `SemaEntity::declared_serial` is where one stands in
  that count, and `ReadingBound` (`sema_scope.h`) is the bound one reading runs
  under. `TemplateInfo::Default::visible` and `PlaceDefault::visible` are the
  same number for 14.1p9's two tiers. `UserType::floating_storage` is 3.9.1p8
  over the storage a class is laid out over, settled in `sema_layout.cpp` and
  read by `returns_indirectly` / `passes_indirectly`.
- *Data flow.* A default at a value place over a list an argument has yet to
  settle becomes the reading itself: `bind_arguments` interns it by spelling
  and region, `substituted` rebuilds the region and evaluates the tree there,
  converted to the place the substitution makes. A bound is recorded where a
  construct is written and put back where it is read again; `find` drops a
  namespace binding past it, `select_overload` drops a later overload from
  3.4.1's half of a set, and 3.4.2's own searches and every instantiated body
  run under no bound at all. The floating fact is one bool per class, settled
  by the layout walk from its bases and members.
- *Expected complexity.* One integer comparison per namespace lookup and per
  candidate; one bool per class settled with one read per subobject; one
  interned reading per (template, argument list), memoized by
  `default_arguments_`; the "is anything unsettled" question carried across the
  fill rather than asked per default, so a head of k places costs k.
- *Validation.* 45 shapes of 14.1p9's default over an unsettled list, agreeing
  with `g++` on all and with the reference binary on the 30 that are
  accept/refuse; 30 shapes of 14.6.4.2p1 crossed over three patterns, five
  later declarations and two argument pairs, agreeing with `g++` on all 30 and
  with the reference on 29 (the reference finds a later *global* overload from
  one of its three patterns and not from the other two, which `g++` refuses at
  all three); 32 ABI shapes byte-identical to the reference; three new course
  fixtures whose `.ref` the reference binary wrote; `through-pa22` at
  2948 / 2948; valgrind clean over 78 inputs; no `rc > 1` over the 416-file
  corpus.

## Next Substantial Checkpoint

**The expression layer's missing refusals**, which is the 12-test
`static_assert` group. The cheapest coherent bundle inside it is the types a
declarator may not derive — 8.3.2p5's reference to void, 8.3.4p1's array of
void, of a reference and of an abstract class — because all three have one
owner (`apply_pointer`/`apply_suffix`, `SpelledTypeId::declarator` and
`substituted`, which is the same rule written three times) and because
`300-invalid-alias-type-formation-sfinae` turns on exactly those four shapes.
5.7p1's arithmetic over a pointer to an incomplete object type is the second.

## Performance Model

Measured on this turn's binary, warm cache, `/usr/bin/time` on the binary
itself. A loop that spawns `timeout` per run reads the same corpus as 45.9 s
against 2.6 s, which is the wrapper's process floor and not the compiler's; a
corpus pass run while a second build saturates the machine reads 5.8 s against
1.9 s, which is that build's.

| sweep | shape | result |
| --- | --- | --- |
| address-argument multiplicity | n objects, one specialization per `&obj` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.12 @1024 - linear |
| function-argument multiplicity | n functions, one specialization per `&f` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| one object named n times | n namings of one `at<&o>` | 0.00 s @32, 0.01 @512, 0.03 @1024 |
| SFINAE multiplicity | n classes x 2 candidates, one failing substitution | 0.01 s @32, 0.03 @128, 0.15 @512, 0.30 @1024 - linear |
| detector multiplicity | n classes x 3 partial specializations | 0.01 s @32, 0.03 @128, 0.14 @512, 0.30 @1024 - linear |
| converting-constructor multiplicity | n calls through 3 constructor templates | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| variable-template multiplicity | n classes x 2 calls gated by `pointed<T>` | 0.00 s @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear |
| **defaulted-argument multiplicity** | n function templates, each naming `S<A>` whose second place takes 14.1p9's default over the deduced one | **0.01 s @32, 0.04 @128, 0.19 @512, 0.39 @1024 - linear.** The pre-checkpoint binary refuses every one of these, so there is no baseline to read against it |
| **definition-bound multiplicity** | n hidden friends, one detector, n later declarations of the name | **0.00 s @32, 0.02 @128, 0.09 @512, 0.20 @1024 - and 0.00 / 0.02 / 0.09 / 0.20 on the pre-checkpoint binary, to the hundredth at every size** |
| substitution nesting | d nested trait layers under one `enable_if` | 0.00 s flat from d = 8 to d = 48 |
| detector nesting | d nested `W<...>` wrappers under one detector | 0.00 s flat from d = 4 to d = 32 |
| **defaulted-argument nesting** | d class templates, each defaulting its second place over the one below | **0.00 s flat from d = 4 to d = 32** |
| declarations of one name | n function templates, one parameter list | 0.04 @400, 0.24 @1600, **0.80 s @3200 - quadratic** |
| whole PA23 corpus | 416 files, one process each | **1.94 s warm, and 1.95 s on the pre-checkpoint binary**; no `rc > 1`, valgrind clean over 78 inputs |

Why this turn's doors stay flat: 14.6.4.2p1's bound is one `std::uint32_t`
comparison inside `find` and inside the candidate walk, taken only where a
bound is set at all - which is inside a second reading and nowhere else - and
the number itself is stamped once per declaration where a region binds it.
14.1p9's stand-in is one entity per (spelling, region), and the list it fills
is memoized by `default_arguments_`, so a template named n times the same way
reads its default once. 3.9.1p8's floating fact is settled by the walk that
already reads every base and member. The one quadratic is PA22's
`TemplateSignature::equivalent` chain walk, named under the known gaps.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.{h,cpp}`, `sema_specialize.cpp`, `sema_analyzer.{h,cpp}`, `sema_function.cpp`, `sema_declarator.cpp`, `sema_scope.{h,cpp}`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal from an instantiated class body; 14.1p3's unnamed *type* place; a dependent value argument keeps its spelling, place and region; `substituted_region`; `dependent_member_type` rebuilt over the class *this* substitution made; 14.5.6.1p5 rather than 13.1's index pairs two declarations; 14.8.2.1p4's qualification conversion; 14.6p2's `typename` over a bare place | 246 → 292 / 400; through-pa22 2948 / 2948 |
| 2 | 14.3.2p1: an address argument is which object it designates | `sema_template_head.{h,cpp}`, `sema_value_expression.cpp`, `sema_expression.cpp`, `sema_constexpr.cpp`, `ast_parser_class.cpp` | 14.1p4's second and third bullets and 14.1p8's adjustment; 14.3.2p5 over 5.19p2's `ConstantAddress`; 14.3.2p1's linkage and 14.3.2p3's subobject refusals; `&` and `nullptr` in the spelling reader; 4.3p1's function name; 14.1p3's *abstract* declarator; 3.2p3's demand for the definition an argument names | 292 → 299 / 400, plus 5 course fixtures; 30-shape sweep agrees with the reference on all |
| 3 | 14.8.2p8 at 14.5.5.1p1's match, and 10p1's `class-or-decltype` | `sema_specialize.{h,cpp}`, `ast_parser_class.cpp`, `sema_derivation.cpp` | `Specialization::matches` runs `match_arguments` and 14.8.2.5p5's read-back inside one `Substitution` - the whole detector idiom; 14.5.5p8.3's unbound place moved out of that scope; 10p1's base written as a decltype-specifier has its operand read and kept | 304 → 314 / 405; 11-shape base sweep agrees with `g++` |
| 4 | 13.3.1.4p1's converting constructor is a template too | `sema_overload.cpp` | a constructor template is a candidate through the specialization the one argument deduces, with 12.3.1p2's `explicit` and 8.4.3p1's deleted asked of the template; 13.3.3p1's last tie-break, so the non-specialization wins a tie | 314 → 319 / 405; 11-shape sweep agrees with `g++` |
| 5 | 14.6.2p2's variable template, and a prefix the arguments settled | `sema_specialize.cpp`, `sema_template.cpp`, `sema_type_id.cpp`, `sema_deduce.cpp` | a naming of a variable template under an outer head gives back a declaration typed by a stand-in, held against the interned list; a settled non-class prefix refuses where 14.8.2.5p5's read-back took it for agreement; 8.3.4p1's bound written over a place is 14.8.2.5p5's non-deduced context | 319 → 324 / 405, plus 4 course fixtures; 15-shape and 6-shape sweeps agree with both oracles |
| 6 | audit: 14.3.2p1's address argument has a fourth reader | `type_model.{h,cpp}`, `lowir_abi.cpp`, `abi_mangle.cpp`, `sema_template_head.{h,cpp}`, `sema_value_expression.cpp` | the encoding names the declaration an address argument designates rather than this unit's table entry, so two units writing `at<&left>` write one definition; `reaches_place` is 14.3.2p5's own conversion list, asked before 8.5's; 14.1p4's fifth place; 13.4p1's target at a function place | 328 / 409 → 332 / 413; 11-shape mangling cross-product identical to the reference |
| 7 | the second reading a substitution makes: the tree, the region and the bound | `sema_declaration.h`, `sema_template.{h,cpp}`, `sema_template_head.cpp`, `sema_deduce.cpp`, `sema_scope.{h,cpp}`, `sema_overload.cpp`, `sema_operator.{h,cpp}`, `sema_argument_lookup.cpp`, `sema_analyzer.{h,cpp}`, `sema_layout.cpp`, `type_model.{h,cpp}` | 14.1p9 at a value place over a list an argument has yet to settle was *evaluated* against a dependent prefix and refused the program - `dependent_default` keeps the tree and the region and 14.7.1p1 evaluates it where the arguments arrive, which is the third form of `DependentDecltype`. 14.6.4.2p1: every reading a pattern left standing was made again against the *whole* unit, so an object declared later under the name of a hidden friend suppressed 3.4.2 and a later overload joined a set - `declared_serial` and `ReadingBound` put 3.4.1's half of such a reading back at the definition context, while 3.4.2's searches and every instantiated body run under none. The sibling sweep found the same gap at both tiers of 14.1p9's own default, which is read at the naming and not through a stand-in, so both carry the bound too. 5.2.2p4: a class of two words or less holding a floating scalar crosses by address in the reference and as its bytes here, which is 3.9.1p8 over the storage the layout walks and had no reader at all. `DependentReadings` and 14.5.6.2's ordering moved to their owners to make the room | 332 / 413 → 338 / 413, plus 3 course fixtures (341 / 416); through-pa22 2948 / 2948; 45 + 30 + 32 sweep shapes against `g++` and the reference; corpus 1.94 s against the baseline's 1.95 s |
