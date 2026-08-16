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

## Current Checkpoint Review

C11 made 14.5.3p4's pattern one reading wherever it stands.  A
parameter-declaration was the last shape still building the type *once* and
expanding that, which is right for `A...` and `A*...` and wrong for every
pattern an instantiation makes: `wrap<A>...` substituted first names one
`wrap<` run `>`, and `TypeTable::substitute` has no `Class` case to take it back
apart.  So `PackReading::read_places` reads the decl-specifier-seq and the
declarator again per element in that element's region, and `substitute_entry`
binds the written pattern's places to one element each rather than substituting
the run into the pattern.

The reading is right where it stands.  Runs of 0 / 1 / 2 / 3 over a
specialization pattern, its pointer, reference, const-reference, member and
nested-specialization spellings, an unnamed place, fixed places before the pack,
two heads writing two runs into one clause, a forwarding call, a declaration
followed by its definition, 14.8.2.4p9's ordering against a non-pack head and a
two-unit program were swept against g++ and the reference on this review: every
accepted pair writes the reference's own LowIR entries and every mangled name is
g++'s byte for byte, including `_Z1fIJEEiDp4wrapIT_E` for the empty run and
`_Z1fIiEi4wrapIT_E` where the non-pack head wins - against the reference's own
`z` the plan already records.  `substitute_entry`'s per-element loop is
load-bearing rather than a second spelling of the old one (17 substitutions on
the checkpoint's own fixture) and falls back to the structural rebuild where a
place the pattern names is unbound.

What the review found is that the reading C11 built its fourth shape on cannot
see what a *spelling* writes inside one node, so C10's rule reached the two
nodes a program writes an expansion at and not the text a template-id is.

### Findings

**1. 14.5.3p5 and 5.3.3p5 were answered by node kind, and a template-id is one
node.**  PA10 flattens a template-id into the text of the node that stands for
it - one decl-specifier, one callee - so `list<A, B...>` writes its inner `...`
and `pair2<A, sizeof...(B)>` its `sizeof...` inside that text.  `names_in`
scanned every identifier out of it, so both counted as packs the *enclosing*
expansion is over, and the parameter-declaration reading C11 added inherited
that answer whole:

| shape | before | `pa20/cppgm++-ref` | g++ |
| --- | --- | --- | --- |
| `count(list<A, B...>()...)` in a call | two packs of different lengths | accepted | accepted |
| `list<A, B...>... p` in a parameter clause | two packs of different lengths | its own reading zips the two | accepted |
| `pair2<A, sizeof...(B)>... p` in a parameter clause | two packs of different lengths | its own substitution fails | accepted |
| the same clause where the two runs are equally long | accepted, and right | zips them | accepted |
| `held<list<A, B...>...>` in an argument spelling | accepted, and right | accepted | accepted |
| `count(list<B...>()...)`, whose only pack the inner one took | refused, and right | accepted | refused |

`names_in` reads each node's text with `spelled_names_in` now, which is the
reading 14.2's argument list already had: the operand each inner `...` was
written after is left out and `sizeof...` is left out with the name it counts.
The four shapes of the one reading - a spelling, a tree, a parameter-declaration
and a type a substitution rebuilds - therefore answer alike, and a pattern that
names no pack of its own is still refused at every one of them, which is the
last row.
