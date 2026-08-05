# PA5 `preproc` Plan

## Stage Design

PA5 completes translation phase 4: the directives PA4 left out, source file
inclusion, and the file-level facts (`__FILE__`, `__LINE__`) they carry.

```
SourceFileTable -> PPTokenLexer (1-3) -> Preprocessor : MacroExpander (4)
    -> PostTokenizer (5-7) -> DebugPostTokenStream(outfile)
```

Ownership:

- `dev/src/source_files.*` owns the file system as the compiler sees it: the
  bytes of each distinct path, its physical line index, and its `stat`
  identity.  Every question about a path is answered once for the run, so a
  header is read, indexed and identified once however many times it is included
  and by however many srcfiles.
- `dev/src/source_reader.*` and `dev/src/pptoken_lexer.*` are phases 1 to 3
  over a borrowed `SourceText`, so an inclusion costs a lex and not a copy.
- `dev/src/macro_expander.*` and `dev/src/macro_model.*` own phase 4 inside one
  file: the 16.3.4 rescan, the interned spellings, the hide sets, the
  text-sequence/directive split, and `#define`/`#undef`.  They leave four seams
  a file-level phase 4 needs and PA4 does not use: `run_directive_line`,
  `pop_source`, `expand_builtin` and `run_text_operator`.
- `dev/src/preprocessor.*` owns what is above one file: the stack of open
  files, the presumed `__FILE__`/`__LINE__` of each, conditional inclusion,
  `#include` search, `#line`, `#pragma`, `#error`, the `_Pragma` operator and
  the predefined macros.
- `dev/src/ctrl_expr.*` keeps owning 16.1; its `defined` oracle is an interface
  so PA5 hands it the real macro table where PA3 hands it a mock.

Typed facts, and where each stops being a string: bytes become translated code
points in `SourceReader`, code points become a `PPToken` (a spelling plus the
byte offset it was spelled at) in phase 3, and a `PPToken` becomes a
`MacroToken` in phase 4 -- 16 bytes of interned spelling, hide set, offset and
kind, which is what makes copying a replacement list a memcpy.  Phase 5 gets a
`PPToken` back and phase 7 a `PostToken`.

`__LINE__` is a per-file presumed line rather than a token field: the byte
offset PA1 already stores, plus the open file's line index and `#line` delta,
answers it exactly.  That holds because an invocation is read from one file and
located against it; the two ways something could reach into one -- a directive
in an argument list, which 16.3/11 leaves undefined, and a file that runs out
inside one -- are refused rather than answered wrongly.

## Performance Model

Dominant operations, in the order they cost: phases 1 to 3 over every byte of
every inclusion, one `intern` per source token, and the rescan's copies.  A
profile of a 7 MB macro-heavy translation unit and of 4000 inclusions of one
header agrees: `SourceReader::fill`, the phase 3 scanners and
`SpellingPool::intern` are 55% to 65% of the run and nothing else is above 5%.

- A distinct path is **read, indexed and `stat`ed once** for the whole run,
  across srcfiles; its bytes have one owner, so an inclusion costs a lex.
- Re-lexing each inclusion is inherent: the macros in force differ.
- `__LINE__` is O(log lines) with no per-token storage; `__FILE__` interns its
  quoted spelling once per presumed name, not once per use.
- An excluded section **skips without building**: its tokens are dropped in
  `fetch`, so it costs phase 3 alone -- no interning, no replacement, no
  directive parse past the name.
- A hide set is the set it extends plus one name, so nesting costs one node per
  level rather than a copy of the level below it.
- The `#if` group stack, the include stack and the pragma-once set are all
  proportional to nesting, not to file size.

Two shapes are quadratic, and both are quadratic in what the semantics ask for
rather than in how they are answered:

- **Nested invocation**, `F(F(...F(1)...))` at depth n.  16.3.1 expands each
  argument before substituting it, and the expanded argument at depth k is
  2(n-k)+1 tokens, so Θ(n²) tokens have to exist.  gcc is quadratic here too.
- **Intersecting the hide sets** of two unrelated deep nestings, which
  materialises both.  The chain representation keeps this off the common path:
  an invocation's head and closing paren carry the same set, so `intersect`
  answers by identity.

## Architecture Review

Reconstructed from the sources rather than from the implementation checkpoint,
and compared with the design above.

- The pipeline is one pull chain with no second path: no fallback lexer, no
  re-parse of emitted text, no place where a phase re-derives a fact an earlier
  one owns.  The only re-lexes are the two the standard asks for -- the
  spelling `##` joins (16.3.3) and the destringized `_Pragma` operand (16.9) --
  and both use the one phase 3 lexer in `Translated` form.
- `#include` had two header-name paths, one before macro replacement and one
  after.  A header-name contains no identifier, so replacing it leaves it
  alone; the two are now one path, which is also where 16.2/4's combined form
  belongs.
- File identity was reached through a static function that bypassed the file
  table, so the one owner of "what the file system says about a path" had two
  doors.  It is now a member of the table, cached with the bytes.
- `Preprocessor` and `MacroExpander` each had a private scratch string named
  `text_`.  The derived one is now `scratch_`.
- Running directives inside an argument list, which PA5 chose to do, breaks the
  invariant `__LINE__` rests on and broke the invocation stack outright; see
  findings 1 and 2.

## Final Architecture Review

- **Ownership is single.**  Bytes, line index and identity: `SourceFileTable`.
  Spellings, hide sets and the macro table: `MacroExpander`.  Presumed
  location, the file stack and every file-level directive: `Preprocessor`.
  16.1: `CtrlExprEvaluator`.  The directive name list is interned once, in
  `MacroSpellings`, because whether an identifier names a directive is one fact
  that both the non-directive rule and the dispatch need.
- **Nothing is computed twice.**  A path is read, indexed and identified once;
  a presumed name is quoted once per `#line`; a macro body is analysed at
  definition and walked at invocation; an excluded section is never interned.
- **No shortcut survives.**  No phase is skipped, no output is embedded, no
  fixture is special-cased, no timeout is worked around, and the `invalid`
  token, `#error` and `#if` error rules are enforced where the assignment puts
  them.
- **Scaling is linear in both axes** on every shape a real translation unit
  has, and the two quadratic shapes are quadratic in the token counts the
  standard defines rather than in the representation.

## Findings

1. **A directive in an argument list corrupted the invocation stack**
   (correctness, fixed).  `expand_function_like` held an `Invocation&` across
   `collect_arguments`, and a directive run from inside the argument list can
   start an invocation of its own, which grows the vector that reference points
   into.  `#if G(...)` inside a call to another macro was a heap-use-after-free
   and **segfaulted**; AddressSanitizer confirms it on the previous build.  The
   invocation is now held by index.  gcc accepts this input and now so does
   this implementation.
2. **An invocation that left its source file was located against the wrong
   one** (correctness, fixed).  `__LINE__` and `__FILE__` are answered by
   indexing the open file with a token's byte offset, which is exact only while
   the two agree.  An `#include` in an argument list, a file that ran out
   inside one, and a `(` the look-ahead reached by leaving a file behind all
   broke that and produced a plausible wrong number.  A source change inside an
   argument list and a `#line` inside one are now errors, and a macro name at
   the end of a file is a name and not an invocation head -- which is what gcc
   and the reference both do.  Covered by
   `cppgm.tests/course/pa5/300-invocation-spans-source-file.t` and
   `300-line-in-argument-list.t`.
3. **`#include` rejected a macro-produced `<...>` header name** (correctness,
   fixed).  16.2/4 has the operand macro replaced and the tokens between a `<`
   and a `>` combined into a header-name in an implementation-defined way.
   `#define HDR <a.h>` then `#include HDR` was an error here and is accepted by
   the reference, gcc and clang.  The combination is now the spellings joined,
   which is what the reference produces for `<a.h>`, for `< a . h >`, and for a
   `<` and a `>` that came from macros of their own.  Covered by
   `300-include-combined-header-name.t`.
4. **Hide sets were quadratic in nesting depth** (performance, fixed).  A set
   was a sorted array hash-consed on a key holding all of it, so a chain of n
   nested macros cost Θ(n²) bytes: 290 MB at n=8000.  A set is now the set it
   extends plus one name -- 7.7 MB for the same input, and 2.3x faster.
5. **`stat` ran per `#include` and read uninitialised memory when it failed**
   (defect and performance, fixed).  The identity of a path is now answered by
   the file table, once per run, and a failed `stat` no longer leaves the out
   parameter holding stack garbage.
6. **`_Pragma` deleted any encoding prefix** (conformance, fixed).  16.9 names
   the `L` prefix and no other, so `_Pragma(u8"once")` was executed here and is
   not by gcc.  Only an `L` prefix is deleted now.  The reference does not
   delete `L` either, so `_Pragma(L"once")` is a pragma here, in gcc and in
   clang, and is not one in the reference; no fixture covers it.
7. **A directive in an argument list is accepted** (recorded, no change).
   16.3/11 leaves it undefined; gcc and clang act on the directive, which is
   what makes the common `#if` inside a call work, and the reference rejects
   the invocation.  This implementation follows gcc, minus the two shapes
   finding 2 refuses.
8. **`__LINE__` is where the token was written** (recorded, no change).  For a
   multi-line invocation the reference answers the line the reader has reached;
   this implementation answers the line of the invocation head for a
   replacement-list token and the line it was written on for an argument token.
   gcc agrees with this in both shapes and clang in one.  No fixture
   distinguishes them, and the positional answer is the one hosted headers
   expect.
9. **The reference rejects `defined(defined)`** (recorded, not followed).
   `defined` is an identifier, so 16.1 makes it a legal operand and the answer
   is 0.  gcc and clang agree with this implementation and no fixture covers
   it.
10. **`#line 0` is an error here** (recorded, no change).  The handout asks for
    a positive integer and leaves anything else undefined; gcc rejects it and
    the reference accepts it.
11. **Inclusion nesting differs** (recorded, no change).  The limit here is
    256, the Annex B minimum an implementation should support; the reference
    fails between 100 and 200.

Findings from the implementation checkpoint, all fixed then and re-verified
now: a file's bytes were held twice; `#line` rejected numbers the assignment
allows; `#pragma once` silently ignored a `__FILE__` it could not identify; a
directory was included as an empty file.

## Changes

- `dev/src/macro_expander.*`: the invocation is held by index, and
  `collect_arguments` takes the macro and the argument base rather than a
  reference into the stack; `collecting_` counts open argument lists;
  `set_source` refuses a source change inside one; a `(` that follows a source
  change does not start an invocation.
- `dev/src/macro_model.*`: `PaintSets` is a chain of `{parent, name}` nodes,
  with `add` and `intersect` memoised on their operand ids and an intersection
  built back up in a canonical order; `<` and `>` join the interned spellings.
- `dev/src/preprocessor.*`: one `#include` operand path, with 16.2/4's combined
  header-name; `#line` refuses to renumber an argument list it is inside of;
  `_Pragma` destringizes as 16.9 spells it; file identity goes through the file
  table; the scratch string is `scratch_`.
- `dev/src/source_files.*`: `identify` is a member, caches what `stat` said
  about a path including that there was none, and no longer reads an
  uninitialised `stat` buffer on failure.
- `cppgm.tests/course/pa5/`: three fixtures, references regenerated with
  `make ref-test-pa5`.

## Performance Evidence

Measured on this machine, best of five, output compared byte for byte with
`preproc-ref` on every row.

| input | mine | reference |
| --- | --- | --- |
| 1.8 MB of C++, 12k invocations of 200 macros | 0.18 s, 7.7 MB | 0.43 s, 8.1 MB |
| the same input x4, 7.2 MB | 0.76 s, 18.5 MB | 1.55 s, 8.1 MB |
| 400 headers, 0.9 MB, included once each | 0.10 s, 14.4 MB | 0.25 s, 20.6 MB |
| 800 headers, 1.8 MB | 0.21 s, 25.5 MB | 0.46 s, 36.0 MB |
| one 3 KB guarded header included 4000 times | 0.46 s, 4.0 MB | 0.92 s, 8.2 MB |
| the same, 8000 times | 0.93 s, 4.2 MB | 1.77 s, 8.2 MB |
| the same header with `#pragma once`, 4000 times | 0.00 s, 3.9 MB | 0.09 s, 8.0 MB |
| 5.6 MB, 200k `__LINE__` uses | 0.44 s, 16.7 MB | 0.76 s, 8.1 MB |
| 3.1 MB, 100k `__FILE__` uses | 0.23 s, 11.8 MB | 0.59 s, 8.1 MB |
| 3.3 MB excluded by one `#if 0` | 0.01 s, 8.0 MB | 0.07 s, 8.1 MB |
| 6000 nested `#if` groups, all excluded | 0.00 s, 4.2 MB | 0.07 s, 8.1 MB |
| 256 srcfiles sharing one header | 0.23 s, 4.2 MB | 0.51 s, 8.1 MB |
| 4.0 MB of `#` and `##` over 50k invocations | 0.33 s, 16.8 MB | 0.80 s, 8.1 MB |
| a 400k-token macro argument | 0.41 s, 86.0 MB | 1.85 s, 552.9 MB |
| a 400-parameter macro invoked 2000 times | 0.35 s, 8.0 MB | 1.26 s, 8.2 MB |
| 100k `#define`/`#undef` of one name | 0.18 s, 30.0 MB | 0.34 s, 8.1 MB |
| a 2000-deep chain of distinct macros | 0.01 s, 5.0 MB | 0.10 s, 8.1 MB |
| `F(F(...F(1)...))` 2000 deep | 0.17 s, 4.4 MB | 2.35 s, 1417 MB |
| the same 4000 deep | 0.67 s, 5.0 MB | crashes at 5.2 GB |

Scaling series, each doubling its axis:

| axis | series |
| --- | --- |
| source bytes, 1.8 -> 7.2 MB | 0.18 -> 0.76 s (linear) |
| inclusions of one header, 4000 -> 8000 | 0.46 -> 0.93 s, flat 4.2 MB (linear) |
| distinct headers, 400 -> 800 | 0.10 -> 0.21 s, 14.4 -> 25.5 MB (linear) |
| srcfiles sharing a header, 64 -> 256 | 0.06 -> 0.23 s, flat 4.2 MB (linear) |
| macro nesting depth, 1000/2000/4000/8000 | 0.00/0.01/0.05/0.20 s, 4.6/5.0/5.9/7.7 MB |
| invocation nesting depth, 1000/2000/4000/8000 | 0.04/0.17/0.67/2.78 s, 4.3/4.6/5.1/6.0 MB |

Macro nesting was 8.8/22.6/76/290 MB before finding 4; it is linear now.
Invocation nesting is quadratic in time and linear in memory, which is the best
the semantics allow: gcc is 0.27/1.10/4.77 s on the first three at 103 MB,
360 MB and 1.4 GB, and the reference crashes at 4000.

## Validation

- `make test-report-through-pa5`: **266 / 266**, pa1 60, pa2 28, pa3 24,
  pa4 78, pa5 76 (70 handout plus the six added).
- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src`: pass.
- 4100 generated preprocessor programs -- nested and repeated inclusion, header
  guards, `#pragma once`, `_Pragma`, `#line` with and without a file name,
  conditional inclusion to three levels, the predefined macros, macro-produced
  header names in all four forms, and multiple srcfiles -- agree with
  `preproc-ref` on exit status and on every output byte.
- 900 of those programs, and every pa5 fixture, run under AddressSanitizer and
  UndefinedBehaviorSanitizer: no report.
- 74 hand-written cases over the shapes the generator does not reach agree with
  the reference except for findings 6 to 10, where gcc and clang were the
  second opinion.
- Every benchmark above compared byte for byte with `preproc-ref`.

## Checkpoint Ledger

| # | checkpoint | result |
| --- | --- | --- |
| 1 | PA5 as one stage: shared file table and line index, include stack with the course search and `#pragma once`, conditional inclusion through PA3, `#line`/`__FILE__`/`__LINE__`, predefined macros, `_Pragma`, `#error`, non-directive, and the `preproc` driver | 73/73 pa5, 263/263 through pa5 |
| 2 | PA5 audit: invocation stack held by index, an invocation located against one source file, 16.2/4 combined header names, chain hide sets, file identity owned by the file table, 16.9 destringizing | 76/76 pa5, 266/266 through pa5 |

## Active Checkpoint

None: the stage is complete and audited.
