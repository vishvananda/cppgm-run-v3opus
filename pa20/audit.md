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

## Current Checkpoint Review

C4 gave 14.8.2 an owner of its own.  The reading is right and was traced end to
end: a use of a function template names it without its arguments, so what makes
a specialization is a *match* of a parameter type P beside the type A of what
the use put there; the two uses that write those pairs - 14.8.2.1's call and
14.8.2.2's target type - meet at 14.8.2p5, which is the one place that turns a
map of places into the flat argument list 14.4p1 keys the tier by.  The four
things the checkpoint added are each one rule: a specialization P is an argument
*list* against another, 14.8.2.1p3's A may be a class derived from what P names,
14.8.2.5p5's nested-name-specifier deduces nothing and is settled by the
substitution instead, and 14.1p9's default fills a place the pairs did not
reach.  The match walks one P and one A and never rescans the candidate set.

What the review found is that C4 wrote its new rule - *a list may end in a run* -
for one list and left every list beside it on the old one, and that the object
file still writes a non-type argument no substitution has settled as a type.

### Findings

**1. A target type could not deduce a run.**  14.8.2.2's A is one whole function
type, so the parameter list inside it is the list the match reads - and `match`
paired that list one entry for one entry, which is exactly the rule
`match_arguments` had just stopped using for a template-argument-list:

```cpp
template<class... Ts> int count(Ts... args) { return (int)sizeof...(args); }
int main() { int (*two)(int, char) = count; return two(1, 'a') - 2; }
```

It is one rule and now has one implementation: 8.3.5p1's parameter list is
matched by `match_arguments`, so a trailing `Ts...` stands for every parameter
the entries before it did not take, at a run of none as much as at a run of two.

**2. A default at a value place could not name the places before it.**  C4 made
14.1p9's default at a value place an expression rather than a type-id and read
it where its twin is read - the head's own region, with what the deduction
settled substituted into the answer.  A type-id survives that, because
substituting into the type it names is the same answer; 5.19's constant
expression does not, because it is *evaluated* where it stands and a place that
is not a constant there names nothing:

```cpp
template<class T, int N = (int)sizeof(T)> int width(T) { return N; }
template<int A, int B = A + 1> int next() { return B; }
```

The first said `sizeof` names an incomplete type and the second that `A` is not
a constant expression.  The class tier already opens a region binding each
earlier place to what the list gave it; the function tier now opens the same
one, so the two tiers read 14.1p9 the same way.

**3. A head that declares a pack and one that does not were one template.**
14.5.6.1p5 asks whether two heads declared their parameters in the same places,
and the signature stood each place for its position alone - so a place that
binds a *run* stood for what a place that binds one argument stands for, an
expansion over it collapsed to the place itself, and `int(#0)` was the signature
of both:

```cpp
template<class T> int pick(T) { return 1; }
template<class... Ts> int pick(Ts...) { return 2; }   // pick is defined twice
```

A pack place now stands for a canonical place of its own.  That makes them two
templates, which 14.5.6.2 then has to order, and the ordering had never been
asked: 14.8.2.4p9 makes a run the other head wrote no argument for a place this
one wrote singly, and a trailing `P...` in the head being deduced stands for
every place the ones before it did not take - each of those a pair over bindings
of its own.  `pick(1)` is now the first one's and `pick(1, 2)` the second's, and
`span(T, U)` beats `span(Ts...)` at two arguments, as in g++ and the reference.

**4. The object file wrote an unsettled non-type argument as a type.**  14.1p4
leaves what an argument *is* a fact of the place it fills, and the ABI writes a
value place's argument as `X <expression> E` - the `Dp` an expansion of a type
is written with is `sp` inside that `X`.  `argument_of` asked only whether the
argument was a settled value or a settled run and wrote everything else as a
type, so every signature holding an unsettled non-type argument was misnamed:

| shape | before | after, g++ and the reference |
| --- | --- | --- |
| `take(S<N>)` over `int N` | `1SIT_E` | `1SIXT_EE` |
| `take(S<Ns...>)` over `int... Ns` | `1SIJDpT_EE` | `1SIJXspT_EEE` |
| `take(S<T, Vs...>)` over `T... Vs` | `1SIT_JDpT0_EE` | `1SIT_JXspT0_EEE` |
| `take(S<N>, S<N>)` | `...ES2_` | `...ES1_` |

The last row is the same fact seen from the compression: a `<template-param>`
written as a type is a substitution candidate and the expression the value place
writes is not, so a signature holding one numbers its substitutions one lower.
`object=` is stripped before the LowIR comparison, so no fixture in either suite
could have seen any of this.

### What the review confirmed rather than found

The typed ownership holds.  `Deduction` reads types and `AnalyzedValue`s and
nothing else, every entry point ends at `arguments_of`, and `sema_template.cpp`
lost 437 lines to it without keeping a second copy of any of them.
14.8.2.1p3's derived-class deduction was swept at each of its exits - by value,
through a reference, through a pointer, down a two-link base chain, and *below*
the top of a pair the use wrote, where it must not apply - and each answers as
g++ does.  So were the non-deduced contexts: a member behind a dependent prefix
paired with another argument that deduces the parameter, paired with nothing
that does, behind a prefix a base declares, and behind one the class turns out
not to declare, which is refused rather than quietly accepted.

The complexity is what the plan claims, and each of the three new paths is
linear in what it walks.  Best of seven, `-O0`, measured with the shell's own
timer against a **0.003 s** process floor: a target type deducing a run of 256 /
1024 / 4096 places costs 0.006 / 0.015 / 0.048 s where the reference takes
1.01 s at 4096; 200 / 800 calls each ordering a pack head against a non-pack one
cost 0.022 / 0.084 s against the reference's 0.71 s at 800; and 400 / 1600 /
3200 calls each reading a value default that names an earlier place cost 0.033 /
0.133 / 0.276 s against the reference's 1.02 s at 3200.  The region a default is
read in is opened once per deduction that reaches one, which is what keeps that
row linear.  Nothing else in the model moved: a pack of 4096 elements is 0.021 s
and a call forwarding 1024 places 0.024 s, as before.

Valgrind is clean - no error of any kind - over the twelve shapes the findings
are about and over the four scaling shapes.  A two-unit `--emit-lowir` run over
a header holding all four fixed shapes writes the same five object-file names as
g++ and the reference, so nothing here is settled per unit.

The differential sweep is 71 shapes through this compiler, through
`reference-binaries/cppgm++` and through g++: explicit argument lists that stop
at, enter, and skip a pack place; derived-class deduction at each of its five
exits; five non-deduced contexts; a specialization P as a type run, a value run,
a leading fixed place, a derived class, an inconsistent pair, a consistent pair
and an empty run; value places in the object file, alone, repeated, expanded and
beside a type; target types, member templates, class-tier and function-tier
defaults, a pack that is not last; partial ordering at four arities; four
redeclaration shapes; parameter lists holding a run behind a function pointer and
a pointer to member; and the two-unit run.  Every one agrees with g++.

### Recorded, not landed

- **14.8.1p9's extension of an explicit list is not read.**  A list that
  *enters* the pack place - `count<int>(1, 2)` - should leave the pack open for
  the call to extend, and both this compiler and the reference refuse it where
  g++ deduces `Ts = <int, int>`.  A list that stops *at* the pack place is what
  C4 fixed and what every fixture in either suite writes.
- **The reference writes a settled value argument of dependent type with `Tn`.**
  `take<int, 1, 2>` over `template<class T, T... Vs>` is `TnT_Li1E` there and
  `Li1E` here and in g++; the two disagree and g++ decides it.
- **PA20's own recorded items are unchanged**: the reference's empty-pack
  *function template* name (`_Z4take1SIJEE` there, `_Z4takeIJEEi...` here and in
  g++), `sizeof...` inside an argument *spelling*, the generated place name that
  collides with a written one, a pack name written without `...`, 10p1 over a
  base pack of more than one element, and the static data member's demand.  A
  head that declares a *value* place is still left declaring a template of its
  own by 14.5.6.1p5's signature, which no valid program in either suite tells
  apart from the merge.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double, the out-of-class member path's
  residual, 12.1's two constructor entry points, and the ABI's decltype return
  type.  A metafunction with no terminating specialization still overflows the
  machine stack rather than being diagnosed.

## Changes

- **`sema_deduce.cpp` — one rule for a list of entries.**  `match`'s function
  type hands its parameter list to `match_arguments`, so 8.3.5p1's list and
  14.2's are read by the one clause that knows about a trailing run.
- **`sema_deduce.cpp` — 14.1p9's default is read where its places are bound.**
  `arguments_of` opens a region of its own and `bind_argument`s every place
  before the one it is filling, so a value default reaches them as constants and
  a type default as typedef-names.
- **`sema_template.cpp`, `sema_analyzer.h` — a pack place has a canonical place
  of its own.**  `canonical_parameter` takes whether the place binds a run, so
  14.5.6.1p5's signature tells `f(T)` from `f(Ts...)`.
- **`sema_overload.cpp` — 14.8.2.4p9 at the ordering.**  A trailing `P...` in
  the head being deduced stands for every place the ones before it did not take,
  each of those a pair over bindings of its own; a run the other head wrote is
  no argument for a place this one wrote singly.
- **`lowir_abi.cpp` — 14.1p4 in the object file.**  `written_as_expression` asks
  whether an argument stands at a value place, and `expression_of` writes it as
  the `<template-param>` it names or as `sp` of one, under `X ... E`.
- **Three fixtures** under `cppgm.tests/course/pa20`, one per finding a fixture
  can pin - the fourth is `object=`, which the comparison strips - each refused
  by the `fe28ba9d` build, each with a `.ref` generated from
  `reference-binaries/cppgm++`, and each returning 0 under g++.

## Performance Evidence

Best of seven, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row is the shape's own cost.  An earlier
turn's harness spawned two processes of its own per run and read that as a
0.11 s floor; it is not one.

| shape | here | `reference-binaries/cppgm++` |
| --- | --- | --- |
| a target type deducing a run of 256 / 1024 / 4096 places | 0.006 / 0.015 / **0.048 s** | 1.01 s at 4096 |
| 200 / 800 calls ordering a pack head against a non-pack one | 0.022 / **0.084 s** | 0.71 s at 800 |
| 400 / 1600 / 3200 calls reading a value default that names an earlier place | 0.033 / 0.133 / **0.276 s** | 1.02 s at 3200 |
| a pack of 512 / 4096 elements: bound, expanded into a base, counted | 0.006 / **0.021 s** | 0.31 s at 4096 |
| a call forwarding a parameter pack of 1024 places | **0.024 s** | - |
| 300 calls deducing a run from a specialization argument | **0.017 s** | - |

Every row is linear in what it walks.  Every row of the plan's model was
re-measured against the `fe28ba9d` build side by side and none moved by more
than run-to-run noise - the exponential doubling spelling included, at 0.510 s
here and 0.499 s there.  The value-default row has no baseline: that shape did
not compile before this turn.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **136 / 175**, from a
  turn-start **133 / 172** - the three added here pass and the 39 failing at
  turn start are the same 39, name for name.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over the twelve finding shapes and the four scaling shapes.
- Every `.ref` under `cppgm.tests/course/pa20` was regenerated from
  `reference-binaries/cppgm++`; the eight that were already there are unchanged.
