# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower.

## Current Checkpoint Review

**C3, reviewed at `e67acde3`** — the call a template joins: 14.8.2.5p3's
parameter written over no template parameter, 14.8.2.1p4's reference top,
8.3.6p1's unwritten trailing arguments, 13.3.1.2p4's first operand, 14.8.2.2's
target type, 14.8.2.1p6's overload set, 14.5.6.2's ordering of two templates
whose conversions tie, 14.5.6.1p5's equivalent declarations, and 8.5.1/8.5.4
split into `sema_init_list.cpp`.

The increment computes the right thing almost everywhere. The split is code
motion and nothing else - the 763 lines that left `sema_overload.cpp` are the
783 that arrived, line for line, apart from the new file's includes and Table
12's ranks moved beside the `OverloadMatch` every walk fills. 14.8.2.5p3's
`continue` over a parameter written over no template parameter is the right
half of the split between substitution and conversion; `relaxed` is passed at
the top of a reference parameter and at no recursion below it, which is exactly
where 14.8.2.1p4 puts it; `deduce_overload_set` copies the bindings per
declaration tried so a set two members deduce leaves the parameter untouched;
and the ordering memo is keyed by the pair of declarations, which is what makes
n call sites over two tied templates cost what one does. The pa1-pa18 baseline
is untouched by all of it.

**The blockers are on the paths the increment did not join up.** 14.5.6.2's
ordering landed at one of its two readers; the deduction landed p2 and p4 of
14.8.2.1 and not p3; the ordering itself stops at p7 and leaves out the p9 that
is the whole point of p5 and p7 having stripped anything. And 14.5.6.1p5's new
answer - that two declarations of one name declare one template - made a
program shape reachable that the tier had no point of instantiation for.

### Findings

**1. 13.4p1's target type chose by declaration order.** `resolve_target` kept
the first template in the set whose deduction made a specialization of exactly
the target type and walked on without asking about the rest, so
`int (*p)(int *) = pick;` over `pick(T)` and `pick(T *)` took `pick(T)`. That
is the same tie 13.3.3p1 sends to 14.5.6.2 when a call leaves it, answered here
by the order the declarations were written in - and `resolve_target` is the one
place four contexts ask through, so an initializer, an assignment, a parameter
and a functional cast were all wrong together. Both oracles choose `pick(T *)`
in every one of them. `more_specialized` is now the single question 13.3.3p1's
tie and 13.4p1's target both ask, and a set that leaves two of which neither is
more specialized names none, which is what g++ does and what `resolve_target`'s
callers already treat as no declaration at all.

**2. 14.5.6.2 stopped at p7, which is where its answer starts.** p5 replaces a
reference by what it refers to and p7 drops the top-level qualifiers, so
`f(T &)` and `f(const T &)` each deduce the other and neither is more
specialized - and a call passing a `const int` lvalue, whose conversions tie
because both parameters are `const int &`, was **ill-formed**. p9 is what those
two clauses took off: at a place both templates wrote a reference, the more
qualified one and the lvalue one are the more specialized, and p10 leaves
neither where two places answer differently. Both oracles choose
`f(const T &)`.

**3. 14.8.2.1p3 is the sibling of the p2 and p4 that landed.** An rvalue
reference written over a parameter nothing qualified deduces from "lvalue
reference to A" wherever the argument is an lvalue, and 8.3.2p6's collapsing is
what then lets the parameter bind it. Without it `f(T &&)` deduced `T = int`,
made a parameter no lvalue can bind, and dropped out of the candidate set - so
`f(T &&)` against `f(const T &)` chose the second where both oracles choose the
first. The type table already collapsed the two references; nothing built the
argument that asks it to.

**4. That deduction makes `T` a reference type, and 5.3.3p2 measures one of
those as the type it refers to.** `sizeof(int &)` and `alignof(int &)` over a
type-id answered 8 and 8 where both oracles answer 4 and 4, so `sizeof(T)` in
the body of a template with a forwarding reference was wrong as soon as finding
3 made that shape reachable. A *name* of reference type was already right,
because 5p5 reads the reference away before the operand has a type at all -
which is why no fixture and no earlier assignment ever saw it.

**5. 14.6.4.1p1's second point of instantiation was nobody's.** A specialization
named where its template had no definition yet left a pending entry that wrote a
`declare function` and stopped, and nothing went back when the definition
arrived. 14.5.6.1p5 is what makes this reachable at all - before C3 the
declaration and the definition were two templates - so a call, a target type and
a member of a class template each named above the definition all emitted a
symbol **no unit in the program defines**, and the link fails. The suite could
not see it: it compares LowIR and never links. The pending entry is now settled
where the walk reaches it, at the end of the unit, where the definition the
template has by then is the one the specialization stands for.

**And one avoidable walk that is also an avoidable graph.**
14.5.6.1p5's question was asked of every *pair* of declarations of one template
name, each pair substituting one head's parameters for the other's - so
declaring the nth overload read the n - 1 before it, and where a parameter was
written over a class template each of those substitutions **instantiated a
specialization** to compare. 512 declarations of one name cost 0.36 s against
0.06 s for the same 512 written without `template`, and 512 over `A<T>` held
44.8 MB. Whether two declarations declare one template is a fact of each
declaration on its own - its type with each parameter standing for the *place*
its head declared it in - so it is computed once and the walk compares types:
0.04 s and 15.8 MB, which is what the ordinary path costs.

### Changes

| what | where |
| --- | --- |
| 14.5.6.2p4's ordering as one question, and 13.4p1's target as its second reader | `sema_overload.cpp`, `sema_analyzer.h` |
| 14.5.6.2p9 and p10, the clauses p5 and p7 leave to be asked | `sema_overload.cpp`, `sema_analyzer.h` |
| 14.8.2.1p3's lvalue reference, collapsed by 8.3.2p6 | `sema_template.cpp` |
| 5.3.3p2 and 5.3.6p3 over a reference type-id | `sema_expression.cpp` |
| 14.6.4.1p1's point of instantiation at the end of the unit | `sema_template.cpp`, `sema_analyzer.h` |
| 14.5.6.1p5's answer as a signature of one declaration | `sema_template.cpp`, `sema_analyzer.h` |

Four regression tests: `300-target-type-chooses-more-specialized-template`,
`300-equivalent-function-template-declarations`,
`300-specialization-named-before-template-definition`,
`300-reference-parameter-template-ordering-and-collapse`.

### Performance Evidence

Fifteen shapes, each timed twice, `--emit-lowir -O0`, n = 32 to 512 unless
noted. Linear in the source: n distinct specializations 0.01 -> 0.10 s; one
specialization named n times 0.01 -> 0.03 s; an n-member template over four
specializations 0.00 -> 0.01 s; an n-deep nest of template-ids 0.01 -> 0.06 s;
n deductions over n classes 0.01 -> 0.09 s; n calls deducing one specialization
0.00 -> 0.02 s; n classes a specialization declares 0.01 -> 0.05 s; two tied
templates ordered at n call sites 0.01 -> 0.02 s; an overload set of n
declarations deduced against a parameter 0.01 -> 0.04 s; **n declarations of one
template name 0.01 -> 0.04 s (0.36 s before the fix, 1.57 s at n = 1024 against
0.08 s after)**; and n specializations named above their definitions 0.01 ->
0.06 s. An n-deep specialization as an ADL argument named n times is 0.05 s at
n = 256, of which 0.02 s is the nest with the calls removed.

Quadratic and 13.3p1's own shape, because a call gathers every declaration of
the name: n templates overloading one name each called once, 0.01 -> 0.18 s; n
target types choosing among n templates, 0.01 -> 0.15 s. The second is the path
finding 1 added work to, and it is the same n^2 candidate set the first is.

**One shape is exponential and it is the spelling.** `typedef P<t,t>` repeated
n times names a class whose written-out spelling doubles at every level: 0.01 s,
0.16 s, 0.62 s, 2.50 s at n = 12, 16, 18, 20 in 23 lines of source.
`reference-binaries/cppgm++` is 0.19 s, 2.85 s, 12.28 s and **46.31 s** on the
same inputs and g++ is 0.06 s at n = 20, so both implementations that store the
spelling have it and the one that does not, does not.

### Validation

- **1777 / 1777** through pa18, unchanged, and pa19 **223 / 295 -> 227 / 299**,
  the four new tests being the four regressions these findings leave. The whole
  pa1-pa19 report is **10.2 s**.
- **File audit passes** for pa19 over `dev/src`, with the five header-weight
  warnings the shared headers have carried since PA18 - and no suppression.
- **Every checked `.ref` and `.ref.witness` in the repository regenerates
  byte-identically** from `reference-binaries/cppgm++` through `make ref-test`,
  so the fixtures are the reference's output and not ours.
- **The differential probe both oracles answer.** 60 synthesized programs over
  the paths this checkpoint owns - partial ordering, target types, overload sets
  as arguments, operator templates, default arguments through a specialization,
  reference binding of every prvalue kind, and source order - compiled by
  `dev/cppgm++`, by `reference-binaries/cppgm++` and by g++, **run**, and their
  exit statuses compared. Every disagreement above was found this way and every
  one of them is now agreement; what remains is a member function template's
  address through a pointer to member, which the reference does not compile
  either.
- **The scaffold is not an oracle for a class passed by value.** `pa13`'s
  LowIR -> CY86 path is what runs these programs, and it hands a by-value class
  parameter garbage - `f(S a, S b)` with no template in sight reads `a.v` as
  `b.v`, identically from our LowIR and from the reference's, and differently on
  each run. Five apparent disagreements were this and not the frontend; run
  evidence is only evidence for programs whose parameters are scalars and
  pointers.
- **Multi-unit.** Two units that each choose the same specialization through a
  target type hold **one weak definition** of `_Z4pickIiEiPT_` between them and
  are canonically identical in both unit orders; `object=` and `binding=` on the
  new fixture's emitted symbols are byte-identical to the reference's.
- **Valgrind clean** with `--error-exitcode` over all 299 fixtures and over the
  newly reached paths.

## Open Gaps

**Recorded, not defects.** A specialization of a template declared in
7.3.1.1p1's unnamed namespace binds `internal` here and `weak` in the
reference; 3.5p4 gives every name in that region internal linkage and **g++
emits it local**, so the reference stands alone. An instantiated constructor
emits both of 12.1's entry points where the reference emits only the
complete-object one; **g++ emits both**, and the reference's own non-template
out-of-class constructor gets both - so this is the reference's rule for
instantiated definitions, and matching it is what turned three fixtures green.

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

**A member function template's address is not a target 13.4p1 chooses through.**
`int (S::*p)(int *) = &S::m;` over two member templates finds no declaration
here; `resolve_target`'s pointer-to-member arm asks each declaration for the
pointer type it *has*, which a template has none of until a deduction makes one.
`reference-binaries/cppgm++` does not compile it either, and no fixture writes
it, so it is recorded where the rest of 14.8.2.5's pointer-to-member pairs are.

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
