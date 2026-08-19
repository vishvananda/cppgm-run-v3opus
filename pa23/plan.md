# PA23 Plan — deduction, substitution and SFINAE

## Stage Design

PA23 keeps the PA22 compiler and finishes the single-feature half of the
template layer: full function-template deduction, partial ordering, and
14.8.2p8's substitution failure as *candidate state* rather than as a
diagnostic. No new output format; the LowIR contract is PA13's.

Owners, in the order a use walks them:

- `ast_parser_class.cpp` / `ast_parser_declarator.cpp` — 14.1p3's abstract
  declarator, 10p1's `class-or-decltype` whose operand tree the arena keeps
  beside its spelling, and 8.3.6p1's default argument, which is the one form a
  parameter-declaration writes after its declarator and so is where 8.2p1's
  ambiguity is settled for `holder made(seed{});`.
- `sema_name.{h,cpp}` — the terminals PA10 flattened a name into, read back:
  which `<` opens 14.2's list and which `>` ends one, over a spelling whose
  separators say where each token began.
- `sema_template_head.*` — 14.1p2's places, 14.1p9's defaults, the bound each
  was written under, 14.3.3p1 of a whole settled argument list, and
  `type_spelling`: the one name every naming of one argument list has to reach,
  written from the type with 7.1.6.1p1's qualifier trailing at every level.
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
  reading, `written_bound` per construct being recorded. Every definition
  14.7.1p1 reads again carries the bound it was written under - the pattern,
  each partial specialization's body, each out-of-class member definition, each
  body `template<>` wrote out, and each body a reading put aside.
- `sema_function.cpp` / `sema_template_signature.*` — 14.5.6.1p5 and
  14.5.6.2's ordering of two function templates.
- `sema_overload.cpp` — 13.3, which drops a candidate a deduction refused,
  gathers 13.3.1.4p1's converting constructors, and asks 14.6.4.2p1 of 3.4.1's
  half of a set and not of 3.4.2's. `sema_constexpr.cpp` draws the same line
  where a fold gathers a set of its own.
- `sema_layout.cpp` / `type_model.cpp` — 5.2.2p4's boundary, which reads
  12.8p12's copy, 12.4p8's destruction, 3.9.1p8's floating storage and 10.4p2's
  abstract class; and 8.3.2p5 / 8.3.4p1's *door*, which is the entry a
  declarator derives a type through as against the entry that interns one.
- `sema_expression.cpp` / `sema_lifetime.cpp` / `sema_init_list.cpp` — the
  refusals a substitution failure is made of: 5.7p1 and 5.2.6p1's completely
  defined pointee, and 8.5.4p7's narrowing of a clause through 13.3.3.1p4; and
  8.5.3p5's object, which is what a braced-init-list at a reference place
  initializes and what the fact it leaves has to carry, because every reader
  below asks the list what type it came to and a reference is not one.
- `sema_class.cpp` / `sema_lifetime.cpp` — 12.9's inheriting constructor: p1's
  candidate set and the head each of them was written under, and p8's forwarding
  of this declaration's own places to the base's.
- `sema_explicit.cpp` — 14.7.3's two clauses over a function or an object:
  p11's count of the templates one head could be a specialization of, which is
  refused at zero and at a written type fitting none, and p5's refusal over a
  member of a class the program wrote out.
- `sema_declarator.cpp` / `sema_pack.cpp` — 8.3.5p10's places a parameter clause
  declares, through the one door `bind_place`, with 14.5.3p4's run carried onto
  the first of them and each of the rest linked back: once the arguments settle
  the run, the declaration is the only carrier the fact has.
- `lowir_abi.cpp` — 14.2's encoding of an argument list, and 14.1p4's own
  answer to which places bind an address.
- `lowir_lower.cpp` — which definitions this object file holds, which is two
  questions and not one: whose definition it is (7.1.2p4, widened by 14.7.3p5)
  and whether this unit writes one nobody used (3.2p4, asked of whether a
  template-id names the class) - and, of a definition it does hold, which of
  12.1's two entry points it owes, which 9.3p2 answers of where the definition
  was written and 14.5.2p1 of whether the member is a template.
- `lowir_image.cpp` — 3.6.2p2's two spellings of one value: the operand naming a
  scalar object's whole storage, at the LowIR type, and an item standing for one
  clause of the image, at that clause's own signedness; and 8.5.1p7's elements
  no clause reached, which are the zero of an *element* and not a run of bytes.

## Current Failure Map

453 tests (400 handout + 53 course), **434 passing** (handout 381 / 400).  The 19
left, all handout, by the compiler behavior each wants:

| group | n | shape |
| --- | --- | --- |
| a call or name no declaration answers | 4 | 300-explicit-template-call-transitive-base-deduction, 400-unnamed-nontype-pack-static-enable-if-default, 300-nondeduced-partial-pattern-recursive-completion, 300-equivalent-alias-return-template-redeclaration |
| LowIR text mismatch | 3 | 500-bool-alias (the reference folds a call no clause makes constant), 500-tcc (`_TCC`'s 11), 300-conversion (an item spelled with the written clause's digits) |
| `__builtin_invoke`, which no layer implements | 2 | `invoke_result_impl<void,Args...>::type` in both decltype-invoke tests |
| the reference's own answer pinned by a `.ref` | 1 | 5.3.7p3 at a pseudo-destructor call |
| a feature below the template layer | 4 | a virtual base's layout, an opaque unscoped enum's underlying type, `auto` deriving 8.3.5p2's unwritten type, 12.8p15's copy of a class value read where no object holds it |
| the rest | 5 | one clause each: a member body's forward template-id (the parser), `args` used as a value where its head declared a type, a base conversion's access, `sizeof`/`decltype` operands of unrelated types, a member alias in an out-of-class SFINAE body |

### Known gaps diagnosed but not landed

Only the ones still live.  Every row below has been re-probed on this turn's
binary.

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
- **`__builtin_invoke` is not implemented**, which is the whole of both
  `invoke_result_impl` tests and nothing to do with 14.8.2.
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
- **A written initializer's uncovered elements of *class* type are still one
  `zero n` run**, where the reference writes each element's own members with the
  padding between them.  `S a[2] = {}` over `struct S { int x; char y; };` is
  the one shape left on that axis; `global_subobjects` already writes a class
  *object*'s members that way, so what is missing is the walk applied per
  element.
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
- **The reference builds a zero-length array.**  `template<unsigned N> struct
  box { int a[N-1]; }; box<1> v;` and four shapes like it are accepted there and
  refused by `g++` and by this build, which 8.3.4p1 is what says.
- **The reference refuses a list at a declared reference or a scalar one.**
  `int const (&r)[3] = {1,2,3};` and `f({0})` at an `int const &` are refused
  there and translated by `g++` and by this build.
- **An array at a reference place in a *fold* is refused.**
  `template<class T> constexpr int inner(T const & t) { return sizeof(t); }`
  over a `static int const v[2]` is `no declaration of inner accepts the
  arguments of a call` here and translated by both oracles, with and without a
  template.  It is the fold's half of the sentence checkpoint 26 fixed in the
  expression layer.
- **14.6p2 at a reading a dependent context defers.**  Widening the clause to
  every dependent prefix costs a pa20 course fixture; the reference implements
  it at no shape at all, so `g++` is the only oracle.
- **A template-id component written after a decltype prefix.**
  `decltype(A::make())::template box<int>` is refused here and a type in both
  oracles, with no template in the program; the same name through a typedef
  comes out right, so the gap is the decltype-qualified name reader.
- **A pack pattern in a partial specialization's own argument list.**
  `D<void_t<typename T::m...>, T...>` leaves the primary unsupported.
- **`TypeTable::substitute` rebuilds a discarded-argument naming structurally**,
  where `SemaAnalyzer::substituted` reads the type-id again.  Both shapes
  reachable through it come out right because their elements are settled by
  then.
- **14.5.7p1's equivalence where the argument a naming threw away is a
  *reading*.**  `300-equivalent-alias-return-template-redeclaration.t` is two
  spellings of one dependent value read as two readings here.
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
- **An *item* and an instruction operand are spelled with the digits the written
  clause had in the reference**, at every width and whichever type it is:
  `f32 0` there and `f32 0f` here, `u32 -1` there for `(unsigned int)-1`.  A
  *scalar* global agrees three ways, which is what pa16's `.ref` pins, so the
  item path would have to keep the written clause apart from the one 8.5p7
  makes.  It is the only diff left in
  `spec/300-conversion-function-template-object-result-copy-init.t`.
- **A function template's trailing return type is mangled `T_`.**  `object=` is
  stripped before every comparison, so no fixture can pin it.
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
- **The parser cannot see a member template declared later in the same class
  body.**  9.2p2 makes a member function body a complete-class context, so
  `pop<long>(v)` written above `template<class U> bool pop(U &)` is a
  template-id in both oracles and two comparisons here - the whole of
  `100-explicit-member-template-id-distinct-from-nontemplate.t` and the one
  remaining `is not a translation unit`.  The fix is the parser deferring member
  function bodies to the closing brace as the semantic layer already does; a
  two-pass class body would be 2^depth over nested classes.
- **The reference refuses `this->v` in a member template's trailing return
  type**, at six of twelve shapes swept, so no course fixture can hold them.
- **`this` written in a *static* or `friend` member function template's
  declarator is 14.6p8's no-diagnostic-required here.**
- **5.3.7p3 at a pseudo-destructor call, where the reference disagrees with the
  standard.**  `spec/300-scalar-pseudo-destructor-noexcept.t` pins the
  reference's answer, so it cannot pass without adopting a rule both other
  oracles refuse.
- **The reference defers an unused *free* explicit function specialization and
  emits an unused ordinary function's definition**, disagreeing with itself
  across the pair.
- **A temporary of a class with a non-trivial destructor is zero-initialized and
  constructed inside an eh region by the reference**, which is a *non-template*
  difference.
- **`template<> int f<int>(int) throw() {}` beside `template<class T> int f(T);`**
  is 15.4p3 refused by `g++` and translated by the reference and by this build.
- **14.7.3p11 where the written type fits two templates 14.5.6.2 leaves
  unordered**, where the refusal would fail a fixture rather than pass one.
- **`template<> int S::h()` over a member of an ordinary class** is refused by
  `g++` and translated by the reference and by this build.
- **A static data member of a class `template<>` wrote out is `binding=strong`
  here and in `g++` and `binding=weak` in the reference**; `binding=` is
  stripped before comparison.
- **`template<> template<> int S<int>::g(char)` written with no argument list on
  either head** runs to the specialization here and in `g++` and to the pattern
  in the reference.
- **`decltype(sizeof...(N))` over a *value* pack is `int` in the reference** and
  `std::size_t` here and in `g++`, which 5.3.3p6 is what says.
- **`decltype((h.*f)(a))` written as a trailing return type is refused here** and
  translated by both oracles, with no pack in the program.
- **The reference emits a second constructor entry point for a base written
  through an alias template**, disagreeing with itself one line away.
- **The reference writes two definitions of one specialization named two ways** -
  an object file no link can hold.
- **The reference calls a function parameter pack of class type variadic**, and
  `arity=` is compared, so no course fixture can hold a pack of class type.
- **The reference refuses an alias template whose type-id is a pointer to or an
  array of a specialization.**
- **`sizeof` over a function name is accepted here**, where 5.3.3p1 is
  ill-formed and both oracles refuse; there is no template in the program.
- **A constructor template whose run is empty is chosen once and refused the
  second time.**  `parenthesized first; of_a_base second;` over one class whose
  only constructor is `template<class... A> C(A&&...)` is `no declaration of C
  accepts the arguments of a call` at the *second* use, and each use alone is
  accepted; the reference takes both.  It is a memo written on the empty
  argument list and no part of 8.5p16 - the checkpoint-27 course fixture writes
  a class per shape to stay clear of it.
- **`type_spelling` writes no declarator parentheses.**  `const int (&)[2]` is
  `int const[2] &` here and `int const (&)[2]` in the reference, so a name over
  a reference or pointer to an array or a function differs; the reference also
  writes `(*const)` closed up inside the parentheses where it writes `* const`
  apart at the top level.  Every other level agrees exactly, including
  `int *[2]`.
- **The reference writes an unused out-of-class destructor's definition.**
  `template<int N> B<N>::~B() {}` over a base whose destruction 12.4p8 leaves
  nothing to run is a definition (D1, with D2 aliased to it) there and no symbol
  at all here - the same 9.3p2 question checkpoint 27 answered for a
  constructor, asked where no entry point runs.
- **A plain class nested in a class template gets no `out_of_class_definition`.**
  `template<int N> O<N>::B::B() : d{0} {}` owes both entry points in the
  reference and writes only the base entry here, where the same definition
  directly in a class template owes both since checkpoint 27.
- **A prvalue of an empty run at an aggregate's clause is more correct here.**
  `Y y = { X(a...), X() }` over an empty run zero-initializes both elements here
  and in `g++`; the reference calls X's default constructor for the first and
  zeroes the second, one clause apart.  5.2.3p2 is what says `X()` is
  value-initialized.
- **`sema_analyzer.h` stands at 2398 of its 2400 lines.**  The next declaration
  added there needs room freed structurally first.

## Active Checkpoint

**The object file a settled program owes: its entry points, its storage and its
image.**  Complete; ledger row 27 is its record.  Five handout tests, at four
owners, and the whole of what was the largest coherent failure group.

- *Owner and data flow.*
  - `sema_lifetime.cpp` / `sema_elision.cpp` — 8.5p16 asked of what a list
    *came to* rather than of the entries the program wrote.  `construct_object`
    reads the `InitializerClauses` once, before it decides anything, and a list
    whose every entry is an expansion of an empty run is 8.5p16's `()`: it
    value-initializes and is no 5.2.3p2 elided prvalue.  The same reading is
    then handed to the arguments, so nothing is expanded twice.
  - `lowir_lower.cpp` — two questions of the object file.  `low_type` gives an
    object 1.8p5's byte where 8.5.1p4 left an array of no elements, so a slot
    is `obj<1x4>` and not `obj<0x4>`.  `writes_base_entry` reads 9.3p2 of an
    *instantiated* member: a definition written outside its class body is one
    this unit writes whole, so it owes both of the ABI's entry points wherever
    a base subobject asked for either - and 14.5.2p1's member template is left
    to the arm that follows the use, which `primary` is what says.
  - `lowir_image.cpp` — `add_zero_elements`, 8.5.1p7's elements no clause
    reached.  Value-initializing one is the zero of an *element*, so the image
    holds one item at the element's own type; only padding and a null pointer
    are runs, and an object 8.5p6 default-initialized keeps its one run.
  - `sema_template_head.cpp` — 7.1.6.1p1's one spelling of a cv-qualifier,
    written after the type at every level of a declarator, with ` *` and ` &`
    apart from what they qualify.  It is what internal LowIR names flatten, so
    `cache<const int>` and `cache<int *const>` stay two names.
- *Expected complexity.*  One `WrittenList` per initialization where there had
  been one, one comparison per `low_type`, two field reads per entry-point
  question, and one pass over an image's uncovered elements bounded by
  `kZeroImageLimit` - with the walk down a nested array's dimensions a loop, so
  no path recurses per bracket or per element.
- *Validation.*  101 generated shapes over the five behaviors, judged through
  the real `compare_results.pl` against the reference: **69 diverged on the
  pre-checkpoint binary and 9 do now**, every one of the 9 a divergence the
  baseline had too, and all 101 run through `lowir2cy86` + `cy86` to `g++`'s
  value.  pa23 **424 / 448 -> 434 / 453** (handout 376 -> 381 / 400) with five
  course fixtures added; `through-pa22` 2948 / 2948; file audit clean; 0 exits
  above 1 over 4091 inputs of pa10 through pa29 and cppgm.tests; valgrind clean
  over 106.

## Next Substantial Checkpoint

**The four calls or names no declaration answers**, which is now the largest
coherent group and 14.8.2 proper: `sema_deduce.cpp` for
300-explicit-template-call-transitive-base-deduction and
300-nondeduced-partial-pattern-recursive-completion, `sema_specialize.cpp` for
14.5.7p1's equivalence in 300-equivalent-alias-return-template-redeclaration,
and `sema_template_head.cpp` for the unnamed non-type pack place.  Beside it,
cheaply: the constructor template whose empty run is chosen once and refused the
second time, which is one memo and three of the SFINAE tests' shape.

## Performance Model

Measured with `/usr/bin/time` on the binary itself, warm cache.  A loop that
spawns `timeout` per run reads the same corpus as 45.9 s against 2.6 s, which is
the wrapper's process floor and not the compiler's; a corpus pass run while a
build saturates the machine reads 5.8 s against 1.9 s, which is that build's.
Absolute times are the load of the turn that took them - the corpus reads 2.16 s
on one turn and 2.00 s on the next from one binary - so only the two figures of
a pair say anything.

Run evidence has a ceiling that is the scaffold's and not the compiler's.  A
function of five or more parameters (`this` among them), and a function taking a
*class by value* at any arity, come out of `lowir2cy86` + `cy86` as a program
that returns the wrong value or crashes - identically from the *reference*
binary's LowIR, with both builds' LowIR byte-identical through the real
comparator.  A probe that has to be run to a value writes four arguments or
fewer and passes scalars, pointers or references.

Every sweep through checkpoint 27 came out linear in multiplicity and flat in
nesting, at or below the pre-checkpoint binary measured in a worktree built the
same way; the individual rows are superseded and the shapes that mattered are
named in the ledger.  The whole corpus of pa10 through pa29 and cppgm.tests -
4091 inputs, one process apiece - reads **17.21 / 18.02 s** on this checkpoint's
binary against **17.50 / 17.70 s** on the one before it, which is the spawn
floor of 4091 processes and no difference between the two.  What is live:

| sweep | shape | result |
| --- | --- | --- |
| empty-run initializations | n classes whose only constructor expands `v(a...)` over an empty run | 0.03 s @200, 0.16 @800, 0.72 @3200 - and 0.03 / 0.15 / 0.69 on the pre-checkpoint binary, so reading the clauses once for 8.5p16 *and* for the arguments costs what reading them once for the arguments alone did |
| non-empty expansion multiplicity | n constructors whose `v{a...}` expands over a run of four | 0.06 s @200, 0.27 @800, 1.19 @3200 - and 0.06 / 0.27 / 1.16 before, so the hoisted reading is the same one reading |
| out-of-class entry points | n class templates with an out-of-class constructor, each a base of one more | 0.07 s @200, 0.34 @800, 1.74 @3200 - and 0.07 / 0.36 / 1.75 before; two field reads per question |
| cv-qualified specialization names | n specializations of one template over `int const * const * volatile[k]` | 0.02 s @200, 0.07 @800, 0.38 @3200 - and 0.02 / 0.09 / 0.54 before, so the trailing spelling is a shorter walk and not a longer one |
| written global image multiplicity | n globals `int g[8] = {1}` | 0.00 s @200, 0.02 @800, 0.10 @3200 - and 0.00 / 0.02 / 0.08 before, which is the 7 items per global now written |
| written global image width | one `int a[n] = {}` | 0.00 s at every n; 1043 lines of LowIR at 1024 elements and 20 at 4096, where `kZeroImageLimit` collapses it - so the axis is bounded by construction and the reference's 2,000,010 lines at a million has no counterpart here |
| declarator depth | `int a[1][1]...[1] = {}` alone, and `C<int const *...*>` | 0.00 / 0.02 / 0.06 s @2000 / 8000 / 20000 brackets and 0.00 / 0.00 / 0.01 @200 / 800 / 3200 stars - identical to the pre-checkpoint binary, so neither `add_zero_elements` nor `type_spelling` recurses per level |
| computed-bound multiplicity | n function templates with `T (&)[N][N], T (&)[N - 1]`, one call apiece | 0.03 s @200, 0.14 @800, 0.59 @3200 - linear, and the same over three distinct bound spellings, so a distinct spelling costs one interning and no walk |
| braced-argument multiplicity | n folded calls whose argument is a braced-init-list | 0.00 s @200, 0.01 @800, 0.03 @3200 - linear |
| pack mem-initializer multiplicity | n folds of one `value(args...)` constructor | 0.00 s @200, 0.01 @800, 0.04 @3200 - linear |
| elementwise-zero multiplicity | n local `int a[2][2] = {}` written as the elements they have | 0.01 s @200, 0.06 @800, 0.24 @3200 - linear, against 0.21 on the pre-checkpoint binary, which is the four stores against one `zeroinit` |
| array-dimension depth | `int a[1][1]...[1] = {}` alone | 0.00 s @2000, 0.02 @8000, 0.06 @20000 - flat per bracket; `zeroed_elementwise` walks down with a loop and stops at `kZeroDimensionLimit`, so neither it nor `zero_elements` recurses per bracket.  The `a[0]...[0]` that reads it back segfaults at 20000, which is the subscript's own recursion recorded above |
| braced array argument multiplicity | n calls passing `{1,2,k}` at an `int const (&)[3]` | 0.01 s @200, 0.03 @800, 0.14 @3200 - linear; an argument that *names* an array at the same place is 0.07 @3200 and identical on every binary |
| braced array argument width | one list of n clauses at a reference to an `int[n]` | 0.00 s @256, 0.01 @1024, 0.03 @4096 - linear, against **0.65 s** in the reference binary at 4096 |
| declarations of one name | n redeclarations of one template, n overloads of one name, n templates of distinct names | 0.07 / 0.08 / 0.10 s @3200, each the same on the pre-checkpoint binary - linear, so the 0.80 s @3200 an earlier row carried does not reproduce |
| overload-set width | one call over n candidate templates that all tie on conversions - the pairwise ordering walk | 0.01 s @128, 0.02 @256, 0.04 @512 |
| aggregate dump depth | one nested aggregate under `--emit-semantics` | 2^depth by construction: one node per scalar subobject, 833 MB in a few hundred bytes.  It is PA12's dump and not the constexpr layer's |

Why the new work costs what it does:

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
- 8.5.1p7's uncovered elements are one pass bounded by `kZeroImageLimit`, and
  the walk down a nested array's dimensions is a loop that multiplies the
  bounds: an array of arrays has no padding between its elements, so the items
  are its scalars in storage order and no level of the declarator is a frame.
- 7.1.6.1p1's spelling moved from a prefix to a suffix, which is the same two
  appends at the same one place in the loop `type_spelling` already ran.
- 8.5.4's list at a call is the same `InitializerClauses` the list needed
  anyway, built once for 13.3's length and once for the reading at the place -
  so a call with n braced arguments pays 2n walks of clauses it already had, and
  a call with none pays nothing.
- 8.5p7's elementwise zero writes one store per element under both limits, where
  the span wrote one `zeroinit`: strictly more instructions for a strictly
  bounded count (64 bytes, 8 dimensions), and the walk is one pass.
- `passed_array` is one `is_reference`, one `kind` and one fact-kind test per
  argument of every call, and the object type it hands back is the one
  `list_initialize_into` had already computed - so what moves is the work now
  actually done, one store per clause where there had been one wrong store.
  `kZeroSpanLimit` is what keeps that bounded on the *zero* axis and nothing
  bounds it on the written-clause axis, which is right: the reference writes one
  store per clause too, and does it 20x slower.
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
