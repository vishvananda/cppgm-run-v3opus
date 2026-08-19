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
| 10 | `a7ef53a2` | 3 / 3 + 7 recorded | **14.7.3p5 says which definition a member *is* and the checkpoint let it answer which unit writes one, and 14.5.3p4's run landed at one of the two doors that declare it.**  Checkpoint 21 made 14.8.2.6p1 the question `template<>` is matched by - `explicit_target` reads the declarator's type once and asks each candidate the question its shape allows, `gather_deduced` deduces the whole list where the head wrote none, 14.5.6.2 orders a pair the type fits two of - beside 14.7.3p5's `made_by_an_instantiation` and 14.5.3p4's run in a region `substituted_region` rebuilds.  The match is right and swept clean: 54 explicit-specialization shapes - a written list, a leading part, no list at all, a member, a static member, an operator, a conversion, a namespace-qualified name, a pack, a member of a class template, a member template of a `template<>` class, one nested two deep, a declaration and then its definition, and an ordered pair - agree with `g++` on acceptance at every one and are byte-identical to the reference through the real `compare_results.pl` wherever the reference accepts, and the declarator being read a second time leaves nothing behind, through a specifier that declares a class, a parameter that declares one and a parameter type that instantiates a class template.  What none of it carried is that `abi_instantiated` had two readers and only one of them wanted the new answer.  `shared_definition` asks whose definition it is, which 14.7.3p5 answers; the deferral exception asks 3.2p4's question about the *source*, which it does not - so `inline int box<float>::q()` that nothing calls became a definition this object file writes where the reference and `g++` both write nothing, while `inline box<float>::box(int)` written the same way is one both do write because 12.1 and 12.4 put its body under both of the ABI's entry points.  An 18-shape cross product of three class kinds, three definition kinds and used-or-not now agrees with the reference on all 18.  And 14.5.3p4's run: `bind_place` is the door *both* pack loops call - the declarator's own and `read_places`, which is the one a substitution takes - and it dropped `pack_run` and the element linkage, so `auto call(F f, A... a) -> decltype(f(a...))` over a settled run of one or more found `a` bound to a plain type that no longer said a pack was written.  That is the fourth door the plan recorded as built by none of the three, and with the run carried onto the declaration 8.3.5p10 named after the pack, twelve shapes the checkpoint left refused come out right and run to `g++`'s value.  Beside them, 14.7.3p11's count has three answers and the checkpoint refused one: a type no candidate has is refused, a pair 14.5.6.2 leaves unordered is the reference's own translation, and a name holding *no* template - one no declaration wrote, one an ordinary function or object holds, one a class template holds with a function declarator after it - was handed to the ordinary walk and read as an ordinary declaration, which `require_declared_template` refuses now as both oracles do.  Recorded rather than fixed: a `binding=` the comparator strips, where a static data member of a `template<>` class is strong here and in `g++` and weak in the reference; the reference deferring an unused *free* explicit specialization it emits for an ordinary function; `template<> template<>` with no list over a member template, which `g++` and this build run to the specialization and the reference to the pattern; 14.7.3p11's unordered pair and `template<> int S::h()` over an ordinary class, two shapes `g++` alone refuses; `decltype(sizeof...(N))` over a value pack, which the reference reads as `int`; and `decltype((h.*f)(a))` in a trailing return type, refused here with no pack in it at all |


## Current Checkpoint Review

Checkpoint 21 - 14.8.2.6p1 as the question a `template<>` head is matched by,
14.7.3p5's class that is no instantiation, and 14.5.3p4's run in a region a
substitution rebuilds: `Specialization::explicit_target` and `gather_deduced`
over the candidates `template_specializations` and 3.4.1p8's walk find,
`made_by_an_instantiation` in place of `is_specialization` at the two ABI
walks, and `PackReading::rebuild_places` beside `substituted_region`'s
carrying of `pack_run` and `pack_element_of` - was reconstructed from its two
commits, from `dev/src` and from the README: which template a head declares a
specialization of, what an object file owes for a member of a class the program
wrote out, and which declarations a parameter clause leaves for a
trailing-return-type to expand.  Three defects were found and fixed across the
reader path each reaches, seven divergences were probed as programs and
recorded, and the rest is what the review confirmed.

### Findings

**1. 14.7.3p5 says which definition a member *is*, and the checkpoint let it
answer which unit writes one.**  `abi_instantiated` has two readers.
`shared_definition` asks whose definition this is - 7.1.2p4's, so that a body
14.7.1p1 made is one no unit owns - and 14.7.3p5 is exactly the sentence that
answers it: the members of a class the program wrote out with `template<>` are
defined the way a normal class's are.  The other reader is the deferral
exception, which asks 3.2p4's question about the *source*: a definition this
unit was told to write outside its class stands where it stands rather than
waiting for a use.  Widening the first widened the second:

```cpp
template<class T> struct box { int q(); };
template<> struct box<float> { int q(); };
inline int box<float>::q() { return 3; }        // nothing calls it
int main() { return 0; }
```

The reference writes no function here and neither does `g++` - an `inline`
definition nobody odr-uses is nobody's - and the checkpoint wrote one.  The two
questions are told apart by what the class is *named* through:
`abi_named_through_specialization` asks whether a template-id names any class
the declaration is reached by, which is a fact of the spelling and not of the
body 14.7.3p5 gave it.  12.1 and 12.4 are the one exception, and the reference
draws it in the same place: a constructor or a destructor written out of class
stands under both of the ABI's entry points, so the unit writing it owes both
names where it stands and has no use to wait for.

An 18-shape cross product - three class kinds (an ordinary class, a
`template<>` one, one an instantiation made) crossed with three definition
kinds (in-class, out-of-class `inline`, out-of-class plain) and used or not -
now agrees with the reference through the real `compare_results.pl` on all 18,
where the checkpoint agreed on 17 and the pre-checkpoint build on 17.  Each
build failed a *different* one, which is why the count never moved.

**2. 14.5.3p4's run landed at one of the two doors that declare it.**  The
checkpoint made `substituted_region` rebuild the one place a written clause
declared for an unsettled run into the run the arguments settled.  But a
trailing-return-type is read against the region the *declarator's* own reading
left, and both loops that fill that region - the declarator's pack loop and
`PackReading::read_places`, which is the one a substitution takes - hand their
places to `SemaAnalyzer::bind_place`, which built the declaration from the name
and the type and dropped `pack_run` and `pack_element_of` on the floor:

```cpp
template<class F, class... A> auto call(F f, A... a) -> decltype(f(a...));
int g(int v);
int main() { return call(g, 3); }   // an entry of a list is expanded
                                    // and names no parameter pack
```

Once the run is settled each place holds one element's plain type, so the
declaration is the only carrier the fact has left - which is why 14.5.3p5's
`note_name` and 5.3.3p5's `sizeof...` both read it off the declaration and not
off the type.  That is the fourth door the plan recorded as built by none of
the three it had instrumented.  Of the seventeen shapes swept - a run of one, of
two, of none, a member of a class template, a nested expansion, an expansion
before a fixed argument, an rvalue-reference pack, a value pack, `sizeof...`
over the run, and an overload set the trailing type is what orders - every one
that the checkpoint refused now agrees with `g++` and runs to its value, and
`spec/300-dependent-decltype-pack-overload-replay.t` turns.  Two are left: a
value pack's `decltype(sizeof...(N))`, which the reference reads as `int`, and
`decltype((h.*f)(a...))`, which is refused here with no pack in the program at
all.  Both are older than the checkpoint and both are recorded below.

**3. 14.7.3p11's count has three answers and the checkpoint refused one.**
`explicit_target` counts how many templates of the name the head could have
been a specialization of and refuses a declarator whose type fits none of them.
Where the count is *zero* it returns null instead, and the ordinary walk then
reads the head as an ordinary declaration - so `template<> int nosuchname(int)`,
`template<> int plainfn(int)` over an ordinary function, `template<> int obj = 3;`
over an ordinary object, and a *class* template's name with a function
declarator written after it were four programs both oracles refuse and this
build translated.  `require_declared_template` is the same sentence asked of
the name alone, before the match rather than after it: the match hangs a
deduced specialization off each candidate, and a lookup of the name made while
those stand does not terminate.  It is asked of an unqualified declarator-id
alone, because 14.7.3p1's head over a member of a class template specialization
writes the member's name and specializes the class's template.

### What the review confirmed rather than found

- **14.8.2.6p1's match answers what `g++` answers.**  54 explicit-specialization
  shapes - a written list, a list that leaves a place, no list at all, a member,
  a `const` member, a static member, an operator, a conversion, a
  namespace-qualified name, a pack, a member of a class template, a member
  template of a `template<>` class, one nested two deep, a declaration and then
  its definition, a return-type mismatch, a parameter type no candidate has, an
  ordered pair and the overloaded pair the checkpoint was written for - agree
  with `g++` on acceptance at every one outside the eight divergences recorded
  below and in the plan, and every one the reference also accepts is
  byte-identical to its LowIR through the real `compare_results.pl` from a
  scratch directory under `tests/` outside those same eight.  The
  pre-checkpoint binary answers fourteen of the 54 differently and is wrong on
  every one of the fourteen.
- **The declarator being read a second time leaves nothing behind.**
  `explicit_target` reads specifiers and declarator once and the walk that
  records the declaration reads them again, which is one constant factor per
  `template<>` *declaration*.  A specifier that declares a class, a parameter
  declaration that declares one, and a parameter type that instantiates a class
  template all come out identical to the reference; the `catch` on that reading
  puts nothing back because `Naming` unwinds and nothing else is opened, and
  the five shapes whose reading refuses - an unknown parameter type, an
  unresolvable prefix, a `void` parameter, an elaborated specifier, a type no
  candidate has - are refused here exactly as both oracles refuse them, where
  the pre-checkpoint build translated four of them.
- **14.7.3p5's other readers were right.**  The vtable's binding, the ABI's own
  `_ZTV`/`_ZTS` names, the two constructor and destructor entry points, a local
  static's owner name and the startup body a dynamic initializer owes were each
  probed over a `template<>` class beside an instantiated one, and all five are
  the reference's.
- **Nothing is gated and no phase is skipped.**  The checkpoint's changed source
  and this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read and no dialect switch keyed on anything but a dialect.
- **Every `.ref` is the reference binary's.**  All 399 handout `.ref` files and
  384 `.ref.stdout` files, and all 40 course fixtures, were regenerated through
  the harness from `cppgm++-ref` and not one changed; the two new course
  fixtures are this audit's.

### Recorded rather than fixed

- **14.7.3p11 where the type fits two templates 14.5.6.2 leaves unordered.**
  `f(T, int)` beside `f(int, T)` with `template<> int f<int>(int, int)` is a
  program `g++` refuses for an ambiguous specialization and the reference
  translates, with or without the written list and with or without a call of it.
  The refusal for the count being zero and the one for the type fitting none are
  both the reference's own answer; this third half of the sentence is not, so
  refusing it would fail a fixture rather than pass one.
- **`template<> int S::h()` over an ordinary class's member** is refused by
  `g++` and translated by the reference and by this build, which is the same
  divergence one tier down.
- **The reference defers an unused *free* explicit function specialization.**
  `template<> int f<int>(int a) { return a; }` that nothing calls is a definition
  `g++` writes and an ordinary function's unused definition is one the reference
  writes, so the reference disagrees with itself across the pair.  It is older
  than the checkpoint and the pre-checkpoint build answers identically.
- **A static data member of a `template<>` class is `binding=strong` here and in
  `g++` and `binding=weak` in the reference.**  `g++` writes `D` for it and `u`
  for an instantiated one, which is this build's answer; `binding=` is stripped
  before every comparison, so no fixture can pin either.
- **`template<> template<> int S<int>::g(char)` written with no argument list on
  either head** runs to the specialization here and in `g++` and to the
  *pattern* in the reference, which the LowIR shows as the body's own return
  value.  The checkpoint is what made the shape reachable at all.
- **`decltype(sizeof...(N))` over a value pack is `int` in the reference** and
  `std::size_t` here and in `g++`, which 5.3.3p6 is what says; the same operand
  over a *function parameter* pack agrees three ways.  It is older than the
  checkpoint.
- **`decltype((h.*f)(a))` written as a trailing return type is refused here**
  and translated by both oracles, with no pack in the program at all - so it is
  the member-pointer call reader and no part of 14.5.3p4's run.

### Changes

| Where | What |
|-------|------|
| `lowir_lower.cpp`, `lowir_abi.{h,cpp}` | the two questions `abi_instantiated` was answering as one: whose definition this is, which 14.7.3p5 widened, and whether this unit writes an `inline` one nobody used, which is 3.2p4's about the source and reads `abi_named_through_specialization` - with 12.1 and 12.4's constructor and destructor the exception the reference draws in the same place. |
| `sema_declarator.cpp`, `sema_pack.cpp`, `sema_analyzer.h` | 14.5.3p4's run carried onto the declaration `bind_place` makes, and each place after the first linked back to it, so both loops that declare a settled run leave the shape a trailing-return-type reads. |
| `sema_explicit.cpp`, `sema_analyzer.h` | 14.7.3p11 where the count is zero: `require_declared_template` refuses a head over a name no function or object template holds, asked before the match because the match is what puts deduced specializations on the name. |
| `sema_specialize.h` | the comment on `explicit_target`, which said an unordered pair was p11 refused where it is handed to the ordinary walk. |
| `course/pa23/100-a-settled-run-is-what-a-trailing-return-type-expands.t` | a run of none, of one and of two through one trailing return type, and `sizeof...` over the run. |
| `course/pa23/100-an-inline-member-of-a-written-out-class-waits-for-a-use.t` | the plain member, the `inline` member and the constructor of one `template<>` class, and which of the three the object file holds. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `6aeb1035` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| trailing-return pack multiplicity | n calls of one variadic template whose trailing return expands a settled run of two | 0.00 s @32, 0.01 @128, 0.03 @512, 0.06 @1024 - linear; the pre-audit binary refuses the program at the first call |
| trailing-return run width | one call whose settled run holds n elements, so `read_places` reads the parameter-declaration n times and `element_region` opens n regions | 0.00 @128, 0.01 @256, 0.02 @512, 0.03 @1024, 0.07 @2048 - linear, which is what says the per-element lookup of `a__pack…` does not make the clause quadratic |
| trailing-return alias nesting | d alias layers between the pack's place and the type its run holds | 0.00 s flat from d = 4 to d = 32 |
| out-of-class definition multiplicity | n `inline` members of one `template<>` class defined out of class - the walk `abi_named_through_specialization` was added to | 0.00 @32, 0.01 @128, 0.02 @512, 0.05 @1024 - and 0.00 / 0.01 / 0.02 / 0.05 on the pre-audit binary |
| explicit-specialization multiplicity | n `template<>` definitions of one function template, each with its own written list - the walk `require_declared_template` was added to | 0.00 @32, 0.01 @128, 0.03 @512, 0.06 @1024 - and the same |
| deduced-list multiplicity | n `template<>` definitions writing no argument list | 0.00 @32, 0.00 @128, 0.03 @512, 0.06 @1024, 0.22 @3200 - against the pre-*checkpoint* binary's 0.00 / 0.01 / 0.03 / 0.07 / 0.49, which is superlinear; checkpoint 21 recorded 1.03 s at n = 1024 for it and that does not reproduce at this shape, so the gain is real and smaller than the row claimed |
| whole PA23 corpus | 400 handout and 42 course files, one process each | **2.23-2.25 s against the pre-audit binary's 2.24-2.28 s**, over three alternating passes after a warming pass of each |

`abi_named_through_specialization` is the owner walk `abi_instantiated` already
makes, asked only where a definition is out of class, is this unit's own source
and is no instantiation - so a program writing none of those never asks it.
`bind_place` carries one `unsigned` and one pointer onto a declaration it
already built, and because the run is read off that declaration rather than off
the type, `sizeof...` and a nested expansion are one field read each.
`require_declared_template` is one lookup per `template<>` head, on a path about
to read the whole declarator anyway.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **404 / 442** (handout
  362 / 400, course 42 / 42), against 401 / 440 at the turn's start: the failing
  38 are a strict subset of the failing 39, with
  `spec/300-dependent-decltype-pack-overload-replay.t` the one that turned and
  the two the total moved by this audit's own fixtures.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.  `sema_analyzer.h` stands
  at 2396 of its 2400 lines.
- 113 probe programs through `g++ -std=c++11 -pedantic-errors -x c++`, the
  reference binary, the checkpoint binary and the pre-checkpoint binary, with
  every mutually accepted one judged through the real `compare_results.pl` from a
  scratch directory under `tests/` - which is what says a `binding=` is stripped
  and a missing function is not - and every accepted one run through
  `lowir2cy86` + `cy86` to `g++`'s value where the scaffold's four-argument
  ceiling allows.
- All 399 handout `.ref` files, 384 handout `.ref.stdout` files and 40 course
  fixtures regenerated through the harness from `cppgm++-ref`: **not one
  changed**.
- All 4050 files of pa10 through pa39 compiled one at a time under their own
  dialects: **0 exits above 1** over the 3168 that a single source file
  compiles, the other 882 being pa30 and above, which answer `not yet
  implemented` to every input.
- `valgrind -q --error-exitcode=9`: **0 errors** over 157 inputs on the final
  binary - the 42 course fixtures and every probe this audit wrote - and 0 over
  323 inputs mid-audit, which added 160 of the handout corpus.
