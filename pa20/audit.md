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

## Current Checkpoint Review

C13 gave the semantic layer back what PA10's flattening dropped.  A name arrives
as the terminals the parse matched with the spaces gone, so
`I<R<A>::v < R<B>::v, B, A>` holds nothing that tells 14.2's list from 5.9's
operator one character at a time; `AngleReading` settles it from the one fact
the spelling still holds - a name writes no `>` that closes nothing - and
respells the argument it hands on with the separator phase 7 wrote, because the
`>` that settled the question is not in that run.  Beside it, 3.4.1p8 puts a
definition's initializer where its declarator-id reaches, and `place_type` is
given the function tier's places so `template<class T, T v>` settles at both.

The reading is right where it stands.  Sixty-six shapes were swept against g++
and `pa20/cppgm++-ref` on this review: `<`, `<=`, `<<` and their parenthesized
`>=`/`>>` twins written between literals, between named constants, between
`sizeof`s and between template-ids; a list of two arguments each holding one;
an operator before a template-id and after one; a dependent `N < 4` written at
a typedef, a base-specifier, a parameter type and an expansion's pattern; a
type argument that is a function type, an array or a `decltype`; a
partial-specialization pattern; a two-unit program; and the same names read in
`--emit-types` and `--emit-semantics`.  Every pair both oracles accept writes
the reference's own LowIR entries, every mangled name is g++'s byte for byte,
and each `.ref` under `pa20/tests` was regenerated from the reference binary
and did not move.  3.4.1p8 reaches a member typedef, a nested class, an
enumerator, a base's enumerator, a static data member, a shadowed name and a
class template's out-of-class definition alike, and the line each writes still
stands where the definition was written.

What the review found is that the reading is a fact of the whole spelling and
was built as though it were a fact of one run, at both the layers that ask it -
once per `<` in the splitters, and once more per level in the reading of a
value argument, which is a doubling rather than a repeat.

### Findings

**1. `spelling_balanced_end` read the whole spelling again at every `<`.**
`split_type_id` and `split_value_expression` walk one spelling and ask for the
end of each balanced run they step over.  C13 answered that question by
constructing an `AngleReading` of the whole spelling inside it, so the one scan
those splitters make became one scan per run:

| an argument naming k template-ids | 1024 | 2048 | 4096 |
| --- | --- | --- | --- |
| before | 0.05 s | 0.16 s | **0.53 s** |
| after | 0.00 s | 0.01 s | **0.02 s** |
| with a distinct specialization each | 0.04 s | 0.09 s | **0.21 s** |

`AngleReading::balanced_end` is the member now and the two splitters make the
one reading their walk asks - which is what the reading already claimed to be.
The scan that member falls back to asks that reading rather than the character
test, so the `<` inside a run and the `<` that opened it are answered by one
question at that exit too.

**2. A value argument was read twice, and a nest of them doubled at every
level.**  `template_argument_value` asks 14.6.1p1's question - does this
spelling *name* a place, rather than compute from one? - by looking a lone word
up before reading the words.  For a word like `W<3>::v` that lookup is the whole
reading of the name, argument list and all, so the argument was read once to
ask the question and once to answer it:

| a value argument nested d deep | 16 | 20 | 24 | 256 | 1024 |
| --- | --- | --- | --- | --- | --- |
| before | 0.41 s | 4.71 s | **43.46 s** | - | - |
| after | 0.00 s | 0.00 s | **0.00 s** | 0.03 s | **0.42 s** |
| `pa20/cppgm++-ref` | 0.00 s | 0.00 s | 0.00 s | 0.30 s | 7.01 s |

`W<W<W<3>::v>::v>::v` made 2^(d+1)-1 readings of its arguments where it owes
d+1.  14.1p4 declares a place under an identifier and 14.6.1p1 binds it under
that identifier, so only 2.11p1's identifier can be the name that question is
about; a qualified name or a template-id names something else and there is
nothing in one for it to find.  Asking it of an identifier alone leaves the
reading one per level.

**3. A `<` between two template-ids is quadratic in PA10's parse, and the
checkpoint's own row does not say so.**  C13 recorded `0.06 s` for 4096 of
5.9's operators in one argument list, which is the cost with *literals* on
either side.  With the operands the fixture writes - `W<0>::v < W<1>::v` - the
list costs 0.12 / 1.71 / **27.35 s** at 256 / 1024 / 4096, and `--emit-ast`
alone accounts for 1.63 of the 1.71 s: the backtracking that tells the two
readings apart flattens the token range of each candidate name, which is 44M
tokens flattened at 1024 against 2.8M at 256.  The same list with no operator in
it is 0.01 / 0.04 s and the same operators between literals are 0.00 / 0.00 s,
so it is the pair that costs.  The owner is `ast_parser_name.cpp` and
`AstTokenStream::flatten`, which is PA10's parse and not this milestone's
layer - and the reference is past 300 s on the shape this compiler takes 27 s
on - so it is recorded in the performance model rather than rewritten from an
audit of this checkpoint.

**4. The reference writes `Tn <type>` before a function template's non-type
argument, and g++ does not.**  `f<char, 66>()` over `template<class T, T v>` is
`_Z1fIcLc66EEiv` here and in g++ and `_Z1fIcTnT_Lc66EEiv` in the reference; the
same holds for a place typed by an earlier one, for two value places, for a
value pack and for a default filling one.  The class tier agrees with the
reference on all five, so the disagreement is the function tier's alone.

**5. The `<` no `>` can put back is a class of spellings and not one of them.**
The plan recorded `K<J<a < b> >`.  The sweep leaves the shape general: wherever
a template-argument-list holds a template-id whose *own* argument holds 5.9's
operator, both readings balance the flattened spelling and only a lookup of the
inner name tells them apart - `Nm<A<a < b>::n>::n` and
`P<A<a < b>, A<b < a> >` among them, which the reference reads and this
compiler refuses.  The reading picks the one where the inner `<` opens, because
the backward pass settles each suffix greedily and the forward walk replays it;
picking the other way would answer these and lose the shapes where the inner
name really is a template.  Nothing in a spelling decides it, which is why it
stands in the failure map under the flattening rather than under this reading.
