# PA16 Audit — `cppgm++ --emit-lowir` object model

The final, whole-stage audit of PA16, re-derived from the README, the standard
and the source rather than from the checkpoint record. Seven blockers, all
fixed; the sweeps that found them; and the evidence.

## Findings

Ranked by what they cost the milestone.

1. **12.6p1's array of class type was quadratic in a bound the source wrote as
   one number.** Every element was written out, and C14 had made each of them a
   step 15.2p2 asks about separately - so `struct YX { YA w[400]; };` was
   494 836 lines in 0.93 s from three lines of source, where the references
   write 55 flat. 8.5.1p7's tail had the same shape one layer up: the analysis
   made one node per unreached element, so an array subobject with two clauses
   was 484 501 lines at 400.
2. **15.2p2's handlers were quadratic in the members a class declares.** Each
   step's handler wrote out the destructions of every step before it, which is
   n(n+1)/2 calls: a class with 2000 members of class type was 6 053 039 lines
   in 12.48 s. 12.4p8's suffix had already been chained past a limit for
   exactly this reason and 12.6.2's steps had not.
3. **Six checked-in `.ref` fixtures held answers no reference binary wrote.**
   `make -C pa16 ref-test` regenerates every `.ref` from `cppgm++-ref`; doing
   that and running the suite left six failures. Two assert `EXIT_FAILURE` for
   `static int count = 0;` in a block, which the references lower and PA15 puts
   out of scope; four hold LowIR this unit generated for thread-local shapes
   the failure map already names as resolved against the references. A fixture
   whose expectation is the implementation's own output tests nothing.
4. **3.4.2's argument-dependent lookup for an ordinary call was unreachable.**
   `f(c)` for a hidden friend or a function only an associated namespace
   declares was refused, and so was the plainer namespace case - which says the
   gap was never about friendship. 6.8p1 was where it sat: the parser let any
   identifier no scope in force declares stand as a decl-specifier, so `f(c);`
   was a declaration of `c` and the call never reached `call_expression`.
   3.4.3.4's global `::` was refused for the same reason.
5. **9.2p13 gave two subobjects of one class the same address.** The ABI gives
   an empty base subobject offset zero and then forbids a second subobject of
   that class from standing there; the layout asked only the first half, so
   `struct Q {}; struct T : Q { Q q; };` was one byte where g++ and the
   references write two - and one level in, `struct H { Q e; }; struct T : Q
   { H m; };` was the same collision, where the references stop short and
   misalign `m` doing it.
6. **9.5p1's anonymous struct declared nothing.** `struct S { struct { unsigned
   a; unsigned b; }; unsigned c; };` laid out 4 bytes and `s.a` named nothing,
   where g++ and the references lay out 12. The union half of the same rule had
   two defects of its own that the struct half found: a class written inside
   another anonymous one had its members re-pointed at the outer object, and
   the object such a class declares at namespace scope was declared and never
   defined.
7. **Two 5.2 forms were wrong or missing.** 4.12's conversion to `bool` was
   skipped for a constant operand, so `(bool)2` passed 2 and `int(bool(2))` was
   2 where g++ and the references write 1; and 5.2.5p1's qualified-id in a
   member access was refused, which is the only way a program has of calling a
   member a derived class hid.

Smaller things the same sweeps turned up, each fixed with the finding above it:
a spelling with two writers, where `8.5p7`'s float zero was `0.0F` beside
`spell_floating`'s `1.5f`; an `index` or a `div` scaled by a one-byte element;
a member of an anonymous class addressed in two steps where it is one offset;
`sema_class.cpp` at 3 137 lines against the audit's 3 000; and `Scope`'s
constructor initializing two members out of declaration order, which was the
one warning the build had.

## Changes

- **12.6p1 and 12.4p8 as a loop.** One number in `sema_facts.h`,
  `kArrayLoopLimit` (16), because three layers have to agree about which form
  was written: the analysis leaves 8.5.1p7's tail as one action carrying how
  many elements it is; the lowering writes the construction and the destruction
  as a loop over an index the function holds; and 12.4p8's suffix counts that
  array as one step. 15.2p2 is what the index is for beyond counting - an
  exception out of the element being built leaves the ones before it standing,
  and how many those are is a value only the loop knows, so the handler reads
  the index back and destroys that many.
- **15.2p2's handlers as a chain.** Past `kUnwindSuffixLimit` a handler
  destroys the subobject the step before it built and enters that step's own
  handler, which is the rest of what it owes - the same chain 12.4p8's suffix
  already used, and the block it enters is the one `unwind_dispatch_` already
  recorded.
- **6.8p1's answer.** `DeclaredNames` keeps, per spelling, the kinds the unit's
  declarations gave it - one bit each, unit-wide rather than per scope, because
  the question is about the unit - and answers `Value` for a name no
  declaration made a type. 14.2p3 keeps its own answer, so `declval<T>()` stays
  a call. A qualified spelling is asked the question of the name it ends in,
  which is what settles 3.4.3.4's global `::`.
- **The ABI's empty subobjects.** A class carries where the empty class
  subobjects of an object of it stand, as the class and the byte, built once
  where 9.2p2 completes it from the base's list and each member's. A member
  whose type would put one where one of its class already stands is moved to
  the next address its alignment allows and asked again. A class with none
  answers before asking anything.
- **9.5p1 over a class rather than over a union.** The injection no longer
  tells the two apart; a class written inside another anonymous one keeps its
  own chain and the outer one gives that object its place; the namespace-scope
  object is defined with internal linkage; and the lowering follows the chain
  the analysis holds, adding the offsets rather than writing an `index` per
  link. An anonymous struct outside a class declares nothing and is refused.
- **5.2.9 and 5.2.5.** The constant path of a cast excludes `bool` for the same
  reason `converted` already did. The member lookup splits a qualified
  id-expression the way every other qualified name is split, asks the class the
  prefix names - the object's own or one 10.2's chain reaches - and runs the
  same one walk from there.
- **The seam `sema_class.cpp` had grown past.** `sema_class.cpp` keeps what a
  class *is* and `sema_lifetime.cpp` takes what running its special members
  comes to. Both read the same `SemaEntity` and neither re-reads syntax the
  other read.
- **Six fixtures removed.** Every remaining `.ref` in the assignment is one
  `cppgm++-ref` wrote, and `make -C pa16 ref-test` is now idempotent. The
  behaviour they asserted is unchanged and is named in the plan's Final
  Architecture Review; the wrapper, the guard, the per-use call and 12.4p11's
  registration are still covered by two thread-local fixtures the references
  agree with.

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at
the end of the audit.

| axis | sizes | before | after |
| --- | --- | --- | --- |
| `YA w[n]` as a member, a local, a namespace-scope object | 50 / 400 / 4000 | 9 386 / 494 836 lines, 0.01 / 0.93 s | 147 lines and 0.00 s at every size |
| an array subobject with two clauses and an 8.5.1p7 tail | 50 / 400 / 4000 | 8 151 / 484 501 lines | 199 lines at every size |
| n members of class type in one class | 250 / 1000 / 2000 / 4000 | 100 414 / 1 526 539 / 6 053 039 lines, 0.18 / 2.99 / 12.48 s | 8 146 / 31 396 / 62 396 / 124 396 lines, 0.01 / 0.05 / 0.11 / 0.22 s |
| n classes each with an empty base and an empty member | 500 / 1000 / 2000 / 4000 | — | 0.02 / 0.04 / 0.10 / 0.22 s |
| a chain of n empty classes | 1000 / 2000 / 4000 | — | 0.03 / 0.08 / 0.26 s |
| n hidden-friend ADL calls in one body | 500 / 1000 / 2000 / 4000 | refused | 0.00 / 0.01 / 0.02 / 0.05 s, 2 n + 19 lines |
| n anonymous structs in one class | 250 / 500 / 1000 / 2000 | refused | 0.01 / 0.02 / 0.04 / 0.10 s |
| n qualified-id member calls in one body | 250 / 500 / 1000 / 2000 | refused | 0.00 / 0.01 / 0.02 / 0.04 s |
| n classes nested one inside the next, member access at the bottom | 250 / 500 / 1000 / 2000 | — | 0.00 / 0.01 / 0.02 / 0.05 s |
| n nested blocks each holding an object of class type | 250 / 500 / 1000 / 2000 | — | 0.00 / 0.01 / 0.04 / 0.16 s |
| a base chain n deep, member access at the bottom | 250 / 500 / 1000 / 2000 | — | 0.00 / 0.01 / 0.02 / 0.05 s, 16 lines at every size |
| n member functions declared and called | 250 / 500 / 1000 / 2000 | — | 0.05 / 0.02 / 0.04 / 0.09 s |
| n distinct non-member operators, each used once | 250 / 500 / 1000 / 2000 | — | 0.04 / 0.13 / 0.57 / 2.22 s |
| n objects built by a constructor with an argument | 250 / 500 / 1000 / 2000 | — | 0.00 / 0.01 / 0.02 / 0.05 s |
| n bit-fields declared and written | 250 / 500 / 1000 / 2000 | — | 0.01 / 0.01 / 0.03 / 0.06 s |
| n placement new-expressions | 250 / 500 / 1000 / 2000 | — | 0.01 / 0.01 / 0.03 / 0.06 s |
| n namespace-scope nested aggregates | 250 / 500 / 1000 / 2000 | — | 0.00 / 0.01 / 0.02 / 0.04 s |

Every axis above is linear at 2.0-2.5x per doubling but the operator one, which
is the lookup each of n classes pays, and the two the plan names and does not
average away: 3.8p1's return through n nested blocks, which is the n^2/2 calls
the source asks for, and a name declared n levels up a chain of n classes, which
is a lookup that misses at every level.

## Validation

- `make test-report-through-pa16` - **1494 / 1494**, from 1493 / 1493 at the
  turn's start with six fabricated fixtures removed and seven regression tests
  added: the array lifecycle at the loop limit, a hidden friend found by an
  ordinary call, a global-scope qualified call with a class argument, an empty
  base and a member of its class, a cast to `bool`, an anonymous struct's layout
  and lookup, and a qualified-id in a member access. Each of the seven agrees
  with `cppgm++-ref` after the relaxed comparison, and each was generated
  through `make -C pa16 ref-test`.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` - passes with
  the two recorded header-weight warnings, `dev/src/lowir_lower.h` and
  `dev/src/sema_analyzer.h`. `sema_class.cpp` is 1 953 lines and
  `sema_lifetime.cpp` 1 250 against the 3 000 limit. The build has no warnings.
- Differential sweeps against `cppgm++-ref`, compared with the harness's own
  canonicalization: 144 programs over array element shape x bound x placement
  (18 identical, 126 in the empty-`@__cppgm_init` and empty-destructor families
  the failure map names); 53 over the callee's placement x the shape of the call
  x seven programs 6.8p1 must still read as declarations (46 identical, 11
  refused by both because `::f(c)` and `(f)(c)` suppress 3.4.2, none refused
  here alone); 60 over base shape x member run (28 identical, 32 in the
  empty-init family and the ABI rule); 350 over 14 cast targets x 10 operands x
  3 spellings (330 identical, 20 the `copy i64` the references write between two
  `i64` types of different signedness); 15 over anonymous struct, union, nested
  and namespace-scope shapes (11 identical); 12 over the shapes a qualified-id
  takes (9 identical, 2 refused by both). Every disagreement is named in the
  plan's Final Architecture Review.
- A 107-shape layout sweep against **g++** - 6 base shapes x 18 member runs -
  agrees on every size, where the direct empty-subobject collision had been
  wrong before.
- Execution through `lowir2cy86` and `cy86`: 35 programs counting the
  constructions and destructions of both array forms at bounds either side of
  the limit; 7 counting them across the 15.2p2 chain's limit at 1, 2, 15, 16,
  17, 18 and 40 members; and one cross-feature program - a base class, an empty
  member, a 20-element array of class type, an anonymous struct, a hidden-friend
  call, two qualified-id calls and a `bool` cast - that this unit and g++ agree
  on.
- Valgrind clean over all **306** pa16 fixture sources and over the 144, 60, 53,
  15 and 12 program sweeps.
- The metadata the comparison strips: `object=`, `binding=`, `storage=` and
  `linkage=` agree with the references over **282 of 284** passing fixtures.
  The two that differ are the `_GLOBAL__N_1` local-name marker C16 resolved for
  g++, which writes the same nested component this unit does.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset zero, 10.2p2/p6 lookup through the chain, 11.2p2/p4 and 11.4p1 access, 12.6.2p5 base initialization and 12.4p8 base destruction, 12.1p5/12.4p3 triviality through the base, 4.10p3 / 8.5.3p4 / 5.2.9p11 as one `base-conversion` node with 13.3.3.1.4p1's rank and 13.3.3.2p4's order, 5.9p2 and 5.16p3, the object model split into `sema_class.cpp` | one derived-to-base conversion written as one node per link of the chain where the references write one, at n·d instructions for n accesses d deep; a chain access-checked at its first link only; the conditional's composite pointer type converting neither operand, so a private base was reachable through `?:` and not through `==`; 8.5.3p4's base half of reference-related missing, so `static_cast<Base&>` was refused; 5.2.9p11's reference downcast refused and its pointer downcast unchecked; an inaccessible destructor called for an object, a member and a base; 11.4p1's additional check on a protected member absent; every constructor and destructor named with the complete-object entry, where the references name a base-only one with the base-object entry and no alias; a reference member's binding claiming `projection=reference_field`; a member whose declaring class the walk never reached converted to the last base anyway; three dead helpers and a reordered initializer list left by the split | pa16 126 / 243 held, the same set passing and failing; five `.ref` files now byte for byte identical and the passing fixtures differing from the reference at all down from 39 to 34; pa1–pa15 1173 / 1173; valgrind clean over 243 fixtures and 88 probes; conversion, lifetime, protected-access, chain-depth and access axes all linear at 2.0–2.3× per doubling, and the n·d output blow-up gone (16 012 007 lines in 54.6 s → 20 007 in 2.0 s); file audit passes with the two recorded header-weight warnings; the stripped metadata — now including `projection=` — agrees with the refs but for `unwind=no` |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for, 13.5.7p1's `x++0`, 13.5.3/13.5.4/13.5.5's member-only `= () []`, 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and revealed by 7.3.1.2p3, 11.3p11's elaborated-type-specifier, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward; 3.2p3's uses read from the whole resolved tree | 3.4.2p2's base chain abandoned wherever the class was already associated, so a hidden friend of a base went unfound when a nested type of the derived class was named first; every class around a nested type associated where 3.4.2p2 associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither of the operator-call sites, so a protected member operator was refused where a friend of the derived class named it and accepted where the object was of the base; a member `operator- + * &` encoded with the unary Itanium terminal, because the arity counted the written parameters and 9.3.1p3 had already made the object an operand; an out-of-class definition of a static member function given an object parameter and declared a second function, so the unit called `@YB__f()` and defined `@YB__f(%this)` - and, where the definition was `inline`, defined nothing at all; 13.5p6 written for its non-member half and not its static-member one; a pointer condition branched on through a `cmp ne ptr` the references do not write | pa16 161 -> 163 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; the ADL association axes linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of all sixteen unary/binary forms of `+ - * &` agree with g++ |
| C6 | 9.6p1's width and the four facts it settles on the member's own declaration, 9.6p2's allocation into storage units, the read as a load-shift-mask at the promoted type, the write as a read-modify-write and as a plain store where the initialization owns the unit, 8.5.1's unnamed field, 12.6.2 and 8.5.1's two instruction orders, 5.17 and 5.3.2 over a field, 5.3.1p3 and 5.3.3p1's refusals, 3.6.2p2's static data as the bytes the bits fall in | the allocation unit read as a bit cursor, so a field packed into the bytes of the member before it - `struct { char c; int x : 3; }` 4 bytes where the references write 8, `char a:3; int b:5; char c:2;` 4 where they write 12, and a derived class's field over the base subobject, which its constructor then stored over after the base had run; the unit loaded and put back at 4.5p3's promoted type rather than at the signed integer of its own width, so every `unsigned`, `unsigned char`, `unsigned short`, `bool` and `unsigned long` field wrote a type no reference writes; an initialization joining the unit with raw operations at a member type narrower than `int` where 4.5 promotes each step and 4.7 converts it back; both masks written for a field that owns every bit of its unit; 3.6.2p2's static image folded into `u8` items where a data item names a whole object and the references initialize it before the program runs; an unnamed bit-field stepped over by the clauses where the references let one reach it; an assignment converting its value after naming the object it writes into; a constant initializer converted by an instruction rather than spelled as the value it produces; 4.12's conversion to `bool` compared at 5.14p1's width rather than at the type of what it converts | pa16 173 -> 174 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 87 probes; every bit-field shape the reference accepts now agrees with it byte for byte - 17 layout shapes, 15 declared types, the read, the write, the initialization and the static image; layout, field-count, nesting, access and static-object axes linear at 2.0-2.3x per doubling; file audit passes with the two recorded header-weight warnings; of the 152 passing fixtures with a reference 110 are byte for byte identical and the rest differ only in top-level order, the internal symbol name and `unwind=no` |
| C7 | 12.2p1's prvalue of class type made an object the function holds, with `tmpobj__n` / `arg__n` / `argobj__n` naming its storage; 8.5.3p5's reference bound to it; 13.3.3.1.2p1's user-defined conversion sequence as a converting constructor's temporary, ranked between the standard sequences and the ellipsis; 5.2.2p4's argument of class type copied into a slot the call owns, with 12.8p31 creating a prvalue argument in it; 12.8p15's memberwise copy; 8.5p7's zero of a value-initialized class with no user-provided constructor; the object model of the lowering split into `lowir_lower_object.cpp` | 5.2.2p4's "the call owns the copy" written into `converted`, the conversion every initialization, assignment, return, conditional arm and cast reaches, so each of them allocated a call's `argobj__n` slot and copied twice - 4000 `YA q = p;` at 52 010 lines and 8002 slots for the 40 010 and 4002 the source asks for, and 4000 conditionals at 136 012 and 20 003 for 100 012 and 8003; 8.5.3p5's "named after the argument that asked" written into `apply_conversion`, so `const YA& r = YA(5);` named its storage `arg__1` and `return YA(6);` named it `argobj__1`; 13.3.3.1.2's temporary always named `arg`, returning before the 12.8p31 rule that would have named it `argobj` at the one place that rule was written for; a call returning a class by value handing back a value where an object was needed, so `make().get()` passed an `obj<4x4>` as a `ptr` and `const YA& r = make();` bound the reference to it; a copy of a class whose copy constructor the program wrote written as the copy of its bytes, so `YA q = p;` computed `p.a` where the program says `p.a + 1`; a declaration of a class object naming its address twice | pa16 186 / 243 held, the same set passing and failing; pa1-pa15 1173 / 1173; byte-identical passing fixtures up from 110 to 116 of 164, and what is left differs only in top-level order, the internal symbol name and metadata the comparison ignores; valgrind clean over 243 fixtures and 130 probes; copy, argument, temporary, conversion, return, value-initialization, conditional and class-nesting axes all linear at 4000, and the per-copy slot and instruction growth gone; file audit passes with the two recorded header-weight warnings |
| C8 | 12.6p1's array of class type constructed element by element and 12.4p8's destroyed the same way, as one action naming the array; an element addressed a dimension at a time, which 8.5p7's value-initialized array member, 3.6.2p2's dynamic initialization and both lifecycle calls share; 3.6.2p2's static image of an array of class type; an aggregate clause's value computed before the address it is stored into; 7.6.2p1's alignment-specifier read by 9.2p13's layout; 5.3.6p1's `alignof` as an expression; 9.1p2's qualified class-head-name | the address of an element computed for a constructor that is never called, and a namespace-scope array of them opening an empty `@__cppgm_init`; a multi-dimensional array constructing one object per row rather than one per element, and an array member value-initialized by `m()` storing a literal of an object type; a namespace-scope array calling its constructor once on the array; the element walk written twice; an alignment-specifier read from wherever it stood among the decl-specifiers, where 7p1 gives one written after them to the type rather than to the declaration, so `int alignas(8) x;` laid a member out at 8 and made its class 16 where g++ and the references write 4 and 8; 7.6.2p3's fundamental alignment never asked for, so `alignas(6)` allocated a member at every sixth byte and `alignas(-4)` made a class one byte; 8.5p7's zero of an object with static storage duration written into a startup body 3.6.2p1 had already made unnecessary - `YA g[4000] = {};` at 20 018 lines of stores into storage the image holds zero, and the empty `@__cppgm_init` for `YA g = YA();` with it | pa16 199 / 243 held, the same set passing and failing; pa1-pa15 1173 / 1173; byte-identical passing fixtures 127 of 177, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 243 fixtures and 116 probes; array, dimension, alignment and static-image axes linear at 2.0-2.2x per doubling and the per-element startup work gone (14 lines at 500 elements and at 4000); file audit passes with the two recorded header-weight warnings |
| C9 | 7.3.3p1's using-declaration in a class made a declaration of that class per declaration the base has of the name, carrying 11p1's access and naming the base's through `shadowed`, with 13.3.3.1p4's object parameter naming the derived class and 11.2p5 leaving the base subobject unchecked; 7.3.3p14's hiding asked in both orders; 12.9's inheriting constructors with 12.9p4's access, the base's parameters and names, and 12.9p8's definition; 13.3.3.2p3's cv tie-break kept through 4.10p3's conversion; the ABI's two entry points written as two definitions | 13.3.1.2's operator expression not read through `shadowed`, so `d + 1` called `@YD__operator_` on the derived object under `_ZN2YDplEi`, a symbol no unit defines, and the base's body was never emitted; 13.4's address of an overloaded brought-in name the same, in three places that all end in `name_function`; 8.3.6's default-arguments unreachable from the declaration the class made, so a call that omitted one was refused; a hidden declaration left in 13.1's index, so a third overload declared after two were hidden was read as a redeclaration - "f is defined twice" for a program g++ accepts; 7.3.3p14 comparing 9.3.1p3's object parameter, which no declarator writes, so a static member function was never hidden by a non-static one 9.4.1p2 does not let it overload; that hiding asked once per member declaration rather than once for the complete class, at n^2 (2.53 s at 4000); 12.9p1's candidate set read as one constructor per declaration carrying its default-arguments, so `YB(int = 1)` inherited was ambiguous with 12.1p5's default constructor; 12.9p1's "unless the class declares one" settled at the using-declaration and repaired by repurposing the declaration in place, which left it under that section's access in both directions; the base-object entry a call names declared nowhere where this unit holds no body; a constructor declared with an ellipsis reading one type past its parameter list, in the analysis and in the lowering | pa16 210 / 247 held, the same set passing and failing, and 220 / 257 with ten regression tests added; pa1-pa15 1173 / 1173; byte-identical passing fixtures 143 of 197, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 257 fixtures and 460 synthesized inputs once the two out-of-bounds reads were closed; a 458-program differential sweep against `cppgm++-ref` with every disagreement judged against g++; ten axes linear at 1.9-2.2x per doubling and the n^2 hiding gone (2.53 s -> 0.27 s at 4000); file audit passes with the two recorded header-weight warnings |
| C10 | 3.7.2's thread storage duration as a fact of the variable, with `storage=thread_local`, 3.7.2p2's `_ZTW` wrapper, a per-object guarded body where 3.6.2p2 does not settle the initializer, a call of it at each use, and 12.4p11 handed to `__cxa_thread_atexit`; 1.4p8's four reserved functions declared by the use that names one, with the object name and the boundary facts a call may assume, and 6.8p1's ambiguity settled in the parser; 3.2p3's emission as a closure from the roots of the unit, with 11.3p5's friend definition the one body the walk still reads | a block-scope object declared `static` or `thread_local` written as an object of its block - `thread_local int x = 0;` one object per call where 3.7.2p1 asks for one per thread, and `static int count = 0;` one object of the program written the same way, because 3.7.1p3's refusal sat in `record_lifetime`, which only an object of class type reaches; a use of a thread-local written before its definition running nothing that initializes it, the map that turns a use into that call being filled as each definition was lowered - the source-order dependence `collect_definitions` exists to remove, and 3.6.2p4's "before the first odr-use"; that map keyed by `entity.id` where 3.1p2 gives one variable as many declarations as the program writes, so the definition's body was invisible to a use bound to the `extern` declaration; one thread-local's body reading another object of the same thread before anything initialized it, the name being recorded only after the body was written; 12.4p11's end of a lifetime reached only from inside the body 3.6.2p2's dynamic initialization opened, so `thread_local Slot cell = {5};` with `~Slot()` registered nothing with `__cxa_thread_atexit`; 7.1.1p1 cited in the comment and not written, so `struct S { static thread_local int t; }; int S::t = 7;` and every other disagreeing pair was accepted; 1.4p8's reserved function declared by a written call and by no other use of the name, so `unsigned long (*p)(const char*) = __builtin_strlen;` and `::__builtin_strlen(s)` were refused | pa16 233 / 262 held, the same set passing and failing, and 240 / 269 with seven regression tests added; pa1-pa15 1173 / 1173; byte-identical passing fixtures 150 of 204, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 389 programs - every pa16 fixture source and every synthesized input of the sweep; a 134-program differential sweep against `cppgm++-ref` with every disagreement judged against g++, which agrees with this unit on the block-scope refusal's subject matter, on the initializer a use runs, and on the destruction a statically initialized thread-local still has; seven axes linear at 2.0-2.3x per doubling, including the definitions-before-bodies pass at 0.03 / 0.06 / 0.12 / 0.25 s for 500 / 1000 / 2000 / 4000 forward-declared thread-locals; file audit passes with the two recorded header-weight warnings once 3.5's linkage and 3.7's storage duration came out of `init_declarator` as one unit |
| C12 | 3.3.7p1's member name made a fact of the class that declares it, with what it declared reachable through the prefix its name gives it; 3.4.1p8's region put in force for the rest of a qualified declarator and for the body after it, in the parser and in the analysis; 10.2p2's base recorded as the class the base-clause reaches, resolved once and walked where a name misses; 8.3.5p2's trailing-return-type with 7.1.6.4's `auto` standing for it alone; 13.1's index keyed by the parameter-type-list a declarator wrote wherever a class declares the name; 7.1.1p10's `mutable`; 7.1.6.2p1's decltype-specifier before `::`; 5.1.1p6's parenthesized callee | a constructor or destructor defined outside its class declared nothing and defined nothing, the node reaching the arm of `declaration` written for an access-specifier - so `YA::YA(int v) { n = v; }` wrote `declare function @YA__YA` and no body, and a definition matching no declaration and one written twice were both accepted where the references refuse them; 3.4.1p8's region opened after the parameter clause and the mem-initializers had been read, because `parse_special_member` builds its declarator by hand, so `YA::YA(int v) : n((held)v)` was not a translation unit and `Outer::Buffer::Buffer(Token)` could not name its enclosing class's type; the ABI's two entry points keyed on which of them a use named, where a definition written outside its class without `inline` is the program's one definition and owes both names - the branch unreachable until such a definition was read at all; 7.1.6.2p1's decltype-specifier before `::` written for the type a declaration names and for nothing else, so `decltype(a)::held` naming a static data member, an enumerator, the address of one or a static member function was "no declaration of decltype(a) is in scope"; the expression that specifier carries written into the syntax tree, which is PA10's own output, so `--emit-ast` wrote a subtree the reference does not; 7.1.1p10's refusal asked only where the declarator went on to declare an object, so `mutable int f() { return 1; }` and `mutable typedef int held;` were accepted | pa16 265 / 283 -> 266 / 283, the same set passing and `200-nested-out-of-class-constructor-enclosing-type` with it, and 269 / 286 with three regression tests added; pa1-pa15 1173 / 1173, and 1174 / 1174 with the pa10 test for the AST dump of a qualified decltype-specifier; byte-identical passing fixtures 152 of 216, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 438 programs - every pa16 fixture source and every synthesized input of the sweep; an 88-program differential sweep against `cppgm++-ref` over out-of-class special members x where the class is declared x binding, decltype before `::` x use shape, `mutable` x declared type, 3.4.1p8's region x what names it, 13.1's index and the parenthesized callee, with 77 byte-identical after canonicalization and every one of the 11 disagreements named above or judged for this unit by g++; eight axes measured, the seven this audit's paths own linear at 1.9-2.1x per doubling and 10.2p2's chain named with its numbers rather than averaged away; file audit passes with the two recorded header-weight warnings |
| C13 | 5.3.4's new-expression: 3.7.4.1's allocation function found by 5.3.4p9's lookup, chosen by 13.3 from 5.3.4p8's byte count and the new-placement's own arguments, with 8.5p16's object built at the address it returned; 5.2.3p3's `T{...}` in the parser for a type-name, a keyword simple-type-specifier and a decltype-specifier, list-initialized by 8.5.1 for an aggregate and 13.3.1.7 for any other class; 12.8p31's elision of `T{...}` into the object it initializes; 8.5.1p2's member of a class that holds nothing written as the nothing the checked-in LowIR writes | 12.5p1's allocation function of a class read as an ordinary member, so one written without `static` was given 9.3.1p3's object parameter and `new((void*)buf) T(3)` was "no declaration of operator new accepts the arguments of a call"; the same name spelled `T::operator new` with the space its tokens are separated by wherever a declarator was qualified, so an out-of-class definition declared and defined `_ZN1T12operator newEmPv` while the call named `_ZN1TnwEmPv`; 8.5.1p2's empty-class subobject suppressed for every clause where 12.8p31 elides a prvalue into one for a prvalue alone, so `{ Mark(), 1 }`, `{ {}, 1 }` and `{ {3, 4}, 1 }` each lost the constructor the program wrote; the address of such a subobject, and of every element of an array of them, computed for an initialization that writes nothing; 5.19's fold answering for a floating operand with the integer no floating literal sets, so `float a = 1.5f;` was `global @a : f32 = 0` and so was every floating member, element and namespace-scope object; 5.2.3p2's floating `T()` spelled `0.0` at all three widths, which `lowir.md` gives f80 an `L` for; 8.5p7's zero of a pointer written as the integer 4.10p1 converts from rather than as the null pointer value; an element of an array of class type indexed by its object type on the subobject path and by the bytes it occupies on the element walk; 8.5.4p7 refusing `struct S { float m; }; S s = { 2.25 };`, whose value the conversion keeps | pa16 277 / 291 -> 278 / 291, the same set passing and `100-default-member-initializer-scalar-brace` with it, and 283 / 296 with five regression tests added; pa1-pa15 1174 / 1174; byte-identical passing fixtures 165 of 241, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 780 programs - every pa16 fixture source and every synthesized input of the three sweeps; three differential sweeps against `cppgm++-ref` of 150, 108 and 210 programs over the allocated type x the initializer form x `::`, over where a class declares its allocation function x how the new-expression finds it x the empty-class subobject's clause x where the object stands, and over the floating types x ten initializer forms x seven places, leaving 6 status disagreements of 468 - all six the floating arithmetic this milestone does not fold - and every text disagreement named above or in the failure map; seven axes measured, all linear at 1.9-2.1x per doubling; file audit passes with the two recorded header-weight warnings |
| C14 | 15.2p2's cleanup around a partly built object: the subobjects a constructor has built kept as the calls it made, each call after the first in an `eh_try` whose handler destroys that list backwards, the instructions that named a subobject written again in the handler's own block, a step needing what the step before it needed naming that block again; 12.4p8's suffix in one `eh_cleanup` with a region per destruction, written wherever control leaves the body and chained past `kUnwindSuffixLimit`; 15.2p2's odr-use asked of the whole list of steps at once | 5.2.4's explicit destructor call asking for neither of the two things `note_destruction_entry` was built to own, so `p->~Box()` on a class whose destructor is implicitly declared wrote `declare function @Box___Box`, a call of it and no body anywhere; 12.2p1's temporary and 5.3.4p12's object taking the step's mark, so the heap object of `T() : p(::new((void*)buf) N), a() {}` joined the list and its handler re-ran the allocation function to find it and named temporaries of a block a handler may not name; an element of an array subobject past the first being no step at all, so `w{V(1), V(2), V(3)}, a()` left `w[1]` and `w[2]` standing where `a`'s constructor threw; that element addressed from one byte cursor, which is what the references write only where the array is the object itself and which no handler can write again - `V w[2][2]` from a braced clause emitted a handler naming a temporary of another block; 8.5.1p7's tail of an array of class type written as a span of zero bytes, so `N w[4] = { N(), N() };` destroyed four objects two constructors had built and `V w[4]` for a class with no default constructor was accepted where the references and g++ refuse it; 7.1.2p2's `inline` read only from the declaration the body is written on, so a constructor or destructor a class declares `inline` and a later definition defines bound strongly and owed both of the ABI's entry points as two definitions | pa16 291 / 301 -> 292 / 301, the same set passing and `300-explicit-destructor-call-enclosing-namespace-type` with it, and 297 / 306 with five regression tests added, one per finding; pa1-pa15 1174 / 1174; byte-identical passing fixtures 162 of 253, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; valgrind clean over 642 programs - every pa16 fixture source and every synthesized input of the sweeps; four differential sweeps of 1 097 programs against `cppgm++-ref` over what a class holds x its destructor's shape x where the object stands, the subobject count across `kUnwindSuffixLimit`, what a mem-initializer's clause can be, and array subobjects x nesting depth x source order, with every remaining disagreement named - 109 the region gap 15.2p1 owns, 40 multiple inheritance the README excludes, 17 the block-scope `static` refusal, 8 the array loop and the empty-body elision, 5 the `zeroinit` limit, 5 the aggregate constructor of an array of aggregates and 3 the entry-point count; the constructor of an array subobject byte-identical to the references at 50 / 100 / 200 / 400 elements and 8.5.1p7's tail linear at 24 041 lines for 4000 elements in 0.06 s; g++ agrees with this unit on the four shapes it turns; file audit passes with the two recorded header-weight warnings |
| C15 | 16.6's `#pragma pack` carried from phase 4 to phase 7 as a value, an epoch counter and a `(position, alignment)` table the layout binary-searches where 9.2p2 completes the class; 2.14.8's user-defined-literal as the call p2 says it is, chosen by the one written parameter-type-list p3 to p6 name per form with p3's raw fallback and 13.5.8's `li` terminal; 5.2.4's pseudo-destructor call settled from the name after the `~` and written as the operand's value discarded; 2.14.5p12's sequence rebuilt from its parts | a literal's form guessed from the characters of its spelling, in `literal_expression` and again in `literal_constant`, so a character-literal whose c-char is a quote was a string - `char c = '"';` refused and `const char* p = '"';` accepted with a `@__strlit__1` no program wrote - and a floating-literal 2.14.4 spells with no integer part was no literal at all, `.5` / `.5f` / `.5e2` outside the subset wherever one stood and `char row['"' - 30];` with them; 16.6's position asked of `AstNode::end`, a span `parse_declaration` widens past the `;` for a class-specifier that is a whole declaration, so `struct S { char c; int i; }` `#pragma pack(push, 1)` `;` was packed by a directive the class was complete before while `... } g;` was not; a `pop` with an empty stack resetting the alignment to the default rather than leaving what a `pack` before it asked for, `#pragma pack(1)` then `#pragma pack(pop)` laying `struct S { char c; int i; };` out at 8 where g++ writes 5, and a labelled `pop` naming a label nothing pushed unwinding nothing where g++ drops the innermost frame | pa16 307 / 312 held, the same five fixtures failing, and 309 / 314 with two regression tests added; pa1-pa15 1174 / 1174; byte-identical passing fixtures 179 of 277, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, both ignored, with `pass=` agreeing on every one; a 462-program layout sweep - 11 directive forms x 4 class shapes x 12 member sets - read against **g++** through every member's byte offset in the emitted LowIR and every size and alignment through a compile-time refusal, 393 agreeing exactly and all 69 that do not in the bit-field and `alignas` families, which disagree with g++ with no directive written at all and agree with the references byte for byte; the same 462 against `cppgm++-ref` unchanged by the fixes at 239 byte-identical and 50 refused by both; nine push/pop shapes measured against g++ one at a time and all nine agreeing; 30 placement, 32 literal-operator, 29 pseudo-destructor and 17 literal-form programs against the references with every disagreement named; valgrind clean over 650 programs; seven scaling axes linear and none above 0.07 s; file audit passes with the two recorded header-weight warnings, and what this audit adds to a header is one `std::uint32_t` inside `AstNode`'s existing padding and one free function |
| final audit | the whole stage, re-derived from the README and the source | 12.6p1's array quadratic in a bound the source wrote as one number, and 8.5.1p7's tail with it; 15.2p2's handlers quadratic in the members a class declares; six `.ref` fixtures no reference binary wrote; 3.4.2 unreachable for an ordinary call because 6.8p1 read `f(c);` as a declaration, and 3.4.3.4's global `::` with it; 9.2p13 giving two subobjects of one class the same address, directly and one level in; 9.5p1's anonymous struct declaring nothing, an anonymous class nested in one losing its chain, and its namespace-scope object declared and never defined; 4.12's conversion to `bool` skipped for a constant operand; 5.2.5p1's qualified-id refused; one float zero spelled by a second hand; an `index` and a `div` scaled by a one-byte element; `sema_class.cpp` past the audit's size limit | pa16 305 -> 299 / 299 with the six removed and **306 / 306** with seven regression tests; pa1-pa16 **1494 / 1494**; the three quadratic axes linear; 107 layout shapes agreeing with g++; valgrind clean over 306 fixtures and 284 sweep programs; the stripped metadata agreeing with the references over 282 of 284 fixtures; file audit passes with the two recorded warnings |

## Durable architecture decisions

- An object of class type is storage, not a value. A conversion cannot produce
  one, so every place that needs a copy of one owns the storage the copy is made
  in and writes the copy there: 5.2.2p4's argument, 6.6.3p2's returned object,
  5.16's conditional, and the object an initialization or an assignment writes
  into. The rule for one of those places is written at that place and not at the
  conversion they share.
- The storage a temporary is given is named after what asked for it, and only a
  call's argument and a return's value ask. A reference a declaration binds, a
  clause of an aggregate, a cast and an assignment all read the object the
  expression already wrote, so it keeps the name it was given where it was
  written.
- 12.8p25 is a fact the class carries, settled where 9.2p2 completes it from its
  own declarations, its base and its members. 12.8p15's memberwise copy is the
  copy of the bytes exactly while that fact holds; where the program wrote a copy
  constructor the copy is what the program wrote, and a milestone that cannot
  call it refuses the copy rather than writing a different one.
- Every prvalue of class type is an object: the one a constructor made and the
  one a call returned alike. What the expression is worth from then on is that
  object, and one piece of storage has one address however many readers it has -
  which is what keeps a declaration and the initialization under it from naming
  the same slot twice.


- An array of class type is its elements: one action names the array and says
  how many objects it is, and one walk - the dimensions outermost first and the
  address of the object at one flat index among them - is what every place that
  names an element uses. 12.6p1's construction, 12.4p8's destruction, 8.5p7's
  value-initialization and 3.6.2p2's dynamic initialization ask for that walk
  rather than each writing one.
- 7p1 says which part of a declaration an alignment-specifier is a fact about:
  one written before the decl-specifiers is about what the declaration declares,
  and one written after them is about the type those specifiers named, which has
  no alignment to ask for. What it asks for is 7.6.2p3's fundamental alignment -
  a positive power of two, or zero, which asks for nothing.
- An object with static storage duration whose whole initialization is the zero
  3.6.2p1 already gave its storage has no action at all. What decides that is
  the initialization, not the constructor: the zero that is already there is
  what leaves nothing to run, so an object that only wants its constructor
  called still gets the body that calls it.
- A declaration a using-declaration made in a class is that class's, and the
  declaration it names is the base's. 11p1's access, 7.3.3p14's hiding and
  13.3.3.1p4's object parameter are facts about the first; every use - the body
  a call runs, the address `&` takes, the offset an access reads, the
  default-argument a call omits, the symbol the object file holds - is a fact
  about the second. Naming a function is where the two part company, and it is
  one description in one place, so a written call, an operator expression, a
  cast and an initialization from an overloaded name all reach the same rule.
- What a class declares is a question answered where 9.2p2 completes it, not
  where the body has got to. 7.3.3p14's hiding and 12.9p1's "unless the class
  declares one with the same signature" are both about the complete class, so
  the order the body wrote a using-declaration and a member declaration in
  changes nothing, and each is one pass rather than one question per
  declaration.
- 12.9p1 inherits a parameter-type-list, not a declaration. The candidate set is
  the base's own list and the shorter ones its defaulted parameters leave when
  they are omitted from the end; 12.9p2's constructor characteristics carry no
  default-argument, and the base's own is what fills the call 12.9p8 writes.
- 8.3.5p4's parameter-type-list is what the declarator wrote. 9.3.1p3's object
  parameter is one this milestone puts in a non-static member function's type,
  so wherever two declarations are told apart by their parameters it is left out
  and 8.3.5p7's cv-qualifier-seq stands beside them - which is what lets a
  static and a non-static declaration of one class meet.
- A name a call writes is one this unit owes the program a declaration of. The
  ABI's base-object entry is a second such name wherever a complete object and a
  base subobject both asked, so the unit writes both the definitions where it
  holds the body and both the declarations where it does not.
- A member's place in its object is a fact about the member, settled once where
  9.2p2 completes the class. Nothing walks a class to find a member again.
- 9.3.1p3's object parameter lives in the function's type, so 13.3 ranks the
  object with the arguments, the ABI encoder drops it and spells 8.3.5p7's
  cv-qualifier-seq as a qualifier of the name, and the lowering passes it first.
- 13.3.3.2p3 needs the type a conversion sequence produced, not just its rank: a
  `Match` carries it, and one rule orders every pair that differs only in
  qualifiers.
- A definition 7.1.2p4 gives the program rather than this unit waits for a use.
  The worklist is drained between top level declarations and never inside one.
  Which definitions those are is decided by where the definition is written, not
  by the region its declarator-id reaches.
- The relaxed LowIR comparison strips `object=`, `binding=`, `linkage=`,
  `role=`, `unwind=`, `projection=`, `effects=`, `capture=`, `access=`,
  `alias=`, `return=`, `keep_alias=`, `trivial_lifecycle=`, `tls_for=`,
  `prefer_local=` and `storage=readonly|writable`, and canonicalizes internal
  symbol names and top-level order, so a passing suite says nothing about any of
  them. `pass=` is *not* stripped. They are diffed against the references
  directly, once per audit.
- Access is a fact about the declaration; 11p2 says what it is and 11p6 says
  whose access a name is checked with, which for a declaration is the entity
  being declared rather than the scope it stands in.
- The analysis says which clause of a braced-init-list reached which subobject,
  and a tail of array elements no clause reached is one fact rather than one node
  per element.
- What the milestone does not model is refused where it is read, named in the
  diagnostic, not described as the program it would be without the construct.
  That includes an operand a resolved tree has nowhere to sequence.
- A name that has to be made unique among n others is given the next suffix, not
  searched for one from the first.
- Which region ends an object's lifetime is a fact about the object, settled from
  3.7.1's storage duration where the object is declared, and independent of the
  form its initializer took. Every way out of that region — falling through it,
  returning, breaking, continuing — runs the same walk over the frames it leaves;
  a way out that cannot be told which frames those are is refused.
- 12.4p12's `const volatile` on a destructor's object parameter says which
  objects it may be called for. 9.3.2p1's `this` says what its body may do to the
  one it is destroying. They are two facts, not one.
- An internal LowIR symbol writes one `_` for each character an identifier cannot
  hold, so two names never flatten to one.
- A derived-to-base conversion is one node however many classes it spans,
  because the base subobject begins where the derived object does. The walk of
  the chain says which class was reached and asks 11.2p5 of each link; it does
  not write a node per link.
- Which of the ABI's two entry points a constructor or a destructor stands under
  is a fact the analysis records on the declaration, from what the program ran
  it on. The lowering reads it, so the order the demand-driven worklist happens
  to reach a definition in cannot change the symbol it is emitted under, and a
  unit does not owe the program an entry point nothing named.
- An operator expression on a class or enumeration operand is the call 13.3.1.2
  says it is, written as the `call-expression` node a written call writes. What
  is left where nothing is viable is the built-in operator, which the caller
  describes; the operator path adds no node kind of its own.
- 3.4.2p2's associated set is gathered once per call, and the two questions it
  is asked - whether a region is already in it, and whether a class's base chain
  has already been walked - are different questions and separate probes. A class
  enters the set without its bases whenever it is the class a nested type is a
  member of.
- A member named on an object asks the same two questions wherever it is named -
  11.2p5's naming class and 11.4p1's additional check - and an operator is one of
  the places it is named. 13.3 has chosen by then, so both are asked of the one
  declaration rather than of everything the lookup reached.
- 9.4.1p2 writes `static` on the declaration inside the class and nowhere else,
  so which kind of member a qualified declarator declares is read from the
  declaration it redeclares. A definition that reads its own specifiers instead
  declares a second function the program does not have.
- 13.5's `+ - * &` name two operators each, told apart by how many operands the
  declaration takes. 9.3.1p3 put the object among them, so it is counted for
  that one question and left out of the encoding for every other.
- 4.12p1's conversion to `bool` writes no instruction where a terminator is the
  only thing that reads the value: an integer or an address is branched on as it
  stands. A floating value is compared, because its zero is not the zero bit
  pattern the terminator tests. Where the value is read rather than branched on,
  it is compared at the type of what it compares and the comparison's own result
  is read at the byte `bool` is stored in - which is a different question from
  5.14p1's truth value, whose width is LowIR's and not the operand's.
- 9.6p2's storage unit is not the member: it is a run of bytes with a width, an
  alignment and a type of its own, opened by the first field that cannot share
  the one before it and shared by the fields declared with that same type while
  their bits fit. An ordinary member and a field of another type each begin
  after it ends, so one member's storage never overlaps another's and the order
  9.2p13 laid the members out in is an order no initialization writes backwards
  over.
- A bit-field is read and written through its unit and is worth a value of the
  type the member was declared with. The unit is loaded, shifted, masked and
  stored at the signed integer of the unit's own width; what the field is worth
  keeps the declared type, so the conversion above a read is the one that type
  asks for and nothing is written to read one spelling as the other.
- An initialization computes the unit it is joining as an expression of the
  member's own type, so 4.5's promotion and 4.7's conversion stand at each step
  where the member is narrower than `int`. An assignment computes with the
  values the source named. The two are different questions and are asked
  separately.
- 8.5 gives an object the value its initializer is worth as that object's type,
  so a constant initializer is spelled as that value and nothing computes it.
  An assignment is not an initialization: it converts, except where the
  immediate already spells the value the conversion produces.
- A data item names a whole object. An object holding a bit-field a clause gave
  a value to is initialized before the program runs, because there is no item
  for a share of a storage unit; a field no clause reached is the zero of its
  unit, written once for every field in it.

- Storage duration is a fact of the declaration that declared the object, not of
  the initializer it was given or of the lifetime a class of its own ends. What
  a thread runs for an object with thread storage duration is what the
  declaration asks for - an initialization 3.6.2p2 left to run, 12.4p11's
  destruction handed to the runtime, or both - and an object that asks for
  neither gets no body. A block-scope object 3.7.1p3 gives static or thread
  storage duration is refused where the declaration is read, whatever its type,
  because writing it as an object of its block describes a different program.
- What identifies an object across the declarations of it is the name the object
  file gives it. 3.1p2 lets one variable be declared as often as the program
  likes, and each unqualified declaration makes an entity of its own here, so
  anything the unit records about the object - that it defines it, that it wrote
  a body a use of it has to run - is keyed by that name and not by a declaration.
- Whether a use of a name runs something first is settled before any body of the
  unit is lowered, as `collect_definitions` settles which definitions the unit
  holds. A rule that reads what the lowering has reached so far makes the
  program depend on the order the source happened to write it in.

- A definition names the declaration it defines; it never declares a second one.
  A constructor or a destructor written outside its class is 3.4.3p3's
  declarator-id naming the class, 13.1's index of that class's chain saying which
  of its declarations this defines, and 9.2p2's end of the translation unit
  reading the body - which is the same description a definition written in the
  class body already asked for, so the two differ only in where the region comes
  from. A member form the analysis has no path for is a program it cannot
  describe, not one it may drop: the switch that dispatches a declaration gives
  no arm silently to a definition.
- 9.3p2 says who may hold a body, and that is what decides how many of the ABI's
  entry points the object file owes. A definition every unit that needs one may
  hold owes the names this unit named; a definition only this unit holds owes
  both, whichever of them a call here happened to write. Counting the uses
  answers a different question and gets the second case wrong.
- The syntax tree is PA10's output. A fact a later assignment needs and the
  grammar leaves no place for - the expression a decltype-specifier before `::`
  holds - is carried beside the tree in a node the dump does not write, as
  7.6.2's alignment-specifier already is. Anything hung on a node the dump walks
  is a change to another assignment's answer.
- Which region a name reaches is one question. A type-specifier, an
  id-expression, a callee and the operand of `&` each ask it, so 7.1.6.2p1's
  decltype-specifier before `::` is answered in one place and those four reach it
  the same way rather than one of them holding the rule.
- What a declaration-specifier says is a fact about what the declaration
  declares, so it is checked where the declaration is read and not where one of
  the things it might declare is built. 7.1.1p10's `mutable` names a non-static
  data member; a member function, a typedef, a static data member and a
  declaration of no class at all are each refused there.

- 2.14.4's floating value is not one this translation carries. Every place a
  floating constant reaches the object file writes the digits the program wrote
  with the suffix the storage asks for, and 5.19's fold refuses a floating
  operand rather than answering with the integer it does not have. The one
  question asked about the value rather than the digits is 8.5.4p7's, and it is
  answered by decoding them the way phase 7 decodes them.
- The zero an object is value-initialized with is a value of that object's own
  type, and a constant the program wrote is what the program wrote. A pointer's
  8.5p7 zero is 4.10p1's null pointer value; a written null pointer constant is
  the integer it was spelled as; a floating zero carries the suffix that says
  which of the three widths it is one of. Which of the two a value is, is a fact
  the analysis records, because the lowering cannot tell them apart from a type.
- Which clause reached a subobject is a fact of the clause, not of the
  subobject's class. 12.8p31 elides a prvalue written with arguments or braces
  into the object it initializes, and everything else - 5.2.3p2's `T()`, a
  braced clause 13.3.1.7 hands to a constructor, an element, an argument, a
  mem-initializer - initializes it where it stands.
- A subobject whose whole initialization writes nothing is given no address.
  What decides that is the initialization - the class holds no bytes and the
  constructor is trivial or was elided - and it holds one step down an array as
  it holds for a member.
- 12.5p1 makes a class's allocation and deallocation functions static members of
  it whether or not `static` was written, because the storage one is asked for
  is what an object of the class would stand in. 13.5p1's list of overloadable
  operators is what tells them apart, and it is one question asked in one place.
- An operator-function-id is one name however its tokens are spelled apart, so a
  qualified declarator binds the same name the class did. A name that differs
  only in a separator is a second function the program does not have.
- 15.2p2's list holds the subobjects of the object a constructor is
  initializing, and nothing else. 12.2p1's temporary and 5.3.4p12's object stand
  at an address the program computed rather than inside that object, so a call
  that builds one is no step of it - which is the same fact that says the call
  is written even where the constructor does nothing.
- A subobject a handler may have to destroy is named by instructions that name
  it from the object. A block an exception reaches names nothing another block
  produced, so a naming that steps forward from a cursor is not a naming; the
  walk down to a subobject - the object, the member, then one subscript per
  dimension - is carried as the walk and written again wherever it is asked for.
  That is the same one description 5.2.1p1 would write and the one every other
  place that reaches an element already used.
- 8.5.1p7 value-initializes a subobject no clause reached, and for one of class
  type that is the constructor 8.5p8 gives it. A span of zero bytes is not that
  initialization: it begins no lifetime for 12.4p8 to end, and it accepts a
  class that has no default constructor to begin one with.
- 7.1.2p2 makes a function inline if any declaration of it says so, so `inline`
  is a fact of the function that its declarations contribute to rather than a
  fact of the declaration the body is written on. 9.3p2's in-class definition is
  one of the declarations that contribute it.
- Which of 2.14p1's literals a terminal was written as is asked of the phase that
  lexed it and never of the characters of its spelling. The token stream spells
  every literal the one way, so the terminal is lexed back into the pp-tokens
  phase 3 read it as and the first of them says the form - one owner, asked by
  every layer that reads a parsed literal. A character of the spelling answers
  nothing: a `"` stands in a character-literal, a `.` opens a floating-literal,
  and an encoding-prefix stands before both.
- A fact a phase before 7 establishes reaches phase 7 as a position in the token
  stream, and the class asks it at the terminal 9.2p2 completes the class on.
  That terminal is the class-specifier's own fact, with one writer, and not the
  span of whatever declared it: a class-specifier that is a whole declaration is
  handed back as that declaration, whose span reaches past the `;`, and a
  directive written there is one the class was already complete before.
- A `#pragma pack` pop is the undoing of a push. It restores what one frame
  saved - the frame the label names where it names one, and the innermost
  otherwise - and where it finds no frame it restores nothing, because there is
  no push whose effect it could be undoing. A pop never sets a value of its own.
- A construct every element or every step writes the same way is written out
  while a reader wants to count them and written as its order past that. One
  number says where the line is, and it is one number rather than three because
  the analysis that leaves 8.5.1p7's tail as one action, the lowering that
  writes 12.6p1's construction and 12.4p8's destruction as a loop, and the
  counting of a destructor's suffix all have to agree about which form was
  written. The order is a loop where the elements differ only by an index and a
  chain where each step owes the one before it plus what that one owed.
- 15.2p2's handler for a run of objects reads back the count the run itself
  carries. How many elements an exception left standing is not something the
  translation knows, so the index the loop holds is what says it, and the
  handler is the same loop run backwards from there.
- 6.8p1's ambiguity is settled by whether a name could be a type at all, which
  is a question about the whole translation unit and not about a region: a name
  no declaration anywhere made a type cannot begin a declaration wherever it is
  written. That is what lets a call the program wrote reach 3.4.2 rather than
  being read as a declaration of its argument, and it is asked only after every
  scope, using-directive and base has been asked, so a name that is declared
  pays nothing for it.
- The ABI's rule about empty subobjects needs to know which class holds nothing
  where, not that a base holds nothing. A class carries the empty class
  subobjects of an object of it, as the class and the byte it begins at, built
  once where 9.2p2 completes it from the base's list and each member's - because
  an empty subobject takes no storage to push the next one along, so the offsets
  alone do not say it.
- 9.5p1 is a rule about an object no name reaches, not about a union. What the
  anonymous union and the anonymous struct share is that their members are
  members of that object; what they differ in is what the class's own layout
  does with them, which was already settled elsewhere. A class written inside
  another anonymous one leaves a chain of such objects, and the chain is what
  the access follows - each object in turn from the outermost in, which is the
  order the offsets add up in and one step in the output.
- A `.ref` is what the reference binary wrote. Where this unit means to give a
  different answer - because the standard and g++ say so, or because the input
  is outside the milestone - the shape belongs in the failure map and in a
  differential sweep, and not in a fixture whose expectation would then be this
  implementation's own output. `make -C paN ref-test` regenerating every `.ref`
  and the suite still passing is what says no fixture is one of those.
