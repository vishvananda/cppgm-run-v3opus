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
  bytes of each distinct path, its line-start index, and its `stat` identity.
  A path is read once per run and its bytes are shared, not copied, by every
  inclusion of it.
- `dev/src/macro_expander.*` keeps owning the rescan engine, the macro table,
  the text-sequence/directive split, and `#define`/`#undef`.  It gains the four
  seams a file-level phase 4 needs and PA4 does not use: `run_directive_line`,
  `pop_source`, `expand_builtin` and `run_text_operator`.
- `dev/src/preprocessor.*` owns what is above one file: the stack of open
  files, the presumed `__FILE__`/`__LINE__` of each, conditional inclusion,
  `#include` search, `#line`, `#pragma`, `#error`, the `_Pragma` operator and
  the predefined macros.
- `dev/src/ctrl_expr.*` keeps owning 16.1; its `defined` oracle became an
  interface so PA5 hands it the real macro table where PA3 hands it a mock.

`__LINE__` is a per-file presumed line rather than a token field: a macro
invocation cannot cross a directive, so it cannot cross an `#include` or a
`#line`, and the byte offset PA1 already stores plus the open file's line index
and delta answers it exactly.  `MacroToken` therefore stays 16 bytes.

## Current Failure Map

70 fixtures, all failing at turn start because `preproc` was a stub.  Grouped
by the compiler behavior each needs:

| group | tests | needs |
| --- | --- | --- |
| driver and format | 100-*, 120, 150-max, 400-multiple-source-files | outfile, `preproc N`/`sof`/`eof`, phase 7 `invalid` is an error |
| directive dispatch | 150-null, 150-error, 170-nondir1..6, 300-*, 700-redef* | the 12 directive names, null and non-directive, `#error` |
| conditional inclusion | 200-if, 200-conditional-exclusion, 400/500-predefined-macros, 300-defined-* | `#if` group state machine, 16.1 through PA3, `defined` protection |
| source file inclusion | 200-include, 400-bad-include, 400-header-guarded, 800-pragma-once | search order, file identity, `#pragma once`, include stack |
| presumed location | 500-predefined, 600/610-line-macro, 660-line-directive, 300-line-new-line, 600-predefined-macro-argument-expansion | `__FILE__`, `__LINE__`, `#line` |
| pragma operator | 500-pragma-ignore, 501/600-pragma-op | `_Pragma` after replacement |
| PA4 replacement | 200-*, 250-*, 500-tricky-join, 650/900/910-recurse, 700-*, 800-placemarker, 850-varargs | already implemented, reached through the new driver |

The last group is the largest and needs no new replacement behavior, which is
why the stage is one checkpoint: everything else is the file level around it.

## Active Checkpoint

None: the stage is complete.

## Performance Model

Dominant operations, in the order they cost: lexing and macro replacing each
inclusion (inherent -- the macros in force differ per inclusion), one `intern`
per source token, and one binary search per `__LINE__`.  Nothing rescans a file
to answer a location and nothing copies a file to read it.

- A distinct path is **read and indexed once** for the whole run, across
  srcfiles, and its bytes have **one owner**: `SourceText` is a shared buffer
  the reader borrows, so an inclusion costs a lex and not a copy.
- `__LINE__` is O(log lines) with no per-token storage; `__FILE__` interns its
  quoted spelling once per presumed name, not once per use.
- An excluded section **skips without building**: its tokens are dropped in
  `fetch`, so it costs phase 3 alone -- no interning, no replacement, no
  directive parse past the name.
- The `#if` group stack, the include stack and the pragma-once set are all
  proportional to nesting, not to file size.

Measured on this machine, output compared byte for byte with `preproc-ref`:

| input | mine | reference |
| --- | --- | --- |
| 2.3 MB of C++, 40k invocations of 200 macros | 0.24 s, 9.8 MB | 0.47 s, 8.1 MB |
| the same input x4, 9.1 MB | 0.94 s, 20.0 MB | 1.65 s, 8.1 MB |
| 400 headers, 1.1 MB, included once each | 0.11 s, 7.3 MB | 0.25 s, 8.1 MB |
| one 3 KB guarded header included 4000 times | 0.30 s, 4.2 MB | 0.60 s, 8.1 MB |
| the same, 8000 times | 0.61 s, 4.2 MB | 1.14 s, 8.0 MB |
| the same header with `#pragma once`, 4000 times | 0.01 s, 4.2 MB | 0.09 s, 7.9 MB |
| 2.2 MB, 200k `__LINE__` uses | 0.16 s, 10.0 MB | 0.35 s, 8.1 MB |
| 6000 nested `#if` groups, all excluded | 0.00 s, 4.5 MB | 0.08 s, 8.1 MB |
| 1.5 MB excluded by one `#if 0` | 0.05 s, 7.3 MB | 0.15 s, 8.1 MB |
| 100 nested inclusions | 0.00 s, 4.3 MB | 0.07 s, 7.6 MB |

Both scaling series are linear in both axes: 4000 to 8000 inclusions is 0.30 s
to 0.61 s at flat memory, and 2.3 MB to 9.1 MB is 0.24 s to 0.94 s with memory
growing by the source and its vocabulary alone.

## Findings

1. **A file's bytes were held twice** (performance, fixed).  `SourceFileTable`
   cached them and every reader over them copied them again, so a 9.1 MB
   translation unit cost 25.9 MB and every inclusion of a header cost a memcpy
   of it.  `SourceReader` now takes a shared `SourceText`: 20.5 MB for the same
   input, and an inclusion costs no copy at all.
2. **`#line` rejected numbers the assignment allows** (correctness, fixed).
   The bound was 2^31-1, where the handout bounds a `#line` number only by
   being a positive integer; `#line 4294967295` was an error here and is
   accepted by the reference.  The bound is now what a signed count holds and
   the presumed line is computed unsigned.  Covered by
   `cppgm.tests/course/pa5/300-line-number-range.t`.
3. **`#pragma once` ignored a `__FILE__` it could not identify** (correctness,
   fixed).  The course defines `once` as the identity of `__FILE__`, which
   `#line` can set to a name no file has; there is then no identity to record,
   and silently doing nothing hid it.  It is now an error, as in the reference.
   Covered by `300-pragma-once-presumed-file.t`.
4. **A directory was included as an empty file** (correctness, fixed).
   `#include "somedir"` stat'd and opened, read zero bytes and contributed
   nothing.  Reading a directory fails badly rather than at end of file, which
   is what `SourceFileTable::open` now distinguishes.
5. **A directive inside an argument list was rejected** (correctness, fixed).
   16.3/11 leaves it undefined and gcc, clang and `preproc-ref` all act on the
   directive, which is what makes the common `#if` inside a call work; this
   rejected the invocation as unterminated.  It now runs the directive and
   goes on collecting.  The reference is the odd one here: it rejects a `#if`
   in an argument list that gcc and clang accept.
6. **The reference rejects `defined(defined)`** (recorded, not followed).
   `defined` is an identifier, so 16.1 makes it a legal operand and the answer
   is 0.  gcc and clang agree with this implementation, no fixture covers it,
   and TESTING_AND_REFERENCES.md prefers the standard on non-test inputs.
7. **Inclusion nesting differs** (recorded, no change).  The limit here is 256,
   which is the Annex B minimum an implementation should support; the reference
   fails somewhere between 100 and 200.

## Validation

- `make test-report-through-pa5`: **263 / 263**, pa1 60, pa2 28, pa3 24,
  pa4 78, pa5 73 (70 plus the three added above).
- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src`: pass.
- 1700 generated preprocessor programs -- nested and repeated inclusion,
  header guards, `#pragma once`, `_Pragma`, `#line` with and without a file
  name, conditional inclusion to three levels, the predefined macros, and
  multiple srcfiles -- agree with `preproc-ref` on exit status and on every
  output byte.
- 30 hand-written cases over the shapes the generator does not reach (a
  macro-produced header name, a stray `#endif` in a header, an unterminated
  `#if` in a header, a directive between a macro name and its `(`, `_Pragma`
  in an argument, a raw and a wide `_Pragma` operand, a directory as a header)
  agree with the reference except for findings 5 to 7, where gcc and clang
  were used as the second opinion.
- Every benchmark above compared byte for byte with `preproc-ref`.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| 1 | PA5 as one stage: shared file table and line index, include stack with the course search and `#pragma once`, conditional inclusion through PA3, `#line`/`__FILE__`/`__LINE__`, predefined macros, `_Pragma`, `#error`, non-directive, and the `preproc` driver | 73/73 pa5, 263/263 through pa5 |
