# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_`, so a template-name at a function-tier place was unchecked in both directions; 14.6.1p1's injected-class-name arm swallowed a template-*id* written at a place; 14.3.3p1's pack matched no run of none and compared only each place's kind; and `match_template_id` read `L<A…>` against a class alone, so 14.5.5.2p1 ordered no two patterns over a template place and 14.8.2.1's two allowances reached neither |
| A | `202f04ec` | 4 / 4 + 6 recorded | **the three things a template-id can be looked up in, with 14.2's own exit written at two of them - and the access 11p1 gives a member template, carried by one of the three tiers that make a declaration from an argument list.**  Checkpoint A landed 14.5.7p1's alias template and moved 14.2p4's keyword into `QualifiedName::part`.  The alias is right and swept clean: 7.1.3p2's transparency, 14.5.7p2's identity, the ABI name, the region a member alias's access travels from, the memo that keeps a nest naming the one below twice flat to depth 24 - 84 probes pass the assignment's own comparator against the reference.  What the keyword's move did not carry is the *reason* it was moved: the commit names `a.template f<A>()` as one of three refusals it ends, and `member_named` - 5.2.5p1's lookup, the third of the three - had no 14.2 exit at all, so every `a.template f<int>()`, `p->template f<int>()` and `this->template f<int>()` was still `no declaration of f<int> is in scope` where both oracles translate.  Beside it the move made `last()` shorter than the span it was taken from, and three readers took the nested-name-specifier off the spelling by that length: `d.B::template f<int>()` came out as `B::templat names no class`.  Under the new exit, 11p1's access is a fact `instantiate_class` and the alias arm both write onto the declaration one argument list makes and the two function-tier exits never did, so a *private* member template reached through `.` was accepted where both oracles refuse.  And `template_id_entity` may now answer with a typedef-name: 14.7.2p2's reader took `made.primary` without asking, so `template struct P<int>;` over an alias **segfaulted** where both oracles refuse |


## Current Checkpoint Review

Checkpoint A is where a template-id stopped having to name a class. 7.1.3p2
makes an alias-declaration *another name for* the type its type-id wrote, so
14.5.7p1's alias template declares nothing an argument list can specialize:
`X<A…>` **is** the type the arguments substitute into the pattern. It joins the
two heads `sema_specialize` already owns whose last step differs from the
primary's, and the declaration it leaves is a `Typedef` carrying a
`TemplateInfo`. Beside it the checkpoint moved 14.2p4's keyword out of one
reader and into `QualifiedName::part`, which is where every reader already asks
for a component.

The alias half is right and is right everywhere it was asked. 7.1.3p2's
transparency holds through a parameter, a pointer, a base-specifier, a
`typedef`, a template-template argument and a second translation unit -
`_Z3use3boxIiE` for a parameter written through the alias agrees with
`g++ -std=c++11` byte for byte; 14.5.7p2 leaves two namings of one list one
type, and `A1<int>` and `A2<int>` over two aliases with one pattern one type;
`Specialization::alias` memoises on `(template, interned argument list)`, so a
nest of aliases each naming the one below **twice** is flat at 6.5 MB and 0.00 s
to depth 24 where the unmemoised reading is 2^depth; 11p1's access travels from
the template onto the typedef-name one list makes, so a private member alias is
refused as `g++` refuses it and the reference does not; and 84 probe programs
pass `pa22/scripts/compare_results.pl` itself against the reference's LowIR.

What the review found is one sentence about the *other* half. The keyword's
move is a good move - the split is the one place that knows where a component
begins - but a component handed back without the keyword is no longer as long
as the span of the spelling it came from, and the exit it was moved out of was
never the exit that made the three refusals it was moved to end.

### Findings

**1. 5.2.5p1's lookup had no 14.2 exit.** `resolve` answers a name no
declaration bound with `template_id_entity` at *both* of its exits, and
`call_expression` answers an id-expression callee with `template_specializations`
before it resolves anything. `member_named` - the third region a name can be
looked up in, the class of the object expression - had neither: its unqualified
arm is `lookup_in` and 12.4's destructor and nothing else. So

```cpp
struct S { template<class T> int f() { return 0; } };
int main() { S s; return s.template f<int>(); }   // no declaration of f<int> is in scope
```

and the same through `->` and through `this->` were three programs both oracles
translate and this build refused - the refusal the checkpoint's own commit names
as one of the three it ends. `template_specializations` now takes the region
5.2.5p1 looks a member up in, which is the one thing the member-access path
knows that a spelling does not, and `member_named` asks it where `resolve`
already asks. `p->~box<int>()` and a member id that is no template-id are
unaffected: the arm answers null for a name the class declares no template of,
and the destructor arm still stands behind it.

**2. Three readers took the prefix off the spelling by the length of the last
component.** `part` hands back a component without 14.2p4's keyword, so
`last().size()` is no longer the width of the span it was read from, and

```cpp
struct B { template<class T> int f() { return 0; } };
struct D : B { };
int main() { D d; return d.B::template f<int>(); }   // B::templat names no class …
```

sliced the nested-name-specifier nine characters late. The other two -
`AstParser::name_qualifier` and `template_specializations`'s own
`spelling - component + id.name()` - reach the same wrong span for the same
spelling; the second happened to heal, because what it left the keyword in front
of was a component `resolve` splits and strips again. `QualifiedName::prefix`
is the reading: the split already recorded where the last component starts, so
the prefix is that offset and depends on nothing the component was written with.

**3. 11p1's access is carried by one of the three tiers that make a declaration
from an argument list.** `instantiate_class` writes `made->access =
primary.access` and the checkpoint's own alias arm writes it too; `specialize`,
`partial_template` and `Specialization::read_variable` never did. It was
invisible while every reader asked 11.2 of the name a lookup *found* - which for
a qualified naming is the primary - and finding 1 makes the member-access path
ask it of the specialization instead, where

```cpp
class S { template<class T> int f() { return 0; } };
int main() { S s; return s.template f<int>(); }   // accepted; both oracles refuse
```

Every tier now says the same thing: the access a class gave was given to the
template, and the declaration one argument list makes of it is only that.

**4. 14.7.2p2's reader took `made.primary` without asking.** `template_id_entity`
answers a template-id, and after this checkpoint one of its answers is a
typedef-name over an alias template - which has no primary, because 7.1.3p2
leaves it no specialization to be one of. `explicit_instantiation` handed that
straight to `require_specialization`, which dereferences
`made.primary->templated`:

```cpp
template<class T> struct box { T n; };
template<class T> using P = box<T>;
template struct P<int>;                          // SIGSEGV; both oracles refuse
```

14.7.2p2's own words are the gate - the elaborated-type-specifier shall name a
class template specialization - and it now covers 14.6.2p1's naming over an
unsettled place as well, which reaches the same reader with the same null.

### What the review confirmed rather than found

- **The alias is one reading per template and interned list.** `alias` asks
  `specialization_of` first and `hold_specialization` last, so `P<int>` written
  3200 times is read once (0.16 s, 47 MB) and a nest whose every level names the
  one below twice is flat from depth 4 to depth 24. The `ReadingList` guard
  refuses an alias whose type-id names its own list rather than recursing, where
  the reference crashes.
- **The alias is transparent to the object file.** One symbol
  `_Z3use3boxIiE` across two translation units, and `holder<P>`'s member is
  `box<int>` and not a class of the alias's own name - the same answer `g++`
  gives.
- **The keyword is dropped once.** `part` and `without_template_keyword` share
  one `past_template_keyword`, so `X::template f<A>`, `a.template f<A>()`,
  `typename A<T>::template B<int>`, `holder<S::template X>` and a chain of them
  are one rule read at one place.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch or an environment
  read; the new exits are reached in all three dialects alike.
- **valgrind is clean** over all 90 probe programs and over the five largest
  scaling inputs, 0 errors.

### Recorded, not landed

- **Two function templates of one name overloaded by *arity*** -
  `f<int>()` and `f<int, char>()` over `template<class>` and
  `template<class, class>` - are `f is defined twice` here and translate in both
  oracles. It is written with no alias, no keyword and no class, so it is the
  declaration-merge tier and not this checkpoint's.
- **`&S::f<int>`** is `a binary operator is outside the PA12 subset`: a
  template-id at a member name outside a call is the plan's parse group, which
  the next checkpoint owns.
- **`s.template f()` on a member that is no template** is accepted here and in
  the reference and refused by `g++`, which is 14.2p5's requirement neither
  oracle asks about.
- **A second `using P = …` with a *different* type-id**, `extern template struct
  P<int>;` and `struct P<int> { };` are accepted here and in the reference and
  refused by `g++`.
- **`template<class T> int P<T>::get()`**, a member definition written through
  an alias template's name, is refused here and in the reference and accepted by
  `g++`.
- **A private member class template, a private member variable template and a
  use written before the alias template's declaration** are refused here and by
  `g++` and accepted by the reference.

## Changes

- **`sema_name.cpp/.h` — `QualifiedName::prefix`**, called from
  `sema_expression.cpp`'s `member_named`, `ast_parser_declarator.cpp`'s
  `name_qualifier` and `sema_template.cpp`'s `template_specializations`: the
  nested-name-specifier read off the split that already recorded where the last
  component starts, rather than off the spelling by that component's length.
- **`sema_expression.cpp` — `member_named`**, with
  `sema_template.cpp`'s `template_specializations` taking the region: 14.2's
  exit at 5.2.5p1's lookup, so a member template named through `.`, `->` or
  `this->` reaches the specializations its argument list makes.
- **`sema_template.cpp` — `specialize` and `partial_template`, and
  `sema_specialize.cpp` — `read_variable`**: 11p1's access carried onto the
  declaration one argument list makes, which the class tier and the alias
  already carry.
- **`sema_template.cpp` — `explicit_instantiation`**: 14.7.2p2 asked of what the
  template-id answered, so an alias template's typedef-name and a naming over an
  unsettled place are refused where they were dereferenced.

## Performance Evidence

Best of three with `/usr/bin/time`, against a `make build` of `202f04ec` in a
worktree so every number has a baseline rather than a memory, and against
`pa22/cppgm++-ref`.

| shape | this build | pre-audit build | `pa22/cppgm++-ref` |
| --- | --- | --- | --- |
| one alias named 400 / 800 / 1600 / 3200 times | 0.02 / 0.04 / 0.07 / **0.16 s** at 11 → 48 MB | 0.02 / — / — / 0.16 s | 3.62 s at 3200 |
| n distinct argument lists through one alias, 400 → 3200 | 0.05 / 0.12 / 0.25 / **0.54 s** at 22 → 135 MB | 0.05 / — / — / 0.54 s | 7.11 s at 3200 |
| member alias template named 400 / 3200 times | 0.02 / **0.16 s** at 12 / 48 MB | 0.02 / 0.16 s | 2.25 s at 3200 |
| alias nest, each naming the one below **twice**, depth 4 → 24 | **0.00 s, flat at 6.5 MB** | 0.00 s | 0.56 s at 24 |
| `S::template f<int>()` written 400 / 3200 times | 0.01 / **0.07 s** at 8 / 24 MB | 0.01 / 0.07 s | 1.10 s at 3200 |
| `s.template f<int>()` written 400 / 3200 times | 0.01 / **0.08 s** at 9 / 27 MB | refused | 0.93 s at 3200 |
| `s.template f<Ti>()` over 3200 *distinct* lists | **0.34 s** at 92 MB | refused | 11.06 s |
| `.template g<int>()` chained, depth 4 / 16 / 64 | 0.00 s, flat at 6.4 / 7.2 MB | refused | — |
| the whole 308-file PA22 corpus | **1.26 s** | 1.26 s | — |

Every dimension is linear in what it sweeps and every one that has a baseline
matches it: `prefix` reads one offset the split already held, the access is one
assignment per specialization made, and the member-access exit is the probe the
qualified path already pays - none of the three is a term of the input. The two
member-access rows have no baseline because the pre-audit build refuses those
programs; both are linear to 3200 and 33× faster than the reference on the
distinct-list sweep. The nesting sweeps were run to depth 24 and the
multiplicity sweeps to 3200; the alias nest carries no 2^depth term because the
reading is interned per template and list.

Not this checkpoint's, but re-measured: an alias nest whose *result* type
doubles per level (`pair2<a<T>, a<T> >`) is inherently 2^depth - depth 20 is
0.39 s / 178 MB and depth 24 is 6.80 s / 2.37 GB, which is the known
per-subobject layout cost and not the alias layer's.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` — **193 / 308**, the turn's
  baseline held, with the failing set byte-identical to the turn's: the three
  `.template`/`->template` tests in it also want the two-clause out-of-class head
  the next checkpoint owns, so the new exit unblocks them without flipping them.
- `make test-report-through-pa21` — **pass**, 2568 / 2568, 21 / 21 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa22 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- 90 systematic probe programs swept against `pa22/cppgm++-ref` and
  `g++ -std=c++11 -pedantic-errors`: 30 over 7.1.3p2 and 14.5.7's alias
  template, 21 over 14.2p4's keyword at each of the three lookups, 10 over the
  access a member template's specialization carries, 8 over what a template-id
  may be explicitly instantiated or specialized as, and the rest over the cross
  product with packs, defaults, value places, template places, bases,
  namespaces, two translation units and source order. Every disagreement judged
  against the standard and the third oracle rather than copied.
- 84 of them - every shape where this build and the reference agree on the
  verdict - through `pa22/scripts/compare_results.pl` itself in a scratch
  directory under `pa22/`: **PASS (84 / 84)**, so the LowIR and not only the
  exit status is the reference's.
- Nesting-depth sweeps to 24 and multiplicity sweeps to 3200 on each new
  reading; all linear, all matched to the `202f04ec` baseline.
- `valgrind -q --error-exitcode=9` over all 90 probes and the five largest
  scaling inputs: **clean**, 0 errors.
- No `.ref` regenerated: every producer this audit changed either refuses a
  program the fixtures do not write or answers one they already pin, which the
  unchanged failing set and the 84-probe comparator run together show.
