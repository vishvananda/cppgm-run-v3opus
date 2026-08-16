# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 **passes: 230 / 230** - all 174 checked-in fixtures and all 56 under
`cppgm.tests/course/pa20` - with pa1-pa19 at **2169 / 2169** and the file audit
passing with the five header-weight warnings it inherited.  Every `.ref` was
regenerated from `pa20/cppgm++-ref` through `make -C pa20 ref-test` on the final
audit turn and none moved, and the two that turn added were generated the same
way, so every fixture holds the reference's own answer and not this compiler's.

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

**3.4.1p8 puts a definition's names where its declarator-id reaches.**  The
rest of a declarator whose declarator-id is qualified is already read there;
the initializer stands after the declarator-id too, so it is read there as
well - which is what lets `const long D::n = sizeof(value_type *);` name what
`D` declares.  The line the definition writes still stands where it was
written.

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
places before it, because 5.19 is evaluated where it stands.  So is the *type*
a value place declares, which may name the places before it
(`template<class T, T v>`): `place_type` answers it with their arguments
substituted in, over a `TemplateInfo`'s entries and over a function template's
places alike - those being declarations of their own rather than entries.

**A run that is not the last place is one entry of the list.**  14.1p11 leaves
a class template's pack last, but 14.8.2 deduces a function template's head
place by place, so `template<class... U, class... T>` needs `<char, short|int>`
told from `<char|short, int>`: the last place's run is written flat and every
earlier run stands as one `Pack` entry (`place_argument`,
`trailing_pack_place`).  The object file writes either `J...E` per run, with a
place written `P...` encoded `Dp` of its pattern, and an argument no
substitution settled written `X <expression> E`.  A pack the list stopped short
of is bound to no arguments at all, and that is the same answer however the
reading arrives at the place: 14.1p11 lets a place written before the pack carry
14.1p9's own default, so filling those is a second way to reach it and `S<>`
over `template<int N = 5, class... T>` binds N to 5 and the pack to nothing.

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

**That question is a fact of the whole spelling, not of the character before
the `<`.**  PA10 flattens a name into the terminals the parse matched and drops
the spaces, so `I<R<A>::v < R<B>::v, B, A>` arrives with nothing left to tell
the two apart one character at a time.  `AngleReading` (`sema_name.h`) settles
it from what the spelling still holds: a name writes no `>` that closes
nothing, so a scan opening a run at every candidate and ending with runs still
open has exactly that many of 5.9's operators among them.  Two backward passes
over the spelling say where the run enclosing each offset closes and where a
scan with no run open still reads to the end, and one forward walk replays the
decisions from the level it stands at - a name's outermost `<` always opens, an
argument's may not.  It costs the one scan the readers were making anyway
wherever the runs do close, which is nearly every name.  A run handed on out of
one - 14.2's argument - is respelled with the separator phase 7 wrote, because
the `>` that settled the question is no longer in it.  Being a fact of the whole
spelling, it is *made* once per spelling: `AngleReading::balanced_end` is the
per-run question the splitters ask of that one reading, and both
`split_type_id` and `split_value_expression` make it where they begin their
walk rather than at each run of it.  What a spelling *names* is the same fact
made once: 14.1p4 declares a place under an identifier, so 14.6.1p1's question
about a lone word is asked only of one, and a word that is a qualified name or
a template-id is read rather than looked up first and read after.

**The spelling PA10 hands on is a substring of the whole stream written out.**
Whether a separator stands between two terminals is a fact of those two and of
the one after them (`AstTokenStream::needs_separator`), and of nothing else -
the range being spelled does not enter into it.  So it is settled once per
terminal where the stream is built and `flatten` is a copy of what the range's
own terminals occupy.  That matters because the parse spells every candidate
name it *tries*: an ordered choice reads `W<0>::v` and then reads it again as
the name a list opens on, and a grammar with full backtracking does that at
every position.  Beside it, `parse_template_argument` is the question
`skip_simple_template_id` memoises one level up, remembered the same way and
under the same version of the names in scope - so a list read once per attempt
at the name in front of it costs one lookup per argument rather than one
re-reading of the suffix, with the nodes of it built again.

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
`member_named`, which is the expression layer's own lookup.  That access is
asked of the *object and the name* (`member_value`, `member_call`), which is all
a spelling holds, so 5.19's two readings write it once between them: the tree
reading resolves the two out of its nodes and the reading over 14.2's words
takes `.` as the postfix operator it is.

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
is what makes every walk one visit per class - and what that walk reached is a
number on the class (`SemaEntity::reached_at`, `SemaModel::next_reach`) rather
than a table it builds and throws away, because the walk is made once per class
and a derivation adding a base at every level makes one per level.  A base at a
byte of its own is a
step in both directions - 4.10p3 forward and 5.2.9p11's cast back - and 11.2p4's
access is asked in the region the conversion was *written* in.  12.6.2p2's index
is keyed by the whole class name a mem-initializer-id wrote, and 14.6.2p3 is a
fact of each specifier rather than of the clause.

**8.3.5p5 leaves one place no declaration could write.**  8.5.1p2's constructor
of an aggregate takes its members by value, and an array is the one member no
by-value parameter carries - the adjustment leaves a pointer, and the pointer
holds no elements.  So that place is the array itself (`member_constructor`):
the object file writes its declared type (`A2_S0_`), the boundary carries the
address of an array object of the caller's (`ptr [pass=decay]`), and the callee
copies the whole of it into the member.  The argument is an object rather than a
subobject of the aggregate - the clauses reach its elements from the one base
its storage was named by (`array_argument` at both layers, `$argarr`) - and
which clauses reach it is 8.5.1p11's own question, braces written, left out, or
8.5.2p1's string literal in their place.  `decayed_arrays_` is what says a place
holds an address rather than the elements, so the one member-initialization that
reads one writes 12.8p15's copy and not 8.5.1's walk.

**13.4p1's target chooses at both kinds of target.**  `&f` naming an overload
set travels up under the `&` the program wrote, and the line goes on standing
for that operator however late `resolve_target` settles what it is taken of.
14.8.2.2p1's deduction is over the one A the target is: the function type where
that target is a pointer or a reference, and - where 8.3.3p1's pointer to member
is the target, which says which member of a class it names rather than being a
function type - the type it points to with 9.3.1p3's object parameter of the
candidate put back in front (`member_target`), checked by the same
`member_pointer_of` the non-template declarations are compared with.

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

0 failing: every checked-in fixture and every course test passes.

Outside the fixtures, the sweeps leave these shapes.  **The declarator** cannot
read 8.3.5p3's ellipsis written without a comma (`int a...`), refuses every call
of a variadic function template whose trailing pack no argument reaches, does not
strip 14.8.2.1p2's top-level cv-qualifier on P, and reads a `...` written inside
a *nested* declarator (`A (*... p)(int)`) as one place.  **PA10's parse** reads
`sizeof(value_type)` written in an out-of-class member definition as 5.3.3's
expression, because the name is a type only in the region the declarator-id
names - so the operand has to be a type the enclosing region also declares,
which `sizeof(value_type *)` is and `sizeof(E)` and a namespace's own
`sizeof(ty)` are not.
**The flattening**
loses a `<` no `>` can put back wherever a template-argument-list holds a
template-id whose *own* argument holds 5.9's operator: `K<J<a < b> >`,
`Nm<A<a < b>::n>::n` and `P<A<a < b>, A<b < a> >` each flatten to a spelling
both readings balance, and only a lookup of the inner name tells them apart, so
the inner `<` is read as 14.2's list and the reference reads all three where
this compiler refuses them.  PA10's own parse refuses `I<R<A>::v < 5>` and five
neighbours outright, which the reference refuses too.  **Outside the
subsets:**
15's try block, 5.5's `.*`, a template template parameter, a dependent array
bound in an argument spelling (`s<T[N]>`), a specialization's body naming its own
class, out-of-class member definitions of a partial specialization, a
decltype-specifier in a base-specifier (which the reference refuses too), 10.3p10's
base subobject that dispatches and does not begin where the object does, and
12.1's constructor over a function parameter pack of *no* elements.
**10.1p3's second subobject:** a class that would hold two subobjects of one
base (`struct D : M1, M2` where both derive from `B`) is refused rather than
laid out, because nothing here tells two subobjects of one class apart - a
conversion names the class and carries one offset - and that refusal is what
makes every walk of a derivation one visit per class.  g++ and the reference
both lay it out.
**7.1.5's own edges:** 5.19p3 folds an object of *arithmetic* type alone, so a
named `constexpr` object of class type is no constant and `s.a` and `s.get()`
on one are refused; a braced clause nested inside another written *in a
spelling* (`O{{1}, 2}`) is one the split does not enter, though the same
clause written at a declaration is read; a fold whose object is a temporary is
carried on no resolved call node, so `const int n = pt{2,5}.sum();` at
namespace scope is dynamically initialized where `f(3)` is written into the
image and the reference writes 7 - the value is still a constant expression
everywhere one is asked for, which is what `int a[n]` and `W<n>` read;
12.6.2p6's delegating
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
their runs differently; on `Tn <type>` written before every non-type argument a
*function* template's list carries (`_Z1fIcTnT_Lc66EEiv` there against g++'s and
this compiler's `_Z1fIcLc66EEiv`), which it does not write at the class tier;
on `case 'ab' - 24672:` and a definition omitting a bound
an earlier declaration wrote; on 10.2p6's ambiguous inherited name and 11.2p4's
conversion to a private base outside every class that reaches it; on laying out
*every* static data member of a specialization one of whose members is named; on
a character ud-suffix it drops; and on 14.8.1p2's explicit list extending a
trailing run.  14.7.3p6 leaves an explicit specialization written *after* the
use that instantiated it ill formed with no diagnostic required: g++ diagnoses
it, the reference silently re-reads the specialization for the instantiation
already made - `int a = S<int>::n;` before `template<> struct S<int>` is 9 there
and 1 here, and an object of a class specialized after it is laid out to the
specialization's size - and this compiler leaves the primary's reading standing.
Every *well formed* late visibility this milestone owes is answered: a
specialization seen before 14.6.4.1's point of instantiation, one declared early
and defined late, and 14.7.1p1's stale primary the checked-in fixture pins.
It also **duplicates a weak global across units**: two units each naming
`Arr<int>::t` leave two `global @Arr_int___t` definitions in one LowIR file
there and one here, where the same two units naming one inline function leave
one in both.  It also **crashes or hangs** where this compiler answers: a
constexpr chain 800 deep is a SIGSEGV there, `fib(40)` a timeout, and a
derivation adding a base at every level is 40.8 s at 800 levels against 0.09 s
here.
**Metadata and shapes the comparison ignores:** 14.5.5p1's partial
specialization of a *function* template is not refused; 14.7.3's explicit
specialization is `binding=weak` where the reference writes `strong`; an unnamed
enumeration through a `decltype` is mangled `__anonymous_enum1` and not `Ut_`; an
array shorter than its bound writes one `zero n` run; `v.a::n` writes a
`base_subobject` step of offset zero; an array of a class with a base at
namespace scope leaves no empty `role=init` function; 3.5p3's const object at
namespace scope that an earlier declaration wrote `extern` is written
`binding=internal` and mangled `_ZL1a` where the reference writes external
linkage; and a name a fold answered, written beside a literal in a longer unit,
stands as the value itself where the reference writes a `copy` of it into a
temporary first - a shape no fixture pins and none of the probes reproduce on
its own; and an unsettled non-type argument that is *compound* is written as the
type of the place it names rather than as `X <expression> E`, so
`g(A<sizeof(T) < 4>)` is `1AIT_E` here where g++ and the reference both write
`1AIXltstT_Li4EEE` - the bare `S<N>` the C4 audit fixed being the only shape
`expression_of` (`lowir_abi.cpp`) has a record for.
**8.3.3p1's pointer to member is no object here:** `low_type` writes `void`
where the reference writes `i128` and the address it holds through a
`copy i64` and a `zext`, so `void (s::*p)(int) = &s::f;` compiles to a `void`
slot - the same for a template `f` now that `member_target` chooses one - and
5.5's `.*` is outside the subset above it.  **12.2p3 is still unmarked:** an
aggregate at a value place whose members have a non-trivial destructor is
destroyed by neither compiler, and the reference writes an empty
`eh_try`/`resume` region around the full-expression where this one writes none -
which is a fact of the class's members and not of the array member C14 added.
**The reference passes a class by address wherever a *floating* member is in
it** and the object is written as a braced prvalue argument (`struct { double
d; }` is `ptr [pass=by_address]` there and `obj<8x8>` here), while the same
class declared and then passed agrees; and it gives `void (*give())(int)` the
returned pointer's own parameter, writing a `@give` its own call does not
match.

## Active Checkpoint

**None - the assignment passes and the final audit is done.**  The next
checkpoint the sweeps point at is **C15, 8.3.3p1's pointer to member as an
object**: `low_type` has no case for one, so `void (s::*p)(int) = &s::f;` writes
a `void` slot where the reference writes `i128` with the address `copy i64`'d
and zero-extended into it; the declarator refuses a *dependent* one outright
(`int (T::*p)(int)` in a function template's parameter clause is "T is written
before `::*` and does not name a class", where g++ and the reference deduce it);
and 5.5's `.*` is the reader above them that would then have something to read.
Its owner is `lowir_lower.cpp` for the representation, `decl_parser_declarator.cpp`
for the dependent prefix, the conversion layer for the address that becomes one,
and `sema_expression.cpp` for `.*`; the validation is the same three oracles over
a member pointer declared, passed, compared, value-initialized and called through.

## Performance Model

Best of five, `-O0`, timed by the shell around the process itself, every row
measured on this turn's build and every shape generated by one script so the
sizes of a row are comparable with each other.  An empty translation unit is
**0.004 s**, so a row is the shape's own cost.  A harness that spawns a process
of its own per run reads this machine's floor as 0.11 s; it is not one.  The
`pa20/cppgm++-ref` column is best of three through the wrapper minus the
**0.534 s** the wrapper costs before the reference binary starts, and it carries
the reference's verdict where that is not "compiled": several of the numbers this
table carried forward were wrong by two orders of magnitude in both directions,
so none is carried forward now.

| shape | here | `pa20/cppgm++-ref` |
| --- | --- | --- |
| a constexpr chain 200 / 800 / 2040 deep | 0.005 / 0.010 / **0.019 s** | SIGSEGV at 800 |
| a constexpr chain deeper than the guard (12000) | refused in **0.030 s** | SIGSEGV |
| `fib(40)`, exponential without the memo | **0.004 s** | >60 s |
| 256 / 1024 / 4096 folded calls over 8 distinct argument lists | 0.009 / 0.025 / **0.094 s** | 0.473 s at 4096 |
| 256 / 1024 / 4096 class prvalues folded through a member call | 0.014 / 0.032 / **0.110 s** | 0.774 s at 4096 |
| an object of 64 / 256 / 1024 members folded once | 0.004 / 0.006 / **0.014 s** | 0.173 s at 1024 |
| 256 / 1024 / 4096 objects a constexpr constructor builds | 0.015 / 0.034 / **0.117 s** | 0.774 s at 4096 |
| one such object written 4096 times | **0.109 s** | 0.674 s |
| an object of 1024 / 4096 / 8192 members a constructor builds | 0.020 / 0.074 / **0.177 s** | 1.275 s at 8192 |
| 256 / 1024 / 4096 calls filling two default-arguments | 0.014 / 0.034 / **0.119 s** | 0.674 s at 4096 |
| 512 / 2048 / 8192 distinct `decltype` spellings in argument lists | 0.024 / 0.090 / **0.393 s** | 6.78 s at 8192 |
| 256 / 1024 / 4096 `decltype` prefixes in one template definition | 0.009 / 0.024 / **0.092 s** | 10.29 s at 4096 |
| a pack of 4096 elements bound and counted | **0.012 s** | 0.174 s |
| `fac<800>` metafunction chain | **0.040 s** | 0.173 s |
| 256 patterns against 512 / 2048 namings | 0.038 / **0.071 s** | 0.574 s at 2048 |
| an expansion of 512 / 2048 in an array's clause list | 0.013 / **0.043 s** | 0.174 s at 2048 |
| a specialization pattern deduced over a run of 512 / 2048 | 0.005 / **0.008 s** | 0.173 s at 2048 |
| one clause read per element from a settled head at 2048 | **0.020 s** | 16.89 s |
| 64 / 256 / 1024 inner expansions inside one spelled pattern | 0.005 / 0.007 / **0.017 s** | 0.574 s at 1024 |
| an expansion nested 8 / 16 / 24 heads deep, each counting its run | 0.004 / 0.005 / **0.006 s** | 0.073 s at 24 |
| 256 / 1024 / 4096 calls deducing two runs in one head | 0.015 / 0.048 / **0.190 s** | 1.375 s at 4096 |
| **100 / 200 / 400 / 800 / 1600 / 3200 levels each adding a second base** | 0.011 / 0.019 / 0.038 / 0.091 / 0.262 / **1.099 s** | 38.9 s at 800 |
| a base pack of 64 / 256 / 1024 elements, each initialized | 0.010 / 0.031 / **0.134 s** | 3.98 s at 1024 |
| 400 / 1600 / 6400 conversions through a 200-deep derivation | 0.042 / 0.134 / **0.517 s** | 8.79 s at 6400 |
| 4096 objects of a class with a 64 / 512-deep base chain | 0.079 / **0.100 s** | 51.6 s at 512 |
| 512 / 2048 / 8192 arrays initialized by a string literal | 0.017 / 0.060 / **0.247 s** | 1.88 s at 8192 |
| 512 / 2048 / 8192 distinct three-character literals | 0.030 / 0.124 / **0.624 s** | 3.08 s at 8192 |
| an aggregate of 2^15 / 2^17 members | 0.482 / **2.290 s** | >120 s at 2^17 |
| **14.5.3p4's recursion over a pack of 256 / 1024** | 0.214 / **1.652 s** | 1.89 s at 256, SIGSEGV at 1024 |
| an argument list of 256 / 1024 / 4096 of 5.9's operators between literals | 0.005 / 0.009 / **0.026 s** | 0.274 s at 4096 |
| the same list with no operator in it, which the reading skips | 0.005 / 0.008 / **0.019 s** | 0.173 s at 4096 |
| the same list of 256 / 1024 / 4096 template-ids and no operator | 0.013 / 0.044 / **0.214 s** | 0.674 s at 4096 |
| **the same list of 256 / 1024 / 4096 operators between template-ids** | 0.020 / 0.118 / **1.474 s** | >120 s at 1024 |
| one argument naming 1024 / 2048 / 4096 template-ids | 0.009 / 0.015 / **0.027 s** | 0.374 s at 4096 |
| the same with a distinct specialization each | 0.044 / 0.098 / **0.204 s** | 0.574 s at 4096 |
| **a value argument nested 24 / 256 / 1024 deep** | 0.004 / 0.012 / **0.102 s** | SIGSEGV at 1024 |
| 256 / 1024 / 4096 out-of-class member definitions | 0.013 / 0.043 / **0.175 s** | >120 s at 4096 |
| an aggregate whose array member has 256 / 1024 / 4096 / 16384 elements | 0.005 / 0.008 / 0.021 / **0.073 s** | 0.374 s at 16384 |
| 128 / 512 / 2048 distinct aggregates each with an array member | 0.011 / 0.036 / **0.152 s** | 0.674 s at 2048 |
| one such aggregate written 256 / 1024 / 4096 times | 0.010 / 0.029 / **0.112 s** | 0.674 s at 4096 |
| 128 / 512 / 2048 array members in one aggregate | 0.006 / 0.014 / **0.045 s** | 0.474 s at 2048 |
| 256 / 1024 / 4096 member accesses read out of argument spellings | 0.014 / 0.033 / **0.117 s** | 0.774 s at 4096 |
| 256 / 1024 / 4096 namings of a head with a default before its pack | 0.018 / 0.068 / **0.321 s** | 1.075 s at 4096 |

Four rows are worse than linear, and each is quadratic in a *fact* rather than
in a table that could be indexed away.

**10.1p3's check** is quadratic in the size of the derivation relation: a program
adding a base at every level states n^2/2 (class, ancestor) pairs, and the rule
is about exactly those pairs.  Marking the class rather than building a table per
walk halved the constant, and the reference is 38.9 s at 800 levels where this is
0.091 s.

**PA10's ordered choice** spells O(n) candidate names of O(n) terminals each in a
list writing 5.9's operator between two template-ids, because each `v` there
reads as a template-name whose own list runs to the end of the outer one.
Settling the separators once per terminal and memoising `parse_template_argument`
took 4096 from 26.6 s to 1.47 s; the residue is the characters those spellings
are, and the reference is past 120 s at 1024.

**14.5.3p4's own recursion** interns n lists of n entries over a pack of n, which
is what the metaprogram asks for; the reference SIGSEGVs at 1024 after 9.4 s.

**A nest of value arguments d deep** splits d spellings of length O(d), the
spelling of each level being the argument of the one above; the reference
SIGSEGVs at 1024 after 6.9 s.

## Architecture Review

The layers a PA20 fact travels through, and what each owns alone.

**PA10's parse is the syntax boundary and hands on two things.**  A tree for
everything the grammar resolved, and a *spelling* for everything it left - a
qualified-id, a template-id, a decltype-specifier - with the terminals the parse
matched and the spaces dropped.  The one operand a spelling cannot carry is an
expression, so the parse keeps the tree it read for each decltype it flattened
(`AstArena::keep_spelled`) and the analyzer borrows that table.  Nothing below
re-lexes: `AstTokenStream` writes the whole stream out once and every spelling is
a substring of it.

**PA11/PA12's model is the declaration layer, and PA20 adds facts to it rather
than a layer of its own.**  `SemaEntity`, `Scope` and `TypeTable` carry every
new fact - `TypeKind::Value` and `TypeKind::Pack` are type-table entries, an
argument list is `TypeTable::type_list` of them, a specialization is an entity
found by `(template, list)`, and a fold is held on the model under the very key
13.1 tells two overloads apart by.  Nothing about a template is a table beside
the model.

**Each rule of clause 14 has one owner, and the owner is the thing the rule
walks.**  `sema_template_head.cpp` reads 14.1's places and binds 14.2's list;
`sema_name.cpp` says what a `<` in a spelling is; `sema_type_id.cpp` and
`sema_value_expression.cpp` are the two readings of a spelling, one per kind of
argument; `sema_pack.h` owns 14.5.3 at all four shapes a list is written in;
`sema_deduce.h` owns 14.8.2's match and `sema_specialize.h` 14.5.5's, which is
the same match over a pattern; `sema_constexpr.h` owns the operands 5.19's
arithmetic cannot answer; `sema_derivation.h` owns 10p1's tree;
`sema_string_init.h` owns 8.5.2.  Where two readings meet one rule - 5.19 over a
tree and over a spelling, 14.5.3p4 over a spelling and a tree and a
parameter-declaration and a built type - the rule is written once and asked of
what both readings hold.

**Lowering reads the model and adds no PA20 representation.**  `lowir_lower*.cpp`
walk the instantiated declarations exactly as they walk written ones;
`lowir_abi.cpp` is the only place that knows an argument list has an encoding;
`lowir_text.cpp` serializes.  The README's handoff - "no PA20-specific output
representation beyond LowIR itself" - holds: `grep` finds no LowIR form this
milestone introduced.

**The tool boundary below is a scaffold and not a runtime.**  `lowir2cy86` and
`cy86` build and run a program, which is what this turn's three run-evidence
programs use; the LowIR the harness compares is validated structurally before
the comparison, so every shape in the sweeps is well-formed LowIR and not only
matching text.

## Final Architecture Review

The review this turn made was independent of the checkpoint ledger: the
architecture above was reconstructed from the source, and each claim it makes was
put to a probe rather than read off a checkpoint's own account of itself.

Four things held.  Ownership is real - every rule named above has one
implementation, and the two places the sweeps found a rule written at one reader
and missing at the other were 5.2.5p1's access and 14.1p9's default at a place
before the pack, both closed this turn.  The type table is the only place a
template fact lives.  Every `.ref` under `pa20/tests` regenerated from the
reference binary and did not move, so no fixture holds this compiler's own
answer.  And the LowIR surface is PA13's: no `--emit-lowir` form exists that
PA19 did not already write.

Three things the review changed, each a fact settled in one place and asked in
another.  The spelling of a token range was decided per range where it is a fact
of the terminals; `parse_template_argument` was re-read once per attempt at the
list around it; and 10.1p3's walk built a table per class where a number on the
class says the same thing.  Together they take the worst shape in the model from
27.35 s to 1.42 s and a 1600-level derivation from 0.56 s to 0.28 s, with no row
of the model slower than it was.

What the model still records as worse than linear is written under it, and each
now has a reason rather than a number: 10.1p3 is quadratic in the size of the
derivation *relation*, which is the fact it is about; PA10's ordered choice
spells O(n) candidate names of O(n) terminals each; 14.5.3p4's own recursion over
a pack of n interns n lists of n entries; and a nest of value arguments d deep
splits d spellings of length O(d).  None is a table that could be indexed away.

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
| C13 | what a `<` in a flattened spelling is, and where a definition written outside its region reads its names: `AngleReading` settling 14.2's list against 5.9's operator over the whole spelling and respelling the argument it hands on, 3.4.1p8 reading the initializer where the declarator-id reaches, and `place_type` given the function tier's places so `template<class T, T v>` and `typename u<B>::fast P` settle at both | 215 / 220 -> **222 / 224**, three fixtures fixed and four course tests added; pa1-pa19 2169 / 2169; 30 shapes swept against g++ and `pa20/cppgm++-ref`, seven of them refused by the reference too and one - `K<J<a < b> >` - a flattening both readings balance, which no spelling can tell apart and which failed before this too; linear at 4096 operators and 4096 out-of-class definitions, 0.06 s where the reference takes 35.9 s; valgrind clean; the sweep also left 3.5p3's `extern`-declared const object written `binding=internal` here and `strong` there |
| C13 audit | the reading a `<` is settled by is a fact of the whole spelling, and was *made* as though it were a fact of one run: `spelling_balanced_end` built one per `<`, so a spelling naming 4096 template-ids paid 4096 readings of the whole of it (0.53 s -> 0.02 s with `AngleReading::balanced_end` asked of the one reading the splitters make); and `template_argument_value` asked 14.6.1p1's question of *any* lone word by looking it up, which for `W<3>::v` is the whole reading of the name, so a nest of them doubled at every level - 43.46 s at depth 24 -> 0.00 s, and 0.42 s at depth 1024 against the reference's 7.01 s - now that only 2.11p1's identifier is asked about | 222 / 224 held, pa1-pa19 2169 / 2169; 66 shapes swept against g++ and `pa20/cppgm++-ref` across the `<`, `<=`, `<<` spellings, dependent and pack patterns, 3.4.1p8's seven readers and two units; every `.ref` regenerated and unmoved; valgrind clean; three shapes recorded rather than fixed - PA10's parse is quadratic in an operator written between two template-ids (27.35 s at 4096, the reference past 300 s), the reference alone writes `Tn <type>` before a function template's non-type argument, and the flattening's `K<J<a < b> >` is a class of spellings no reading of text can settle |
| C14 | the object 8.5.1p2's clauses build where 8.3.5p5 describes no place for it, and 13.4p1's target at both kinds of target: `member_constructor` giving an array member a place whose declared type is the array (`A2_S0_`) and whose boundary is `ptr [pass=decay]`, `array_argument` building the elements in an array object of the caller's (`$argarr`) out of the run of clauses 8.5.1p11 leaves it, `decayed_arrays_` telling the one member-initialization that reads such a place to write 12.8p15's copy, the `&` a line stands for kept where the target settles it late, and `member_target` deducing 14.8.2.2p1's specialization from a pointer-to-member target; `element_of` moved to `TypeTable` and `member_pointer_of` to `sema_facts.h`, freeing 12 header lines | 222 / 224 -> **228 / 228**, the assignment complete, with four course tests added; pa1-pa19 2169 / 2169; 54 shapes swept against g++ and `pa20/cppgm++-ref` through the harness's own comparison, 9 disagreeing - 8 of them the reference refusing what g++ and this compiler accept, and the ninth the reference giving `void (*give())(int)` a parameter its own call does not pass; three programs built through `lowir2cy86` and run, each returning what the source computes; linear at 16384 elements, 2048 aggregates, 4096 objects and 2048 array members; valgrind clean |
| final audit | the architecture reconstructed from the source and put to a probe rather than read off the ledger: `AstTokenStream` writing the whole stream out once so a range's spelling is a substring of it and not a re-reading of its terminals, `parse_template_argument` memoised the way `skip_simple_template_id` already was, 10.1p3's walk marking the class rather than building a table per walk, 14.1p9's default before a pack place leaving the pack a run of none, 5.2.5p1 asked of the object and the name so 5.19's two readings write it once between them, and 14.6p8's count put back at the third probe that throws a reading away | 228 / 228 -> **230 / 230** with two course tests added; pa1-pa19 2169 / 2169; 81 shapes swept against g++ and `pa20/cppgm++-ref` through the harness's own comparator, 5 of the disagreements fixed and 9 recorded; every `.ref` regenerated and unmoved; the whole performance model re-measured against a worktree build of the pre-audit commit with no row slower and three much faster - 4096 operators between template-ids 26.6 s -> 1.47 s, 1600 levels of derivation 0.56 s -> 0.26 s, a value argument 1024 deep 0.42 s -> 0.10 s; valgrind clean over 232 inputs; three programs built through `lowir2cy86` and run, each returning what the source computes |
