# PA23 Audit — deduction, substitution and SFINAE

A review of each landed checkpoint, in the order one use of a function template
travels: what a deduction settles, what building the declaration it settled
means, and what a failure of that building says about the candidate that asked.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| 1 | `188e92bc` | 3 / 3 + 3 recorded | **the scope 14.8.2p8 makes of one attempt, landed with no bound on the attempt asking for itself - and the order the same sentence says that attempt is made in.**  Checkpoint 1 made a substitution failure candidate state: `Substitution` wraps the three deduction entry points and the written-argument-list `specialize`, `Instantiated` is the class body 14.7.1p1 read walking past it, 14.1p3's unnamed place is declared, a dependent value argument keeps its spelling and is read again, `dependent_member_type` builds over the class *this* substitution made, and 14.5.6.1p5 rather than 13.1's index pairs two templates.  Those rules are right and swept clean - 8 unnamed-place shapes, 10 SFINAE and qualification-conversion shapes including the multi-level pointer 4.4 refuses, 6 shapes of the `Instantiated` boundary, and the discarded attempt leaves nothing a later naming reads, because the failure happens before `hold_specialization` and the class instantiations it made are 14.7.1p1's and permanent.  What none of it carried is that the same clause bounds the attempt: `Specialization::chosen` and `SemaAnalyzer::specialize` each hold their answer only once it is built, so a request arriving while that same one is being served recursed without bound - and the two new readings closed the loop, a dependent value argument read again at substitution and a stand-in rebuilt over the class the arguments named.  `300-recursive-streamable-sfinae-guard.t` and `300-dependent-adl-hidden-friend-before-later-value.t` are two programs the pre-checkpoint build translated to exit 0 and this one **exhausted the stack on**, reported by the harness as ordinary `EXIT_FAILURE` mismatches, with `300-recursive-trailing-return-sfinae-cache.t` the same crash one tier down and older than the checkpoint.  `TemplateInfo::choosing` and `SemaAnalyzer::specializing_` are the mark each tier takes, and a re-entrant request is refused - which 14.8.2p8 turns into the candidate discarded, the answer `g++` gives all three.  Beside them: 14.8.2p8's *lexical order*, which `substituted` read backwards for every declarator - the parameter list was built first however the return type was written, so a leading result type whose substitution is a hard error was never reached when a parameter was what failed, and `SemaEntity::trailing_result` is 8.3.5p2's fact that says which came first; and 14.1p12, which has no door at the pair 14.5.6.1p5 had just made findable, so two heads of one template each giving one place a default was a program both oracles refuse and this build translated.  Recorded rather than fixed: 14.6p2 is asked where the *pattern* reading reads a type-specifier, so five shapes whose reading a dependent context defers reach the clause with a prefix an argument list has already settled; and n declarations of one template name is quadratic in PA22's `TemplateSignature::equivalent` walk |
| 2 | `a63c183b` | 4 / 4 + 6 recorded | **the name the object file gives an address argument was this unit's entry number for it, and 14.3.2p5's conversions were 8.5's.**  Checkpoints 2-5 made 14.1p4's address places, 10p1's `class-or-decltype`, 13.3.1.4p1's constructor template, 14.6.2p2's variable template and 14.6.2p1's settled prefix.  Those rules are right and swept clean - 11 base shapes, 7 converting-constructor shapes, 8 array-bound shapes, a variable-template stand-in that leaves no global behind, and `Substitution` having no destructor, so `took_places` standing lexically inside the attempt is the walk outside its `try` the comment claims.  What none of it carried is that the *bits* of an address argument are an entry of the constant-address table, numbered in the order this unit reached each address: `at<&left>` was written `_ZN2atILPi1EE4readEv` where both oracles write `_ZN2atIXadL_Z4leftEEE4readEv`, and two units of one program that each name it write one weak definition under two names, which no link can merge.  The suite could not see it, because the comparator drops `object=` from every function header before comparing - the checkpoint's own `100-an-address-argument-is-which-object-it-designates` fixture disagreed with its `.ref` on all five names and passed.  Beside it: 14.3.2p5's conversions were `at_pointer_place`/`at_reference_place`, which are 8.5's readings of the same places and take the three the clause's own note leaves out - a zero-valued integral constant, 4.10p3's derived-to-base, and any pointee at all - so `at<&d>` at a `B2 *` place over a `struct D : B1, B2` was accepted and *ran to the wrong storage*; 14.1p4's fifth place was declared by `non_type_place` and refused by every argument reader, so `template<decltype(nullptr) N>` was a head no list could fill; and 13.4p1 had no door at a function place, so `H<f>` beside two declarations of `f` took whichever the chain led with.  Recorded rather than fixed: a pointer-to-member place, which the layer below has no pointer to member at all for; a `void *` place; `(int*)0`, whose cast the spelling reader reads only integral targets of; and `char (&a)[sizeof(T)]` as a function parameter, which the pre-checkpoint build refuses identically and is PA22's |

## Current Checkpoint Review

Checkpoints 2 through 5 - 14.3.2p1's address argument, 14.8.2p8 at 14.5.5.1p1's
match with 10p1's `class-or-decltype`, 13.3.1.4p1's converting constructor
template, and 14.6.2p2's variable template with 14.6.2p1's settled prefix - were
reconstructed from their commits, from `dev/src` and from the README: what an
address argument *is*, which readers of it there are, which conversions reach
one, and what each new dimension costs. Four defects were found and fixed, six
gaps were probed as programs and recorded, and the rest is what the review
confirmed.

### Findings

**1. The object-file name of an address argument was this translation unit's
entry number for the address, so two units name one weak entity two ways - and
the comparator drops exactly the metadata that says so.** 14.3.2p1 makes such an
argument *which object it designates*, and the plan's own row names four readers
of that fact: the binding, the spelling, the mangling and the read-back. The
mangling was not one of them. `LocalContexts::argument_of` reached its
`is_value` arm and wrote `L <type> <value> E` over `types_.value_bits(type)` -
which for an address place is the `AddressTable` entry, numbered in the order
this unit first reached each address:

```cpp
// v1.t                                    // v2.t
int left = 1; int right = 2;               int left;
template<int *P> struct at { /* ... */ };  template<int *P> struct at { /* ... */ };
int both() { return at<&right>::read()     int main() { return at<&left>::read()
                  + at<&left>::read(); }                     + both(); }
// _ZN2atILPi2EE4readEv                    // _ZN2atILPi1EE4readEv
```

One `[binding=weak]` definition under two names is two definitions the link
never merges and one use no definition answers; the reference binary writes
`_ZN2atIXadL_Z4leftEEE4readEv` in both. The suite could not see it:
`lowir_function_shape_text` strips the whole `[object=..., binding=...]` bracket
from a function header before comparing, so the checkpoint's own
`100-an-address-argument-is-which-object-it-designates` fixture disagreed with
its `.ref` on all five of its names and passed.

The fix is the reader the fact was missing. `TypeTable::set_address_object` is
told, where 14.3.2p5's argument is settled, which declaration an entry stands
for; `LocalContexts::entity_of` hands the encoder that declaration under an
`ABI_DEFINITION_ENTITY` record - the facility `abi_mangle` already had and
nothing asked for - and `ABI_TEMPLATE_ARGUMENT_ENTITY` writes the two spellings
the ABI draws apart, `L _Z... E` where a reference names the object and
`X ad L _Z... E E` where a pointer is its address. The declaration's own encoding
is the one this unit already names its definition by, which is what a static data
member of a specialization (`_ZN3boxIiE1sE`) and a chosen function
(`_ZN3boxIiE3getEv`) each need and no spelling of the name can be split into.
An eleven-shape cross-product - a namespace variable, a static data member of a
plain class and of a specialization, an internal-linkage object, a member
function, a function template specialization, a null pointer at a pointer and at
a function-pointer place, an array decayed, and a reference place - is now
byte-identical to the reference binary on all eleven and to
`g++ -std=c++11` on ten. The eleventh is a substitution index inside a nested
function template's own name (`_Z4pickIiET_S0_E` against `g++`'s `S1_`), where
the reference agrees with us.

**2. 14.3.2p5's conversions were 8.5's, which take three the clause's own note
leaves out.** `address_argument` handed the argument to
`ConstexprReading::at_pointer_place` and `at_reference_place`. Those are the
readings an *initialization* of an object of that type makes: they take a
zero-valued integral constant as a null pointer, retype any address to the
place's type, and reach a class through a conversion function. 14.3.2p5 names
three conversions at a pointer place - 4.4's qualification, 4.2p1's
array-to-pointer, and 4.10p1 *from an argument of type `std::nullptr_t`* - and
none at all at a reference place, and its note says which two an initialization
would also take and this does not.

```cpp
struct left { int a; }; struct right { int b; }; struct both : left, right {};
static both object;
template<right *P> struct at { static int read() { return P->b; } };
int main() { object.b = 3; return at<&object>::read() - 3; }   // ran to -3
```

The derived-to-base one is not merely a program accepted: the address kept is
the whole object's, so `P->b` reads `left`'s storage. `TemplateHead::reaches_place`
is 14.3.2p5's own list, asked before either 8.5 reading runs, and
`SemaAnalyzer::qualification_convertible` is 4.4 already written once. Four
programs both oracles refuse translated before it and are refused now: the
derived-to-base above, `at<0>` at an `int *` place (the note's own example),
`at<&n>` at an `int *` place over a `long n`, and `at<n>` at a `const int &`
place over a `long n`.

**3. 14.1p4's fifth place was declared and no argument could fill it.**
`non_type_place` returned `std::nullptr_t` as one of the types a non-type place
may have, and `template_argument_value` ended at `integral_type(type) == kNoType`
and refused every argument written at one - so `template<decltype(nullptr) N>`
was a head both oracles accept and this build had no list for. 14.3.2p1's last
bullet is the door: an address constant expression *of type* `std::nullptr_t`,
which is the same line the note beside 14.3.2p5 draws at a pointer place, so
`one<nullptr>` names the place and `one<0>` still refuses where `g++` refuses it.

**4. 13.4p1 had no door at a function place.** `TemplateArgumentReader::name`
looked the spelling up and read whatever the chain led with, so `H<f>` beside
`int f(int)` and `int f()` at an `int (*)()` place refused with *a call passes
the wrong number of arguments*, and `H<pick>` over a function template refused
outright. The target is the place's own type and `SemaAnalyzer::resolve_target`
is the walk `int (*p)() = f;` already goes through, including 14.8.2.2p1's
deduction from the target - so the reader is handed the place, exactly as it is
handed 8.3.2p1's *designate this name* at a reference place, and the first name
read answers it. `H<f>`, `H<&f>`, `H<pick>` and a function-reference place all
agree with `g++` now.

### What the review confirmed rather than found

- **10p1's `class-or-decltype` is read at both arms and is no second parser.**
  Eleven shapes translate and run the value `g++` gives them: a plain
  `decltype(make())` base, `decltype(make())::self`, a dependent one, a pack
  expanded as `decltype(pick<T>())...`, one under a template-id, one whose
  operand holds a comma and one whose operand holds `>`, and a base reached
  through `traits<decltype(make())>::type`. The one refusal is a `virtual` base,
  which this milestone lays out nowhere.
- **13.3.1.4p1's candidate set and 13.3.3p1's tie-break are right.** Seven
  shapes: the template alone, the template tied with a non-template, `explicit`,
  `= delete`, two constructor templates over unrelated classes, an ambiguity
  between two classes, and one gated by `enable_if` - each agrees with both
  oracles.
- **The variable-template stand-in leaves nothing in the output.** `enabled<T>`
  under an outer head gives back a `Variable` held against the interned list
  with no constant and no definition; the emitted LowIR for a program gated on
  one holds no `global` and no `declare global` at all, as the reference's does
  not.
- **The widened array bound reaches every reader of `bound_place`.** Eight
  shapes over `char[sizeof(T)]` - two patterns whose bounds differ, a bound over
  `sizeof...`, an alias template, a member, a plain place bound, and a list no
  pattern takes - all agree with both oracles. `match_bound`, `collect_packs`,
  `key_of`, `substituted_array` and `is_dependent` each already read a bound
  that names no place as the nothing it is.
- **`took_places` really does stand outside the attempt.** `Substitution` has no
  destructor - it holds `stood_in_` and is asked by an explicit `discards` - so
  the walk running while `attempt` is still in scope is outside its `try`, and
  the `Instantiated` it throws refuses the program through every enclosing
  attempt.
- **Nothing is gated and no phase is skipped.** The whole diff of checkpoints 2
  through 5 and this audit's holds no `getenv`, no fixture name, no timeout, no
  environment read, no dialect switch keyed on anything but a dialect, and no
  caught exception standing for a success.
- **`valgrind -q --error-exitcode=9` is clean over 140 inputs**: every probe of
  this audit, the eight scaling inputs and the eight course fixtures.

### Recorded rather than fixed

- **A pointer-to-member place, because the layer below has no pointer to member
  at all.** `int S::*p = &S::m;` and `int (S::*q)() = &S::f;` are two programs
  both oracles translate and this build refuses, so 14.1p4's fourth bullet is a
  milestone boundary and not a gap of this checkpoint's.
- **A `void *` place.** `g++` accepts the declaration and takes a null argument
  at it; the reference accepts the declaration and refuses every argument. 14.1p4
  says *pointer to object*, and `void` is none, so the three part company where
  the standard's own text is narrowest. Ours refuses the declaration.
- **A cast written as a template argument reads only integral targets**, so
  `at<(int*)0>` and `at<(int*)&n>` are refused where 14.3.2p5's note names both
  as valid. The reference refuses them too; only `g++` takes them. The consequence
  is that `at<nullptr>` and `at<(int*)0>`, which are one specialization, cannot
  be written the second way.
- **`char (&a)[sizeof(T)]` as a *function parameter* is `sizeof names an
  incomplete type`.** Both oracles take it. The pre-checkpoint binary refuses it
  identically, so the bound over a place is read where a class template's member
  and an alias write it and not where a function template's parameter does - a
  PA22 reading and not this checkpoint's.
- **Two partial specializations reached through a non-deduced bound are not
  ordered.** `D<T, char[sizeof(T)]>` beside `D<T, char[4]>` over `D<int, char[4]>`
  is ambiguous to both oracles and answers 2 here. The plan already carries it.
- **`std::nullptr_t` is mangled `LDn0E`**, which is what the reference writes and
  what `g++` wrote before it dropped the value from that production. Nothing
  compares the two, `object=` being stripped, so the reference is followed.
- **The reference emits a stub for a `std::nullptr_t` place forwarded through an
  outer head**: `through<M>::only()` returning `one<M>::only()` is lowered by it
  as `return i32 0`, with the call gone. No compile-pass fixture can pin that
  shape, so the course fixture names the place directly.

### Changes

| Where | What |
|-------|------|
| `type_model.h`, `type_model.cpp` | `TypeTable::set_address_object` / `address_object`: which declaration an entry of the constant-address table stands for, for the one reader that has a value and needs a name. |
| `sema_template_head.cpp` | `address_argument` tells the table which declaration the entry it settled is. |
| `lowir_abi.cpp` | `LocalContexts::entity_of` and the address arm of `argument_of`: 14.3.2p1's argument is written as the declaration's own mangled name. |
| `abi_mangle.cpp` | `ABI_TEMPLATE_ARGUMENT_ENTITY`'s two spellings - `L _Z... E` for a reference argument and `X ad L _Z... E E` for a pointer one. |
| `sema_template_head.{h,cpp}` | `TemplateHead::reaches_place`, 14.3.2p5's own conversions, asked before 8.5's readings of the same places. |
| `sema_value_expression.cpp` | 14.3.2p1's last bullet at a `std::nullptr_t` place; `TemplateArgumentReader::target` and 13.4p1's set at a function place. |
| `sema_template_head.{h,cpp}` | `TemplateHead::function_place`, which of 14.1p4's address places 13.4p1's target is asked at. |

### Performance Evidence

Measured on the audited binary, warm cache, `/usr/bin/time` on the binary itself
(a loop that spawns `timeout` per run reads the same 413-file corpus as 45.9 s
against 2.6 s, which is the wrapper and not the compiler).

| sweep | shape | result |
| --- | --- | --- |
| address-argument multiplicity | n objects, one specialization per `&obj` | 0.00 s @32, 0.01 @128, 0.06 @512, 0.12 @1024 - linear |
| function-argument multiplicity | n functions, one specialization per `&f`, each mangled through a nested encoder | 0.00 s @32, 0.01 @128, 0.06 @512, 0.13 @1024 - linear |
| one object named n times | n namings of one `at<&o>` | 0.00 s @32, 0.01 @512, 0.03 @1024 - one specialization and one entity record |
| SFINAE multiplicity | n classes x 2 candidates, one failing substitution | 0.01 s @32, 0.03 @128, 0.15 @512, 0.30 @1024 - and 0.01 / 0.03 / 0.14 / 0.32 on the pre-checkpoint binary |
| substitution nesting | d nested trait layers under one `enable_if` | 0.00 s flat from d = 8 to d = 48 |
| name nesting | d nested class templates over the object an argument designates | 0.00 s flat from d = 4 to d = 32 |
| whole PA23 corpus | 413 files, one process each | **2.6 s** warm, no `rc > 1`, valgrind clean |

`entity_of` is one record per declaration per encoded name and `abi_symbol_of`
of it is the walk that unit already makes for the definition, so a name written n
times costs one; `reaches_place` is a type comparison over the pointer levels
already in hand; and `resolve_target` walks one lookup chain and only where the
place is a pointer or reference to a function.

### Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa23'` - **332 / 413**, against
  328 / 409 at the turn's start, with no test that passed then failing now: the
  handout set is unchanged at 324 / 405 and the four new course fixtures pass.
- `make test-report-through-pa22` - **2948 / 2948**, 22 / 22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src` - **pass**,
  with the five `bad-division` warnings it already had.
- All 413 corpus files compiled one at a time: **0 crashes**.
- 140 inputs under `valgrind -q --error-exitcode=9`: 0 errors.
- Every probe in this audit compared against `g++ -std=c++11 -pedantic-errors`
  and against `pa23/cppgm++-ref`, and every accepted one run through
  `lowir2cy86` + `cy86` to the value `g++` runs it to.
