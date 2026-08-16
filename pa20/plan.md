# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **215 / 220** - 169 of the 174 checked-in fixtures and all 46
under `cppgm.tests/course/pa20` - from a turn-start baseline of **212 / 217**,
with pa1-pa19 at **2169 / 2169** and the file audit passing with the five
header-weight warnings it inherited.  Every `.ref` was regenerated from
`pa20/cppgm++-ref` through `make -C pa20 ref-test` on this turn and none moved,
so every fixture holds the reference's own answer and not this compiler's.

The milestone gives PA19's template tier what 14.4p1 keys it by: an argument
list, whose entries are types, 14.3.2p1's *values* and 14.5.3p1's *runs* alike -
all of them `TypeId`s, because every fact the tier holds (the specialization,
the substitution, the object-file name) reads that list as
`std::vector<TypeId>`.

## Stage Design

**Everything an argument list holds is a type-table entry.**  `TypeKind::Value`
(`type_model.h`) is 14.3.2p1's argument at a non-type place - the type it was
converted to and the bits it holds, interned so `value_type(int, 3)` is one
entry however many times `f<3>` is written; `is_dependent` asks only its type.
`TypeKind::Pack` is 14.5.3p1's *run*, interned by its elements, or 14.5.3p4's
*expansion* `P...`, interned by its pattern; `is_dependent` is true for every
expansion and for a run holding a dependent element.  Nothing declares an
object of either.  5.2.3p2's object of literal class type is a `Value` too: its
type is the class and its bits are the identifier of the interned list of what
its subobjects hold, so `S{5}` written twice is one entry.  Those bits are no
value, so only a fold whose result is arithmetic is carried on the node that
folded it.

**14.1p4 and 14.1p11's places say what each takes.**
`TemplateInfo::Parameter` (`sema_template.h`) is a place rather than a name -
the spelling, whether it binds a value, whether it binds a run, the syntax that
says the value's type, and the type standing for the place.
`open_parameter_region` opens 14.6.1p1's region once per template; `pack_place`
(and `function_pack_place` for a head read by the declaration path) is what
tells a written argument which place it fills; 14.1p3's unnamed place is
declared too, because 14.1p9's default is the place's fact.  A value place
binds a `SemaKind::TemplateValue` constant rather than a typedef-name, and a
run binds a `Pack`-typed name, so every reader that already folds a constant
folds one.  14.1p9's default is read at both tiers in a region binding the
places before it, because 5.19 is evaluated where it stands.

**A run that is not the last place is one entry of the list.**  14.1p11 leaves
a class template's pack last, but 14.8.2 deduces a function template's head
place by place, so `template<class... U, class... T>` needs `<char, short|int>`
told from `<char|short, int>`: the last place's run is written flat and every
earlier run stands as one `Pack` entry (`place_argument`,
`trailing_pack_place`).  The object file writes either `J...E` per run, with a
place written `P...` encoded `Dp` of its pattern, and an argument no
substitution settled written `X <expression> E`.

**5.19 is read out of the spelling, like 8.1p1's type-id is.**  14.2 writes the
argument list inside a name, so it arrives as text: `sema_value_expression.cpp`
is to a value argument what `sema_type_id.cpp` is to a type argument.  The
terminals are recovered rather than re-lexed, the split is kept per spelling,
and what a `<` in one *is* - 14.2's list or 5.9's operator - is one question
answered in one place (`sema_name.cpp`), which also counts `(`, `[` and
5.2.3p3's `{` as runs whose contents are expressions, so the comma in
`A<S{1, 2}>` belongs to the initializer.  Each is a reader of its own that
borrows the analyzer, because where in the words it stands is state no walk of a
tree has.

**An operand a spelling cannot answer for is asked of the parse.**
7.1.6.2p1's decltype-specifier holds an expression, so the parse keeps the tree
it read for every operand it flattened into a name (`AstArena::keep_spelled`)
and the analyzer borrows that table; `decltype_type` is then the one reading
every other one asks.  3.4.3p1's prefix that an argument list has settled
demands its definition through `require_settled_type`, which puts 14.6p8's
reading aside whole - depth and dialect - because a specialization completed in
the checking dialect is left with none of 12.1's members it is owed.

**An expansion is one reading per element, and the inner one owns its packs.**
`sema_pack.h` owns it: the pattern is read again per argument of the run, in a
region binding the packs it names to that element, and nothing rewrites the
pattern's syntax.  14.5.3p5 leaves a pack named inside an *inner* expansion to
that one, and 5.3.3p5's `sizeof...` counts a run rather than standing in one, so
both are left out of the packs the outer run is over - by node kind for a tree
and by `spelled_names_in` for a spelling, and a node's own text *is* a spelling,
because PA10 flattens a template-id into one terminal.  The one reading answers
from the four shapes a list is written in: a spelling (`expand`), a call's
argument tree (`run_of_node`), a parameter-declaration (`read_places`) and a
type a substitution built (`substitute_entry`) - the third because a pattern
that is a specialization cannot be taken apart after `TypeTable::substitute`
built it once for the whole run.  `expand_type` stays the structural rebuild for
a run already inside a built type.

**A list the program wrote is what that reading is asked of.**  `WrittenList`
(`sema_pack.h`) is one written list and what its entries come to, so 8.5.1's
clauses, 5.2.2's arguments, 5.2.3's conversion, 5.3.4's placement, 8.3.4p3's
bound and 13.3.3.1.5's length ask one question; a list holding no expansion
pays one node-kind test per entry.  `InitializerClauses` adds 8.5.1p11's cursor
so an elided subaggregate reads each clause where 14.5.3p4 put it.  A list of
*one* entry is such a list too, and a run no argument list has settled stands as
the one entry it was written as.

**14.8.2 is a match, and `sema_deduce.h` owns it.**  A parameter type P is
walked beside the type A of what the use put there; 14.8.2.1's call and
14.8.2.2's target type write those pairs and both end at 14.8.2p5.  A list of
entries is one rule (`match_arguments`) - 14.2's argument list and 8.3.5p1's
parameter list are both entries with a trailing `P...` taking the rest - so a
specialization P against a specialization A deduces a run, and 14.8.2.1p3's A
may be a class derived from what P names at the top of a written pair.

**14.5.5's pattern is that same match, and `sema_specialize.h` owns it.**  A
partial specialization is a template beside the primary: a second body an
argument list may be read from, chosen by `match_arguments` over the interned
list, memoised on `TemplateInfo` and dropped where a later pattern arrives;
14.5.5.2p1's ordering is the same match between two patterns, and
14.5.6.1p5's signature tells a redeclaration from a second pattern.
14.5.1p1's variable template is the third tier: its specialization *is* the
constant its initializer evaluates to, so `TemplateInfo::reading` is what a
naming of a list already being read finds.  A template one of whose second
bodies could not be read answers no argument list at all.

**7.1.5's constexpr function is a body, so it is a reading of its own.**
`sema_constexpr.h` owns 5.19p2's operands the arithmetic cannot answer - an
id-expression, a unary or binary operator, 5.2.5p1's member access, and the one
shape the grammar hands on as a call.  That shape is 5.2.3p1's cast,
5.2.3p2/p3's object of literal class type, or 5.2.2p1's call, and the one lookup
of the name before the parentheses says which.  A fold reads 7.1.5p3's body
itself - one `return` statement with only typedefs, alias-declarations,
using-declarations, using-directives, static_asserts and null statements around
it - in a region of its own opened over the region the declarator gave the
places, binding each parameter to what its argument came to and, for a call on
an object, each non-static data member of that object to what the object holds.
So nothing waits for the body to have been walked: `SemaEntity::constexpr_body`
and `constexpr_region` are recorded where the definition is *met*, which is what
lets a member function defined in a class body answer a call written in the same
class.  The list the places are filled with is one reading
(`passed_arguments`): each written argument converted to its place, and
8.3.6p1's default-argument - read in the region 8.3.6p9 leaves it to - for every
place the call stopped short of.  The answer is a fact of the callee and that
list, so it is keyed by the declaration and `TypeTable::type_list` of the
entries - exactly the key 13.1 tells two overloads apart by - and held in the
model (`SemaModel::folded_call`).  `folding_depth` bounds a function that calls
itself.

**8.5.1p1 says which initialization an object at a value place takes.**  An
aggregate takes 8.5.1p2's clauses, one per member in declaration order with
8.5.1p7 value-initializing the rest.  Every other class takes 8.5p16's
direct-initialization, and there the object is what 12.6.2's mem-initializers of
the chosen constructor come to (`object_from_constructor`): read in a region
binding the places and, in 12.6.2p10's declaration order, the members already
settled, so `b(a + 1)` names `a` and 8.3.5p10's place of that name still shadows
it.  7.1.5p1 is read off a constructor as off any other function, 7.1.5p4
refuses a member no mem-initializer reaches and a body that is more than
7.1.5p3's declarations, and the answer is held under the very key a call of a
function is.  12.3.2p1's conversion function is how such an object reaches
14.1p4's value place: one that reaches the place itself is chosen, and a class
offering two that reach it only through a further conversion is 13.3.3p1's
ambiguity and refused.  5.2.5p1 is the other reader of one - `E.m` is the
subobject its list holds and `E.f(args)` is `call` on it - and both go through
`member_named`, which is the expression layer's own lookup.

**A folded call is a value where the image holds it and a call where a body
runs it.**  7.1.5p2 makes a constexpr function implicitly inline, and the
resolved call node carries the value when the callee is constexpr, its result is
arithmetic and every argument is constant - so 3.6.2p2 gives an object at
namespace scope its value in the program image, and an object a fold answered
with, whose bits are a list identifier and no value, carries nothing.  3.2p2
then reads that node as a use only inside a body: `demand_referenced` stops at a
folded call standing outside every function definition, because nothing there
ever runs it.

**10p1's derivation is a tree, and `sema_derivation.h` owns it.**  A
base-specifier-list of n entries gives an object n subobjects: `SemaEntity::bases`
places each at a byte of its own, 10.2p2's lookup asks each and refuses the name
two answer, 12.6.2p10 constructs in list order and 12.4p8 destroys in the
reverse.  10.1p3's repeated base is refused where the class is completed, which
is what makes every walk one visit per class.  A base at a byte of its own is a
step in both directions - 4.10p3 forward and 5.2.9p11's cast back - and 11.2p4's
access is asked in the region the conversion was *written* in.  12.6.2p2's index
is keyed by the whole class name a mem-initializer-id wrote, and 14.6.2p3 is a
fact of each specifier rather than of the clause.

**14.7.1p1 leaves a static data member's storage to whatever reaches it** - a
name not folded away, an object of the class (`demand_object_storage`, one walk
per class), or 14.7.3p1's own `template<>`.  3.9p7's incomplete array is
completed by the definition of the object, both ways about.  8.5.2's string
literal initializing an array of character type is an initialization and not a
conversion (`sema_string_init.h`), read the same way from a declaration, a
clause and 8.3.4p3's bound.  2.14's literal is one question two readers ask:
`CharacterLiterals` settles PA2's course dump and the language's own types
together, and `append_ordinary_units` is the one implementation of 2.14.5p5's
execution encoding.

## Current Failure Map

5 failing, grouped by what would fix them.

| group | n | owner |
| --- | --- | --- |
| a name a head has yet to settle: `value_type` written inside 14.5.1.3p1's out-of-class definition of `Deque<T>::block_size` "does not name a type", and `typename uint_for<Bits>::fast Poly` at a value place leaves `crc_kind<32, Poly>` an incomplete class an object is declared of | 2 | `sema_name.cpp`, `sema_template.cpp` |
| 14.2's `<` inside an argument spelling that is 5.9's: `I<R<A>::v < R<B>::v, B, A>` splits into one argument and reports `no declaration of I<R<A>::v<R<B>::v,B,A>::type` | 1 | `sema_name.cpp` |
| 8.5.1p2's aggregate built as an object of its own where a member is an array: 8.3.5p5 leaves `struct array { T e[N]; }` no by-value parameter list, so `T{item(0), item(1)}` has no constructor and the clauses have to initialize the subobjects where they stand | 1 | `sema_lifetime.cpp`, `sema_init_list.cpp` |
| `&function_template` as a constructor argument reaches a unary operator the PA15 lowering has no case for | 1 | `lowir_lower_expression.cpp` |

Outside the fixtures, the sweeps leave these shapes.  **The declarator** cannot
read 8.3.5p3's ellipsis written without a comma (`int a...`), refuses every call
of a variadic function template whose trailing pack no argument reaches, does not
strip 14.8.2.1p2's top-level cv-qualifier on P, and reads a `...` written inside
a *nested* declarator (`A (*... p)(int)`) as one place.  **Outside the subsets:**
15's try block, 5.5's `.*`, a template template parameter, a dependent array
bound in an argument spelling (`s<T[N]>`), a specialization's body naming its own
class, out-of-class member definitions of a partial specialization, a
decltype-specifier in a base-specifier (which the reference refuses too), 10.3p10's
base subobject that dispatches and does not begin where the object does, and
12.1's constructor over a function parameter pack of *no* elements.
**7.1.5's own edges:** 5.19p3 folds an object of *arithmetic* type alone, so a
named `constexpr` object of class type is no constant and `s.a` and `s.get()`
on one are refused; the spelling reading answers no member access at all, so
`pt{2,5}.sum()` and `p.y` written in an argument list are refused where the
tree reading takes both; a braced clause nested inside another
(`O{{1}, 2}`) is outside the spelling reading; 12.6.2p6's delegating
constructor initializes no member of its own and is refused, as the reference
refuses it; 12.6.2p8's brace-or-equal-initializer is not read, so a class with
one builds no object; 5.2.5p1's `->` has a pointer on its left, which no
constant expression here names; 8.5.4p3's narrowing is not refused, which the
reference does not refuse either; and a call of a function *template* is
refused because the specialization's body is not read until the end of the
unit - the last accepted by g++ and the reference both.  **The reference disagrees with g++ and this
compiler** on `arity=variadic` and a trailing `z` for every parameter clause
writing `specialization... name`; on four pattern shapes it refuses
(`pair_of<A, A>...`, `s<wrap<A>...>*`, `list<A, B...>...`, and such a head chosen
by a function-pointer target); on two argument lists that flatten alike but split
their runs differently; on `case 'ab' - 24672:` and a definition omitting a bound
an earlier declaration wrote; on 10.2p6's ambiguous inherited name and 11.2p4's
conversion to a private base outside every class that reaches it; on laying out
*every* static data member of a specialization one of whose members is named; on
a character ud-suffix it drops; and on 14.8.1p2's explicit list extending a
trailing run.  It also **crashes or hangs** where this compiler answers: a
constexpr chain 800 deep is a SIGSEGV there, and `fib(40)` a timeout.
**Metadata and shapes the comparison ignores:** 14.5.5p1's partial
specialization of a *function* template is not refused; 14.7.3's explicit
specialization is `binding=weak` where the reference writes `strong`; an unnamed
enumeration through a `decltype` is mangled `__anonymous_enum1` and not `Ut_`; an
array shorter than its bound writes one `zero n` run; `v.a::n` writes a
`base_subobject` step of offset zero; and an array of a class with a base at
namespace scope leaves no empty `role=init` function.

## Active Checkpoint

**C13 - the two names a spelling this reading resolves wrong leaves behind.**
Selected because the name layer owns three of the five remaining failures and
the other two are singletons of different owners.

- **Owner.**  `sema_name.cpp` for the split and the lookup, with
  `sema_template.cpp` for what a substitution settles.
- **Data flow.**  `opens_template_arguments` decides whether a `<` written after
  a name opens 14.2's list; where the run it would close holds a second `<` that
  is 5.9's operator (`R<A>::v < R<B>::v`), the balanced scan closes at the wrong
  `>` and the whole argument becomes one word.  The two lookups are the other
  half: 3.4.3p3 makes the class's own members visible inside an out-of-class
  member definition, which is where `value_type` should be found, and 14.6.2p1's
  stand-in for `typename uint_for<Bits>::fast` has to be settled by the
  substitution before the place's type says whether `crc_kind<32, Poly>` is
  complete.
- **Expected complexity.**  One scan per spelling, kept per spelling as the
  split already is, and one lookup per name - no reading is repeated.
- **Validation.**  The three fixtures; a sweep of `<` and `>` written at every
  depth of an argument spelling and of names written inside an out-of-class
  member definition, against g++ and `pa20/cppgm++-ref`; scaling rows for a
  spelling with 4096 relational operators and for 4096 out-of-class definitions;
  and a valgrind run of each.

## Performance Model

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.004 s**, so a row below is the shape's own cost.  A
harness that spawns a process of its own per run reads this machine's floor as
0.11 s; it is not one, and the `pa20/cppgm++-ref` wrapper adds ~0.6 s of its own
before the reference binary starts, which is subtracted from its column.  Rows
are re-measured whenever the checkpoint touches the path they time; the ones
below were all taken on this turn's build.

| shape | here | `pa20/cppgm++-ref` |
| --- | --- | --- |
| a constexpr chain 200 / 800 / 2040 deep | 0.005 / 0.010 / **0.019 s** | SIGSEGV at 800 |
| `fib(40)`, exponential without the memo | **0.004 s** | >30 s |
| 256 / 1024 / 4096 folded calls over 8 distinct argument lists | 0.009 / 0.025 / **0.096 s** | - |
| 256 / 1024 / 4096 class prvalues at a value place | 0.022 / 0.080 / **0.388 s** | - |
| an object of 64 / 256 / 1024 members folded once | 0.005 / 0.006 / **0.012 s** | - |
| 256 / 1024 / 4096 objects a constexpr constructor builds | 0.023 / 0.089 / **0.421 s** | 13.1 s at 4096 |
| one such object written 4096 times | **0.115 s** | - |
| an object of 1024 / 4096 / 8192 members a constructor builds | 0.025 / 0.095 / **0.204 s** | 1.92 s at 8192 |
| 256 / 1024 / 4096 calls filling two default-arguments | 0.012 / 0.038 / **0.146 s** | 1.11 s at 4096 |
| a constexpr chain deeper than the guard (12000) | refused in **0.03 s** | SIGSEGV |
| 512 / 2048 / 8192 distinct `decltype` spellings in argument lists | 0.018 / 0.069 / **0.300 s** | 9.7 s at 8192 |
| 256 / 1024 / 4096 `decltype` prefixes in one template definition | 0.021 / 0.082 / **0.410 s** | 23.9 s at 4096 |
| a pack of 4096 elements bound and counted | **0.018 s** | 0.159 s |
| `fac<800>` metafunction chain | **0.037 s** | 0.167 s |
| 256 patterns against 2048 distinct lists | **0.063 s** | 11.1 s |
| an expansion of 1 / 64 / 512 / 2048 in an array's clause list | 0.004 / 0.005 / 0.015 / **0.051 s** | 1.30 s at 2048 |
| a specialization pattern deduced over a run of 1 / 64 / 512 / 2048 | 0.004 / 0.006 / 0.017 / **0.054 s** | 0.004 / 0.022 / 0.149 / 0.720 s |
| the same clause read per element from a settled head at 2048 | **0.082 s** | - |
| 64 / 256 / 1024 inner expansions inside one spelled pattern | 0.006 / 0.013 / **0.043 s** | 1.87 s at 1024 |
| an expansion nested 4 / 8 / 16 / 24 heads deep, each counting its run | 0.004 / 0.006 / 0.010 / **0.019 s** | >300 s at 24 |
| 256 / 1024 / 4096 calls deducing two runs in one head | 0.065 / 0.276 / **1.221 s** | 0.55 / 4.2 s |
| 100 / 400 / 800 / 1600 levels each adding a second base | 0.011 / 0.049 / 0.154 / **0.584 s** | 0.10 s at 800 |
| a base pack of 64 / 256 / 1024 elements, each initialized | 0.012 / 0.037 / **0.165 s** | 14.3 s at 1024 |
| 400 / 1600 / 6400 conversions through a 200-deep derivation | 0.049 / 0.166 / **0.646 s** | 2.20 s at 6400 |
| 4096 objects of a class with a 64 / 512-deep base chain | 0.117 / **0.139 s** | - |
| 512 / 2048 / 8192 arrays initialized by a string literal | 0.018 / 0.064 / **0.268 s** | 1.60 s at 8192 |
| 512 / 2048 / 8192 distinct multicharacter literals | 0.008 / 0.018 / **0.058 s** | 0.649 s at 8192 |
| an aggregate of 2^15 / 2^17 subobjects | 1.483 / **6.713 s** | - |
| 14.5.3p4's recursion over a pack of 1024 | **1.560 s** | 9.3 s |

The one shape whose cost is worse than linear and is recorded as such: 10.1p3's
own check is quadratic in a derivation that adds a base per level, because every
level asks the whole class below it whether the base it adds is repeated.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 14.1p4's non-type parameter and 14.3.2's integral argument: `TypeKind::Value`, `TemplateInfo::Parameter` as a place, 14.6.1p1's region opened once, 5.19 read out of the argument spelling, 7.1.5p9's constexpr object, and 10p1's settled base completed inside 14.6p8's reading | 39 -> **85 / 164** |
| C2 | 14.7.3's explicit specialization: a `template<>` head declaring the specialization and no template, both bodies keyed by the interned argument list | 85 -> **92 / 164**; `fac<200>` SIGSEGV -> 0.01 s |
| C1, C2 audit | the spelling a value argument arrives as: 14.2's `<` told from 5.9's and 5.8's, 3.4.3p1's rooted name, 4.12p1's conversion, 5.2.3's notation, 8.5p16/8.5.4p3's initializers, 14.6p8's count put back by a discarded probe | 92 -> **103 / 169**, five fixtures |
| C3a | 14.5.3's place and run at the class tier: `TypeKind::Pack` both ways, `pack_place`, one reading per element, 5.3.3p5's `sizeof...`, 14.5.3p4's base-specifier pattern | 103 -> **108 / 169** |
| C3b | the function tier: 14.8.2.1p1 deducing a trailing `P...`, `deduced_arguments` splicing it flat, 8.3.5p3's ellipsis told from an expansion, one place per element under 8.3.5p10's names | 108 -> **118 / 169** |
| C3c | 14.5.3p4 in a call's argument list, read over the tree; explicit lists counted by the pack place; 14.3.2p1 refusing a pack of types at a non-type place | 118 -> **123 / 169**; linear at 512 |
| C3 audit | the two kinds of settled pack and the list the object file writes: one element region for a spelling and a tree alike, a run of no elements, 14.5.3's `J...E` and `Dp` | 123 -> **127 / 172**, three fixtures; linear at 4096 |
| C4 | 14.8.2 given its own owner (`sema_deduce.h`): a specialization P matched as an argument list, 14.8.2.1p3's derived A, 14.8.2.5p5's non-deduced context, 14.1p9's default, 14.6.2p1's dependent member; `user_types_` a deque | 127 -> **133 / 172** |
| C4 audit | the list a use is chosen from and the one the object file writes: 8.3.5p1's parameter list matched by 14.2's rule, 14.1p9's default read in a region binding the earlier places, 14.5.6.1p5's signature and 14.8.2.4p9's ordering, `X <expression> E` | 133 -> **136 / 175**, three fixtures; 71 shapes agree with g++ |
| C5 | 14.5.5's pattern and 14.5.1p1's variable template given one owner (`sema_specialize.h`): a pattern held beside the primary, 14.5.5.1p1 as `match_arguments` memoised per template, 14.5.5.2p1's ordering, 14.5.6.1p5's signature | 136 -> **145 / 178**, three fixtures; 48 shapes agree with g++ |
| C5 audit | what a pattern this milestone could not read leaves behind: three exits let the *primary* answer for every list, and 14.5.1p1's self-naming specialization ran the stack out | 145 -> **147 / 180**, two fixtures |
| C6 | a decltype an argument list wrote, and the prefix it stands before: `AstArena::keep_spelled`, `decltype_type` as the one reading, `require_settled_type`; `sema_type_id.cpp` became `SpelledTypeId`, freeing 19 header lines | 147 -> **156 / 182**, two fixtures; 49 shapes swept; linear at 8192 |
| C6 audit | the demand a prefix makes, at all three walks and in all three modes: `qualified_in_type`, `resolve_prefix`'s decltype branch, `id_constant` asking one implementation, the arena travelling through `emit_translation_units`, 14.6.1p1 binding a value place as a value | 156 -> **158 / 184**, two fixtures; 134 shapes |
| C7 | 14.5.3p4 in every list a program writes one into: `WrittenList` as the one owner, `InitializerClauses` with 8.5.1p11's cursor, `array_from_clauses`, 5.2.3p2's demand for a complete type | 158 -> **162 / 184**; 20 shapes; linear at 2048; valgrind clean |
| C7 audit | a list of *one* entry is a list too: five readers took `children[0]`, the arity none asked, and 5.19's own reading of 5.2.3; `fold_constant_object` split out | 162 -> **164 / 186**, two fixtures; 88 shapes |
| C8 | 5.19 outside the integral subset: 2.14.3p1's multicharacter literal, 5.19p2's string subobject, `sizeof...` out of a spelling, 3.9p7's incomplete array, 2.14.8p3's literal operator template; `sema_enum.cpp` split out | 164 -> **176 / 191**, five fixtures; 80 shapes; linear at 8192 |
| C8 audit | the dialect a character-literal is read in: `CharacterLiterals` settling both facts, `append_ordinary_units` as one implementation, `literal_value` with 3.9.1p1's sign, 5.1.1p6's parentheses, the template operator asked first | 176 -> **178 / 193**, two fixtures; 112 shapes |
| C9 | 10p1's base-specifier-list of more than one entry: `bases` as lists, one subobject per entry, 10.2p2, 12.6.2p10 and 12.4p8, the ctor-initializer's expansion, 10.1p3's refusal, `__vmi_class_type_info`; `sema_string_init.h` and `sema_derivation.h` split out | 178 -> **185 / 196**, three fixtures; 38 shapes; linear at 1600 classes |
| C9 audit | the byte a base subobject stands at, at every reader that had only seen zero: 5.2.9p11's cast back, 11.2p4 asked in the written region, 10.3p1's real test, 12.6.2p2's key, 14.6.2p3 per specifier | 185 -> **189 / 200**, four fixtures; 89 shapes |
| C10 | 14.1p11's *second* place binding a run: `place_argument` and `trailing_pack_place`, 14.5.3p5's inner expansion, 14.8.1p2's explicit fill, the type a value argument carries, and 14.7.1p1's storage left to whatever reaches it | 189 -> **196 / 203**, three fixtures; 50 shapes; linear at 4096 |
| C10 audit | the packs a pattern is written over, at both readings and for the two nodes that already expanded one; 3.2p3's demand made at 9.2p1's member; 14.3.2p1 asked of the argument each element is | 196 -> **201 / 208**, five fixtures; 52 shapes; every `.ref` regenerated and unmoved |
| C11 | 14.5.3p4's pattern that is a class template specialization: `PackReading::read_places` as the fourth shape of the one reading, and a substitution binding the pattern's places one element at a time | 201 -> **204 / 211**, three fixtures; 44 shapes; linear at 2048 |
| C11 audit | the two things a *spelling* writes inside one node: `list<A, B...>` and `pair2<A, sizeof...(B)>` carry their own `...` and `sizeof...` in the text of one terminal, so `names_in` reads each node's text with `spelled_names_in` | 204 -> **205 / 212**, one fixture; linear at 2048 |
| C12 | 5.19p2's call of a constexpr function, and the object of literal class type one may be called on: `sema_constexpr.h` as the owner of 5.19's three non-arithmetic operands, 7.1.5p3's body re-read in a region of its own binding the places and 9.2p1's members, the answer keyed by the callee and `type_list` of the converted arguments in `SemaModel::folded_call`, 5.2.3p2/p3's object interned as the list of its subobjects, 12.3.2p1's conversion function at 14.3.2p5, 7.1.5p2's implicit `inline` with `demand_referenced` stopping at a call the image folded, and `{`/`}` counted as a balanced run so `A<S{1, 2}>` is one argument | 205 / 212 -> **212 / 217** with five fixtures added, four compile-pass and one refusing; pa1-pa19 2169 / 2169; 25 constexpr shapes swept against g++ and the reference, every accepted pair writing the reference's own LowIR but the two it accepts and this milestone does not; every checked-in `.ref` regenerated from the reference binary and unmoved; linear at a chain of 2040, 4096 folded calls and 4096 class prvalues, `fib(40)` 0.004 s where the reference times out and g++ takes 7.9 s, a chain past the guard refused rather than crashed; valgrind clean |
| C12 audit | the object a constant expression *builds*, which C12 gave one initialization and every class type: 8.5.1p1's aggregate told from 8.5p16's direct-initialization, 7.1.5p1 read off a constructor, 12.6.2's mem-initializers in declaration order under a call's own key, 7.1.5p4's uninitialized member and non-empty body refused, 8.3.6p1's default-argument in the one list a fold reads, 13.3.3p1's ambiguous conversion refused rather than guessed at, 5.2.5p1 given a reading so an object's member can be read at all, and an object never carried where a value is meant; 12.6.2p2's index built once rather than scanned per member | 212 / 217 -> **215 / 220**, three fixtures, two compile-pass and one refusing; pa1-pa19 2169 / 2169; 41 constexpr shapes swept against g++ and `pa20/cppgm++-ref`, every shape all three accept coming to the same value here as there - the two-unit program among them; every checked-in `.ref` regenerated and unmoved; linear at 4096 constructor-built objects, 4096 default-argument folds and 8192 members, with the per-member scan's 1.400 s at 8192 down to 0.204 s; valgrind clean |
