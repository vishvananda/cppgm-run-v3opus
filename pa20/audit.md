# PA20 Audit — `cppgm++ --emit-lowir` compile-time metaprogramming

A review of each landed checkpoint, in the order a fact travels: the place a
head declares, the spelling an argument arrives as, the value it converts to,
the specialization it names, and the definition a program wrote for one.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| C1, C2 | `0cda3f77` | 6 / 6 + 1 perf | **the spelling a value argument arrives as, which C1 widened and no reader of one was told.**  Making an argument a *value* let 5.9's `<` and 5.8's `<<` into a name, and all three scans that split a spelling counted every `<` as opening 14.2's list - so `b<(1<2)>::n` found no `::` and `Box<0 < 1, int>` no `,`; 3.4.3p1's rooted name was read by an exit of its own that took no argument list with it; 4.12p1 was missing from the conversion 14.3.2p5 makes the argument a *converted* constant by, so `template<bool>` had one specialization for 3 and another for `true`; and 5.2.3's functional notation and 8.5p16's direct initialization - the other spellings of the cast and the constant object this milestone already folds - folded nowhere |

## Current Checkpoint Review

C1 gave PA19's tier a second kind of argument and C2 gave the tier the
definition a program writes for one argument list.  The tier's own shape is
sound and was traced end to end: a value argument is a `TypeKind::Value` entry
interned by `(type, bits)`, so the specialization table, `substitute`, the dump
and the ABI encoder each read an argument list without knowing which kind of
argument it holds; every switch that reaches a `default` refuses a `Value` where
a real type belongs; `open_parameter_region` reads one head's syntax once
however many argument lists bind it; and 14.7.3p1's explicit specialization is
keyed by the interned argument list the specialization is already found by, so
an instantiation asks one hash lookup on a number the caller has.

What the review found is on the other side of that boundary: the *spelling*.
C1's own design note is that 5.19 is read out of the argument spelling the way
8.1p1's type-id is - and it widened what a spelling may hold without telling the
three scans that split one, without giving the value reader the exits the
type-id reader already had, and without giving the folding it depends on the two
conversions and two initializer forms that write the same constant.

### Findings

**1. A `<` written inside a template-argument-list was always read as opening
another one.**  `outside_brackets` - which `QualifiedName` splits `::` with and
`TemplateId` splits `,` with - and the two copies of `balanced_end` each counted
`<` and `>` alone.  14.2 makes only `>` end a list, so 5.9's `<` and 5.8's `<<`
are ordinary operators inside one, and every one of them left the scan one
level deep for the rest of the name:

```cpp
template<int N> struct b { static const int n = N; };
int main() { return b<(1<2)>::n; }      // no declaration of b<(1<2)>::n is in scope
```

`b<(1<<3)>`, `b<(1<=1)>`, `b<(lo<hi)>`, `b<(1<2), 3>` and `w<b<(1<2)> >` were the
same defect, and `Box<0 < 1, int>` - a checked-in fixture - was read as one
argument.  `b<(2>1)>` *worked*, which is what kept it out of sight: the stray
`<` and the stray `>` cancelled wherever the operator happened to be written the
other way round.  It is one rule and now has one implementation: 5.1.1p6's
parentheses and 5.2.1p1's subscript hold 5's whole expression grammar and are
stepped over whole, a `<` opens a list only after a name 2.11p1 leaves not
opening with a digit, and a run that would have to close outside the group it
stands in never opened.  `sema_type_id.cpp` and `sema_value_expression.cpp` had a
copy of the scan each and now ask `sema_name.cpp`.

**2. 3.4.3p1's rooted name was a sibling exit in one reader and missing from the
other.**  `split_type_id` read `::x` with a scan of its own that took no
template-argument-list with it, so `trait< ::arg<int> >` was "a template
argument written outside the PA12 subset" - a checked-in fixture - while
`holder< ::plain>` worked.  `split_value_expression` had no arm for a rooted
name at all, so `b< ::k>` was "`::` names no constant".  Both now read a rooted
name with the scan that reads every other one.

**3. 14.3.2p5's conversion was 4.7's and not 4.12p1's.**  `convert` masked the
operand to the destination's width, which for `bool` keeps the low *byte*:

```cpp
template<bool B> struct flag { static int get() { return (int)B; } };
int main() { return flag<3>::get(); }   // 3 here, 1 in the reference and in g++
```

So 14.4p1's identity was broken where the clause is most visible - `flag<3>` and
`flag<true>` were two specializations of a two-valued place - and the same
conversion is what `int a[(bool)5]` and 8.5.4's narrowing check ask, which gave
that array five elements.  4.12p1 is a fact of the conversion rather than of the
argument, so it is answered in `convert`, where every reader of it asks.

**4. 5.2.3p1's functional notation folds nowhere, where `(T)x` and
`static_cast<T>(x)` both do.**  `short(42)`, `int(3)`, `E(1)` and 5.2.3p3's
`int{3}` were each "a construct PA11 does not evaluate" - in an array bound, an
enumerator, a constexpr object and a template argument alike - and the reference
folds all of them.  The grammar hands the shape on as a call because it cannot
say whether the name is a type; a callee naming an arithmetic type is 5.4's
cast, and a callee naming a function is 5.19p2's constexpr function and still is
not a constant expression.  It is one rule at the two places C1's design names -
`sema_constant.cpp` over a tree and `sema_value_expression.cpp` over a spelling -
and was landed at both.

**5. 5.19p3 was read from one of its three initializers.**  `const int k = 3` was
a constant and `const int k(3)` and `const int k{4}` were not, because
`declare_object_declarator` evaluated `initializer->children[0]` and 8.5p16's
direct-initialization writes a `ParenInitializer` there and 8.5.4p3 a
`BracedInitList`.  Both write the very expression the third form does.

**6. 14.6p8's stand-in count survived a reading that was thrown away.**
`probe_type_id` is how 5.4p2's ambiguity is settled - a parenthesized spelling
is *asked* whether it is a type-id - and a `sizeof` of a dependent type met
inside a probe that then failed left `stood_in_` raised, which is what
`static_assert_declaration` compares across its condition.  The count is of what
a reading stood in, and a discarded reading stood nothing in.

**7 (performance).**  The shared scan asks four more questions of every
character than the one it replaced, and a name is scanned once per name written
inside it, so a 1024-deep `s<s<...<int> >` spelling paid 18%.  The six
characters a balanced run is written with are one table lookup, so every other
character answers one question rather than six, and 13.5p1's `operator<` is only
compared for after a name ending in `r`.

### What the review confirmed rather than found

The typed ownership holds.  `TypeKind::Value` was swept at every reader a new
type-table kind has - `substitute`, `dependent_walk`, `append_description`,
`key_of`/`intern`, `type_spelling`, `bind_argument` and `LocalContexts::
argument_of` - and each answers it; nothing else reaches one, because the only
producer is `template_argument_value` and the only consumers are an argument
list's readers.  `value_bits` is the `unsigned long long` an array bound already
used, so no argument is truncated, and the interning key carries the kind, the
target and the bits, so `value_type(int, 3)` is one entry and never collides
with `int[3]`.

The complexity is what the plan claims.  `value_words_` keys the *split* by
spelling, `dependent_values_` keys 14.6.2p2's stand-in by spelling, and
`default_arguments_` keys one list of explicit arguments by
`(primary id, interned list)` - a `std::uint32_t` each, so the 64-bit key cannot
collide.  `open_parameter_region` is once per template.  No shape in the
Performance Model regressed, and two the pre-audit build refused now run.

The C2 parse change - a `template<>` head declaring a class-name rather than a
template-name - was swept at its siblings: an explicit instantiation and an
`extern template` written beside a specialization, a specialization declared and
then defined, one written above and below its first use, one in a namespace and
one named through `n::`, a function template's with and without a written
argument list, a member defined outside a specialization's class, a negative
value argument, and a specialization whose class-name is then written as a
declaration.  All fourteen are identical to `reference-binaries/cppgm++`
as canonicalized LowIR.

`reference-binaries/cppgm++` accepts the PA20 slice, so it is a differential
oracle here even though the README says the `.ref` files are the only one.
Every shape below was compiled through this compiler and through it, with g++
as the third oracle wherever the two disagreed.

### Recorded, not landed

- **A static data member's definition is written on a different demand here than
  in the reference, in both directions.**  This compiler writes it where the
  specialization is *completed*; the reference writes it where the
  specialization is *named* in a declaration that declares an entity.  Both are
  wider than 14.7.1p1, which instantiates the definition only for an odr-use,
  and g++ writes none of the ten shapes below.  It is PA19's member-demand model
  rather than this checkpoint's - the `f51f751d` build answers every one of
  these identically - and it is what
  `100-nontype-template-argument-static-member-no-storage` fails on.

  | shape | ref | here | g++ |
  | --- | --- | --- | --- |
  | `holder<int> o;` | writes | writes | - |
  | `holder<int>* p;` | writes | - | - |
  | `holder<int> f();` / `void g(holder<int>*);` | writes | - | - |
  | `struct d : holder<int> {};` | - | writes | - |
  | `struct d : holder<int> {}; d o;` | writes | writes | - |
  | `struct d : holder<int> {}; d::n` folded | - | writes | - |
  | `struct d : holder<int> {}; &d::n` | writes | writes | writes |
  | `typedef holder<int> h;` alone | - | - | - |

- **14.7.3p1's explicit specialization of a member function of a class
  template** - `template<> int box<int>::g() { return 2; }` - is "g is defined
  twice" here and compiles in both other compilers.  `record_explicit_function`
  asks whether the declarator-id's *last* component is a template-id, and this
  form writes the argument list on the prefix.  The README's Assignment Boundary
  names the class and function template forms and not this one, and no fixture
  in either suite writes one.

- **A functional-notation cast spelled with more than one keyword** -
  `b<unsigned long(3)>` - is not read as one, because the split leaves each
  keyword a word of its own and only a single word is probed as a type.  `(T)x`
  and `static_cast<T>(x)` take the multi-word spelling.

- **A relational operator between two *names* with no parentheses around it** -
  `b<lo < hi>` - is read as a nested argument list, because both readings are
  spellings of the same characters and only a lookup tells them apart.  The
  reference has the parse and answers it; every fixture and every operand a
  literal writes takes the landed rule.

- **The 1024-deep nested spelling still costs 0.21 s against 0.19 s** and is
  quadratic in the characters it writes, which is PA19's recorded shape.  The
  residue is the one question the scan now asks at each `<`.

- **PA19's recorded items are unchanged** and still apply: the exponential
  spelling of a specialization whose arguments double, the out-of-class member
  path's residual, 12.1's two constructor entry points, and the ABI's decltype
  return type.

## Changes

- **`sema_name.h`/`.cpp` — one scan for three readers.**
  `opens_template_arguments` answers 14.2's question about a `<` and
  `spelling_balanced_end` the run it opens; `outside_brackets` counts 5.1.1p6's
  group apart from 14.2's list.  `sema_type_id.cpp` and
  `sema_value_expression.cpp` each dropped a copy.
- **`sema_type_id.cpp`, `sema_value_expression.cpp` — 3.4.3p1's rooted name** is
  read by the scan that reads every other name, and `split_type_id`'s separate
  exit for one is gone.
- **`sema_value_expression.cpp` — 5.2.3p1's functional notation** beside the two
  cast spellings the reader already folds, and `probe_type_id` puts 14.6p8's
  count back where its reading failed.
- **`sema_constant.cpp` — 4.12p1's conversion to `bool`**, and 5.2.3p1/p3's
  functional and braced notation as `evaluate`'s `CallExpression` arm.
- **`sema_analyzer.cpp` — 8.5p16 and 8.5.4p3's initializers** read for 5.19p3's
  constant, which was landed for `= x` alone.
- **Five fixtures** under `cppgm.tests/course/pa20`, each refused or
  mis-answered by the `0cda3f77` build, each with a `.ref` generated from
  `reference-binaries/cppgm++`, and each returning 0 under g++.

## Performance Evidence

Best of five, `-O0`, against the `0cda3f77` pre-audit build:

| shape | before | after |
| --- | --- | --- |
| 512 distinct value arguments over two templates (1024 specializations) | 0.09 s | 0.09 s |
| 4096 distinct value arguments over two templates (8192 specializations) | 0.88 s | 0.85 s |
| `fac<200>` / `fac<800>` metafunction chain | 0.01 / 0.06 s | 0.01 / 0.06 s |
| a 2000-deep chain instantiated but not evaluated | 0.09 s | 0.09 s |
| 256- / 1024-deep `s<s<...<int> >` spelling | 0.02 / 0.19 s | 0.02 / **0.21 s** |
| one template-id of 1024 arguments | 0.01 s | 0.01 s |
| 512 arguments each written `(i < n)` | **refused** | 0.02 s |
| 1024 value arguments each written `(i < n)` | **refused** | 0.02 s |
| one argument spelling of 512 `+` operands | 0.00 s | 0.00 s |

The one shape that moves is the 1024-deep spelling, whose cost is quadratic in
the characters it writes and was so before this checkpoint; 256-deep does not
move, and nothing else does.  The two shapes marked refused are the ones finding
1 is about.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **103 / 169**, from a
  turn-start **92 / 164** - six checked-in fixtures newly pass, the five added
  here pass, and no fixture that passed at turn start fails.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over all 98 passing `pa20/tests` fixtures, the five added
  fixtures and thirteen audit shapes, with no finding of any kind.
- **A differential sweep against `reference-binaries/cppgm++`**, in seven
  groups: the declared type of a non-type place over all **15** integral
  spellings; every operator of the 5.19 subset with its precedence and
  associativity, **27** of them; **21** static-data-member demand sites; **14**
  explicit-specialization and parse-sibling shapes; **21** argument-spelling
  shapes over `<`, `::`, nesting and multiplicity; **19** conversion and
  initializer shapes; and **19** further value-argument shapes - argument
  equivalence over five spellings of 3, `sizeof`/`alignof` of a parameter,
  5.14/5.15/5.16's unevaluated operand, static_assert in six positions.  Every
  one is identical as canonicalized LowIR except the demand-site table recorded
  above, and every disagreement there was decided against g++ as well.
