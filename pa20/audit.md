# PA20 Audit — `cppgm++ --emit-lowir` compile-time metaprogramming

A review of each landed checkpoint, in the order a fact travels: the place a
head declares, the spelling an argument arrives as, the value it converts to,
the specialization it names, and the definition a program wrote for one.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| C1, C2 | `0cda3f77` | 6 / 6 + 1 perf | **the spelling a value argument arrives as, which C1 widened and no reader of one was told.**  Making an argument a *value* let 5.9's `<` and 5.8's `<<` into a name, and all three scans that split a spelling counted every `<` as opening 14.2's list - so `b<(1<2)>::n` found no `::` and `Box<0 < 1, int>` no `,`; 3.4.3p1's rooted name was read by an exit of its own that took no argument list with it; 4.12p1 was missing from the conversion 14.3.2p5 makes the argument a *converted* constant by, so `template<bool>` had one specialization for 3 and another for `true`; and 5.2.3's functional notation and 8.5p16's direct initialization - the other spellings of the cast and the constant object this milestone already folds - folded nowhere |
| C3 | `603dcb83` | 4 / 4 | **the two kinds of settled pack, and the list the object file writes for either.**  A pack is a *run an argument list bound* or *the places one expansion of a function parameter pack declared*, and each of the four readings that meets one knew a different subset: the spelling reading walked the elements of a type that holds none and crashed; one reading of a pattern replaced the pack with an element, so a nested expansion and `sizeof...` inside that pattern found no pack at all; a run of no elements declared no place and so declared nothing, leaving `sizeof...(args)` naming nothing in `f()`; and the ABI, handed the flattened argument list 14.4p1 keys the tier by, wrote `f<int,int>(int,int)` as `_Z1fIiiEiv` - the arguments unpacked and the parameter list *void* - where g++ and the reference both write `_Z1fIJiiEEiDpT_` |
| C4 | `fe28ba9d` | 4 / 4 | **the list a use of a function template is chosen from, and the one the object file writes.**  C4 taught a template-argument-list to end in a run and left every list beside it on the old rule: 14.8.2.2's target type paired a *parameter* list one for one, so `int (*)(int, char)` deduced nothing from `f(Ts...)`; 14.5.6.1p5's signature stood a pack place for the same thing as a single place, so `f(T)` and `f(Ts...)` were one declaration - "defined twice" - and 14.8.2.4p9 had never had to order the two; 14.1p9's default at a *value* place was read where its type-id twin is, and a constant expression is evaluated where it stands rather than substituted afterwards, so `int N = sizeof(T)` and `int B = A + 1` named nothing; and the object file wrote an unsettled non-type argument as a type, `1SIT_E` where g++ and the reference both write `1SIXT_EE` |
| C5 | `818dfd9f` | 2 / 2 | **a pattern this milestone could not read, which the reading dropped and the program was never told about.**  C5 gave an argument list a *second body* it may be read from and left three exits where that body goes unrecorded - a head declaring a template template parameter, a pattern whose reading threw, and a place the pattern does not deduce - each of which leaves the primary's body answering instead, which is a different program read silently: `s<C<T> >` over a template template parameter computed the primary's 0 where g++ and the reference both compute 1, `s<T[N]>` answered 0 for `s<int[3]>` where both compute 3, and `template<class T, class U> struct s<T>` was accepted where both refuse.  And 14.5.1p1's specialization *is* the constant its initializer evaluates to, so nothing is held while that initializer is read: one whose initializer names it ran until the machine stack ran out, where both oracles diagnose it |
| C6 | `d7c036b5` | 4 / 4 | **the demand a prefix makes, which C6 answered at one of the three walks that make it.**  3.4.3p1 looks a name up *in* the region its prefix named, and this compiler writes that walk three times - `resolve_prefix` for a prefix spelled as a name, its own decltype branch, and `qualified_in_type` for a prefix that is a *type*.  C6 taught the first and left the other two on the demand 14.6p8's reading answers with nothing, so `decltype(make_it())::type` written in a template definition reached a class with no region and was refused where both oracles accept, and a decltype prefix an argument list has yet to settle threw `no declaration of decltype(...) is in scope` instead of leaving 14.6.2p1's stand-in every other prefix is left with.  The arena C6 made the analyzer borrow reached one of the three modes that read such a name, so `--emit-types` and `--emit-semantics` refused what `--emit-lowir` accepts.  And 14.6.1p1's current instantiation puts a *place* at every argument, so an out-of-class member definition bound a value place as a type: `template<class T, int N> int holder<T, N>::value = N * 2;` - every out-of-class definition of a member of a class template with a non-type parameter - was refused at 5.1.1p8 |
| C7 | `350c92f4` | 3 / 3 | **a list of *one* entry is a list too, and five readers took that entry by index.**  C7 gave every walk of a written list one reading and converted the readers that walk *many* entries; the readers that take the list's single entry kept indexing `children[0]`, so `int x(a...)`, `int(a...)` and `: v(a...)` over a run of one each reached a `pack-expansion-expression` no reader below answers for and were refused where both oracles accept - while their class-typed twins, which C7 did convert, were right.  8.5p16's arity was never asked either, so `int x(1,2)` was accepted where the reference and g++ both refuse.  And 5.19's own copy of 5.2.3 had no answer at all for a run 14.6p8's reading cannot count, so `int arr[int(N...)]` written in a template definition was refused where `sizeof...(N)` in the same place stands a value in |
| C8 | `8dfad19a` | 5 / 5 | **the dialect a character-literal is read in, which covers two facts and was moved for one.**  What a c-char above the ordinary range is worth, and whether a run of them is a literal at all, are one question PA2's dump and the language answer differently - the reference splits them the same way, its `#if` reading `'é'` as 233 where its phase 7 reads -23 - so `MulticharacterLiterals` was the right shape and reached one of the two: every ordinary character literal above 127 stayed PA2's `int` holding a code point, so `sizeof('\xff')` was 4 here and 1 in the reference and g++ alike.  The packing was a second implementation of 2.14.5p5's execution encoding beside the one `string_literal.cpp` has owned since PA2, and it packed code *points*: `'aé'` was `0x61e9` where both oracles write `0x61c3a9`, and the acceptance set was wrong with it.  A `char` literal may now hold a negative value, which found `literal_value` shifting a literal's bytes together without 3.9.1p1's sign, so `long long g = '\xff'` wrote 255.  C8's own claim that one reading answers a tree and a spelling alike was untrue for 5.1.1p6's parentheses, which the tree strips and the spelling did not, so `p<("abc")[1]>` was refused where both oracles accept.  And 2.14.8p3's raw operator was asked before the literal operator template, where the reference calls the template |

## Current Checkpoint Review

C8 read 5.19 outside the integral subset by giving each of its operands to the
reader it belongs to: 2.14.3p1's multicharacter literal to `PostTokenizer`
through `MulticharacterLiterals`, 5.19p2's subobject of a string literal to
`string_element`, 5.3.3p5's `sizeof...` in an argument spelling to the same
`PackReading` a tree of it asks, 3.9p7's incomplete array to the definition of
the object, and 2.14.8p3's literal operator template to `specialize` over the
characters the program wrote.  7.2's enumeration became `sema_enum.cpp`, which
is a move: the only difference is a file-local `counted` where the analyzer's
own `decimal` stood, which is the idiom every other owner in this tree already
follows.

Three of those are right where they stand.  `sizeof...` out of a spelling is
the one `PackReading` and nothing else, so a pattern read per element counts
the run it came from at both tiers and inside and outside 14.6p8's reading.
The bound both ways about is 8.3.4p3's declaration first and the list second,
and the two shapes it answers differently from an oracle - a definition that
omits a bound an earlier declaration wrote, and a redeclaration whose element
type differs - are 3.3's matching of two declarations rather than this
clause, which is why they stay recorded.  And `string_element` is one reading
for a spelling and a tree, with 2.14.5p12's terminating null an element like
any other.

What the review found is that the *reader's* half of the checkpoint was
short of the fact it names.  Which reading of a character literal a layer asks
for settles two things and C8 moved one of them; behind that, the value it
moved was a second implementation of an encoding this tree already owned; and
behind *that*, the literal it now writes reaches a byte reader that never had
to be signed.  Two more stand beside them: the parentheses one of C8's two
readings strips and the other does not, and the order 2.14.8p3's two
fallbacks are asked in.

### Findings

**1. The dialect covers two facts and the parameter reached one.**  PA2's dump
is course defined to hold a character literal's *code point*, so an ordinary
one above 127 is an `int` there; the language gives every ordinary character
literal type `char`.  C8 made the run-of-c-chars half a fact of the reader and
left the single-c-char half reading PA2's rule inside the compiler:

| shape | before | `reference-binaries/cppgm++` | g++ |
| --- | --- | --- | --- |
| `sizeof('\xff')` | 4 | 1 | 1 |
| `'\xff'` | 255 | -1 | -1 |
| `'\377'`, `'\u00ff'` | 255 | -1 | -1 |
| `'é'` | 233, width 4 | -23, width 1 | 0xc3a9, width 4 |
| `sizeof('a')`, `'a'` | 1, 97 | 1, 97 | 1, 97 |

The parameter is the right shape - the reference splits the same question the
same way, and its own preprocessor answers `#if 'é' == 233` where its phase 7
answers -23, which is exactly what `ctrl_expr.cpp` already asks for - so the
fix is its reach and not its existence.  It is named `CharacterLiterals`
(`CourseSubset`, `Language`) now, because a reader that takes it is choosing
a reading of the whole literal and not of one spelling of it.

**2. The packing was a second implementation of 2.14.5p5's encoding.**
`packed_multicharacter` packed each c-char as `code point & 0xFF`, while
`string_literal.cpp`'s `encode_narrow_part` has since PA2 read a numeric
escape as one code unit and every other element as the UTF-8 its code point
comes to.  So one c-char was one byte inside `'...'` and two inside `"..."`,
and `'aé'` was `0x61e9` where the reference and g++ both write `0x61c3a9` -
`'\x41\xe9'` agreed only because a numeric escape is one byte under either
reading.  The rule is one implementation now (`append_ordinary_units`), which
both bodies call.

With the encoding right the acceptance set follows it: the reference reads the
first c-char as a whole literal before it counts the rest, so it refuses a
multicharacter literal whose first c-char is above the ordinary range.
`'\xff\x41'`, `'\xe9\x41'`, `'\x80\x41'`, `'\xc3\xa9'`, `'\341\x41'`
and `'éé'` are all refused there and were all taken here.

**3. One reader of a literal's bytes had no sign.**  `literal_value` built the
constant by shifting `PostToken::data` together as unsigned bytes, which no
literal could reach while every character literal above 127 was an `int`
holding a code point and every integer literal was non-negative.  With finding
1 landed, `long long g = '\xff';` wrote 255 where the reference writes -1.  It
asks `PostToken::integer_value` now, which is 3.9.1p1's value in the literal's
own type.  The four other readers of the same bytes were checked and are
right: `literal_constant` and `string_element` follow with 4.7p2's conversion,
`LiteralValue::integer` sign-extends of its own, and `__strlit__`'s data items
are 2.14.5's unsigned code units by intent - `const char* p = "\xfe"` writes
the reference's global byte for byte.

**4. 5.1.1p6's parentheses are stripped by one of the two readings.**  C8's
claim is that `string_element` answers a subscript written as a tree and one
written inside an argument list alike, and the tree walk strips
`ParenthesizedExpression` before it asks what is subscripted where the
spelling reader only reached a literal word written bare.  So `("abc")[1]` was
read in a tree and `p<("abc")[1]>` was `a constant expression holds a literal
that has no integral value`, which both oracles accept.  One `literal_operand`
answers for both spellings of the operand now, and the parenthesized primary
reaches it however many parentheses stand around it.

**5. 2.14.8p3's two fallbacks were asked in the wrong order.**  A ud-suffix
whose lookup found no operator taking the value fell to the raw operator and
only then to the literal operator template, so a set declaring both called the
raw one and wrote `@__strlit__1` and a `const char*` call where the reference
calls `operator""_c<'7'>()` and writes `_Zli2_cIJLc55EEEiv`.  2.14.8p3 makes
such a set ill-formed - g++ refuses it - so no well-formed program can tell
the order apart, and asking the template first is what the reference answers
for the ill-formed one.

### What the review confirmed rather than found

The typed ownership holds.  `CharacterLiterals` is a value the reader is
constructed with and holds for its life, so no layer below `PostTokenizer` has
to be told which dialect it is in; `append_ordinary_units` appends into the
caller's `std::string` and owns nothing; `string_element` takes a spelling and
an index and returns a `Constant` by value; and `literal_operator_template`
returns a `SemaEntity*` the model owns, which `specialize` is the only writer
of.  The three drivers that print a token and 16.1's controlling expression
ask for `CourseSubset` and the three that feed the compiler ask for
`Language`, which is every construction of `PostTokenizer` in the tree.

The complexity is what the plan claims and the fixes cost nothing measurable.
A character literal is one scan of the c-chars phase 3 already found however
it is read, and the code units it comes to are appended into one string per
literal: 512 / 2048 / 8192 distinct multicharacter literals are 0.008 / 0.018 /
0.058 s against the `8dfad19a` build's 0.008 / 0.020 / 0.058, and the same
count holding a c-char above the ordinary range is 0.025 / 0.044 / 0.120
against 0.024 / 0.043 / 0.120.  The shared encoder is unmoved for the body it
already owned - 512 / 2048 / 8192 string literals carrying escapes are 0.015 /
0.053 / 0.228 against 0.015 / 0.053 / 0.229.  The parenthesized primary is one
index comparison per parenthesized run: 256 / 1024 / 4096 parenthesized
argument spellings that hold no literal are 0.009 / 0.023 / 0.085 s on both
builds, and the shape the finding is about is 0.010 / 0.029 / 0.109 - linear,
and 1.3x the 0.083 s the same count written without parentheses costs, which
is the cast probe those parentheses already paid for.  Asking the template
first costs a raw ud-literal one scan of its own candidate list: 256 / 1024 /
4096 raw ud-literals are 0.011 / 0.033 / 0.134 against 0.011 / 0.033 / 0.135,
and the same count of template ones 0.017 / 0.057 / 0.255 against 0.016 /
0.057 / 0.257.  `fac<800>` and a pack of 4096 bound and counted are 0.037 and
0.011 s on both builds.

Valgrind is clean - no message of any kind - over the nine shapes the findings
are about and the two fixtures added here.

The differential sweep is 112 shapes: 88 through this compiler, through
`reference-binaries/cppgm++` and through g++, compared on the LowIR the first
two wrote rather than on the exit status alone, and 24 more character-literal
spellings compared for the value *and* the width the two compilers give them.
The literal shapes are the cross product of one c-char and two, three, five
and eight of them, against a source character, a universal-character-name, a
hex escape, an octal escape and a simple escape, at each of the four
encodings, inside and outside an argument spelling and inside `#if`; the rest
are the twelve string-element shapes, seven `sizeof...` spellings, twelve
array-bound shapes and fourteen ud-literal shapes.  Every accepted pair now
writes byte-identical LowIR but the two recorded below, and the PA2 dump of
`'\xff'` and `'é'` is still `pa2/posttoken-ref`'s byte for byte.

### Recorded, not landed

- **The two oracles disagree on a single non-ASCII c-char.**  `'é'` is a
  `char` holding -23 in the reference and a multicharacter `int` holding
  `0xc3a9` in g++, because g++ counts the code *units* of the source character
  as c-chars where the reference counts the character.  This compiler answers
  the reference, which is what the object file is compared against, so the
  fixture added here pins the rule through `'\xff'` and `'\377'` - where the
  two oracles agree - rather than through a source character.
- **The reference drops a ud-suffix on a character literal.**  `'a'_c` is 97
  there whatever `operator""_c(char)` returns, so it folds the call away;
  this compiler and g++ both call the operator.  Reproducing it would be
  wrong, so the LowIR differs for that one shape.
- **8.5.2p1's string literal does not initialize a character array.**
  `char s[3] = "ab"` and `char s[] = "ab"` are `an expression has no
  conversion to the type it initialises` here, at namespace and block scope
  alike, where both oracles accept - while the same literal written as an
  aggregate's clause (`struct k { char s[4]; }; k v = { "abc" };`) is right.
  That is a PA12-era initialization this checkpoint does not reach; it is also
  what would make C8's braced deduction count `char s[] = {"ab"}` as three
  elements rather than the one clause it holds.
- **PA20's own recorded items are unchanged**: a specialization's body cannot
  name its own class, a partial specialization has no out-of-class members, a
  template template parameter in any head, a dependent array bound in an
  argument spelling, a constructor template written in a class body, 12.1's
  constructor over a function parameter pack of no elements, the reference's
  empty-pack function-template name, 14.8.1p9's extension of an explicit list,
  `Tn` for a settled value argument of dependent type, the generated place
  name that collides with a written one, a pack name written without `...`,
  10p1 over a base pack of more than one element, the static data member's
  demand, `1["abc"]` refused by the reference and this compiler alike, and the
  reference's refusal of a braced scalar initializer holding an expansion.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double, the out-of-class member path's
  residual, 12.1's two constructor entry points, and the ABI's decltype return
  type.  A *class* metafunction with no terminating specialization still
  overflows the machine stack rather than being diagnosed, in this compiler
  and in the reference alike; it needs a depth guard rather than C5's
  same-list one.

## Changes

- **`literal_scan.h` / `literal_scan.cpp` — the dialect, and one encoder.**
  `MulticharacterLiterals` became `CharacterLiterals` (`CourseSubset`,
  `Language`), because what it settles is the whole reading of a character
  literal: in `Language` an ordinary one holding a single c-char is a `char`
  whatever that c-char is, and in `CourseSubset` it is PA2's `int` holding the
  code point.  `append_ordinary_units` is 2.14.5p5's encoding of one element -
  a numeric escape is one code unit, everything else is the UTF-8 of its code
  point - and `packed_multicharacter` builds the run through it and refuses a
  first c-char above the ordinary range, as the reference does.
- **`string_literal.cpp` — the body that already owned the rule.**
  `encode_narrow_part` calls `append_ordinary_units` rather than writing the
  same two branches a second time, so a string literal's body and a character
  literal's cannot drift apart again.
- **`sema_expression.cpp` — 3.9.1p1's sign, and 2.14.8p3's order.**
  `literal_value` reads the token through `PostToken::integer_value`, which is
  the literal's value in its own type, so a `char` holding a high bit is
  negative where 4.7p2 widens it.  The literal operator template is asked
  before the raw operator, which no well-formed program can tell apart and
  which is what the reference answers for the set 2.14.8p3 makes ill-formed.
- **`sema_value_expression.cpp` — 5.1.1p6's primary.**  The parenthesized run
  the reader could not read as a cast is stripped to the primary it holds, so
  a literal reaches 5.19p2's subscript through however many parentheses stand
  around it; `literal_operand` is that reading, and it is the one both
  spellings of the operand ask.
- **Two fixtures** under `cppgm.tests/course/pa20`, each with a `.ref`
  generated from `reference-binaries/cppgm++`, each accepted by g++ and each
  refused by a `make build` of `8dfad19a`: what a c-char above the ordinary
  range is worth, as one c-char and in a run of them and read back out of a
  string literal; and the literal inside the parentheses around it, in a tree
  and inside an argument spelling.

## Performance Evidence

Best of five over three interleaved rounds, `-O0`, timed by the shell around
the process itself: an empty translation unit is **0.004 s**, so a row is the
shape's own cost.  Every row was measured against this build and against a
`make build` of `8dfad19a` on the same machine.

| shape | here | `8dfad19a` |
| --- | --- | --- |
| 512 / 2048 / 8192 distinct multicharacter literals | 0.008 / 0.018 / **0.058 s** | 0.008 / 0.020 / 0.058 s |
| the same holding a c-char above the ordinary range | 0.025 / 0.044 / **0.120 s** | 0.024 / 0.043 / 0.120 s |
| 512 / 2048 / 8192 string literals carrying escapes | 0.015 / 0.053 / **0.228 s** | 0.015 / 0.053 / 0.229 s |
| 256 / 1024 / 4096 parenthesized argument spellings | 0.009 / 0.023 / **0.085 s** | 0.009 / 0.023 / 0.085 s |
| 256 / 1024 / 4096 string elements read through parentheses | 0.010 / 0.029 / **0.109 s** | refused |
| the same written without parentheses | **0.083 s** at 4096 | 0.083 s |
| a literal parenthesized 24 deep in a spelling | **0.004 s** | refused |
| 256 / 1024 / 4096 raw ud-literals | 0.011 / 0.033 / **0.134 s** | 0.011 / 0.033 / 0.135 s |
| 256 / 1024 / 4096 literal-operator-template ud-literals | 0.017 / 0.057 / **0.255 s** | 0.016 / 0.057 / 0.257 s |
| `fac<800>` metafunction chain | **0.037 s** | 0.037 s |
| a pack of 4096 elements bound and counted | **0.011 s** | 0.011 s |

Every row this review touched is linear in its own multiplicity and unmoved
from the build that read the literals the other way, which is what says the
encoding is one pass over the c-chars phase 3 already found and not a second
scan of them.  The one row that moved is the shape the old build refused, so
its `8dfad19a` column is the cost of failing rather than a comparison; the
control beside it - the same count of parenthesized spellings holding no
literal - is identical on both builds, which is what says the primary probe
costs nothing and the 1.3x is the subscript that now happens.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **178 / 193**, from a
  turn-start **176 / 191** - the two added here pass and the 15 failing at
  turn start are the same 15, name for name.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over the nine finding shapes and the two added fixtures.
- Every `.ref` under `cppgm.tests/course/pa20` and under `pa20/tests` was
  regenerated from `reference-binaries/cppgm++`; all 191 that were already
  there are byte-identical.
- `dev/posttoken` still writes `pa2/posttoken-ref`'s dump for `'\xff'` and
  `'é'`, and the reference's own `#if` dialect is what `ctrl_expr.cpp` asks
  for, so widening the compiler's reading left PA2's and 16.1's alone.
- Both added fixtures are refused by a `make build` of `8dfad19a` and accepted
  by g++, so each is a test of this review rather than of its own output.
