# PA20 Audit — `cppgm++ --emit-lowir` compile-time metaprogramming

A review of each landed checkpoint, in the order a fact travels: the place a
head declares, the spelling an argument arrives as, the value it converts to,
the specialization it names, and the definition a program wrote for one.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| C1, C2 | `0cda3f77` | 6 / 6 + 1 perf | **the spelling a value argument arrives as, which C1 widened and no reader of one was told.**  Making an argument a *value* let 5.9's `<` and 5.8's `<<` into a name, and all three scans that split a spelling counted every `<` as opening 14.2's list - so `b<(1<2)>::n` found no `::` and `Box<0 < 1, int>` no `,`; 3.4.3p1's rooted name was read by an exit of its own that took no argument list with it; 4.12p1 was missing from the conversion 14.3.2p5 makes the argument a *converted* constant by, so `template<bool>` had one specialization for 3 and another for `true`; and 5.2.3's functional notation and 8.5p16's direct initialization - the other spellings of the cast and the constant object this milestone already folds - folded nowhere |
| C3 | `603dcb83` | 4 / 4 | **the two kinds of settled pack, and the list the object file writes for either.**  A pack is a *run an argument list bound* or *the places one expansion of a function parameter pack declared*, and each of the four readings that meets one knew a different subset: the spelling reading walked the elements of a type that holds none and crashed; one reading of a pattern replaced the pack with an element, so a nested expansion and `sizeof...` inside that pattern found no pack at all; a run of no elements declared no place and so declared nothing, leaving `sizeof...(args)` naming nothing in `f()`; and the ABI, handed the flattened argument list 14.4p1 keys the tier by, wrote `f<int,int>(int,int)` as `_Z1fIiiEiv` - the arguments unpacked and the parameter list *void* - where g++ and the reference both write `_Z1fIJiiEEiDpT_` |
| C4 | `fe28ba9d` | 4 / 4 | **the list a use of a function template is chosen from, and the one the object file writes.**  C4 taught a template-argument-list to end in a run and left every list beside it on the old rule: 14.8.2.2's target type paired a *parameter* list one for one, so `int (*)(int, char)` deduced nothing from `f(Ts...)`; 14.5.6.1p5's signature stood a pack place for the same thing as a single place, so `f(T)` and `f(Ts...)` were one declaration - "defined twice" - and 14.8.2.4p9 had never had to order the two; 14.1p9's default at a *value* place was read where its type-id twin is, and a constant expression is evaluated where it stands rather than substituted afterwards, so `int N = sizeof(T)` and `int B = A + 1` named nothing; and the object file wrote an unsettled non-type argument as a type, `1SIT_E` where g++ and the reference both write `1SIXT_EE` |
| C5 | `818dfd9f` | 2 / 2 | **a pattern this milestone could not read, which the reading dropped and the program was never told about.**  C5 gave an argument list a *second body* it may be read from and left three exits where that body goes unrecorded - a head declaring a template template parameter, a pattern whose reading threw, and a place the pattern does not deduce - each of which leaves the primary's body answering instead, which is a different program read silently: `s<C<T> >` over a template template parameter computed the primary's 0 where g++ and the reference both compute 1, `s<T[N]>` answered 0 for `s<int[3]>` where both compute 3, and `template<class T, class U> struct s<T>` was accepted where both refuse.  And 14.5.1p1's specialization *is* the constant its initializer evaluates to, so nothing is held while that initializer is read: one whose initializer names it ran until the machine stack ran out, where both oracles diagnose it |
| C6 | `d7c036b5` | 4 / 4 | **the demand a prefix makes, which C6 answered at one of the three walks that make it.**  3.4.3p1 looks a name up *in* the region its prefix named, and this compiler writes that walk three times - `resolve_prefix` for a prefix spelled as a name, its own decltype branch, and `qualified_in_type` for a prefix that is a *type*.  C6 taught the first and left the other two on the demand 14.6p8's reading answers with nothing, so `decltype(make_it())::type` written in a template definition reached a class with no region and was refused where both oracles accept, and a decltype prefix an argument list has yet to settle threw `no declaration of decltype(...) is in scope` instead of leaving 14.6.2p1's stand-in every other prefix is left with.  The arena C6 made the analyzer borrow reached one of the three modes that read such a name, so `--emit-types` and `--emit-semantics` refused what `--emit-lowir` accepts.  And 14.6.1p1's current instantiation puts a *place* at every argument, so an out-of-class member definition bound a value place as a type: `template<class T, int N> int holder<T, N>::value = N * 2;` - every out-of-class definition of a member of a class template with a non-type parameter - was refused at 5.1.1p8 |
| C7 | `350c92f4` | 3 / 3 | **a list of *one* entry is a list too, and five readers took that entry by index.**  C7 gave every walk of a written list one reading and converted the readers that walk *many* entries; the readers that take the list's single entry kept indexing `children[0]`, so `int x(a...)`, `int(a...)` and `: v(a...)` over a run of one each reached a `pack-expansion-expression` no reader below answers for and were refused where both oracles accept - while their class-typed twins, which C7 did convert, were right.  8.5p16's arity was never asked either, so `int x(1,2)` was accepted where the reference and g++ both refuse.  And 5.19's own copy of 5.2.3 had no answer at all for a run 14.6p8's reading cannot count, so `int arr[int(N...)]` written in a template definition was refused where `sizeof...(N)` in the same place stands a value in |

## Current Checkpoint Review

C7 gave 14.5.3p4's entry one reading wherever a program writes a list.
`WrittenList` (`sema_pack.h`) is one written list and what its entries come to -
the children as written until an entry is `pattern...`, and from there each
entry paired with the region it is read in - so 8.5.1's clauses, 5.2.2's
arguments, 5.2.3's conversion, 5.3.4's placement and initializer, 8.3.4p3's
deduced bound and 13.3.3.1.5's length ask it once.  `InitializerClauses` carries
that list beside 8.5.1p11's cursor, so an elided subaggregate reads each clause
where the expansion put it; a run no argument list has settled stands as the one
entry it was written as; and 8.5.1p2's array walk became one implementation
driven by that cursor.  That is right where it stands: the node an entry comes
to is the arena's and the region is the model's, so a walk that read them out
owns neither and both outlive it; a list holding no expansion keeps the written
node, pays one node-kind test per entry and allocates nothing, which is every
list PA15-PA19 lower; and `AnalyzedValue::clauses` is written at the one place
an argument is met and read at the one place 13.3 ranks it, so no candidate ever
sees a length the reading did not settle.

What the review found is that C7 converted every reader that walks a list of
*many* entries and left every reader that takes a list's *one* entry by index.
There are five of them, in four rules, and each of them is a list a program can
write `pattern...` into.  Behind the last of those, one more: 5.19's own copy of
5.2.3 had no answer at all for a run 14.6p8's reading cannot count.

### Findings

**1. Five readers took the one entry a list holds by index.**  A list of one
argument is 14.5.3p4's list too - the entry may stand for a run of one, which is
that pattern read in the element's region and not the entry the program wrote.
C7 left every reader of such a list on `children[0]`, so each of them handed a
`pack-expansion-expression` to a walk that has no case for one:

| shape | before | g++ and the reference |
| --- | --- | --- |
| `int x(a...);` - 8.5p16 over a non-class object | `an expression is outside the PA12 subset` | accepted |
| `return int(a...);` - 5.2.3p1 to a non-class type | the same | accepted |
| `holder(A... a) : v(a...) {}` - 12.6.2p7 over a member of non-class type | the same | accepted |
| `const int k(a...);` - 5.19p3's fold of what the declaration leaves | the same | accepted |
| `int arr[int(N...)];` - 5.19's own reading of 5.2.3 | `a constant expression holds a construct PA11 does not evaluate` | accepted |

Each has a class-typed twin - `pair2 p(a...)`, `pair2(a...)`, `: object(a...)` -
which C7 *did* convert, because a class routes through the constructor walk that
already carried `Clauses`.  So one milestone answered one rule two ways
depending on the type of what was initialized.  All five now read the list the
way the walk beside them does.

**2. 8.5p16's arity was never asked of the list at all.**  The same five
readers took the first entry and ignored the rest, so `int x(1,2)` was accepted
here and `x` held `1`, where the reference refuses it (`scalar paren-initializer
requires one expression`) and g++ refuses it too.  Left as it was, the fix for
finding 1 would have made a run of *two* silently take its first element - so
the count the list came to is now what says whether it initializes the object at
all, at every one of the three sites that has an arity to ask.

**3. 14.6p8's reading had no stand-in for a run it cannot count.**
`sizeof...(N)` written where a constant is demanded stands a value in while the
definition is read, and `int(N...)` in the same place did not: the reading
asked the cast for its operands, got an unsettled run, and refused the
definition.  So `template<int... N> struct room { int slots[int(N...)]; };` was
refused where it stands - the template need not even be named - which both
oracles accept.  An unsettled run now stands a 1 in and counts a value stood in
for, exactly as `sizeof...` does, and each argument list that arrives afterwards
is read for itself.

### What the review confirmed rather than found

The typed ownership holds.  `WrittenList` holds a `const AstNode*` it does not
own and a `Scope*` the model owns, and `element_region` opens into
`SemaModel::open` - so the region a walk reads an entry in outlives the walk,
which is what lets `fold_constant_object` keep the clause and its region after
the list is gone.  `SemaModel::open` writes no dump node, so a list read twice
adds nothing to `--emit-semantics`.  `AnalyzedValue::clauses` has exactly one
writer (`argument_expression`) and one reader (`match_argument`).

The complexity is what the plan claims and the fixes cost nothing measurable.  A
list holding no expansion is one node-kind test per entry and no allocation: an
expansion in an array's clause list, an aggregate's clause list and a
constructor's argument list at 1 / 64 / 512 / 2048 entries are 0.004 / 0.005 /
0.015 / 0.051, 0.004 / 0.005 / 0.017 / 0.058 and 0.004 / 0.006 / 0.018 /
0.061 s - linear, and identical to the `350c92f4` build row for row.  The
readers this review changed are linear in their own multiplicity: 256 / 1024 /
4096 scalar paren declarations are 0.014 / 0.045 / 0.177 s, functional casts
0.009 / 0.026 / 0.094 s, mem-initializers of a non-class member 0.017 / 0.058 /
0.226 s and constant casts over a settled run 0.015 / 0.052 / 0.195 s.  5.19p3's
fold reads the list only where a const arithmetic object asks, so 2048 ordinary
declarations carrying a braced clause are 0.078 s against `350c92f4`'s 0.077 s,
and `fac<800>` and 14.5.3p4's quadratic recursion over a pack of 1024 are
0.037 / 1.560 s against 0.035 / 1.560 s there - measured interleaved, best of
five, three rounds.

Valgrind is clean - no message of any kind - over the nine shapes the findings
are about and the three scaling shapes.

The differential sweep is 88 shapes through this compiler, through
`reference-binaries/cppgm++` and through g++, compared on the LowIR the first
two wrote rather than on the exit status alone.  62 of them are the cross
product of every list 14.5.3p4 admits an expansion into - a call's arguments
with the expansion first, last and alone, a functional cast to a class and to a
scalar, a braced list over a class, an aggregate and a scalar, an array's
deduced bound, a paren and a braced declaration, a mem-initializer of a base, of
a class member, of a scalar member and of a braced member, a new-expression's
placement list and its initializer, an elided and a written subaggregate, a
braced argument, an assignment from a list, and a constant cast - each over a
run of none, one and two elements.  Every one agrees with both oracles, and
every accepted pair writes byte-identical LowIR.  The other 26 are the finding
shapes and their twins, at the two tiers a pack can be declared at and inside
and outside 14.6p8's reading; the two disagreements are the reference's own
gap, recorded below.

### Recorded, not landed

- **12.1's constructor over a function parameter pack of *no* elements is not a
  candidate.**  `template<class... A> struct s { s(A... a); };` and then `s<>`
  reports `no declaration of s accepts the arguments of a call`, where both
  oracles build it; `s(int k, A... a)` over an empty run fails the same way at
  arity one, so it is the clause and not the arity.  The same head's member
  *function* over the same empty run is right (`s<>::f()` emits `(%this)`), and
  the same constructor over a run of one or two is right - so this is 8.3.5p10's
  parameter clause and not 14.5.3p4's list, which is why it is recorded here
  rather than fixed with the readers above.
- **The reference refuses a braced scalar initializer holding an expansion.**
  `int x{a...}` and `const int x = {a...}` are `unsupported expression in PA12
  first slice [kind pack-expansion-expression]` there; this compiler and g++
  both accept, and the parenthesised spellings of the same two are accepted by
  all three.
- **A const object folded from parentheses is not an array bound inside a
  template definition.**  `const int b(N...); int r[b];` is refused here and by
  the reference and accepted by g++; the cast spelling `const int b = int(N...)`
  is accepted by all three.
- **PA20's own recorded items are unchanged**: a specialization's body cannot
  name its own class, a partial specialization has no out-of-class members, a
  template template parameter in any head, a dependent array bound in an
  argument spelling, a constructor template written in a class body, the
  reference's empty-pack function-template name, 14.8.1p9's extension of an
  explicit list, `Tn` for a settled value argument of dependent type,
  `sizeof...` inside an argument spelling, the generated place name that
  collides with a written one, a pack name written without `...`, 10p1 over a
  base pack of more than one element, and the static data member's demand.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double, the out-of-class member path's
  residual, 12.1's two constructor entry points, and the ABI's decltype return
  type.  A *class* metafunction with no terminating specialization still
  overflows the machine stack rather than being diagnosed, in this compiler and
  in the reference alike; it needs a depth guard rather than C5's same-list one.

## Changes

- **`sema_analyzer.cpp` — 8.5p16 over a non-class object, and 5.19p3's fold.**
  `write_initializer`'s parenthesised arm reads its list through `Clauses`, so
  the expression it initializes from is the one the list came to, read in that
  entry's region, and a list that came to more than one initializes no such
  object at all.  5.19p3's fold moved to `fold_constant_object`, which asks the
  same question of the same list - and asks it only where a const object of
  arithmetic type is being declared, so a declaration that folds nothing pays
  nothing for the clauses it wrote.
- **`sema_overload.cpp` — 5.2.3p1 to a non-class type.**  `functional_cast`
  already counted its arguments through `WrittenList`; it now *reads* them
  through the same one, so the operand of a cast over a run of one is the
  pattern in that element's region rather than the entry the program wrote, and
  the one clause 5.2.3p3 puts where the operand stands is found the same way.
- **`sema_lifetime.cpp` — 12.6.2p7 over a member of non-class type.**  The
  mem-initializer's list is read through `Clauses`, so a run of none is 8.5p10's
  value-initialization, a run of one is that one expression, and a run of more
  is the same refusal a written list of more already got.
- **`sema_constant.cpp` — 5.19's own reading of 5.2.3.**  The evaluator reads
  the cast's list through `Clauses` as the lowering does, and 14.6p8's
  unsettled run stands a value in and is counted, exactly as `sizeof...(N)` in
  the same place already was.
- **Two fixtures** under `cppgm.tests/course/pa20`, each with a `.ref`
  generated from `reference-binaries/cppgm++` and each accepted by g++ and
  refused by the `350c92f4` build: the one clause a list came to, at all four
  readers of one and over runs of none and one beside their class-typed twins;
  and a cast a definition cannot count, in a template that is named and in one
  that never is.

## Performance Evidence

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row is the shape's own cost.  Every row
was measured against this build, and the rows marked \* were measured
interleaved against a `make build` of `350c92f4` on the same machine.

| shape | here | `350c92f4` |
| --- | --- | --- |
| \* an expansion of 1 / 64 / 512 / 2048 in an array's clause list | 0.004 / 0.005 / 0.015 / **0.051 s** | 0.051 s at 2048 |
| \* the same in an aggregate's clause list | 0.004 / 0.005 / 0.017 / **0.058 s** | 0.059 s at 2048 |
| \* the same as a constructor's argument list | 0.004 / 0.006 / 0.018 / **0.061 s** | 0.061 s at 2048 |
| 256 / 1024 / 4096 scalar paren declarations over a run of one | 0.014 / 0.045 / **0.177 s** | - |
| 256 / 1024 / 4096 functional casts over a run of one | 0.009 / 0.026 / **0.094 s** | - |
| 256 / 1024 / 4096 mem-initializers of a non-class member | 0.017 / 0.058 / **0.226 s** | - |
| 256 / 1024 / 4096 constant casts over a settled run | 0.015 / 0.052 / **0.195 s** | - |
| \* 2048 ordinary declarations carrying a braced clause | **0.078 s** | 0.077 s |
| \* `fac<800>` metafunction chain | **0.037 s** | 0.035 s |
| \* 14.5.3p4's recursion over a pack of 1024 | **1.560 s** | 1.560 s |
| \* a pack of 4096 elements bound and counted | **0.012 s** | 0.012 s |
| a cast nested 24 deep over a run of one | **0.004 s** | - |

Every changed reader is linear in its own multiplicity - 4x the sites is 3.5 to
4x the time at each of the four - and the list rows are linear in the entries
and unmoved from the build that did not read them, which is what says the
readings this review added are one per list met and not one per candidate or
one per element scanned twice.  The quadratic pack recursion was measured
interleaved over three rounds and is the same to within 0.7 %.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **164 / 186**, from a
  turn-start **162 / 184** - the two added here pass and the 22 failing at turn
  start are the same 22, name for name.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
  `declare_object_declarator` had reached 241 of the audit's 240 function lines,
  which is what 5.19p3's own owner freed.
- **Valgrind clean** over the nine finding shapes and the three scaling shapes.
- Every `.ref` under `cppgm.tests/course/pa20` was regenerated from
  `reference-binaries/cppgm++`; the twenty that were already there are
  byte-identical.
- Both added fixtures are refused by a `make build` of `350c92f4` and accepted
  by g++, so each is a test of this review rather than of its own output.
