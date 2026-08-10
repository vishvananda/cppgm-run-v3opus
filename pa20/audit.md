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

## Current Checkpoint Review

C6 gave 7.1.6.2p1's decltype-specifier the one thing a spelling cannot hold.
14.2 writes a template-argument-list inside a name, so the specifier reaches
this analysis as text, and only 5.1.1p8's id-expression could be looked up
there - a call through 13.5.4's operator, a delete-expression and 5.2.3p1's
explicit type conversion each say nothing such a lookup reaches.  The parse now
keeps the tree it read for every operand it flattened, under the spelling it
flattened it into (`AstArena::keep_spelled`), the analyzer borrows that table,
and `decltype_type` - the one reading that answers every other one - answers a
specifier met as text.  That is right where it stands: the nodes are the
parse's and not the tree's, which is exactly what a template-argument-list
drops; the table is keyed by the text, so one spelling names one tree however
many times a program writes it; and the tree carries no region, so every reader
of it asks `decltype_type` with its own context.

What the review found is that C6 landed *one* of the three walks that make
3.4.3p1's demand, and that the borrow it added reaches one of the three modes
that need it.  Behind those, one more: 14.6.1p1's current instantiation puts a
place at every argument, and a place that binds a value had been bound as a
type wherever an out-of-class definition is read against it.

### Findings

**1. Three walks make 3.4.3p1's demand and C6 changed one.**  A name is looked
up *in* the region its prefix named, and this compiler asks that three ways: a
prefix spelled as a name (`resolve_prefix`), a decltype prefix met as text (its
own branch, which C6 added), and a decltype prefix met as a *type*
(`qualified_in_type`, which the declarator, the expression, the `&` and the
callee readers all reach).  `require_complete_type` answers with nothing inside
14.6p8's reading, which is the whole of what C6 fixed - and it was still what
the third walk asked:

| shape | before | g++ and the reference |
| --- | --- | --- |
| `typedef decltype(make_it())::type held;` in a template definition | `a decltype-specifier written before `::` names no class or enumeration` | accepted |
| `typedef box<decltype(make_it())::type> named;` in one | the same | accepted |
| `holder<decltype(make_it())::value>` in one | `no declaration of decltype(make_it()) is in scope` | accepted |

The third row is the other half: a decltype prefix an argument list has *not*
settled names no region either, and the walk fell through to an ordinary lookup
of the spelling rather than leaving 14.6.2p1's stand-in - the one every prefix
written as a name is left with, and the one the substitution settles.  Both
walks now answer the way `resolve_prefix` does: settled, `require_settled_type`
puts the reading aside for the definition; dependent, each component behind the
prefix is a member of the one before it.

**2. The tree the parse kept reached one mode of three.**  `set_expressions` was
called from `emit_lowir` alone, on the reasoning that no other mode reads a
template-argument-list.  But a decltype-specifier is flattened into the name
around it whenever one is written before `::`, template or no:
`typedef decltype(decltype(h)::value) held;` is refused by `--emit-types` and
`--emit-semantics` and accepted by `--emit-lowir`, which is one program with two
answers.  The arena now travels with the tree through
`emit_translation_units`, so every mode that reads such a name reads it the same
way.

**3. A value place bound as a type wherever a definition is read against the
current instantiation.**  14.6.1p1's current instantiation puts at each place
the place *itself*, and `bind_argument` was handed one kind for the whole list -
so an out-of-class member definition's head bound `int N` as a typedef-name and
5.1.1p8 refused every use the definition made of it:

```cpp
template<class T, int N> struct holder { static T slot; static int count(); };
template<class T, int N> T holder<T, N>::slot = N * 2;   // N names a type ...
template<class U, int M> int holder<U, M>::count() { return M; }
```

This is every out-of-class definition of a member of a class template with a
non-type parameter, and it is the one shape C1's `open_parameter_region` had
always got right in the class's own body - the two readings of one head
disagreed.  A place standing for itself now says which it is
(`parameter_value_type`), so both readings bind a value place as a value.

### What the review confirmed rather than found

The typed ownership holds.  `AstArena` owns the nodes and hands out a
`const AstNode*`; `SemaAnalyzer` borrows the arena the way it already borrows
the pack and include tables, and null still means no table.  `SpelledTypeId` is
a reader of its own beside `TemplateArgumentReader`, and the header it was cut
from stands at 2380 of the audit's 2400 lines.  The spelling table is one entry
per *distinct* operand and never cleared, which is bounded by the program: a
speculative parse that is reset re-enters the same text, and `insert` keeps the
first, so a discarded reading cannot displace a kept one.  The arena is built
inside the per-unit loop, so nothing of one unit is reachable from another.

`dependent_member_type` is the fourth reader that looks a name up behind a
settled prefix, and it asks `require_complete_type` too.  It was instrumented
and run over every `.t` in `pa20/tests`, `cppgm.tests/course/pa20` and the
pa17-pa19 suites: it is never entered with `checking_ > 0`, so the demand it
makes is the one 3.9p5 makes outside 14.6p8's reading and is right as it
stands.

The complexity is what the plan claims, and the fixes cost nothing measurable.
The spelling table is one entry per distinct operand and one hash lookup per
naming: 512 / 2048 / 8192 distinct spellings are 0.018 / 0.069 / 0.300 s and
8192 namings of *one* are 0.180 s, which is what says the key is the text.
Nesting is flat - a spelling nested 24 deep is 0.004 s against an empty unit's
0.003 s - because a nested spelling is one more entry and not one more scan.
`require_settled_type` behind a decltype prefix is one instantiation per settled
specialization a definition names and one integer test otherwise, and the
dependent stand-in is memoised per prefix and component, so 64 names behind one
prefix cost one reading of the prefix.  Every row measured against the
`d7c036b5` build and against this one is the same to within the noise, the
quadratic pack recursion included.

Valgrind is clean - no message of any kind - over the eleven shapes the findings
are about and the four scaling shapes.

The differential sweep is 134 shapes through this compiler, through
`reference-binaries/cppgm++` and through g++, compared on the LowIR the first
two wrote rather than on the exit status alone.  100 of them are the twenty
decltype operands 7.1.6.2p1 admits - an id-expression, a call, a call through a
pointer, a member call, a call through 13.5.4's operator, a delete and a
delete[], 5.2.3p1's functional cast, a new-expression, a static_cast, a
`sizeof`, a conditional, a comma, an assignment, a subscript, `&`, `*`, a
parenthesized operand, an addition and a `!` - written in each of five places: a
namespace-scope typedef, a typedef in a class template, a type argument's
spelling, a non-type argument's spelling, and a type argument's spelling inside
a template.  All three compilers agree on all 100, and every one of the 55 both
accept writes byte-identical LowIR.  The other 34 are the prefix and
out-of-class shapes the findings are about, at one, two and three components,
at 64 names behind one prefix, over a renamed place, a pack place and a value
place beside a pack: every one agrees with both oracles, and the one LowIR
difference is the emission order the comparison canonicalizes.

### Recorded, not landed

- **The reference folds a static data member of a specialization of a template
  with a non-type parameter.**  `template<int N> int holder<N>::value = 7;` and
  then `holder<3>::value == 7` is a `load` here and g++ emits one too; the
  reference writes `cmp eq i32 7, 7` while emitting the storage all the same.
  The same head over a *type* parameter is byte-identical.  It is the family of
  `100-nontype-template-argument-static-member-no-storage`, which the failure
  map already owns.
- **A two-unit run writes one weak global where the reference writes two.**
  The program builder holds one symbol per name with external linkage, which is
  what 3.5 leaves crossing the boundary, so a definition two units both emit is
  written once.  This is the builder's design rather than a gap, and it predates
  every decltype shape here: the same two units over a *type* parameter differ
  the same way.
- **A decltype-specifier is outside the base-specifier grammar.**
  `struct outer : decltype(make_it())::inner` does not parse, here or in the
  reference; g++ accepts it.
- **The reference refuses a static member function called through a decltype
  prefix** (`decltype(make_it())::f()`), which this compiler and g++ both
  accept.
- **PA20's own recorded items are unchanged**: a specialization's body cannot
  name its own class, a partial specialization has no out-of-class members, a
  template template parameter in any head, a dependent array bound in an
  argument spelling, the reference's empty-pack function-template name,
  14.8.1p9's extension of an explicit list, `Tn` for a settled value argument of
  dependent type, `sizeof...` inside an argument spelling, the generated place
  name that collides with a written one, a pack name written without `...`,
  10p1 over a base pack of more than one element, and the static data member's
  demand.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double, the out-of-class member path's
  residual, 12.1's two constructor entry points, and the ABI's decltype return
  type.  A *class* metafunction with no terminating specialization still
  overflows the machine stack rather than being diagnosed, in this compiler and
  in the reference alike; it needs a depth guard rather than C5's same-list one.

## Changes

- **`sema_declarator.cpp` — 3.4.3p1's demand at the walk C6 did not reach.**
  `qualified_in_type` asks `require_settled_type` for a settled prefix, which is
  what 14.6p8's reading answers in earnest, and leaves 14.6.2p1's stand-in for
  a dependent one instead of throwing that the prefix names no class.
  `resolve_prefix`'s own decltype branch reports a dependent prefix the way it
  reports one written as a name, rather than falling through to a lookup of the
  spelling.
- **`sema_constant.cpp` — one implementation of one rule.**  `id_constant` asks
  `decltype_qualified_name`, which is what every other reader of a
  decltype-rooted id-expression asks, instead of a second copy of it with a
  stand-in of its own; the stand-in the entity's dependent type already earns is
  the one that answers.
- **`ast_emit.h`, `ast_emit.cpp`, `types_emit.cpp`, `semantics_emit.cpp` — the
  tree travels with every mode.**  `emit_translation_units` hands the arena to
  the writer, so 7.1.6.2p1's kept operand is there in the two dump modes that
  read such a name as well as in the lowering one.
- **`sema_template_head.cpp` — 14.1p4 at the current instantiation.**
  `bind_argument` binds a place standing for a *value* place as a
  `SemaKind::TemplateValue`, whichever kind the list is being bound under, so
  14.5.1.3p1's out-of-class definition reads its head's names the way the
  class's own body reads them.
- **Two fixtures** under `cppgm.tests/course/pa20`, each with a `.ref`
  generated from `reference-binaries/cppgm++` and each accepted by g++: a
  decltype prefix a template definition writes, settled and deferred, at one and
  two components and inside both kinds of argument spelling; and a value place
  an out-of-class definition names, over a static data member, two member
  functions, a renamed place and a pack place.

## Performance Evidence

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row is the shape's own cost.  Every row
was regenerated and re-measured against this build; none is carried forward.

| shape | here | `reference-binaries/cppgm++` |
| --- | --- | --- |
| 512 / 2048 / 8192 distinct decltype spellings in argument lists | 0.018 / 0.069 / **0.300 s** | 0.148 / 0.691 / 9.7 s |
| 8192 namings of *one* such spelling | **0.180 s** | 0.949 s |
| a decltype spelling nested 24 deep | **0.004 s** | 0.013 s |
| 256 / 1024 / 4096 decltype prefixes in one template definition | 0.021 / 0.082 / **0.410 s** | 23.9 s at 4096 |
| 64 names behind one dependent decltype prefix | **0.006 s** | 0.022 s |
| 256 / 1024 / 4096 out-of-class definitions over a value place | 0.014 / 0.046 / **0.188 s** | 2.084 s at 4096 |
| 256 / 2048 settled prefixes named in one definition | 0.015 / **0.119 s** | 0.250 s at 2048 |
| 14.5.3p4's recursion over a pack of 1024 | **1.556 s** | 9.3 s |
| a pack of 4096 elements bound and counted | **0.017 s** | 0.159 s |
| 256 patterns against 2048 distinct lists | **0.063 s** | 11.1 s |
| `fac<800>` metafunction chain | **0.032 s** | 0.167 s |

The last four rows were measured against a build of `d7c036b5` and against this
one: 1.589 / 0.018 / 0.064 / 0.032 s there against 1.556 / 0.017 / 0.063 /
0.032 s here, so the fixes cost nothing this machine can see.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **158 / 184**, from a
  turn-start **156 / 182** - the two added here pass and the 26 failing at turn
  start are the same 26, name for name.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over the eleven finding shapes and the four scaling shapes.
- Every `.ref` under `cppgm.tests/course/pa20` was regenerated from
  `reference-binaries/cppgm++`; the eighteen that were already there are
  byte-identical.
- `dependent_member_type` was instrumented and run over every `.t` in
  `pa20/tests`, `cppgm.tests/course/pa20` and the pa17-pa19 suites: it is never
  entered from inside 14.6p8's reading, which is why the demand it makes needed
  no change.
