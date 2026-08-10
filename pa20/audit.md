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

## Current Checkpoint Review

C5 gave 14.5.5's pattern and 14.5.1p1's variable template one owner
(`sema_specialize.h`), and the reading is right where it reads.  A partial
specialization is a template *beside* the primary rather than a second
declaration of it: its head declares places nothing else declares and its
declarator-id writes an argument pattern over those, so what it adds is a second
body one argument list may be read from.  14.5.5.1p1 is which, and it is
`match_arguments` - the same list-against-list reading 14.8.2.5p4 already was -
memoised on the template under the interned list a naming already holds and
dropped whole where a later declaration adds a pattern that list never saw.
14.5.5.2p1's ordering is that match run between two patterns.  A variable
template is the same three steps over an object, and what one list makes of it is
the constant its initializer evaluated to and no object at all.  Both were traced
end to end and both hold: 14.7.3's `template<>`, 9.4.2p1's qualified
declarator-id and 14.5.6.1p5's signature each answer once for both tiers.

What the review found is that both halves left a program's meaning resting on a
reading that could quietly not happen.  A pattern C5 could not read was dropped
and the *primary* answered for every list it would have taken; and a variable
template's specialization is not held while its own initializer is read, so an
initializer that names it had nothing to stop it.

### Findings

**1. A pattern this milestone could not read left the primary answering.**  The
recording has three exits that leave a partial specialization unrecorded - a head
`read_template_head` gives no meaning to, a pattern whose reading threw, and (at
each naming) a place the match left unbound - and each of them returned as though
the program had not written the declaration.  It had: 14.5.5p1 makes the pattern
a *second body*, so the list it would have been read from is read from the
primary's instead, which is a different program with no diagnostic anywhere:

| shape | before | g++ and the reference |
| --- | --- | --- |
| `template<template<class> class C, class T> struct s<C<T> >` | `s<vec<int> >::w` is the primary's 0 | 1 |
| `template<class T, int N> struct s<T[N]>` | `s<int[3]>::w` is the primary's 0 | 3 |
| `template<class T, class U> struct s<T>` | accepted, primary answers | both refuse |
| `template<class T, class U> struct s<T*, int>` | accepted, primary answers | g++ refuses |

The second row is the shape the plan had already recorded as *"the pattern is
left unrecorded, so the primary answers"* - written down as a gap and not as the
wrong answer it was.  The rule is now the one the primary tier has always had:
what cannot be read is what cannot be instantiated.  A declaration whose pattern
could not be read leaves the template `supported` false, so every argument list
of it is refused at 14.3p1's gate - the one gate every naming already passes -
and 14.5.5p8.3's undeducible place is refused at the list that matched it.  A
template *may* still be declared and never named, which is what 14p1 allows and
what the primary tier already does with a head it gives no meaning to.

**2. A variable template's initializer could name what it was the initializer
of.**  A class specialization is held before its body is read, so a naming inside
that body finds the declaration already made; 14.5.1p1's specialization *is* the
constant its initializer evaluates to, so there is nothing to hold until the
reading is over and the second naming starts the same reading again:

```cpp
template<int N> constexpr int circular = circular<N> + settled<N>;
int main() { return circular<1>; }              // SIGSEGV
```

Both oracles diagnose it.  The reading now holds the one thing it has - the
argument list it is reading - so a naming reached from inside it is the circle
5.19p2 makes ill-formed rather than a second reading.  It is a depth, so the two
mutually recursive variable templates are caught by the same rule.

### What the review confirmed rather than found

The typed ownership holds.  `Specialization` reads syntax, `TypeId`s and the
analyzer's own readers, and every entry point ends at a `TemplateInfo` field or a
`SemaEntity`; `sema_template.cpp` kept no second copy of any of it, and the two
readers of `partials` - `complete_specialization` for a class body and
`variable_reading` for an initializer - are the only two bodies an argument list
is chosen between.  `template_patterns_` is a deque, so the `TemplateInfo*` each
`Partial` holds stays put while the next head is read.

The complexity is what the plan claims.  14.5.5.1p1's choice is one match per
pattern per *distinct* argument list, memoised on the template, so 256 patterns
against 2048 lists cost 0.277 s against the reference's 22.1 s; a template no head
partially specialized pays one test of an empty vector, which is why no row above
the new ones moved.  14.5.5.2p1's ordering is quadratic in the patterns that
*match* - 64 nested-pointer patterns all matching one list is 4096 comparisons and
0.008 s.  The reading guard added this turn is a scan of the reading's own depth:
a variable-template chain 3000 and 6000 deep costs 0.031 s and 0.070 s, so the
scan is not what the shape costs.  The one shape that is quadratic in its input is
14.5.3p4's recursion over a pack - `lst<H, T...>` over 1024 elements walks
argument lists whose lengths sum to n^2/2 - and it is the shape's own cost rather
than the choice's: ours is 1.573 s where the reference is 10.120 s and g++ 0.210 s.

Valgrind is clean - no message of any kind - over the ten shapes the findings are
about and the four scaling shapes.  A two-unit `--emit-lowir` run over a header
holding a partial specialization and a variable template writes the same
object-file names as the reference, so nothing here is settled per unit.

The differential sweep is 68 shapes through this compiler, through
`reference-binaries/cppgm++` and through g++, compared on the LowIR the first two
wrote rather than on the exit status alone: patterns over a pointer, a reference,
a specialization, an array, a value, a pack, an empty run and a nested pattern;
ordering at two, three and four patterns, ambiguous and unordered pairs included;
a pattern redeclared, defined twice, forward-declared and defined after; a
partial specialization's members, bases, constructors, destructors and virtual
functions; variable templates read from a primary, a pattern and a `template<>`,
named in a class body, a function template, an array bound, a `static_assert`, a
template argument and a second unit.  Every shape both compilers accept writes
the same LowIR, save two: `unwind=no`, which the comparison canonicalizes, and
the ill-formed-no-diagnostic-required program below that writes a pattern after
the list it would have taken.  Ten shapes are refused here and accepted by both
oracles, and every one of the ten is a gap recorded below rather than a
disagreement about a rule.

### Recorded, not landed

- **A specialization's body cannot name its own class.**  `typedef s self;`
  inside `template<class T> struct s<T*>` finds the *primary*, and `s<T*>` written
  there is read as 12.1p1's constructor name and does not parse.  This is not
  C5's: `template<> struct s<int> { typedef s self; };` has answered the same way
  since C2, because a specialization's class-head-name is a template-id and the
  injected-class-name declared from it is that whole spelling.  Both oracles
  accept all four shapes; this compiler refuses each of them.
- **A partial specialization has no out-of-class members.**
  `template<class T> int s<T*>::f() { return 1; }` is refused where both oracles
  accept it, because `member_definition_owner` reaches the primary's members and
  not a pattern's.
- **What this milestone will not read is now loud rather than wrong.**  A
  template template parameter in any head, a dependent array bound in an argument
  *spelling* (`s<T[N]>`, which `s<Arr>` over a typedef still reads), and a
  pattern naming a template no declaration wrote are each refused; g++ refuses
  the last of the three too.
- **A partial specialization written after a list was already answered.**  The
  reference reads the list again and this compiler keeps the answer the primary
  gave; g++ refuses the program outright, so no valid program tells them apart.
- **The reference accepts a pattern whose head declares a place it does not
  deduce** (14.5.5p8.3); g++ refuses it and so, now, does this compiler.
- **PA20's own recorded items are unchanged**: the reference's empty-pack
  function-template name, 14.8.1p9's extension of an explicit list, `Tn` for a
  settled value argument of dependent type, `sizeof...` inside an argument
  spelling, the generated place name that collides with a written one, a pack
  name written without `...`, 10p1 over a base pack of more than one element, and
  the static data member's demand.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double - 0.912 s here against the reference's
  2.277 s at 2^20 leaves - the out-of-class member path's residual, 12.1's two
  constructor entry points, and the ABI's decltype return type.  A *class*
  metafunction with no terminating specialization still overflows the machine
  stack rather than being diagnosed, in this compiler and in the reference alike;
  it needs a depth guard rather than this turn's same-list one.

## Changes

- **`sema_specialize.cpp`, `sema_template.h` — what cannot be read cannot be
  instantiated.**  `record` leaves the primary `supported` false where
  `read_pattern` returned nothing, so 14.3p1's gate refuses every argument list
  of a template one of whose second bodies is unknown, rather than each of those
  lists being answered from the primary.
- **`sema_specialize.cpp` — 14.5.5p8.3 at the list that matched.**  A place the
  match left unbound is a declaration no list can be read against, so `matches`
  says so instead of reporting that the pattern did not match.
- **`sema_specialize.cpp`, `sema_template.h` — one reading per argument list.**
  `TemplateInfo::reading` holds the lists a variable template is being read for,
  so 5.19p2's circle is diagnosed where a class's is stopped by the declaration
  being held first.
- **Two fixtures** under `cppgm.tests/course/pa20`, each with a `.ref` generated
  from `reference-binaries/cppgm++` and each refused by g++.  The pattern one is
  accepted by the `818dfd9f` build and refused now; the variable-template one
  pins the shape rather than the fix, because the harness records that build's
  SIGSEGV as `EXIT_FAILURE` too.

## Performance Evidence

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.004 s**, so a row is the shape's own cost.  Every row was
regenerated and re-measured against this build; none is carried forward.

| shape | here | `reference-binaries/cppgm++` |
| --- | --- | --- |
| 64 / 128 / 256 patterns against 512 / 1024 / 2048 distinct lists | 0.055 / 0.122 / **0.277 s** | 22.1 s at 256 |
| 64 nested-pointer patterns all matching one list, ordered pairwise | **0.008 s** | 0.499 s |
| 512 / 2048 distinct variable-template specializations | 0.015 / **0.051 s** | 0.230 s at 2048 |
| a variable-template chain 800 / 3000 / 6000 deep | 0.010 / 0.031 / **0.070 s** | - |
| 14.5.3p4's recursion over a pack of 256 / 512 / 1024 | 0.108 / 0.399 / **1.573 s** | 10.120 s at 1024 |
| a pack of 4096 elements: bound, expanded into a base, counted | **0.189 s** | 0.609 s |
| the doubling spelling at 2^20 leaves | **0.912 s** | 2.277 s |

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **147 / 180**, from a
  turn-start **145 / 178** - the two added here pass and the 33 failing at turn
  start are the same 33, name for name.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over the ten finding shapes and the four scaling shapes.
- Every `.ref` under `cppgm.tests/course/pa20` was regenerated from
  `reference-binaries/cppgm++`; the fourteen that were already there are
  byte-identical.
- The three silent exits were instrumented and run over every `.t` in
  `pa20/tests`, `cppgm.tests/course/pa20` and the pa17-pa19 suites: none of them
  fires on any checked-in fixture, which is why the wrong answers they wrote were
  invisible and why refusing them regresses nothing.
