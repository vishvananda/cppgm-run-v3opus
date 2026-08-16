# PA20 Plan — `cppgm++ --emit-lowir` compile-time metaprogramming

PA20 stands at **205 / 212** - 159 of the 166 checked-in fixtures and all 46
under `cppgm.tests/course/pa20` - from a turn-start baseline of **204 / 211**,
with pa1-pa19 at **2169 / 2169** and the file audit passing with the five
header-weight warnings it inherited.  Every `.ref` was regenerated from
`pa20/cppgm++-ref` through `make -C pa20 ref-test` on this turn and none moved,
so every fixture holds the reference's own answer and not this compiler's.

The milestone gives PA19's template tier two things its argument list did not
have: 14.3.2p1's argument at a non-type place, which is a *value*, and
14.5.3p1's place that binds a *run* of arguments rather than one.  Both are
type-table entries, because 14.4p1 makes an argument list what tells two
specializations apart and every fact the tier keys - the specialization, the
substitution, the object-file name - reads that list as `std::vector<TypeId>`.

## Stage Design

**A value argument is a type-table entry.**  `TypeKind::Value`
(`type_model.h`) holds the type the argument was converted to and the bits it
holds, interned like a pointer or an array: `value_type(int, 3)` is one entry
however many times `f<3>` is written.  `is_dependent` asks only its type - the
bits are settled - and `substitute` rebuilds it for `template<class T, T v>`.

**A pack is the same kind of entry, twice.**  `TypeKind::Pack` is either a
*run* - interned by its elements, which is what an argument list bound to a
pack place - or 14.5.3p4's *expansion* `P...`, interned by its pattern, which
is what stands in a list until the run arrives.  `is_dependent` is true for
every expansion and for a run holding a dependent element, so a substitution
reaches both.  Nothing declares an object of either.

**14.1p4 and 14.1p11's places say what each takes.**
`TemplateInfo::Parameter` (`sema_template.h`) is a place rather than a name:
the name its head wrote, whether it binds a value, whether it binds a run, the
syntax that says what type that value has, and the type standing for the place.
`open_parameter_region` opens 14.6.1p1's region once per template and settles
them there.  `pack_place` is what tells a written argument which place it fills;
a function template's head is read by the ordinary declaration path instead, so
`function_pack_place` asks the same question of its declarations.  14.1p3's
unnamed place is declared there too, because 14.1p9's default is the *place's*
fact and not the name's.

**5.19 is read out of the spelling, like 8.1p1's type-id is.**
`sema_value_expression.cpp` is to a value argument what `sema_type_id.cpp` is to
a type argument: 14.2 writes the argument list inside a name, so it arrives as
text.  The terminals are *recovered* rather than re-lexed, the split is kept per
spelling, and what a `<` in such a spelling *is* - 14.2's list or 5.9's
operator - is one question with one answer (`sema_name.cpp`).  Each is a reader
of its own that borrows the analyzer - `SpelledTypeId` and
`TemplateArgumentReader` - because where in the words the reading stands is
state no walk of a tree has.

**But an operand a spelling cannot answer for is asked of the parse.**
7.1.6.2p1's decltype-specifier holds an *expression*, and 5.1.1p8's
id-expression is only one of them: a call through 13.5.4's operator, a
delete-expression and 5.2.3p1's explicit type conversion each say nothing a
lookup of their text could reach.  So the parse keeps the tree it read for every
such operand (`AstArena::keep_spelled`), under the spelling it flattened it
into - the nodes are the parse's and not the tree's, which is exactly what a
template-argument-list drops - and the analyzer borrows that table
(`set_expressions`).  A specifier met as text is then answered by
`decltype_type`, the one reading that answers every other one.

**What a literal is worth is one question two readers ask.**  PA2's dump is
course defined to hold a character literal's *code point* and to refuse a run
of c-chars; the language gives every ordinary character literal type `char` and
a run of them an `int`.  Both halves are one fact of the reader, so
`CharacterLiterals` (`literal_scan.h`) is what a reader is built with:
`PostTokenizer` takes it, the three drivers that feed the compiler ask for the
`Language` and the three that print a token - and 16.1's controlling
expression, which the reference reads the same way - keep the `CourseSubset`.
The value is 2.14.5p5's execution encoding, which the course defines as UTF-8
and `append_ordinary_units` is the one implementation of: a numeric escape is
one code unit and every other c-char is the UTF-8 its code point comes to, so a
string literal's body and a character literal's cannot drift apart, and a run
is its last four code units with the first of them most significant.  5.19p2's
one object read out of storage is a subobject of a string literal, so
`string_element` takes a spelling and an index and answers for a subscript
written as a tree and for one written inside an argument list alike - through
5.1.1p6's parentheses in either - which is also why an encoding-prefix closes
up with the quoted run it stands before, exactly as `sizeof` closes up with its
`...`.

**2.14.8p3's third form takes the characters rather than the value.**  A
ud-suffix whose lookup found no operator taking the value may still have found
a literal operator *template* - one place binding a run of `char` values and no
function parameter at all - and that one is called with `specialize` over the
characters the program wrote.  It is asked before the raw operator, because
2.14.8p3 makes a set declaring both ill-formed and so no well-formed program
tells the order apart.  What the call passes is therefore one of three things,
and the cooked form passes the literal 2.14.2 or 2.14.4 already made rather
than the parameter's own type, because 5.2.2p4's conversion is what brings an
argument to a parameter.

**3.9p7's incomplete array is completed by the definition of the object.**  A
static data member declared `T b[]` is an incomplete type until the definition
written outside the class deduces its bound from 8.5.1's clauses, so that bound
is written back onto the declaration - and 8.3.4p3 the other way about, a
definition that omits the bound where an earlier declaration wrote one is a
definition of *that* array and lays out as many elements as the declaration
said.

**An expansion is one reading per element, and the inner one owns its own
packs.**  `sema_pack.h` owns it: the
pattern is read again for each argument of the run, in a region binding the
packs it names to that element, and nothing rewrites the pattern's syntax.
14.5.3p5 leaves a pack named inside the pattern of an *inner* expansion to that
one, so `sum(get<U>(t...)...)` is one reading per element of `U`, each of which
reads the whole of `t`; 5.3.3p5's `sizeof...` is the same rule, because it
counts a run rather than standing in one.  A name already expanded where it
stands is therefore left out wherever the two are written.  A tree writes them
as nodes, which `names_in` leaves out by kind; a spelling writes them inside its
own text, and `spelled_names_in` leaves out the operand each inner `...` was
written after and the parenthesized name after a `sizeof...`, read backwards the
way a postfix-expression is written.  A node's own text *is* such a spelling -
PA10 flattens a template-id into one terminal, so `list<A, B...>` is one
decl-specifier and one callee - so the tree reading reads each node's text with
the spelling reading and the two are one answer.  That same reading answers
from the four shapes a list is written in - a spelling
(`expand`), a tree a call's argument list holds (`run_of_node`), a
parameter-declaration (`read_places`), and a type a substitution built
(`substitute_entry`) - so a run of n elements costs n readings of one pattern
wherever it stands.

**A pattern that is a specialization is why the reading is per element and not
per type.**  `wrap<A>...` substituted first names one `wrap<` run `>` for every
element at once, and nothing can take that back apart: `TypeTable::substitute`
has no `Class` case because it cannot instantiate one.  So a
parameter-declaration is read like the other three - its decl-specifier-seq and
its declarator read again per element, in that element's region - and a list a
substitution rebuilds binds the pattern's *places* to one element each and
substitutes the written pattern, which is what reaches `instantiate_class`.
`expand_type` stays what it was, the structural rebuild for a run already
standing inside a built type (`A*...`, `A&...`, `A...`).

**A list the program wrote is what that reading is asked of.**  `WrittenList`
(`sema_pack.h`) is one written list and what its entries come to: the children
as written until an entry is `pattern...`, and from there each entry paired with
the region it is read in.  So 8.5.1's clauses, 5.2.2's arguments, 5.2.3's
conversion, 5.3.4's placement and initializer, 8.3.4p3's bound and 13.3.3.1.5's
length are one question with one answer, and a list holding no expansion - every
list PA15-PA19 lower - pays one node-kind test per entry and allocates nothing.
`InitializerClauses` is that list plus 8.5.1p11's cursor, so the walk that lets
a subaggregate take clauses out of the enclosing list reads each of them where
14.5.3p4 put it (`Clauses::in`).  A run no argument list has settled stands as
the one entry it was written as, which is what leaves 8.3.4p3's bound unsettled
inside 14.6p8's reading rather than wrong.  A list of *one* entry is such a list
too, so 8.5p16's parenthesised initializer, 5.2.3p1's cast to a non-class type,
12.6.2p7's mem-initializer of a non-class member and 5.19p3's fold of the
constant a declaration leaves each ask it what their one expression is and how
many the list came to - and 5.19's own reading of 5.2.3 stands a value in for a
run it cannot count, as `sizeof...` does.

**A function parameter pack is places, not a type.**  8.3.5p3's `f(int...)` and
14.5.3p4's `f(Args... args)` are told apart by whether the declarator's type
names a pack; the second declares one place per element, and 8.3.5p10 names the
first of them after the pack itself.  A run of *no* elements has no first place
and is still a declaration, so the clause declares the run itself.

**A run that is not the last place is one entry of the argument list.**
14.4p1 keys the tier by that list, and 14.1p11 leaves a *class* template's pack
last - but 14.8.2 deduces a function template's head place by place, so
`template<class... U, class... T>` binds two runs and one flat list cannot say
where the first ended: `<char, short | int>` and `<char | short, int>` are two
specializations of the same three types.  So the last place's run is written
flat, which is every list PA19 and the class tier build, and every earlier run
stands as one `Pack` entry (`place_argument`, `trailing_pack_place`).  The
object file writes either the same way - `J...E` per run, with a place written
`P...` encoded `Dp` of its pattern.

**14.8.2 is a match, and `sema_deduce.h` owns it.**  A use of a function
template names it without its arguments, so what makes a specialization is a
walk of a parameter type P beside the type A of what the use put there.  Two
uses write those pairs - 14.8.2.1's call and 14.8.2.2's target type - and both
end at 14.8.2p5, which asks whether the pairs, 14.5.3p4's run and 14.1p9's
defaults between them left every place with an argument.  The match knows
nothing about calls, so it is a reading of its own rather than part of 14.7.1's
instantiation.

**A list of entries is one rule.**  14.2's template-argument-list and 8.3.5p1's
parameter list are both entries paired one for one with a trailing `P...`
standing for every entry the ones before it did not take, so `match_arguments`
is what reads either - which is what lets 14.8.2.2's target type, whose A is one
whole function type, deduce a run.

**A specialization is a P/A pair of its own shape.**  `A<T>` against `A<int>`
is one argument list against another (`match_arguments`), so a trailing `P...`
in the pattern is the same run deduction a trailing parameter is - and
14.8.2.1p3's A may be a class *derived* from what P names, which is a walk of
10p1's base chain and is allowed only at the top of a pair the use wrote.

**14.5.5's pattern is that same match, and `sema_specialize.h` owns it.**  A
partial specialization declares places of its own and writes an argument
*pattern* over them, so it is a template beside the primary rather than a second
declaration of it: what it adds is a second body an argument list may be read
from.  14.5.5.1p1 is which - `match_arguments(pattern, arguments)` - and
14.5.5.2p1's ordering is the same match run between two patterns, because the
general one is the one that takes the specialized one as its arguments.  The
answer is a fact of the *template*, so it is memoised on `TemplateInfo` under the
interned list a naming already holds, and dropped whole where a later
declaration adds a pattern that list never saw.  Two heads spell one pattern
over places of their own, so what tells a redeclaration from a second pattern is
14.5.6.1p5's signature: the pattern with each place standing for its position.

**14.5.1p1's variable template is the third tier.**  A head over an object is
the same three steps - record the pattern, bind an argument list, read the
pattern once per list - and what differs is what one list makes of it.  A
specialization of one is reached where 5.19 asks for a constant, so the reading
leaves the constant its initializer evaluated to and declares no object: it is a
`SemaKind::TemplateValue` binding, exactly what a non-type place is bound to, so
every reader that already folds one folds this.  14.5.5's pattern and 14.7.3's
`template<>` answer for one argument list here as they do for a class.

**A value place binds a constant, not a typedef-name.**  `bind_argument` is the
one place a region takes an argument: a type argument is a typedef-name of it, a
value argument is a `SemaKind::TemplateValue` declaration carrying
`constant`/`value`, and a run is a `Pack`-typed binding of the pack's name.  14.3.2p1's refusal of a
type where a value belongs is asked of the argument each element *is*, so an
expansion at a value place is refused where its pattern is the pack's own name
(`f<T...>`) and read where the pattern computes something out of it
(`f<sizeof(T)...>`), which a reading before any argument list settles the run
can only tell from the pattern.  A
place *standing for itself* - which 14.6.1p1's current instantiation puts at
every argument, and which 14.5.1.3p1's out-of-class definition is read against -
says which it is on its own type, so the two readings of one head bind it the
same way.  So
14.1p9's default is read in a region that binds the places before it at *both*
tiers: a type-id could be substituted afterwards, but 5.19's constant expression
is evaluated where it stands and needs `A` to be a constant there.

**A value argument carries the type it was converted to where its digits do
not.**  14.3.2p1 makes an argument at a value place the value it was converted
to, and the name a specialization is written under is built from that value -
so 7.2p9's enumeration, which has no spelling of its own, is written as
5.2.9p10's cast (`(policy)2`) and 2.14.6p1's `bool` as its own two literals.
Every other arithmetic type is its digits, which is what `spell_value` already
answered for a case label and a dump.

**14.7.1p1 leaves the storage of a static data member to whatever reaches it.**
The definition written outside a class template is one no unit wrote for any
one argument list, so 3.2p3 puts what it lays out in the program where the
program reaches it - exactly as the body of a member function of the same
specialization already waits (`deferred_`).  Three things reach it: a name that
is not folded away, an object of the class - 9.2p1's member and 10p1's base
subobject are objects of their own, so `demand_object_storage` walks the tree
the object is, once per class - and 14.7.3p1's `template<>`, which is written
out and is this unit's own definition however little of the unit reaches it
(`SemaContext::instantiated_member` tells that reading from an instantiation's).
The declarator that asks is the one that *defines* an object: 9.2p1's non-static
data member lays out none of its own and is one subobject of every object of its
class, which is where the walk reaches it - so the answer, which is latched once
per class, is settled where the walk can give it rather than while the class
that holds the member is still being read.

**14.1p4 reaches the object file too.**  An argument at a value place is an
expression, so one no substitution has settled is written `X <expression> E` -
`XT_E` for the place and `XspT_E` for 14.5.3p4's expansion of one, where a type
place writes `T_` and `DpT_`.  The `<template-param>` inside is a substitution
candidate and the `X ... E` around it is not.

**14.5.6.1p5 tells a pack place from a single place.**  The signature two heads
are compared by stands each parameter for its position, and a place that binds a
*run* is a position of its own - otherwise `f(T)` and `f(Ts...)` are one
declaration.  Two templates then need ordering, and 14.8.2.4p9 leaves the head
that wrote a place ahead of the head that wrote a run.

**10p1's derivation is a tree, and `sema_derivation.h` owns it.**  A
base-specifier-list of more than one entry gives an object one subobject per
entry, so `SemaEntity::bases` and `Scope::bases` are lists rather than the one
base each held: 9.2p13 places each at a byte of its own, 10.2p2's lookup asks
each in turn and refuses the name two of them answer, 12.6.2p10 initializes them
in the order the list was written and 12.4p8 destroys them in the reverse of it.
What makes every walk of that tree well defined is 10.1p3's repeated base being
refused where the class is completed: a class then stands below another at most
once, the path to a subobject is the one path there is, and the offset of a base,
11.2p4's access through every link on the way and 14.8.2.1p3's class in the
derivation are each one visit per class rather than one per path into it.
10.3p10's secondary table and its thunks are not emitted, so a class holding a
base subobject that dispatches and does not begin where the object does is
refused - which is a second base that dispatches or one standing after storage
another base already took, and not a dispatching base beside a plain one, whose
table is the one this class owns (`dispatching_base`); every other class with
bases the ABI does not describe as one public base at the start of the object
writes `__vmi_class_type_info`.

**A base at a byte of its own is a step in both directions.**  4.10p3's
conversion moves an address on by the place the derived class gave the base, and
5.2.9p11's cast back to that class moves it back (`derived_value`, a
`base-conversion` node the lowering spells as a negative index) - nothing at all
where the base begins where the object does, which is what leaves every
single-inheritance conversion the output it already had.  11.2p4 asks the access
those steps need in the region the conversion was *written* in, which outlives
the reading of the operand it converts: an initialization applies its conversion
after `read_expression` has given that region back, so `apply_conversion` holds
it (`Written`) for the declaration initializer, the returned object, the clause
and the bound reference alike.

**Two facts of one base-specifier are not facts of the list.**  Which class a
mem-initializer-id names is what 12.6.2p2's index is keyed by - the whole name
that class has, because two bases may be classes whose names end in the same
component - and every other id is keyed by the last component it wrote, which is
12.6.2p2's member.  14.6.2p3 is the same: 3.4.1's search of a member's
unqualified name looks in the bases whose own specifier named a settled type
(`Scope::open_bases`) and leaves off only the ones an argument list settles, so a
class deriving from a settled class and a dependent one is searched in the
settled one exactly as it would be with the other unwritten.

**8.5.2's array of character type is an initialization and not a conversion.**
No expression of array type converts to another array type, so a string literal
never reaches 8.5p16's reading at all: `sema_string_init.h` is the one reading of
it, and the three places a program writes one - a declaration, a clause of a
braced-init-list, and 8.3.4p3's bound the literal settles - each ask it once.
What the elements hold is the code units phase 6 already built, so an array of n
elements is one scan of n units.

**14.6.2p1's dependent member is settled by the substitution.**  A name written
after a prefix the definition could not settle is a stand-in carrying the two
facts the ABI writes apart - the prefix and the name.  So `substituted` settles
the prefix and looks the name up in the class it became, exactly as it re-reads
a `decltype` the definition left standing; and 14.8.2.5p5 makes the same
stand-in a *non-deduced* context, because a nested-name-specifier says nothing
about what its prefix names.

**A prefix an argument list has already settled is asked for its definition.**
3.4.3p1 looks a name up *in* the region its prefix named, which for a class
template specialization is 3.9p5's context requiring a complete type - so the
demand is `require_settled_type` and not `require_complete_type`.  14.6p8's
reading of a template definition asks for nothing, but a prefix no argument list
can still change is one the definition itself is read against, so the reading is
put aside for that demand exactly as 10p1's base class puts it aside.  Two walks
make that demand - `resolve_prefix` for a prefix the spelling reaches and
`qualified_in_type` for one that is a *type*, which is 7.1.6.2p1's
decltype-specifier - and both answer alike.  A prefix that is still dependent is
untouched by either: every component behind it is a member of the one before it,
which is 14.6.2p1's stand-in the substitution settles.

**What this milestone cannot read is what it cannot instantiate.**  14p1 lets a
program declare a template it never names, so a head or a pattern outside the
slice is recorded rather than refused where it stands - but 14.5.5p1's pattern is
a *second body* an argument list may be read from, so one that could not be read
is not a declaration that may be left out: every list would then be read from the
primary's body, which is a different program.  So a template one of whose second
bodies is unknown answers no argument list at all, at 14.3p1's gate that every
naming already passes, and 14.5.5p8.3's undeducible place is refused at the list
that matched it.

**One reading per argument list, and the tiers hold it differently.**  A class
specialization is held before its body is read, so a naming inside that body
finds the declaration already made and the reading terminates on its own.
14.5.1p1's specialization *is* the constant its initializer evaluates to, so
there is nothing to hold until the reading is over - `TemplateInfo::reading` is
therefore what a variable template holds instead, and a naming of a list already
being read is 5.19p2's circle rather than a second reading of it.

**14.6p8's reading is put aside whole.**  `stood_in_` is a count of the values a
reading stood in for; `checking_` is the depth of the reading itself and
`unit_dialect_` is what the unit is read in.  10p1's base class and 3.4.3p1's
settled prefix are what a pattern's reading demands in earnest, so
`require_settled_type` puts *both* aside - a specialization completed in the
checking dialect is left with none of 12.1's members it is owed.

## Current Failure Map

7 failing, grouped by what would fix them.

| group | n | owner |
| --- | --- | --- |
| 5.19 outside the integral subset: `B{}` at a value place, which is a class prvalue reaching 7.1.5's constexpr conversion function, and `array::max_size()` in a static member's initializer, which the reference folds to 64 where this milestone writes a `load` - both are 5.19p2's call of a constexpr function, which PA21 owns and which two fixtures already need | 2 | `sema_constant.cpp` |
| 14.2's `<` inside an argument spelling that is 5.9's: `I<R<A>::v < R<B>::v, B, A>` does not parse, and `box<(traits::least < 0)>` written in a course fixture had to be spelled without the relational | 1 | `sema_name.cpp`, `sema_value_expression.cpp` |
| 8.5.1p2's aggregate built as an object of its own where a member is an array: 8.3.5p5 leaves the class no by-value parameter list, so `T{item(0), item(1)}` for `struct array { T e[N]; }` has no constructor to call and the clauses have to initialize the subobjects where they stand | 1 | `sema_lifetime.cpp`, `sema_init_list.cpp` |
| three singletons: an alias rewrite whose `value_type` does not name a type inside 14.5.1.3p1's out-of-class definition; a value place whose type names the current instantiation (`typename uint_for<Bits>::fast Poly`), which leaves an object of an incomplete class declared; and `&function_template` as a constructor argument, which reaches a unary operator the PA15 lowering has no case for | 3 | mixed |

Outside the fixtures, the largest shapes the sweeps now leave are four the
declarator still cannot read.  8.3.5p3's ellipsis written *without* a comma -
`int a...`, at namespace scope and in a class body alike - is not in the parse at
all, which is a PA10 gap both oracles read and which has nothing to do with a
pack.  A trailing pack that no argument reaches leaves `Deduction::from_call`
refusing every call of a *variadic* function template
(`template<class... A> int g(int first, ...)`), because the deduction is refused
whole where 14.8.2p5 would leave the run empty.  14.8.2.1p2's top-level
cv-qualifier on P is not stripped, so `f(const A... a)` deduces nothing and the
same head reads with a written list.  And a `...` written inside a *nested*
declarator - `A (*... p)(int)` - is not the top-level ParameterPack the reading
looks for, so the clause is read as one place of a pointer type.

Beside those the sweeps leave these shapes this milestone refuses where
both oracles accept: 10.1p3's repeated base class, which is two subobjects a
name of either is ambiguous between and which nothing here tells apart, and
10.3p10's base subobject that dispatches and does not begin where the object
does - a second base that dispatches, or a dispatching base standing after one
that took storage; 15's try block and 5.5's `.*` are outside the PA12 statement
and operator subsets, which predates every template question here; a
specialization's body cannot name its own class - `typedef s self;`
inside `struct s<T*>` finds the primary and `s<T*>` written there is read as
12.1p1's constructor name and does not parse, which has been true of
`template<>` specializations since C2; a partial specialization has no
out-of-class member definitions; a dependent array bound is unreadable in an
argument *spelling* (`s<T[N]>`, where `s<Arr>` over a typedef reads); a template
template parameter is outside every head; `typename c<true>::type` written in a
template nobody instantiates, where `c` is only declared, is refused where it
stands - which g++ refuses too ([temp.res]p8) and the reference accepts; a
constructor template written in a class body
(`template<class... A> D(A... a) : B(a...) {}`) is refused where it stands,
which is 12.1p1's name read without a head over it and not a list question; a
decltype-specifier is outside the base-specifier grammar
(`struct outer : decltype(mk())::inner`), which the reference refuses too; and
14.8.1p2's explicit list does not *extend* a trailing run the call would
deduce more of, so `template<class... T> int inner(T...)` named `inner<char>`
and called with three arguments makes the one-element specialization and
refuses the call - which the reference refuses too and g++ accepts; and
12.1's constructor over a function parameter pack of *no* elements is not a
candidate, so `template<class... A> struct s { s(A... a); };` and then `s<>`
reports `no declaration of s accepts the arguments of a call` where both oracles
build it - while the same head's member *function* over the same empty run, and
the same constructor over a run of one or two, are right.

The reference's own reading of a specialization pattern is the widest of the
other-way divergences.  It writes `arity=variadic` and mangles a trailing `z` for
*every* function whose parameter-declaration-clause writes
`specialization... name` - `wrap<A>...`, `wrap<A>*...`, `const wrap<A>&...`,
`typename wrap<A>::type...`, a run of one and a run of none alike - while still
declaring the right place per element; g++ mangles `_Z1fIJicEEiDp4wrapIT_E` with
no `z`, which is what this compiler writes byte for byte, and every one of those
pairs agrees on the whole of the rest of the LowIR.  A pattern with no
specialization in it (`A...`, `A*...`) is not marked so in either.  The
reference also refuses four pattern shapes g++ and this compiler read:
`pair_of<A, A>...`, which names one pack at two places; `s<wrap<A>...>*`, an
expansion inside the argument list of a parameter's own type;
`list<A, B...>...`, 14.5.3p5's nested expansion at a declarator; and
`int (*)(wrap<int>, wrap<char>)` as the target type a specialization of such a
head is chosen by.

Four more stand the other way, where this milestone accepts or answers and one
oracle does not: two argument lists that flatten alike but split their runs
differently - `split(types<char, short>(), 4)` beside
`split(types<char>(), (short)1, 4)` for `template<class... A, class... R>` - are
two specializations here and in g++, whose mangled names this compiler writes
byte for byte, where the reference reuses the first and refuses the second call;
that is why the course fixture writes the two with different flat lists.  A
redeclaration of a static data member with a *different*
element type is taken rather than refused (`static unsigned char b[];` then
`int s::b[] = {...}`), which both oracles refuse and which is 3.3's matching and
not 8.3.4p3's bound; `case 'ab' - 24672:` is folded here and in g++ where the
reference emits a `load` into the switch, which no case label may be; and a
definition that omits an array bound an earlier declaration wrote lays out the
declared length here and in g++ where the reference refuses the program.  The
reference also refuses `L'ab'` and `#if 'ab'` with us where g++ accepts both,
which is the course-defined limit on a character literal's encoding, and it
drops a ud-suffix written on a *character* literal - `'a'_c` is 97 there
whatever the operator returns, where this compiler and g++ both call it.  The
two oracles disagree outright on a single non-ASCII c-char: `'é'` is a `char`
holding -23 in the reference and a multicharacter `int` holding `0xc3a9` in
g++, and this compiler answers the reference, which is what the object file is
compared against.  10.2p6's ambiguous inherited name and ambiguous inherited
typedef-name are refused here and by g++ where the reference takes one of them;
and 11.2p4's conversion to a private or protected base of a class, *written
outside* every class the access reaches from, is refused here and by g++ where
the reference allows it.  14.7.1p1's own storage stands the same way: naming one
static data member of a specialization makes the reference lay out *every* one
whose definition it has read - `S<int>::m` beside the `S<int>::n` a program
named - where this compiler and g++ lay out only the ones a use reaches, and a
`...::inner` whose base-clause writes a nested expansion is refused by the
reference with its own substitution error where g++ and this compiler read it.

Four more are metadata, a shape the comparison ignores, or a family already
owned: 14.5.5p1 does not refuse a partial specialization of a *function*
template; 14.7.3's explicit specialization of a function template is emitted
`binding=weak` where the reference writes `binding=strong`; an unnamed
enumeration reached through a `decltype` is mangled `N1S17__anonymous_enum1E`
where the ABI writes `N1SUt_E`; and a static data member of a specialization of
a template with a *non-type* parameter is a `load` here and in g++ where the
reference folds the initializer.  One more is 14.7.1p1's storage read in the
safe direction: a 9.4.2p3 constant read for its *value* inside a body still
names the object at `declare_entity`, so `traits::least == -100` written in
`main` lays out storage neither oracle lays out - a definition too many and
never one too few.  Three shapes of the lowering's own writing
differ without a fixture: an array whose list is shorter than its bound writes
one `zero n` run at namespace scope where the reference writes n explicit
elements; a *qualified* access to a base's member (`v.a::n`) writes a
`base_subobject` step of offset zero the reference folds away; and an array of a
class with a base declared at namespace scope leaves this unit no `role=init`
function where the reference writes an empty one - `A v[3];` for `struct A : B`,
where neither writes one for a class with no base.  All three predate this
checkpoint and none is a value the program can observe.

## Active Checkpoint

**C12 - 5.19p2's call of a constexpr function, and the class prvalue that
reaches one.**  Selected because it is the only remaining group that owns more
than one failing fixture and every other failure is a singleton of a different
owner: `static constexpr size_type max_array_size = array::max_size();` is a
`load` here where the reference folds 64, and `box<B{}>` refuses a class prvalue
whose conversion to `bool` is 7.1.5's constexpr conversion function.

- **Owner.**  `sema_constant.cpp` - it already walks an expression to a
  `Constant` and already reads a call node as 5.2.3p1's cast, and 5.2.2's call
  of a function is the other arm of that same node.
- **Data flow.**  The callee is what 13.3's overload resolution already chose,
  so what the fold adds is one reading of the *body*: 7.1.5p3 leaves a constexpr
  function one `return` statement, and its expression is read as a constant in a
  region binding each parameter to the constant its argument folded to - which
  is the region 14.1p9's value default is already read in.  9.3.1p3's object
  parameter is one of those bindings, so `array::max_size()` and the conversion
  function `B{}` reaches are one reading.  The answer is a fact of the callee
  and the argument list, so it is memoised on that pair the way a specialization
  is memoised on its own list.
- **Expected complexity.**  One reading per distinct (callee, argument list),
  so a chain n deep costs n of them - the shape the `fac<800>` row already
  measures - behind the depth guard the plan owes `instantiate_class`, which a
  self-calling constexpr function reaches first.
- **Validation.**  The two fixtures above; a sweep of 7.1.5's forms - a call in
  an array bound, in a `static_assert`, at a non-type place, through a member
  and through a conversion function - against g++ and `pa20/cppgm++-ref`;
  scaling rows for a chain 800 / 3000 deep and for one call named 4096 times;
  and a valgrind run of each.

## Performance Model

Best of five, `-O0`, timed by the shell around the process itself: an empty
translation unit is **0.003 s**, so a row below is the shape's own cost.  A
harness that spawns a process of its own per run reads this machine's floor as
0.11 s; it is not one, and the `pa20/cppgm++-ref` wrapper adds a further ~0.5 s
of its own before the reference binary starts.

The pattern rows were re-measured on this turn's build against a floor of
0.004 s, which is the floor the rows around them were taken at, so every row in
the table stands beside every other; each is unmoved from the `51f8f135` build,
which is what says the reading that walks a node's text costs nothing it did
not already cost.  The reference is timed through its wrapper and its own floor
is 0.608 s, which is subtracted from the numbers in its column.

| shape | here | `pa20/cppgm++-ref` |
| --- | --- | --- |
| 512 / 2048 / 8192 distinct `decltype` spellings in argument lists | 0.018 / 0.069 / **0.300 s** | 0.148 / 0.691 / 9.7 s |
| 8192 namings of *one* such spelling | **0.180 s** | 0.949 s |
| a `decltype` spelling nested 24 deep | **0.004 s** | 0.013 s |
| 256 / 1024 / 4096 `decltype` prefixes in one template definition | 0.021 / 0.082 / **0.410 s** | 23.9 s at 4096 |
| 64 names behind one dependent `decltype` prefix | **0.006 s** | 0.022 s |
| 256 / 1024 / 4096 out-of-class definitions over a value place | 0.014 / 0.046 / **0.188 s** | 2.084 s at 4096 |
| 256 / 2048 settled prefixes named in one definition | 0.015 / **0.119 s** | 0.250 s at 2048 |
| a pack of 4096 elements bound and counted | **0.018 s** | 0.159 s |
| `fac<800>` metafunction chain | **0.037 s** | 0.167 s |
| 256 patterns against 2048 distinct lists | **0.063 s** | 11.1 s |
| 14.5.3p4's recursion over a pack of 1024 | **1.560 s** | 9.3 s |
| an expansion of 1 / 64 / 512 / 2048 in an array's clause list | 0.004 / 0.005 / 0.015 / **0.051 s** | 1.30 s at 2048 |
| the same in an aggregate's clause list | 0.004 / 0.005 / 0.017 / **0.058 s** | 1.40 s at 2048 |
| the same as a constructor's argument list | 0.004 / 0.006 / 0.018 / **0.061 s** | 1.69 s at 2048 |
| 256 / 1024 / 4096 scalar paren declarations over a run of one | 0.014 / 0.045 / **0.177 s** | - |
| 256 / 1024 / 4096 functional casts over a run of one | 0.009 / 0.026 / **0.094 s** | - |
| 256 / 1024 / 4096 mem-initializers of a non-class member | 0.017 / 0.058 / **0.226 s** | - |
| 256 / 1024 / 4096 constant casts over a settled run | 0.015 / 0.052 / **0.195 s** | - |
| 2048 ordinary declarations carrying a braced clause | **0.078 s** | - |
| a cast nested 24 deep over a run of one | **0.004 s** | - |
| a braced list nested 24 deep with an expansion at the leaf | **0.093 s** | - |
| an aggregate of 2^15 / 2^17 subobjects, with / without an expansion | 1.483 / 1.499 and 6.713 / **6.734 s** | - |
| a call forwarding a parameter pack of 1024 places | 0.025 s | - |
| a target type deducing a run of 4096 places | 0.041 s | - |
| 800 calls ordering a pack head against a non-pack one | 0.022 s | - |
| 3200 calls reading a value default that names an earlier place | 0.192 s | - |
| 4096 distinct value arguments over two templates | 0.550 s | - |
| a 2000-deep chain instantiated but not evaluated | 0.085 s | - |
| 14.8.2.1p3 through a 200-deep base chain | 0.011 s | - |
| 64 nested-pointer patterns all matching one list, ordered pairwise | 0.008 s | - |
| 512 / 2048 distinct variable-template specializations | 0.015 / 0.051 s | - |
| a variable-template chain 800 / 3000 / 6000 deep | 0.010 / 0.031 / 0.070 s | - |
| the doubling spelling at 2^20 leaves | 0.912 s | 2.277 s |
| 512 / 2048 / 8192 string-literal elements read as constants | 0.007 / 0.017 / **0.057 s** | 0.714 s at 8192 |
| `sizeof...` in a spelling expanded over a run of 256 / 1024 / 4096 | 0.013 / 0.044 / **0.178 s** | 7.62 s at 1024 |
| 256 / 1024 / 4096 static member arrays completed by their definitions | 0.022 / 0.082 / **0.363 s** | SIGSEGV at 4096 |
| a specialization pattern deduced over a run of 1 / 64 / 512 / 2048 | 0.004 / 0.006 / 0.017 / **0.054 s** | 0.004 / 0.022 / 0.149 / 0.720 s |
| the same clause read per element from a settled head at 1 / 64 / 512 / 2048 | 0.004 / 0.007 / 0.024 / **0.082 s** | - |
| the same clause over a bare place (`A... a`) at 2048 | **0.036 s** | - |
| a specialization pattern nested 12 / 24 / 48 deep over a run of 8 | 0.006 / 0.009 / **0.015 s** | - |
| 64 / 256 / 1024 inner expansions inside one spelled pattern | 0.006 / 0.013 / **0.043 s** | - |
| a string element nested 24 parentheses deep in a spelling | **0.004 s** | - |
| 24 nested heads each counting its own run | **0.007 s** | - |
| 512 / 2048 / 8192 distinct multicharacter literals | 0.008 / 0.018 / **0.058 s** | 0.649 s at 8192 |
| the same holding a c-char above the ordinary range | 0.025 / 0.044 / **0.120 s** | - |
| 512 / 2048 / 8192 string literals carrying escapes | 0.015 / 0.053 / **0.228 s** | - |
| 256 / 1024 / 4096 string elements read through parentheses | 0.010 / 0.029 / **0.109 s** | - |
| the same written without parentheses | **0.083 s** at 4096 | - |
| 256 / 1024 / 4096 parenthesized argument spellings holding no literal | 0.009 / 0.023 / **0.085 s** | - |
| a literal parenthesized 24 deep in a spelling | **0.004 s** | - |
| 256 / 1024 / 4096 raw ud-literals | 0.011 / 0.033 / **0.134 s** | - |
| 256 / 1024 / 4096 distinct literal-operator-template specializations | 0.017 / 0.057 / **0.255 s** | SIGSEGV at 4096 |
| 200 / 800 / 1600 classes, unrelated | 0.010 / 0.031 / **0.063 s** | - |
| the same as one chain of single bases | 0.010 / 0.028 / **0.058 s** | 0.10 s at 1600 |
| 100 / 400 / 800 / 1600 levels each adding a second base | 0.011 / 0.049 / 0.154 / **0.584 s** | 0.10 s at 800 |
| one class with 64 / 256 / 1024 direct bases | 0.006 / 0.011 / **0.036 s** | 0.00 s at 1024 |
| a base pack of 64 / 256 / 1024 elements, each initialized | 0.012 / 0.037 / **0.165 s** | 14.3 s at 1024 |
| 400 / 1600 / 6400 conversions through a 200-deep derivation | 0.049 / 0.166 / **0.646 s** | 2.20 s at 6400 |
| 256 / 1024 / 4096 casts back to a class through a base at byte 4 | 0.023 / 0.084 / **0.336 s** | 1.50 s at 4096 |
| 256 / 1024 / 4096 specializations deriving from a settled base and a dependent one | 0.047 / 0.199 / **0.920 s** | 13.4 s at 4096 |
| 512 / 2048 / 8192 arrays initialized by a string literal | 0.018 / 0.064 / **0.268 s** | 1.60 s at 8192 |
| 256 / 1024 / 4096 calls deducing two runs in one head | 0.065 / 0.276 / **1.221 s** | 0.55 / 4.2 s |
| 4096 objects of a class with a 64 / 512-deep base chain | 0.117 / **0.139 s** | - |
| the same 4096 objects of a class with no base at all | **0.114 s** | - |
| 64 / 256 / 1024 inner expansions in one pattern | 0.006 / 0.011 / **0.033 s** | 1.87 s at 1024 |
| an expansion nested 4 / 8 / 16 / 24 heads deep, each counting its own run | 0.004 / 0.006 / 0.010 / **0.019 s** | >300 s at 24 |
| a run of 256 / 1024 / 4096 expanded through a pattern that computes a value | 0.006 / 0.011 / **0.034 s** | - |
| 4096 objects of a class holding a member of a specialization, over a 0 / 64 / 512-deep base chain | 0.075 / 0.079 / **0.092 s** | - |

The decltype table is one entry per *distinct* operand spelling and one hash
lookup per naming, so the 512 / 2048 / 8192 row is linear in the spellings a
program writes and the 8192-namings row is cheaper than the 8192-spellings one -
which is what says the table is keyed by the text and not by the naming.
Nesting is flat because a nested spelling is one more entry, not one more scan.
`require_settled_type` at a prefix adds one integer test per component of a
nested-name-specifier and one instantiation per settled specialization a
definition names, which is why the pattern and metafunction rows did not move.

Every literal row is linear in its own multiplicity at 512 / 2048 / 8192 or at
256 / 1024 / 4096, and flat in nesting depth.  A character literal is one scan
of the c-chars phase 3 already found however it is read, and the code units it
comes to are appended into one string per literal - so the row holding a c-char
above the ordinary range, which is the one that appends two units where the
others append one, is the same shape and 2x the cost of writing the wider
spelling out.  A string element is one scan of the code units phase 6 already
built; the bound a definition deduces costs one lookup per *unbounded* array
declarator with a braced list and nothing at all for every other declarator;
and a literal operator template is one `specialize` per distinct character
list, which is the memoised specialization every other template naming makes.
Asking that template before 2.14.8p3's raw operator costs a raw ud-literal one
scan of its own candidate list, which the raw row does not measure at 4096.
5.1.1p6's primary is one index comparison per parenthesized run, so the
parenthesized-spelling row is the same on the build that reads the literal
inside it and the build that refuses it, and the 1.3x over the bare spelling is
the subscript that now happens.  The reference is 12x slower on the literals,
170x on the counted run, and dies on the object-file rows at 4096.

A written list costs one node-kind test per entry until an expansion is found
and nothing at all after that for a list holding none, which is why the three
2048-entry rows are linear in the entries and 3.4x their own 512 rows.  A list
of *one* entry is the same reading and one per list met: the four
readers of one are each linear in their own multiplicity at 256 / 1024 / 4096,
and 5.19p3's fold reads the list only where a const object of arithmetic type
asks, so 2048 ordinary declarations carrying a braced clause are unmoved.

The deepest
nesting is flat for the same reason a list is: a nested list or cast is one more
list, not one more scan.  The 2^17-subobject aggregate is exponential in its
*type* and not in the reading - the row with an expansion and the row without it
are the same to within the noise - so it is the same shape's-own-cost the
doubling spelling below is.

10p1's derivation costs what the classes in it cost, and 10.1p3's own check
costs what the *tree* is.  1600 unrelated classes read 0.063 s and the same 1600
as one chain of single bases read 0.058 s, so a derivation of one base per class
adds nothing at all: every walk of it stops at the first answer, and a class with
fewer than two direct bases skips the repeated-base check outright.  A class with
two does pay it, and it is one hash insert per class below - so a derivation that
adds a base per *level* pays that walk per level and the row is quadratic in the
depth: 0.011 / 0.049 / 0.154 / 0.584 s at 100 / 400 / 800 / 1600 levels, 12x for
4x the classes.  That is the shape of the question rather than of this
implementation - whether two subobjects of one class stand below a class is a
fact of the whole set below it, so completing n classes each merging two sets
cannot cost less than their sum - and 1600 levels of multiple inheritance is
0.58 s, so it is recorded rather than paid down.  One class with 1024 direct
bases is linear at 64 / 256 / 1024 because those bases have nothing below them.
A conversion through a 200-deep derivation is one addition per level and the row
is linear in the conversions written, not in their product with the depth; so is
5.2.9p11's step back through a base at byte 4, at 256 / 1024 / 4096 casts.  The
reference is 90x slower on the base pack and 15x on the specializations deriving
from a dependent base, and *faster* than this compiler on the rows that only
declare classes - it is one oracle for what a program means and no oracle at all
for what a shape costs.

8.5.2's array is one scan of the code units phase 6 already built, so the
512 / 2048 / 8192 row is linear in the arrays a program writes and the
reference is 5x slower at 8192.  The bound a declaration deduces costs one node
kind test per *unbounded* array declarator and nothing at all for every other
one, which is what keeps the probe off every initializer that is not a literal.

14.5.5.1p1's choice is one match per pattern per *distinct* argument list and
nothing else.  14.5.5.2p1's ordering is quadratic in the patterns that *match*,
which the 64-deep row measures at 4096 comparisons; a use matches one or two in
every shape a program writes.

A head binding two runs costs one deduction and one specialization per call, so
the row is linear at 256 / 1024 / 4096 - 4.4x per 4x, where each call also
declares a class of its own - and the reference, which reuses the first split
for every list that flattens alike, is 8x and 15x slower on the same programs.
The run at every place but the last is one entry, so nothing is spliced and
nothing rescanned: reading a bound list is one `place_argument` per place.

3.2p3's demand for the storage a static data member stands in is one walk per
*class* and not per object: 4096 objects of a class with a 512-deep base chain
read 0.139 s where 4096 objects of a class with no base at all read 0.114 s, so
the whole derivation costs 0.025 s once - the same 4096 objects over a 64-deep
chain read 0.117 s, which is the 8x deeper tree costing 0.022 s more.  A
declaration that lays out no object - a pointer, a reference, a typedef, and
9.2p1's non-static data member, which is a subobject of the object the walk
already reaches - asks nothing at all.  The row above, which holds a member of a
specialization at every level, is unmoved by that: 0.075 / 0.079 / 0.092 s at 0
/ 64 / 512 levels against 0.075 / 0.078 / 0.094 s on the `9196229b` build.

Both readings of which packs a pattern is over are linear in the pattern's text.
The spelling one walks it once and steps back over the operand each inner `...`
was written after, and those operands are disjoint except where expansions nest,
so 1024 inner expansions in one pattern read 0.033 s - 3x the 256 row for 4x the
expansions and 4x the text - and the deepest well-formed nesting, 24 heads each
counting its own run, reads 0.019 s where the reference does not finish in five
minutes.  Reading a run through a pattern that computes a value is the same n
readings of one pattern every other expansion is: 0.006 / 0.011 / 0.034 s at
256 / 1024 / 4096 elements.  Every row this checkpoint's readings do not touch
is unmoved from the `9196229b` build - 0.015 s for a 2048-entry clause list and
0.011 s for a pack of 4096 bound and counted, against 0.015 s and 0.012 s there.

Reading a parameter-declaration once per element costs what reading it once cost
times the length of the run, and nothing more: a clause over a bare place is
unmoved by the rewrite - 0.027 s at 2048 elements on this build against 0.027 s
on the `2a6adc7c` build, and 0.008 s at 512 on both - and a clause whose pattern
is a specialization, which also instantiates one class per distinct element,
runs 0.003 / 0.005 / 0.020 / 0.072 s at 1 / 64 / 512 / 2048 against the
reference's 0.004 / 0.022 / 0.149 / 0.720 s.  A pattern nested 24 deep is 0.006 s,
because `packs_in` answers per type rather than per path and the memo each
element's substitution carries is its own.

Two shapes are not linear in what they walk, and both are the shape's own cost
rather than a reading's: a type whose arguments *double* is exponential in the
spelling, and 14.5.3p4's recursion over a pack walks argument lists whose lengths
sum to n^2/2 - g++ is 0.210 s at 1024 where this compiler is 1.560 s and the
reference 9.3 s.  A *class* metafunction with no terminating specialization
still overflows the machine stack rather than being diagnosed, here and in the
reference alike; a depth guard is owed whenever a checkpoint touches
`instantiate_class` again.  `sema_analyzer.h` is at 2393 of the audit's 2400
header lines and `sema_expression.cpp` at 2975 of its 3000, so the next
checkpoint that adds a declaration to either owes a structural move first: 4.10p3
and 5.2.9p11's steps between an object and its base subobject are the readings
that would leave with `Derivation`, which already owns every other question a
base-specifier answers.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | 14.1p4's non-type parameter and 14.3.2's integral argument: `TypeKind::Value` as an interned converted constant, `TemplateInfo::Parameter` as a place, 14.6.1p1's region opened once, 5.19 read out of the argument spelling, `SemaKind::TemplateValue` bound as a constant, 7.1.5p9's constexpr object, 7p4 deferred behind a counted stand-in, and 10p1's settled base completed inside 14.6p8's reading | 39 -> **85 / 164**; pa1-pa19 2169 / 2169 |
| C2 | 14.7.3's explicit specialization: a `template<>` head declaring the specialization and no template, the class body read in place of the pattern and the function body run in place of the pattern's, both keyed by the interned argument list | 85 -> **92 / 164**; `fac<200>` SIGSEGV -> **0.01 s** |
| C1, C2 audit | the spelling a value argument arrives as: 14.2's `<` told apart from 5.9's and 5.8's, 3.4.3p1's rooted name, 4.12p1's conversion, 5.2.3p1/p3's notation, 8.5p16 and 8.5.4p3's initializers, and 14.6p8's count put back by a discarded probe | 92 / 164 -> **103 / 169** with five fixtures added |
| C3a | 14.5.3's place and run at the class tier: `TypeKind::Pack` as both a run and an expansion, `pack_place` counting a written list, an expansion read once per element, 5.3.3p5's `sizeof...` parsed and answered, and 14.5.3p4's base-specifier pattern laid out where the run holds one base | 103 -> **108 / 169** |
| C3b | the function tier: 14.8.2.1p1 deducing a trailing `P...` as a run, `deduced_arguments` splicing it into one flattened list, 8.3.5p3's ellipsis told from 14.5.3p4's expansion by the declarator's type, one place declared per element under 8.3.5p10's names, and a substitution splicing an expansion inside a parameter list | 108 -> **118 / 169** |
| C3c | 14.5.3p4 in a call's argument list, read over the tree rather than a spelling, with a function parameter pack as one of the answers; explicit argument lists counted by the pack place; and 14.3.2p1 refusing a pack of types at a non-type place | 118 -> **123 / 169**; expansion linear at 1/64/512 elements |
| C3 audit | the two kinds of settled pack and the list the object file writes for either: one element region for a spelling and a tree alike, an element that carries the pack a nested expansion and `sizeof...` still name, a run of no elements declared where it declared no place, and 14.5.3's `J...E` and `Dp` in every mangled name | 123 / 169 -> **127 / 172** with three fixtures added; linear at 4096 elements |
| C4 | 14.8.2 given its own owner (`sema_deduce.h`), and the four things a use it could not match needed: a specialization P matched as an argument *list* so a trailing `P...` deduces a run, 14.8.2.1p3's A that is a class derived from what P names, 14.8.2.5p5's non-deduced context, 14.1p9's default at a value place - unnamed places included - and 14.6.2p1's dependent member settled by the substitution.  10p1's base is now completed in the unit's own dialect, an explicit list that stopped at the pack place still deduces, and `user_types_` is a deque because every reader of it holds a reference while a class is completed | 127 -> **133 / 172**; pa1-pa19 2169 / 2169; floor re-measured at 0.00 s |
| C4 audit | the list a use is chosen from and the one the object file writes: 8.3.5p1's parameter list matched by the same rule 14.2's list is, so 14.8.2.2's target type deduces a run; 14.1p9's value default read in a region binding the places before it, as the class tier already read it; 14.5.6.1p5 telling a pack place from a single one, and 14.8.2.4p9 ordering the two heads that makes; and 14.1p4's `X <expression> E` for every non-type argument no substitution has settled | 133 / 172 -> **136 / 175** with three fixtures added; every one of 71 swept shapes agrees with g++ |
| C5 | 14.5.5's pattern and 14.5.1p1's variable template given one owner (`sema_specialize.h`): a partial specialization as a head, an argument pattern and a body held beside the primary; 14.5.5.1p1's choice as `match_arguments` over the interned list, memoised per template and dropped where a later pattern arrives; 14.5.5.2p1's ordering as that same match between two patterns; 14.5.6.1p5's signature telling a redeclaration of one pattern from a second; and a variable template's specialization as the constant one init-declarator evaluates to - with 14.7.3's `template<>` and 9.4.2p1's qualified declarator-id answering for both tiers | 136 / 175 -> **145 / 178** with three fixtures added; pa1-pa19 2169 / 2169; every one of 48 swept shapes agrees with g++; ref 20x slower on the pattern row |
| C5 audit | what a pattern this milestone could not read leaves behind: three exits dropped a partial specialization and let the *primary* answer for every list it would have taken, so a template one of whose second bodies is unknown now answers no argument list at all, and 14.5.5p8.3's undeducible place is refused at the list that matched; and 14.5.1p1's specialization is the constant its initializer evaluates to, so one that names itself ran until the machine stack ran out where both oracles diagnose it | 145 / 178 -> **147 / 180** with two fixtures added; pa1-pa19 2169 / 2169; 68 swept shapes |
| C6 | a decltype an argument list wrote, and the prefix it stands before: the parse now keeps the tree it read for every decltype operand it flattened into a name (`AstArena::keep_spelled`), so a specifier met as text is answered by `decltype_type` and a call, a delete-expression or 5.2.3p1's conversion reads where only 5.1.1p8's id-expression did; the same carried tree is asked by 5.19's `id_constant` and by `resolve_prefix`, which every other reader of an id-expression already asked; and 3.4.3p1's prefix that an argument list has settled now demands its definition through `require_settled_type`, which is what lets `typename c<lower>::type` be written in a template definition at all.  `sema_type_id.cpp` became its own owner, `SpelledTypeId`, freeing 19 header lines | 147 / 180 -> **156 / 182** with two fixtures added; pa1-pa19 2169 / 2169; 37 decltype-operand shapes and 12 prefix shapes swept against both oracles, every accepted pair writing the reference's LowIR but one unnamed enum's mangled name; linear at 8192 spellings |
| C7 | 14.5.3p4 in every list a program writes one into, given one owner: `WrittenList` is a written list and what its entries come to, so 8.5.1's clauses, 5.2.2's arguments, 5.2.3's conversion, 5.3.4's placement and initializer, 8.3.4p3's deduced bound and 13.3.3.1.5's length all ask it once; `InitializerClauses` carries it with 8.5.1p11's cursor so an elided subaggregate reads each clause where the expansion put it; an unsettled run stands as the one entry it was written as, which leaves 14.6p8's reading a bound rather than a wrong one; 8.5.1p2's array walk became one implementation (`array_from_clauses`) driven by that cursor; and 5.2.3p2's temporary now demands 3.9p5's complete type, which is what lets `pr<int>{1,2}` stand as an argument at all.  8.5.1's aggregate-through-a-constructor half moved to its own owner beside 8.5.1's clause walk | 158 -> **162 / 184**; pa1-pa19 2169 / 2169; 20 list shapes swept against g++ and the reference with no new divergence, linear at 1 / 64 / 512 / 2048 entries, valgrind clean |
| C6 audit | the demand a prefix makes, at all three walks that make it and in all three modes that read one: `qualified_in_type` asks `require_settled_type` for a settled prefix and leaves 14.6.2p1's stand-in for a dependent one, and `resolve_prefix`'s decltype branch reports a dependent prefix rather than looking its spelling up; `id_constant` asks `decltype_qualified_name` instead of a second copy of it; the arena travels through `emit_translation_units`, so the two dump modes read such a name the way the lowering one does; and 14.6.1p1's current instantiation binds a value place as a value, which is what lets an out-of-class member definition of a class template with a non-type parameter name its own head | 156 / 182 -> **158 / 184** with two fixtures added; pa1-pa19 2169 / 2169; 134 swept shapes, every accepted pair writing the reference's LowIR |
| C7 audit | a list of *one* entry is a list too: 8.5p16's parenthesised initializer of a non-class object, 5.2.3p1's cast to a non-class type, 12.6.2p7's mem-initializer of a non-class member and 5.19p3's fold of the constant a declaration leaves each took the list's one entry by `children[0]`, so a run of one reached a `pack-expansion-expression` no reader answers for while the class-typed twin C7 converted was right; the arity none of them asked, which had `int x(1,2)` holding 1 where both oracles refuse; and 5.19's own reading of 5.2.3, which now stands a value in for a run 14.6p8 cannot count as `sizeof...` does.  5.19p3's fold became `fold_constant_object`, freeing the function-line ceiling `declare_object_declarator` had crossed | 162 / 184 -> **164 / 186** with two fixtures added; pa1-pa19 2169 / 2169; 88 swept shapes, every accepted pair writing the reference's LowIR; linear and unmoved from the `350c92f4` build at 1 / 64 / 512 / 2048 entries and 256 / 1024 / 4096 readers; valgrind clean |
| C8 | 5.19 outside the integral subset, and the literal it reads: 2.14.3p1's multicharacter literal made a token of the language `PostTokenizer` reads and not of PA2's dump, with the last four c-chars packed one code unit each; 5.19p2's subobject of a string literal answered from a spelling and an index, so a subscript reads as a tree and inside an argument list alike - and an encoding-prefix closes up with its quoted run as `sizeof` does with its `...`; 5.3.3p5's `sizeof...` read out of an argument spelling, so a pattern expanded per element still counts its own run; 3.9p7's incomplete array completed by the definition of the object, both ways about; and 2.14.8p3's literal operator template called with the characters the program wrote, with a cooked call now passing the literal 2.14.2 made rather than the parameter's type.  7.2's enumeration became its own owner (`sema_enum.cpp`), freeing the 3000-line ceiling `sema_analyzer.cpp` crossed | 164 / 186 -> **176 / 191** with five fixtures added; pa1-pa19 2169 / 2169; 80 swept shapes, every accepted pair writing the reference's LowIR; linear at 8192 literals, 4096 elements and 4096 definitions where the reference is 12x slower or dies; valgrind clean |
| C8 audit | the dialect a character-literal is read in, which covers two facts and was moved for one: PA2's dump holds a c-char's code point and refuses a run of them where the language gives every ordinary literal a `char` and a run an `int`, so `CharacterLiterals` now settles both and `sizeof('\\xff')` is 1 rather than 4; 2.14.5p5's execution encoding became one implementation (`append_ordinary_units`) that a string literal's body and a character literal's both call, so `'aé'` is `0x61c3a9` rather than `0x61e9` and a first c-char above the ordinary range is refused as the reference refuses it; `literal_value` reads a literal's bytes with 3.9.1p1's sign, which a `char` literal could not reach before; 5.1.1p6's parentheses are stripped by the spelling reader as the tree already stripped them, so `p<("abc")[1]>` reads; and 2.14.8p3's literal operator template is asked before the raw operator, which is what the reference answers for the set that declares both | 176 / 191 -> **178 / 193** with two fixtures added; pa1-pa19 2169 / 2169; 112 swept shapes, every accepted pair writing the reference's LowIR but the one it drops a character ud-suffix on; unmoved from the `8dfad19a` build at 8192 literals, 8192 string bodies and 4096 ud-literals; valgrind clean |
| C9 | 10p1's base-specifier-list of more than one entry, and the tree it makes: `SemaEntity::bases` and `Scope::bases` as lists, one subobject placed per entry, 10.2p2's lookup asking each base and refusing the name two of them answer, 12.6.2p10's construction in list order and 12.4p8's destruction in the reverse, 14.5.3p4's expansion reaching the ctor-initializer - the parse now records the `...` a mem-initializer wrote and the list is read once per element in the region binding its packs - 10.1p3's repeated base refused where the class is completed, which is what makes every walk of the tree one visit per class, and the ABI's `__vmi_class_type_info` for a class the one public base at the start of the object does not describe.  4.10p3's conversion now asks 14.7.1p1 for the definition of the specialization it converts.  8.5.2's string literal initializing an array of character type became its own owner (`sema_string_init.h`), read the same way from a declaration, a clause and 8.3.4p3's bound; and 10p1's derivation became one (`sema_derivation.h`), which is what freed the header ceiling `sema_analyzer.h` was at | 178 / 193 -> **185 / 196** with three fixtures added; pa1-pa19 2169 / 2169; 29 base-clause shapes and 9 string-literal shapes swept against g++ and the reference, every accepted pair writing the reference's LowIR but the two shapes it already wrote differently; linear at 1600 classes, 1024 direct bases, a base pack of 1024 and 8192 string arrays where the reference is 33-67x slower; valgrind clean |
| C9 audit | the byte a base subobject stands at, at every reader that had only seen zero: 5.2.9p11's cast back to a derived class wrote no step, so `static_cast<C*>(q)` through the second base of `C : A, B` held the subobject's address and a member read through it stood past the end of the object; 11.2p4's access was asked of the region an *expression* was read in, which is given back before an initialization converts, so `B* p = this;` inside the class that named a protected base was refused with every other conversion written as an initializer, a return, a clause or a bound reference; 10.3p1 refused a dispatching base that was not the only base rather than one that does not begin where the object does, so a polymorphic first base beside a plain one - the ABI's own primary base - was refused with the shapes that owe a thunk; 12.6.2p2's index was keyed by the last component of a mem-initializer-id, so `struct both : n1::part, holder::part` initialized one base twice; and 14.6.2p3 was a fact of the base-clause where it is a fact of each specifier, so a settled base beside a dependent one was left off 3.4.1's search | 185 / 196 -> **189 / 200** with four fixtures added; pa1-pa19 2169 / 2169; 77 base-class and conversion shapes and 12 string shapes swept against g++ and the reference, every accepted pair writing the reference's LowIR but the offset-zero step it folds; linear at 1600 classes, 1024 direct bases, a base pack of 1024, 6400 conversions and 4096 casts back, with 10.1p3's own check quadratic in a derivation that adds a base per level and recorded as such; valgrind clean |
| C10 | 14.1p11's *second* place binding a run, and the two things the argument list it makes had never carried: a run that is not the last place stands as one entry of the list (`place_argument`, `trailing_pack_place`), so `<class... U, class... T>` tells `<char, short \| int>` from `<char \| short, int>` and writes each the ABI's own `J...E J...E`; 14.5.3p5 leaves a pack named inside an inner expansion to that one, so `sum(get<U>(t...)...)` is one reading per element of `U`; 14.8.1p2's explicit list fills a non-trailing pack as the run it is; 14.3.2p1's value argument carries the type it was converted to where its digits would not say which value it is - `(policy)2` and `true`; and 14.7.1p1 leaves the storage a static data member stands in to whatever reaches it, which is a name, an object of the class (`demand_object_storage`) or 14.7.3p1's own `template<>` | 189 / 200 -> **196 / 203** with three fixtures added; pa1-pa19 2169 / 2169; 26 pack shapes, 12 value-argument spellings and 12 storage shapes swept against g++ and the reference, every accepted pair writing g++'s own mangled names; linear at 4096 two-run calls where the reference is 15x slower, and one walk per class rather than per object at 4096 objects over a 512-deep chain; valgrind clean |
| C10 audit | the packs a pattern is written over, at both readings that ask and for the two nodes that already expanded one: 14.5.3p5's inner expansion was left out of the tree reading and not of the spelling, so `list<list<A, B...>...>` in an argument list and `: wrap<list<A, B...> >...` in a base-clause were refused as two packs of different lengths where both oracles read them, and a pattern whose only pack the inner one took was accepted where both refuse; 5.3.3p5's `sizeof...` counts a run rather than standing in one and neither reading knew it, so `add(one(sizeof(B) + sizeof...(A))...)` and `nums<(N + sizeof...(A))...>` were runs of two packs at once.  3.2p3's demand for storage was made at 9.2p1's non-static data member, which lays out no object and is read before the definitions written after its class - so a class declaring a member of a specialization laid out storage with no object of it anywhere, and the same class with the definition written after it laid out none where the reference does.  And 14.3.2p1's refusal of a type where a value belongs was asked of the packs a pattern names rather than of the argument each element is, so `sizes<sizeof(T)...>` was refused with `f<T...>` | 196 / 203 -> **201 / 208** with five fixtures added, three of them compile-pass and two refusing; pa1-pa19 2169 / 2169; 20 expansion spellings, 19 value-argument types and 13 storage orders swept against g++ and the reference, every accepted pair writing the reference's LowIR and every mangled name g++'s own; every checked-in `.ref` regenerated from the reference binary and unmoved; linear at 1024 inner expansions and 4096 computed elements, unmoved from the `9196229b` build at 2048 clause entries, 4096 bound elements and 4096 objects over a 512-deep chain; valgrind clean |
| C11 | 14.5.3p4's pattern that is a class template specialization, read once per element like every other pattern: a parameter-declaration became the fourth shape of the one reading (`PackReading::read_places`), so its decl-specifier-seq and its declarator are read again per element in that element's region and `wrap<A>... p` declares `wrap<int>` at the first place and `wrap<char>` at the second where building the type once left both the whole run; and a list a substitution rebuilds binds the pattern's *places* one element at a time and substitutes the written pattern, which reaches `instantiate_class` where `TypeTable::substitute`, having no `Class` case, could not.  `expand_type` stays the structural rebuild for a run already inside a built type, and `note_node` is what merges the packs two trees of one declaration name | 201 -> **204 / 211** with three fixtures added, two of them compile-pass and one refusing; pa1-pa19 2169 / 2169; 44 pattern and declarator shapes swept against g++ and the reference, every accepted pair writing the reference's LowIR but the `arity=variadic` and trailing `z` it marks every specialization pattern with and g++ does not; linear at 1 / 64 / 512 / 2048 elements where the reference is 10x slower, unmoved from the `2a6adc7c` build on a bare place, 0.006 s at a pattern nested 24 deep; valgrind clean |
| C11 audit | the two things a *spelling* writes inside one node, which the reading C11 built its fourth shape on could not see: PA10 flattens a template-id into one terminal, so `list<A, B...>` carries its own inner `...` and `pair2<A, sizeof...(B)>` its own `sizeof...` in the text of one decl-specifier or one callee, where a rule answered by node kind reaches neither - so both were counted as packs the enclosing run is over and `list<A, B...>... p` and `pair2<A, sizeof...(B)>... p` in a parameter clause, and `count(list<A, B...>()...)` in a call at the very reading C10 changed, were refused as two packs of different lengths where g++ reads them and the reference reads the call.  `names_in` reads each node's text with `spelled_names_in` now, so the spelling, the tree, the parameter-declaration and the type a substitution rebuilds are one answer | 204 / 211 -> **205 / 212** with one fixture added; pa1-pa19 2169 / 2169; runs of 0-3 over a specialization pattern and its pointer, reference, const-reference, member and nested spellings, an unnamed place, fixed places before the pack, two runs in one clause, a forwarding call, a redeclaration, 14.8.2.4p9's ordering and a two-unit program swept against g++ and the reference, every accepted pair writing the reference's own LowIR entries and every mangled name g++'s byte for byte; every checked-in `.ref` regenerated from the reference binary and unmoved; linear at 2048 elements, 48 levels of nesting and 1024 inner expansions, and unmoved from the `51f8f135` build; valgrind clean |
