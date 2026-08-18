# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `sema_template_head.*` — 14.1p2's places, named and unnamed alike.
- `sema_deduce.{h,cpp}` — 14.8.2. `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, and `Instantiated` is the error that escapes it.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and the dependent stand-ins a pattern leaves behind
  (`dependent_written_`) for both a decltype-specifier and a value argument.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5, which says
  when two template-declarations declare one template.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused.

## Current Failure Map

405 tests (400 handout + 5 this turn's), 304 passing (turn-start baseline
292 / 400). The 101 left, grouped by the compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a call no declaration accepts | 19 | the candidate a deduction or a substitution should have kept is dropped |
| LowIR text mismatch | 20 | the program compiles; the emitted LowIR differs |
| a name a substitution should have reached | 13 | `no declaration of X is in scope` for a member the arguments do settle |
| a wrong overload survives to a false `static_assert` | 10 | the refusal a substitution should have made is not made at all |
| an initialization with no conversion | 4 | 13.3.1.4/13.3.1.5 over a specialization the deduction did make |
| a value place still outside the read subset | 3 | `sizeof...` and a qualified alias at a value place, `typename` written as one |
| a class the reading left incomplete | 2 | a base or a `sizeof` naming one 14.7.1p1 has not settled |
| a pack the reading does not find | 2 | `Args is expanded and names no parameter pack` |
| the rest | 28 | one-off clauses, one test each |

Known gaps diagnosed but not landed, with their evidence:

- **A global with an initializer is written per element by the reference and as
  one `zero n` run by us.** `cppgm++-ref` writes `i8 0` four times for
  `char a[4] = {};` and `zero 4` only where the declaration wrote *no*
  initializer at all; the tail of `const char *n[3] = {"Jan","Feb"}` is its one
  `zero 8`, so the rule is per *element* and `zero` is that element's own
  spelling for a null pointer and for padding. One PA23 fixture turns on it
  (`400-qualified-static-array-pointer-nontype-argument`, whose NTTP half now
  passes); the collapse is PA13's and 152 earlier `.ref` lines hold `zero 1`.
- **14.6p2 at a reading a dependent context defers.** The clause is asked where
  a type-specifier is read while its prefix is still a place, which is the
  declaration exits: `T::type v = 0;` in a template no use instantiates is
  refused, and `static_cast<T::type>(0)` beside it is not read until the
  arguments have settled `T`, by which time the prefix is a class. Five shapes
  `g++` refuses translate here — a template argument, a `static_cast`, a
  `sizeof`, a c-style cast and a default template argument.
- **14.6p2 beyond a bare place.** `require_written_type` refuses only a name
  whose prefix is a place its own head declared. `pa20/course/pa20/100-a-
  decltype-an-argument-list-wrote-is-a-tree.t` writes `box<decltype(make())>
  ::type` with no `typename` and its `.ref` expects EXIT_SUCCESS, so widening
  the clause to every dependent prefix costs that fixture.
- **14.3.2p1 at a member of a dependent class.** `helper<&T::const_cast_from>`
  in `300-static-member-template-function-pointer-nttp` names a member function
  *template* through a place, which 14.8.2.2 deduces from the place's own
  function-pointer type. The address layer now takes every other shape;
  the missing half is choosing a specialization from the target type.
- **`__builtin_invoke`.** Two tests naming a door no reading has.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain: n declarations of one name is 0.80 s at
  n = 3200. The walk is PA22's; keying the index by the canonical form is also a
  change to what 7.3.3p14's hiding key is built from.

## Active Checkpoint

**14.3.2p1: an address argument is which object it designates.** Complete; the
ledger row below is its record.

- *Owner.* `TemplateHead` (`sema_template_head.{h,cpp}`) owns 14.1p4's place
  type, 14.3.2p5's binding at one, the spelling a specialization is named by,
  and the read-back at a use. `sema_value_expression.cpp` owns the spelling
  reader that recovers `&` and `nullptr`; `sema_constexpr.cpp` owns 4.3p1's
  function name; `ast_parser_class.cpp` owns 14.1p3's abstract declarator.
- *Data flow.* The written argument splits into words, the reader folds it to a
  `SemaConstant` whose `bits` is the `AddressTable` identifier of the object it
  designates, `address_argument` converts that to the place's own type and
  interns it as a `TypeKind::Value` entry, `bind` puts it on the region as a
  `TemplateValue` carrying the identifier, and `address_value` turns it back
  into the expression that names the object where the place is read.
- *Expected complexity.* One interning per declaration, one type-table entry per
  (place, object), two dump nodes per use; no walk of anything at any of them.
- *Validation.* The six PA23 fixtures the group is written for, the two `-bad`
  ones 14.3.2p1's linkage requirement pins, five new course fixtures whose
  `.ref` the reference binary wrote, a 30-shape sibling sweep across the
  cross-product of place kind and argument form, `through-pa22`, valgrind, and
  the multiplicity and nesting sweeps in the Performance Model.

## Next Substantial Checkpoint

**The candidate a deduction drops and should not**, which is the largest group
left and the one the stage owns outright: 19 calls end in `no declaration of X
accepts the arguments of a call` where both oracles name one. The two doors to
walk are `Deduction::match_argument`'s pairs — 14.8.2.1p2's reference and
array/function decay corners, 14.8.2.5's non-deduced contexts, and the braced
argument 14.8.2.1p1 leaves undeduced — and `arguments_of`, where a place no pair
reached takes 14.1p9's default. The 13 `no declaration of X is in scope` cases
sit under the same walk, one tier down.

## Performance Model

Measured on this turn's binary, warm cache, `/usr/bin/time` on the binary
itself (a harness that spawns `date`/`timeout` per run invents a ~0.1 s floor).

| sweep | shape | result |
| --- | --- | --- |
| address-argument multiplicity | n objects, one specialization per `&obj` | 0.01 s @32, 0.02 @128, 0.08 @512, 0.20 @1024 — linear |
| abstract-declarator nesting | a value place written `int (*)(int (*)(…))` d deep | 0.00 s flat from d = 4 to d = 20 |
| SFINAE multiplicity | n classes × 4 candidates, 3 of which fail substitution | 0.01 s @32, 0.04 @128, 0.18 @512, 0.34 @1024 — linear |
| substitution nesting | d nested `enable_if` chains under 3 candidates | 0.01 s flat from d = 8 to d = 48 |
| dependent value nesting | d nested calls each deducing `X<A + 1>` | 0.00 s @12, 0.01 s @48 |
| declarations of one name | n function templates, one parameter list, distinct result types | 0.04 @400, 0.09 @800, 0.24 @1600, **0.80 s @3200 — quadratic** |
| whole PA23 corpus | 405 files, one process each | 2.70 s warm, no crash, valgrind clean |

Why the address layer stays linear: an address is interned once per declaration
and held on it (`SemaEntity::address`), so a name read n times is one interning
and n reads; the argument is a `TypeKind::Value` entry keyed by that identifier,
so two namings of one object are one type and one specialization; and the
read-back at a use builds two dump nodes and looks nothing up. The abstract
declarator is tried only where the *named* one failed, and it fails at the top
of one declarator rather than inside each level of it — so the retry is one
extra pass over that declarator and not one per level.

Why the rest stays linear: 14.8.2p8's attempt is a `try` block per candidate,
which costs nothing until a candidate actually fails; `specialize` and
`instantiate_class` are still interned per argument list, so a failed attempt
re-reads only what it built itself; and a dependent value argument is interned
per (reading, spelling) so one spelling under one head is one type however many
times a declarator writes it. 14.7.1p1's two cycle marks — `TemplateInfo::
choosing` and `SemaAnalyzer::specializing_` — are one hash insert and one erase
apiece beside a list that is interned anyway. The one quadratic is PA22's
`TemplateSignature::equivalent` chain walk, named under the known gaps above.

## Completed Checkpoints

| # | checkpoint | owner | what landed | measured |
| --- | --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce.{h,cpp}`, `sema_template.{h,cpp}`, `sema_specialize.cpp`, `sema_analyzer.{h,cpp}`, `sema_function.cpp`, `sema_declarator.cpp`, `sema_scope.{h,cpp}`, `ast_model.h` | `Substitution` scope around all three deduction entry points and around the written-argument-list `specialize`, so a refusal discards the candidate; `Instantiated` marks a refusal that came from reading an instantiated class body, which is outside the immediate context and still refuses the program; 14.1p3's unnamed *type* place is declared into the head's region, so `template<typename, typename>` is a head an argument list fits; a dependent value argument keeps its spelling, its place and its region, and 14.7.1p1 reads 5.19 over them again, so `X<A + 1>` in a function template's declarator substitutes; `substituted_region` binds a value place to its constant; `dependent_member_type` rebuilds the stand-in over the class *this* substitution made and refuses a member a concrete class does not declare; 14.5.6.1p5 rather than 13.1's parameter-list index is what pairs two declarations written under heads; 14.8.2.1p4's qualification conversion on the argument of a pair; 14.6p2's `typename` over a prefix that is a bare place. The audit added 14.7.1p1's bound on the attempt asking for itself (`TemplateInfo::choosing`, `SemaAnalyzer::specializing_`), which is what three stack-overflow crashes were; 14.8.2p8's lexical order, so a leading result type is substituted before the parameter list (`SemaEntity::trailing_result`); and 14.1p12 over the pair 14.5.6.1p5 now settles | 246 → 292 / 400; through-pa22 2948 / 2948; 0 corpus crashes against 3; sweeps above |
| 2 | 14.3.2p1: an address argument is which object it designates | `sema_template_head.{h,cpp}`, `sema_value_expression.cpp`, `sema_expression.cpp`, `sema_constexpr.cpp`, `ast_parser_class.cpp` | 14.1p4's second and third bullets and 14.1p8's adjustment (`non_type_place`, `address_place`), so a place of pointer, lvalue-reference, array or function type is one an argument reaches; 14.3.2p5 at such a place is `at_pointer_place`/`at_reference_place` over 5.19p2's `ConstantAddress`, so the argument's bits are the interned object and two namings of one object are one specialization (`address_argument`); 14.3.2p1's linkage and 14.3.2p3's subobject refusals, which the two `-bad` fixtures pin; `&` and `nullptr` in the spelling reader, and 5.3.1p3's operand read as storage rather than as a value (`designating_`), which 8.3.2p1 asks for at a reference place with no `&` written; 4.3p1's function name read as which function it is, so `H<f>` reaches a function-pointer place; 14.1p3's *abstract* declarator in a non-type parameter, so `template<M *>` parses at all; the argument read back where the name stands as the object's own name, `&` on it, or the function's name (`address_value`), so `++Ref` writes the object and `*P = true` writes it through `addr`; and 3.2p3's demand for the definition of a function an argument names | 292 → 299 / 400, plus 5 new course fixtures; through-pa22 2948 / 2948; 30-shape sibling sweep agrees with the reference on all 20 accepted and all 10 refused; sweeps above |
