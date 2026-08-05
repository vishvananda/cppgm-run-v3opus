# PA6 `recog` Plan

## Stage Design

`recog` answers one question per source file: does its phase-1..7 token
sequence match `translation-unit` of `pa6.gram`?  Output is `OK`/`BAD` only, so
the stage owns three new pieces of typed state and reuses everything else:

| Owner | Fact it owns |
| --- | --- |
| `dev/src/parse_token.h/.cpp` | `ParseToken`: one PA6 terminal plus the lexical facts the grammar asks about a token (mock-lookup name categories, `ST_OVERRIDE`/`ST_FINAL`, `ST_EMPTYSTR`, `ST_ZERO`), 4 bytes each.  Built once per file from `Preprocessor` + `PostTokenizer`, with `OP_RSHIFT` split into `ST_RSHIFT_1 ST_RSHIFT_2` and a trailing `ST_EOF`. |
| `dev/src/parse_cursor.h` | `ParseCursor`: a position in that sequence plus the one bit of bracket state 14.2.3 reads back, and the `Mark` that undoes a failed alternative. |
| `dev/src/parse_depth.h` | `ParseDepth`: how deep a descent may go, and the `Frame` a re-entrant rule opens.  A file that nests deeper is refused, not crashed. |
| `dev/src/memo_table.h/.cpp` | `MemoTable`: the open-addressed table of remembered `(rule, position, angle)` results. |
| `dev/src/recognizer.h` + `recognizer{,_name,_expression,_declarator,_statement,_member}.cpp` | The recursive-descent recognizer: one function per nonterminal, built on the three above. |
| PA1-PA5 modules | unchanged; phases 1 to 7 are reused as-is. |

Mock name lookup is a lexical fact of the spelling (`C`/`T`/`Y`/`E`/`N`), so it
is computed once per token when the stream is built and never re-derived while
parsing.

Five decisions drive the parser shape:

- **Angle commit.** An identifier that is a `template-name` followed by `OP_LT`
  *must* parse as a `simple-template-id`; no alternative falls back to reading
  it as a plain identifier.  That is what makes `int x = T1 < 2;` `BAD`, and it
  is what bounds the work deep template nesting can cost.
- **Angle guard.** While the innermost open bracket pair is `<>`, `OP_GT` is
  refused as a relational operator and `ST_RSHIFT_1 ST_RSHIFT_2` as a shift
  operator, so the first non-nested close-angle-bracket always closes the pair.
- **FOLLOW-checked alternatives.** Where two alternatives share an unbounded
  prefix (`template-argument`, `parameter-declaration`, `template-parameter`,
  `class-head`, `enum-head`, `enum-base`, `alignment-specifier`,
  `exception-declaration`, `function-definition`), the alternative is accepted
  only when it ends where its caller can continue.  That is what tells `TC1<C*>`
  from `TC1<C+1>`, `alignas(C1)` from `alignas(C1+1)`, and `template<class T1...>`
  (a pack) from `template<class... T1>` (a type parameter) without backtracking
  into a rule that already succeeded.
- **Shorter qualifications.** A `nested-name-specifier` is greedy, but its last
  step can belong to the name after it: `::E1::a1` is the type `::E1` and the
  declarator `::a1` when `a1` names nothing.  `nested_name_specifier_ends`
  reports every prefix, longest first, and the rules that need a name right
  after a qualification try them in that order.
- **Bounded descent.** Every rule that can re-enter itself opens a `Frame`, so
  one counter bounds the machine stack; a file that nests deeper than the
  recognizer supports is `BAD`, never a crash.

## Performance Model

- **Token stream** - one pass, 4 bytes per token, no per-token allocation.
- **The memo is the dominant operation.** It is written about three times as
  often as it is read, so its cost is insertion, not lookup.  `MemoTable` is an
  open-addressed array of `(key, value)` slots: an entry is one slot and no
  allocation, and a probe is a multiply and a mask.  Two rules whose bodies are
  a token test plus another remembered rule (`unqualified-id`,
  `attribute-specifier-seq`) were 39% of all memo traffic at a 0.27 and 0.00 hit
  ratio and are no longer remembered.  Together: 16.3 MB of C++ went from
  3.98 s to 2.02 s, and the parse itself from about two thirds of the run to
  under a third.
- **Memoization is load-bearing, not an optimisation.** `template-argument` has
  three alternatives that each re-descend, so `N` nested `TC1<` costs `3^N`
  without a memo.  Measured with the memo lookup disabled, the deep witness
  takes 0.10 s at N=6, 0.40 s at N=7, 2.9 s at N=8 and more than 30 s at N=10.
  With the memo it is 0.00 s at N=1600.
- **Memo budget.** A memo entry is a fact about a position, so dropping one
  costs time and never correctness.  The table is released at a top-level
  declaration boundary once it passes 2^18 entries, which bounds it by the
  largest single declaration instead of by the translation unit.
- **Throughput** - 2.0 MB in 0.30 s / 26 MB, 8.2 MB in 1.14 s / 41 MB, 16.3 MB
  in 2.02 s / 74 MB: linear in tokens.  Of the 8.2 MB run, 0.70 s is phases 1
  to 7 and 0.44 s is the recognizer.
- **Depth** - the descent is bounded at 10000 open frames.  Measured at that
  limit the deepest accepted input uses 0.3 to 2.3 MB of the 8 MB default
  stack, and the deepest nesting accepted is 2499 levels for template argument
  lists, 3331 for parenthesized expressions, 4997 for unary operators and about
  10000 for the rest.  Annex B recommends supporting 256.

## Architecture Review

The recognizer is a backtracking recursive descent over a flat terminal array.
Three properties carry the whole design, and the audit checked each end to end:

- **One owner per fact.**  A terminal and its lexical facts are decided once in
  `parse_token.cpp`; bracket state lives only in `ParseCursor`; the depth bound
  lives only in `ParseDepth`; the remembered results live only in `MemoTable`.
  No rule re-derives any of them.
- **Interface discipline.**  A rule that succeeds leaves the cursor one past
  its last token; a rule that fails leaves position *and* angle state as it
  found them, through `Mark`.  Every `open_bracket` is paired with a restore on
  the success path and by `Mark` on the failure path.
- **Every recursion cycle is counted.**  The call graph of the six recognizer
  files has exactly six cycles that do not pass a remembered rule
  (`balanced-token`, the template-parameter list, the two declarator cycles,
  the cast/unary/delete cycle, and the bounded binary-operator cascade).  All
  but the bounded one open a `Frame`.

## Final Architecture Review

The audit reconstructed the architecture from the sources rather than from the
checkpoint, and used two oracles the checkpoint did not:

- **Static cycle analysis.**  The recognizer call graph was extracted and its
  strongly connected components computed with every memoized rule removed.
  That found the five unguarded recursion cycles below; before the fix each of
  them segfaulted on a deeply nested input instead of reporting `BAD`.
- **Grammar-driven generation.**  A derivation generator for `pa6.gram`
  (restricted where the assignment adds semantics the grammar file does not
  carry: mock name lookup, the decl-specifier-seq rule, and 14.2.3) produced
  20000 sentences covering 196 of the 198 nonterminals, and a shrinker reduced
  every rejected sentence to a minimal derivation by pruning only optional and
  repeated parts.  The rejection rate went from 622/20000 to 227/20000.

`recog-ref` is **not** usable as an oracle outside the checked-in fixtures: it
recovers from a failed declaration by skipping to the next `;`, so it answers
`OK` for `int +;`, `+ ;` and `int [];`.  Its `BAD` answers are informative, its
`OK` answers are not.  Every finding below was adjudicated against `pa6.gram`,
the handout and N3485 instead.

Two limitations remain, both instances of one structural property: **a
specifier sequence commits to its longest reading, and the declaration around
it cannot ask for a shorter one.**  A shorter reading is offered wherever the
choice is local (`simple-type-specifier`, `elaborated-type-specifier`,
`typename-specifier` all try each qualification prefix), but not across the
specifier-sequence boundary.  So `union T1<>::a1 -> long = default;`, where the
`T1<>::` belongs to the declarator-id rather than the type, and the same shape
after a `typename-specifier`, are refused.  Lifting it would require a rule to
remember several end positions per position rather than one, which is a change
to the memo contract that the assignment does not need; PA10 replaces this
recognizer with a typed parser and is the right place to revisit it.

Two other shapes the generator produced are refused on purpose: `new char * b`
(5.3.4/3 takes the *new-declarator* to be as long as possible, so the `*` binds
to the type) and `a > b` unparenthesized inside `<>` (14.2.3, as the handout's
own `TC1< 1>2 >` example shows).

## Findings

| # | Finding | Verdict |
| --- | --- | --- |
| F1 | Five recursion cycles were not counted by the depth guard, which only counted memoized rules: `balanced-token`, `ptr-declarator`, `ptr-abstract-declarator`, `template-parameter-list` and `cast`/`unary`/`delete`.  Each segfaulted rather than reporting `BAD` (measured: 48000 to 200000 levels of nesting, all with plausible C++ shapes such as `!!!...!x`, `(int)(int)...x`, `[[a(((...)))]]`, `int ((((x))))`). | fixed |
| F2 | The depth limit itself was unsafe: at 20000 memoized frames a nested parenthesized expression needed 6247 KB of the 8 MB default stack, so the guard was 24% away from the crash it exists to prevent, not the "well under a third" the plan claimed. | fixed |
| F3 | A depth refusal was remembered as an ordinary failure, so a rule refused near the limit could poison the memo for a shallower attempt at the same position. | fixed |
| F4 | `alignment-specifier` tried `type-id` and then `assignment-expression` with `&&`, so a type-id matching a proper prefix hid the expression: `alignas(C1+1)` was `BAD`. | fixed |
| F5 | `simple-type-specifier` and `class-or-decltype` returned on `KW_DECLTYPE` before the nested-name-specifier path, so `decltype(x)::Y1` and `struct C2 : decltype(x)::C1` were `BAD`. | fixed |
| F6 | `template-parameter` used the function-parameter FOLLOW set, which contains `...`; a `type-parameter` therefore swallowed `class T1` out of `template<class T1...>`, where the `...` makes it a parameter-declaration with an abstract-pack-declarator. | fixed |
| F7 | A type-parameter's default argument tried `type-id` then `id-expression` with `&&`, so `template<template<class> class = E1::a1>` was `BAD`. | fixed |
| F8 | `enum-base` took a defining type-specifier greedily, so the `{` of `enum a1 : enum a1 { }` was read as a nested enumeration's body rather than the enumeration's own. | fixed |
| F9 | `typename-specifier`, `elaborated-type-specifier` and `simple-type-specifier` used only the greedy `nested-name-specifier`, so a name that belongs to the declarator after the type made the whole specifier fail (`alignas(void -> typename ::a1::*)`, `::E1::a1 -> const int = delete;`). | fixed |
| F10 | `declaration` and `member-declaration` committed to `function-definition` without checking that a declaration can follow it, so `float f1 -> long { }, f2 -> float;` (a declarator list with a braced initializer) was `BAD`. | fixed |
| F11 | The handout's decl-specifier rule is about a *type-name*, but the implementation also refused a second class, enum, elaborated or typename specifier.  None of those can be mistaken for a declarator, so holding them out only lost sentences (`catch (enum a1 class { })`). | fixed |
| F12 | The memo was a node-based `unordered_map`; `memoize` plus its hashtable insert were 32% of the run on real source, and two of the seventeen remembered rules were 39% of the traffic while saving nothing. | fixed |
| L1 | A specifier sequence commits to its longest reading and the declaration cannot ask for a shorter one, so `union T1<>::a1 -> long = default;` is refused. | left, recorded above |

## Changes

- `memo_table.h/.cpp`: `MemoTable`, an open-addressed table replacing the
  node-based `unordered_map`.
- `parse_depth.h`: `ParseDepth` and its `Frame`, the descent bound, owned apart
  from the recognizer because the rule it enforces is about the machine stack
  rather than about the grammar.
- `recognizer.h`: builds on both; `at_template_parameter_end`; two rules
  dropped from `MemoRule`.
- `recognizer.cpp`: `memoize` uses `Frame` and never remembers a depth-limited
  result; `recognize` reports overflow; `parse_balanced_tokens` opens a
  `Frame`; `parse_attribute_specifier_seq` is no longer remembered;
  `parse_alignment_specifier` FOLLOW-checks its two alternatives;
  `parse_declaration_body` FOLLOW-checks `function-definition`.
- `recognizer_name.cpp`: `nested_name_specifier_ends`;
  `parse_typename_specifier` and `parse_elaborated_type_specifier` try each
  qualification prefix; `parse_unqualified_id` is no longer remembered.
- `recognizer_expression.cpp`: `parse_unary_expression` and
  `parse_cast_expression` open a `Frame`.
- `recognizer_declarator.cpp`: `parse_ptr_declarator` and
  `parse_ptr_abstract_declarator` open a `Frame`; `parse_simple_type_specifier`
  tries each qualification prefix and keeps `decltype-specifier` last; the
  decl-specifier rule applies to type-names only.
- `recognizer_member.cpp`: `parse_template_parameter_list` opens a `Frame`;
  `parse_template_parameter` uses the template FOLLOW set for both
  alternatives; `parse_type_parameter` FOLLOW-checks its default argument;
  `parse_enum_base` takes the closer it must reach; `parse_class_or_decltype`
  offers the qualified form first; `parse_member_declaration` FOLLOW-checks
  `function-definition`.
- `cppgm.tests/course/pa6/800-*`: six regression tests, one per fixed
  recognition finding, with reference fixtures regenerated through
  `make -C pa6 ref-test`.

## Performance Evidence

| Workload | Before | After |
| --- | --- | --- |
| 16.3 MB of representative C++ | 3.98 s / 74 MB | 2.02 s / 74 MB |
| 8.2 MB of the same | 1.94 s / 39 MB | 1.14 s / 41 MB |
| 2.0 MB of the same | 0.51 s / 20 MB | 0.30 s / 26 MB |
| phases 1-7 vs recognizer, 8.2 MB | 0.72 s vs 1.51 s | 0.70 s vs 0.44 s |

Scaling is linear in every shape measured, at 4000 / 16000 / 64000 repetitions:
declarations, functions, statements, class members, parameter lists, template
argument lists, function-pointer declarators, lambdas, `new` expressions,
attribute runs, `operator` declarations, conditional chains, ambiguous
`C1 * a * b` chains, and inputs that fail at the end of a long prefix.  Two
attribute-heavy shapes were added because dropping the attribute memo is only
safe if re-parsing an attribute run stays cheap: 32000 attributes on one
declaration is 0.03 s, and 32000 attributed declarations is 0.15 s.  Five more
were added because trying every qualification prefix bypasses the
nested-name-specifier memo: `decltype`, `N1::C1::Y1`, `N1::C1::TC1<int>::Y1`,
`typename N1::C1::Y1` and elaborated specifiers are all linear, at 0.15 s to
0.30 s for 64000 declarations.

Depth is bounded in every recursion cycle.  At the limit the peak stack is
328 KB (cast chains), 481 KB (unary chains), 483 KB (balanced tokens), 638 KB
(`sizeof` chains), 794 KB (parenthesized declarators), 796 KB (parenthesized
declarator ids), 948 KB (abstract declarators), 1524 KB (parenthesized
expressions), 1573 KB (template parameter lists), 1886 KB (compound
statements), 2048 KB (braced initializers) and 2277 KB (template nesting) --
against 6247 KB before for parenthesized expressions alone, on an 8 MB default
stack.  Inputs of 200000 nesting levels in each of those twelve shapes now
report `BAD`; before, five of them dumped core.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src`: pass.
- `make test-report-through-pa6`: 319/319, PA1 through PA6.
- 20000 generated derivations of `pa6.gram` covering 196 of 198 nonterminals:
  227 refused, all reduced to the two recorded limitations plus the two shapes
  refused on purpose.
- Deep-nesting witnesses in eleven recursion shapes: no crash, stack bounded.

## Completed Checkpoints

| # | Checkpoint | Result |
| --- | --- | --- |
| CP1 | Whole `recog`: PA6 terminal stream (`parse_token`), token cursor with the 14.2.3 angle state (`parse_cursor`), and the full `pa6.gram` recursive-descent recognizer over six modules, with angle commit, FOLLOW-checked alternatives and a `(rule, position, angle)` memo | 0/47 -> 47/47 pa6; 313/313 through pa6; file audit clean; scaling witness measured |
| CP2 | PA-wide audit: every recursion cycle bounded and the bound given its own owner (F1-F3), eight recognition findings from grammar-driven generation (F4-F11), and the memo replaced by an open-addressed table in its own module with two rules dropped (F12) | 319/319 through pa6; file audit clean, no warnings; 16.3 MB in 2.02 s, 2.0x faster; generated-sentence rejections 622 -> 227 |

## Active Checkpoint

None: PA6 is complete and audited.  Next work on this stage is only whatever a
later PA needs from the parse, which will replace the recognizer's `bool`
results with typed syntax facts and is where limitation L1 should be revisited.
