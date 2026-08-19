# PA23 Audit — deduction, substitution and SFINAE

A review of each landed checkpoint, in the order one use of a function template
travels: what a deduction settles, what building the declaration it settled
means, and what a failure of that building says about the candidate that asked.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| 1 | `188e92bc` | 3 / 3 + 3 recorded | **the scope 14.8.2p8 makes of one attempt, landed with no bound on the attempt asking for itself - and the order the same sentence says that attempt is made in.**  Checkpoint 1 made a substitution failure candidate state: `Substitution` wraps the three deduction entry points and the written-argument-list `specialize`, `Instantiated` is the class body 14.7.1p1 read walking past it, 14.1p3's unnamed place is declared, a dependent value argument keeps its spelling and is read again, `dependent_member_type` builds over the class *this* substitution made, and 14.5.6.1p5 rather than 13.1's index pairs two templates.  Those rules are right and swept clean - 8 unnamed-place shapes, 10 SFINAE and qualification-conversion shapes including the multi-level pointer 4.4 refuses, 6 shapes of the `Instantiated` boundary, and the discarded attempt leaves nothing a later naming reads, because the failure happens before `hold_specialization` and the class instantiations it made are 14.7.1p1's and permanent.  What none of it carried is that the same clause bounds the attempt: `Specialization::chosen` and `SemaAnalyzer::specialize` each hold their answer only once it is built, so a request arriving while that same one is being served recursed without bound - and the two new readings closed the loop, a dependent value argument read again at substitution and a stand-in rebuilt over the class the arguments named.  `300-recursive-streamable-sfinae-guard.t` and `300-dependent-adl-hidden-friend-before-later-value.t` are two programs the pre-checkpoint build translated to exit 0 and this one **exhausted the stack on**, reported by the harness as ordinary `EXIT_FAILURE` mismatches, with `300-recursive-trailing-return-sfinae-cache.t` the same crash one tier down and older than the checkpoint.  `TemplateInfo::choosing` and `SemaAnalyzer::specializing_` are the mark each tier takes, and a re-entrant request is refused - which 14.8.2p8 turns into the candidate discarded, the answer `g++` gives all three.  Beside them: 14.8.2p8's *lexical order*, which `substituted` read backwards for every declarator - the parameter list was built first however the return type was written, so a leading result type whose substitution is a hard error was never reached when a parameter was what failed, and `SemaEntity::trailing_result` is 8.3.5p2's fact that says which came first; and 14.1p12, which has no door at the pair 14.5.6.1p5 had just made findable, so two heads of one template each giving one place a default was a program both oracles refuse and this build translated.  Recorded rather than fixed: 14.6p2 is asked where the *pattern* reading reads a type-specifier, so five shapes whose reading a dependent context defers reach the clause with a prefix an argument list has already settled; and n declarations of one template name is quadratic in PA22's `TemplateSignature::equivalent` walk |
| 2 | `a63c183b` | 4 / 4 + 6 recorded | **the name the object file gives an address argument was this unit's entry number for it, and 14.3.2p5's conversions were 8.5's.**  Checkpoints 2-5 made 14.1p4's address places, 10p1's `class-or-decltype`, 13.3.1.4p1's constructor template, 14.6.2p2's variable template and 14.6.2p1's settled prefix.  Those rules are right and swept clean - 11 base shapes, 7 converting-constructor shapes, 8 array-bound shapes, a variable-template stand-in that leaves no global behind, and `Substitution` having no destructor, so `took_places` standing lexically inside the attempt is the walk outside its `try` the comment claims.  What none of it carried is that the *bits* of an address argument are an entry of the constant-address table, numbered in the order this unit reached each address: `at<&left>` was written `_ZN2atILPi1EE4readEv` where both oracles write `_ZN2atIXadL_Z4leftEEE4readEv`, and two units of one program that each name it write one weak definition under two names, which no link can merge.  The suite could not see it, because the comparator drops `object=` from every function header before comparing - the checkpoint's own `100-an-address-argument-is-which-object-it-designates` fixture disagreed with its `.ref` on all five names and passed.  Beside it: 14.3.2p5's conversions were `at_pointer_place`/`at_reference_place`, which are 8.5's readings of the same places and take the three the clause's own note leaves out - a zero-valued integral constant, 4.10p3's derived-to-base, and any pointee at all - so `at<&d>` at a `B2 *` place over a `struct D : B1, B2` was accepted and *ran to the wrong storage*; 14.1p4's fifth place was declared by `non_type_place` and refused by every argument reader, so `template<decltype(nullptr) N>` was a head no list could fill; and 13.4p1 had no door at a function place, so `H<f>` beside two declarations of `f` took whichever the chain led with.  Recorded rather than fixed: a pointer-to-member place, which the layer below has no pointer to member at all for; a `void *` place; `(int*)0`, whose cast the spelling reader reads only integral targets of; and `char (&a)[sizeof(T)]` as a function parameter, which the pre-checkpoint build refuses identically and is PA22's |
| 3 | `30c7c2b5` | 3 / 3 + 3 recorded | **14.6.4.2p1's bound was written for three narrow readings and set to *none* for the two largest ones, so an instantiated body reached the whole unit and chose a later overload.**  Checkpoint 7 made three things: 14.1p9's default over a list an argument has yet to settle travels as a tree and a region; 14.6.4.2p1 puts 3.4.1's half of a second reading back at the definition context, through `declared_serial` and `ReadingBound`; and 3.9.1p8's floating scalar crosses 5.2.2p4's boundary by address.  The first and the third are right and swept clean - 10 shapes of the default over a dependent prefix agreeing with the reference on all and with `g++` on 9, and 28 boundary shapes byte-identical to the reference through the real comparator and running to `g++`'s value on all 28 (the boundary sweep the checkpoint recorded was vacuous: every one of its 20 programs returned a scalar from a function declared to return the class, so both binaries refused all 20 and the comparator called it a pass).  The second landed only for the readings a *pattern* interned - the decltype-specifier, the two tiers of 14.1p9's default - and wrote `ReadingBound(model_, 0)` at `instantiate_body` and at `complete_specialization`, which is every function body and every class body 14.7.1p1 reads again.  So `template<class T> int f(T t) { return late(t); }` above `int late(int);` was a program both oracles refuse and this build translated, `pick(long)` above the template and `pick(int)` below it ran to `pick(int)` where `g++` runs to `pick(long)`, and the same held of a member body, an out-of-class definition, a partial specialization's body and an alias template's type-id.  The bound is a fact of *each* definition, so `TemplateInfo::visible`, `Partial::visible`, `Member::visible`, `WrittenBody` and `PendingDefinition::visible` carry it and each reading sets its own - taken after the first reading of the body, because 11.3p1's friend and the template's own name are declarations the body itself has to find.  `ConstexprReading::selected` passing `kAssociatedUnknown` - the gap the plan recorded as reaching no fixture - is the same clause at the fold, and `W<int>::v` over `fold(long)` above and `fold(int)` below ran to 200 here against 100 in *both* oracles.  Recorded rather than fixed: 8.3.6p9's default *function* argument, which the non-template `int g(int n = late());` refuses identically and is no reading of a template's; a static data member's dependent initializer, deferred past the class body, where the reference agrees with us against `g++`; and 8.5.4p3's narrowing at a value place, which is the plan's own `static_assert` group |
| 4 | `07cb3fb3` | 3 / 3 + 3 recorded | **the refusals landed at one exit each, and the fact 3.9.3p5 made had one reader of four.**  Checkpoint 9 gave 14.8.2p8 four things to fire on: 8.3.2p5 and 8.3.4p1 through one *door* the three type-deriving readers call, 10.4p2 as a fact of the type, 5.7p1's completely-defined pointee, 8.5.4p7 at the constructor a list-initialization chooses, and 14.3.3p1 at `instantiate_class`.  Those rules are right and swept clean - the door refuses through a declarator, a flattened spelling, a pack expansion and a substitution alike, an abstract class reached by inheritance is one, 14.3.3p1 answers the same at a class template, an alias, a variable template, a function template, a pack and a naming that never instantiates, and 8.5.4p7 refuses through all seven initialization paths a constructor is reached by.  What none of it carried is that each refusal has one exit more than the checkpoint wrote.  5.2.1p1's pointee is 5.7p1's - `Inc *p; p[0];` is a program the reference and `g++` both refuse and this build translated, and its own comment restated the clause the code never asked.  8.5.4p7's fourth bullet was written as width and equal-width signedness, which is not "cannot represent all the values": signed reaching a *wider* unsigned has negative values at every width, and `bool` holds two of them however wide its storage - so `unsigned x[] = { g() }` off a `char g()` and `bool x[] = { g() }` were translated, and the same statement gated the fold, so `unsigned x[] = { (char)-1 }` never reached the constant exception either.  And 3.9.3p5 landed at `pointer_convertible`'s 4.10p2 arm alone: 4.4's own walk, 5.9p2's composite pointer type and 14.8.2.1p2's conversion each read `cv` of an array node, which is zero, so `const int (*p)[3] = &a;` was refused four ways - beside a walk that asked 4.4p4's second condition of the level it had just compared, which refused `volatile int *q = p;` with no array in it at all.  225 + 675 narrowing shapes and 20 qualification shapes now agree with `g++` on all, `200-range-array-reference-mutable-begin.t` turned, and every course `.ref` regenerated from the reference binary is unchanged.  Recorded rather than fixed: `void *p; p[0]`, which the reference accepts and `g++` and this build refuse; a subscript detector, which the reference refuses whole so no fixture can pin it; and `typedef abstract_class a[2];`, which the plan already carries |
| 5 | `54fc10e2` | 1 / 1 + 3 recorded | **the naming the checkpoint made a second type of is the type its type-id named, so every declaration a program wrote twice stopped pairing.**  Checkpoint 11 gave 14.8.2p8 the one thing 7.1.3p2's collapse had taken from it: a naming of an alias template whose type-id threw an argument away keeps that argument, so `discard<typename T::pointer>` is built where 14.7.1p1 arrives and a `T` with no `pointer` is a candidate dropped.  That rule is right and swept clean - the detector answers the two classes apart at eight syntactic sites and runs to `g++`'s value at all of them, `held<Ts &...>` binds a pattern that is not a place through six shapes, `note_places` carries a value argument's packs through a reading read again, and 14.3.3p1's place takes an alias.  What none of it carried is 14.5.7p1, the other half of the same sentence: a template-id over an alias template *is* the associated type, so an entry standing beside that type is a second type for one the program may also write out longhand - and `template<class T> void f(discard<T> *)` declared beside `template<class T> void f(void *)` defined stopped being two declarations of one template.  Three readers ask it: `TemplateSignature::of`'s canonical form at 14.5.6.1p5's four tiers, 13.1's index of a parameter-type-list, and the return-type comparison two declarations of one function are paired by - so a function template, its out-of-class definition, a member of a class template, a member template of a class that is not one, and a naming under another argument list were five programs both oracles and the pre-checkpoint build accept and this one refused.  `rebuilt` is where the two halves meet: an argument that is a *place* is looked up by the substitution and can refuse nothing, so the naming collapses to what its type-id named exactly as 14.5.7p1 says, and an argument the substitution *builds* again - a member of a prefix, a specialization, a derived type, an expression read a second time - is what the entry is kept for.  Recorded rather than fixed: the same equivalence where the discarded argument is one of those readings, which needs the associated type as a canonical form rather than a scope; 8.3.1p4 and 8.3.3p3 at a deduction, where the two oracles disagree with each other; and `TypeTable::substitute`'s structural rebuild, which the plan already carries |
| 6 | `e4191b39` | 2 / 2 + 3 recorded | **the list a member wrote is a second fact of its name, and the two readers that had been asking the old one question got a wrong answer each.**  Checkpoint 13 made 14.2p4's argument list part of what a dependent member's name stands for: it is read where the reading stands, interned beside the prefix and the name, built again by `Substitution::member` and finished at 14.3.3p1's two exits, with a class that declares no such member template 14.8.2p8's failure.  That rule is right and swept clean - 24 shapes of the idiom across the sites a type-specifier stands at, the member kinds a class can declare and the argument kinds a list can hold, agreeing with `g++` on all 24 and running the 21 accepted ones to its value; the ABI writes the third form of `<unresolved-name>` byte-identical to the reference and to `g++` for the plain, the pack and the value form, and two units naming one such signature write one weak definition under one name.  What none of it carried is that widening `dependent_member` from the whole written component to the bare name left 14.6p2's reader comparing the two: `require_written_type` asked whether the stand-in's member equals `written.last()`, which for `T::template box<int>` is `box<int>` against `box` - so `T::template box<int> b;` written with no `typename` stopped being refused, a program `g++` refuses and the pre-checkpoint build refused.  And the value half copied 7.1.5p2 onto every specialization `specialize` makes, which is every specialization the *program* wrote out too: `template<> int f<int>()` declared without `constexpr` beside a `constexpr` primary folded, so `char a[f<int>()]` was a program both oracles refuse and this one translated.  The copy is gone and the clause is asked where it is needed - `constexpr_declared` reads the template's flag for a specialization that is not 14.7.3p1's, which is the one reading that writes no definition to read it off, and the same answer is what says whether the refusal is 5.19's about the program or the edge of this build.  Recorded rather than fixed: a template-id component after a *decltype* prefix, which the pre-checkpoint build refuses identically and both oracles accept; 14.6p2 itself, which the reference implements at no shape at all so no fixture can pin it; and `TypeTable::substitute`'s structural rebuild, which leaves a member stand-in standing as it leaves a discarding naming and which the plan already carries |
| 7 | `adf23e13` | 2 / 2 + 3 recorded | **the number a second binding stands at, and the count a call hands the ordering.**  Checkpoint 15 made 14.8.2.4p3's `limit` - `kEveryPlace`, the count the call wrote arguments for, or `kResultPlace` - the answer `ordering_parameters` and `ordering_places` read, with 14.8.2.4p12's trailing run a place of its own past it; 14.8.2.3p2 and p4 taking the reference and the top-level qualification off P and A wherever the *other* one came from; and 14.6.4.2p1's number taken from the binding a second declaration stood over.  Those rules are right and swept clean - 135 generated ordering shapes across five candidate kinds (a free pair, two non-static members, two static members, a static against a non-static one, and two non-member operators) crossed with three first-parameter shapes, five trailing-default shapes and one or two written arguments, agreeing with `g++` on all 135 and running the 124 accepted ones to its value, 111 of them programs the pre-checkpoint binary refuses; ten conversion shapes agreeing with `g++` on all, including the reference and the qualification arriving from either side; and the memo answering one pair under five call arities.  What none of it carried is that both new facts are read one step wide of where they were settled.  3.3p4: the number is the first binding's only where the two declarations declare the *same kind* of name - `struct probe;` above a pattern and `int probe(int);` below it bind one spelling to two entities, and numbering the function where the class stood put it in the pattern's reach, so a program both oracles refuse and the pre-checkpoint build refused was translated to 8.  And 14.8.2.4p3's limit is 13.3.1p3's count, which holds a place for the implicit object argument whether or not a candidate wrote a parameter for it: `s.f(&v)` over two static member templates was ordered over two places where the call wrote one, so a trailing default decided the ordering and left the pair unordered - while `S::f(&v)`, the same pair over the same argument, came out right.  `ordering_limit` is that count restated in the places the two lists hold, which is the object place kept where either declaration wrote one and dropped where 9.4p1's static member of the same class is what the other is ordered against.  Recorded rather than fixed: 13.5.2p1's arity of a non-member operator function, which has no reader at any tier and which the checkpoint's own ordering un-hid - `operator-(tag, T, int = 0)` is a declaration `g++` refuses and the reference and this build accept, and before the checkpoint the *call* over it was refused for having no best declaration; the three static-member orderings this audit turns, which `g++` accepts and the reference refuses along with every other shape of p3's first bullet; and `B<int>::held` named in a pattern above the definition of `B`, which the pre-checkpoint build accepts identically |
| 8 | `26f065b2` | 2 / 2 + 3 recorded | **12.9p2 lists four constructor characteristics and the checkpoint landed the first, and 12.9p3's exclusion was read of a declaration it does not name.**  Checkpoint 17 made a using-declaration that names a base's constructor *template* declare a constructor template here: the base's own head rather than a copy of it, `TemplateSignature::indexed` as 13.1's key, `specialize` carrying 12.9's facts and resolving `inherited` to the base's specialization over the same list, `demand_constructor_definition` taking the places from the template's record with a pack entry expanded to the run this list bound, and 12.9p8's `static_cast<P&&>(p)`.  Those rules are right and swept clean - 47 probes across the base kinds, the argument kinds, the chain, the pack, the ellipsis, the two-parameter and partial-ordering pairs and the constant contexts agree with `g++` on every one, two units naming one such specialization write one weak definition under `_ZN4keptC1IiEET_` which is the reference's own name for it, and `parameter_value` opens a dump node per call so 12.9p8's category is written on nothing shared.  What none of it carried is the other three sentences those two paragraphs are made of.  12.9p2's fourth characteristic - `constexpr` - had no reader at any tier, so a base constructor the program wrote it on left a derived class 3.9p10 calls no literal type: `constexpr derived d(3);`, `char a[derived(3).v]` and the same through a constructor template were programs both oracles build and this one refused.  It travels with the declaration and to the specialization beside 12.9's other three, and 12.9p8 is what the *fold* then needs - `constructed_object` writes one mem-initializer entry for the base the using-declaration named, carrying this constructor's own arguments rather than a tree to read clauses out of, so the base subobject is built by `object_of` exactly as a written mem-initializer's is and is not default-constructed to a silent wrong value.  And 12.9p3 excludes a base constructor whose parameters a constructor the class *declared itself* already takes: an inherited one is the other declaration that index probe finds, and standing one of the two for both dropped a whole base's candidate set - `struct both : left, right` inheriting one parameter-type-list from each ran to `left`'s constructor where both oracles refuse the use for having no best declaration.  Recorded rather than fixed: 7.1.5p4's every-member-initialized at an inherited constructor, where `g++` alone implements C++14's relaxation; 12.9p3 at a *non-template* pair and a class's own constructor beside an inherited one, two shapes the reference answers against `g++` and this build; and the run scaffold's five-parameter ceiling, which is `lowir2cy86`'s and reads the same from the reference's LowIR |
| 9 | `30118790` | 2 / 2 + 4 recorded | **the demand 5p8's operand does not make is one a constant expression inside it still needs, and one spelling function was asked two questions.**  Checkpoint 19 made 14.7.1p1's second sentence at a member class - `hold_member_class` records the class-specifier beside the region the enclosing class opened, 14.6.4.2p1's bound and the dialect that reading spoke, and `complete_held_class` reads it at 3.9p5's first demand - beside 3.2p2 at a naming, 5.1.1p3's object at a member template's declarator with `DependentDecltype::self` carrying it into the second reading, a value spelled at the LowIR type holding it, and 14.1p4's address table asked of a pointer or a reference place alone.  Those rules are right and swept clean: 41 member-class shapes - layout, a data member of the held type, an array, a base outside, `new`/`delete`, a virtual member, a destructor, a nested member class two deep, a member class of a member template, a partial specialization's own, a `template<>` for one and one for a member no class declares, an out-of-class member definition, a friend, an enum, a typedef, a member template of it, and one nothing completes - agree with `g++` on acceptance and on the value at every one, and the mangled names of the address places are byte-identical to the reference's across five cross products.  What none of it carried is that 3.2p2 and 14.7.1p1 are two sentences about one naming with two answers: 5p8's operand odr-uses nothing, which is what `named_function`'s new gate says, and a *template argument* written inside it is a constant expression whose fold requires the definition to exist all the same - so `decltype(box<width<int>()>())` and `noexcept(taking<width<int>()>())` were `width is not a constexpr function this unit has defined`, four programs both oracles and the pre-checkpoint build accept.  The demand belongs to the fold: `ConstexprReading::call` asks 14.7.1p1 for the body where it needs one and asks the unit for no definition, so the operand still writes no symbol and our LowIR is byte-identical to the reference's.  And `spell_value` had two consumers with two answers: the operand of `global @x : i64 = v` names the whole storage of a scalar object and is spelled at the LowIR type - `= -1` and no wider spelling of the same bits, which is the reference's and what the checkpoint gained a test for - while an *item* stands for one clause of the image and carries that clause's digits, so `{ 1UL, 18446744073709551615UL, 2UL }` is `i64 18446744073709551615` there and the checkpoint made it `i64 -1`, a difference the real comparator fails on.  The question is the caller's now.  Recorded rather than fixed: the reference's item and instruction-operand digits, which are the written clause's at every width - it writes `u32 -1`, `u16 -1` and `u8 -1` - and which this build has no reading of the clause to spell from, the same half of the same sentence as the floating item the plan already carries; a function template's trailing return type mangled `T_` where `g++` and the reference both write `DT...E` or the resolved type, which `object=` canonicalization hides from every fixture; a decltype-specifier in a member template's declarator read at the point of instantiation rather than where it stands, so `decltype(sizeof(*this))` reaches a complete class where both oracles refuse it, which the pre-checkpoint build accepts identically; and 14p2's local class shall have no member templates, which the reference accepts as this build does |

## Current Checkpoint Review

Checkpoint 19 - 14.7.1p1's laziness and the object 5.1.1p3 gives a member
template's declarator: `PatternReading::hold_member_class` and
`complete_held_class` over `DependentReadings::classes`, 14.7.3p1's `template<>`
for a member class asked where the body is held as well as where it is read,
3.2p2 at a naming with `Evaluated` taking 5p8's operand off at each
instantiation's door, `declarator_object` walking out through the heads a member
template wrote with `DependentDecltype::self` carrying that object into the
second reading, `spell_value` reading the LowIR type, and 14.1p4's address table
asked of a pointer or a reference place alone - was reconstructed from its four
commits, from `dev/src` and from the README: what an instantiation declares and
what it defines, which reading completes a held body and under which bound and
dialect, what `this` stands for from a member template's cv-qualifier-seq to the
end of its declarator, and which digits a value is written with at each of the
places LowIR spells one. Two defects were found and fixed across the reader path
each reaches, four gaps were probed as programs and recorded, and the rest is
what the review confirmed.

### Findings

**1. 3.2p2 and 14.7.1p1 are two sentences about one naming and the checkpoint
gave them one answer.** 5p8 leaves the operand of `decltype`, `sizeof` and
`noexcept` unevaluated, so a function named there is odr-used by nothing and the
unit owes no definition of it - which is what the checkpoint's `used` gate in
`named_function` says, and it is right. What it also stopped doing is
14.7.1p1's own sentence, which is not about the symbol but about the body: a
specialization is instantiated when it is referenced *in a context that requires
the function definition to exist*, and a template argument written inside that
operand is a constant expression whose value has to be read:

```cpp
template<int N> struct box { char pad[N]; };
template<class T> constexpr int width() { return sizeof(T) + 1; }
template<int N> int taking() { return N; }
typedef decltype(box<width<int>()>()) inside;          // width is not a
bool ok = noexcept(taking<width<char>()>());           // constexpr function
                                                       // this unit has defined
```

Both oracles and the pre-checkpoint build translate all four shapes swept - the
free template inside `decltype` and inside `noexcept`, a member template of a
class that is not one, and the same naming written above an evaluated one. The
fold arrived at a declaration with no body and read the refusal `constexpr_body
== nullptr` writes.

The demand belongs to the fold, because the fold is the context: `ConstexprReading::call`
asks 14.7.1p1 for the definition where it needs one and asks the unit for
nothing, so the operand still odr-uses nothing and the emitted LowIR is
byte-identical to the reference's - no `@width` definition stands in either. It
is asked only of a specialization whose body is not yet read, outside 14.6p8's
dialect, so a call that was already named in a potentially-evaluated expression
and every call inside a pattern's own reading pay one field read. The sweep's
one behavior change beside the four fixes is `decltype(box<need<T>()>())` over a
`need<T>` whose *body* is ill-formed, which is now the hard error `g++` makes of
it - 14.8.2p8's immediate context does not reach into an instantiated definition
- where the reference discards the candidate.

**2. `spell_value` has two consumers and the checkpoint gave them one answer.**
`lowir.md` names no `u64`, so the operand of `global @x : i64 = v` - which names
the whole storage of a scalar object - is spelled at the LowIR type's
signedness, and `unsigned long g = (unsigned long)-1;` is `= -1` and no wider
spelling of the same bits. That is the reference's answer and the checkpoint's
gain. But the same function spells the *items* of a structured global, and an
item is not that operand: it stands for one clause of the image the program
wrote and carries that clause's own digits.

```cpp
unsigned long a[3] = { 1UL, 18446744073709551615UL, 2UL };
// reference: i64 1 / i64 18446744073709551615 / i64 2
// checkpoint: i64 1 / i64 -1 / i64 2
```

The pre-checkpoint build agreed with the reference on this shape and disagreed
on the scalar; the checkpoint traded one for the other. It is a difference the
*real* comparator fails on, not one `object=` canonicalization hides: the same
two files run through `compare_results.pl` from a scratch directory under
`tests/` report `generated LowIR does not match reference`.

The question is the caller's now - `image_value` and `constant_text` take
`stored`, the two namespace-scope operands pass it, and the items ask what they
asked before. Every shape swept agrees again: a scalar global written either
way, an array item, a class item, a `u8`/`u16`/`u32` item, a `switch` label, an
enum, a value place and a static member.

### What the review confirmed rather than found

- **14.7.1p1's laziness answers what `g++` answers.** 41 member-class shapes -
  the layout of a class holding one, a data member of the held type inside and
  outside the template, an array of it, a base outside, `new`/`delete`, a
  virtual member, a destructor, a nested member class two deep, a member class
  of a member template of a class template, a partial specialization's own, a
  `template<>` for one and one for a member no class declares, an out-of-class
  member function and static data member, a friend, an enum, a typedef, a member
  template of it, a completion demanded inside `decltype`, a completion inside a
  substitution, and one nothing ever completes - agree with `g++` on acceptance
  and on the value at every one, and the emitted LowIR is the reference's
  wherever the two accept. `template<> struct outer<char>::nosuch { … };` and a
  `template<>` written after the class holding it was instantiated are two
  programs `g++` refuses, the reference translates, and this build refuses.
- **The object 5.1.1p3 gives a member template's declarator is `g++`'s.** 15
  shapes - a plain member template, one of a class template, one defined out of
  class, `const`, `decltype(this)`, a member of a member class, a member of a
  member class of a class template, a conversion template, a default argument -
  agree with `g++` on all, and the reference refuses six of them at every shape
  swept, which is the divergence the checkpoint recorded. A `static` member
  template writing `this` and a namespace-scope function template writing it are
  refused three ways.
- **14.1p4's address table is asked of the two places that bind an address.**
  Five cross products - a value place, a pointer place, a reference place, a
  function place written with and without `&`, an enum, a `bool`, a `char`, an
  `unsigned long`, two addresses beside two values, and the address of a
  specialization whose own value argument is a small integer - write `object=`
  names byte-identical to the reference's at every one. The LowIR entries the
  diff shows are internal symbol names, which `lowir.md` makes a presentation
  tie-breaker and which the real comparator canonicalizes: run through
  `compare_results.pl`, both programs pass.
- **Nothing is gated and no phase is skipped.** The checkpoint's changed source
  and this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read and no dialect switch keyed on anything but a dialect; the one
  `catch (...)` on the path puts two fields back and rethrows, and
  `complete_held_class`'s two catches do the same before turning what the body
  refuses into 14.8.2p8's `Instantiated`, exactly as `complete_specialization`
  does.
- **Every course `.ref` is the reference binary's.** All 38 were regenerated
  through the harness from `cppgm++-ref` and not one changed; the two new ones
  are this audit's, and the reference builds both and writes what this build
  writes.
- **`valgrind -q --error-exitcode=9` is clean over 286 inputs** - the 38 course
  fixtures, 108 probes and 140 of the corpus - and no input exits above 1 in the
  4883 compilations of pa10 through pa39 run one at a time under their own
  dialects.

### Recorded rather than fixed

- **The reference spells an item and an instruction operand with the digits the
  written clause had, at every width.** `unsigned int c[2] = { (unsigned int)-1,
  4294967295u };` is `u32 -1` and `u32 4294967295` there - a negative spelling at
  an unsigned LowIR type - and `long b[2] = { -1, (long)18446744073709551615UL };`
  is `i64 -1` and `i64 18446744073709551615`. It follows through a cast and
  through a named constant, so it is a fact of the clause and not of either type.
  This build has no reading of the written clause to spell an item from and
  answers with the type the clause was written at, which is right for every item
  but one written as a conversion of a negative value - the same half of the same
  sentence as the floating item the plan already carries, and the same answer the
  instruction operand of `cmp eq i64 -1, …` wants.
- **A function template's trailing return type is mangled `T_`.**
  `template<class T> auto call(T v) -> decltype(grow(v))` is
  `_Z4callIiET_S0_` here, `_Z4callIiEDTcl4growfp_EET_` in `g++` and
  `_Z4callIiEiT_` in the reference, and `holder::self` returning `decltype(this)`
  is `_ZN6holder4selfIiEET_S1_` here against `_ZN6holder4selfIiEEPS_T_` in both
  oracles. The two oracles agree against this build wherever the return type is
  written as one, so it is a defect and not a judgment call - but `object=` is
  stripped before every comparison, so no fixture can pin it, and this build's
  own two units agree with each other. It is older than the checkpoint, which
  reached it only by making one more such declaration acceptable.
- **A decltype-specifier in a member template's declarator is read at the point
  of instantiation and not where it stands.** 9.2p2 makes a member function
  *body* a complete-class context and leaves the trailing-return-type outside
  one, so `decltype(sizeof(*this))` and `decltype(sizeof(holder))` written in a
  member template are programs both oracles refuse and this build translates -
  the specifier is put aside as a dependent reading whatever it names, and by the
  time the substitution reads it the class is complete. The non-template shape is
  refused where it stands, and the pre-checkpoint build accepts the template one
  identically.
- **14p2's "a local class shall not have member templates" has no reader.** A
  member function template and a member class template of a class declared in a
  block are programs `g++` refuses and the reference and this build translate,
  with no `this` and no trailing return type in them; the reference names the
  member `_ZZ4mainEN5local3getIiEET_S0_` and this build names it as a member of no
  function at all. It is older than the checkpoint.

### Changes

| Where | What |
|-------|------|
| `sema_constexpr.cpp` | 14.7.1p1 at the fold: reading what a call comes to is a context that requires the definition to exist, so the demand 5p8's operand does not make is made by the reading that needs the body - and by it alone, so the operand still asks the unit for no definition. |
| `lowir_image.cpp` | the two questions `spell_value` was answering as one: the operand naming a scalar object's whole storage is spelled at the LowIR type, and an item of a structured global keeps the signedness of the clause it stands for. `image_value` and `constant_text` carry which is being written. |
| `lowir_lower.h` | `stored` on the three declarations, which is the caller saying which of the two it is. |
| `course/pa23/100-a-constant-expression-inside-an-unevaluated-operand-asks-for-a-body.t` | the argument list inside `decltype` and inside `noexcept`, and the definitions neither odr-uses. |
| `course/pa23/100-an-item-of-a-structured-global-keeps-its-own-signedness.t` | `= -1` at the scalar operand and `i64 18446744073709551615` at the item, in one program. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `30118790` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| constexpr-unevaluated multiplicity | n constexpr function templates, each named once inside a `decltype` whose template argument folds it | 0.00 s @32, 0.02 @128, 0.07 @512, 0.14 @1024 - linear, and the same on the pre-*checkpoint* binary; the pre-audit binary refuses the program at the first fold |
| constexpr-unevaluated nesting | d constexpr function templates, each folding the one below, one naming inside a `decltype` | 0.00 s flat from d = 4 to d = 32 |
| constexpr-evaluated multiplicity | n folds of one constexpr function template at an array bound - the path that already had a body, which now asks one field read more | 0.00 @32, 0.01 @128, 0.03 @512, 0.06 @1024 - and the same on the pre-audit binary |
| item multiplicity | n eight-byte unsigned items of one structured global | 0.00 @1024, 0.02 @4096 - and the same |
| member-class multiplicity | n class templates, each a member class the use requires complete | 0.00 @32, 0.02 @128, 0.07 @512, 0.16 @1024 - and the same |
| held-member multiplicity | n member classes nothing completes, so `declared_only_` never returns to zero | 0.00 @32, 0.02 @128, 0.09 @512, 0.19 @1024 - and the same |
| declarator-object multiplicity | n member templates of one class, each a `decltype(this->v)` trailing return, one call apiece | 0.00 @32, 0.02 @128, 0.07 @512, 0.14 @1024 - and the same |
| whole PA23 corpus | 400 handout and 38 course files, one process each | **2.05-2.09 s against the pre-audit binary's 2.07-2.10 s**, over three alternating passes after a warming pass of each; no `rc > 1`; valgrind clean |

The fold's demand is `callee.constexpr_body == nullptr` and two field reads,
made where the walk was about to refuse - so a callee whose body is already read
pays the two reads and a callee inside 14.6p8's dialect is never asked at all,
which is what the evaluated-fold sweep says. `instantiate` is the same door the
naming would have gone through and holds `instantiated` itself, so a body is read
once however many folds arrive. The item's spelling is one `bool` handed down
two call chains and no question anyone asks twice.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **396 / 440** (handout
  356 / 400, course 40 / 40), against 394 / 438 at the turn's start: the failing
  44 are the same files name for name, and the two the count moved by are this
  audit's own fixtures.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- 108 probe programs through `g++ -std=c++11 -pedantic-errors -x c++`, the
  reference binary, the checkpoint binary and the pre-checkpoint binary, with
  every accepted one's LowIR diffed against the reference's and, where the
  scaffold's ceiling allows, linked through `lowir2cy86` + `cy86` and run to
  `g++`'s value. Four cross products were judged through the real
  `compare_results.pl` from a scratch directory under `tests/`, which is what
  says an internal symbol name is canonicalized and an item's digits are not.
- All 4883 files of pa10 through pa39 compiled one at a time under their own
  dialects: **0 exits above 1**; 286 inputs under `valgrind -q
  --error-exitcode=9`: 0 errors.
