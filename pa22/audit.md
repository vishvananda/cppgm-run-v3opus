# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_`, so a template-name at a function-tier place was unchecked in both directions; 14.6.1p1's injected-class-name arm swallowed a template-*id* written at a place; 14.3.3p1's pack matched no run of none and compared only each place's kind; and `match_template_id` read `L<A…>` against a class alone, so 14.5.5.2p1 ordered no two patterns over a template place and 14.8.2.1's two allowances reached neither |
| A | `202f04ec` | 4 / 4 + 6 recorded | **the three things a template-id can be looked up in, with 14.2's own exit written at two of them - and the access 11p1 gives a member template, carried by one of the three tiers that make a declaration from an argument list.**  Checkpoint A landed 14.5.7p1's alias template and moved 14.2p4's keyword into `QualifiedName::part`.  The alias is right and swept clean: 7.1.3p2's transparency, 14.5.7p2's identity, the ABI name, the region a member alias's access travels from, the memo that keeps a nest naming the one below twice flat to depth 24 - 84 probes pass the assignment's own comparator against the reference.  What the keyword's move did not carry is the *reason* it was moved: the commit names `a.template f<A>()` as one of three refusals it ends, and `member_named` - 5.2.5p1's lookup, the third of the three - had no 14.2 exit at all, so every `a.template f<int>()`, `p->template f<int>()` and `this->template f<int>()` was still `no declaration of f<int> is in scope` where both oracles translate.  Beside it the move made `last()` shorter than the span it was taken from, and three readers took the nested-name-specifier off the spelling by that length: `d.B::template f<int>()` came out as `B::templat names no class`.  Under the new exit, 11p1's access is a fact `instantiate_class` and the alias arm both write onto the declaration one argument list makes and the two function-tier exits never did, so a *private* member template reached through `.` was accepted where both oracles refuse.  And `template_id_entity` may now answer with a typedef-name: 14.7.2p2's reader took `made.primary` without asking, so `template struct P<int>;` over an alias **segfaulted** where both oracles refuse |
| P | `0d1c75c9` | 4 / 4 + 6 recorded | **the two forms 14.7.2 writes one requirement in, with p2 asked at one of them - and the parse form the same commit opened, which reached no reading at all.**  Checkpoint P made every PA22 test parse: 14.2p4's keyword became optional at a member name, 14.2p1's operator template-id got one shared `skip_template_arguments`, and 14.7.2p1's declaration was let end at a `;` no out-of-class constructor otherwise may.  The member reading is right and is *narrower* than the reference's - `s.a < 1 > (0)` over two `int` members is two comparisons here and refused there - and 13.5p6 at the specialization agrees with `g++` on all five shapes probed.  What the commit's own comment claims and the code does not is that "the target is read either way": `explicit_instantiation` returned on `!owed` **before** reading anything, so `extern template struct P<int>;` over an alias, over a non-template, over a name no template declares and over a prefix naming no type were four programs `g++` refuses and this build accepted - the p9 twin of the segfault checkpoint A's audit had just fixed.  Beside it `is_explicit_instantiation_target` began accepting a `SpecialMemberDeclaration` that `explicit_instantiation` had no arm for: `extern template box<int>::box();` passed only because the early return got there first, and `template box<int>::box();` came out of the *class* arm as `an explicit instantiation defines a class`.  And the parser's `names_specialization_` was taken once and never put down, so every body under such a head read its statements as out-of-class special members: `template<> void f<int>(int) { A::A(); }` was refused where the same statement in an ordinary function is accepted |


## Current Checkpoint Review

Checkpoint P is where every PA22 test began to parse. Three readings landed:
14.2p4's keyword became optional at a member name, so `h.get<int>(4)` is a
template-id the parse has to recognise with nothing but a unit-wide record and
the `(` that 5.2.2p1 says has to follow; 14.2p1's other two template-ids -
`operator+<int>` and 14.5.6.1's `operator+<>` - moved onto one
`skip_template_arguments` shared with the simple-template-id; and 14.7.2p1 and
14.7.3p1 were let name a specialization rather than declare anything, so a
declaration may end at a `;` 12.1p1 otherwise gives an out-of-class constructor
no meaning at.

The member reading is right, and it is right in the direction that matters: it
is *narrower* than the reference's. `s.a < 1 > (0)` written over two `int`
members is two comparisons here and `unknown member function` there, because the
guess is taken only for a spelling some declaration of the unit made a template
and only where a `(` follows the `>`. 13.5p6 asked where a function template
specialization is *made* agrees with `g++` on every shape probed, and costs one
`compare(0, 8, "operator")` per specialization.

What the review found is all on the third reading, and all of it is the same
shape: the checkpoint wrote one exit of 14.7.2 and left the others reading the
old question - or reading nothing at all.

### Findings

**1. 14.7.2p9's form read no target.** The commit's own comment says p9
"differs from p8's form in what it asks of this unit and in nothing else - p2's
requirement that the declaration name a specialization is the same requirement
written the same way - so the target is read either way". The code returned on
`!owed` before reading it. So

```cpp
template<class T> struct box { T n; };
template<class T> using P = box<T>;
extern template struct P<int>;                   // accepted; g++ refuses
```

and `extern template struct plain;` over an ordinary class, `extern template
struct nosuch<int>;` over a name no template declares, and `extern template
box<nosuch>::box();` over a prefix naming no type were four programs this build
accepted and `g++` refuses - the p9 twin of exactly the `template struct P<int>;`
that checkpoint A's audit had found segfaulting. The `owed` gate now stands
where p8's *demand* is, which is the one thing p8 and p9 differ in;
`instantiated_class` is p2's reading and both forms reach it.

**2. The parse form the same commit opened reached no arm.**
`is_explicit_instantiation_target` began accepting a `SpecialMemberDeclaration`
so that `extern template box<int>::box();` would parse. `explicit_instantiation`
had arms for a `SimpleDeclaration` and a `ClassForwardDeclaration` and nothing
else, so that program passed only because finding 1's early return got there
first, and its p8 twin

```cpp
template box<int>::box();                        // an explicit instantiation
                                                 // defines a class …
```

fell through to the *class* arm and came out with a diagnostic about a class it
does not write. 12.1p1 gives a constructor no type a declarator can be read
for, so the specialization such a declaration names is written as its **prefix**
- and that is where p2 is now asked. The definition p8 would then owe is
14.5.2's constructor template, which the next checkpoint owns and which this
declaration is told to ask nothing of.

**3. 14.7.2p1's member class of a specialization.** Once the target is read for
both forms, `outer<int>::in` reaches the same reading, and its last component is
no template-id - so `template_id_entity` answered null and p2 refused a program
both oracles translate. p1 lets an explicit instantiation name a *member class*
of a class template specialization, which is no specialization of a template of
its own: the class its prefix named declared it as an ordinary member. So the
component is looked up in the region `resolve_prefix` has just had to settle,
and p2 falls on the prefix - `plain::in` is still refused, `outer<int>::in::deep`
is not, and `require_specialization` is asked only of what a primary made.

**4. `names_specialization_` was taken once and held through every body under
it.** The parser sets it around the `parse_declaration` a specialization's head
stands on, and 14.7.3p1's declarator is the whole of what it is about. Nothing
put it down for the body that declaration holds, so

```cpp
template<class T> void f(T) { }
template<> void f<int>(int) { A::A(); }          // refused; the same statement
                                                 // in an ordinary function is
                                                 // accepted
```

read its statements as out-of-class special members - a flag that says what the
reader is reading, stolen by the next thing the reader reads. It is put down at
the compound statement and taken up again where the body ends, which makes an
explicit specialization's body read exactly as any other body does.

### What the review confirmed rather than found

- **The member template-id guess is bounded by the two facts it was given.**
  `names_a_template` is a hash lookup on the unit-wide record, and 5.2.2p1's
  `(` is what keeps `a.b < c > d` two comparisons. Over two plain `int` members
  the reference guesses and this build does not; `h.get<int>(0)`,
  `h->get<int>(0)` and `s.template h<int>()` all translate.
- **13.5p6 at the specialization is the right tier and the right cost.** A
  non-member `operator+<int>(1, 2)` is refused here and by `g++`; the ordinary
  `1 + 2`, a class operand, a deduced class operand and `operator<` over
  `const T&` are accepted by all three. One name comparison per specialization
  made, and none at all for the ones a template did not name `operator…`.
- **14.2p1's three template-ids are one reader.** `operator+<A>`,
  `operator< <A>` written with the space 14.2p3 needs, `operator><A>` and
  14.5.6.1's `operator+<>` are the same `skip_template_arguments` the
  simple-template-id uses, taken behind the same veto depth.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch, a timeout or an
  environment read; the new exits are reached in all three dialects alike, and
  `instantiated_class` keeps PA11's and PA12's "instantiate nothing" answer
  exactly where it stood.
- **valgrind is clean** over all 25 probe programs and the five largest scaling
  inputs, 0 errors.

### Recorded, not landed

- **The member reading is asked where the parse stands, not where the unit
  ends.** A member function template named through `.`, `->` or `this->`
  *before* its declaration in the same class body is a parse failure here and
  translates in both oracles: 9.2p2's complete-class context is not something a
  single-pass parse answers, and the *unqualified* reading escapes it only
  because it is gated negatively - `!is_value(name)` - where a member name has
  to be gated positively. Widening the member gate to match would make
  `a.b < c > (d)` a template-id everywhere, which is the trade the `(` bound was
  chosen to avoid.
- **A member that shares a template's spelling is guessed wrong.**
  `s.get < 1 > (2)` over an `int` member named `get`, where the unit also
  declares a template `get`, is a template-id here and two comparisons in `g++`.
  The reference refuses it too; only a lookup in the object's class tells them
  apart, and the parse models no class.
- **`template box<int>::box();` and `template struct outer<int>::in;`**
  translate here and in `g++` and are `unsupported explicit instantiation
  target` in the reference.
- **The five p9 forms finding 1 now refuses** are accepted by the reference and
  refused by `g++`, which is the same disagreement checkpoint A's audit recorded
  for their p8 twins.
- **`A::A();` written as a statement** is accepted here and in the reference and
  refused by `g++`, in an ordinary function as much as in a specialization's
  body. It is 5.1.1p8's question about a constructor's name, not this
  checkpoint's.
- **The definition 14.7.2p8 owes for a constructor** is 14.5.2's constructor
  template, which is the next checkpoint.

## Changes

- **`sema_template.cpp` — `explicit_instantiation`**, with the new
  `instantiated_class`: 14.7.2p2 read off the elaborated name either form wrote,
  so p9's `extern template` asks the same requirement p8's does and the `owed`
  gate stands where p8's demand for the definitions is.
- **`sema_template.cpp` — `explicit_instantiation`'s special-member arm**:
  12.1p1's own declaration of a constructor names the specialization as its
  prefix, which is where p2 is asked, instead of falling through to the class
  arm's diagnostic.
- **`sema_template.cpp` — `instantiated_class` and
  `names_a_specialization_member`**: 14.7.2p1's member class of a specialization
  looked up in the region the prefix resolved to, with p2 falling on the prefix
  and `require_specialization` asked only of what a primary made.
- **`ast_parser_statement.cpp` — `parse_compound_statement`**: 14.7.2p1's and
  14.7.3p1's "this declaration names a specialization" put down for the body it
  holds, so a statement is read as a statement.

## Performance Evidence

Best of three with `/usr/bin/time`, against a `make build` of `0d1c75c9` in a
worktree so every number has a baseline rather than a memory, and against
`pa22/cppgm++-ref`.

| shape | this build | pre-audit build | `pa22/cppgm++-ref` |
| --- | --- | --- | --- |
| `extern template struct box<Ti>;`, n distinct, 400 → 3200 | 0.02 / 0.04 / 0.08 / **0.18 s** at 11 → 52 MB | 0.01 / — / — / 0.17 s at 47 MB | 0.76 s at 3200 |
| `extern template box<Ti>::box();`, n distinct, 400 → 3200 | 0.02 / 0.04 / 0.08 / **0.17 s** at 11 → 51 MB | 0.01 / — / — / 0.17 s | 0.74 s at 3200 |
| `template struct box<Ti>;`, n distinct, 400 → 3200 | 0.03 / 0.06 / 0.14 / **0.33 s** at 15 → 85 MB | 0.03 / — / — / 0.33 s | 3.75 s at 3200 |
| `h.get<int>(i)` with no keyword, n times, 400 → 3200 | 0.01 / 0.02 / 0.05 / **0.10 s** at 8 → 28 MB | 0.01 / — / — / 0.10 s | 1.04 s at 3200 |
| `h.get<Ai>(a)` over n *distinct* argument lists, 400 → 3200 | 0.07 / 0.14 / 0.28 / **0.61 s** at 23 → 141 MB | 0.06 / — / — / 0.62 s | 35.37 s at 3200 |
| compound statements nested inside a specialization's body, depth 4 → 256 | 0.00 / 0.00 / 0.00 / **0.01 s**, 6.5 → 8 MB | 0.00 / — / — / 0.01 s | — |
| the whole 308-file PA22 corpus, one process per file | **1.31 s** | 1.28 s | — |

Every dimension is linear in what it sweeps and every one matches its baseline.
The one row that moved is the first, and it moved in memory rather than in time:
p9's form now makes the specialization entity p2 has to be asked about, which is
5 MB over 3200 declarations and the same entity p8's form has always made. The
compound-statement guard is two assignments per block and carries no term of the
depth, which the 4 → 256 sweep holds flat. Nesting sweeps were run to depth 256
and multiplicity sweeps to 3200.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa22'` — **200 / 308**, the turn's
  baseline held, with the failing set byte-identical to the turn's: no fixture
  writes any of the four shapes above, which is why the checkpoint landed with
  them.
- `make test-report-through-pa21` — **pass**, 2568 / 2568, 21 / 21 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa22 --paths dev/src` — **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth. The
  guard finding 4 needed was written into `parse_compound_statement` rather than
  as a class in `ast_parser.h`, because eight declaration lines there is what
  pushes that header past the audit's body-weight line - which is the same
  ceiling the checkpoint itself moved `BracketGuard`'s bodies out to stay under.
- 25 systematic probe programs swept against `pa22/cppgm++-ref` and
  `g++ -std=c++11 -pedantic-errors`: 15 over what each of 14.7.2's two forms may
  name - a specialization, an alias, an ordinary class, an undeclared name, a
  constructor, a member class, a member class two deep - 5 over what a body
  under a specialization's head may hold, and 5 over the member template-id and
  operator template-id readings. Every disagreement judged against the standard
  and the third oracle rather than copied: this build now agrees with `g++` on
  **all 25**, and with the reference on 16.
- All 25 through `pa22/scripts/compare_results.pl` itself in a scratch directory
  under `pa22/`, with `KEEP_GOING=1` so the run does not stop at the first
  difference: **16 pass**, and the 9 that do not are the recorded disagreements
  above, each one a verdict rather than a LowIR difference.
- Nesting-depth sweeps to 256 and multiplicity sweeps to 3200 on each changed
  reading; all linear, all matched to the `0d1c75c9` baseline.
- `valgrind -q --error-exitcode=9` over all 25 probes and the five largest
  scaling inputs: **clean**, 0 errors.
- No `.ref` regenerated: every producer this audit changed either refuses a
  program the fixtures do not write or answers one they already pin, which the
  unchanged failing set and the 16-probe comparator run together show.
