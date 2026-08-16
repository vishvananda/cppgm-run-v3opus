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
| C8 | `8dfad19a` | 5 / 5 | **the dialect a character-literal is read in, which covers two facts and was moved for one.**  What a c-char above the ordinary range is worth, and whether a run of them is a literal at all, are one question PA2's dump and the language answer differently - the reference splits them the same way, its `#if` reading `'é'` as 233 where its phase 7 reads -23 - so `MulticharacterLiterals` was the right shape and reached one of the two: every ordinary character literal above 127 stayed PA2's `int` holding a code point, so `sizeof('\xff')` was 4 here and 1 in the reference and g++ alike.  The packing was a second implementation of 2.14.5p5's execution encoding beside the one `string_literal.cpp` has owned since PA2, and it packed code *points*: `'aé'` was `0x61e9` where both oracles write `0x61c3a9`, and the acceptance set was wrong with it.  A `char` literal may now hold a negative value, which found `literal_value` shifting a literal's bytes together without 3.9.1p1's sign, so `long long g = '\xff'` wrote 255.  C8's own claim that one reading answers a tree and a spelling alike was untrue for 5.1.1p6's parentheses, which the tree strips and the spelling did not, so `p<("abc")[1]>` was refused where both oracles accept.  And 2.14.8p3's raw operator was asked before the literal operator template, where the reference calls the template |
| C9 | `964bc63d` | 5 / 5 + 1 perf | **the tree a derivation became, which C9 built and the four readers of one base were left reading a chain.**  10p1's list gives a base subobject a byte of its own, and every reader that had only ever seen offset zero kept the answer it had: 5.2.9p11's cast back to a derived class wrote *no* step at all, so `static_cast<C*>(q)` where `C : A, B` held the address of the `B` subobject and a member read through it stood past the end of the object - the reference writes `index i8 %t, -4` there, and the same was true of a single base a class had put after its own vpointer since PA17.  11.2p4's access was asked of `reading_`, which is set while an *expression* is read and given back before the initialization converts it, so every conversion to a non-public base written as a declaration's initializer, a return, an aggregate clause or a bound reference was refused - `B* p = this;` inside the class that named the base among them, where both oracles accept.  10.3p1's refusal covered a base subobject that dispatches *and is not the only one* rather than one that does not begin where the object does, so a polymorphic first base beside a plain second - which the ABI lays out as its own primary base and needs no thunk - was refused with the shapes that do owe one.  12.6.2p2's index was keyed by the last component of the mem-initializer-id, which is not a name of a base once a class can have two: `struct both : n1::b, n2::b` reported `initializes n2::b twice`.  And 14.6.2p3 was made a fact of the whole base-clause, so a settled base beside a dependent one was left off 3.4.1's search and `struct A { int a; }; template<class T> struct C : A, T { int f() { return a; } };` named nothing.  The perf finding is 10.1p3's own check: it is quadratic in a derivation that adds a base per level, 0.011 / 0.049 / 0.154 / 0.584 s at 100 / 400 / 800 / 1600 levels |
| C10 | `9196229b` | 3 / 3 | **the packs a pattern is written over, which C10 settled at one of the three readings that ask it.**  14.5.3p5 leaves a pack named inside an *inner* expansion to that expansion, and C10 answered it at the tree - so `sum(get<U>(t...)...)` reads once per element of `U` while the same shape written in 14.2's argument list, where the pattern is text, still counted `t`: `list<list<A, B...>...>` was refused as `a pack expansion names two parameter packs of different lengths` where both oracles read it, and a pattern whose *only* pack an inner expansion already took was accepted where both refuse.  5.3.3p5's `sizeof...` is the same question and neither reading asked it, so `f(x, sizeof...(A))...` and `nums<(N + sizeof...(A))...>` were runs of two packs of different lengths.  Behind that, 3.2p3's demand for the storage an instantiation lays out was made at 9.2p1's non-static data member, which lays out no object of its own and is met before the definition it looks for has been read: `struct holds { later<int> m; };` with no object of `holds` laid out storage the reference does not, and the same class with the definition written after it *latched* and laid out none where the reference does.  And 14.3.2p1's refusal of a type where a value belongs was asked of every pack the pattern names rather than of the argument each element is, so `sizes<sizeof(T)...>` was refused with `T...` |
| C11 | `51f8f135` | 1 / 1 | **the two things written *inside* a spelling rather than beside it, which the reading C11 built its fourth shape on could not see.**  A parameter-declaration became the fourth reading of which packs a pattern is over, and it asks `names_in` - the reading over a tree.  But a template-id is one terminal of that tree, so `list<A, B...>` carries its own `...` and `pair2<A, sizeof...(B)>` its own `sizeof...` in one node's *text*, where a rule answered by node kind reaches neither: both were counted as packs this run is over, so `list<A, B...>... p` and `pair2<A, sizeof...(B)>... p` in a parameter clause were refused as two packs of different lengths where g++ reads them - and so was `count(list<A, B...>()...)` in a call, at the very reading C10 changed, where the reference and g++ both read it.  The tree reading reads each node's text the way an argument list's is read now, so the spelling, the tree, the parameter-declaration and the type a substitution rebuilds answer alike |
| C12 | `80cefaed` | 5 / 5 + 1 perf | **the object a constant expression builds, which C12 gave one initialization and every class type.**  5.2.3p2/p3's `T(x)` and `T{x}` were read as 8.5.1p2's clauses - one per member in declaration order - which is what an *aggregate* takes them as, and 8.5.1p1 leaves no class that declares 12.1's constructor one: `constexpr` was never read off a constructor at all, so `S(3)` with `S(int v) : a(v * 2)` was the object holding 3 where both oracles hold 6, `S(int v) : a(v), b(v + 1)` held `3, 0` where both hold `3, 4`, a default constructor's `a(9)` held 0, and a two-parameter constructor of a one-member class was refused as writing "more initializers than S has members".  8.3.6's default-argument was no part of the list a fold reads at any of its three exits, so `f(1)` where `f(int, int = 10)` was refused where both oracles accept and `f()` where `f(int a = 2)` wrote a `role=init` function and the definition of `@f` where the reference writes the value in the image and nothing else.  12.3.2p1's conversion was a fallback rather than a choice - the first constexpr conversion function reaching *any* arithmetic type where none reached the place - so a class declaring `operator char` beside `operator int` answered at a `long` place where both oracles refuse the ambiguity.  The answer of a fold was carried on the resolved node whatever it was, and an object's bits are the identifier of its interned subobject list, which is no value at all to the readers of one.  And 5.2.5p1 had no reading, so an object of a class one of whose members is a class - `wrapped { pair held; }`, the checkpoint's own fixture - could be converted and never taken apart.  The perf finding is 12.6.2p2's index: the ctor-initializer was scanned once per member, 0.394 / 1.400 s at 4096 / 8192 members against 0.191 / 0.479 s with the list indexed once, which is what declaring the class costs with the fold taken away |
| C13 | `a3a74360` | 2 / 2 + 3 recorded | **the reading C13 made a fact of the whole spelling, made again at every run of it.**  Which `<` opens 14.2's list is settled once per spelling and asked at each of the runs `split_type_id` and `split_value_expression` step over - but the reading was built inside `spelling_balanced_end`, which is the *per-run* question, so a spelling naming k template-ids paid k readings of the whole of it: 0.05 / 0.16 / 0.53 s at 1024 / 2048 / 4096 against 0.00 / 0.01 / 0.02 s with the one reading handed in.  Behind it, `template_argument_value` asked 14.6.1p1's question - does this lone word name a place? - with a lookup of *any* single word, and for `W<3>::v` that lookup is the whole reading of the name, so every value argument was read twice and a nest of them doubled at every level: `W<W<...W<3>::v...>::v>::v` 24 deep took 43.5 s where the reference is flat, and 0.00 s once the question is asked only of 2.11p1's identifier that could answer it |
| C14, final | `6e25a9b8` | 3 / 3 + 5 recorded | **two facts settled per *use* that are facts of the thing itself, and one rule written at one of its two readers.**  `AstTokenStream::flatten` decided the separator between two terminals from the range being spelled, and PA10's ordered choice spells every candidate name it tries, so a list writing 5.9's operator between two template-ids re-read the whole suffix per argument: 26.6 s -> 1.4 s at 4096 with the separators settled once per terminal and `parse_template_argument` memoised the way `skip_simple_template_id` already was.  10.1p3's walk built a hash table per class where a number on the class says the same thing, which was 37% of a 1600-level derivation.  And 5.19 is read twice in this compiler - over a tree and over 14.2's words - with 5.2.5p1's access written in the first only, so `chosen<point{2,5}.sum()>` was refused where the same expression at a declaration folded.  Beside them 14.1p11's default before a pack walked past the pack place and asked it for a default of its own |

## Final Audit

The stage passes, so this review took the architecture apart rather than the
failure list: the layers a PA20 fact travels through were reconstructed from the
source and each ownership claim was put to a probe, independently of what the
checkpoints above say they landed.  Eighty-one shapes were swept through the
harness's own comparator against `pa20/cppgm++-ref`, with g++ as the third
oracle on every one of them; the reference's `.ref` files were all regenerated;
and the performance model was re-measured end to end against a worktree build of
the pre-audit commit, because a table that carries numbers forward is a table
that stops being true.

### Findings

**1. The spelling of a token range was decided per range.**
`AstTokenStream::flatten` asked `needs_separator` for every terminal of every
range it was given, and whether a separator stands between two terminals is a
fact of those two and the one after them - the range does not enter into it.
PA10's ordered choice spells every candidate name it *tries*, and the shape that
pays for it is an argument list writing 5.9's operator between two template-ids:
in `W<0>::v < W<1>::v, W<1>::v < W<2>::v, ...` each `v` reads as a template-name
whose own list runs to the end of the outer one, so the k-th argument's reading
covers every argument after it.  62% of that compile was inside `flatten`, and
another 15% in the hashing and string building under it.

**2. `parse_template_argument` was the one question in that descent with no
memo.**  `skip_simple_template_id` is remembered per position, and the rule one
level below it - what a single `template-argument` matched - was not, so every
attempt at the enclosing list re-read *and re-built the nodes of* every argument
after the one it was at.  Those nodes are dropped: the rule that asks only wants
to know where the argument ended, because PA10 flattens the whole template-id
into one spelling and keeps no tree of its arguments.

**3. 10.1p3's walk built a table per class.**  The refusal of a repeated base is
asked once per class completed and the walk that answers it covers the whole
derivation below that class, so a program adding a base at every level makes one
walk per level - each of which allocated an `unordered_set`, hashed every class
into it, and threw it away.  37% of a 1600-level compile was the walk and 27%
more was the allocator behind it.

**4. 14.1p9's default written before a pack place asked the pack for one.**
14.1p11 lets a place before a class template's pack carry a default template
argument, and the reading that fills those walked on past the pack place:
`template<int N = 5, class... T> struct S` refused `S<>` as a list "too few
arguments" long, where g++ and the reference both bind `N` to 5 and the pack to
nothing.  The list *reaching* that place already answered it the other way - a
pack the list stopped short of is a run of none - so one rule had two answers
depending on which of the two ways the reading arrived.

**5. 5.2.5p1 was written at one of 5.19's two readings.**  A constant expression
is read over a tree where a declaration wrote one and over the words 14.2 leaves
a template argument as, and the member access existed only in the first: `.` was
not among the operators the spelling splits into at all, so
`chosen<point{2,5}.sum()>` and `chosen<p.y>` were refused where g++ and the
reference read them and where the *same expression* written at a declaration or
in a `static_assert` folded.

**6. One probe of the three that throw a reading away did not put 14.6p8's count
back.**  `probe_type_id` and `Specialization::record` both restore `stood_in_`
where their reading throws, and `ConstexprReading::call_or_cast`'s lookup of the
name before a `(` did not - and that name may be a template-id, so an argument
list is read on the way there and the count can move before the throw.  No probe
reproduced a wrong answer from it; it is the same rule at a third exit, closed
for that reason.

### Recorded rather than fixed

**14.7.3p6's specialization written after the use that instantiated it** is ill
formed with no diagnostic required.  g++ diagnoses it; the reference silently
re-reads the specialization for the instantiation already made, so
`int a = S<int>::n;` written before `template<> struct S<int>` is 9 there and 1
here, and an object declared before the specialization is laid out to the
specialization's size.  Every *well formed* late visibility the README asks for
is answered - a specialization seen before 14.6.4.1's point of instantiation, one
declared early and defined late, and the stale primary the checked-in fixture
pins - so the reference's recovery on an ill-formed input is not the contract.

**10.1p3's second subobject of one base** is refused rather than laid out, which
is the decision that makes every walk of a derivation one visit per class.  Both
oracles lay it out.

**8.3.3p1's pointer to member** is C15's own work, and the sweep adds one shape
to it: the declarator refuses a *dependent* member pointer outright, so
`template<class T> int g(int (T::*p)(int))` is "T is written before `::*` and
does not name a class" where both oracles deduce it.

**Three edges of 5.19's object reading** stay outside the spelling reading or
outside the image: a named `constexpr` object of class type is no constant, a
braced clause nested inside another (`O{{1}, 2}`) is not entered by the split,
and a fold whose object is a temporary is carried on no resolved call node, so
`const int n = pt{2,5}.sum();` is dynamically initialized where the reference
writes 7 into the image - while remaining a constant expression everywhere one is
asked for.

**Two shapes where this compiler is the one that is right.**  It folds
`H<char>::v` where the reference loads it, `H<T>::v` being a `const int` with a
constant initializer; and two units each naming `Arr<int>::t` leave *two*
`global @Arr_int___t` definitions in the reference's one LowIR file and one in
this compiler's, where the same two units naming one inline function leave one in
both.

### Changes

- `AstTokenStream` writes the whole stream out once, with the offset each
  terminal's spelling begins at, and `flatten` is the substring the range's own
  terminals occupy.
- `parse_template_argument` is memoised on the key and the version
  `skip_simple_template_id` already uses, with the body split out as
  `parse_template_argument_body`.
- `Derivation::require_distinct` marks the classes it reached with
  `SemaEntity::reached_at` from `SemaModel::next_reach`, which is the shape
  `SemaModel::visit_` already marks a region with.
- `bind_template_arguments` stops filling defaults at the pack place.
- `ConstexprReading::member_value` and `member_call` are 5.2.5p1 asked of the
  object and the name, which both readings of 5.19 now ask; the spelling reader
  gained `.` (and `...` in front of it, so an expansion stays one word) and a
  postfix walk.
- `ConstexprReading::call_or_cast` puts 14.6p8's count back where its lookup
  throws.
- `spelled` and `keep_decltype` moved to `ast_parser.cpp`, keeping
  `ast_parser.h` under the audit's header-weight line.
- Two course tests added, each generated from the reference binary and accepted
  by g++: `200-a-default-written-before-the-pack-place` and
  `200-a-member-read-out-of-an-argument-spelling`.

### Performance Evidence

The whole model was re-measured on this turn's build against a git worktree
built from the pre-audit commit `6e25a9b8`, one shape per invocation and
alternating between the two binaries so neither reading carries the page cache
or the heap of the one before it.  Every row of the model was measured, not just
the rows the changes touch; **no row is slower**, and the rows that moved are:

| shape | before | after | `pa20/cppgm++-ref` |
| --- | --- | --- | --- |
| 1024 / 4096 of 5.9's operators between two template-ids | 2.66 / **26.56 s** | 0.118 / **1.474 s** | >120 s at 1024 |
| a value argument nested 1024 deep | **0.422 s** | **0.102 s** | SIGSEGV at 1024 after 6.9 s |
| 1600 / 3200 levels each adding a second base | 0.558 / **2.294 s** | 0.262 / **1.099 s** | 38.9 s at 800 |
| 4096 class prvalues folded through a member call | refused | **0.110 s** | 0.774 s |
| 4096 member accesses read out of argument spellings | refused | **0.117 s** | 0.774 s |
| 4096 namings of a head with a default before its pack | refused | **0.321 s** | 1.075 s |

The three fixes were profiled rather than guessed at.  Before: `flatten` was
62% of the parse of a 4096-argument list and the hashing and string building
under it another 15%; `Derivation::require_distinct` was 37% of a 1600-level
derivation and the allocator behind it 27% more.  After: `flatten` does not
appear, and the derivation walk is 63% of a much smaller total - pointer chasing
over the (class, ancestor) pairs the rule is about, with nothing left to remove
that is not the fact itself.

The residues are written into the model with a reason each rather than a number:
10.1p3 is quadratic in the size of the derivation relation, PA10's ordered choice
spells O(n) candidate names of O(n) terminals, 14.5.3p4's recursion interns n
lists of n entries, and a nest of d value arguments splits d spellings of length
O(d).  Peak memory on the worst of them fell with the time: 4096 operators
between template-ids is 322 MB against 379 MB, and the earlier build's arena held
the same spellings built one extra time per attempt.

### Validation

- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src` - **pass**,
  with the five header-weight warnings the stage inherited and no sixth: moving
  `spelled` and `keep_decltype` into `ast_parser.cpp` is what keeps
  `ast_parser.h` under the line after the memo above it was declared.
- `make test-report-through-pa20` - **pass**, 2399 / 2399 through pa20, with
  pa20 itself 230 / 230 (174 fixtures and 56 course tests).
- `make -C pa20 ref-test` regenerated every `.ref` under `pa20/tests` from
  `pa20/cppgm++-ref` and none moved, so no fixture holds this compiler's answer.
  The two course tests added this turn were generated the same way and are
  accepted by g++.
- 81 shapes swept through the harness's own comparator - which validates both
  LowIR texts structurally before comparing them - against `pa20/cppgm++-ref`,
  with g++ as the third oracle on each: pack against explicit specialization,
  constexpr, derivation, array members and defaults; every declared type at a
  value place and at a value pack; every declared type of a member read out of a
  spelling; two of each thing that fixtures write one of; late specialization at
  five points of instantiation; and four shapes all three oracles refuse.
- Two units compiled together, over a header naming every PA20 feature at once:
  byte-for-byte the reference's LowIR apart from the weak global it writes twice.
- `valgrind --error-exitcode=99 -q` over all 232 pa20 fixtures, course tests and
  probe inputs: **clean**.
- Three programs built through `lowir2cy86` and `cy86` and run, each returning
  what the source computes - 16, 15 and 15 - two of them over the rules this
  turn added.
