# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C14 — 15.2p2's cleanup around a partly built object, reviewed at `a1eb9ba4`.**
The rule the checkpoint found is right and is one rule: a subobject is built by a
call, what an exception out of a later call has to undo is the calls already
made, and the handler destroys that list backwards. Keeping the list as the
*instructions that named* each subobject rather than as the address they produced
is what lets a handler stand in a block of its own, and "the list only grows, so
equal length is equal content" is a real invariant that turns the reuse question
into one probe. 12.4p8's suffix given the same shape found the defect underneath
it — a `return` in a destructor's body had been running none of the member
destructions — and the suffix chained past `kUnwindSuffixLimit` is where the
references stop writing them out too. Sweeps over the subobjects a class holds
crossed with its base, its constructor, its destructor and where the object
stands leave that core byte-identical to `cppgm++-ref` at 50, 100, 200 and 400
members.

**What the review found is that membership of the list is decided by whichever
constructor call happens to find the mark, so an object that is no subobject
joined it and a subobject that should have joined it did not** — and that once an
array element is a step, the cursor 8.5.1's array path addressed elements with is
not a naming another block can write again. Beside them, 8.5.1p7's tail of an
array of class type was a span of zero bytes standing in for the construction the
standard asks for, 5.2.4's explicit destructor call asked for no definition, and
`inline` written on a class's declaration of a constructor or destructor was
dropped.

**1. 5.2.4's explicit destructor call declared a symbol and defined nothing.**
C14 made `note_destruction_entry` the one owner of the two questions every end of
a lifetime asks — which of the ABI's entry points the use names, and the
definition 12.4p6 gives an implicitly declared destructor. It left the one use of
a destructor that is not an end of a lifetime the analysis wrote asking neither:
`p->~T()` set the entry flag by hand and never asked for the body. `struct Held {
~Held(); }; struct Box { Held held; }; p->~Box();` wrote `declare function
@Box___Box` and a call of it, and no unit defines it — the object file names a
symbol that is nowhere. It is one call of the owner C14 built.

**2. 12.2p1's temporary and 5.3.4p12's object joined 15.2p2's list.** The mark is
planted where a step begins and taken by the next constructor call, and a
new-expression written in a mem-initializer's clause makes one before the step's
own call does. `struct T { N * p; N a; T() : p(::new((void*)buf) N), a() {} };`
put the *heap* object on the list, so an exception out of `a`'s constructor
destroyed an object 15.2p2 says nothing about — and, because a handler writes the
naming again, it re-ran the allocation function to find it and named temporaries
of a block a handler may not name. `always` already says the object stands at an
address the program computed rather than in the object being built; that is
exactly what says it is no step of it, and it now says so.

**3. An element of an array subobject past the first was no step at all.**
`struct T { V w[3]; N a; T() : w{V(1), V(2), V(3)}, a() {} };` put only `w[0]` on
the list: 8.5.1's array path makes one call per element and the mark it found was
the mem-initializer's own, which the first call took. An exception out of `a`'s
constructor destroyed `w[0]` and left `w[1]` and `w[2]` standing. Each element is
now a step, which is what 12.6p1's own array path already did.

**4. And an element was named by a cursor no other block can name.** Making the
element a step is only sound if the element is named the way the program would
name it — the object, the member, then the subscripts — because that naming is
what the handler writes again. `initialize_array` addressed elements from one
base by byte, which is what the references write where the array *is* the object
and not what they write where it is a subobject of one; a handler built from it
named a temporary of the block it left. `LowObject` now carries the chain of
subscripts the walk stepped through instead of one, so a dimension further in is
named from the object too — without which `V w[2][2]` written from a braced
clause emitted a handler naming a temporary of another block, which is malformed
LowIR. The addressing this puts back is the one 5.2.1p1 would write and the one
every other place that reaches an element already used.

**5. 8.5.1p7's tail of an array of class type was a span of zero bytes.**
`N w[4] = { N(), N() };` constructed two elements, zeroed the other two and
destroyed all four — 12.4p8 ends the lifetime of every element whether a clause
reached it or not, so two destructors ran on objects no constructor had. And
`struct T { V w[4]; T() : w{V(1), V(2)} {} };` for a class with no default
constructor was accepted, where the references and g++ refuse it: the zero was a
success path standing in for an initialization the class cannot perform.
8.5.1p7 value-initializes an element no clause reached, which for one of class
type is the constructor 8.5p8 gives it and not a span of bytes; the aggregate
path already wrote it that way and the array path now does too. It is linear —
`V w[4000]` with two clauses is 24 041 lines in 0.06 s, the same count the
references write.

**6. `inline` on a class's declaration of a constructor or destructor was
dropped.** 7.1.2p2 makes a function inline if any declaration of it says so.
This read the specifier only from the declaration the body is written on, so
`struct Box { inline ~Box(); }; Box::~Box() { ... }` bound strongly and, being a
definition no other unit may hold, owed both of the ABI's entry points as two
definitions where the references write one and an alias. That is the checked-in
`300-explicit-destructor-call-enclosing-namespace-type`, whose failure had
nothing to do with the enclosing namespace's type; it passes now. The
member-specifier list was already read for `explicit` and is read for `inline`
beside it.

### Left for a later checkpoint

- **15.2p1 and 15.2p2's region around a whole full-expression.** The references
  put a cleanup around every full-expression that can throw while an object is
  still owed a destruction, not only around the call that builds a subobject: an
  object a declaration named earlier in the block, a call written as a
  mem-initializer's argument (`b(side())`), and a new-expression written in one
  are each a region there and none here. The three are one rule and one
  checkpoint — the value the expression produces has to cross the region, which
  is what the `$call__n` slot the references materialize is for. The first of
  them is what the failure map already named as the block-scope gap; the other
  two are named with it now. 95 of the 800 programs of the class-shape sweep and
  14 of 144 of the clause-shape sweep are this and nothing else.
- **The ABI's two entry points counted from uses this unit never writes.** The
  flags are set by the analysis wherever it reads a definition, and the 3.2p3
  closure then emits only some of them, so a class no program reaches makes its
  base owe both names: `struct B { ~B(); }; struct D : B { ~D(); }; int main() {
  B b; }` writes two definitions of `~B` where the references write one and an
  alias. It predates the object model and is bounded — the extra name is a
  definition nothing calls, never a call of a name nothing defines — but the
  question belongs where C14 put its half of it, in what the lowering wrote, and
  answering it needs the closure to run before the naming is decided.
- **12.6p1's array of class type as a loop** (C15), **`#pragma pack`** and the
  eight single defects the failure map lists — unchanged and owned elsewhere.

### Confirmed intact

- pa1-pa15 hold at 1174 / 1174 from a clean tree. pa16 was 291 / 301 at the
  turn's start and is 292 / 301 at the end — the same fixtures passing and
  `300-explicit-destructor-call-enclosing-namespace-type` with them — and
  297 / 306 with the five regression tests this audit adds, one per finding.
- Of the 253 passing fixtures with a reference output to compare, 162 are byte
  for byte identical. What is left differs only in the order the top-level
  definitions are written in, in the internal symbol name `lowir.md` makes a
  presentation tie-breaker, and in `unwind` and `trivial_lifecycle`, which the
  comparison ignores. `pass=` differs on no passing fixture.
- Four differential sweeps of 1 097 programs against `cppgm++-ref` — 800 over
  what a class holds x its destructor's shape x where the object stands, 93 over
  the number of subobjects across `kUnwindSuffixLimit`, 144 over what a
  mem-initializer's clause can be x where the object stands, and 80 over array
  subobjects, nesting depth and source order. Every remaining disagreement is
  named above or in the failure map: 95 + 14 the region gap, 40 multiple
  inheritance the README puts out of scope, 17 the block-scope `static` refusal,
  8 the array loop and the empty-body elision, 5 the `zeroinit` limit, 5 the
  aggregate constructor of an array of aggregates, and 3 the entry-point count.
- Valgrind is clean over 642 programs — every pa16 fixture source and every
  synthesized input of the array, nesting, clause-shape and multiplicity sweeps.
- No fallback success path, timeout workaround, source-specific gate, dummy
  output or file-audit bypass survives. Finding 5 removes the one fallback this
  audit found — a zero written where a constructor is owed — by writing the
  construction, which is also what makes the refusal appear where the class has
  no default constructor. The two remaining rules that write less than the
  program says are unchanged and named: 8.5.1p2's empty-class subobject, which a
  checked-in `.ref` asks for, and the `kZeroSpanLimit` / `kUnwindSuffixLimit`
  bounds, both of which change the spelling of an order and never its content.
- The file audit passes with the same two `bad-division` warnings every audit
  since C1-C2 has recorded, in `sema_analyzer.h` and `lowir_lower.h`. What this
  audit adds to a header is one field and one nested description on `LowObject`
  and one flag on `UnwindMark`; the rules are in the `.cpp` files that own them.

### Checked and left alone

- **The references do not destroy an array a braced initializer named.**
  `N w[4] = { N(), N() };` at block scope constructs four elements there and
  destroys none; g++ runs four destructors, and so does this unit. It is the
  reference's own gap and the values are what the program says.
- **Multiple inheritance is refused.** `struct T : BaseN, BaseT` is accepted by
  the references and is on the README's out-of-scope list, so the 40 programs of
  the sweep that write one are not a defect of this milestone.
- **An array of aggregates written from braces calls the constructor 8.5.1 gives
  the element where the references store its members.** `struct P { int a; int
  b; }; struct T { P w[2]; T() : w{{1,2},{3,4}} {} };` is 8 stores there and 2
  calls here. It is C11's synthesized constructor, the mirror of the divergence
  the failure map already names for `T x = T{...}`, and the values agree.
- **A class's `operator new`, the floating spellings, `_GLOBAL__N_1`, the
  discarded prvalue's name and every divergence the C6-C13 audits recorded** —
  all unchanged.

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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis is linear in its size but the one named below the
table, which is the chain 10.2p2 asks a missed name to walk.

| axis | sizes | times |
| --- | --- | --- |
| n classes each with an out-of-class constructor definition | 500 / 1000 / 2000 / 4000 | 0.08 / 0.16 / 0.33 / 0.63 s |
| n classes each with an out-of-class destructor definition | 500 / 1000 / 2000 / 4000 | 0.09 / 0.17 / 0.34 / 0.69 s |
| n out-of-class constructor definitions of one class's overloads | 500 / 1000 / 2000 / 4000 | 0.04 / 0.07 / 0.13 / 0.26 s |
| n out-of-class member definitions naming a member typedef | 500 / 1000 / 2000 / 4000 | 0.05 / 0.10 / 0.20 / 0.41 s |
| n uses of a decltype-qualified name in one body | 500 / 1000 / 2000 / 4000 | 0.03 / 0.05 / 0.09 / 0.18 s |
| n mutable members written through a const object | 500 / 1000 / 2000 / 4000 | 0.03 / 0.06 / 0.11 / 0.22 s |
| n classes in a chain, each declaring a member of its own | 500 / 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.06 / 0.12 s |
| n classes in a chain, each naming the root's member typedef | 500 / 1000 / 2000 / 4000 | 0.09 / 0.33 / 1.25 / 6.06 s |
| n base members each brought in by a using-declaration of its own | 500 / 1000 / 2000 / 4000 | 0.01 / 0.03 / 0.06 / 0.13 s |
| one using-declaration bringing in n overloads of one name | 500 / 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.07 / 0.14 s |
| n brought-in overloads, all hidden by n declarations of the class's own | 500 / 1000 / 2000 / 4000 | 0.03 / 0.06 / 0.13 / 0.27 s |
| n classes deep, each bringing in the one before it | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.05 / 0.10 s |
| n calls of a brought-in member in one body | 500 / 1000 / 2000 / 4000 | 0.01 / 0.03 / 0.05 / 0.11 s |
| n constructors of one base, all inherited | 500 / 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.09 / 0.29 s |
| n classes each inheriting the constructors of the one before it | 500 / 1000 / 2000 / 4000 | 0.03 / 0.06 / 0.12 / 0.26 s |
| n objects built through one inherited constructor | 500 / 1000 / 2000 / 4000 | 0.03 / 0.05 / 0.10 / 0.21 s |
| n classes constructed and destroyed both as a complete object and as a base | 500 / 1000 / 2000 / 4000 | 0.17 / 0.36 / 0.73 / 1.49 s |
| one base constructor with n defaulted parameters, inherited | 500 / 1000 / 2000 / 4000 | 0.02 / 0.05 / 0.16 / 0.56 s |
| n elements in one local array, constructed and destroyed | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.05 / 0.09 s |
| n elements in one namespace-scope array, constructed and destroyed | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.05 / 0.09 s |
| n x 2 member array constructed by the class it belongs to | 250 / 500 / 1000 / 2000 | 0.01 / 0.01 / 0.03 / 0.06 s |
| n-element namespace-scope array value-initialized by `{}` | 500 / 1000 / 2000 / 4000 | 0.00 / 0.00 / 0.00 / 0.00 s |
| n-element array member value-initialized by `m()` | 500 / 1000 / 2000 / 4000 | 0.00 / 0.00 / 0.00 / 0.00 s |
| n members each with an alignment-specifier | 500 / 1000 / 2000 / 4000 | 0.00 / 0.01 / 0.01 / 0.03 s |
| n class clauses of one namespace-scope array's static image | 500 / 1000 / 2000 / 4000 | 0.00 / 0.01 / 0.01 / 0.03 s |
| array dimensions deep, 2^d elements | 6 / 8 / 10 / 12 | 0.00 / 0.01 / 0.05 / 0.26 s |
| n classes nested one inside the next, each holding an array of two | 8 / 12 / 16 / 20 | 0.00 / 0.00 / 0.00 / 0.00 s |
| n thread-local objects, each with a constructor | 500 / 1000 / 2000 / 4000 | 0.02 / 0.05 / 0.10 / 0.23 s |
| n thread-local objects, each with only a destructor to register | 500 / 1000 / 2000 / 4000 | 0.03 / 0.05 / 0.12 / 0.25 s |
| n thread-locals declared `extern` and used before all n definitions | 500 / 1000 / 2000 / 4000 | 0.03 / 0.06 / 0.12 / 0.25 s |
| n uses of one thread-local in one body | 500 / 1000 / 2000 / 4000 | 0.01 / 0.01 / 0.03 / 0.06 s |
| n calls of a reserved function | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.06 / 0.11 s |
| n unused inline definitions | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.03 / 0.07 s |
| n namespace-scope variable declarations, each redeclaration-probed | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.04 / 0.09 s |
| n placement new-expressions of a class in one body | 500 / 1000 / 2000 / 4000 | 0.03 / 0.05 / 0.12 / 0.22 s |
| n classes each declaring its own `operator new`, each new'd once | 500 / 1000 / 2000 / 4000 | 0.09 / 0.19 / 0.37 / 0.76 s |
| n braced functional casts written as arguments | 500 / 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.07 / 0.14 s |
| n aggregates with an empty-class subobject built from `Z{1,2}` | 500 / 1000 / 2000 / 4000 | 0.03 / 0.06 / 0.12 / 0.25 s |
| n namespace-scope floating objects, each an image item | 500 / 1000 / 2000 / 4000 | 0.01 / 0.03 / 0.05 / 0.10 s |
| n member functions declared, each asked 12.5p1's question | 500 / 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.06 / 0.13 s |
| an aggregate whose member is an n-element array of a class | 500 / 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.07 / 0.14 s |
| n members of class type, each a 15.2p2 step | 50 / 100 / 200 / 400 | 0.01 / 0.02 / 0.10 / 0.40 s |
| n elements of one array subobject written from a braced clause | 50 / 100 / 200 / 400 | 0.01 / 0.05 / 0.20 / 0.83 s |
| an n-element array subobject with two clauses, tail value-initialized | 500 / 1000 / 2000 / 4000 | 0.01 / 0.01 / 0.03 / 0.06 s |

- 5.3.4's new-expression costs one qualified lookup of `operator new`, one
  overload resolution over what it reached and the initialization a declaration
  of the same object costs - no scan of the unit and no second pass over the
  type: n of them in one body are 10 n + 36 lines at 0.22 s for n = 4000, linear
  at 2.0x per doubling. n *classes* each with an allocation function of its own
  and one new-expression each is 34 n + 12 lines at 0.76 s, which is the class
  scope's lookup and the definition of each function, not a search.
- 12.5p1's question is one comparison of the name a member declaration wrote
  against `operator`, asked where 9.3.1p3 decides whether the function has an
  object parameter: 4000 member functions declared are 0.13 s and 21 lines,
  linear at 2.0-2.2x, and a class with no allocation function pays that one
  comparison per member.
- 5.2.3p3's braces cost the one reading of the list the initialization would
  cost anyway: 4000 braced functional casts as arguments are 7 n + 36 lines at
  0.14 s. 8.5.1p2's empty-class subobject costs one flag on the action and one
  test in the lowering - 4000 of them are 13 n + 20 lines at 0.25 s - and the
  address it no longer computes is two instructions per subobject that are no
  longer written.
- 2.14.4's floating image is one spelling per item, so n namespace-scope
  floating objects are 2 n + 5 lines at 0.10 s for n = 4000, linear at 2.0x.
  Nothing walks the digits twice: the fold refuses a floating operand at the
  first node it reaches, and the one decode 8.5.4p7 asks for happens only under
  a braced clause whose types differ in width.
- An element of an array of class type is now indexed the one way, so an
  aggregate whose member is an n-element array of a class is 14 n + 14 lines at
  0.14 s for n = 4000 - the same instructions per element the element walk
  already wrote, and one description rather than two.
- 15.2p2's list costs one entry per call a constructor made and one handler per
  step that needs different destructions from the step before it, so n
  subobjects are the n(n+1)/2 calls the rule asks for - which is what the
  references write and is bounded by what the source spelled out. n members of
  class type are 5 118 / 17 693 / 65 343 / 250 643 lines at 50 / 100 / 200 / 400
  in 0.01 / 0.02 / 0.10 / 0.40 s, byte-identical to `cppgm++-ref` at every size.
  An array subobject written from a braced clause is the same shape now that
  each element is a step - 9 396 / 33 746 / 127 446 / 494 846 lines in
  0.01 / 0.05 / 0.20 / 0.83 s, with the constructor byte-identical to the
  references and only the destructor differing, where they write the loop C15
  owes.
- Naming an element from the object each time costs the walk the source would
  write - one `decay` and one step per dimension - and nothing is walked twice:
  the address is written once by whoever needs it, so a scalar element that
  stores its clause names itself once and not once for the store and once for
  the step. 8.5.1p7's value-initialized tail is one action per element, which is
  linear and is what the references write: an n-element array subobject with two
  clauses is 6 n + 41 lines at 0.01 / 0.01 / 0.03 / 0.06 s for
  500 / 1000 / 2000 / 4000, against the references' 0.52 / 0.54 / 0.64 / 0.86 s
  for the same count.

- A constructor or a destructor defined outside its class costs one probe of
  13.1's index of the class's chain and the same body pass a definition written
  in the class body costs: n classes each with one are 35 n + 9 lines at
  0.63 s for n = 4000, and n such definitions of one class's overloads
  0.26 s - each doubling 1.9-2.1x. What the object file holds is two definitions
  of one body per strong definition and one plus an alias per inline one, so a
  class with n constructors writes 2 n and not 2^n.
- 7.1.6.2p1's decltype-specifier before `::` costs one reading of its expression
  and one lookup in the region that type names, wherever the name is written:
  4000 uses in one body are 0.18 s for 24 014 lines, linear at 2.0x. The
  expression is read once by the parser and kept, so nothing re-parses it and the
  version the parse memoizes against does not move.
- 7.1.1p10's `mutable` is one flag on the member's own declaration and one mask
  where 5.2.5p4 carries the object's cv: 4000 mutable members written through a
  const object are 0.22 s for 36 020 lines, linear at 1.9-2.0x, and a class with
  no mutable member pays one test per member access.
- The one super-linear axis is 10.2p2's chain, and it is the lookup that misses
  at every level: n classes in a chain each naming a name the root declares are
  0.09 / 0.33 / 1.25 / 6.06 s at 500 / 1000 / 2000 / 4000, which is n steps for
  each of n classes and 15 lines of output at every size. A name the class itself
  declares costs one probe - the same chain with a member of its own at each
  level is 0.12 s at 4000 - and a name no declaration of the unit wrote is one
  probe of the declared names before any base is searched. The quadratic is what
  a program that writes it asks for; it is named here rather than hidden, because
  nothing in the fixtures reaches a chain more than a few classes deep.

- The pass that lowers the thread-local definitions before any body costs one
  walk of the unit's top level and lowers each definition once - the second
  reach of the same node in the ordinary pass writes nothing, which
  `emitted_globals_` is what says. 4000 thread-locals declared `extern`, used in
  one body and then defined take 0.25 s for 104 011 lines, against 0.23 s and
  104 006 for the same 4000 defined before the use: the same work in the same
  order, and one call per use rather than none.
- 3.2p3's closure holds after this audit: 4000 unused inline definitions are
  4 output lines at 0.07 s, and the definitions-before-bodies pass reads no body
  it would not otherwise have read.
- A declaration of a variable now asks the region it declares into for the
  declaration it already holds, so that 7.1.1p1 can be answered. That is one
  probe of one hash per declaration: 4000 namespace-scope declarations are
  0.09 s, linear at 2.0-2.3x per doubling.

- What the hiding finding removed is per declaration and not per program: n
  brought-in overloads hidden by n declarations of the class's own took 2.53 s at
  n = 4000 when 7.3.3p14 was asked once per member declaration, and 0.27 s now
  that it is one pass where 9.2p2 completes the class - 2.0-2.2x per doubling,
  and 16 lines of output at every size, because what the program uses is one
  call.
- 7.3.3p1's using-declaration writes no work at a use: 4000 calls of a
  brought-in member in one body are 24 017 lines in 0.11 s, six lines a call, and
  a class 4000 deep each bringing in the one before it is 17 lines in 0.10 s
  because 10.2 finds the member in the class it was named on.
- 12.9's inherited constructors are one pass over the base's chain with one probe
  of 13.1's index per member of 12.9p1's candidate set: n classes each inheriting
  the one before it is 13 n + 14 lines at 2.0-2.2x per doubling, and n objects
  built through one inherited constructor 10 n + 36. n constructors of one base
  all inherited is 48 lines whatever n is, and its 3.2x at the last doubling is
  the type layer interning n distinct parameter types: declaring the same n
  constructors with no using-declaration at all grows 0.02 / 0.03 / 0.07 / 0.22 s
  over the same sizes.
- The ABI's two entry points are two definitions of one body: 4000 classes each
  constructed and destroyed both ways are 368 009 lines in 1.49 s, 92 lines a
  class, each doubling 2.0-2.1x. Where the unit holds no body they are two
  declarations instead, which is two lines rather than none.
- Every one of the array axes writes output exactly linear in the objects the
  source asks for: at 500 and at 4000 the line counts are 5033 / 40 033 for the local
  array, 5042 / 40 042 for the namespace-scope one and 1513 / 12 013 for the
  static image - eight times the size for eight times the output. An array of
  2^d elements d dimensions deep is 982 lines at d = 6 and 110 614 at d = 12,
  which is the elements it has times the steps to one of them.
- What the startup finding removed is per element and not per program: a
  namespace-scope array value-initialized by `{}` is 14 lines at 500 elements and
  at 4000, where it had been 5 n + 18 - 20 018 of them at 4000 - writing zero
  into storage the program image already holds. The same holds of an array member
  value-initialized by `m()`, which is 24 lines at every size because 8.5.1p7's
  span is one `zeroinit` past 64 bytes.
- A class with n alignment-specifiers costs one constant evaluation and one
  comparison each in the one 9.2p13 pass: 23 lines of output at every size, and
  0.03 s at 4000.
- A class holding an array of two of the next, nested d deep, is 2^d objects and
  d definitions: 404 lines at depth 20, because each level writes the two calls
  its own constructor makes rather than the objects beneath them.

- Measured at the C7 audit and unchanged, each doubling 1.9x-2.3x, at
  500 / 1000 / 2000 / 4000: n declarations `YA q = p;` copying one class object,
  0.04 / 0.06 / 0.10 / 0.17 s; n calls passing one by value,
  0.04 / 0.05 / 0.07 / 0.12 s; n written temporaries each passed to a by-value
  parameter, 0.04 / 0.06 / 0.09 / 0.15 s; n arguments reaching a class parameter
  through 13.3.3.1.2's converting constructor, 0.04 / 0.05 / 0.09 / 0.15 s; n
  functions each returning a temporary by value, 0.05 / 0.09 / 0.16 / 0.29 s; n
  value-initialized 16-byte class objects, 0.04 / 0.07 / 0.11 / 0.21 s; n
  conditionals over two class lvalues, 0.05 / 0.09 / 0.15 / 0.27 s; n classes
  nested one inside the next, 0.03 / 0.05 / 0.07 / 0.13 s. A copy of one class
  object is still one `copyobj` written where the object it goes into is known,
  and no place allocates a slot for a copy it already owns storage for.
- Measured at the C6 audit and unchanged, each doubling 2.0x-2.3x: n one-bit
  fields in one class, aggregate-initialized then assigned one by one,
  0.02 / 0.05 / 0.10 / 0.23 s; n fields of four alternating declared types, so
  every field opens its own unit, 0.02 / 0.03 / 0.07 / 0.12 s; n classes nested
  one inside the next each with two fields, 0.01 / 0.02 / 0.06 / 0.13 s; n reads
  of two fields of one class in one body, 0.02 / 0.04 / 0.08 / 0.16 s; n
  namespace-scope objects of a class with fields each dynamically initialized,
  0.02 / 0.04 / 0.10 / 0.20 s. 9.6p2's layout still carries a byte cursor and the
  open unit's type, start and used bits, so a field costs one comparison and one
  addition and a class with no bit-field never opens a unit.
- Measured at the C5 audit and unchanged, each doubling 2.0x-2.4x: one call with
  n arguments of n distinct associated classes, 0.02 / 0.06 / 0.12 / 0.29 s;
  base-chain depth with one ADL call two arguments deep,
  0.00 / 0.01 / 0.03 / 0.06 s; n ADL calls each four classes above the friend,
  0.01 / 0.02 / 0.05 / 0.10 s; a nested enum then a class eight deep,
  0.01 / 0.03 / 0.06 / 0.11 s, and that shape by chain depth,
  0.00 / 0.01 / 0.03 / 0.08 s; a chained `operator<<`,
  0.00 / 0.01 / 0.01 / 0.03 s; operator nesting depth,
  0.01 / 0.01 / 0.03 / 0.06 s; n namespaces each with a class and an ADL free
  function, 0.04 / 0.10 / 0.21 / 0.43 s; n friend declarations revealed by as
  many definitions, 0.03 / 0.07 / 0.15 / 0.35 s.
- The one quadratic axis is quadratic because the resolution is: one class with n
  friend `operator<<` overloads used n times, 250 / 500 / 1000 / 2000 at
  0.05 / 0.15 / 0.62 / 2.99 s, the same before this audit and after.
- Nested namespace depth is super-linear and belongs to the scope layer, not to
  the operator or friend paths: 500 / 1000 / 2000 / 4000 deep with one ADL call
  at the bottom take 0.01 / 0.02 / 0.07 / 0.24 s, unchanged by this audit.
- Measured at the C4 audit and unchanged, each doubling 2.0x-2.3x:
  derived-to-base conversions in one body four deep, 500 / 1000 / 2000 / 4000 at
  0.01 / 0.02 / 0.03 / 0.07 s; accesses to a base's member four deep,
  0.01 / 0.01 / 0.03 / 0.08 s; chain depth with one access to the root member,
  0.01 / 0.02 / 0.04 / 0.09 s; 11.4p1 protected accesses through a five-deep
  chain, 0.00 / 0.00 / 0.01 / 0.02 s; classes in a chain with nothing used,
  0.01 / 0.02 / 0.04 / 0.09 s. The n*d output blow-up that audit removed stays
  removed: a 4000-deep chain with 4000 accesses is 20 007 lines in 2.0 s where it
  had been 16 012 007 in 54.6 s.
- Measured earlier and unchanged: 4000 mem-initializers in one constructor,
  0.07 s; 4000 default member initializers in one class, 0.06 s; 4000
  namespace-scope objects constructed and destroyed, 0.09 s; 4000 loops each with
  a `break` leaving one object, 0.33 s; 2000 constructor overloads chosen between
  for one call, 0.06 s; a single `break` unwinding 800 nested blocks, 0.04 s;
  8000 members laid out and initialized twice, 0.14 s; nested aggregate depth
  800, 0.80 s; 8.5.1p7's zero-fill bound at 2^20 elements in under 0.01 s.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` - 291 / 301 at the start of
  this audit and 292 / 301 at the end, the same set passing and
  `300-explicit-destructor-call-enclosing-namespace-type` with it, and
  297 / 306 with the five regression tests it adds, one per finding: an explicit
  destructor call on a class whose destructor is implicitly declared; a
  new-expression written in a mem-initializer's clause before a member of class
  type; a three-element array subobject written from a braced clause with a
  member after it; an array subobject whose braced clause leaves a tail; and a
  destructor a class declares `inline` and a later definition defines. All five
  agree with g++ on the value the program computes.
- `make test-report-through-pa15` - 1174 / 1174, from a clean tree and after
  every change this audit makes.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` - passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over every pa16 fixture source and every
  synthesized input of the sweeps and the probes - 642 programs, with no error
  reported.
- Differential against `pa16/cppgm++-ref` on 1 097 synthesized programs in four
  sweeps. 800 over what a class holds - a base, one to three members, a
  one-element and a two-element and a two-dimensional array, an aggregate member
  of class type, a member with a default member initializer, a member whose
  class ends a lifetime with nothing, a member whose class begins one with
  nothing - crossed with eight destructor shapes and five places the object can
  stand: 665 byte-identical, 95 the region 15.2p1 owns and 40 the multiple
  inheritance the README excludes. 93 over the number of subobjects a
  constructor builds and a destructor destroys, 0 through 20 and 31 through 33,
  as members, as one array and over a base: 85 byte-identical, 7 the array loop
  C15 owes and 1 the empty-body elision. 144 over what a mem-initializer's
  clause can be - a new-expression before and after the member, a conditional, a
  call, a braced clause for an aggregate and for an array, a default member
  initializer, a base with an argument, a member reading the one before it -
  crossed with eight places: 110 byte-identical, 17 the block-scope `static` and
  `thread_local` refusal already named, 14 the region gap and 3 the entry-point
  count. 80 over array subobjects of one to seventeen elements, partial and full
  clause lists, scalar and aggregate and multi-dimensional elements, aggregates
  nested two three and four deep, and a class used before its definition:
  72 byte-identical, 5 the `zeroinit` limit and 5 the aggregate constructor of
  an array of aggregates.
- g++ was asked about every shape this audit turns. It runs a destructor for
  every element of `N w[4] = { N(), N() };` - four constructions and four
  destructions - which is what this unit writes and what the references write
  neither of. It refuses `V w[4]` from two clauses where `V` has no default
  constructor, as the references do and as this unit now does. It binds a
  destructor a class declares `inline` weakly wherever the body is written. And
  it destroys no object a new-expression made when a later mem-initializer
  throws. Where g++ and the references agree against this unit the finding was
  taken as a defect; the one place they disagree with each other - the
  destruction of a braced-initialized array - is recorded as the reference's own
  gap rather than resolved against it.
- Scaling: three new single-axis series at four sizes each for the paths this
  audit touched, with the output line counts recorded beside the times, plus the
  C4-C13 axes carried forward. The two 15.2p2 axes are the n(n+1)/2 the rule
  asks for and are byte-identical to the references at every size; 8.5.1p7's
  tail is linear at 2.0-2.2x per doubling.
- Byte-identity: of the 253 passing fixtures with a reference output, 162 are
  identical without any stripping at all. The rest differ only in the order the
  top-level definitions are written in, in the internal symbol name `lowir.md`
  makes a presentation tie-breaker, and in `unwind` and `trivial_lifecycle`,
  which the comparison ignores - `pass=`, which it does not ignore, therefore
  agrees on every one.
