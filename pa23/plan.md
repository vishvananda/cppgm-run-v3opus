# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `ast_parser.cpp` / `ast_parser_class.cpp` / `ast_parser_declarator.cpp` /
  `parse_deferred.h` — 14.1p3's abstract declarator, 10p1's `class-or-decltype`
  whose operand tree the arena keeps beside its spelling, 8.3.6p1's default
  argument, which is the one form a parameter-declaration writes after its
  declarator and so is where 8.2p1's ambiguity is settled for
  `holder made(seed{});`, and 9.2p2's complete-class context, whose owner is
  `parse_deferred.h`: `DeferredReadings` is the function-bodies put aside where
  the member specification met them - 8.4p1's ctor-initializer and
  compound-statement both - each read at the `}` that completes the *outermost*
  class of a nest, because the clause completes a class within its own bodies
  "including such things in nested classes".  Beside each entry travel the
  regions its own class gave it, which the class around it has left by then: the
  qualifier its members were remembered under and the names it declared,
  outermost first, with 14.1p2's clause and 8.3.5p10's places declared inside
  them.
- `sema_name.{h,cpp}` — the terminals PA10 flattened a name into, read back:
  which `<` opens 14.2's list and which `>` ends one, over a spelling whose
  separators say where each token began.
- `sema_template_head.*` — 14.1p2's places, 14.1p9's defaults, the bound each
  was written under, 14.3.3p1 of a whole settled argument list, and
  `type_spelling`: the one name every naming of one argument list has to reach,
  written from the type with 7.1.6.1p1's qualifier trailing at every level.
  `bind` is the one door an argument reaches a place through and `run_binding`
  is what a *pack* place has to be told, because 14.5.3p1's settled run is
  interned by its elements and says nothing of the place it came from.
- `sema_deduce.{h,cpp}` — 14.8.2, and 7.1.6.4p6, which hands it the whole of
  what a placeholder stands for: `from_initializer` is one P/A pair over the
  declarator's own type with `TypeTable::placeholder_type` standing where `auto`
  was written, and `placeholder_declaration` is that pair with p4's "no
  non-static data member" and p7's "every declarator of one declaration deduces
  the same type" around it.  `Deduction` reads the P/A pairs;
  `Substitution` is the *scope* one attempt at building the declaration runs
  in, `Instantiated` is the error that escapes it, and `naming_value` /
  `settled_value` are 14.4p1's identity of a value a dependent qualified-id
  names: the prefix, the member and the place, which is what every spelling of
  one name agrees on where the characters do not.
- `sema_specialize.cpp` — 14.5.5.1p1's match, 14.5.5.2p1's ordering, 14.5.1p1's
  variable template and 14.5.7p1's alias.
- `sema_template.cpp` — 14.3p1's substitution (`substituted`), 14.7.1p1's
  instantiation, and `DependentReadings`: the constructs a pattern left
  standing, each with the region and the bound 14.7.1p1 reads it again under.
- `sema_scope.{h,cpp}` — 3.4's lookups, and 14.6.4.2p1's bound on them:
  `declared_serial` per namespace-bound declaration, `ReadingBound` per
  reading, `written_bound` per construct being recorded. Every definition
  14.7.1p1 reads again carries the bound it was written under - the pattern,
  each partial specialization's body, each out-of-class member definition, each
  body `template<>` wrote out, and each body a reading put aside.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5 and
  14.5.6.2's ordering of two function templates.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused,
  gathers 13.3.1.4p1's converting constructors, and asks 14.6.4.2p1 of 3.4.1's
  half of a set and not of 3.4.2's. `sema_constexpr.cpp` draws the same line
  where a fold gathers a set of its own.  `one_specialization` is 13.4p1 asked
  by a reading that hands its set to nobody - 5.19's `&` and the fold's
  id-expression - which have no target type to choose with and so name the one
  declaration the written list completed and none where it completed two.
- `sema_layout.{h,cpp}` / `type_model.cpp` — 5.2.2p4's boundary, which reads
  12.8p12's copy, 12.4p8's destruction, 3.9.1p8's floating storage and 10.4p2's
  abstract class; and 8.3.2p5 / 8.3.4p1's *door*, which is the entry a
  declarator derives a type through as against the entry that interns one.
  `ClassLayout` is the storage one class definition is laid out over, held as
  the walk that places each subobject into it, so 9.2p13's order is the order of
  its steps - the vpointer, the bases 10.1p4 does not share, the members, and
  last the shared ones, whose place is the *complete* object's answer.
- `sema_derivation.{h,cpp}` — 10p1's one walk of the derivation, and 10.1p4's
  fact of it: which base-specifier wrote `virtual`, which is what 5.2.9p11,
  5.2.9p12 and 4.11p2 all refuse a conversion for and what says the byte the walk
  summed is the complete object's.
- `sema_builtin.{h,cpp}` — 1.4p8's reserved functions, 3.7.4's allocation
  functions, and the calls the implementation answers itself: the two whose
  meaning is no function type at all - `__builtin_constant_p`, which asks about
  its operand, and 20.8.2p1's `__builtin_invoke`, which is whichever *call* its
  first expanded operand and the rest make.
- `sema_conditional.{h,cpp}` — 5.16, whose p3 is the one conversion in the
  expression layer asked of *both* directions at once and applied only where
  exactly one of them can be made.  Its three bullets are three different
  writings: an lvalue reached by a reference that binds directly, a slice -
  which is *one* copy-initialization of the result object and not a step written
  above one, so `transfer_arm_to_result` is where it is written and 13.3 is what
  chooses the constructor the base subobject binds through - and an ordinary
  conversion to what the other operand is worth as a prvalue.
- `sema_expression.cpp` / `sema_lifetime.cpp` / `sema_init_list.cpp` — the
  refusals a substitution failure is made of: 5.7p1 and 5.2.6p1's completely
  defined pointee, and 8.5.4p7's narrowing of a clause through 13.3.3.1p4; and
  8.5.3p5's object, which is what a braced-init-list at a reference place
  initializes and what the fact it leaves has to carry, because every reader
  below asks the list what type it came to and a reference is not one.
- `sema_class.cpp` / `sema_lifetime.cpp` — 12.9's inheriting constructor: p1's
  candidate set and the head each of them was written under, and p8's forwarding
  of this declaration's own places to the base's.  `special_member_type` is also
  where 12.1p1's own parameter list is built, which is the second reader of
  14.5.3p4's settled run and the one 14.7.1p1 asks a second time.
- `sema_explicit.cpp` — 14.7.3's two clauses over a function or an object:
  p11's count of the templates one head could be a specialization of, which is
  refused at zero and at a written type fitting none, and p5's refusal over a
  member of a class the program wrote out.
- `sema_declarator.cpp` / `sema_pack.cpp` — 8.3.5p10's places a parameter clause
  declares, through the one door `bind_place`, with 14.5.3p4's run carried onto
  the first of them and each of the rest linked back: once the arguments settle
  the run, the declaration is the only carrier the fact has.  `resolve` and
  `qualified_in_type` are the two walks over a qualified name - one whose prefix
  is spelled as a name and one whose prefix is 7.1.6.2p1's decltype-specifier -
  and the second is the one door all seven readers of such a name go through, so
  each of the *three* asks a component gets is written there once: 3.4.3p1's
  lookup, 14.2's class-or-alias template-id through `template_id_entity`, and
  14.8.1p2's function template-id through `template_specializations`, which is
  the one no lookup of a spelling can answer because no region declares a
  specialization.
- `lowir_abi.cpp` — 14.2's encoding of an argument list, and 14.1p4's own
  answer to which places bind an address.
- `lowir_lower_object.cpp` / `sema_lifetime.cpp` — 5.2.2p4's parameter object and
  5.2.2p7's argument the ellipsis matched, which is the same object question
  asked with no parameter on the other side to answer it.  `passed_operand` is
  the one lowering door every argument walk reaches, and `name_ellipsis_object`
  is the one sema door the two walks that write a call ask - `finish_call`'s,
  which an ordinary call and 13.3.1.2p1's operator call share, and the
  constructor's own, which 12.1p1's leading object parameter keeps apart.
- `lowir_lower.cpp` — which definitions this object file holds, which is two
  questions and not one: whose definition it is (7.1.2p4, widened by 14.7.3p5)
  and whether this unit writes one nobody used (3.2p4, asked of whether a
  template-id names the class) - and, of a definition it does hold, which of
  12.1's two entry points it owes, which 9.3p2 answers of where the definition
  was written, 14.5.2p1 of whether the member is a template and 10.1p4 of whether
  the class has a virtual base - and that last answer is read twice, because
  `writes_base_entry` says whether *both* names are owed and `abi_variant` says
  which one the single definition is written under.
- `lowir_image.cpp` — 3.6.2p2's *three* spellings of one value: the operand
  naming a scalar object's whole storage, spelled at the LowIR type; an item
  standing for one clause the program wrote, spelled with that clause's own
  digits and signedness; and `made_zero`, 8.5p7's and 8.5.4p3's zero, which no
  clause stands for and which is spelled from the type alone.  Beside them
  8.5.1p7's elements no clause reached, which are the zero of an *element* and
  not a run of bytes - one item per subobject of it, by the walk
  `global_subobjects` makes over a written list asked of the type.  Which
  objects reach any of them is 3.6.2p2's `constant_initialization` and not
  5.19p3's `constant`: one fold answers both clauses and only the first is about
  storage.  Two walks lay that storage out - `global_constructed` down the
  constructor's own definition, which is where 5.19p2's addresses are, and
  `constant_image` down the fold's interned list, which is where an array's
  elements are - and 10.3p1's pointer is the first item of a complete object in
  both.

## Current Failure Map

490 tests (400 handout + 90 course), **487 passing** (handout 397 / 400).  The 3
left, all handout, are each the *reference's* own answer against `g++`'s and
this build's - there is no shape left where a fixture wants behavior the two
agreeing oracles say this build lacks:

| group | n | shape |
| --- | --- | --- |
| LowIR text mismatch | 2 | 500-bool-alias-function-template-result-metadata (the reference folds `value ? 0 : 1` over a `bool_constant<true>` to `return i32 0` and writes no `operator bool` at all, which no clause makes constant at `-O0`), 500-tcc-member-constructible-pack-sfinae (14.8.2p8 at a default *template* argument the reference does not fire on: 4 here and in `g++`, 11 in the `.ref`) |
| the reference's own answer pinned by a `.ref` | 1 | 300-scalar-pseudo-destructor-noexcept, 5.3.7p3 at a pseudo-destructor call - 11 `noexcept` shapes were run through the three oracles and the reference answers `true` for *every* writing of a pseudo-destructor call over a potentially-throwing operand (`p->~T()`, `(*p).~T()`, `static_cast<int *>(p)->~T()`, `(p->~T(), 0)`) and `false` for the same operand written as `*p`, `p + 1` or on its own, so its 5.3.7p3 walk does not descend into 5.2.4p1's postfix-expression - while its *lowering* of that expression emits the call, byte-identical to this build's.  `g++` agrees with this build at all 11 |

### Known gaps diagnosed but not landed

Only the ones still live.  The rows checkpoint 32's probes reached are re-probed
on this turn's binary; the rest carry the wording of the turn that recorded
them.

- **13.3.3.1.5p1 ranks a one-element list by the element's own conversion.**
  `f({1})` over `f(int)` beside `f(double)` is `f(int)` in `g++` and no best
  declaration here, on the fold path *and* on the ordinary one - so it is
  13.3's ranking of a list-initialization sequence and no part of 5.19.
- **8.3.5p5's adjustment is applied to the parameter *object* and not only to
  the function type.**  A `T const value` parameter binds in a fold under the
  adjusted `T`, so its name reads as an `int` rather than a `const int` lvalue -
  which is why `designates` is set for a reference's name alone.  Setting it for
  every name makes `rank(T &)` beside `rank(T const &)` tie on conversions
  instead of on cv, and 14.5.6.2's ordering then has to break the tie;
  `pa21/course/pa21/300-which-declaration-a-constant-expression-calls.t` and
  `pa21/tests/spec/300-constexpr-const-by-value-template-parameter.t` both pin
  the prvalue reading, and both fail if the wider fact lands without the
  ordering.
- **A subscript of a deeply nested array recurses per dimension.**
  `a[0]...[0]` over 20000 dimensions segfaults; the declaration alone and its
  `= {}` do not, so the reader is the subscript's and no part of 8.5p7's walk.
- **20.8.2p1's pointer-to-member arms are refused one layer down.**  The other
  four bullets of `INVOKE` are 5.5's `(t1.*f)(t2...)` and `t1.*f`, which have no
  reader at any tier here.  `BuiltinReading::invoke` refuses such a callable as
  14.8.2p8's failure, but nothing reaches that refusal: `&S::m` on its own is
  `an expression is outside the PA15 lowering subset`, so the program dies in the
  lowering rather than dropping a candidate, and a detector written with an
  `f(...)` fallback is refused here and answered by the reference's data-member
  arm.  `pa34/tests/run/800-builtin-invoke-run.t.1` and
  `800-builtin-invoke-pointer-like-member-run.t.1` pin all three arms, including
  C++17's `(*t1).*f` through a smart-pointer-like class, so they are the later
  milestone's.
- **`__has_builtin` has no reader at any tier.**  It is a preprocessor question
  and `pa34/tests/run/800-builtin-invoke-run.t.1` opens with one, so a program
  that asks before writing the call is refused in phase 4.
- **The fold has no reading of the three calls the translation answers itself.**
  `ConstexprReading::callee_candidates` asks `BuiltinReading::reserved` and not
  `answers`, so `__builtin_invoke`, `__builtin_constant_p` and `__builtin_abort`
  written where 5.19 reads are `is written where a constant expression calls and
  names no function`.  The reference refuses the same programs - `static_assert
  unevaluated`, `unsupported constexpr variable initializer` - so no fixture can
  pin either answer.
- **`::__builtin_invoke` is accepted here and `unknown function` in the
  reference.**  3.4.3.2p1 is what `reserved` already reads for
  `::__builtin_strlen`, and the three calls the translation answers itself now
  read it the same way; the reference draws the line the other way for
  `__builtin_invoke` alone, and agrees with us on `::__builtin_constant_p`.
- **`__builtin_abort` is outside the PA15 lowering subset**, written with a
  nested-name-specifier or without one, and translated by the reference.  It is
  PA12's own recognition with no lowering behind it and is older than the
  template layer.
- **A program that declares `int __builtin_invoke(int);` names its own
  declaration here and the builtin in the reference.**  17.6.4.3.2p1 makes such
  a program ill-formed with no diagnostic required, so either answer conforms;
  ours is the one every other name the implementation reserves already gets,
  which is that ordinary lookup is asked first.
- **13.5.2p1's arity of an operator function has no reader at any tier.**
  `template<class T> int operator-(tag, T, int = 0)` is a declaration `g++`
  refuses and the reference and this build accept.  The reference implements the
  clause at no arity, so no fixture can pin it.
- **14.8.2.4p3's first bullet at a call written through an object is `g++`'s
  answer against the reference's.**  Three programs `g++` accepts and the
  reference refuses, so no course fixture can hold them.
- **14.8.2p8 at a default template argument the reference does not fire on.**
  `500-tcc-member-constructible-pack-sfinae.t` runs to 4 here and in `g++` and
  to 11 in the checked-in `.ref`.
- **A `static const` member whose initializer calls a non-`constexpr` member
  template** is refused by `g++` and translated by the reference and by us: a
  call written where 5.19 reads asks the declaration chain, and a static data
  member's initializer does not.
- **The reference bails to a startup body where this build lays out the image.**
  A union and `struct big { int a[400]; } g[3] = {{{1}}}` are two shapes it
  writes as one `zero n` run plus code and this build writes as items, and
  checkpoint 43 added seven more on the array axis: an array whose element class
  has a base, two bases, an empty base, a member of class type, a member of
  array type, a mem-initializer carrying an argument to a base, or three levels
  of nesting.  The reference lays out an array of a *flat* class and bails as
  soon as the element's own walk reaches a subobject; `g++` agrees with this
  build at all seven and all seven run to its value.
- **Three shapes where the reference lays out an image this build leaves to the
  program.**  A class with a *bit-field* beside an ordinary member is
  `zero 4, i32 3` there for `A g = A()`, `A g{}`, `A g = {}` and over an array,
  and a startup body here - 9.6p2's run of bits inside a unit its neighbours own
  is no item of its own, which is what both image walks refuse; `A g;` over the
  same class is byte-identical.  An *array* whose element class has a pointer
  member is the second: 5.19p2's address is a value `constant_image` holds no
  item for, where `global_constructed` writes it for one object.  The third is
  `struct A { int i = 3; A() { } };` over an array, which the reference
  constant-initializes although 3.6.2p2's second bullet asks for a constexpr
  constructor and 12.1p5 gives a *user-provided* one none; `g++` agrees with this
  build and runs a startup body.
- **3.6.2p2 at thread storage duration is `g++`'s answer against the
  reference's.**  The clause names "static or thread storage duration" in all
  three of its bullets, so `thread_local A g;` over a class with a constexpr
  default constructor is `i32 3` here and in `g++` and `zero 4` plus a startup
  body there; no fixture writes it.
- **An array of a class that dispatches is left to the program**, here and in
  the reference: 10.3p1's pointer is no entry of the list the fold arrived at and
  the item spelling it is read off the *type*, so the run would be one fact
  repeated over storage the fold said nothing about.
  `pa18/course/pa18/300-static-image-vpointer` pins it; `g++` lays it out.
- **A constructor's mem-initializer of a *reference* member is ordered
  differently from the reference.**  `A() : r(one)` writes the address before
  the member's own place here and after it there, in a startup body neither
  binary folds.  It is older than the image axis and no fixture pins it.
- **8.5p7's elementwise zero of an array of *class* type is one span store per
  element here and the members there.**  `zero_object` writes `store i64 0` over
  a `{char; int;}` element where the reference writes the two members - and one
  line away, for `padded a[2] = {}` as a local, the reference calls an aggregate
  helper function this build has no counterpart for, so it does not answer the
  shape the same way twice.
- **An integer literal at a floating item is `f32 3` in both builds, and
  `lowir2cy86` reads it as bits rather than converting.**  `float a[] = {3}` runs
  to 0 rather than 3 from the *reference's* own LowIR as from this one, so the
  item spelling the `.ref` pins is one the scaffold below cannot run.
- **3.7.1p3's object a block declared spells its image as its own body would.**
  `static float x = 1.5F` is `1.5F` there and `1.5f` here where a namespace-scope
  object normalizes; a value the reference does not fold - `(float)3`,
  `1.0f + 0.5f` - is `zero` plus a guarded body there and an item here.
- **`kZeroImageLimit` and `kZeroSpanLimit` are deliberate divergences on the
  same axis.**  The
  reference writes one store per element at *every* size - `int a[1000000] = {}`
  is 2,000,010 lines of LowIR and 16.23 s there against 12 lines and 0.00 s here
  - so above 16 ints a local array's zero disagrees, and above 4096 bytes an
  image's does.  Raising either would turn those diffs and make an unbounded
  array's zero unbounded work; the written-clause axis has no such limit and
  needs none, because there the reference does the same work 20x slower.
- **The reference has no 8.5.4 in a fold at all.**  10 of 12 shapes of a
  braced-init-list argument to a `constexpr` call are `static_assert
  unevaluated` there and translated by `g++` and by this build, as are
  `pick()(3)` through a returned function pointer and
  `char (&)[sizeof(T) == 4 ? 1 : 2]` as a parameter.  Checkpoint 25's whole
  constexpr half is therefore beyond what any course fixture can pin.
- **An array at a reference place in a *fold* is refused.**
  `template<class T> constexpr int inner(T const & t) { return sizeof(t); }`
  over a `static int const v[2]` is `no declaration of inner accepts the
  arguments of a call` here and translated by both oracles, with and without a
  template.  It is the fold's half of the sentence checkpoint 26 fixed in the
  expression layer.
- **14.6p2 at a reading a dependent context defers.**  Widening the clause to
  every dependent prefix costs a pa20 course fixture; the reference implements
  it at no shape at all, so `g++` is the only oracle.
- **A template-id written after a decltype prefix *without* `template` is more
  correct here since checkpoint 41.**  `decltype(A::make())::box<int>` is a type
  in `g++` and in this build and `unsupported namespace-scope decl-specifier-seq`
  in the reference, which reads the clause only where the keyword is written.
  The form with the keyword is what the course fixture pins.
- **The reference reads no *id-expression* through a decltype prefix at all,
  and no plain member call through one.**  `&decltype(A::make())::template
  pick<int>` and the same at a value are `unknown id-expression` there at every
  one of 24 shapes, and `decltype(A::make())::pick(3)` over a non-template
  member is `unknown function` at 12 more; `g++` and this build translate all
  36 since checkpoint 42.  The *call* of a template-id is the one of the three
  the reference reads, which is what the course fixture pins.  It also accepts a
  **private** member template reached that way, where 11.2 refuses it here and
  in `g++`.
- **13.4p1's target type at a non-type template-argument place has no reader.**
  `H<&A::template pick<int> >` at a place of type `int (*)(int)` beside two
  declarations the list completes is translated by both oracles - the place's
  own type is what 13.4p1 chooses with - and is `names more than one function
  template specialization` here, at the decltype spelling identically and on
  every binary back through checkpoint 41.  It is `folded_name`'s missing half
  and no part of the door checkpoint 42 opened.  The same place read the other
  way about - one declaration, `H<&decltype(A::make())::template pick<int> >` -
  is accepted here and refused by both oracles.
- **14.6p8 answers a non-dependent name in a class-template body and stands one
  in for the same name in a function-template body.**  A detector with no
  `f(...)` fallback over a prefix that is not a class is refused where an enum
  initializer in a class body writes it - which is `g++`'s answer - and accepted
  where a `return` statement in a function template writes it, because
  `nonthrowing_operand`'s sibling stands a value in there.  Both are 14.6p8's
  no-diagnostic-required and the reference accepts all ten shapes; 14 of the 246
  shapes checkpoint 41 swept are the refusing half and 11 the accepting one.
- **A pack pattern in a partial specialization's own argument list.**
  `D<void_t<typename T::m...>, T...>` leaves the primary unsupported.
- **`TypeTable::substitute` rebuilds a discarded-argument naming structurally**,
  where `SemaAnalyzer::substituted` reads the type-id again.  Both shapes
  reachable through it come out right because their elements are settled by
  then.  It does not walk a dependent prefix either, which a member type and a
  member value both stand behind.
- **A dependent value that is *not* one name is still a fact of its spelling.**
  `A<T>::value + 1` and `sizeof(T) == 4` intern by `dependent_expression_key`,
  which normalizes a template parameter's name and copies every other word - so
  two declarations of one template that spelled a typedef differently inside an
  arithmetic argument are still two.  Checkpoint 31 answers the whole of what a
  qualified-id names and no part of what an operator computes over one.
- **8.3.1p4 and 8.3.3p3 at a deduction, where the oracles disagree with each
  other.**  `probe(T *)` over `T = int &`, a member pointer to a reference or to
  `void`, and `T C::*` over a non-class `C` are three answers no two oracles
  share.
- **Two partial specializations reached through a non-deduced array bound are
  not ordered against each other.**
- **14.8.2.3p4's second sentence at a reference the *reference binary* does not
  read.**  `operator T()` reaching a required `const holder &` deduces
  `T = holder` here and in `g++`, `T = const holder &` there; no fixture can pin
  either.
- **An *item* of *unsigned* type is spelled with the digits the written clause
  had in the reference**, which writes `u32 -1` for `(unsigned int)-1` and
  `u8 -1` for `(unsigned char)-1` where this build writes the unsigned value.
  It is the integral half of the sentence checkpoint 28 answered for the
  floating one, and no fixture pins it.
- **A decltype-specifier in a member template's declarator is read at the point
  of instantiation and not where it stands**, so `decltype(sizeof(*this))` in a
  member *template* is a program both oracles refuse and this build translates.
- **14p2's "a local class shall not have member templates" has no reader**, and
  the reference names such a member as a member of the function where this build
  names it as a member of none.
- **8.3.5p8's function returning an array or a function is not refused**, so
  `T()` over `T = int[3]` derives a type `g++` refuses; the reference accepts it
  and no fixture writes it.
- **14.1p4's fourth bullet has no layer below it.**  A pointer-to-member place
  is refused because `int S::*p = &S::m;` is.
- **13.1's index cannot key a template declaration by what 14.5.6.1p5 asks**, so
  `declare_function` walks the chain.  3200 redeclarations of one template, 3200
  overloads of one name and 3200 templates of distinct names are 0.07, 0.08 and
  0.10 s, so the walk is what is left recorded and not a measurement.
- **8.3.6p9's default *function* argument is read at the call and not where it
  was declared.**  `int g(int n = late());` above `int late();` is refused by
  both oracles and translated here, with no template in the program.
- **A static data member's *dependent* initializer is read past the class
  body**, so `static const int v = late(T())` reaches a `late` declared below.
- **`void *p; p[0];` is refused here and by `g++` and accepted by the
  reference.**  A subscript detector cannot be pinned by a fixture at all.
- **12.9p2's default arguments, the ellipsis a constructor's type drops, 12.9p3
  where the two declarations of one list are not templates, a constructor
  declared beside an inherited one, and 12.9p8's by-value parameter** are five
  shapes where the reference or `g++` stands alone; all three oracles run the
  programs to the same value and no fixture writes them.
- **7.1.5p4's "every non-static data member shall be initialized" at an
  inherited constructor is `g++`'s later model against the standard's.**
- **9.2p2's other three complete-class contexts are still read where they
  stand, and the reference translates all three.**  Checkpoint 35 deferred the
  member function body and checkpoint 36 the ctor-initializer beside it and the
  bodies a nested class writes; a *default argument*, a
  *brace-or-equal-initializer* and an *exception-specification* are still read
  at the member specification, so `int f(bool b = self().pop<long>(v))`,
  `bool m = self().pop<long>(v);` and
  `int f() noexcept(sizeof(self().pop<long>(v)) == 1)` written above the
  `template<class U> bool pop(U &)` they name are `is not a translation unit`
  here and translated by the reference and by `g++`.  All three stand inside the
  *declarator*, which is read before the definition is known to be one, so none
  travels with the entry a function-body does - the mem-initializer-list, which
  checkpoint 36 moved, is the one of the four that stands after it.  A course
  fixture could pin all three.
- **A function-try-block member has no reader at any tier.**  `int f() try {
  return 1; } catch (...) { return 2; }` is refused here, written in a class or
  outside one, and translated by the reference and by `g++`.  It is 15.3p1's own
  gap and older than the template layer.
- **A *template* parameter pack at an out-of-class member definition.**
  `template<template<class> class... Cs> int box<Cs...>::total()` is refused
  here and by the reference, and its member-template form is `total is defined
  twice` here - so `run_binding`'s third tier has no shape a fixture can pin.
- **14.5.3p4's "shall be expanded" has no reader.**  `sizeof(ts)` and
  `int n = ts;` over an unexpanded function parameter pack are accepted here and
  by the reference and refused by `g++`.
- **An empty run of *values* at a member template's definition is refused by the
  reference.**  `constants<>::total()` over a `template<int... Ns>` whose member
  template writes `sum(Ns...)` is `unknown non-dependent name in function
  template body Ns` there, and translated by `g++` and by this build since
  checkpoint 35 - so the course fixture pins the shapes the reference agrees on
  and this one has no oracle.
- **The reference refuses `this->v` in a member template's trailing return
  type**, at six of twelve shapes swept, so no course fixture can hold them.
- **`this` written in a *static* or `friend` member function template's
  declarator is 14.6p8's no-diagnostic-required here.**
- **5.3.7p3 at a pseudo-destructor call, where the reference disagrees with the
  standard.**  `spec/300-scalar-pseudo-destructor-noexcept.t` pins the
  reference's answer, so it cannot pass without adopting a rule both other
  oracles refuse.  Checkpoint 41 placed exactly what that rule would have to be:
  the walk must not descend into 5.2.4p1's postfix-expression, which is the one
  subexpression the clause itself says *is* evaluated - and the reference's own
  lowering of the same expression emits the call, so the answer it gives is not
  one a coherent model of the operand produces.
- **`decltype((h.*f)(a))` written as a trailing return type is refused here** and
  translated by both oracles, with no pack in the program.
- **`sizeof` over a function name is accepted here**, where 5.3.3p1 is
  ill-formed and both oracles refuse; there is no template in the program.
- **A plain class nested in a class template gets no `out_of_class_definition`.**
  `template<int N> O<N>::B::B() : d{0} {}` owes both entry points in the
  reference and writes only the base entry here, where the same definition
  directly in a class template owes both since checkpoint 27.
- **A prvalue of an empty run at an aggregate's clause is more correct here.**
  `Y y = { X(a...), X() }` over an empty run zero-initializes both elements here
  and in `g++`; the reference calls X's default constructor for the first and
  zeroes the second, one clause apart.  5.2.3p2 is what says `X()` is
  value-initialized.
- **A class nested d deep with a body at every level is d^2.5 on both
  binaries.**  100 / 200 / 400 / 800 levels are 0.02 / 0.09 / 0.52 / 3.30 s here
  against 0.01 / 0.07 / 0.41 / 2.65 on the pre-checkpoint-36 binary, a flat 1.2x
  at every depth, which is the one scope and one name-map copy per level a
  nested reading opens.  It is lower order than the walk that already dominates
  and neither is the deferral's: 5000 levels time out on both binaries.
- **`sema_analyzer.h` stands at 2394 of its 2400 lines**, and
  `SemaAnalyzer::construct_object` at 239 of the audit's 240.  Checkpoint 33
  freed the room it needed by moving 1.4p8's and 3.7.4's declarations to
  `sema_builtin.h`; checkpoint 37 moved 5.16's four to `sema_conditional.h`,
  which also took 172 lines out of `sema_expression.cpp` and 21 out of
  `sema_lifetime.cpp`, and spent two of what that freed.  The next declaration
  added at either place needs room freed the same way, by moving a group with an
  owner of its own rather than by trimming comments.
- **14.8.2.1p4's ambiguity at a base-class deduction has no reader, at either
  arm.**  Two bases of one argument that both match P - `D : P<int,char>,
  P<long,char>` against `P<A,char>`, and `held : one<int>, one<char>` against a
  template place's `L<A>` - is ill-formed in `g++` and accepted here and by the
  reference, which take the first.  Checkpoints 29 and 30 made both walks go on
  past a base the pattern *refuses*, which is `g++`'s answer; counting the ones
  they accept is the half no fixture pins.
- **The reference deduces through a *template place* from a base at no shape at
  all.**  Seven programs `g++` and this build translate since checkpoint 30 are
  `unknown function pick` there, and three more are ones where it falls back to
  an `f(...)` the template now beats - so no course fixture can pin
  `match_naming`'s arm.
- **A `decltype` operand naming no member is a hard error at a default
  argument.**  `int probe(T, int (*)[sizeof(decltype(declval<T>().nope)) == 4]
  = 0)` beside an `f(...)` fallback is `no declaration of nope is in scope` here
  and SFINAE in the reference.  The same operand written in a *trailing return
  type* is SFINAE here since checkpoint 33, which is what
  `300-expression-sfinae-decltype` wanted.
- **A member *variable template* at a value place is one argument.**
  `box<A<T>::val<3> >` and `box<A<T>::val<4> >` both mangle `box<T_>`, one
  definition is written for the two declarations that name them, and the program
  runs to 44 where `g++` runs to 34.  Checkpoint 32 made 14.4p1's identity stand
  aside for a member written as a template-id, which the entry cannot carry; the
  collapse is in the reading above it, is the pre-checkpoint answer too, and the
  shape is C++14's, which `g++` refuses under `-pedantic-errors`.
- **11.2's access at a member reached through a settled dependent prefix is not
  14.8.2p8's immediate context, at the *type* half.**  `typename T::type` over a
  private typedef, and a member the class befriended, are translated by both
  oracles and refused here from `instantiate_body`.  Checkpoint 32 gave the
  *value* half the region to ask the question in; `dependent_member_type` has
  none, and the reference asks it at neither half - `f<holder>` over a private
  `value` beside an `f(...)` runs to the template there and to the fallback here
  and in `g++`, so no fixture can hold either answer.
- **14.3.2p5's converted constant expression does not refuse a narrowing
  conversion.**  `box<-1>` at an `unsigned` place is accepted here and by the
  reference and refused by `g++`, with a naming, through an alias template and
  with no template in the program at all.
- **5.19p2's restriction has no reader at a pointer place.**  A `static int *
  const` member initialized with `&g` is read as a constant here and refused by
  the reference and by `g++`, with and without a dependent prefix.
- **A naming at a *reference* place writes no definition of the member.**  A
  `static int &` defined out of class and named at an `int &` place is a global
  plus a startup body in the reference and no symbol at all here; `g++` refuses
  the program.
- **A pack whose prefix a substitution moved is not deduced through.**
  `f(list<A<X,Ts>::value ...> *, Ts ...)` deduces `Ts` in both oracles and is
  `no declaration of f accepts the arguments of a call` here, before checkpoint
  31 as much as after it.
- **5.2.2p7's argument of class type differs from the reference at two shapes
  of sixteen.**  A prvalue a *call* handed back in registers is spilled into the
  `arg` slot after the call there and before it here, and a call whose step
  holds a temporary with a destructor spills its own result here and not there.
  Every other shape - an empty class, a small, a 16-byte and a large POD, a
  named lvalue, a `const` one, two class arguments, one after a scalar, one with
  floating members, one with a non-trivial destructor or copy constructor, one
  through a member ellipsis, and a derived class - is byte-identical.
- **An overloaded name as `INVOKE`'s callable is refused here and by the
  reference.**  13.4p1 gives a value operand no target type, so
  `__builtin_invoke(target, 1)` over two `target`s names nothing; writing the
  call out longhand resolves it in `g++`, which is 20.8.2p1 read as a rewriting
  rather than as a builtin over values.
- **A lambda body may declare no local variable it then names.**
  `[](){ int n = 1; return n; }()` is `no declaration of n is in scope` on this
  binary *and on the pre-checkpoint one*, written with `auto`, at a template
  argument or called outright - so it is 5.1.2's own gap and no part of
  7.1.6.4's.  It is also why nothing can nest one `auto` declaration inside
  another's initializer, which is the one shape checkpoint 37's second reading
  of an initializer could have made 2^depth.
- **`auto x{1}` deduces `int` in `g++` and is refused here.**  7.1.6.4p6 in
  C++11 makes a braced-init-list deduce `std::initializer_list`, which the
  README puts out of scope; N3922 replaced that rule after C++11 and `g++`
  implements the later one.  `auto x = {1,2}` is refused by both readings here.
- **5.16p3's *xvalue* bullet and 5.16p4's xvalue result have no reader.**  Two
  xvalues of one type are an xvalue naming one of the two objects in `g++` and a
  prvalue here, so `1 ? static_cast<B &&>(a) : static_cast<B &&>(b)` costs one
  move `g++` does not make, and the same holds where p3's second bullet converts
  one of them.  All 6 shapes of 128 swept are `unsupported conditional operands`
  in the reference, so no fixture can pin either answer.
- **`1 ? A() : B2()` where each class reaches the other is ambiguous in `g++`
  alone.**  `A` declaring `A(B2 const &)` beside `B2` declaring `operator A()`
  is a conditional `g++` refuses and this build and the reference accept.  It is
  13.3's ambiguity between a converting constructor and a conversion function one
  layer below 5.16, and the reference writes one definition more than this build
  for the same program.
- **5.16p3's cv condition is `g++`'s answer against the reference's.**  `const D
  d; 1 ? d : B()` is refused here and by `g++`, because the class reached is less
  cv-qualified than the operand's own, and translated by the reference.
- **The reference reads 7.1.6.4 more loosely than the standard at five shapes.**
  `auto x = 1, y = 2.0;` (p7), `struct S { auto m = 1; };` and `static auto v;`
  (p4), `auto int x = 1;` (p1) and `const auto * p = f;` over a function name are
  five programs `g++` refuses with this build and the reference translates, so no
  fixture can pin the checkpoint's own refusals.  `auto auto x = 1;` is the one
  the other way about: accepted here and by the reference and refused by `g++`.
- **`new auto(1)` is 7.1.6.4p2's remaining context and has no door**, refused
  here and by the reference and translated by `g++`.
- **The reference refuses 5.16p3 where the conversion is a conversion function
  *template* of an unconstrained class.**  `struct C { template<class G>
  operator const G &() const; }` beside a `W` lvalue is `unsupported conditional
  operands` there and translated by `g++` and by this build - while the same
  conversion written in a *class template* is translated there, which is what
  `500-conversion-function-template-reference-conditional-auto-ref.t` pins.  So
  the clause has an oracle at one site and none at the other.
- **The reference reads a comma operator's discarded left operand only under
  `auto`.**  `auto y = (x, 2.0)` writes a `load i32 $x` there and none with the
  type written out; this build writes none either way, which is 5.18p1 leaving
  4.1's conversion unapplied.
- **The reference alone disagrees at these shapes, and no fixture can pin any
  of them.**  Each was swept and each was left as it stands, because the
  reference's answer is the one a `.ref` would hold and both other oracles agree
  with this build.  It refuses a constructor's ellipsis at a second class
  argument and at a call result, and writes an *empty* init body for
  `C c = C(1, P());`; it reads 5.3.1's indirection as producing the pointer, so
  `(*f)(3)` is one `unary decay` short; it parenthesizes `int (**)[2]` per level;
  it builds a zero-length array and refuses a list at a declared reference or a
  scalar one; it accepts a member template whose two spellings differ in a
  parameter type and then writes no definition for it, writes two definitions of
  one specialization named two ways, and emits a second constructor entry point
  for a base written through an alias template; it defers an unused free
  explicit specialization while emitting an unused ordinary function, defines
  one enumeration twice, and writes an unused out-of-class destructor's
  definition; it zero-initializes and constructs a temporary with a non-trivial
  destructor inside an eh region; it accepts `template<> int f<int>(int)
  throw() {}` and `template<> int S::h()` over a member of an ordinary class,
  which `g++` refuses; it makes a static data member of a written-out
  `template<>` `binding=weak`, mangles a function template's trailing return
  type `T_`, writes its own `<unresolved-name>` four ways of six that `g++` does
  not, and calls a function parameter pack of class type variadic; it runs
  `template<> template<> int S<int>::g(char)` with no argument list to the
  pattern; it says `decltype(sizeof...(N))` is `int`; it refuses an alias
  template whose type-id is a pointer to or an array of a specialization, a
  conversion-type-id spelled with two words, 5.1.1p13's third bullet at a
  `sizeof`, `int&& (*)(int)` as a namespace-scope template argument, and a cast
  on an overloaded function name; it writes an unused `index` for a by-value
  class parameter reached through a base; and it names a function-local static
  inside a conversion function from the conversion-type-id's spelling where this
  build names it from the type's description.  14.7.3p11 where the written type
  fits two templates 14.5.6.2 leaves unordered is the one of the set where a
  refusal would *fail* a fixture rather than pass one.

- **10.1p4's shared subobject has no dynamic offset, which PA28 owns.**  Three
  shapes follow from that and each is refused precisely rather than answered
  wrongly: a class with a virtual base named as a base class itself (the
  subobject moves, and its distance from the object stops being a fact of the
  class the conversion starts at), one that is also polymorphic (10.3p10 reaches
  the subobject through the table, whose entry, construction tables and views are
  PA28's), and the base-object entry point of such a class's constructor (which
  builds no shared subobject and so is a different function - nothing here can
  ask for it, because no base-specifier may name the class).  What the reference
  does instead is a hidden vbase-pointer *parameter*: `D::get` reads
  `(%this, %__vbptr0)`, a by-value parameter of such a class reads
  `(%d, %__pvbptr0)`, and `pa28/tests/general/100-virtual-base-manipulator-hidden-argument.t`
  and its five siblings pin all of it, so the milestone boundary is where the
  handout puts it.  Its own answer at PA23 is not usable as one: for
  `struct B { B(); }; struct D : virtual B {};` it emits `_ZN1BC2Ev` twice, once
  as a function and once as an alias of `_ZN1BC1Ev`, which is an object file that
  does not link - so no compile-pass fixture can pin the shape either way.
- **5.2.9p12 and 4.11p2 are answered for the virtual base alone.**  A cast
  between two pointer-to-member types whose classes are *unrelated*, or related
  through a base 11.2 does not reach, is accepted here and refused by `g++` and
  by the reference: `cast_conversion` returns for any operand that is not of
  class type, so 5.2.10p9's reinterpretation and 5.2.9p12's conversion are one
  unchecked arm.  The clause the checkpoint landed is the one the derivation
  fact answers; the other two are 11.2's and 5.2.10p9's and want the standard
  conversion sequence between two member pointers, which nothing here builds.
  `d.*q` is `an expression is outside the PA12 subset` one layer down, so no
  fixture reaches either answer through a run.
- **The reference writes an object file that does not link for a class with a
  virtual base.**  It emits both ABI entry points, the base one taking a hidden
  `__vbptr0` parameter, and writes `alias object _ZN1VC2Ev = @V__V` beside a
  definition already carrying that object name - so the base entry of the base is
  defined twice, and its own LowIR fails `lowir2cy86` with
  `call @D__D__ov2 expects exactly 3 argument(s), got 2`.  6 of 17 divergences
  swept are that, so no compile-pass fixture can pin what such a unit owes.
- **The reference's implicitly-defined copy-assignment does not assign the shared
  base subobject at all**, where 12.8p28 makes the copy memberwise over each base
  and this build and `g++` both call the base's own `operator=`.
- **The reference rounds the non-virtual part up to the class's own alignment
  before placing the shared subobject.**  `alignas(16)` is `sizeof` 32 with the
  subobject at 16 there and 16 with it at the byte the data reached here, which
  is `g++`'s reading; `#pragma pack(1)` is the same sentence at 8 there, 5 here
  and 13 in `g++`, all three the packed byte.
- **`g++` gives a class with a virtual base a vtable pointer and reaches the
  subobject through it.**  Every `sizeof` and every offset differs from `g++` by
  those eight bytes and agrees with the reference exactly, so this milestone's
  layout is the course ABI's; PA28's hidden argument is what would change it.
- **4.11p2's *implicit* pointer-to-member conversion has no reader at any tier.**
  `int D::* q = p;` over a *plain* base is `an expression has no conversion to
  the type it initialises` here and translated by both oracles, so the shared
  base's refusal is reachable only through 5.4p4's cast notation.
- **15.3p3's handler matching a shared base and 5.5's `.*` through one have no
  reader**, because `try`/`catch` and `.*` are outside the PA12 subset with or
  without a virtual base.
- **The image never holds an object of a class with a virtual base.**
  `constant_image` walks the bases in declaration order and the shared one stands
  after the members, so the walk steps backwards and returns false to a startup
  body.  12.4p5 is what keeps that unreachable: such a class has no trivial
  destructor, so 3.9p10 leaves it no literal type and no constant initializer of
  one exists.
- **A class with a virtual base and a virtual function is refused, as is one
  named as a base.**  Both are programs `g++` and the reference translate.  They
  are what keep every offset this milestone answers exactly the standard's,
  because a shared subobject only ever stands in a complete object here.
- **7.1.5p4's first bullet is a refusal no fixture can pin**: the reference
  translates a `constexpr` constructor of a class with a virtual base, which
  `g++` and this build now refuse.

## Active Checkpoint

**Checkpoint 43: 3.6.2p2's constant initialization is a fact of the
initialization and not of the `const` the declaration wrote.**  Complete.

`struct A { int i = 3; }; A g;` was `zero 4` plus a startup body here and
`i32 3` in the reference and in `g++`, and so was every other object of class
type a declaration wrote no `const` on - a written `constexpr` constructor as
much as 12.6.2p8's brace-or-equal-initializer, an array of them, a static data
member defined outside its class, an object a block declared `static`.  The fold
that answers the image ran for a `const` object alone, because one flag carried
two clauses: 5.19p3 says what a *name* of the object is worth and answers it for
a `const` object only, and 3.6.2p2 says what the object's storage holds and
reads the initialization.  Splitting them is the checkpoint; four walks then had
to be told what the wider fold reaches.

- *Owner and data flow.*  `sema_scope.h` — `SemaEntity::constant_initialization`
  is 3.6.2p2's fact beside 5.19p3's `constant`, over the one value the fold
  already writes.  `sema_constexpr.{h,cpp}` — `fold_declared_object` takes
  3.6.2p2's own question, `before_the_program`, and runs for an object of class
  or array type with static storage duration whatever its cv-qualifiers; where
  5.19p3 did not ask, a refusal writes neither of that clause's facts, so a
  declaration with no `const` gives a later name of it nothing to run out on.
  `sema_analyzer.cpp` — `declare_object_declarator` answers `before_the_program`
  from 3.7.1p1 and the definition together, so `extern holder<box> never_defined;`
  asks its type for nothing.  `lowir_image.cpp` / `lowir_lower.h` — `global_image`
  reads the new fact, `vpointer_item` writes 10.3p1's pointer as the first item
  of a complete object in both image walks, `constant_image` lays out an array of
  class type from the fold's own list where the dump holds one action for the
  whole run, and the definition the walk went through is demanded where the
  constructor initializes anything.
- *Expected complexity.*  One fold per object a declaration defines with static
  storage duration and none for any other declaration, bounded by the object's
  own subobject count; the image walk is one pass down the layout, one item per
  scalar subobject.  `kMaxConstexprSteps` already bounds the element run, so an
  array above 1048576 elements falls back to 3.6.2p1's zero at once.
- *Validation.*  157 generated shapes - 28 class shapes crossed with four
  initializer forms, three declaration places, the array and the out-of-class
  static data member - through this build, the pre-checkpoint binary, the
  reference and `g++`: **143 of 157 byte-identical to the reference through the
  real comparator against 33 before**, 157 of 157 agreeing with the reference on
  exit status, and **144 of 144 buildable ones running through `lowir2cy86` +
  `cy86` to `g++`'s value** with 0 disagreements (13 unbuildable: 12 polymorphic,
  which the scaffold refuses out of the reference's own LowIR identically, and 1
  ill-formed in all three).  Of the 14 left, 7 stood before this checkpoint and 7
  are shapes this build now lays out and the reference leaves to the program,
  each running to `g++`'s value.  pa23 **485 / 488 -> 487 / 490**;
  `through-pa22` 2948 / 2948; file audit unchanged at five `bad-division`
  warnings; 2 course fixtures added, each failing on the pre-checkpoint binary;
  every handout and course `.ref` regenerated with not one tracked file changed;
  0 exits above 1 over 5240 inputs; valgrind clean over 71.

## Next Substantial Checkpoint

**9.2p2's last three complete-class contexts.**  A *default argument*, a
*brace-or-equal-initializer* and an *exception-specification* naming a member the
class declares below them - `int f(bool b = self().pop<long>(v))`,
`bool m = self().pop<long>(v);` and
`int f() noexcept(sizeof(self().pop<long>(v)) == 1)` - are each
`is not a translation unit` here and read by the reference and by `g++`, so an
agreeing oracle exists for all three and the failure is the *parser's*: the `<`
of a member template declared below cannot be read as 14.2's list where the
declarator stands.  All three stand inside the declarator, which is read before
the definition is known to be one, so none travels with the entry a function-body
does; what they need is a deferral of the token range each writes and not the
entry checkpoints 35 and 36 built.

None of the 3 fixtures still failing is reachable by it, or by any other work on
this compiler: checkpoint 41 was the last of the three whose failure named a
behavior the two agreeing oracles have and this build lacks, and each of the
three left is the reference's own answer against `g++`'s and this build's, each
now placed exactly.  Passing them is a separate decision - adopting a rule
`g++` refuses - and not a checkpoint; what is left for the compiler is the
divergences above, which no fixture pins.

## Performance Model

Measured with `/usr/bin/time` on the binary itself, warm cache.  A loop that
spawns `timeout` per run reads the same corpus as 45.9 s against 2.6 s, which is
the wrapper's process floor and not the compiler's; a corpus pass run while a
build saturates the machine reads 5.8 s against 1.9 s, which is that build's.
Absolute times are the load of the turn that took them - one binary reads the
same corpus 8% apart on two consecutive passes - so only the two figures of a
pair, taken on one turn, say anything.

Run evidence has a ceiling that is the scaffold's and not the compiler's.  A
function of five or more parameters (`this` among them), and a function taking a
*class by value* at any arity, come out of `lowir2cy86` + `cy86` as a program
that returns the wrong value or crashes - identically from the *reference*
binary's LowIR, with both builds' LowIR byte-identical through the real
comparator.  A probe that has to be run to a value writes four arguments or
fewer and passes scalars, pointers or references.

Every sweep through checkpoint 31 came out linear in multiplicity and flat in
nesting, at or below the baseline binary measured in a `/tmp` worktree built the
same way; superseded rows are dropped and the shapes that mattered are named in
the ledger.  A shape a checkpoint *un-refuses* has no baseline on the earlier
binary - refusing it is less work, not the same work - so it is timed against
the nearest shape that already worked, and where the un-refused shape has an
exact plain-base twin that twin is the baseline.  The pa23 corpus - 490 inputs,
one process apiece - reads **2.39 / 2.41 / 2.40 s** on this binary against
**2.45 / 2.46 / 2.48 s** on the pre-checkpoint one over three paired passes,
which is the spawn floor and no difference between the two.  What
is live:

| sweep | shape | result |
| --- | --- | --- |
| 3.6.2p2 object multiplicity | n namespace-scope objects of a class with two brace-or-equal-initializers, all now folded and laid out | 0.01 s @400, 0.03 @1600, 0.07 @3200, 0.14 @6400 against 0.01 / 0.03 / 0.07 / 0.16 on the pre-checkpoint binary and the reference's 0.57 / 0.70 / 0.92 / 1.68 - linear, and the fold costs nothing the startup body it replaces did not |
| 3.6.2p2 array multiplicity | `A g[n]` over the same class, one item per element | 0.00 s @1000, 0.01 @10000, 0.10 @100000 and 1.09 s / 272 MB at 1048576 against the reference's 0.57 / 0.83 / 3.77 - linear in the items the image holds, which is the output's own size; `kMaxConstexprSteps` caps the run, so 1048577 elements fall back to `zero n` plus a startup body at 0.00 s and 7 MB |
| 3.6.2p2 subobject nesting depth | a class nested d deep, one member and one nested object per level | 0.00 s @40, 0.01 @80 / @160, 0.03 @320, identical on the pre-checkpoint binary and against the reference's 0.57 / 0.67 / 1.01 / 2.39 - 2d + 2 items and one pass down the layout, so the walk is linear in the subobjects and no part of it is 2^depth |
| dependent-member multiplicity | n function templates each taking a `typename T::type` parameter, each called once | 0.15 s @800, 0.71 @3200, 1.53 @6400 against 0.15 / 0.70 / 1.51 on the pre-audit binary - linear and identical, the axis `require_settled_type` now stands on |
| detector multiplicity | n classes each asked by one `typename U::tag` detector at namespace scope | 0.11 s @800 and 0.53 @3200 against 0.10 / 0.52 on the pre-audit binary - the door checkpoint 41 moved, re-measured |
| the same detectors inside a template definition | the shape the checkpoint un-refuses, n asks written in one class-template body | 0.14 s @800, 0.70 @3200, 1.72 @6400 against its namespace-scope twin's 0.15 / 0.74 / 1.60, which both binaries run identically - so the n^1.3 is the n class templates and n specializations the corpus itself writes and no part of the reading; the pre-checkpoint binary's 0.12 / 0.75 / 1.17 on the un-refused shape is the fallback it takes instead and no baseline |
| decltype-prefixed template-id multiplicity | n namings of `decltype(A::make())::template box<int>::w` | 0.02 s @800 and 0.09 @3200 on both binaries - the specialization is made once and each further naming is one lookup |
| decltype-prefixed *function* template-id multiplicity | n calls `decltype(A::make())::template pick<int>(k)` | 0.01 s @200, 0.03 @800, 0.12 @3200 - linear.  The pre-audit binary refuses the program at every n, so the baseline is the name-spelled twin `A::template pick<int>(k)` at 0.00 / 0.02 / 0.09 and the same call through a plain member at 0.01 / 0.02 / 0.10, both identical on both binaries |
| the same, nesting depth | the call written d deep in its own argument | 0.00 s at d = 2, 8, 32 and 128, as is the name-spelled twin on both binaries - the region is read once per naming and the prefix is not re-read per level |
| 13.4p1 at a reader with no set, multiplicity | n foldings of `&decltype(A::make())::template pick<int>` | 0.00 s @200, 0.01 @800, 0.06 @3200 against the name-spelled twin's 0.00 / 0.01 / 0.03 on both binaries - linear, and the 2x is the one decltype operand each naming reads |
| detector crossed with class-nesting depth | one detector asked at every level of d nested class templates | 0.00 / 0.00 / 0.01 / 0.05 / 0.31 s at d = 20 / 40 / 80 / 160 / 320, identical on both binaries - the depth cost is the chain of regions already recorded and the door adds nothing per level |
| class-layout width and depth | 400 classes of 60 members apiece, and a single-inheritance chain 500 and 1500 deep with two members per level | 0.21 s wide against 0.20, and 0.02 / 0.09 s deep, identical on both binaries - so making the layout a walk with its own state costs nothing per subobject placed |
| virtual-base multiplicity | one class deriving virtually from n classes, and the same over n *empty* ones | 0.02 / 0.04 / 0.10 s @500 / 1000 / 2000 with data members - the same three figures as the same class deriving from the same n classes *plainly*, which is the twin the pre-checkpoint binary also runs - and 0.02 / 0.04 / 0.08 empty at the same n, 0.18 / 0.45 / 1.10 at 4000 / 8000 / 16000, which is n^1.16 over a 32x range: the empty shape is the one that puts an entry in `empty_subobjects` per base and asks each placement against all of them |
| virtual bases crossed with derivation depth | n chains of d empty classes with the leaves named virtually | 0.04 s at (100, 10), 0.20 at (200, 20), 0.48 at (400, 20), 1.12 at (400, 40) - linear in the classes written, so depth does not multiply the pairwise scan |
| pointer-to-member casts | n `static_cast<int C::*>` across a plain base, which is the door `shares_subobject` was added to | 0.01 s @400 and 0.04 @1600 against 0.01 / 0.05 on the pre-audit binary - the two `base_in` walks a member-pointer cast now makes cost nothing at a derivation of one level |
| member-body multiplicity | one class with n member functions whose bodies each name a member template declared below them | 0.00 s @200, 0.01 @400, 0.02 @800, 0.05 @1600, 0.10 @3200 against 0.01 / 0.01 / 0.02 / 0.05 / 0.11 on the pre-checkpoint binary - one balanced-brace skip and one vector entry per body, and the body read once |
| nested-class multiplicity | one class with n nested classes, each with a body naming a member the class around them declares below | 0.01 s @200, 0.05 @800, 0.26 @3200 against 0.01 / 0.05 / 0.25 on the pre-audit binary - one entry handed up per body and one region per nested class, and no class rescans a body the class inside it already skipped |
| nested-class member width | n nested classes declaring 20 members apiece, each with a body | 0.04 s @200, 0.20 @800, 0.94 @3200 against 0.04 / 0.20 / 0.91 - the name map a region carries is copied once per nested class that deferred anything, and not once per body |
| class-nesting depth, one body per level | d = 100, 200, 400, 800 | 0.02 / 0.09 / 0.52 / 3.30 s against 0.01 / 0.07 / 0.41 / 2.65 - d^2.5 on both binaries and a flat 1.2x here, which is the chain of regions a nested reading opens; 5000 levels time out on both |
| ctor-initializer multiplicity | n constructors with a four-entry mem-initializer-list | 0.02 s @200, 0.08 @800, 0.38 @3200 against 0.02 / 0.08 / 0.35 - the list is read once to find the `{` and once where the class is complete, and a constructor writing none pays one field test |
| member-body nesting depth | a class nested d deep with a member function body at every level, both as a local class inside each body and as a plain nested class, and the same with 120 statements in each body | 0.00 s at d = 4, 8, 12, 16 and 20 for both shapes on both binaries, and 0.00 / 0.01 / 0.02 s at d = 2 … 16 for the heavy one, linear in the bytes - so putting a body aside is not a retry and nothing is 2^depth: the entries a class takes out before reading the first of them are what makes one body one reading |
| `__builtin_invoke` multiplicity | n distinct `INVOKE` calls over n classes | 0.05 s @200, 0.24 @800, 0.98 @3200 against 0.05 / 0.24 / 1.02 on the pre-audit binary, and the reference's 0.89 / 2.83 / **39.51** over its own 0.53 s floor - linear here and superlinear there, one ordinary call per operand split |
| `__builtin_invoke` operand width | one `INVOKE` over a run of n expanded operands | 0.00 s at 8, 32 and 128 and 0.02 at 512 on both binaries - the run is walked once, by the `InitializerClauses` the arguments needed anyway |
| `__builtin_invoke` nesting depth | `INVOKE(fwd(), INVOKE(fwd(), ...))` written d deep over a template `operator()` | 0.00 s at 2, 8, 32 and 128 on both binaries, against the reference's 1.00 s at 12, 8.11 at 16 and 122.68 at 20 - flat here and 2^depth there, because the callable is split off the settled operands rather than the reading being made again per level |
| ellipsis class-argument multiplicity | n calls passing a class prvalue through `...`, n passing a named lvalue, and n passing two scalars | 0.01 s @200, 0.03 @800, 0.14 @3200 for the prvalue and 0.00 / 0.02 / 0.08 for both the lvalue and the scalar - so an lvalue crossing an ellipsis costs exactly what a scalar does, which is what making no copy means.  The pre-checkpoint binary refuses all three class shapes, so they have no baseline |
| constructor-ellipsis multiplicity | n declarations `C c(k, P())` over a `C(int, ...)` | 0.01 s @200, 0.06 @800, 0.25 @3200 on both binaries - the object naming is one field test per argument past the declared parameters, at the walk a constructor's list gets |
| value-callee multiplicity | n calls through n pointers to one function, and n calls of n distinct functions by name | 0.01 s @200, 0.04 @800, 0.16 @3200 through pointers and 0.01 / 0.04 / 0.20 by name, identical on both binaries - the question "does this operand name a declaration" is two pointer tests on an arm the call already took |
| dependent-value naming multiplicity | n distinct `traitK<T>::value` namings in one template, and one naming written n times | 0.02 s @200, 0.11 @800, 0.58 @3200 distinct against 0.02 / 0.12 / 0.57 on the pre-audit binary, and 0.00 / 0.02 / 0.08 for the one written n times on both - one map probe per naming read and one entry per distinct one |
| dependent-value substitution multiplicity | n classes, one `take<kI>()` apiece over a declarator holding one such naming | 0.03 s @200, 0.13 @800, 0.61 @3200 against 0.03 / 0.13 / 0.62 - one lookup, one access question and one 5.19 read per specialization, where the spelling form re-read the expression against a rebuilt region |
| two spellings of one naming, multiplicity | n member templates declared with the class's typedef and defined with the parameter, against n that spelled both the same way | 0.03 s @200, 0.13 @800, 0.56 @3200, identical to the one-spelling shape and to the pre-audit binary; the pre-checkpoint binary refuses the two-spelling program at every n and so has no baseline for it |
| dependent-prefix depth | `T::inner::…::value` written d levels deep | 0.00 s @50, 0.00 @200, 0.01 @800 - identical on both binaries, so neither the interning nor the substitution recurses per level twice |
| naming-expansion width | `list<trait<Ts>::value ...>` expanded over a run of n | 0.00 s at 8, 32 and 128 on both binaries - the expansion finds its run by walking the prefix the entry was interned by, where checkpoint 31 also scanned the spelling on every read |
| base-class deduction multiplicity | `tuple<int x n>` over the `impl<I,Head,Tail...>` chain, one `helper<i>` call per index - n calls each walking n bases | 0.00 s @8 and @16, 0.01 @32, 0.02 @48, 0.05 @96, 0.09 @128, against the reference's **1.73 s @96 and 2.77 s @128** over its own 0.56 s floor; the walk is one visit per base subobject and one bindings copy per base it attempts |
| base-list multiplicity | one call over a class with n bases naming P's template, the last of which answers, at the named-template arm and at the template-place one | 0.01 s @200, 0.07 @800, 0.36 @3200 on both arms and identical to the pre-audit binary, so the trial map copied per naming attempted costs nothing measurable |
| base-chain depth | a d-deep single-inheritance chain deduced through both arms at once | 0.01 s @200, 0.04 @800, 0.30 @3200 - identical on both binaries; and no diamond can make either walk 2^depth, because a class holding two subobjects of one type is refused where it is laid out |
| decltype-probe nesting depth | `sizeof(decltype(...))` written d deep at a template argument, the same over `declval<box<...> >().v`, and the same over an innermost operand the reading *refuses* inside a substitution | 0.00 s at 2, 4, 6, 8, 10, 12, 16, 20 and 40, 0.01 at 100 and 0.02 at 200 - flat then linear; the stand-in is the fallback, only the failing path pays, and `check_expression_names` stops at a nested `DecltypeSpecifier` rather than reading it again |
| enum-specifier multiplicity | n globals `enum e {} v;`, against n globals `enum e { x } v;` | 0.01 s @200, 0.03 @800, 0.13 @3200 against 0.01 / 0.03 / 0.16 for the written enumerator on both binaries - the shape checkpoint 30 un-refuses costs what the one beside it cost |
| published-conversion multiplicity | n classes publishing a private base's conversion function, against n declaring one of their own and n publishing an ordinary member | 0.05 s @200, 0.23 @800, 1.06 @3200 against 0.05 / 0.22 / 0.99 and 0.05 / 0.23 / 1.03 on both binaries - one type-id read per using-declaration that names a conversion function and none for any other |
| nested-declarator depth | a non-type place wrapped in d nested parentheses | 0.00 s at 1, 50 and 200, 0.02 at 800 - identical on both binaries, so walking through 8.3p1's parentheses is one step per level |
| empty-run initializations | n classes whose only constructor expands `v(a...)` over an empty run, each used twice | 0.05 s @200, 0.20 @800, 0.88 @3200 - linear; the pre-audit binary refuses the program at every n, which is the second-naming defect checkpoint 28 fixed |
| non-empty expansion multiplicity | n constructors whose `v{a...}` expands over a run of four | 0.06 s @200, 0.27 @800, 1.19 @3200 - the same before, so the hoisted reading is the same one reading |
| out-of-class entry points | n class templates with an out-of-class constructor, each a base of one more | 0.07 s @200, 0.34 @800, 1.74 @3200 - two field reads per question |
| class-element image multiplicity | n globals `pad g[3] = { { k, 'a' } }` over `{int;char;}` | 0.01 s @200, 0.04 @800, 0.16 @3200 - linear, against 0.01 / 0.03 / 0.15 before, which is the 12800 item lines now written where there had been 3200 `zero` runs |
| scalar-element image multiplicity | n globals `int s[3] = { k }`, and n globals `float f[3] = { 1.5F, k }` | 0.00 / 0.02 / 0.08 s on both binaries - so neither the element walk nor the item echo costs an element it already wrote anything |
| empty-list scalar multiplicity | n globals `float e = {}` | 0.00 / 0.01 / 0.04 s - identical |
| specialization-name multiplicity | n specializations named over `int (*)[k]`, and over `int const * const * volatile[k]` | 0.02 / 0.07 / 0.33 s and 0.02 / 0.07 / 0.38 - identical on both binaries, so two strings per declarator level cost what one did |
| class-element nesting depth | `l0 { int; }` nested d deep, `l(d-1) deep[2] = { {} }` | 0.00 s at 4, 8, 16, 31 and 40 - flat; past `kZeroClassDepthLimit` the element is one `zero` run holding the same bytes |
| written global image width | one `int a[n] = {}`, and one `pad wide[n] = {{1,'a'}}` | 0.00 s at every n; 1043 lines at 1024 `int` elements and 20 at 4096, 1549 lines at 512 `pad` elements and 17 at 4096, where `kZeroImageLimit` collapses each - so the axis is bounded by construction and the reference's 2,000,010 lines at a million has no counterpart here |
| declarator depth | `int a[1][1]...[1] = {}` alone, and `C<int const *...*>` | 0.00 / 0.02 / 0.06 s @2000 / 8000 / 20000 brackets and 0.00 / 0.00 / 0.01 @200 / 800 / 3200 stars - so neither the image's element walk nor `type_spelling` recurses per level |
| computed-bound multiplicity | n function templates with `T (&)[N][N], T (&)[N - 1]`, one call apiece | 0.03 s @200, 0.14 @800, 0.59 @3200 - linear, and the same over three distinct bound spellings, so a distinct spelling costs one interning and no walk |
| braced-argument multiplicity | n folded calls whose argument is a braced-init-list | 0.00 s @200, 0.01 @800, 0.03 @3200 - linear |
| pack mem-initializer multiplicity | n folds of one `value(args...)` constructor | 0.00 s @200, 0.01 @800, 0.04 @3200 - linear |
| elementwise-zero multiplicity | n local `int a[2][2] = {}` written as the elements they have | 0.01 s @200, 0.06 @800, 0.24 @3200 - linear, against 0.21 on the pre-checkpoint binary, which is the four stores against one `zeroinit` |
| array-dimension depth | `int a[1][1]...[1] = {}` alone | 0.00 s @2000, 0.02 @8000, 0.06 @20000 - flat per bracket; `zeroed_elementwise` walks down with a loop and stops at `kZeroDimensionLimit`.  The `a[0]...[0]` that reads it back segfaults at 20000, which is the subscript's own recursion recorded above |
| braced array argument multiplicity | n calls passing `{1,2,k}` at an `int const (&)[3]` | 0.01 s @200, 0.03 @800, 0.14 @3200 - linear; an argument that *names* an array at the same place is 0.07 @3200 |
| braced array argument width | one list of n clauses at a reference to an `int[n]` | 0.00 s @256, 0.01 @1024, 0.03 @4096 - linear, against **0.65 s** in the reference binary at 4096 |
| declarations of one name | n redeclarations of one template, n overloads of one name, n templates of distinct names | 0.07 / 0.08 / 0.10 s @3200 - linear |
| overload-set width | one call over n candidate templates that all tie on conversions - the pairwise ordering walk | 0.01 s @128, 0.02 @256, 0.04 @512 |
| placeholder multiplicity | n declarations `auto v = k;` in one body | 0.01 s @200, 0.03 @800, 0.12 @3200, identical on both binaries - linear, so the second reading of an initializer is one extra reading and not a retry |
| placeholder initializer width | `auto x = 0+1+...+n` against the same at a written `int` | 0.02 / 0.04 / 0.08 s at n = 2000 / 4000 / 8000 against 0.01 / 0.03 / 0.06 - both linear and a flat 1.4x, which is the one operand tree read twice while the parse, the declaration and the lowering are read once |
| conditional slice, prvalue operand | n calls `total(1 ? derived(k) : named)` | 0.02 s @200, 0.08 @800, 0.34 @3200 - linear.  The pre-audit binary refuses the program at every n, so the baseline is the nearest shape that worked: the same conditional over a derived *lvalue* is 0.17 @3200 on both binaries and one over a base prvalue 0.26, and the difference is the one object 5.16p3 says a slice of a prvalue creates |
| conditional, same class and derived lvalue | n calls `total(1 ? base(k) : named)` and n over a derived lvalue | 0.01 / 0.06 / 0.26 s and 0.01 / 0.04 / 0.17, identical on both binaries |
| conditional, class reaching a scalar | n conditionals `1 ? reaching : k` over an `operator int` | 0.01 / 0.04 / 0.15 s against 0.01 / 0.04 / 0.16 - the last bullet's ordinary-conversion arm, which is one overload resolution per operand asked twice by `reaches_other` and once more where the arm is taken |
| conditional, two scalars | n conditionals `1 ? k : k + 1` | 0.01 / 0.03 / 0.13 s against 0.01 / 0.03 / 0.12 - the two `kind` tests above the last bullet, which is what a conditional over scalars pays |
| conditional nesting depth | `(1 ? ... : 1)` written d deep | 0.00 s at d = 4, 8, 16 and 24 on both binaries |
| placeholder nesting depth | `auto` declared inside a lambda inside another `auto`'s initializer, d deep | unreachable: a lambda body may declare no local at all on either binary, so the one shape that could double per level does not exist.  d = 2 … 14 of the closest reachable shape is 0.00 s |
| aggregate dump depth | one nested aggregate under `--emit-semantics` | 2^depth by construction: one node per scalar subobject, 833 MB in a few hundred bytes.  It is PA12's dump and not the constexpr layer's |

Why the work costs what it does:

- 8.3.4p1's bound asks `dependent_reading` once per non-trivial bound, which is
  a walk of the heads a declarator stands under, and interns one reading per
  distinct spelling *and* region - so a template naming one bound n times pays
  one interning and n map probes.  `substituted_array` reads the number out of
  an already-substituted `Value`, so a specialization pays one 5.19 evaluation
  per bound and not one per reader of the type.
- 3.9.3p5 in the deduction is two calls of `object_cv` / `object_unqualified`
  per array pair, each a loop down the dimensions, and the `relaxed` flag is a
  parameter rather than a second walk.
- 8.5p16's emptiness is asked of the `InitializerClauses` the initialization was
  going to build anyway: `construct_object` builds it once, before the form is
  settled, and hands the same one to the arguments - so a list holding no
  expansion pays one node-kind test per entry and allocates nothing, and one
  holding an expansion opens its element regions once rather than twice.
- 8.5.1p7's uncovered elements are one pass bounded two ways.  The walk down a
  nested array's dimensions is a loop that multiplies the bounds, because an
  array of arrays has no padding between its elements; a *class* element is a
  frame, because each of its members stands at an offset of its own, and
  `kZeroClassDepthLimit` bounds that nesting the way `kZeroImageLimit` bounds
  the count.  Past either, the storage says it holds zero and the same bytes.
- an item's spelling is the digits `floating_image` already read out of the
  clause, so the echo is one call fewer than the normalization it replaced; a
  made zero is one string built from the type at one owner, which is where the
  body's immediate and the image's item now both ask.
- 14.5.3p4's settled run is one `is_settled_run` test per declared parameter at
  12.1p1's list as it already was at every other declarator's, so a constructor
  with no pack pays one comparison and the reading 14.7.1p1 makes a second time
  reaches the same count.
- 8.3.4p1's parentheses are two strings built in the one pass `type_spelling`
  already ran - the run of marks apart and the same run closed up - and the arm
  that needs them picks; no level is walked twice and no spelling is rebuilt.
- 8.5.4's list at a call is the same `InitializerClauses` the list needed
  anyway, built once for 13.3's length and once for the reading at the place -
  so a call with n braced arguments pays 2n walks of clauses it already had, and
  a call with none pays nothing.
- 8.5p7's elementwise zero writes one store per element under both limits, where
  the span wrote one `zeroinit`: strictly more instructions for a strictly
  bounded count (64 bytes, 8 dimensions), and the walk is one pass.
- 14.4p1's identity of a value naming is three integers rather than a spelling
  and the region it stood in, so a naming read again anywhere - a second
  declaration, an alias body, a substitution that left the prefix dependent -
  is one map probe and reaches the entry the first reading made.  The
  substitution's own prefix walk is what says whether the entry moved, so
  nothing is rebuilt where nothing did.  A naming *is* rebuilt and not re-read,
  which is why 14.5.3p5 records nothing on it: the packs it names are the
  prefix's, and `collect_packs` walks the prefix already.  The region beside the
  entry is 11.2's context and is read once per naming a substitution settles.
- 20.8.2p1's operands are the arguments the call wrote, read once through the
  list every other call's go through - so the expansion is the one expansion and
  the split is a vector copy of what is left.  Nothing about the callable is
  asked before the run is bound, which is why depth is flat: each level is one
  ordinary call whose operands are already settled.
- 5.2.2p7's class argument is one `is_class` test per argument past the declared
  parameters, and where the object already stands somewhere it is the address
  and no copy - strictly less work than the by-value parameter beside it, which
  owes 12.8p15's copy because the parameter is a second object.  Naming that
  object is one more field test at the same place, asked through
  `name_ellipsis_object` by both argument walks: `finish_call`'s, which an
  ordinary call and 13.3.1.2p1's operator call share, and `construct_object`'s,
  which a constructor needs because 12.1p1's object parameter stands first.
- Whether a callee that came to one value *names* a declaration is two pointer
  tests, asked once per call on the arm `call_value` already took - so 5.2.2p1's
  direct call of a name costs a call of a pointer nothing, and the callee line
  it writes is the one `name_function` already builds for every other call.
- `passed_array` is one `is_reference`, one `kind` and one fact-kind test per
  argument of every call, and the object type it hands back is the one
  `list_initialize_into` had already computed.  `kZeroSpanLimit` is what keeps
  the *zero* axis bounded and nothing bounds the written-clause axis, which is
  right: the reference writes one store per clause too, and does it 20x slower.
- 9.2p2's reading is made once per body wherever it stands.  `defer_body` moves
  the cursor past the `}` it recorded, so a class handing its entries up never
  makes the class around it rescan them; the region a nested reading is put back
  in is one scope, one `inherit` and one name-map copy per class level, and the
  copy is taken once per nested class body that deferred anything rather than
  once per body.  A member of the class making the reading opens no chain at all,
  which is every body a non-nested class writes.
- 7.1.6.4p6's deduction is asked only of a declarator whose type still *mentions*
  the place, which is one walk of a type at most a few nodes deep, and only for a
  declaration that wrote `auto` at all - a `mentions` call guarded by a `kNoType`
  test, so every other declarator in the program pays one comparison.  What it
  costs where it does fire is one more reading of the initializer expression,
  with 5p8's demand off so the reading asks for no definition the real one will
  ask for anyway, and the pair itself is `match_argument`, which is the walk a
  call already makes.  The place is interned once, so the bindings map is keyed
  by an integer and the substitution walks the declarator's own type.
- 5.16p3's last bullet is two `match_argument` calls, and they are reached only
  where the two operands are neither the same type nor both arithmetic and one
  of them is of class type - so every conditional over scalars pays the two
  `kind` tests the branch above it already made.  The arm that is taken measures
  its conversion once more, which is one overload resolution per class-typed
  conditional and no walk of anything the pair did not already reach.  A pair of
  *related* classes writes no conversion at all here: 5.16p3 makes that one a
  copy-initialization of the result object, which is the initialization
  `transfer_arm_to_result` already made of whichever arm the program took, so
  the slice costs one base walk and the object the clause says is created.
- 12.6.2p1's list is read twice for a constructor written in its class and once
  for one written outside it: the first reading is what says where the `{` is,
  and `skip_ctor_initializer` is what says it when no reading can.  Both are one
  pass over the same tokens, so the axis is linear and a constructor with no
  mem-initializer-list pays a boolean.

## Completed Checkpoints

| # | checkpoint | owner | measured |
| --- | --- | --- | --- |
| 1 | 14.8.2p8: substitution failure is candidate state | `sema_deduce`, `sema_template`, `sema_specialize`, `sema_analyzer`, `sema_function`, `sema_declarator`, `sema_scope`, `ast_model.h` | 246 -> 292 / 400 |
| 2 | 14.3.2p1: an address argument is which object it designates | `sema_template_head`, `sema_value_expression`, `sema_expression`, `sema_constexpr`, `ast_parser_class` | 292 -> 299 / 400 + 5 course; 30-shape sweep |
| 3 | 14.8.2p8 at 14.5.5.1p1's match, and 10p1's `class-or-decltype` | `sema_specialize`, `ast_parser_class`, `sema_derivation` | 304 -> 314 / 405; 11-shape base sweep |
| 4 | 13.3.1.4p1's converting constructor is a template too | `sema_overload` | 314 -> 319 / 405; 11-shape sweep |
| 5 | 14.6.2p2's variable template, and a prefix the arguments settled | `sema_specialize`, `sema_template`, `sema_type_id`, `sema_deduce` | 319 -> 324 / 405 + 4 course; 15 + 6 shapes |
| 6 | audit: 14.3.2p1's address argument has a fourth reader | `type_model`, `lowir_abi`, `abi_mangle`, `sema_template_head`, `sema_value_expression` | 328 -> 332 / 413; 11-shape mangling cross-product |
| 7 | the second reading a substitution makes: the tree, the region and the bound | `sema_declaration.h`, `sema_template`, `sema_template_head`, `sema_deduce`, `sema_scope`, `sema_overload`, `sema_operator` | 332 -> 341 / 416; 45 + 30 + 32 shapes |
| 8 | audit: what a body read again may name | `sema_template`, `sema_declaration.h`, `sema_scope.h`, `sema_function`, `sema_analyzer`, `sema_pattern`, `sema_specialize` | 341 -> 343 / 417; 23 + 28 + 10 shapes |
| 9 | the refusals SFINAE has nothing to fire on | `type_model`, `sema_declarator`, `sema_type_id`, `sema_template`, `sema_template_head`, `sema_virtual`, `sema_expression` | 343 -> 354 / 420; 90 + 25 + 48 + 14 shapes |
| 10 | audit: the qualifiers an array carries, and the pointee a subscript moves | `sema_expression`, `sema_init_list`, `sema_overload`, `sema_deduce`, `type_model` | 354 -> 356 / 421; 225 + 675 narrowing and 20 qualification shapes |
| 11 | the arguments an alias template's type-id threw away | `type_model`, `sema_specialize`, `sema_template`, `sema_deduce`, `sema_pack`, `sema_template_head` | 356 -> 363 / 422; 36 shapes; every course `.ref` regenerated |
| 12 | audit: the type a naming is, and the arguments worth keeping | `sema_specialize` | 363 -> 364 / 423; 40 probes |
| 13 | a dependent member written as a template-id, and the specialization no list settled | `type_model`, `sema_specialize`, `sema_deduce`, `sema_declarator`, `sema_template`, `sema_constexpr`, `sema_pack` | 364 -> 369 / 425; 16 + 12 probes |
| 14 | audit: the list a member wrote is a second fact of its name | `sema_template`, `sema_constexpr`, `sema_declarator` | 369 -> 370 / 426; 24 cross-product shapes + 34 probes |
| 15 | the types a deduction between two declarations is asked over | `sema_template_signature`, `sema_analyzer.h`, `sema_template.h`, `sema_overload`, `sema_deduce`, `sema_scope` | 370 -> 376 / 429; 42 shapes |
| 16 | audit: the number a second binding stands at, and the count a call hands the ordering | `sema_scope`, `sema_template_signature`, `sema_overload`, `sema_analyzer.h` | 376 -> 377 / 430; 135 cross-product shapes + 40 probes |
| 17 | 12.9p1's inherited constructor is a template too | `sema_class`, `sema_template`, `sema_lifetime`, `sema_deduce`, `lowir_lower` | 377 -> 384 / 434; 72 + 66 shapes run to a value |
| 18 | audit: the characteristics an inherited constructor is declared with | `sema_class`, `sema_template`, `sema_constexpr_object`, `sema_declaration.h` | 384 -> 386 / 436; 47 probes |
| 19 | 14.7.1p1's laziness, and the object 5.1.1p3 gives a member template's declarator | `sema_pattern`, `sema_template`, `sema_declaration.h`, `sema_overload`, `sema_declarator`, `sema_deduce`, `sema_function` | 386 -> 394 / 438; 68 probes, every accepted one linked |
| 20 | audit: the demand 5p8's operand does not make, and the two spellings of one value | `sema_constexpr`, `lowir_image`, `lowir_lower.h` | 394 -> 396 / 440; 108 probes |
| 21 | 14.8.2.6: the template a `template<>` head is a specialization of | `sema_specialize`, `sema_explicit`, `lowir_abi`, `sema_pack`, `sema_template` | 396 -> 401 / 440; 65 probes across five sweeps |
| 22 | audit: the definition a member *is*, and the run a settled clause leaves | `lowir_lower`, `lowir_abi`, `sema_declarator`, `sema_pack`, `sema_explicit`, `sema_analyzer.h`, `sema_specialize.h` | 401 -> 404 / 442; 113 probes |
| 23 | 14.2p3's ending delimiter, and the readings one written argument gets | `sema_name`, `sema_value_expression`, `sema_specialize`, `ast_parser_declarator`, `sema_type_id` | 404 -> 415 / 446; 61 probes |
| 24 | audit: the ending delimiter read backwards | `sema_pack` | 415 -> 416 / 447; 167 probes, 123 through the real comparator |
| 25 | the constructs 5.19 and a deduction had no fold for | `sema_constexpr`, `sema_constexpr_statement`, `sema_constexpr_object`, `sema_deduce`, `sema_declaration.h`, `sema_analyzer.h`, `sema_template`, `lowir_lower_object`, `lowir_lower.h` | 416 -> 422 / 447 (handout 369 -> 375 / 400); 31 + 36 shapes against `g++`, the reference and this build; 3918-input crash sweep clean; valgrind clean |
| 26 | audit: the object a list at a reference place initializes | `sema_init_list`, `lowir_lower_expression`, `lowir_lower_object`, `lowir_lower.h`, `sema_constexpr`, `sema_declaration.h` | 422 / 447 -> 424 / 448 (handout 375 -> 376 / 400); 140 probes, 101 through the real comparator and 41 run to `g++`'s value; 13229-input crash sweep clean; valgrind clean over 123 |
| 27 | the object file a settled program owes: its entry points, its storage and its image | `sema_lifetime`, `sema_elision`, `lowir_lower`, `lowir_lower.h`, `lowir_image`, `sema_template_head` | 424 / 448 -> 434 / 453 (handout 376 -> 381 / 400); 101 shapes through the real comparator, 69 diverging before and 9 after, all 101 run to `g++`'s value; 4091-input crash sweep clean; valgrind clean over 106 |
| 28 | audit: the value an item carries and the one an initialization makes | `lowir_image`, `lowir_lower.h`, `lowir_lower_body`, `lowir_local_static`, `sema_class`, `sema_template_head` | 434 / 453 -> 438 / 456 (handout 381 -> 382 / 400); 264 shapes through the real comparator, 118 diverging before and 43 after, 122 run to `g++`'s value; 4099-input crash sweep clean; valgrind clean over 131 |
| 29 | the refusals that stand between a call and the declaration answering it | `ast_parser_class`, `sema_enum`, `sema_deduce`, `sema_constant`, `sema_scope.h`, `sema_overload`, `sema_operator`, `sema_template_head`, `sema_analyzer`, `lowir_lower_body` | 438 / 456 -> 449 / 462 (handout 382 -> 387 / 400); 57 shapes across five sweeps against the reference and `g++`, 10 more byte-compared through the real LowIR comparator; base-walk 0.08 s @128 against the reference's 2.78; 4099-input crash sweep clean; valgrind clean over 47 |
| 30 | audit: the sibling readers still asking the question five clauses moved | `sema_declarator`, `sema_template_head`, `sema_using`, `sema_overload`, `sema_deduce.{h,cpp}` | 449 / 462 -> 452 / 465 (handout 387 / 400); 121 probes through the real comparator, 44 diverging before and 13 after, 108 run to `g++`'s value; every handout and course `.ref` regenerated with not one tracked file changed; 4108-input crash sweep clean; valgrind clean over 140 |
| 31 | 14.4p1: the value a dependent qualified-id names is the member and not the spelling | `type_model.{h,cpp}`, `sema_declaration.h`, `sema_value_expression`, `sema_deduce.{h,cpp}`, `lowir_abi`, `abi_mangle.{h,cpp}` | 452 / 465 -> 456 / 467 (handout 387 -> 389 / 400); 63 probes against the reference, `g++` and the pre-checkpoint binary, 52 run to `g++`'s value and 61 byte-identical through the real comparator against 60 before; 6 mangling shapes agreeing with `g++` where 0 did; every handout and course `.ref` regenerated with not one tracked file changed; 4110-input crash sweep clean; valgrind clean over 142 |
| 32 | audit: what an identity does not carry, and what the reading it replaced asked | `sema_value_expression`, `sema_deduce.{h,cpp}`, `sema_declaration.h`, `type_model.{h,cpp}`, `sema_pack`, `abimangle` | 456 / 467 -> 457 / 468 (handout 389 / 400); 69 probes through the real comparator, 57 byte-identical to the reference and 55 run to `g++`'s value; 8 of 8 mangling shapes identical to `g++` where the reference is at 3; every handout and course `.ref` regenerated with not one tracked file changed; 4110-input crash sweep clean; valgrind clean over 203 |
| 33 | 20.8.2p1's `INVOKE`, and the object 5.2.2p7 has no parameter for | `sema_builtin.{h,cpp}` (new), `sema_overload`, `sema_expression`, `sema_analyzer.{h,cpp}`, `sema_constexpr`, `sema_definition_names`, `lowir_lower_object` | 457 / 468 -> 465 / 473 (handout 389 -> 392 / 400); 61 probes through the real comparator, 58 of 61 agreeing with the reference on acceptance and 44 of 46 byte-identical, 7 run to `g++`'s value and a PA34 `__builtin_invoke` fixture with them; 5 course fixtures added; every handout and course `.ref` regenerated with not one tracked file changed; 4116-input crash sweep clean; valgrind clean over 207 |
| 34 | audit: the second door each of two settled facts is asked at | `sema_lifetime`, `sema_analyzer.h`, `sema_overload` | 465 / 473 -> 467 / 475 (handout 392 / 400); 69 probes through the real comparator, 58 agreeing with the reference and every one of the 11 that do not recorded, 15 run through `lowir2cy86` + `cy86` with 14 reaching `g++`'s value; 2 course fixtures added, each failing on the pre-audit binary; every handout and course `.ref` regenerated with not one tracked file changed; 4155-input crash sweep clean; valgrind clean over 162 |
| 35 | 14.1p4 at a run, and 9.2p2's complete-class context in the parser | `sema_template_head.{h,cpp}`, `sema_deduce`, `sema_template`, `sema_pack`, `sema_declarator`, `sema_function`, `sema_constexpr`, `ast_parser.{h,cpp}`, `ast_parser_class`, `ast_parser_declarator` | 467 / 475 -> 471 / 477 (handout 392 -> 394 / 400); 31 pack shapes agreeing with `g++` at 31 of 31 with 21 run to its value, 25 complete-class shapes at 24 of 25 with 23 run to its value, and 5 more placing the two contexts left; corpus 19.12 s against 19.30 on the pre-checkpoint binary, member bodies linear at 3200 and flat at depth 20; 2 course fixtures added, each refused by the pre-checkpoint binary; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 4157 inputs; valgrind clean over 180 |
| 36 | audit: the rest of each sentence - 9.2p2 in a nested class, 8.4p1's other half, and 14.1p4's sixth door | `parse_deferred.h` (new), `ast_parser.{h,cpp}`, `ast_parser_class`, `ast_names.h`, `sema_template_head.{h,cpp}` | 471 / 477 -> 473 / 479 (handout 394 / 400); 50 generated probes through the real comparator, 49 byte-identical to the reference and 49 run to `g++`'s value, 12 refused by the pre-audit binary; 122 probes in all with 115 agreeing with the reference on acceptance; 2 course fixtures added, each refused by the pre-audit binary; file audit back from 6 warnings to 5, `ast_parser.h` at 179 of 180; corpus 19.16 / 18.85 s against 19.20 / 19.34; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 4161 inputs; valgrind clean over 179 |
| 37 | 7.1.6.4's placeholder deduced from its initializer, and 5.16p3's conversion of one operand to the other's type | `type_model.{h,cpp}`, `sema_declarator`, `sema_analyzer.{h,cpp}`, `sema_deduce.{h,cpp}`, `sema_conditional.{h,cpp}` (new), `sema_expression`, `sema_lifetime` | 473 / 479 -> 476 / 481 (handout 394 -> 395 / 400); 90 generated probes agreeing with `g++` on acceptance at 89 of 90, 78 byte-compared through the real comparator with 70 identical to the reference and every one of the 8 that differ recorded, 76 of 77 run through `lowir2cy86` + `cy86` to `g++`'s value, 66 of 78 refused by the pre-checkpoint binary; 2 course fixtures added, each refused by the pre-checkpoint binary; corpus 9.52 / 9.68 s against 10.96 / 9.58, `auto` linear at 4000 declarations and 1.5x one initializer; `sema_analyzer.h` 2398 -> 2394 of 2400 with 5.16 moved to its own owner; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 3463 inputs; valgrind clean over 152 |
| 38 | audit: the object a conditional slices a derived operand into | `sema_conditional.{h,cpp}` | 476 / 481 -> 477 / 482 (handout 395 / 400); 220 placeholder probes reading their deduced type back through `decltype` agreeing with `g++` at 220 of 220 and with the reference at 219; 128 conditional probes crossing four value categories with eight class relations running to `g++`'s value at 122 of 128 against 113 before, each of the 6 left a shape the reference refuses whole; 92 probes through the real comparator with 87 byte-identical to the reference; 1 course fixture added, refused by the pre-audit binary; corpus 14.26 / 14.13 s against 14.06 / 14.21 over 3220 inputs; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 4101 inputs; valgrind clean over 203 |
| 39 | 10.1p4's virtual base is a fact of the derivation, and the four refusals precise where one broad one stood | `sema_scope.{h,cpp}`, `sema_derivation.{h,cpp}`, `sema_layout.{h,cpp}` (`ClassLayout` new), `sema_cast`, `sema_class`, `sema_lifetime`, `sema_allocation`, `lowir_lower.{h,cpp}`, `sema_analyzer.{h,cpp}` | 477 / 482 -> 480 / 484 (handout 395 -> 396 / 400); 32 generated shapes judged one at a time through the real `compare_results.pl` with 18 byte-identical to the reference, 5 where the reference is wrong and `g++` agrees with this build, 3 refused by design and 4 answered by PA28's hidden vbase-pointer ABI; 16 of 17 accepted shapes run through `lowir2cy86` + `cy86` to `g++`'s value; 9 pointer-to-member probes placing 4.11p2 and 5.2.9p12 against `g++`; 2 course fixtures added, each refused by the pre-checkpoint binary and byte-identical to the reference; corpus 10.32 / 10.25 s against 10.28 / 10.16, layout flat at 1500 levels and virtual bases linear at 2000; `sema_analyzer.h` 2394 -> 2369 of 2400 with the layout moved to its own owner; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 5334 inputs; valgrind clean over 164 |
| 40 | audit: the entry point a class with a virtual base owes, and 7.1.5p4's first bullet | `lowir_lower.cpp`, `sema_constexpr_declaration.cpp`, `lowir_abi.h`, `lowir_lower_unwind.cpp`, `sema_allocation.cpp` | 480 / 484 held (handout 396 / 400); 123 probes against the reference, `g++` and the pre-audit binary - 40 through the real comparator with 23 byte-identical to the reference and every one of the 17 that are not recorded, 24 deterministic shapes run through `lowir2cy86` + `cy86` with 23 of 24 reaching `g++`'s value, and 5 two-unit programs of which 2 did not link before and all 5 now run to `g++`'s value; virtual-base multiplicity identical to the plain-base baseline at n = 2000 and n^1.16 over a 32x range for the empty shape; corpus 13.94 / 14.07 s against 13.97 / 14.06; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 4201 inputs; valgrind clean over 193 |
| 41 | 14.6p8's reading owes a settled class its definition, and the two doors a decltype-specifier's region was missing | `sema_template.cpp`, `sema_deduce.cpp`, `sema_declarator.cpp`, `sema_overload.cpp` | 480 / 484 -> 483 / 486 (handout 396 -> 397 / 400); 246 generated shapes - six prefix forms crossed with seven uses crossed with five contexts - run through this build, the pre-checkpoint binary, the reference and `g++`, with 232 of 246 agreeing with the reference on acceptance and all 232 both accept byte-identical to it, 235 of 246 agreeing with `g++`, and 16 refused by the pre-checkpoint binary and accepted by all three other oracles; 11 `noexcept` shapes placing the reference's 5.3.7p3 walk and 24 decltype-prefix shapes; 2 course fixtures added, each byte-identical to the reference and each failing on the pre-checkpoint binary; dependent-member multiplicity linear at 6400 and below the pre-checkpoint binary, detector-in-a-template within 8% of the namespace-scope twin both binaries run alike, flat in nesting to depth 320; corpus 12.95 / 13.19 / 13.10 s against 13.40 / 13.23 / 13.38; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 3835 inputs; valgrind clean over 160 |
| 42 | audit: the third ask a name written after a decltype-specifier makes, and 13.4p1 at a reader with no set | `sema_declarator.cpp`, `sema_overload.cpp`, `sema_analyzer.h` | 483 / 486 -> **485 / 488** (handout 397 / 400); 120 generated shapes - four prefix forms crossed with ten uses crossed with three contexts - through this build, the pre-audit binary, the reference and `g++`, with 120 of 120 agreeing with `g++` on acceptance and 120 of 120 running through `lowir2cy86` + `cy86` to its value, 84 agreeing with the reference and 36 refused by the pre-audit binary; 121 probes byte-identical to the reference through the real comparator; the new door linear at 3200 and within 30% of the name-spelled twin both binaries run alike, flat at depth 128; corpus 12.82 / 12.67 s against 12.79 / 12.78; 2 course fixtures added, one byte-identical to the reference and refused by the pre-audit binary and one refused by all three oracles; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 4358 inputs; valgrind clean over 191 |
| 43 | 3.6.2p2: constant initialization is a fact of the initialization and not of the `const` a declaration wrote | `sema_scope.h`, `sema_constexpr.{h,cpp}`, `sema_analyzer.cpp`, `lowir_image.cpp`, `lowir_lower.h` | 485 / 488 -> **487 / 490** (handout 397 / 400); 157 generated shapes - 28 class shapes crossed with four initializer forms, three declaration places, the array and the out-of-class static data member - through this build, the pre-checkpoint binary, the reference and `g++`, with 143 of 157 byte-identical to the reference through the real comparator against 33 before, 157 of 157 agreeing with the reference on exit status, and 144 of 144 buildable ones running through `lowir2cy86` + `cy86` to `g++`'s value at 0 disagreements; of the 14 left, 7 stood before the checkpoint and 7 are shapes this build now lays out and the reference leaves to the program, each running to `g++`'s value; object multiplicity linear at 6400 and array multiplicity linear to the `kMaxConstexprSteps` cap with an instant fallback above it, subobject nesting linear at depth 320; corpus 2.39 / 2.41 / 2.40 s against 2.45 / 2.46 / 2.48; 2 course fixtures added, each failing on the pre-checkpoint binary; every handout and course `.ref` regenerated with not one tracked file changed; 0 exits above 1 over 5240 inputs; valgrind clean over 71 |
