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
| 11 | `c6e77dd5` | 1 / 1 + 4 recorded | **the ending delimiter is a fact PA10's flattening holds, and the checkpoint taught it to the five scans that read a spelling forwards and to none of the one that reads one backwards.**  Checkpoint 23 made `closes_template_arguments` the question every `>` of a flattened name is asked, with `opens_template_arguments`'s mirror for `<=`, `<<` and `<<=`; 7.1.6.3p1's keyword joined to the qualified name after it in a value argument; 14.6.2p2 asked before 5.3.1p3's `&`; 8.3.6p1's `= initializer-clause` as the whole of what a parameter-declaration writes; and 14.7.1p1's demand made at every *naming* of an alias template rather than at the one reading that built the type.  Those rules are right and swept clean, and they are right for a reason worth keeping: `needs_separator` is what wrote the spelling the predicates read - `munches_together` separates every pair phase 3 would munch into a third spelling, `> =`, `< =`, `< <` and `- >` among them, and closes `>>` up on purpose because 14.2p3 asks it to - so each of the four spellings they key on can only be the token they say it is, `>>=` reads right from both ends, and the pairs left closed up that they do not name (`-->`, `-=>`, `<::`) are ones no template-argument ends or begins with.  What none of it carried is `sema_pack.cpp`'s `operand_start`, which walks a pattern spelling *backwards* from an inner `...` to the `<` the template-id before it opened, counting every `>` and `<` character and stepping over no group: a `>` inside 5.1.1p6's parentheses, a `>=` and a `->` each made the count overshoot the operand's own `<` and take the names written before it along, so `list<list<A, bc<(sizeof(B) > 1)>...>...>` left the *enclosing* expansion `expanded and names no parameter pack` - three programs both oracles translate, one per spelling, and the symptom names the outer list rather than the inner pattern that caused it.  The walk asks the same two predicates now and skips groups as the forward scans do, and the four shapes it is asked over - a plain template-id, a nested one, a `>>` closing two lists, and a qualified name - are byte-identical to the reference.  167 probe programs across the four rules and their cross product agree with `g++` and the reference on acceptance at every one outside the divergences below; 123 were judged through the real `compare_results.pl` from a scratch directory under `tests/` with 119 byte-identical, and 136 run to `g++`'s value through `lowir2cy86` + `cy86`.  Recorded rather than fixed: the reference writes *two* definitions of one specialization named both through a pattern and directly, one `object=` under two LowIR names, which no link can hold and which the checkpoint's `&T::m` made reachable; the reference writes `arity=variadic` and mangles a trailing `z` for a function parameter pack whose pattern is a class template-id, where `g++` writes what this build writes; the reference refuses an alias template whose type-id is a pointer to or an array of a class template specialization, which `g++` and this build translate; and `sizeof` over a function name is accepted here and refused by both oracles, which is 5.3.3p1's own gap and older than the checkpoint |


## Current Checkpoint Review

Checkpoint 23 - 14.2p3's ending delimiter over a flattened spelling, and the
three siblings of what one written argument comes to:
`closes_template_arguments` beside the `opens_template_arguments` already in
`sema_name.{h,cpp}`, 7.1.6.3p1's keyword joined to the name after it and
14.6.2p2 asked before 5.3.1p3's `&` in `sema_value_expression.cpp`, 8.3.6p1's
`= initializer-clause` as the whole of what a parameter-declaration writes in
`ast_parser_declarator.cpp`, and 14.7.1p1's demand made at every naming of an
alias template in `sema_specialize.{h,cpp}` - was reconstructed from its one
commit, from `dev/src` and from the README: which `>` of a spelling ends a list,
what an argument opening with a keyword or an `&` comes to, which run 8.2p1
reads as an object, and what a naming that reads nothing again still owes.

One defect was found and fixed - the one scan of a spelling that reads it
backwards - four divergences were probed as programs and recorded, and the rest
is what the review confirmed.  167 probe programs were written against `g++`,
the reference binary and the pre-checkpoint binary; 123 of them were judged
through the real `compare_results.pl` from a scratch directory under `tests/`
and 119 are byte-identical to the reference; 136 run to `g++`'s value through
`lowir2cy86` + `cy86`.  The four LowIR differences and the six run differences
are the reference's own answers, the scaffold's ceiling, or older than the
checkpoint, and each is named below with the program that found it.

### Findings

**1. The checkpoint taught the ending delimiter to the five scans that read a
spelling forwards, and to none of the one that reads one backwards.**
`sema_pack.cpp`'s `operand_start` is asked at an inner `...` for the first
character of the pattern that expansion is over, and it walks back from the
`>` before the `...` to the `<` its template-id opened by counting every `>` and
`<` character in between - stepping over no group and asking 14.2p3 nothing:

```cpp
template<class... A> struct outer {
  template<class... B> struct inner {
    typedef list<list<A, bc<(sizeof(B) > 1)>...>...> type;   // and `>=`, and `->`
  };
};
```

The `>` inside the parentheses is 5.9's operator, so the count reached zero one
`<` too early, `operand_start` returned the position of the *outer* `list<`, and
every name at or after it was popped as belonging to the inner expansion - `A`
among them.  The enclosing `...` was then over a pattern that names no pack, and
the refusal says so about the outer list rather than about the argument that
caused it: `list<A,bc<(sizeof(B)>1)>...> is expanded and names no parameter
pack`.  Three spellings reach it - a `>` inside 5.1.1p6's parentheses, a `>=`,
and a `->` written in a decltype-specifier - and all three are programs both
oracles translate.

The walk asks `closes_template_arguments` and `opens_template_arguments` now,
and steps over 5.1.1p6's parentheses, 5.2.1p1's subscript and 5.2.3p3's braces
as the five forward scans do, because a `<` or `>` written inside one of those
is an operator however it is spelled.  The `)` and `]` arms keep their own plain
count, which is the mirror of a balanced run of one character and reads no angle
at all.  Seven shapes were swept - a plain template-id, a nested one, a `>>`
closing two lists, a namespace-qualified name, a pattern under a second inner
pack, `sizeof...` beside the expansion, and the three spellings above - and
every one is byte-identical to the reference and runs to `g++`'s value.
`course/pa23/100-a-relational-in-a-pattern-leaves-the-outer-expansion-its-pack.t`
pins all three spellings; the pre-audit binary refuses it.

### Why the two predicates are sound

`closes_template_arguments` answers from two characters - a `>` written against
an `=` after it or a `-` before it belongs to another token - and
`opens_template_arguments` from one, a `<` written against an `=` or a `<`.
That is a reading of the *spelling*, and it is exact because `needs_separator`
in `ast_tokens.cpp` is what wrote the spelling: `munches_together` separates
every pair phase 3 would munch into a third spelling, `> =`, `< =`, `< <` and
`- >` among them, and the one pair the flattening closes up on purpose is `>>`,
because 14.2p3 is what asks it to.  So each of the four spellings the predicates
key on can only be the token the predicate says it is, and `>>=` reads right
from both ends: its first `>` closes a list, because the character after it is a
`>`, and its second does not, because the character after it is an `=`.  The
pairs the flattening also leaves closed up and the predicates do not name -
`-->`, `-=>` and 2.5.3's `<::` - are ones no template-argument can end or begin
with.  The fact is therefore the flattening's rather than a second parse of it,
which is what lets one predicate serve the five scans a spelling gets and every
run they hand on.

### What the review confirmed rather than found

- **Every reader of the widened fact asks it.**  The five scans in
  `sema_name.cpp` - the constructor's balance count, `resolve`'s backward and
  forward passes, `outside_brackets` and `balanced_end` - each take the
  predicate, and every other reader in that file reads the `opens_` the
  resolution wrote or the `spelled` respelling that carries the separator into
  an extracted run; the second parser (`SpelledTypeId`) and the value split both
  reach it through `balanced_end`.  The three naive `<`/`>` counts *outside*
  that file were probed at the sites that reach them: `operand_start` is the
  finding above, and the other two are not reached with a spelling they answer
  wrongly - `struct e : base<1 >= 1> { kind m; };` through
  `DeclaredNames::canonical` and an instantiated `helper<&T::m>` name through
  `name_components` are byte-identical to the reference and run to `g++`'s
  value.  31 further shapes across
  six operators, three nesting depths, a base clause, a partial specialization's
  own argument list, a `template<>` head, an explicit call list, a default
  template argument and a member template-id agree three ways.
- **The reading is no slower and no wider.**  A `<=` used to open a candidate
  run that never closed, so `A<x<=1>` made `AngleReading::resolve` run; it no
  longer does, and a `>=` never made it run either way.  The one shape that
  newly reaches `resolve` is a `<` operator followed by a `->`, which no name a
  program writes holds.
- **The value reading's two new arms answer where the old ones did.**  The
  `typename` join is one string concatenation before every arm, and the arm it
  feeds is `probe_type_id`, which `SpelledTypeId` drops the keyword in - through
  a nested class, a member of a class template, a member template-id and
  5.2.3p1's parenthesised form as well as 5.2.3p3's braced one.  The 14.6.2p2
  arm hoisted above the designating one fires only where the old dependent arm
  below would have: its condition is that same one, so a name that is neither
  dependent nor a place reaches 5.3.1p3 exactly as it did - `&s::m` at a pointer
  place, `s::r` at a reference place, `&s::f` at a function place and `&e` over
  an `extern` object are all unchanged, and two units naming `helper<&T::m>`
  write one weak definition under `_ZN6helperIXadL_ZN1s1mEEEE4readEv`, which is
  `g++`'s own name for it.
- **The alias naming's demand reaches the class at every door a naming has.**
  `named_specialization` is the walk `require_complete_type` makes, run at the
  memo hit as well as at the first reading, so a class an alias names is marked
  wherever `instantiate_class` would have marked it and nowhere else: a pointer
  or a reference alias marks nothing, which is what keeps `box<T> *` over an
  ill-formed `box<int>` a program this build and `g++` both translate.  A
  dependent naming, a naming under substitution, an alias of an alias, a pack
  alias, a discarding alias, an array alias, an alias naming a member class and
  the ADL that a naming through one reaches were each probed and each is the
  reference's.
- **The parameter clause.**  `void f(int{3});` and `void f(s{3});` are refused
  now as both oracles refuse them; `= 3`, `= {4}`, `= seed{1}`, `= T()`, a
  defaulted function pointer and a defaulted member-function parameter all still
  parse; and 8.2p1's other readings are unmoved - `holder made(seed());`,
  `holder made();` and `holder made(int(n));` are function declarations here as
  in both oracles.
- **Nothing is gated and no phase is skipped.**  The checkpoint's changed source
  holds no `getenv`, no fixture name, no timeout, no environment read and no
  dialect switch keyed on anything but a dialect.  `perl
  scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` passes with the five
  `bad-division` warnings it already had.
- **Every `.ref` is the reference binary's.**  All 400 handout tests and all 46
  course fixtures were regenerated through the harness from `cppgm++-ref` and
  not one tracked file changed; the checkpoint's own four course fixtures each
  fail on the pre-checkpoint binary and pass here, so each pins the rule it was
  written for.

### Recorded rather than fixed

- **The reference writes two definitions of one specialization named two ways.**
  `helper<&T::m>` reached through a pattern and `helper<&s::m>` written out
  longhand are one specialization, and the reference emits both
  `@helper__T__m___read` and `@helper__s__m___read` with one
  `object=_ZN6helperIXadL_ZN1s1mEEEE4readEv` between them - an object file no
  link can hold.  `g++` and this build write one.  The same holds of a reference
  place (`h<T::r>` beside `h<s::r>`), where the comparator fails on the extra
  function.  The checkpoint is what made the shape reachable, so no course
  fixture can hold it.
- **The reference calls a parameter pack of class type variadic.**
  `template<class... Ds> int take(box<Ds>... vs)` is
  `_Z4takeIJclEEiDp3boxIT_Ez` with `arity=variadic` there and
  `_Z4takeIJclEEiDp3boxIT_E` in `g++` and here.  It fires only where the
  expanded pattern is a class template-id: `T...` and `T *...` agree three ways.
  `arity=` is compared, so no course fixture can hold a pack of class type.
- **The reference refuses an alias template whose type-id is a pointer to or an
  array of a specialization.**  `template<class T> using p = box<T> *;` and
  `using arr = box<T>[2];` are programs `g++` and this build translate and the
  reference refuses, with and without a use.  It is not the checkpoint's: the
  pre-checkpoint binary refuses the array shape for its own reason and accepts
  the pointer one.
- **`sizeof` over a function name is accepted here.**  `int f(); sizeof(f)` is
  5.3.3p1 ill-formed, refused by both oracles and translated by this build with
  no template in the program at all.  It is older than the checkpoint and is the
  parse behind `holder made(seed());` being observable at all.

### Changes

| Where | What |
|-------|------|
| `sema_pack.cpp` | 14.2p3 read backwards: `operand_start`'s walk from an inner `...` to the `<` its pattern opened asks the two predicates and steps over 5.1.1p6's parentheses, 5.2.1p1's subscript and 5.2.3p3's braces, so a `>` that closes nothing no longer takes the enclosing expansion's pack with it.  The `)` and `]` arms keep their own plain count. |
| `course/pa23/100-a-relational-in-a-pattern-leaves-the-outer-expansion-its-pack.t` | the three spellings that reach it - a `>` inside parentheses, a `>=`, and a `->` in a decltype-specifier - each under one nested expansion, checked by the outer expansion's own element count. |

### Performance Evidence

Measured on the audited binary against `/tmp` worktrees of `ea32fedb` (the
pre-checkpoint binary) and `c6e77dd5` (the pre-audit binary) built the same way,
warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| spelled-argument multiplicity | n typedefs, each a `>=` inside a nested argument list | 0.00 s @32, 0.00 @128, 0.01 @512, 0.02 @1024 - linear; the pre-checkpoint binary refuses the program |
| spelled-argument nesting | d `typename w<...>::type` wrappers around one `>=` argument, which is where `resolve` reads the spelling twice | 0.00 s flat from d = 4 to d = 32; refused by the pre-checkpoint binary |
| alias-naming multiplicity | n typedefs naming one alias over one list - the memo hit `named_specialization` was added to | 0.00 @32, 0.00 @128, 0.00 @512, 0.01 @1024 - and the same on the pre-checkpoint binary |
| alias-naming distinctness | n distinct lists, so no two namings share the memo | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| alias-naming nesting | d alias layers between the naming and the class its type-id names | 0.00 s flat from d = 4 to d = 32 |
| dependent-address multiplicity | n classes, one `helper<&T::m>` typedef apiece | 0.01 @32, 0.03 @128, 0.13 @512, 0.30 @1024 - linear; the pre-checkpoint binary refuses the program |
| braced-declaration multiplicity | n `holder made(seed{n});` declarations - the parse 8.2p1 settles | 0.00 @32, 0.01 @128, 0.04 @512, 0.08 @1024 - linear; refused by the pre-checkpoint binary |
| nested-expansion multiplicity | n nested expansions whose inner pattern holds a `>` that closes nothing - the walk this audit corrected | 0.00 @32, 0.02 @128, 0.08 @512, 0.18 @1024 - linear; the pre-audit binary refuses the program |
| nested-expansion multiplicity, neutral | the same n written `box<B>...`, the pattern both binaries read | 0.00 @32, 0.01 @128, 0.04 @512, 0.09 @1024 - against 0.00 / 0.01 / 0.04 / 0.08 on the pre-audit binary |
| pattern depth | d template-id levels between the pattern's `<` and the `...`, which is the length the walk runs back over | 0.00 s flat from d = 4 to d = 32; refused by the pre-audit binary |
| whole PA23 corpus | 400 handout and 47 course files, one process each | 2.24-2.28 s against the pre-audit binary's 2.18-2.26 s over seven alternating passes in both orders - which resolves nothing: 100 runs over an empty translation unit take 0.372-0.383 s on this build and 0.369-0.385 s on the pre-audit one, so ~1.7 s of the 2.25 s is the process floor.  The largest fixture run 20 times is 0.164 s against 0.162 s, and the sweeps above are where the measurement is |

`closes_template_arguments` is one or two character comparisons per `>` on a
scan already being made, `named_specialization` is one `strip_cv`, one `kind`
test and one `type_owner` probe per alias naming, the `typename` join is one
concatenation per keyword written in a value argument, the parameter parse does
strictly less than it did, and the corrected backward walk is the same one pass
over the same characters with two predicate calls and a group counter added -
which is why the neutral nested-expansion sweep is the pre-audit binary's.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **416 / 447** (handout
  369 / 400, course 47 / 47), against 415 / 446 at the turn's start: the failing
  31 are the same tests file for file, and the total moved by this audit's own
  fixture.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.  `sema_analyzer.h`
  stands at 2396 of its 2400 lines.
- 167 probe programs through `g++ -std=c++11 -pedantic-errors -x c++`, the
  reference binary, the checkpoint binary and the pre-checkpoint binary, with
  123 mutually accepted ones judged through the real `compare_results.pl` from a
  scratch directory under `tests/` (119 byte-identical) and 136 run through
  `lowir2cy86` + `cy86` to `g++`'s value.  The six that do not run to it pass a
  class **by value**, which comes out of the *reference's* LowIR with the same
  wrong value and the same segfault - the scaffold's ceiling, now recorded in
  the plan beside its arity half.
- All 400 handout tests and 47 course fixtures regenerated through the harness
  from `cppgm++-ref`: **not one tracked file changed**, and the new fixture's
  own `.ref` is the reference binary's.
- All 3918 single-file inputs of pa10 through pa29 compiled one at a time:
  **0 exits above 1**.
- `valgrind -q --error-exitcode=9`: **0 errors** over 160 inputs mid-audit and
  64 on the final binary - the 47 course fixtures and every nested-expansion
  probe.
