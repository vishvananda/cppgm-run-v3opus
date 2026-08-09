# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C12, reviewed at `7afd0f26`** — 14.6.2.1p9's nested class of the current
instantiation made a dependent type; 3.4.1p8's base-clause of a member class
defined outside its class read in the region its class-head-name reached;
7.1.6.3p1's elaborated-type-specifier where a template argument spells a
type-id, with 3.3.2p6 declaring the class a class-key reaches none of;
3.4.3.1p2's second arm, so a using-declaration names a base's constructors
through a typedef-name; 5.3.6's operator under the two spellings 1.4p8 reserves;
14.6p8 over 8.3.4p1's bound; and **14.7.1p1's point** - a specialization
declared where it is named and defined where a completely-defined type is
required.

**Five of the seven rules are right and swept clean.**  14.6.2.1p9 holds over a
nested class, a nested enumeration, a class nested two deep, one forward-declared
above the base-specifier that writes it and the base-clause each of them makes -
identical to the reference in every one, with the flag settled where the type is
made and the memo it invalidates its own.  3.4.1p8's base-clause finds a member
type of the enclosing class and a template-argument-list written over one, and
the region is the class-head-name's only where one was written.  7.1.6.3p1 holds
over `struct`, `class` and `enum` keys, a qualified name, a simple-template-id, a
nested class, a class 3.3.2p6 declares at namespace and at block scope, and the
class an object of that spelling hides - with a typedef-name after `struct` and a
class-key that disagrees each refused here and by **g++** where the reference
alone accepts them.  3.4.3.1p2 names a base's constructors through a typedef-name
and through a dependent base, and refuses a class that is no base in all three
compilers.  `__alignof` and `__alignof__` are the operator where a `(` follows and
the identifier a member declares where none does.

**The sixth is broader than the clause it answers, and is left standing.**
14.6p8 over 8.3.4p1 stands one element in wherever a bound comes out zero while
`checking_ > 0`, which is right for the quotient `size_of`'s stand-in value
makes and also swallows a bound a reading could have answered - `template<class
T> struct S { int a[0]; };`, which both oracles accept and this compiler now
accepts too.  Telling the two apart means marking the evaluation that read a
stand-in, and there is nothing to tell apart yet: an instantiation recomputes
the bound with `checking_ == 0` and refuses a zero it really arrives at, so the
relaxation reaches no specialization and no fixture.  What it does leave is a
compiler that accepts `int a[0]` inside a template and refuses it outside one -
a difference that predates this tier and is recorded under Open Gaps.

**One blocker, and it is the checkpoint's own rule read at the wrong question.**
14.7.1p1 instantiates a class template specialization where it is used in a
context requiring a completely-defined type *and nowhere else*, and C12 landed
the opposite default: **naming** a specialization required its definition unless
the walk happened to stand inside a `simple-declaration`'s decl-specifier-seq.
The grammar writes that name in a dozen other places, so **20 of 30 swept naming
spellings instantiated a class the standard leaves declared** - and the same
boundary left the demand missing where an expression is the first thing to ask,
so a specialization a typedef had named was still incomplete when 13.3.1.2p3
looked an operator up in it.  The two errors mask each other in the fixtures: the
over-instantiation is what was completing the classes the missing demands needed.

### Findings

**1. A second naming instantiated what the first left declared.**
`asked_specialization` marked a specialization declared-only on the *first*
naming and fell through to `require_specialization` on every one after it, so
`typedef holder<box> a; typedef holder<box> b;` read the pattern at the second
typedef - where `box` is still incomplete - and `holder<box>`'s
`static const int factor = T::factor;` was
`factor is written after a name that is not a namespace, class or enumeration`
on a program both oracles compile.  Three typedefs, a typedef beside an alias,
and two `extern` declarations of one object are the same shape.

**2. The suppression reached one of the grammar's spellings and not the other
twelve.**  A `simple-declaration`'s decl-specifier-seq was read under a depth and
nothing else was, so **7.1.3p2's alias-declaration** (`using h = holder<box>;`),
**8.3.5p6's parameter of a function nobody is defining** (`void f(holder<box>);`,
and a member function's the same), a **trailing-return-type**, a **default
argument**, a **cast's and a `static_cast`'s type-id**, a **`sizeof` over a
pointer to one**, a **new-expression over a pointer to one**, a
**condition-declaration**, a **friend declaration** and a **member
alias-declaration** each named a specialization and instantiated it.  Every one
of the twelve is accepted by `reference-binaries/cppgm++` and by **g++** and
refused here.  Telling them apart by where the walk stands is a list the grammar
keeps adding to; the clause says the demand belongs to the context that requires
a complete type, so the naming is now never one and
`asked_specialization` only marks.

**3. And the demand was landed at the declarator rather than at the definition.**
3.9p5 requires the declared type of an object complete *where the object is
defined*, and `init_declarator` asked it of every declarator that is not a
typedef and not a function - so `extern holder<box> e;` and 9.4.2p2's
`static holder<box> m;` written in a class each instantiated a class neither
defines an object of.  The question `require_creatable_object` is already asked
under is the same one - `defines_object`, or a non-static data member - so the
two now stand together in `declare_object_declarator`.

**4. Two contexts that do require one were asking for nothing.**  With the
naming no longer a demand, what is left has to be the whole of 3.9p5's list this
analysis reaches, and two of its entries had no reader at all - which the
over-instantiation had been covering for, so each is a defect of the checkpoint
as it stands and not of the fix.  **8.3.5p6's other
arm**: the parameter and return types of a function *definition* shall be
complete, and `declare_parameters` - the one walk only a definition makes - built
the objects a body names out of a class with no layout, which emitted
`copyobj` with a **zero byte count** that the harness's own sanity validation
rejects.  **3.9p5 over an expression**: 3.4.2p2's associated classes,
13.3.1.2p3's member candidates, 13.3.1.1.2's surrogate calls, 13.3.1.4's
conversion functions and 12.4p11's destructor are each read off an expression's
class, and a reference parameter naming a specialization a typedef had declared
reached all of them with the class still empty -
`an operand of + or - is neither arithmetic nor a pointer` for `h + 3` on a
program both oracles compile.  `SemaAnalyzer::expression` is where every
expression the layer reads leaves, so the demand is one call there and two tests
for the pointer, the reference and the scalar it usually finds.

**5. And the demand had to be told that a reading is not a use.**  14.6p8's
reading of a template definition names specializations and asks for nothing -
which is what `instantiate_class` already answers `checking_ == 0` with - so a
demand reaching one while `checking_ > 0` would read the pattern in the checking
dialect and leave a class with none of 12.1's members it is owed.
`check_template_definition` declares the pattern's own parameters, so finding 4's
demand fired there: `pair<const string, tag>` was completed by the reading of
`make_pairish`'s body and
`12.8p32 asks for a constructor the program has none of` at the call.
`require_complete_type` returns on `checking_ > 0` for the same reason
`asked_specialization` is never called there.

### Changes

| what | where |
| --- | --- |
| 14.7.1p1: naming a specialization is never a demand, however many times it is written | `sema_template.cpp` (`asked_specialization`) |
| 14.6p8: a reading asks for no definition (`require_complete_type` on `checking_ > 0`) | `sema_template.cpp` |
| 3.9p5: the demand moved from the declarator to the declarator that *defines* an object | `sema_analyzer.cpp` (`declare_object_declarator`) |
| 8.3.5p6's definition arm: the return type and the parameter objects a body names | `sema_function.cpp` (`declare_parameters`) |
| 3.9p5 over an expression, at the one place every expression leaves | `sema_expression.cpp` (`expression`) |
| the depth and the scaffolding the suppression needed, dropped | `sema_analyzer.cpp`, `sema_analyzer.h`, `sema_declaration.h` |

Five regression tests under `cppgm.tests/course/pa19`, one per naming family and
one per demand: `100-specialization-named-again-declares-once`,
`100-declared-object-of-specialization-defines-none`,
`100-function-declaration-names-no-definition`,
`100-function-definition-completes-its-places` and
`100-operator-on-a-declared-specialization`.  **Four of the five fail against the
pre-audit binary built from `7afd0f26`** - three on a pattern read where its
argument was still incomplete, and the last from the other side, `h + 3` over a
reference parameter with no member candidates.  The fifth is the demand 8.3.5p6's
definition arm makes, which that binary got from the naming it should not have
instantiated at.

### Performance Evidence

The demand is one integer test where a unit named no specialization and, where
one is outstanding, a `strip_cv`, a kind test and - for a class alone - one hash
probe.  The one place it is asked per *node* rather than per construct is
`SemaAnalyzer::expression`, so that is what the sweep is built around.  Each
shape timed twice against a `7afd0f26` worktree build made with `make build`,
`--emit-lowir -O0`:

| shape | 32 | 128 | 512 | 2048 | before |
| --- | --- | --- | --- | --- | --- |
| n arithmetic expressions in one body | 0.00 s | 0.00 s | 0.01 s | 0.07 s | same |
| the same with one specialization outstanding | 0.00 s | 0.00 s | 0.01 s | 0.07 s | same |
| n expressions of a specialization's own class type | 0.00 s | 0.00 s | 0.01 s | **0.05 s** | 0.06 s |
| n specializations named by a typedef and never used | 0.00 s | 0.00 s | 0.02 s | 0.12 s | same |
| n specializations named by a typedef and each used | 0.00 s | 0.02 s | 0.09 s | 0.43 s | same |
| n specializations named through a pointer typedef only | 0.00 s | 0.00 s | 0.02 s | - | same |
| n function definitions taking one by value | 0.00 s | 0.00 s | 0.02 s | 0.11 s | same |
| n specializations of one template, each with a member | 0.00 s | 0.01 s | 0.06 s | - | same |
| n out-of-class member definitions of one template | 0.00 s | 0.01 s | 0.03 s | - | same |
| n class templates, each deriving from the previous | 0.00 s | 0.01 s | 0.04 s | - | same |

The quadratic the tier already had is unmoved: n out-of-class member definitions
of a template with n specializations is 0.03, 0.11 and 0.47 s at n = 32, 64 and
128 against the pre-audit 0.03, 0.11 and 0.48 s.  Peak RSS is flat at n = 2048 -
36.2 -> 36.2 MB for the typedef-only shape, 96.7 -> 96.8 MB for the used one,
21.4 -> 21.4 MB for the class-typed expressions - because what the change adds
per specialization is one `bool` and one counter, and what it removes is a
reading of a pattern per naming.

### Validation

- **360 / 369 -> 365 / 374**, the five new tests being five of the shapes these
  leave and the failing 9 the same 9 by name; pa1-pa18 **1778 / 1778**.
- **File audit passes** for pa19 over `dev/src` with the five header-weight
  warnings the shared headers have carried since PA18, and no suppression; the
  build prints nothing.  `sema_analyzer.h` is at **2393 against
  the limit of 2400** and `sema_template.cpp` at 2876 against 3000; this audit
  leaves `sema_declaration.h` 25 lines lighter and `sema_analyzer.cpp` 14, because
  the depth and the `ReadingHeld` class the suppression needed are gone.
- **Every `.ref` regenerates byte-identically** over all 319 fixtures under
  `pa19/tests` and all 50 already checked in under `cppgm.tests/course/pa19`,
  diagnostics included; no committed fixture moved.  The five new `.ref` files
  are the reference's own output.
- **Forty-seven synthesized shapes through three binaries** - this compiler, the
  `7afd0f26` pre-audit build and `reference-binaries/cppgm++` - with **g++**
  beside them: **30 contexts that name a specialization and require no complete
  type** (three counts of typedef, two alias-declarations, a member typedef and a
  member alias, two `extern` declarations, a static data member declaration, a
  return type, three parameter forms, a trailing-return-type, a default argument,
  a pointer, a reference member, two pointer typedefs, a template argument, two
  cast type-ids, a `sizeof`, a condition, a new-expression, a friend declaration,
  an array of pointers and a qualified member declaration) and **17 that do**
  (an object, a data member, an array, a by-value parameter, a return by value,
  new and delete, `sizeof`, `alignof`, copy-initialization, assignment, a base
  class, member access through a pointer, an operator on a reference parameter,
  ADL, a reference binding, a template argument used as a member, and a
  definition's places).  **46 of the 47 are identical to the reference as
  emitted LowIR after the harness's own canonicalization**; the survivor is
  `struct d : holder<box> { };`, where the reference emits a constructor
  definition this unit elides - the pre-audit binary emits exactly what this one
  does, so it is 12.8p12's group and not this audit's.  The pre-audit binary
  refuses **20 of the 30** and **1 of the 17**.  Fourteen further shapes over
  14.7.2's explicit instantiation, a specialization nothing ever demands, a
  static data member defined out of class, a virtual member and a destructor, a
  nested class of a specialization, a specialization over a specialization, a
  conversion function, an out-of-class member definition, a base that is itself
  a specialization, a deduced call and an overload set are identical to the
  reference in every one.
- **Run evidence with scalars.**  One unit writing eleven namings over ten of the spellings
  over a specialization whose own declarations need its argument complete -
  lowered here, byte-identical to the reference's LowIR, run through
  `lowir2cy86` and `cy86` - returns **39**, which is what g++ builds it to
  return.  The pre-audit binary refuses that program outright.
- **Two units, both orders.**  Two units each naming one specialization through
  a typedef, an alias-declaration and an `extern` declaration and each using it
  emit LowIR **identical to the reference's** in either order, and the program
  they make runs to 0 as g++ builds it to.
- **Valgrind clean** with `--error-exitcode=99` over all 374 fixtures.

## Open Gaps

**An array of a class that holds a class owes the reference an empty startup
body.**  `struct box { int q; }; struct named { box v; }; named table[3];` emits
an empty `__cppgm_init` in `reference-binaries/cppgm++` and none here, while the
same class declared as one object and the same array over a class of scalars
alone are identical in both.  It predates this tier - the `7afd0f26` build emits
exactly what this one does and no template is needed to write it - and it is the
same question C11's audit answered for 3.6.3p1's registered end, read at 12.6p1's
walk of an array's elements.  No fixture writes it.

**8.3.4p1's zero bound, which the reference and g++ both take as an array.**
`struct S { int a[0]; };` is `an array bound is zero` here and accepted by
`reference-binaries/cppgm++` and by **g++**, which reads it as the GNU
zero-length array; at namespace scope `int a[0];` is refused by this compiler
and by the reference, and accepted by g++ alone.  The clause says the bound
shall be greater than zero and this compiler follows it, so the refusal is not
wrong - what is worth recording is that C12's reading of 14.6p8 now accepts the
same declaration *inside* a template pattern, so one program is read two ways by
one compiler.  No fixture writes either, and 8.3.4p1's own text is the reason
the narrower rule was not landed instead.

**5.16p4's conditional a reference binds, where only g++ agrees.**  `const int &
r = c ? 5 : 6;` binds one temporary of the conditional's own type here and one
temporary *per arm* in `reference-binaries/cppgm++`, and the reference's arm
binds the *arm's* type: `const long & r = c ? 5 : 6L;` gives it a `tmpref__2 :
i32` and then reads `load i64` out of it.  Where one arm is an lvalue it binds
that object rather than a copy - `const int & r = c ? 5 : x;` with `x = 2;` after
it reads **2** in the reference and **1** here and in **g++**, which is 5.16p4:
operands of different value categories make the result a prvalue, so the
reference binds is a temporary.  Two answers against one, and one of them reads
eight bytes out of four, so the reference is alone; the `condaddr` path this
would take is the one it already writes for an lvalue conditional, and turning it
on for a prvalue would be following the reference into both.

**12.2p5's temporary a namespace-scope reference binds wants static storage.**
The temporary is given a slot in the startup body's frame - by
`reference-binaries/cppgm++` as of its own output and by this compiler as of the
audit's fix - so `const int & bound = 20; int main() { return bound; }` runs to
**152** through `lowir2cy86` in both and to **20** in g++, which gives the
temporary static storage as 12.2p5 requires.  Matching the reference is what the
suite grades and what the fix does; the storage class is a rule of its own and no
fixture reaches it, so it is recorded rather than landed.  The block-scope
`static` form is refused outright here, which is 6.7p4's guard variable and a
feature of its own.

**A specialization's written-out name spells a pointer and a cv-qualifier
differently.**  `held<int *>::value` is `@held_int_____value` in
`reference-binaries/cppgm++` and `@held_int____value` here - the reference writes
the argument as `int *` with the space a program writes and this compiler as
`int*` - and `held<const int *>` is `@held_int_const_____value` there against
`@held_const_int____value` here, the reference writing 7.1.6.1p1's qualifier
*after* the type.  A global's LowIR name is not masked by the comparison, so a
fixture over either would fail; it is the naming path the tier audit landed
rather than this checkpoint's, and getting it right is the whole of 8.1p1's
declarator spelling - array, function, reference and member-pointer arguments
included - so it is recorded with the ABI's decltype encoding.

**The image spells a floating zero and an empty array differently.**  `double a =
0.0;` is `= 0` in the reference and `= 0.0` here, with `0f` against `0.0f` and
`0L` against `0.0L` for the other two widths, and a *body* operand is `0.0` in
both - so the difference is the image's spelling of 2.14.4's value and not the
value.  Beside it, `int arr[3] = {};` is three `i32 0` items there and one `zero
12` here.  Both predate this tier (the `b95b5da0` build writes what this one
does), both are PA14's image rather than any template rule, and no fixture writes
either.

**A static data member defined with `= T()` runs the zeroing in the reference.**
`pair_of plain_owner::member = pair_of();` and `template<class T> T
held<T>::value = T();` each write the zeros into `__cppgm_init` there and are the
image here, while the *same* initializer on a namespace-scope variable is the
image in both.  So the rule the reference is following is about 9.4.2p2's
definition rather than about 14.7.1p1's instantiation - the difference stands
with no template in the program - and it writes storage that 3.6.2p1's zero
already holds.  It predates this tier and no fixture writes it.

**Two stripped keys this compiler never writes.**  `trivial_lifecycle=yes`, which
the reference puts on every definition 12.8p12 leaves trivial, and the absence of
`unwind=no` on a destructor the reference does not mark - this compiler writes
`unwind=no` there and the reference does not.  Both are in
`compare_results_common.pl`'s relaxed set, so no fixture can fail on either;
they are named here because that is the only reason they have not been noticed.

**Two units of one weak global.**  Two units each defining one instantiated
static data member emit **two** `global` lines for it in the reference and one
here.  A link needs one, and the program builder keeping the first is what the
recorded `object_root` gap is about; this is the same merge seen from the data
side.

**14.8.2's substitution failure ends the translation unit.**  A deduction that
substitutes the arguments into a decltype-specifier reads the expression again,
and where that reading has no answer the error is thrown rather than the
candidate dropped: `pick(T a) -> decltype(a + a)` beside `pick(T * a) ->
decltype(*a + *a)` called on an `int *` is
`an operand of + or - is neither arithmetic nor a pointer` here, and both oracles
choose the second.  It is the shape a decltype return type exists for, and
**substitution-failure candidate dropping and SFINAE are on PA19's Out Of Scope
list**, so the reading is left where the milestone puts it; what it owes when the
subset reaches it is 14.8.2p8's immediate context - the reading a candidate's own
substitution does, and no reading below it, failing that candidate alone.

**Two declarations that name their places differently, where only g++ agrees.**
`template<class T> auto pick(T a, T b) -> decltype(a + b);` followed by
`template<class T> auto pick(T x, T y) -> decltype(x + y) { }` is
`a call of pick has no best declaration` here and in
`reference-binaries/cppgm++`, and **g++ compiles it**: 14.4p1 makes two
expressions equivalent when they contain the same sequence of tokens with the
names looked up to the same entities, and 8.3.5p10 does not put a place's name in
the function's type - so whether a place is "the same entity" across two
declarations is the question the clause leaves open, and this compiler and the
reference both answer it by the spelling.  The head's own names are canonicalized
by position, which is the half 14.4p1 states outright; the places' are not, and no
fixture through pa24 writes a pair.

**A decltype-specifier over a non-static data member at class scope wants an
object.** `struct box { int v; decltype(v + v) doubled(); };` is
``this` is written outside a member function` here and both oracles accept it:
5.1.1p13 lets an id-expression naming a non-static data member stand in an
unevaluated operand, and the expression layer adds 9.3.1p3's object to every
member name it reads.  The same declaration with no template anywhere is refused
identically, so it is the PA12 expression layer's rather than this tier's - what
it owes is a reading of a member name that stops at the declaration where no
object is being named.  `decltype(v)` alone, which is 7.1.6.2p4's id-expression
arm, is answered without one and works.

**One region is rebuilt per decltype-specifier rather than per region.**
14.7.1p1's second reading builds a fresh `Scope` for each of 14.1p1's and
3.3.7p1's regions standing over the specifier, so a clause of n places each of
whose type is a decltype-specifier over the first costs n regions of up to n
declarations - 0.19 s and 99.9 MB at n = 512, against 0.05 s and 30.7 MB at
n = 256.  The narrower rule is one rebuild per region and bindings, memoised on
the substitution, which needs a key over the bindings map that the per-call memo
does not have.  A 512-place clause is not a translation unit and every shape a
program writes is linear, so it is recorded rather than landed.

**A trailing-return-type in a function declarator is dumped without its
spelling.**  Found by putting all 59 shapes the C7 audit swept through
`--emit-ast` against the pa10 reference: `auto f(int a) -> int` is
`trailing-return-type` here and `trailing-return-type int` there, `-> int*` is
`trailing-return-type int*` there, and `-> C` agrees because
`parse_trailing_return_type` spells the node only where the type-id is one
`TypeName`.  A *lambda*'s `-> int` is textless in both, so the reference spells
the declarator's and not the lambda's.  It is PA10's rather than this
checkpoint's - the code is `15d8a001`'s, the milestone that wrote the AST - and
the one pa10 fixture with a function declarator's trailing-return-type writes
`-> C`, which is why 1778 / 1778 stands over it.  The other 58 shapes dump
byte-identically.

**A member function template's explicit argument list does not parse.**  C10
landed the declaration, 9.3.1p3's object parameter and the out-of-class
definition, and this audit landed 9.4p1's static arm of it and 14.5.6.2p2's
ordering - so `template<class E> int graph::search(E const &) const { }` written
outside the class compiles and matches the reference's LowIR.  What is left is
`s.f<int>(0)`, which is not a translation unit at all: `parse_member_id` accepts
a bare identifier after `.` and never tries 14.2's argument list, so 14.8.1p2's
partial list is unreachable through a member access.  Both oracles compile it,
it is why `pa22`'s `300-explicit-instantiation-deduced-member-function-template`
is the one explicit-instantiation fixture in the later assignments this compiler
cannot read, and it is what the plan's C11 owns along with the two-clause head
and 12.3.2p1's conversion function template.

**14.7.2p5's second explicit instantiation is unasked.**  `template int f<int>
(int);` written twice is ill-formed and **g++ refuses it**; this compiler and
`reference-binaries/cppgm++` both accept it and emit one rooted definition.  It
is a rule of its own rather than a reader of the one the C7 audit landed, and no
fixture through pa24 writes it.

**Six more places the reference is alone, found sweeping 8.2p7 and 14.8.1p2.**
It refuses `int g(int (x[10]))`, `int g(int (x), decltype(x) y)` and
`template<class T> int g(T (x))` - three spellings of 8.2p7's parenthesized
place that this compiler and **g++** both read as a declarator - and it refuses
all three forms of a partly written argument list at an explicit instantiation:
`template int mix<char>(double);`, the same over a static member template, and
`extern template`.  14.8.1p2 lets a trailing argument be omitted wherever a
template-id names a function template, 14.7.2p2 asks only that the declaration
name a specialization, and **g++ accepts all three**, so the audit lands them on
g++'s side.  None can be a committed fixture, because the reference is what
writes a `.ref`.

**Three places the reference is alone, found sweeping 8.3p1 and 14.7.2.**  It
cannot find a parameter name in the body of a function whose declarator-id
stands under a nested clause - `int (*f(int a))(int)`, `int (*f(int a))[2]`,
`int (&f(int a))[2]`, `int (*(f(int a)))(int)`, the same with an
exception-specification, as an out-of-class member definition and under a
template head, eight shapes in all - where 8.3p1, its own `_Z1fl` and **g++**
say the place is the function's own.  It refuses `template int t<int>::v;`,
which 14.7.2p1 lists outright and **g++ accepts**.  And it roots no definition
for a member *class*'s members at an explicit instantiation, where **g++** emits
all 40 of a 40-deep chain as this compiler does.  None can be a committed
fixture and each is decided by g++ and the standard.

**The class's own parameter names are still reachable from a definition written
outside it.** `template<class T> struct A { void f(); };` with
`template<class U> void A<U>::f() { T x; }` compiles here and in
`reference-binaries/cppgm++`; g++ refuses it, because 14.1p2 gives the
definition a head of its own and the class's is not in scope for it. The seam is
structural rather than a rule left unasked: the class scope is what a member
definition's body is read inside, and the region enclosing that class is the one
the class body itself was read against - the one binding the class-head's names.
Refusing here means the class scope having two enclosing regions, one for its
own body and one for a definition written outside it, which is a change to what
a class *is* in this model rather than to what a rule asks. The names the C5
audit settled are the ones the definition's own head wrote, which is what decides
which argument a name reaches.

**15.4p1 over two declarations of one function template.**
`template<class T> void f(T) throw();` followed by
`template<class U> void f(U) { }` is refused here and accepted by
`reference-binaries/cppgm++`, which refuses the same mismatch between two
declarations of an ordinary function; g++ warns on both and compiles both. The
standard makes all of them ill-formed, so the checkpoint's rule is asked of the
declaration pair the reference does not ask it of. No fixture writes one.

**An exception-specification writes none of the EH the reference writes.**
`void f() throw(); void f() throw() { }` emits, in the reference, an
`eh_try`/`eh_filter` dispatch calling `__cxa_call_unexpected`, and this compiler
emits the body alone - for an ordinary function and for an instantiated member
alike. It predates this tier (the pre-C5 binary emits the same), no fixture
reaches it, and 15.4p8's `unexpected` handler is what the work owes.

**Recorded, not defects.** A specialization of a template declared in
7.3.1.1p1's unnamed namespace binds `internal` here and `weak` in the
reference; 3.5p4 gives every name in that region internal linkage and **g++
emits it local**, so the reference stands alone. An instantiated constructor
emits both of 12.1's entry points where the reference emits only the
complete-object one; **g++ emits both**, and the reference's own non-template
out-of-class constructor gets both - so this is the reference's rule for
instantiated definitions, and matching it is what turned three fixtures green.

**14.5.1.3p1 read for every specialization rather than for every use.** A class
template's out-of-class member definitions are read once per specialization,
wherever that specialization is completed, so n definitions and n
specializations cost n^2 readings even where no member of any of them is called
- 0.73 s at n = 128, against `reference-binaries/cppgm++`'s 1.00 s. C8 landed
14.7.1p1's half of this - a member defined *in* its class waits on
`held_definitions_` for the use that names it - and `instantiate_member`'s is
what is left: an out-of-class definition is read where it stands, for every
specialization already made, so it is the milestone's one quadratic in the
*tier* rather than in the program.

**Out of scope and still named.** A variable template's partial specialization
is written into the object file as `_Z6v<T,T>`, which is not an ABI name.
14.5.5 partial specialization and variable templates are both in PA19's Out Of
Scope list, so the input's behaviour is undefined for this milestone; the name
is left where the feature is.

**The spelling a specialization is named by** is exponential in the depth of a
nest whose arguments double, as measured above. It is the reference's shape too
- and by a factor of 18 at n = 20 - and no fixture reaches it, so it is recorded
rather than re-architected: fixing it means not storing a specialization's
written-out name at all.

**14.6.1p6 where only g++ agrees.** The rule is now asked wherever a
declaration binds a name, and the reference accepts six of those where the
standard and g++ refuse them: `typedef int T`, `int T`, an enumerator, a
parameter of a definition, a local function declaration, and the template's own
declared name.  It also accepts a class, an enumeration or a namespace-alias
written in a *nested* block of the body while refusing the same declaration at
the body's own level - what it is answering there is a collision with the
bindings it puts in that one region, not 14.6.1p6.  This compiler refuses all of
them at every depth.  The one shape the seam does not reach is a parameter of a
declaration that writes no body: 3.3.4's function prototype scope is not a
region this model opens, so the name is bound nowhere to be asked about, and
both oracles accept it.

**6.6.4p1 is asked where one of the three body readings ends.**
`require_labelled_gotos` runs where a body written outside its class is read and
where an instantiation reads one; `write_definition`, which reads a member
function defined *in* its class at the end of the unit, does not ask it, so
`struct S { void f() { goto miss; } };` is accepted. It was accepted before this
checkpoint too, and `reference-binaries/cppgm++` accepts it while g++ refuses
it, so it is recorded rather than changed.

**A handler's name is where the reading meets a construct the milestone does
not model.** `catch (int caught) { return caught; }` in an uninstantiated
function template is refused here - the definition-time reading looks `caught`
up and the walk that reads a handler declares nothing for it - and both oracles
accept it. The cause is not the reading: the same try-block in an *ordinary*
function is `a statement is outside the PA12 subset`, so an instantiated one is
refused whatever the reading says, and declaring the exception-declaration for
the reading alone would make an uninstantiated template compile where the
instantiated one cannot. It is recorded with the try-block rather than fixed
half-way, and 15.3p2's region is what the fix owes when the subset reaches it.

**7.1.3p3's other half - that a redeclaration names the same type - is unasked.**
`typedef int A; typedef long A;` at namespace and at block scope, and `struct A
{}; typedef int A;` in either, each declare one name for two types and are
accepted here. g++ refuses all of them; the reference accepts every one but
`struct A {}; typedef int A;` at namespace scope, where it stops with an
internal-looking `missing class info` rather than a diagnosis and accepts the
same shape written with an enumeration or inside a block. What is unasked is
7.1.3p3's agreement of *type* between two declarations rather than 9.2p1's
declared-twice, which is a rule of its own and not the half this checkpoint
landed, so it is left where a reading of 7.1.3p3 would put it.

**A member function template's address is not a target 13.4p1 chooses through.**
`int (S::*p)(int *) = &S::m;` over two member templates finds no declaration
here; `resolve_target`'s pointer-to-member arm asks each declaration for the
pointer type it *has*, which a template has none of until a deduction makes one.
`reference-binaries/cppgm++` does not compile it either, and no fixture writes
it, so it is recorded where the rest of 14.8.2.5's pointer-to-member pairs are.

**The parameter record is keyed two ways.** C6 moved `defaults_` from
`function.id` to `wrote_defaults(function).id` at the writer and at
`accepts_arity` and `write_default_argument`; `required_parameters` and
`has_default_argument` (`sema_class.cpp`) still ask `function.id`. The two keys
differ only where `primary` or `shadowed` is set, and **a member of a class
template specialization has neither** - `primary` is written at exactly two
places, both of them a specialization of a template *itself*, so an instantiated
constructor's record is its own and the two readers find it. What is left is a
function-template specialization and a using-declaration's copy, and 12.8p12
keeps both out of what those two readers are asked about; 12.9's inherited
constructors from a class template base and 12.8p2's copy constructor with a
defaulted second parameter emit identical LowIR either way. C8 has now landed
14.7.1p1's instantiated declarations and the two keys still agree - a member of
a specialization with a default argument emits the same parameter list as the
reference - so it stays one key per reader.

**A `= default` copy constructor is trivial in the reference and user-provided
here.** 8.4.2p5 leaves a function explicitly defaulted on a declaration that is
not its first one *user-provided*, so `struct box { box(const box&); ... };`
with `box::box(const box&) = default;` has a non-trivial copy constructor and a
copy of a `box` is a call of it. `reference-binaries/cppgm++` writes `copyobj`
at the copy and marks the definition `trivial_lifecycle=yes`. Beside it, an
*in-class* `= default` copy constructor of a trivially copyable class: the
reference emits the weak definition and this compiler emits none, so a call from
another unit would find nothing to link to. Both predate this tier - they are
PA16-PA18 lifecycle questions rather than 8.3.5p10's - and no fixture reaches
either, so they are recorded where the rest of 12.8's triviality is. A fixture
over a defaulted definition has to make the class non-trivially copyable for
another reason, which the six the C6-audit review added do.

**Two places the reference is alone, found sweeping 8.3.5p10.** It binds a body's
name to a parameter place 3.3.4 ends at another declarator: in
`int chosen = 7; int pick(int) { return chosen - 2; } int pick(int chosen);` the
reference loads the parameter slot where this compiler and `g++ -std=c++11` both
load the global, and the reference's program returns the wrong value. And it
writes `binding=strong` for an explicit specialization of a function template
where this compiler writes `weak`, which the comparison strips. Neither can be a
committed fixture and neither is C6's.  The first survives the 240-ordering
cross-product the C6 audit ran, where it is the only shape of an ordinary
function's parameter names the two compilers disagree about.

**12.9p1's inherited constructor keeps its arity in both other compilers.** For
`base_of<int>(int, int = 0)` inherited through `using base_of<int>::base_of;`,
`reference-binaries/cppgm++` and `g++ -std=c++11` both emit a two-parameter
`derived::derived` and apply the default at the call, where this compiler emits
the one-parameter candidate 12.9p1 forms by omitting the trailing defaulted
parameter and 12.9p3 gives no default argument to - a *signature* difference the
comparison does not strip, so a fixture over it would fail. Two oracles against
one makes it a defect rather than a reading. It is **not C8's**, which is what
the C7-audit ledger row guessed: the identical difference stands over
`struct base_of { base_of(int, int = 0); };` with no template anywhere in the
program, so what forms the candidate set is 12.9p1's own reading and not
14.7.1p1's instantiated declarations. It is a PA16-PA18 lifecycle question and
no fixture through pa24 writes one.

**A member class's table is asked for where the specialization is completed and
not where an object of it is built.** 10.3p10 leaves it unspecified whether a
virtual member of a class template is instantiated where nothing else would
instantiate it, and the walk this audit lands takes the broad reading: every
class the instantiation made is asked for its virtual members, whether or not
this unit ever builds an object of it and so whether or not it ever emits that
class's table. The cost is one grant per virtual member, which is flat in the
count - 512 sibling member classes are 0.09 s -> 0.10 s - and quadratic in the
lexical *depth* of the nest the members stand in, because the body granted at
depth n looks its names up through an n-deep chain: a 512-deep class nest with a
virtual member at every level is 0.40 s and a 1024-deep one 1.51 s, against
0.06 s for the same nest with no virtual member. Both numbers are the pre-C8
binary's to the hundredth, so the reading is the milestone's rather than this
audit's - what the `43aa2aa0` build saved on that shape it saved by writing an
unresolved symbol. `reference-binaries/cppgm++` is 0.03 s and 0.06 s, which is
the narrow reading: the table is emitted where the vpointer is stored, so the
demand a class's slots make is the demand its *constructor* gets. Making that
the rule means walking `entity.vtable`'s final overriders from
`demand_constructor_definition` rather than the declarations from
`complete_specialization`, and a 512-deep class nest is not a translation unit,
so it is recorded rather than re-architected.

**Two units defining one specialization's member keep one copy, and the root
goes with the copy that is dropped.** `template int S<int>::m();` in one unit
and `s.m()` in another emit three `function` entries in the reference - one per
unit plus `main` - and two here, because the program builder keeps the first
unit's definition of a symbol and drops the second. With the explicit
instantiation first that is harmless; with it second, the copy kept is the one
with no `object_root=yes` and the program loses a root it owes. The reference is
order-dependent here too for a function template's own explicit instantiation -
both compilers write no root when the instantiation is the later unit - so what
this is, is one merge dropping a fact rather than choosing between two: the
`object_root` of a dropped duplicate should join the copy that stays. It is the
program builder's rather than this tier's and no fixture is multi-unit, so it is
recorded with the rest of what only a link would see.

**Run evidence needs scalars.** The `pa13` LowIR -> CY86 path is the only way to
run what this milestone emits, and it hands a by-value class parameter garbage -
from our LowIR and from the reference's alike. A differential probe that passes
a class by value is measuring the scaffold; every disagreement one reports has
to be reproduced with scalars and pointers before it is a finding.

## Checkpoint Audit Ledger

| # | checkpoint | reviewed at | blockers found / fixed | result |
| --- | --- | --- | --- | --- |
| C1, C2, C2 completion | the whole tier as landed, reviewed at its completion: `TemplateInfo` as the pattern a template-declaration parameterises, 14.7.1p1's instantiation as a second reading of it, the function tier and 14.5.1.3p1's out-of-class members, the two points a specialization has, 14.6.2p1's dependent argument list, `SemaAnalyzer::substituted`, and 14.8.2's deduction | `aa6fb90f` | 6 / 6 + 1 performance, in one family - **the object file's name for a specialization, which this suite cannot see**: `canonicalize_lowir_for_compare` strips `object=`, `binding=` and `alias object` and pairs functions by masked body shape, so eleven fixtures emitted symbols containing `<`, `>`, `,` and spaces and **nine of them passed**. A name was split out of a spelling at every `::`, so a template-argument-list that spells a qualified name made `api::pair<const api::text<char>,api::tag>::pair` five components and not three; `owning_classes` walked the region a definition was *written* in, so 9.7p3's out-of-class nested class lost the template above it; `abi_type` handed the encoder `Box<int>::Tag` as one spelling; the ABI's `<template-param>` was never made a substitution candidate, so **every** function-template specialization's symbol differed from g++ and the reference alike; 14.7.1p1's instantiated definition was bound `strong`, so two units naming `Box<int>` would each claim to own `_ZN3BoxIiE5twiceEv` - a duplicate symbol at link, over 19 symbols, none of which failed; and the same fact's two other readers kept asking `inline_function`, so an instantiated constructor owed a `C2` entry the reference does not and an instantiated virtual destructor owed a `D0` for a class no unit owns. Beside them, 14.6.2p1's own cost: `is_dependent` recursed with no memo over what is a graph and not a tree, and `substituted` asked it in front of its own memo | 194 → **200 / 295**, two of them the regression tests these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` and `.ref.witness` regenerates byte-identically; `object=` differences against the reference 54 → 9 tests and every survivor a definition rather than a name; 13 names byte-identical to g++; a three-unit program order-free in all four permutations; seven scaling shapes to 512 and the one that is not, measured against the reference; valgrind clean; the pa1-pa19 report 15.6 s |
| C3 | the call a template joins, and what the ordering it reaches leaves out: 14.8.2.5p3's parameter written over no template parameter, 14.8.2.1p2/p4's reference, 8.3.6p1's unwritten trailing arguments, 13.3.1.2p4's first operand, 14.8.2.2's target type, 14.8.2.1p6's overload set, 14.5.6.2's ordering, 14.5.6.1p5's equivalent declarations, and 8.5.1/8.5.4 split into `sema_init_list.cpp` | `e67acde3` | 5 / 5 + 1 performance, all of them on paths the increment did not join up - **the readers a landed rule was not given, and the clauses beside the ones it landed**: 14.5.6.2's ordering reached 13.3.3p1's tie and not 13.4p1's target, so `int (*p)(int *) = pick;` over `pick(T)` and `pick(T *)` took whichever was declared first, in all four contexts `resolve_target` answers; the ordering itself stopped at p7, so `f(T &)` against `f(const T &)` was **ill-formed** where p9 - the clause p5 and p7 exist to leave - chooses the second; 14.8.2.1 landed p2 and p4 and not p3, so a forwarding reference deduced a parameter no lvalue can bind and dropped out of the candidate set; that same deduction makes `T` a reference type, and 5.3.3p2 measured `sizeof(int &)` as 8 where both oracles say 4; and 14.5.6.1p5's new answer that two declarations declare one template made a specialization named *above* that definition reachable, which emitted a `declare function` and no definition - **a program that does not link**, over a call, a target type and a member alike, and a suite that compares LowIR and never links could not see it. Beside them, 14.5.6.1p5's own cost: the question was asked of every pair of declarations of one name, each pair substituting one head's parameters for the other's and, over a class template, instantiating a specialization to do it - quadratic in time and in memory, where the answer is a fact of one declaration | 223 → **227 / 299**, the four new tests being the four regressions these leave; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 60 synthesized programs run through `dev/cppgm++`, the reference and g++ with every exit status now agreeing; declaring 512 overloads of one template name 0.36 s → 0.04 s and 44.8 MB → 15.8 MB; fifteen scaling shapes and the two that are 13.3p1's own quadratic; valgrind clean over all 299 fixtures; the pa1-pa19 report 10.2 s |
| C4 | the reading a template definition gets where it stands: 14.6p8's body read once at its own point in the PA11 dialect and again for each specialization, 14.7.1p1's naming made a declaration rather than a use, 14.6.1p6's redeclared template parameter, 9.2p1's member type declared twice, `FunctionReading` and `DialectReading`, and `sema_declaration.h`/`sema_function.cpp` split out | `fa07d078` | 5 / 5, all of them the reading's own edges - **the reading left something behind, and the two questions it added landed at some of the places a declaration binds a name and not the rest**: a template-id written in a definition being read makes a specialization, and it went onto the list that is not an inventory but what a *later* declaration is read for - 14.5.1.3p1's out-of-class member definition and the definition the template itself gets - so a specialization no instantiation asked for was completed and given a member, and where the member definition arrived after the reading it was completed from inside `instantiate_member` and the member was declared twice: **`m is defined twice` on a program both oracles compile**; 14.6p8's walk reached an expression written as a statement and stopped at every declaration's initializer, so `int y = nowhere;`, `for (int i = nowhere; ...)` and `S s = { nowhere };` were accepted; 3.4.2p2 was read as leaving *every* callee to the instantiation, where a fundamental type associates no namespace and `nowhere_at_all()` and `nowhere(1)` name what nothing declares; 14.6.1p6 reached a typedef, an alias, an object and a class-scope using-declaration and not a class, an enumeration, an elaborated declaration, a namespace-alias, an enumerator, a parameter, a function or a template's own name; and 9.2p1's question landed on the typedef-name side alone, so `typedef int A; struct A {};` and its enum and elaborated forms declared one name as two kinds of type in a class, a namespace and a block alike. Both oracles refuse the first three and the type-name over a typedef-name; g++ refuses the six 14.6.1p6 shapes the reference accepts, beside the two this checkpoint already refused | 235 / 301 → **242 / 308**, the seven new tests being the seven regressions these leave and the failing 66 the same 66; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 95 synthesized programs through `dev/cppgm++`, the reference and g++, with 19 of them compared as emitted LowIR and byte-identical to the reference; two units each naming a specialization no call instantiates emit only what they asked for, in either order; `object=` differences 9 → 7 tests and `binding=` 12 → 10, every survivor a test that already fails but the recorded unnamed-namespace one; every scaling shape where the checkpoint left it, including the two 13.3p1 quadratics and the exponential spelling; valgrind clean over all 308 fixtures; the pa1-pa19 report 10.4 s |
| C5 | the class a template makes of its own parameters: 14.6.1p1's current instantiation read once against a kept parameter region, 14.5.1.3p1's out-of-class member definitions read against the same two with 14.1p2's own head names, 14.6.2p1's dependent qualified name and 14.6.2p3's dependent base, 9.2p2's held bodies, 9.4.2p1's qualified class-head, 9.3.1p3's object parameter for a pattern, and 15.4p1 | `277a48bb` | 3 / 3 + 1 beside them, all of them **what the reading kept and what it did not keep**: 14.1p2 leaves each declaration of one template free to spell its parameters as it likes, and the names an out-of-class member definition's head wrote were bound in the one region the class was completed against - the region every reading of its members looks names up through - so they outlived the definition, and a head spelling those places in another order was refused there and read against the class's own spelling instead: `template<class U, class T> void A<U,T>::f() { U a; T b; }` over `A<int,char>` emitted `i8` and `i32` where the reference and g++ emit `i32` and `i8`, in a return type as much as in a body, and where two heads permute one another the second was `V does not name a type` on a program both oracles compile, and a name only the definition before it wrote was still standing; 9.2p2's held bodies were taken off the list and walked once, so a class declared *in* a body being read held bodies above the mark again that nobody ever read - `struct local { int reach() { return nowhere_at_all(); } }` in a function template was accepted - and `check_template_definition` never drained the list at all; and 14.6.2p1's dependent member name kept its spelling rather than the two facts the object file writes it from, so `typename T::car_type` was `T_` where **`reference-binaries/cppgm++` and g++ both write `NT_8car_typeE`**. Beside them, on the naming path the tier already owned: a specialization's argument list was spelled without the space a program writes, so a static data member of a two-argument one was `@A_int_char___v` against the reference's `@A_int__char___v` - a global name, which the comparison does not mask | 254 / 308 → **256 / 310**, the two new tests being the two regressions these leave and the failing 54 the same 54; pa1-pa18 1777 / 1777; file audit passes; every checked `.ref` regenerates byte-identically; 73 synthesized programs through `dev/cppgm++`, the reference and g++, 56 of them compared as emitted LowIR with every comparison agreeing but the two the exception-specification gap owns; the dependent-member names byte-identical to the reference *and* to g++ at two and at three components; two units with differently-spelled member heads order-free; `object=` differences 7 and `binding=` 10, every survivor a test that already fails but the recorded unnamed-namespace one; eleven scaling shapes measured against a pre-C5 worktree build and each where the checkpoint left it, plus the 14.5.1.3p1 quadratic the checkpoint made reachable, on which the reference is slower; valgrind clean over all 308 fixtures under `pa19/tests` |
| C6 | what a definition takes from the other declarations of the same entity, and what it owes the object file: 9.4.2p3's in-class brace-or-equal-initializer as the value the definition's storage holds and as what a read of the member is, 8.3.5p10's parameter name made a fact of the function held beside the default-argument, 8.3.5p5's array and function parameters made the pointer objects they are, and 8.5p8's "holds nothing" read of the whole object | `955dce9f` | 1 found, 1 fixed: a declaration written below a *template's* definition renamed the parameter objects every specialization had already made, so `template<class T> T zero_of(T) {...}` followed by `template<class T> T zero_of(T sample);` emitted `%sample` where the reference emits `%__param0` - **a regression C6 shipped**, on a difference the suite compares, that no fixture had the shape for. 14.7.1p1 leaves a specialization a declaration nothing wrote, so the pattern's spelling is frozen where the definition giving it a body is read and only a program's own declaration puts an object on the waiting list.  Recorded, not fixed: `required_parameters` and `has_default_argument` still key the record by `function.id` where C6 moved the writer to `wrote_defaults`, which is vestigial today because 12.8p12 keeps special members out of the template tier | 262 / 311 -> **264 / 313**, the two new tests being the regression this fixes and the failing 49 the same 49 by name; pa1-pa18 1777 / 1777; file audit passes; every `.ref` regenerates byte-identically over all 313 fixtures; 30 synthesized programs through the pre-audit binary, `reference-binaries/cppgm++` and g++, of which C6 moved 15 onto the reference and 2 off it and all 17 now agree; the three left disagreeing are metadata the comparison strips and one the reference is alone on against g++; three scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build at about 2% more memory; valgrind clean over all 313 fixtures under `pa19/tests` |
| C6 audit | 8.3.5p10's parameter name made a fact of the function rather than of any one of its declarations: `ParameterRecord` split out of `Default` beside `HeldInitializer`, a definition's own declarator beating the record, the objects a definition left unnamed waiting for the first declaration to name them, and 14.7.1p1 narrowing which of the template's declarations a specialization is spelled from | `d6700f4a` | 2 / 2, both of them **the same fact asked at fewer places than have it**: 14.7.1p1's narrowing froze a specialization's spelling where the pattern's *definition* is read, where `reference-binaries/cppgm++` freezes it at the template's *first declaration* - over all 120 orderings of an unnamed declaration, two differently named ones, an unnamed definition and a call, this compiler disagreed on the **16 whose first declaration is the unnamed one**, per place as well as per function (`two(T, T b)` then `two(T a, T)` is `__param0, b` and not `a, b`), while **the same 120 orderings over an ordinary function agree in every one**; and the widened fact reached the declarator path and not the one 12.8p28's and 12.9p8's definitions take, which have no declarator and are handed one declaration's list - so `box::box(const box&) = default;` below a class that named the place, an out-of-class `= default` naming a place the class did not, and an inherited constructor whose base is defined below the using-declaration each wrote `__param1` where the reference writes the name, and `= default` outside the class recorded nothing at all, being the one declaration form that returns before `record_declared_parameters` | 266 / 315 -> **272 / 321**, the six new tests being the six shapes these leave and the failing 49 the same 49 by name; pa1-pa18 1777 / 1777; file audit passes; every `.ref` regenerates byte-identically over all 321 fixtures; 240 declaration orderings plus 55 shapes over members, constructors, operators, static members, array/reference/function parameters, default arguments, class templates, special members and inherited constructors, through this compiler, the pre-audit binary and `reference-binaries/cppgm++`, with all six regressions failing against the pre-audit binary; what is left disagreeing is emission order, an `alias object` line and the one place the reference binds a body's name to a parameter 3.3.4 ends elsewhere; four scaling shapes at n = 32, 128 and 512 unchanged against a pre-audit worktree build within 1% of its memory; valgrind clean over all 321 fixtures; the build's two `-Wall` warnings gone |
| C7 | the type a declarator-id ends up under, and what lookup answers before a `<`: 8.3p1's constructor read from the level the id stands in, 14.2p3 asked of the overload set, 7.3.4p2's using-directive answered after every region the name is written inside, and 14.7.2's explicit instantiation with `object_root=yes` as the demand 3.2p3 has no use to point at | `c3f2411f` | 3 / 3, two of them **a rule landed at the question and not at the walk that answers it** and the third a fact carried as a terminal in a dump an earlier assignment is graded on: `declares_function` walks in to the declarator-id and asks which suffix it ends up under, and `declarator_type` - which is what *binds* a definition's places - still spent them on the outermost parameter-clause, so the two agree only where the level around the id wrote none.  `char (*getter(long key))(char value)` declared the right type (`_Z6getterl`) and emitted `function @getter(%value : i8)`: a body that names `key` was **refused on a program g++ compiles**, and one that names `value` - a place of the type `getter` *returns* - was accepted, over eight shapes counting the pointer, the array, the redundant parentheses, the exception-specification, the out-of-class member definition and the template head.  And the grammar's `explicit-instantiation-target` is a class-declaration *or a simple-declaration*, whose arm checked that a declarator existed and then `return`ed: `template int carried<int>(int);` emitted **nothing** where `reference-binaries/cppgm++` and g++ both emit the definition rooted - which four checked-in `.ref` files under pa22 and pa24 pin byte for byte - while `int slot; template int slot;` and `int plain(int); template int plain(int);` were **accepted** where both oracles refuse them.  And that form's node was the declaration's with `KW_TEMPLATE` hung on it, so the PA10 dump the shared AST feeds read `explicit-instantiation-declaration KW_TEMPLATE:template` where the reference writes `explicit-instantiation-definition` - and no pa10 fixture writes the form, so 1777 / 1777 stood over it.  What is right and swept clean: 14.2p3's overload set and 7.3.4p2's directive agree with both oracles over 15 shapes including the three that must not find the name, and 14.7.2p8's walk of the region a specialization opened agrees with **g++** at every depth, rooting all 40 definitions of a 40-deep member-class chain where the reference roots none | 283 / 326 -> **288 / 331**, the five new tests being five of the six shapes these leave and the failing 43 the same 43 by name; pa1-pa18 1777 -> **1778 / 1778** with the sixth, which is the pa10 dump; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 under `cppgm.tests/course/pa19`, diagnostics included; three cross-products through this compiler, the pre-audit binary and `reference-binaries/cppgm++` with g++ beside them - 20 declarator shapes now agreeing with g++ in every one and with the reference in twelve, 21 explicit-instantiation shapes agreeing with the reference symbol for symbol and root for root in twenty, and 15 lookup shapes agreeing everywhere; four later-PA `.ref` files that were missing `object_root=yes` now matching; a function returning a function pointer run through `lowir2cy86` to the value g++ builds it to return; two units in either order; seven scaling shapes at n = 32, 128 and 512 and the parenthesis nest measured to the 8000 the parser accepts; the PA10 dump byte-identical to the reference for both explicit instantiation forms; valgrind clean over all 332 fixtures |
| C8 | what a name in an instantiated body reaches, and which of a specialization's bodies an instantiation reads: 14.6.2p3's dependent base left off 3.4.1's chain for the specialization as well as for the definition, as a fact of the base-specifier the program wrote once; and 14.7.1p1's instantiation of the *declarations* of a class's members, each body held on `held_definitions_` for the use that names it | `43aa2aa0` | 2 / 2 + 1 beside them, and the two are **one shape - the deferral was landed at the three places a *use* stands and neither demand that has no expression behind it was given it**, over an output where a body nobody grants is a `declare function` and no definition, which is a program that does not link and a suite that compares LowIR and never links: 10.3p10's table was asked of the specialization's own declarations and not of the classes the same reading made, so `struct Inner { virtual int f(); }` nested in a class template emitted `@Outer_int___Inner__vtable` naming `@Outer_int___Inner__f` and **nothing defined it**, at one level and at three alike, where the `4ec3d164` binary and `reference-binaries/cppgm++` both do; and 14.7.2p1's explicit instantiation - the one declaration 3.2p3 has no use to point at - set `explicitly_instantiated` on a declaration whose body was still held, so `template int tester<int>::test();` over a member defined *in* its class emitted **nothing at all** where both of those binaries emit it with `object_root=yes`.  Beside them and older, the same clause asked of the innermost class alone: `template int tester<int>::probe::test();` was refused where the reference and **g++** both root it, because what says the declaration names a specialization was read off the class the prefix named rather than off the classes that class stands in.  What is right and swept clean: 14.6.2p3 over eight shapes - unqualified, `this->`, qualified, a non-dependent base under a template head, a nested class, an out-of-class definition, a non-template class deriving from a specialization and a class below one - picks the reference's callee in every one with g++ compiling all eight, and `deferred_conversion<incomplete>`, the clause the deferral is for, compiles here and is refused by `4ec3d164` | 294 / 331 -> **297 / 334**, the three new tests being the three shapes these leave and the failing 37 the same 37 by name; pa1-pa18 1778 / 1778; file audit passes and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; thirty use shapes - calls, `&`, conversion and operator functions, special members, uncalled and nested and inherited virtuals, all four 14.7.2 forms, subobject copy/assign/destroy, chained and cross-class bodies, target types, temporaries, arrays and two specializations - each swept for a symbol some entry names that no `function` line defines, all clean here and matching the reference's definition count where `43aa2aa0` leaves two unresolved; a rooted explicit instantiation run through `lowir2cy86` to the 42 g++ builds it to return; seven scaling shapes at n = 32, 128 and 512 against a `43aa2aa0` worktree build, unchanged but for the class nest 512 deep that returns to the pre-C8 0.40 s the pre-audit build bought with an unresolved symbol; valgrind clean over all 334 fixtures |
| C9 | the region a declarator's own places stand in, and the type an argument list reads: 3.3.7p1's function prototype scope with each place declared as its declarator-id is read, 5.1.1p3's `this` over a member declarator's trailing-return-type, 14.6.2.2p1's type-dependent decltype-specifier made a type of its own and answered by 14.7.1p1 reading the expression again, and 14.2's argument list read for a spelled decltype-specifier | `1b135271` | 3 / 3 + 1 beside them, and the three are **one shape - the type a decltype-specifier stands for was made a fact of the *reading* and not of the expression**: keyed by the AST node and the region's id, so one function written twice had two return types and `template<class T> auto added(T a, T b) -> decltype(a + b);` with the definition below a call emitted **`declare function @added` and no definition** - a program that does not link, over a suite that compares LowIR and never links - while 14.5.1.3p1's out-of-class member definition of the same shape was refused outright, both on programs the two oracles compile; and the second reading rebuilt every declaration the region had, so a place written *after* the specifier whose own type was a decltype over that region sent the substitution back into itself - `auto differ(T a, decltype(a - a) b) -> decltype(a - b)` was a **segmentation fault** at eight places as at two, where 3.3.7p1 begins a place's potential scope at its own declarator-id and the specifier could not have named it.  14.4p1 is the key both want: the specifier as written and the declarations the names it writes reach, with what each head *spelled* left out of both - 14.1p2 lets each declaration name its parameters as it likes and 14.4p1 makes a parameter equivalent to the one at the same position, which is what `-> decltype(T() + seed)` and `-> decltype(U() + seed)` need to be one specifier.  Beside them, 5.1.1p3 was asked of the region alone, so a **static** member's and a friend's declarator each got an object of the class - the decl-specifier-seq is what says, and both oracles refuse the static form.  And older, the other arm of the sentence C9's spelled form leans on: 7.1.6.2p4's unparenthesized class member access names an entity, so `-> decltype(box.slot)` emitted `-> ptr` where the reference and **g++** emit `-> i32`.  What is right and swept clean: 3.3.7p1's region over twelve shapes - a later type-id, a default argument, a trailing-return-type, an inner clause, an unnamed place, 3.3.2p6's class - agreeing with g++ in every one and never escaping the declarator | 304 / 334 -> **310 / 340**, the six new tests being the six shapes these leave and the failing 30 the same 30 by name; pa1-pa18 1778 / 1778; the file audit passes and did not before - `sema_template.cpp` reached **3029 lines against the limit of 3000**, so 8.1p1's reading of a type-id out of a spelling is `sema_type_id.cpp` - and the build prints nothing; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 12 checked-in under `cppgm.tests/course/pa19`; 52 synthesized shapes through this compiler, the `1b135271` pre-audit and `e067d2e9` pre-C9 builds and `reference-binaries/cppgm++` with g++ beside them, 42 of them compared as emitted LowIR and identical to the reference in 41, the survivor a reference member's initializer the pre-audit binary differs on identically; a unit writing all three forms run through `lowir2cy86` to the 42 g++ builds it to return; two units defining one such specialization order-free; seven scaling shapes at n = 32, 128 and 512 against a `1b135271` worktree build, unchanged but for n declarations of one template 0.04 s -> 0.02 s, and the quadratic the fix opens measured to n = 512 where the pre-audit binary segfaults at 8; valgrind clean over all 340 fixtures |
| C10 | the region a qualified declarator-id declares into, the member a member template declares, and the arguments a template-id left out: 9.4.2p1's and 3.4.1p8's class-head-name and declarator-id recording their pattern where the name reaches with 14.1p1's own head standing inside it, 3.4.3p1's prefix walked component by component, 14.5.2's member template given 9.3.1p3's object parameter, 14.8.1p2's partly written argument list made a declaration of its own, and 8.2p7's `T (X)` | `0b3f72b8` | 7 / 7, and six of them are **one shape - the object parameter a member template now carries has readers the checkpoint did not give it, and 14.8.1p2 has a third**: `declares_static_member` asks 13.1's index of the class's chain, which is keyed by the parameter list *as written*, and 14.1p2 gives each head parameters of its own - so 9.4.1p2's `static` was read as unwritten and `template<class T> int counter::raised(T)` over a static member was **`a definition of raised matches no declaration of it`** on a program both oracles compile, while 14.7.1p1's second reading of the same definition could ask neither that index nor 14.5.6.1p5's signature - the region an instantiation stands in binds arguments and not parameters - and emitted `function @counter__raised(%this : ptr, ...)` for a member 9.4p1 calls on no object.  And 14.5.6.2's ordering refused every pair whose `object_member` differed, which before this checkpoint was never two member templates: a static and a non-static member template of one name called through an object is **`a call of r has no best declaration` where the pre-C10 `e7eb1c1a` build, the reference and g++ all choose `r(T *)`** - a regression C10 shipped - and the same guard left 13.5p6's member operator template, which this checkpoint is what put in the candidate set, unordered against the non-member beside it, where 14.5.6.2p2 gives the one non-static member a first parameter of "reference to cv A" and 13.3.1p4 gives the static one an implicit one that matches any object.  Beside them 14.8.1p2's other arm - a trailing argument omitted where it is **obtained from a default template-argument** rather than deduced - was never landed at all, `record_function_template` recording `nullptr` for every default; 3.4.3p1's new prefix walk looked each component up in `named->scope`, which a typedef-name naming a class has none of, and began at `ctx.scope` for a name written `::v::S<T>::f`, both of which `resolve_prefix` already answers; and 14.7.2p1's explicit instantiation matched the written type against the *template's* own, so `template int mix<char>(double);` named no specialization where the arm beside it already deduces with `deduce_target`.  What is right and swept clean: `StandingIn` over eight qualified declarator-id forms and 8.2p7 over twelve places, agreeing with g++ in every one and with the reference wherever it compiles the shape at all | 322 / 346 -> **327 / 351**, the five new tests being five of the shapes these leave and the failing 24 the same 24 by name; pa1-pa18 1778 / 1778; the file audit passes and the build prints nothing, with `sema_analyzer.h` at **2391 against the limit of 2400**; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 27 checked-in under `cppgm.tests/course/pa19`; 96 synthesized shapes through this compiler, the `0b3f72b8` pre-audit and `e7eb1c1a` pre-C10 builds and `reference-binaries/cppgm++` with g++ beside them, 80 compared as emitted LowIR and identical to the reference in all 80, with **no shape in the sweep accepted by both oracles and refused here** and six the reference alone refuses; one unit writing all six rules run through `lowir2cy86` to the 42 g++ builds it to return, and the same unit less the line the reference refuses byte-identical to its LowIR; two units order-free and `object=` for `object=`; nine scaling shapes at n = 32, 128 and 512 against a `0b3f72b8` worktree build, every one where the checkpoint left it within 1% of its memory, and the one the audit makes compilable at 0.63 s at n = 512 against 1.17 s for the same call count the pre-audit binary already compiled and **21.01 s at n = 128** in the reference; valgrind clean over all 351 fixtures |
| C11 | the LowIR an instantiated unit owes and the entries it does not: 14p1's template-declaration declaring no object, 14.7.1p6's startup body kept only where something runs, 12.2p1's temporary a reference binds given storage named after the place that asked, 6.4's condition that is a literal lowered as the jump one of its edges is, 12.1p5's constructor of a subobject that holds nothing naming no address, 12.8p31's returned object taking the constructor the elision left, and 4.10p1's null pointer value | `52c679e1` | 6 / 6, and four of them are **one shape - a rule landed at one reader of a fact and not at the others that have it**.  The fold asked the *operand a lowering wrote* where the reference asks the *expression*: `int(1)`, `(int)1`, `(long)1`, `char(1)`, `(int *)0`, `((void)0, 1)`, `(1, 2)`, `(side(), 1)` and `(p = 1)` are each an expression over a literal the reference leaves the terminator standing for, and all of them folded here - **39 of the 208 swept conditions** - while the block 5.14's right operand is read in was reserved even where the operand before it says which edge is taken, which renumbers every block after it (`if_then_5` against the reference's `if_then_4` over `true &&`, `false &&`, `true ||` and `false ||`).  `folded_edge` asks the node and emits nothing, which is what lets the reservation be asked for after the question; and the same fold's sibling exit is 5.14's operator read as a **value**, which asked the question a third way: the operand that folds leaves the operator standing for its right one - `(true && side()) + (false || side())` was two slots and six blocks against the reference's straight line and `if (true && wrap())` the same around 12.2p3's destructor - while `decided_logical`'s own `fact.constant` let a *pointer* literal decide the value, `int q = pointer() && side();` being `store i32 0` against the reference's `branch nullptr`, so the two paths now ask one question.  And 12.2p1's temporary and 4.10p1's null pointer value were landed in the body and not in the **image**: `const int & r = 5;` wrote `global @r : ptr = 5` - a reference that points at address 5 - where the reference writes `zero` and binds a `tmpref` in the startup body, and `pointer p = pointer();` wrote `= 0` against its `nullptr` with a pointer *subobject* `ptr 0` against its `zero 8`, which the array path beside it already answered.  Beside them 3.6.3p1's registered end of a lifetime dropped 3.6.2p2's entry it stands in - a unit whose one object is an instantiated static member of a class with a destructor emits `__cppgm_fini` and no `__cppgm_init` - and 14p1's pattern was told from 14.5.1.3p1's static data member by the *spelling* rather than the region, so `template<class T> T n::v;` laid out `global @n__v : void`, which is not LowIR any tool below reads.  What is right and swept clean: 12.1p5's empty subobject over an empty member, base, base-of-base, array, vpointer class and 12.8p15's copy of each, with and without a member that unwinds; 12.8p31's returned object over a prvalue, an aggregate, a call, a conditional, two returns and an elided local, the mark taken and cleared by the outermost call so no later construction loses 8.5p7's zero; and `retref`/`refarg`/`tmpref` the reference's names everywhere this milestone binds a value | 343 / 358 -> **348 / 363**, the five new tests being five of the shapes these leave and the failing 15 the same 15 by name; pa1-pa18 1778 / 1778; the file audit passes with its five inherited warnings and the build prints nothing, `sema_analyzer.h` where C10's audit left it at 2391 against 2400; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 44 checked-in under `cppgm.tests/course/pa19`, diagnostics included; 379 synthesized shapes through this compiler, the `52c679e1` pre-audit and `b95b5da0` pre-C11 builds and `reference-binaries/cppgm++` with g++ where a value can be run - 208 condition expressions (52 each in an `if`, a `while`, a `do` and a `for`), 62 further condition and control-flow shapes with `break`, `continue`, `goto`, a condition-declaration, a temporary and the block numbering a second statement shows, 39 further constant kinds and value-context short-circuits, 22 reference bindings, 20 image shapes and 28 over the empty subobject, the returned object, the startup body and 14p1's pattern - **every shape both compilers accept identical as emitted LowIR** but the four the reference is alone on, each recorded; one unit writing the fold rules and the bindings byte-identical to the reference's LowIR and run through `lowir2cy86` to the 39 g++ builds it to return; two units binding a namespace-scope reference and registering an instantiated member's end emitting `__cppgm_init` and `__cppgm_fini` byte-identical to the reference's and order-free; nine scaling shapes at n = 32, 128 and 512 against a `52c679e1` worktree build, every one where the checkpoint left it and linear to n = 2048, with the value-path fold **0.22 s -> 0.17 s and 68.5 -> 51.5 MB** at n = 2048 and the one memory cost 12.8 -> 19.4 MB for the temporaries 12.2p1 says the program has; valgrind clean over all 363 fixtures |
| C12 | 14.7.1p1's point, asked at the naming instead of at the demand: 14.6.2.1p9's nested class of the current instantiation, 3.4.1p8's base-clause region, 7.1.6.3p1's elaborated template argument, 3.4.3.1p2's second arm and 5.3.6's two spellings reviewed beside it | `7afd0f26` | 1 / 1, and it is **the checkpoint's own rule read at the wrong question, wrong in both directions at once**.  14.7.1p1 instantiates a specialization where a completely-defined type is required *and nowhere else*, and the checkpoint landed the opposite default - naming one required its definition unless the walk stood inside a `simple-declaration`'s decl-specifier-seq - so **20 of 30 swept naming spellings read a pattern the standard leaves declared**: 7.1.3p2's alias-declaration, 8.3.5p6's parameter of a function nobody is defining, a member function's parameter, a trailing-return-type, a default argument, a cast's and a `static_cast`'s type-id, a `sizeof` and a `new` over a pointer to one, a condition-declaration, a friend declaration and a member alias-declaration, each accepted by `reference-binaries/cppgm++` and by **g++** and refused here; a *second* naming fell through the mark the first left, so `typedef holder<box> a; typedef holder<box> b;` alone was `factor is written after a name that is not a namespace, class or enumeration`; and the demand it did make was `init_declarator`'s rather than the definition's, so 3.1p2's `extern` declaration and 9.4.2p2's static data member declaration instantiated a class neither defines an object of.  The same boundary left **two of 3.9p5's own contexts with no reader at all**, which the over-instantiation had been masking: 8.3.5p6's definition arm, where `declare_parameters` built the objects a body names out of a class with no layout and emitted `copyobj` with a **zero byte count**; 3.9p5 over an expression, where 3.4.2p2's associated classes, 13.3.1.2p3's member candidates and 12.4p11's destructor are read - `h + 3` over a reference parameter was `an operand of + or - is neither arithmetic nor a pointer`.  And a third question stands beside them - 14.6p8's reading must ask for *nothing*, because a demand answered under `checking_ > 0` reads the pattern in the checking dialect and leaves a class with none of 12.1's members.  So the naming only marks, `require_complete_type` is the one demand and returns on a reading, and the depth and its `ReadingHeld` scaffolding are gone.  What is right and swept clean: 14.6.2.1p9 over a nested class, a nested enumeration, a two-deep nest and a forward-declared one; 3.4.1p8's base-clause; 7.1.6.3p1 over four class-keys, a qualified name, a simple-template-id and 3.3.2p6's declaration at namespace and block scope, refusing a typedef-name and a disagreeing class-key with **g++** where the reference alone accepts both; 3.4.3.1p2 through a typedef-name and through a dependent base; and `__alignof`/`__alignof__` as the operator and as the identifier a member declares | 360 / 369 -> **365 / 374**, the five new tests being five of the shapes these leave and the failing 9 the same 9 by name, four of the five failing against the `7afd0f26` pre-audit build; pa1-pa18 **1778 / 1778**; file audit passes with the five header-weight warnings and the build prints nothing, `sema_analyzer.h` at 2393 against the limit of 2400; every `.ref` regenerates byte-identically over all 319 fixtures under `pa19/tests` and all 50 already checked in under `cppgm.tests/course/pa19`; **47 synthesized shapes through three binaries** - 30 that name a specialization and require no complete type and 17 that do - with `reference-binaries/cppgm++` and **g++** beside them, **46 of 47 identical as emitted LowIR** after the harness's canonicalization, the survivor a base-class copy the pre-audit build emits identically and 12.8p12's group owns, and the pre-audit build refusing 20 of the 30 and 1 of the 17; one unit writing eleven namings over ten of the spellings run through `lowir2cy86` to the **39** g++ builds it to return, which that build refuses outright; two units naming one specialization three ways, order-free and identical to the reference; ten scaling shapes at n = 32, 128, 512 and 2048 against a `7afd0f26` worktree build, every one where the checkpoint left it with the class-typed expression path 0.06 -> **0.05 s** at n = 2048 and peak RSS flat; valgrind clean over all 374 fixtures |
