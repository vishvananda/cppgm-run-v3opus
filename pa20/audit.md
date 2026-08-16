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

## Current Checkpoint Review

C9 made 10p1's derivation a tree.  `SemaEntity::bases` and `Scope::bases` are
lists, one subobject is placed per base-specifier, 10.2p2's lookup asks each
base and refuses the name two of them answer, 12.6.2p10 constructs them in the
order the list wrote them and 12.4p8 destroys them in the reverse, 14.5.3p4's
expansion reaches the ctor-initializer, and 10.1p3's repeated base is refused
where the class is completed - which is what makes every walk of the tree one
visit per class.  10p1's derivation became its own owner
(`sema_derivation.h`), and 8.5.2's string literal initializing an array of
character type became another (`sema_string_init.h`).

Both owners are right where they stand.  `StringInitialization` is one reading
asked from the three places a program writes one, the code units are the ones
phase 6 already built, and twelve string shapes swept against both oracles
agree but the run of zeros the lowering has written for a short list since
PA15.  `Derivation` keeps
the four questions a tree answers together and each of them stops at the first
answer, which is what 10.1p3 buys.  The base-clause reading, the layout, the
lookup, the two orders and the ABI's `__vmi_class_type_info` were swept over 77
base-class and conversion shapes against g++ and the reference, and every
accepted pair writes the reference's LowIR but the offset-zero
`base_subobject` step the plan already records.

What the review found is that a base subobject now stands at a byte of its own,
and the readers that had only ever seen offset zero were not told.  Behind that,
the *region* an access is asked in outlives the reading that set it; and behind
that, two facts C9 made one - the base a class dispatches through, and the
clause 14.6.2p3 leaves off a lookup - are facts of one base-specifier and not of
the list.

### Findings

**1. 5.2.9p11's cast back to the object wrote no step.**  4.10p3's conversion
to a base moves the address on by the place the derived class gave it, and the
cast the other way about has to move it back.  It wrote nothing at all, on the
reading that the base "begins where the derived object does" - true of every
class this milestone could lay out before C9, and false for every base after
the first:

| shape | before | `pa20/cppgm++-ref` |
| --- | --- | --- |
| `static_cast<C*>(q)`, `C : A, B`, `q` a `B*` | the address `q` held | `index i8 %t, -4` |
| `static_cast<C&>(r)`, the same classes | the address `r` named | `index i8 %t, -4` |
| `static_cast<C*>(p)`, `C : A` with a vpointer C added | the address `p` held | `index i8 %t, -8` |

A member read through the result then stood one base's width past the end of
the object - `r->b = 5` wrote at `&c + 8` of an eight-byte object.  The step is
`derived_value` now, which is `base_value` read the other way about and writes
4.10p3's own `base-conversion` node with the offset and a `downward` fact the
lowering spells as a negative index; a base that does begin where the object
does still writes nothing, which is what leaves every single-inheritance cast
the output it already had.

**2. 11.2p4's access was asked where the operand's reading had already been
given back.**  `reading_` is set by `read_expression` for the region an
expression is written in and restored where that expression ends, and the
conversion an *initialization* applies runs after it: 8.5's declaration
initializer, 6.6.3p2's returned object, 8.5.1's clause and 8.5.3's bound
reference each convert with `reading_` holding whatever the enclosing reading
left, which at the top of a statement is nothing.  So a conversion to a
protected or private base written in those four places was refused wherever it
stood, including inside the class that named the base:

| shape | before | both oracles |
| --- | --- | --- |
| `B* p = this;` in a member of `C : protected B` | refused | accepted |
| `return this;` from `B* C::g()` | refused | accepted |
| `B* q[1] = { this };`, `const B& r = *this;` | refused | accepted |
| `take(this)`, `(B*)this`, `p = this` | accepted | accepted |

The region is held over the whole conversion now (`Written`, asked once in
`apply_conversion`), which is where 11.2p5 says the question is: the place the
program *wrote* the conversion.  A conversion to a base of a class from outside
it is still refused, as g++ refuses it.

**3. 10.3p1's refusal counted the bases rather than where the one that
dispatches stands.**  The ABI gives a class holding a base subobject that
dispatches and does not begin at the object's first byte a secondary table and
a thunk, and neither is emitted here - but the guard refused every class with a
dispatching base and more than one base at all.  A polymorphic *first* base is
the ABI's primary base, needs no secondary table, and was refused with the
shapes that do owe one: `struct C : A, B` for polymorphic `A` and plain `B`,
the same with a class below it, the same with a virtual destructor, and
`struct C : empty, P` where the empty base takes no storage - all four accepted
by both oracles and all four now writing the reference's LowIR.  What the class
inherits is the table of the base it dispatches *through* (`dispatching_base`)
rather than of `bases[0]`, which is what makes the empty-base shape right.

**4. 12.6.2p2's index was keyed by a name two bases can share.**  A
ctor-initializer is read into one entry per mem-initializer-id, keyed by the
last component of what was written - which named the base uniquely while a
class had one.  `struct both : n1::part, holder::part` writing
`: n1::part(1), holder::part(2)` reported `initializes holder::part twice` where
both oracles build it.  An id that names a direct base is held under the whole
name that class has now, and every other id under the last component it wrote,
which is 12.6.2p2's member - the class the id resolves to is what tells the two
apart, so a member and a type of one name stay two questions.

**5. 14.6.2p3 was made a fact of the base-clause and is a fact of each
base-specifier.**  A name written in a template definition is not looked up in a
base an argument list still has to settle, and C9 recorded that as one flag for
the whole clause: a class deriving from a settled class *and* a dependent one
had *both* left off 3.4.1's search, so `template<class T> struct C : A, T`
could not name `A`'s own member in its definition - `a names nothing where the
template that writes it is defined`, where both oracles read it.  The regions
3.4.1 looks in are the bases whose own specifier named a settled type
(`Scope::open_bases`), which the specialization an argument list makes answers
the same way because the fact was recorded where the pattern was read.

**6. 10.1p3's check is quadratic in the derivation, which is the shape and not
a defect.**  Refusing a repeated base is what makes every other walk one visit
per class, and it is one hash insert per class below the one being completed -
so a derivation that adds a base per level pays that walk per level: 0.011 /
0.049 / 0.154 / 0.584 s at 100 / 400 / 800 / 1600 levels, against 0.010 /
0.028 / 0.058 s for the same class count as one chain of single bases, which
pays nothing.  Every other walk of the tree is linear in what it walks and the
rows are in the plan.  The reference is 90x slower on a base pack of 1024 and
faster than this compiler on the plain class-declaration rows, so the two are
measured side by side rather than one against the other.
