# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `ast_parser_class.cpp` — 14.1p3's abstract declarator and 10p1's
  `class-or-decltype`, whose operand tree the arena keeps beside its spelling.
- `sema_template_head.*` — 14.1p2's places, named and unnamed alike.
- `sema_deduce.{h,cpp}` — 14.8.2. `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, and `Instantiated` is the error that escapes it.
- `sema_specialize.cpp` — 14.5.5.1p1's match, 14.5.5.2p1's ordering, 14.5.1p1's
  variable template and 14.5.7p1's alias.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and the dependent stand-ins a pattern leaves behind.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused and
  gathers 13.3.1.4p1's converting constructors, and 13.4p1's target.
- `lowir_abi.cpp` — 14.2's encoding of an argument list, which asks the type
  what each argument *is*: a value, a run, a template name, an expression, or
  the declaration 14.3.2p1's address argument designates.

## Current Failure Map

413 tests (405 handout + 8 course), 332 passing (handout-only 324 / 405, which
this audit turn left where the checkpoints put it). The 81 left, by the compiler
behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a wrong answer survives to a false `static_assert` | 14 | the refusal a substitution should make is not made at all |
| a call or name no declaration answers | 17 | the candidate a deduction or a substitution should have kept is dropped |
| LowIR text mismatch | 18 | the program compiles; the emitted LowIR differs |
| a value place still outside the read subset | 4 | `sizeof...`, a qualified alias and `typename` written at one |
| a pack the reading does not find | 2 | `Args is expanded and names no parameter pack` |
| `__builtin_invoke` | 2 | a door no reading has |
| the rest | 24 | one-off clauses, one test each |

The 14 `static_assert` failures are one theme with fourteen clauses under it:
the expression layer accepts what the standard refuses, so SFINAE has nothing
to fire on. Each needs its own rule — 5.7p1's pointer arithmetic over an
incomplete pointee, 8.5.4p3's narrowing, 8.3.2p5/8.3.4p1's underivable types,
14.1p2's template-template kind mismatch, 5.2.4p1's pseudo-destructor,
CWG 1558's substitution into an alias template's *unused* arguments (`first_t`,
a `void_t` that is a direct alias). No two of them share an owner.

Known gaps diagnosed but not landed:

- **A global with an initializer is written per element by the reference and as
  one `zero n` run by us.** `cppgm++-ref` writes `i8 0` four times for
  `char a[4] = {};` and `zero 4` only where the declaration wrote *no*
  initializer; the collapse is PA13's and 152 earlier `.ref` lines hold
  `zero 1`.
- **14.6p2 at a reading a dependent context defers.** `T::type v = 0;` in a
  template no use instantiates is refused where `g++` refuses it, and
  `static_cast<T::type>(0)` beside it is not read until the arguments settle.
  Widening the clause to every dependent prefix costs
  `pa20/course/pa20/100-a-decltype-an-argument-list-wrote-is-a-tree.t`, whose
  `.ref` expects EXIT_SUCCESS for `box<decltype(make())>::type` with no
  `typename`.
- **A pack pattern in a partial specialization's own argument list.**
  `D<void_t<typename T::m...>, T...>` leaves the primary unsupported; `g++` and
  the reference both take it.
- **Two partial specializations reached through a non-deduced array bound are
  not ordered against each other.** `D<T, char[sizeof(T)]>` beside
  `D<T, char[4]>` over `D<int, char[4]>` is ambiguous to both oracles and
  answers the second here.
- **8.3.4p1's bound over a place is read in a class template's member and in an
  alias and not in a function template's parameter.** `char (&a)[sizeof(T)]` at
  a parameter is `sizeof names an incomplete type`; the reading is PA22's, and
  the pre-checkpoint binary refuses it identically.
- **14.1p4's fourth bullet has no layer below it.** A pointer-to-member place is
  refused because `int S::*p = &S::m;` is, so it is a milestone boundary rather
  than a template gap. A `void *` place and a cast written as a template
  argument (`at<(int*)0>`) are the two smaller ones beside it.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain: n declarations of one name is 0.80 s at
  n = 3200. The walk is PA22's.

## Active Checkpoint

**The audit of 14.3.2p1's address argument and the four rules beside it.**
Complete; ledger row 2 of `audit.md` and completed checkpoint 6 below are its
record. What the turn found is that "which object an argument designates" has a
fourth reader nobody had told - the object file - and that the conversions
reaching such a place were the ones an *initialization* takes.

- *Owner.* `TypeTable::set_address_object` / `address_object` hold which
  declaration an entry of the constant-address table stands for;
  `LocalContexts::entity_of` (`lowir_abi.cpp`) hands the encoder that
  declaration and `ABI_TEMPLATE_ARGUMENT_ENTITY` (`abi_mangle.cpp`) writes the
  ABI's two spellings of one; `TemplateHead::reaches_place` owns 14.3.2p5's own
  conversion list and `TemplateHead::function_place` says where 13.4p1's target
  is asked; `SemaAnalyzer::template_argument_value` owns 14.3.2p1's last bullet
  and `TemplateArgumentReader::target` 13.4p1's set.
- *Data flow.* An address argument is settled once, in
  `TemplateHead::address_argument`, and that is where the table is told which
  declaration the entry it interned is. Every later reader asks the type: the
  binding and the read-back ask `address_place`, the spelling asks the
  `AddressTable`, and the encoding asks `address_object` and then names the
  declaration by the same walk this unit names its definition by.
- *Expected complexity.* One record per declaration per encoded name, one type
  comparison per argument bound, and one lookup-chain walk per argument written
  at a function place; no walk added at any of them.
- *Validation.* An 11-shape mangling cross-product byte-identical to the
  reference binary and matching `g++` on 10 of 11; four new course fixtures whose
  `.ref` the reference binary wrote; sweeps of 35 + 21 + 15 shapes over the
  address places, 11 over `class-or-decltype`, 7 over the converting-constructor
  set and 8 over the widened array bound, each judged against `g++` and the
  reference; `through-pa22` at 2948 / 2948; valgrind clean over 140 inputs; and
  the multiplicity and nesting sweeps in the Performance Model.

## Next Substantial Checkpoint

**The expression layer's missing refusals**, which is the 14-test
`static_assert` group. The cheapest coherent bundle inside it is the types a
declarator may not derive — 8.3.2p5's reference to void, 8.3.4p1's array of
void, of a reference and of an abstract class — because all three have one
owner (`apply_pointer`/`apply_suffix`, `SpelledTypeId::declarator` and
`substituted`, which is the same rule written three times) and because
`300-invalid-alias-type-formation-sfinae` turns on exactly those four shapes.
5.7p1's arithmetic over a pointer to an incomplete object type is the second,
and it is two fixtures.

## Performance Model

Measured on this turn's binary, warm cache, `/usr/bin/time` on the binary
itself. A loop that spawns `timeout` per run reads the same corpus as 45.9 s
against 2.6 s, which is the wrapper's process floor and not the compiler's.

| sweep | shape | result |
| --- | --- | --- |
| address-argument multiplicity | n objects, one specialization per `&obj` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.12 @1024 - linear |
| function-argument multiplicity | n functions, one specialization per `&f`, each mangled through a nested encoder | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| one object named n times | n namings of one `at<&o>` | 0.00 s @32, 0.01 @512, 0.03 @1024 - one specialization and one entity record |
| SFINAE multiplicity | n classes x 2 candidates, one failing substitution | 0.01 s @32, 0.03 @128, 0.15 @512, 0.30 @1024 - and 0.01 / 0.03 / 0.14 / 0.32 on the pre-checkpoint binary |
| detector multiplicity | n classes x 3 partial specializations, 2 of which refuse substitution | 0.01 s @32, 0.03 @128, 0.14 @512, 0.30 @1024 - linear |
| converting-constructor multiplicity | n calls, each converting through one of 3 constructor templates | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| variable-template multiplicity | n classes x 2 calls gated by `pointed<T>` | 0.00 s @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear |
| substitution nesting | d nested trait layers under one `enable_if` | 0.00 s flat from d = 8 to d = 48 |
| detector nesting | d nested `W<...>` wrappers under one detector | 0.00 s flat from d = 4 to d = 32 |
| name nesting | d nested class templates over the object an argument designates | 0.00 s flat from d = 4 to d = 32 |
| declarations of one name | n function templates, one parameter list | 0.04 @400, 0.24 @1600, **0.80 s @3200 - quadratic** |
| whole PA23 corpus | 413 files, one process each | 2.6 s warm, no `rc > 1`, valgrind clean over 140 inputs |

Why this turn's doors stay linear: 14.8.2p8's attempt is a `try` block, which
costs nothing until a candidate actually fails, and the one around
`Specialization::matches` sits *inside* the `chosen` memo, so n namings of one
argument list are one set of attempts. 13.3.1.4p1's deduction is one
`from_call` per constructor template per measured conversion, and `specialize`
is interned per argument list. 14.6.2p2's stand-in is one entity per (template,
interned list). The audit's own doors are each O(1) per argument: `reaches_place`
is a type comparison over the pointer levels already in hand, `resolve_target`
walks one lookup chain and only at a function place, and `entity_of` is one
record per declaration per encoded name whose symbol is the walk this unit
already makes for that declaration's definition. The one quadratic is PA22's
`TemplateSignature::equivalent` chain walk, named under the known gaps.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.{h,cpp}`, `sema_specialize.cpp`, `sema_analyzer.{h,cpp}`, `sema_function.cpp`, `sema_declarator.cpp`, `sema_scope.{h,cpp}`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal from an instantiated class body, which is outside the immediate context and still refuses the program; 14.1p3's unnamed *type* place is declared into the head's region; a dependent value argument keeps its spelling, place and region so `X<A + 1>` substitutes; `substituted_region`; `dependent_member_type` rebuilt over the class *this* substitution made; 14.5.6.1p5 rather than 13.1's index pairs two declarations; 14.8.2.1p4's qualification conversion; 14.6p2's `typename` over a bare place. The audit added 14.7.1p1's bound on the attempt asking for itself, 14.8.2p8's lexical order, and 14.1p12 over the pair 14.5.6.1p5 settles | 246 → 292 / 400; through-pa22 2948 / 2948; 0 corpus crashes against 3 |
| 2 | 14.3.2p1: an address argument is which object it designates | `sema_template_head.{h,cpp}`, `sema_value_expression.cpp`, `sema_expression.cpp`, `sema_constexpr.cpp`, `ast_parser_class.cpp` | 14.1p4's second and third bullets and 14.1p8's adjustment, so a place of pointer, lvalue-reference, array or function type is one an argument reaches; 14.3.2p5 at such a place over 5.19p2's `ConstantAddress`, so two namings of one object are one specialization; 14.3.2p1's linkage and 14.3.2p3's subobject refusals; `&` and `nullptr` in the spelling reader and 5.3.1p3's operand read as storage; 4.3p1's function name; 14.1p3's *abstract* declarator in a non-type parameter; the argument read back at a use; 3.2p3's demand for the definition of a function an argument names | 292 → 299 / 400, plus 5 course fixtures; 30-shape sibling sweep agrees with the reference on all 20 accepted and all 10 refused |
| 3 | 14.8.2p8 at 14.5.5.1p1's match, and 10p1's `class-or-decltype` | `sema_specialize.{h,cpp}`, `ast_parser_class.cpp`, `sema_derivation.cpp` | `Specialization::matches` runs `match_arguments` and 14.8.2.5p5's read-back inside one `Substitution`, so `void_t<typename T::iterator_category>` is a pattern the lists whose class declares no such member simply do not match - the whole detector idiom; 14.5.5p8.3's unbound place moved out of that scope into `took_places`, where a defect of the partial specialization's own declaration still refuses the program. 10p1's base written as a decltype-specifier had its operand skipped and never parsed, so no tree stood beside the spelling and the name lookup was handed the flattened text: the operand is now read and kept, the qualified reading is tried first so `decltype(e)::type` does not stop at the specifier, and `Derivation` reads the spelling as a type-id | 304 → 314 / 405; 11-shape base sweep agrees with `g++` on all 11 |
| 4 | 13.3.1.4p1's converting constructor is a template too | `sema_overload.cpp` | 13.3.3.1.2p1's user-defined conversion sequence was measured over the constructors a class *declared*, so `box<int>` reached `const box<long> &` through nothing; a constructor template is now a candidate through the specialization the one argument deduces for it, with 12.3.1p2's `explicit` and 8.4.3p1's deleted asked of the template before the deduction. The sibling sweep found 13.3.3p1's last tie-break missing with it - a class declaring both `box(const box<int> &)` and the template offered two candidates that tie, which read as ambiguous and left the class unreachable - so the non-specialization now wins the tie | 314 → 319 / 405; 11-shape sweep agrees with `g++` on all 11 |
| 5 | 14.6.2p2's variable template, and a prefix the arguments settled | `sema_specialize.cpp`, `sema_template.cpp`, `sema_type_id.cpp`, `sema_deduce.cpp` | `enabled<T>` under an outer head was folded to the primary's own initializer, so a trait written as a variable template answered the whole program with one constant; the naming now gives back a declaration holding no constant and typed by a stand-in, held against the interned list, and the *expression*'s stand-in stays the one the reading that wrote it makes. 14.6.2p1 left `typename T::missing` standing whenever the prefix was not a class, which 14.8.2.5p5's read-back takes for agreement - a settled non-class prefix now refuses, while a run, a template name and a value keep the old reading. 8.3.4p1's bound written as an expression over a place (`int[sizeof(T)]`) is read as 14.8.2.5p5's non-deduced context rather than refused | 319 → 324 / 405, plus 4 course fixtures; 15-shape prefix sweep and 6-shape variable-template sweep agree with `g++` and the reference on every shape |
| 6 | audit: 14.3.2p1's address argument has a fourth reader, and 14.3.2p5's conversions are not 8.5's | `type_model.{h,cpp}`, `lowir_abi.cpp`, `abi_mangle.cpp`, `sema_template_head.{h,cpp}`, `sema_value_expression.cpp` | the object-file name of an address argument was this unit's `AddressTable` entry number, so two units that each name `at<&left>` wrote one weak definition under two names - the encoding now names the declaration, which `TypeTable::set_address_object` carries from where the argument is settled; the comparator strips `object=`, which is why the checkpoint's own fixture disagreed with its `.ref` on every name and passed. `reaches_place` is 14.3.2p5's own conversion list, asked before 8.5's readings of the same places, so a zero-valued integral constant, 4.10p3's derived-to-base (which *ran to the wrong storage*), an unrelated pointee and a reference place's type mismatch are four programs both oracles refuse and this build translated. 14.1p4's fifth place is now one an argument can fill, and 13.4p1's target chooses one declaration of an overloaded name at a function place | 328 / 409 -> 332 / 413, the handout set unchanged at 324 / 405; 11-shape mangling cross-product identical to the reference and matching `g++` on 10 of 11; through-pa22 2948 / 2948; valgrind clean over 140 inputs |
