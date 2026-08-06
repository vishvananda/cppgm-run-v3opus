# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C7 — 12.2p1's class prvalue, reviewed at `2609becc`.** The architecture is the
right one. A prvalue of class type is an object the analysis declares, its
storage is the function's, and everything that reads it — a reference bound to
it, a member named in it, a base subobject of it, an argument passed it —
reaches the one object through the one entity on the `temporary-object` node.
13.3.3.1.2's user-defined conversion is that same temporary reached from the
argument side rather than a second mechanism, ranked once between the standard
sequences and the ellipsis. The layer split holds: `sema_class.cpp` builds the
temporary, `sema_overload.cpp` ranks it, `lowir_lower_object.cpp` writes it, and
the lowering re-resolves nothing.

What the review found is one shape, at six exits. **A rule about one place a
class object is needed was written at the junction every place goes through.**
5.2.2p4 says an argument of class type is a copy the *call* owns; that was
written into `converted`, which is the conversion an initialization, an
assignment, a return, an arm of a conditional and a cast all reach. 8.5.3p5 and
12.8p31 say the storage a temporary takes is named after the *argument* that
asked for it; that was written into `apply_conversion`, which every
initialization reaches. Both fired everywhere. Three findings stand beside that
one, each about an object of class type that had nowhere to stand or was copied
as something it is not.

**1. Every copy of a class object made a call's argument slot.** `YA q = p;`
allocated an `argobj__n` frame slot, copied `p` into it, and then wrote the whole
object out of it into `q` — three instructions and a slot where the references
write one `copyobj` from `p` to `q`. So did `q = p`, so did each arm of
`c ? p : q`, so did `return v`, so did `YA v = make()`, and so did
`static_cast<YA>(p)`. The cost is per copy, not
per program: 4000 declarations `YA q = p;` were 52 010 output lines and 8002
slots where the source asks for 40 010 and 4002, and 4000 conditionals were
136 012 lines and 20 003 slots for 100 012 and 8003. A class copy is now one
`copyobj` written where the object it goes into is known — an initialization
writes into the storage the declaration already named, an assignment into the one
the left operand names, an argument into `argobj__n`, a return into `retobj__n`,
an arm into the object the conditional is, and 5.2.9p4's cast into the temporary
12.2p1 makes of it — and `converted` refuses to read a class as a value at all,
so a seventh exit cannot pick the argument's rule up by accident.

**2. A temporary was named after an argument wherever a conversion made one.**
`const YA& r = YA(5);` named its storage `arg__1` where the references write
`tmpobj__1`, and `return YA(6);` named it `argobj__1` for their `retobj__1` — no
argument asked in either. In the other direction 8.3.6p1's default-argument
*is* the call's argument and was named `tmpobj__1` where they write
`argobj__1`. Which place asked is now what `apply_conversion` is told, and only
a call's argument — written or defaulted — and a return's value ask; everything
else keeps the name the expression that wrote the prvalue gave it.

**3. 13.3.3.1.2's temporary was always named `arg`, never `argobj`.** The branch
that makes the conversion's temporary returns before the 12.8p31 rule below it,
so `f(7)` reaching `f(YA)` through a converting constructor wrote `arg__1` where
the references write `argobj__1` — the one place the rule was written for, and
the one place it could not reach.

**4. A call that returns a class by value handed back a value where an object
was needed.** 12.2p1 was landed for `T(args)` and `T()` and not for the other
prvalue of class type. `make().get()` passed the returned `obj<4x4>` as the
implicit object argument, where the callee's `this` is a `ptr`, and
`const YA& r = make();` stored that same value as the address the reference
holds. Both are output no reader can make sense of. A prvalue of class type that
stands in no storage of its own is now given some, once, where it is first read
as an object.

**5. A copy the program wrote was written as the copy of its bytes.** 12.8p15's
memberwise copy *is* the copy of the bytes exactly while every member's own copy
is. A class with a user-provided copy constructor was copied with `copyobj` and
the constructor never called, so `struct YA { int a; YA(const YA& o) { a = o.a + 1; } };
YA q = p;` gave `q.a` the value `p.a` where the program says `p.a + 1` — a
different program, written without a word. 12.8p25 is now a fact the class
carries, settled at 9.2p2 completion from its own declarations, its base and its
members, and `copy_class_object` — the one place an object of class type is
copied — refuses the copy rather than writing a different one.

**6. A declaration of a class object named its address twice.** The declaration
computes the address its lifetime begins at; the initialization computed a second
`addr` for the same slot. One piece of storage has one address, and the
initialization now uses the one the declaration named.

### Left for a later checkpoint

- **A returned class prvalue is copied after the call rather than before it.**
  The references name the storage the result stands in, call, and copy into it;
  we call, then name the storage. The instructions are the same three and the
  program is the same; the order differs because the storage is decided in the
  lowering and not in the tree. Giving the call's result a `temporary-object`
  node of its own is what settles it, and is the same node `T(args)` already has.
- **`T{}` is not read.** The parser reads no braced functional cast, `int{}`
  included, so `YA v = YA{};` is refused where the references accept it. That is
  the syntax layer rather than the object model.
- **Direct-initialization from an object of the class's own type is refused** —
  `YA q(p);`, a mem-initializer `: m(v)`, `YB(YB(2))` — because no class here
  declares the implicit copy constructor 12.8p7 gives it. Copy-initialization
  reaches the same object through 12.8p31's elision, which is why only the direct
  forms show it. The same gap refuses 4.10p3's derived object passed to a base
  parameter by value.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 holds at 186 / 243: the
  same fixtures pass and the same fail as at `2609becc`. The findings change the
  verdict on no fixture, because no fixture declares a copy constructor, returns
  a class by value, or binds a name to a temporary — which is what made this
  checkpoint worth sweeping shape by shape against the references rather than
  fixture by fixture.
- Of the 164 passing fixtures with a reference output to compare, 116 are byte
  for byte identical, up from 110 at the C6 audit. What is left differs only in
  the order the top-level definitions are written in, in the internal symbol name
  `lowir.md` makes a presentation tie-breaker, and in the metadata the comparison
  ignores — `unwind`, `trivial_lifecycle`, `binding`, `role`, `keep_alias`,
  `object` and `projection`, every one of them on the ignore list. `pass=` is not
  on that list and does not differ on any passing fixture.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. The two refusals this audit adds name
  the construct and the clause; `converted` refusing a class is a place that can
  no longer be reached rather than a program that can no longer be written.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and 130
  synthesized inputs — value-initialization, temporaries, arguments, default
  arguments, conversions, returns, conditionals, casts, copies and the eight
  scaling axes.
- The file audit passes with the same two `bad-division` warnings the C1–C2, C3,
  C4, C5 and C6 audits recorded, both the heuristic counting declarations rather
  than bodies in `sema_analyzer.h` and `lowir_lower.h`.

### Checked and left alone

- **8.5p7's zero before a non-trivial default constructor.** Where a class writes
  no constructor but its base or a member does, 8.5p7 zero-initializes the object
  and *then* runs the constructor the standard gave it. We write both; the
  references write only the call, so `struct YA { int a = 7; int b; }; YA()`
  leaves `b` holding whatever the storage held. The handout and the standard
  outrank reference parity on inputs no fixture reaches, so ours stays.
- **12.4's destructor for an object a prvalue initialized.** `YA v = YA();` with
  a member that has a destructor is destroyed here and is not by the references,
  which lose the object's lifetime through the elision. Ours stays.
- **Past 64 bytes the zero of a class object is one `zeroinit`**, where the
  references write one eight-byte store however many there are — the deliberate
  divergence the Performance Model names, at the limit 8.5.1p7's array span
  already uses.
- **A conditional over two lvalues of class type is an lvalue here.** We write
  the address of the arm control reached; the references copy each arm into an
  object of their own, which loses what 5.16p4 makes an lvalue. Ours stays.
- **`static_cast<const YA&>(YA(4))` names its temporary `tmpobj`**, where the
  references name it `arg`. A cast is not an argument, and nothing but the name
  differs.
- **An empty `@__cppgm_init`.** The references write one for `YA g;` and none for
  `YA g = YA();`, which are the same program with the same empty body. We write
  one for both, which is what four of their own fixtures do.
- **The references pass a class holding a bit-field by address**
  (`pass=by_address`) where they pass every other class of the same size by
  value. Not about this checkpoint, and no fixture passes such a class.
- **An array subscript converts its index and evaluates it twice**, and **nested
  namespace depth is super-linear** — both unchanged and both owned elsewhere.
- **One class with n friend operator overloads used n times stays quadratic**, at
  n calls each ranking n candidates.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset zero, 10.2p2/p6 lookup through the chain, 11.2p2/p4 and 11.4p1 access, 12.6.2p5 base initialization and 12.4p8 base destruction, 12.1p5/12.4p3 triviality through the base, 4.10p3 / 8.5.3p4 / 5.2.9p11 as one `base-conversion` node with 13.3.3.1.4p1's rank and 13.3.3.2p4's order, 5.9p2 and 5.16p3, the object model split into `sema_class.cpp` | one derived-to-base conversion written as one node per link of the chain where the references write one, at n·d instructions for n accesses d deep; a chain access-checked at its first link only; the conditional's composite pointer type converting neither operand, so a private base was reachable through `?:` and not through `==`; 8.5.3p4's base half of reference-related missing, so `static_cast<Base&>` was refused; 5.2.9p11's reference downcast refused and its pointer downcast unchecked; an inaccessible destructor called for an object, a member and a base; 11.4p1's additional check on a protected member absent; every constructor and destructor named with the complete-object entry, where the references name a base-only one with the base-object entry and no alias; a reference member's binding claiming `projection=reference_field`; a member whose declaring class the walk never reached converted to the last base anyway; three dead helpers and a reordered initializer list left by the split | pa16 126 / 243 held, the same set passing and failing; five `.ref` files now byte for byte identical and the passing fixtures differing from the reference at all down from 39 to 34; pa1–pa15 1173 / 1173; valgrind clean over 243 fixtures and 88 probes; conversion, lifetime, protected-access, chain-depth and access axes all linear at 2.0–2.3× per doubling, and the n·d output blow-up gone (16 012 007 lines in 54.6 s → 20 007 in 2.0 s); file audit passes with the two recorded header-weight warnings; the stripped metadata — now including `projection=` — agrees with the refs but for `unwind=no` |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for, 13.5.7p1's `x++0`, 13.5.3/13.5.4/13.5.5's member-only `= () []`, 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and revealed by 7.3.1.2p3, 11.3p11's elaborated-type-specifier, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward; 3.2p3's uses read from the whole resolved tree | 3.4.2p2's base chain abandoned wherever the class was already associated, so a hidden friend of a base went unfound when a nested type of the derived class was named first; every class around a nested type associated where 3.4.2p2 associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither of the operator-call sites, so a protected member operator was refused where a friend of the derived class named it and accepted where the object was of the base; a member `operator- + * &` encoded with the unary Itanium terminal, because the arity counted the written parameters and 9.3.1p3 had already made the object an operand; an out-of-class definition of a static member function given an object parameter and declared a second function, so the unit called `@YB__f()` and defined `@YB__f(%this)` - and, where the definition was `inline`, defined nothing at all; 13.5p6 written for its non-member half and not its static-member one; a pointer condition branched on through a `cmp ne ptr` the references do not write | pa16 161 -> 163 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; the ADL association axes linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of all sixteen unary/binary forms of `+ - * &` agree with g++ |
| C6 | 9.6p1's width and the four facts it settles on the member's own declaration, 9.6p2's allocation into storage units, the read as a load-shift-mask at the promoted type, the write as a read-modify-write and as a plain store where the initialization owns the unit, 8.5.1's unnamed field, 12.6.2 and 8.5.1's two instruction orders, 5.17 and 5.3.2 over a field, 5.3.1p3 and 5.3.3p1's refusals, 3.6.2p2's static data as the bytes the bits fall in | the allocation unit read as a bit cursor, so a field packed into the bytes of the member before it - `struct { char c; int x : 3; }` 4 bytes where the references write 8, `char a:3; int b:5; char c:2;` 4 where they write 12, and a derived class's field over the base subobject, which its constructor then stored over after the base had run; the unit loaded and put back at 4.5p3's promoted type rather than at the signed integer of its own width, so every `unsigned`, `unsigned char`, `unsigned short`, `bool` and `unsigned long` field wrote a type no reference writes; an initialization joining the unit with raw operations at a member type narrower than `int` where 4.5 promotes each step and 4.7 converts it back; both masks written for a field that owns every bit of its unit; 3.6.2p2's static image folded into `u8` items where a data item names a whole object and the references initialize it before the program runs; an unnamed bit-field stepped over by the clauses where the references let one reach it; an assignment converting its value after naming the object it writes into; a constant initializer converted by an instruction rather than spelled as the value it produces; 4.12's conversion to `bool` compared at 5.14p1's width rather than at the type of what it converts | pa16 173 -> 174 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 87 probes; every bit-field shape the reference accepts now agrees with it byte for byte - 17 layout shapes, 15 declared types, the read, the write, the initialization and the static image; layout, field-count, nesting, access and static-object axes linear at 2.0-2.3x per doubling; file audit passes with the two recorded header-weight warnings; of the 152 passing fixtures with a reference 110 are byte for byte identical and the rest differ only in top-level order, the internal symbol name and `unwind=no` |
| C7 | 12.2p1's prvalue of class type made an object the function holds, with `tmpobj__n` / `arg__n` / `argobj__n` naming its storage; 8.5.3p5's reference bound to it; 13.3.3.1.2p1's user-defined conversion sequence as a converting constructor's temporary, ranked between the standard sequences and the ellipsis; 5.2.2p4's argument of class type copied into a slot the call owns, with 12.8p31 creating a prvalue argument in it; 12.8p15's memberwise copy; 8.5p7's zero of a value-initialized class with no user-provided constructor; the object model of the lowering split into `lowir_lower_object.cpp` | 5.2.2p4's "the call owns the copy" written into `converted`, the conversion every initialization, assignment, return, conditional arm and cast reaches, so each of them allocated a call's `argobj__n` slot and copied twice - 4000 `YA q = p;` at 52 010 lines and 8002 slots for the 40 010 and 4002 the source asks for, and 4000 conditionals at 136 012 and 20 003 for 100 012 and 8003; 8.5.3p5's "named after the argument that asked" written into `apply_conversion`, so `const YA& r = YA(5);` named its storage `arg__1` and `return YA(6);` named it `argobj__1`; 13.3.3.1.2's temporary always named `arg`, returning before the 12.8p31 rule that would have named it `argobj` at the one place that rule was written for; a call returning a class by value handing back a value where an object was needed, so `make().get()` passed an `obj<4x4>` as a `ptr` and `const YA& r = make();` bound the reference to it; a copy of a class whose copy constructor the program wrote written as the copy of its bytes, so `YA q = p;` computed `p.a` where the program says `p.a + 1`; a declaration of a class object naming its address twice | pa16 186 / 243 held, the same set passing and failing; pa1-pa15 1173 / 1173; byte-identical passing fixtures up from 110 to 116 of 164, and what is left differs only in top-level order, the internal symbol name and metadata the comparison ignores; valgrind clean over 243 fixtures and 130 probes; copy, argument, temporary, conversion, return, value-initialization, conditional and class-nesting axes all linear at 4000, and the per-copy slot and instruction growth gone; file audit passes with the two recorded header-weight warnings |

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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis is linear in its size.

| axis | sizes | times |
| --- | --- | --- |
| n declarations `YA q = p;` copying one class object | 500 / 1000 / 2000 / 4000 | 0.04 / 0.06 / 0.10 / 0.17 s |
| n calls passing one class object by value | 500 / 1000 / 2000 / 4000 | 0.04 / 0.05 / 0.07 / 0.12 s |
| n written temporaries each passed to a by-value class parameter | 500 / 1000 / 2000 / 4000 | 0.04 / 0.06 / 0.09 / 0.15 s |
| n arguments reaching a class parameter through 13.3.3.1.2's converting constructor | 500 / 1000 / 2000 / 4000 | 0.04 / 0.05 / 0.09 / 0.15 s |
| n functions each returning a temporary by value, each called once | 500 / 1000 / 2000 / 4000 | 0.05 / 0.09 / 0.16 / 0.29 s |
| n value-initialized 16-byte class objects | 500 / 1000 / 2000 / 4000 | 0.04 / 0.07 / 0.11 / 0.21 s |
| n conditionals over two class lvalues, each initializing an object | 500 / 1000 / 2000 / 4000 | 0.05 / 0.09 / 0.15 / 0.27 s |
| n classes nested one inside the next, the outermost value-initialized and copied | 500 / 1000 / 2000 / 4000 | 0.03 / 0.05 / 0.07 / 0.13 s |

- Every one of those axes writes output exactly linear in its size: at 500 and at
  4000 the line counts are 5010 / 40 010 for the copies, 4021 / 32 021 for the
  by-value calls, 3533 / 28 033 for the temporaries, 8522 / 68 022 for the
  returns, 5508 / 44 008 for the value-initializations and 12 512 / 100 012 for
  the conditionals - eight times the size for eight times the output.
- What the copy finding removed is per copy and not per program. At 4000
  declarations `YA q = p;` the checkpoint wrote 52 010 lines and 8002 slots where
  the source asks for 40 010 and 4002; at 4000 conditionals it wrote 136 012 and
  20 003 where it asks for 100 012 and 8003. The frame of a function with n class
  copies in it no longer grows by n slots the program never named.
- 12.8p25's fact costs one pass at 9.2p2 completion - the same pass 9.2p13's
  layout already makes - and one probe per copy. A class with no copy constructor
  anywhere beneath it pays one flag test.
- 12.2p1's storage for a returned class prvalue is made once, the first time the
  prvalue is read as an object, and every later reader uses the address it was
  made at.

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

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 186 / 243, the same set
  passing and the same failing as at `2609becc`.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over all 243 pa16 fixtures and 130
  synthesized inputs — value-initialization shapes, temporaries, by-value
  arguments, default arguments, converting constructors, returns, conditionals,
  casts, copies, empty classes, unions, bit-fields, bases and the eight scaling
  axes — no error.
- Differential against `pa16/cppgm++-ref` on 130 synthesized programs. Every
  shape both accept now agrees byte for byte but for the ones named above: the
  order a returned prvalue's storage is named in, the conditional over two class
  lvalues, `static_cast<T&>`'s name for its temporary, the empty
  `@__cppgm_init` for `YA g = YA();`, the `zeroinit` limit past 64 bytes, and the
  three the reference and this unit disagree about what a program means —
  8.5p7's zero before a non-trivial default constructor, which the reference does
  not write; 12.4's destructor for an object a prvalue initialized, which the
  reference drops; and `pass=by_address` for a class holding a bit-field.
- Scaling: eight single-axis series at four sizes each, with the output line and
  slot counts at 500 and 4000 recorded beside the times.
- The whole stripped set — `object=`, `binding=`, `linkage=`, `role=`, `unwind=`,
  `projection=`, `effects=`, `capture=`, `access=`, `alias=`, `return=`,
  `keep_alias=`, `trivial_lifecycle=`, `tls_for=`, `prefer_local=` and
  `storage=` — diffed against the reference for all 164 passing fixtures whose
  reference output is not empty. 116 are identical without any stripping, up from
  110; what is left differs only in top-level order, in an internal symbol name
  and in those ignored keys. `pass=`, which the comparison does not strip, agrees
  on every one of them.
