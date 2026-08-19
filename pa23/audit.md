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

## Current Checkpoint Review

Checkpoint 17 - 12.9p1's inherited constructor is a template too: the base's own
head on the declaration a using-declaration makes, `TemplateSignature::indexed`
as 13.1's key for it, `specialize` handing the specialization 12.9's `inherited`,
`defaulted` and `inline_function` and resolving the first to the base's
specialization over the same argument list, `demand_constructor_definition`
taking its places from the template's record with a pack entry expanded to the
run this list bound, 12.9p8's `static_cast<P&&>(p)`, 8.3.5p4's ellipsis at a
deduction and the entry points an instantiated `constexpr` constructor owes - was
reconstructed from its commit, from `dev/src` and from the README: what 12.9
declares into the derived class and with which characteristics, which
declarations one probe of 13.1's index stands for, what one argument list makes
of such a declaration, and what its definition is at a fold as against at a call.
Two defects were found and fixed across the reader path each reaches, three gaps
were probed as programs and recorded, and the rest is what the review confirmed.

### Findings

**1. 12.9p2 says a constructor has four characteristics and the checkpoint
landed the first.** The paragraph is a list - the template-parameter-list, the
parameter-type-list, absence or presence of `explicit`, absence or presence of
`constexpr` - and 12.9p3 declares the inherited constructor with the same four.
`inherit_constructor` carried the parameter-type-list and `explicit_function`
and the checkpoint added the head; `constexpr` had no reader at any tier, so the
class the using-declaration is written in had no constexpr constructor of its own
and 3.9p10 called it no literal type:

```cpp
struct base { int v; constexpr base(int x) : v(x) {} };
struct derived : base { using base::base; };
constexpr derived d(3);          // the constexpr object d is declared with
static_assert(d.v == 3, "");     // struct derived, which is not a literal type
```

Both oracles build the program; so does `char a[derived(3).v];` with no
`constexpr` object in it at all, and so does the same shape over a constructor
*template*. `r08` is the control: a base constructor written *without*
`constexpr` leaves a refusal all three make.

The characteristic travels with the declaration now, and `specialize` hands it to
the specialization beside 12.9's other three rather than through 14.7.3p1's
chain - which is what `constexpr_declared` answers for a specialization a
*pattern* is behind, and this one is not.

Carrying it is only half the sentence, because 12.9p8 is the definition the fold
then has to read. A defaulted constructor with no written body reaches
`subobject_entries`, which default-initializes every subobject no mem-initializer
names - so an inherited constructor marked `constexpr` and left there would have
built the base subobject out of nothing and folded to a *wrong value* rather than
to a refusal. `constructed_object` writes one entry into the mem-initializer
index for the base the using-declaration named, carrying what this constructor's
own arguments came to rather than a tree to read clauses out of, and
`subobject_initialized` builds that subobject with the same `object_of` a written
mem-initializer reaches. A chain of three, a second base beside the named one, a
member with a brace-or-equal-initializer of its own and a base default argument
the truncated list leaves behind all fold to the value `g++` runs them to.

**2. 12.9p3 excludes a constructor the class declared *itself*, and the probe
that finds one finds an inherited one too.** 13.1's index answers "does a
constructor of this class already take these parameters" in one probe, which is
what the checkpoint rekeyed by `TemplateSignature::indexed` so that two written
under a head pair by 14.5.6.1p5. What the probe cannot tell apart is which kind
of declaration answered, and 12.9p1 declares one constructor here for each
candidate of *each* base:

```cpp
struct left  { int value; left()  : value(0) {} template<class U> left(U u)  : value(int(u)) {} };
struct right { int other; right() : other(0) {} template<class U> right(U u) : other(int(u) + 1) {} };
struct both : left, right { using left::left; using right::right; };
int main() { both one(1); return one.value == 1 ? 0 : 1; }   // ambiguous in both oracles
```

The second using-declaration's whole candidate set was dropped, so the use ran to
`left`'s constructor and the program to 0 where `g++` and the reference each
refuse it for having no best declaration. The exclusion asks `prior->inherited`
now: a constructor the class declared itself keeps it, and an inherited one
leaves both declared, with 13.1's index holding the first because one declaration
per list is what a redeclaration is paired against. A class declaring its own
constructor beside an inherited one of that list, a class declaring its own
*template* beside one, two bases whose lists differ, and the pair uncalled all
answer as they did.

### What the review confirmed rather than found

- **The checkpoint's own rules answer what `g++` answers.** 47 probes - a base
  constructor template reached by two argument types and by two derived classes
  over one base, a member template of a class template, a chain of three, a pack
  at none, one and three arguments, an ellipsis, a two-parameter pair, a partial
  ordering of `U` against `U *`, an rvalue-reference place taking an lvalue, a
  default argument, `explicit` at a copy-initialization, a protected base
  constructor, and 15 shapes of 8.3.5p4's ellipsis at a call - agree with `g++`
  on acceptance and on the value at every one, and every disagreement left is a
  recorded reference divergence or the scaffold's five-parameter ceiling.
- **The head the declaration shares leaves nothing behind.** Two derived classes
  over one base, one derived class under three argument types, and a chain of
  three each deduce and specialize independently: the specializations are held
  per primary, and the object file names them `_ZN4keptC1IiEET_` byte for byte as
  the reference does. Two units naming one such specialization write one weak
  definition under one name.
- **12.9p8's category is written on nothing shared.** `parameter_value` opens a
  dump node of its own per call, so `forward_parameter`'s xvalue is a fact of
  that one argument.
- **Nothing is gated and no phase is skipped.** The checkpoint's changed source
  and this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read, no dialect switch keyed on anything but a dialect, and the two
  `catch (...)` on the path each put a scope back and rethrow.
- **Every course `.ref` is the reference binary's.** All 34 were regenerated
  through the harness from `cppgm++-ref` and not one changed; the two new ones
  are this audit's, and the reference builds one and refuses the other exactly as
  this build does.
- **`valgrind -q --error-exitcode=9` is clean over 143 inputs** - the 36 course
  fixtures, 47 probes and 60 of the corpus - and no input exits above 1: not one
  of pa23's 436, and not one of the 2284 files pa10 through pa22 run through
  their own dialects.

### Recorded rather than fixed

- **7.1.5p4's "every non-static data member shall be initialized" at an inherited
  constructor is `g++`'s later model.** `struct derived : base { int extra; using
  base::base; };` with `constexpr derived d(3);` leaves `extra` holding nothing;
  the reference and this build refuse it and `g++` translates it, which is the
  relaxation C++14 made of the clause. It is the one shape of the fix above where
  the two oracles disagree, and this build is on the standard's side of it.
- **12.9p3 where the two declarations are not templates, and a class's own
  constructor beside an inherited one, are `g++`'s answer against the
  reference's.** Two bases whose *non-template* constructors agree on a list, and
  a base whose own two constructors reach one list through their default
  arguments, are programs `g++` refuses and the reference translates; `using
  base::base;` beside `derived(int)` or beside `template<class U> derived(U)` is
  the mirror, which `g++` and this build translate and the reference refuses. No
  course fixture can hold either, and this build answers `g++` at all four.
- **Run evidence has a ceiling that is `lowir2cy86`'s.** A function of five or
  more parameters, `this` among them, comes out of `lowir2cy86` + `cy86` as a
  program that returns the wrong value or crashes - and it does so from the
  *reference* binary's LowIR identically, with no template and no inheritance in
  the program. A probe that has to be run to a value writes four arguments or
  fewer, which is what the checkpoint's own `bottom three(5, 1, 2)` fixture does.

### Changes

| Where | What |
|-------|------|
| `sema_class.cpp` | 12.9p2's fourth characteristic carried onto the declaration a using-declaration makes, and 12.9p3's exclusion asked only of a constructor the class declared itself - so a second base's candidate set is declared beside the first's rather than standing behind it. |
| `sema_template.cpp` | the same characteristic handed to what one argument list makes of such a declaration, beside 12.9's `inherited`, `defaulted` and `inline_function` and not through 14.7.3p1's chain. |
| `sema_constexpr_object.cpp` | 12.9p8 at a fold: the one mem-initializer of a definition no program wrote, carrying this constructor's own arguments, and the base subobject built by the `object_of` a written one reaches. |
| `sema_declaration.h` | `WrittenMemInitializer::forwarded`, which is what an entry holds where the clauses have already been read. |
| `course/pa23/100-an-inherited-constructor-carries-the-constexpr-it-was-written-with.t` | 12.9p2's fourth characteristic and 12.9p8's fold of it, at a namespace-scope object and at an array bound. |
| `course/pa23/100-bad-two-using-declarations-inherit-one-parameter-type-list.t` | the exclusion 12.9p3 does not make: two bases, one list, and 13.3 with no best declaration - which the reference refuses as this build does. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `26f065b2` built the
same way, warm cache, `/usr/bin/time` on the binary itself.

| sweep | shape | result |
| --- | --- | --- |
| constexpr-inherited multiplicity | n classes, each inheriting one `constexpr` constructor and folding one object of it | 0.00 s @32, 0.02 @128, 0.09 @512, 0.20 @1024 - linear; the pre-audit binary refuses the program at the first object |
| constexpr-inherited chain nesting | d classes, each inheriting the `constexpr` constructor the one below inherited | 0.00 s flat from d = 4 to d = 48, 0.01 at d = 64 - linear; refused by the pre-audit binary |
| inherited-candidate agreement width | n bases whose constructor templates all agree on one parameter-type-list | 0.00 @8, 0.00 @32, 0.02 @128, 0.04 @256 - and the same on the pre-audit binary, which declares one of the n |
| inherited-constructor multiplicity | n classes, each inheriting one constructor template and building one object | 0.00 @32, 0.01 @128, 0.07 @512, 0.15 @1024 - and the same |
| inherited-pack multiplicity | one inherited constructor template over a pack, n calls | 0.00 @32, 0.00 @128, 0.02 @512, 0.03 @1024 - and the same |
| variadic-call multiplicity | n calls of one variadic function template | 0.00 @32, 0.00 @128, 0.01 @512, 0.03 @1024 - and the same |
| whole PA23 corpus | 400 handout and 36 course files, one process each | **2.04-2.07 s against the pre-audit binary's 2.09-2.11 s**, over three alternating passes after a warming pass of each; no `rc > 1`; valgrind clean |

A characteristic is one field read where the declaration is made and one field
written where a specialization of it is. 12.9p3's exclusion is the index probe it
already was, with one pointer test on the answer; what grew is the *chain*, by
one declaration per base whose candidate set agrees with another's - a candidate
13.3 has to rank to answer the use at all, and the width sweep is what says n of
them cost what n bases cost without them. 12.9p8 at a fold is one map insert and
the same `object_of` a written mem-initializer of that base would reach, so an
inheriting chain d deep folds d of them and no level is read twice.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **386 / 436** (handout
  350 / 400, course 36 / 36), against 384 / 434 at the turn's start: the failing
  50 are the same files name for name, and the two the count moved by are this
  audit's own fixtures.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- 47 probe programs through `g++ -std=c++11 -pedantic-errors -x c++`, the
  reference binary, the checkpoint binary and the pre-checkpoint binary. Every
  one this build accepts that is not one of the divergences recorded above runs
  through `lowir2cy86` + `cy86` to the value `g++` runs it to.
- All 436 pa23 corpus files and all 2284 files pa10 through pa22 compiled one at
  a time under their own dialects: **0 exits above 1**; 143 inputs under
  `valgrind -q --error-exitcode=9`: 0 errors.

