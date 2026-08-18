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

## Current Checkpoint Review

Checkpoint 15 - the types a deduction between two declarations is asked over:
`limit` as 14.8.2.4p3's own three answers, `ordering_parameters` and
`ordering_places` as where it is read, 14.8.2.4p12's trailing run a place of its
own past it, 14.8.2.3p2 and p4 taking the reference and the top-level
qualification off P and A wherever the *other* one came from, and 14.6.4.2p1's
number taken from the binding a second declaration stood over - was
reconstructed from its commit, from `dev/src` and from the README: which
contexts ask for an ordering at all and what each of them has to name, which
places the two lists hold and where the count the call hands them starts, and
which second bindings of a spelling a region may number as one name. Two defects
were found and fixed across the reader path each reaches, three gaps were probed
as programs and recorded, and the rest is what the review confirmed.

### Findings

**1. 3.3p4: a second binding stands where the first did only where the two
declare the same kind of name.** The checkpoint's own case is right - `extern int
anchor;` above a pattern and `int anchor = 4;` below it are one object, the
second binding is what the lookup then reads, and numbering it where it was
written puts a name the pattern could see out of its reach. What the rule was
written over is every second binding of a spelling, and a declaration that binds
one spelling to another *kind* of entity is a declaration of its own however the
region already spelled the name:

```cpp
struct probe;
template<class T> int run(T value) { return probe(value); }
int probe(int value) { return value + 7; }
int main() { return run(1) == 8 ? 0 : 1; }
```

What the pattern's reading finds is the class - `probe(value)` is 5.2.3p1's
functional cast of an incomplete type and no call - so both oracles refuse the
program and the pre-checkpoint build refused it. This one gave `int probe(int)`
the number the class stood at, put it in the pattern's reach and translated the
program to 8.

The number is taken from the slot this declaration overwrites now - a tag's from
`binding.tag` and everything else's from `binding.ordinary` - and only where that
slot holds a declaration of the same kind, so `find` goes on filtering the
function out and answering with the tag that survives it. `extern`-then-definition,
a typedef 7.1.3p3 lets a region write twice, two `extern`s before one definition,
a class declared above and defined below, a namespace reopened below, an overload
chain and a hidden friend all keep the answer the checkpoint gave them.

**2. 14.8.2.4p3's limit is 13.3.1p3's count, and the two ordering lists do not
always hold a place for its implicit object argument.** `better_candidate` was
handed the number of ranked conversions, which 13.3.1p3 gives a place to the
object argument in whether or not a candidate wrote a parameter for it -
and `ordering_parameters` keeps a place for that argument only where one of the
two declarations wrote one. So the same pair over the same argument was ordered
two ways:

```cpp
struct S {
  template<class T> static int f(T, int = 0) { return 1; }
  template<class T> static int f(T *, long = 0) { return 2; }
};
int main() { S s; int v = 0; return s.f(&v) == 2 ? 0 : 1; }   // ambiguous here
// int main() { int v = 0; return S::f(&v) == 2 ? 0 : 1; }    // and right here
```

Written through the object, the limit was 2 where the call wrote one argument, so
`int` against `long` - a default argument, which p3's footnote says is no
argument here - was asked and answered neither way, and the call had no best
declaration. `g++` accepts both spellings; the pre-checkpoint build refuses both.
9.4p1's static member ordered against a non-static one of its own class is the
same program one step further on, because that is the pair whose object place
`ordering_parameters` drops from *both* lists.

`ordering_limit` is the count restated in the places the lists hold: the object
place is kept where either declaration wrote one and dropped where it was dropped
from both, and 13.3.1.2p4's first operand is no such argument at all - a
non-member operator wrote it as its own first parameter and the member candidate
beside it has its object parameter standing in that same place, so the caller
counts it among the arguments and the ordering asks both lists over it.

### What the review confirmed rather than found

- **The ordering answers what `g++` answers across the candidate kinds.** 135
  generated shapes - a free pair, two non-static members, two static members, a
  static against a non-static one and two non-member operators, crossed with
  three first-parameter shapes (`T`/`T *`, `T &`/`T *&`, `T *`/`T **`), five
  trailing-default shapes and one or two written arguments - agree with `g++` on
  all 135, and the 124 it accepts run through `lowir2cy86` + `cy86` to the value
  `g++` runs them to. 111 of them are programs the pre-checkpoint binary refuses.
- **14.8.2.3's stripping is right from either side.** Ten conversion shapes - `A`
  a plain type, a `const` one, an lvalue reference, a `const` reference, a
  reference to a class, a `volatile` destination, a by-value argument, a
  qualification-converted pointer, `operator T *` against `operator T **`, and a
  conversion template beside a declared conversion function - agree with `g++` on
  all ten, with the one the reference alone refuses already recorded.
- **The memo is keyed by the pair and the limit and stays a short run.** One pair
  asked under five call arities answers each of them, and 64 x 5 such calls cost
  0.01 s; the answer a limit was given is never read for another.
- **The bind rule is the same answer for every declaration kind.** A tag then a
  variable, a variable then a tag, an enumeration named before its constants, a
  later-only declaration, a using-declaration, a qualified name into a namespace
  reopened below the pattern and 13.1's overload chain all answer as they did,
  and `pick(int)` written below a pattern still joins no set the pattern gathers.
- **Every course `.ref` is the reference binary's.** All 29 were regenerated from
  `cppgm++-ref` through the harness and not one changed; the thirtieth is this
  audit's, and the reference refuses it as this build does.
- **Nothing is gated and no phase is skipped.** The checkpoint's changed source
  and this audit's hold no `getenv`, no fixture name, no timeout, no environment
  read, no dialect switch keyed on anything but a dialect, and no caught
  exception standing for a success.
- **`valgrind -q --error-exitcode=9` is clean over 298 inputs** - the 30 course
  fixtures, 60 of the corpus, 15 `pa12` inputs and 193 probes of the paths this
  audit touched - and no input exits above 1: not one of pa23's 430, and not one
  of the 2284 files pa10 through pa22 run through their own dialects.

### Recorded rather than fixed

- **13.5.2p1's arity of an operator function has no reader at any tier**, and the
  checkpoint's ordering is what un-hid it. `template<class T> int operator-(tag,
  T, int = 0)` is a declaration `g++` refuses outright - a non-member `operator-`
  shall have one or two parameters - and the reference and this build accept.
  Before the checkpoint the *call* over such a pair was refused for having no
  best declaration, so a broad refusal stood where the precise one is missing;
  eleven of the sweep's operator shapes are that program. The reference
  implements the clause at no arity, so no fixture can pin it, and 13.5's own
  answer differs for every operator it names.
- **The three static-member orderings this audit turns are `g++`'s answer against
  the reference's.** `s.f(x)` over two static member templates, over a static and
  a non-static one, and a namespace-scope pair reached from a member body are
  programs `g++` accepts and the reference refuses - it implements 14.8.2.4p3's
  first bullet at no shape at all, which is what the checkpoint's own two
  recorded divergences already say - so no course fixture can hold them.
- **`B<int>::held` named in a pattern above the definition of `B`.** `g++`
  refuses it and the reference, this build and the pre-checkpoint build all
  translate it, so it is 14.6.4.1's point of instantiation rather than
  14.6.4.2p1's number and not this checkpoint's.

### Changes

| Where | What |
|-------|------|
| `sema_scope.cpp` | `bind` takes 14.6.4.2p1's number from the slot the declaration overwrites and only where that slot holds a declaration of the same kind, so 3.3p4's second binding of one spelling to another kind of entity stands where it is written. |
| `sema_template_signature.cpp` | `ordering_limit` - 14.8.2.4p3's first bullet counted in the places the two lists hold rather than in the conversions 13.3.1p3 ranked, and `more_specialized` reads it wherever a call is what asks. |
| `sema_overload.cpp` | `select_overload` says how many of the ranked places are the implicit object argument a candidate need have written no parameter for, and `better_candidate` hands it on. 13.3.1.2p4's operand is not one. |
| `sema_analyzer.h` | the two signatures, and `reference_order`'s dropped third answer put back in its comment. |
| `course/pa23/100-bad-a-later-declaration-of-another-kind-stands-where-it-is-written.t` | the half of 3.3.1p1 the checkpoint's own fixture does not write: a class above the pattern and a function of that name below it are two declarations, and the reference refuses the program as this build does. |

### Performance Evidence

Measured on the audited binary against a `/tmp` worktree of `adf23e13` built the
same way, warm cache, `/usr/bin/time` on the binary itself, best of three.

| sweep | shape | result |
| --- | --- | --- |
| ordering multiplicity | n calls of one pair the limit is what orders | 0.00 s @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same on the pre-audit binary |
| ordering distinctness | n distinct pairs, one call each, so no two share a memo entry | 0.00 @32, 0.02 @128, 0.08 @512, 0.17 @1024 - against 0.00 / 0.02 / 0.08 / 0.18 |
| member-call ordering multiplicity | n calls through an object over two non-static member templates - the path `objects` was added to | 0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same |
| static-member ordering multiplicity | n calls through an object over two static member templates - the shape whose limit this audit corrected | 0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - linear; the pre-audit binary refuses the program at the first call |
| overload-set width | one call over n candidate templates that all tie on conversions | 0.01 @128, 0.02 @256, 0.04 @512 - and the same |
| ordering-limit distinctness | one pair asked under five call arities, n times over - the run the memo keeps per pair | 0.00 @8, 0.00 @16, 0.01 @32, 0.01 @64 - and the same |
| conversion-ordering multiplicity | n classes x two conversion templates, 14.8.2.4p3's second bullet run once per initialization | 0.01 @32, 0.03 @128, 0.13 @512, 0.27 @1024 - and the same |
| binding multiplicity | n namespace-scope declarations of distinct names | 0.00 @32, 0.00 @128, 0.01 @512, 0.01 @1024 - and the same |
| rebinding multiplicity | n names each declared twice, which is the slot the kind test reads | 0.00 @32, 0.00 @128, 0.01 @512, 0.02 @1024 - and the same |
| tag-then-function multiplicity | n spellings bound as a tag and then as a function - the pair the kind test tells apart | 0.00 @32, 0.00 @128, 0.02 @512, 0.04 @1024 - and the same |
| ordering nesting | d nested `W<...>` under the one place two templates are ordered at | 0.00 s flat from d = 4 to d = 32, on both binaries |
| conversion-ordering nesting | d pointer levels in the two conversion-type-ids being ordered | 0.00 s flat from d = 4 to d = 32, on both binaries |
| whole PA23 corpus | 400 handout and 30 course files, one process each | **2.08 s against the pre-audit binary's 2.12 s**, over three alternating passes after a warming pass of each; no `rc > 1`; valgrind clean |

`ordering_limit` is two pointer comparisons and three field reads per ordering,
on a path that already walks both parameter lists and memoises its answer, and it
carries no new question to the call. The bind test is one `kind` comparison per
namespace binding and reads a slot the function already holds, which is why n
declarations of distinct names and n declared twice cost what they did. The
corpus is 0.04 s under the pre-audit binary, which is the ordering that no longer
runs over a place the call did not write.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **377 / 430** (handout
  347 / 400, course 30 / 30), against 376 / 429 at the turn's start: the failing
  set is the same 53 files name for name, and the thirtieth course fixture is
  this audit's.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- The failing set of the checkpoint binary was taken as a whole and compared:
  this audit turns one test - its own fixture - and regresses none.
- 135 generated cross-product shapes plus 40 hand-written probes through `g++
  -std=c++11 -pedantic-errors`, the reference binary, the checkpoint binary and
  the pre-checkpoint binary. Every one this build accepts that is not one of the
  divergences recorded above runs through `lowir2cy86` + `cy86` to the value
  `g++` runs it to.
- All 430 pa23 corpus files and all 2284 files pa10 through pa22 compiled one at
  a time under their own dialects: **0 exits above 1**; 298 inputs under
  `valgrind -q --error-exitcode=9`: 0 errors.
