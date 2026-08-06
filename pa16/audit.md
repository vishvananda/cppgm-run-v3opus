# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C5 — 13.5 operator overloading, 3.4.2 ADL and 11.3 friends, reviewed at
`1bd1885f`.** The architecture holds and is the right one. An operator
expression whose operand has class or enumeration type gathers one candidate
set, resolves it with the same 13.3 a written call uses, and rewrites its own
operand nodes into the `call-expression` node the call path already writes — so
nothing new reaches the lowering, and every question about the call is asked in
one place. 11.3p6's friend declaration is a member of the region around the
class that binds no name there, held on the class for 3.4.2p2 and moved into the
region's own chain when 7.3.1.2p3's matching declaration reveals it; that is one
fact in one owner rather than a second visibility rule. `sema_operator.cpp` owns
both halves — which declarations a use of a name reaches, and how an operator
expression becomes the call it stands for — and is the right size for them.

What the review found is two shapes. **The first is a question asked of one
walk that is really two questions**, and it is both of the ADL findings: the set
of associated classes was read as if every class in it had had its bases walked,
and the walk up to the innermost enclosing namespace was read as if every class
it climbed through were the class the type is a member of. **The second is the
one this audit has found at every checkpoint: a rule written for the exits it
had in hand and not for the one beside them.** 13.3.1.2's operator call is the
third way a member is named on an object and asked neither of the two questions
`.` and `->` ask about that object; 13.5p6 is one clause with two halves and only
one was written; 9.4.1p2's `static` is written in one place and was read in
another; and the arity that tells `-` written for one operand from `-` written
for two was counted without the operand 9.3.1p3 had already put in the type.

**1. 3.4.2p2's base chain was abandoned wherever the class was already
associated.** `associate_type` walked the chain while the class was not already
in the set — but a class also enters that set as *the class a nested type is a
member of*, and that path associates none of its bases. So
`g(e, d)` with `e` of type `NS::YDer::E` and `d` of type `NS::YDer` associated
`YDer` from the enum, stopped at once on the class argument, and never reached
`YBase` — a hidden friend declared there was not found, while `g(1, d)`, whose
first argument associates nothing, found it. What stops the walk is
now having walked that class's chain, which is a different fact from having the
class in the set, and both are probes: gathering costs the classes the argument
types reach rather than their square.

**2. Every class around a nested type was associated, not the one it is a member
of.** 3.4.2p2 associates the class itself, its base classes, and the class it is
a member of; the classes *that* class is in turn a member of are not
associated. `associate_region`
pushed each class scope it climbed through on its way to the namespace, so a
friend of `YA` was visible to a call whose argument is a `YA::YB::YC` — which
g++ refuses. Only the innermost is taken now; the walk still climbs past the
rest, because what it is looking for beyond them is the one namespace they
stand in.

**3. An operator that names a member on an object asked neither of the two
questions `.` asks about that object.** The C4 audit wrote 11.2p5's naming class
and 11.4p1's additional check at the two member-access sites. 13.3.1.2's call is
the third, and `operator_expression` called `require_access` with no naming
class and never called `require_protected_object` at all. Both halves were
wrong in opposite directions:
`struct YB { protected: int operator+(int) const; }; struct YD : YB { friend int
q(YD&); }; int q(YD& d) { return d + 1; }` was refused although 11.2p5's naming
class grants it, and
`struct YO : YB { int t(YB& o) { return o + 1; } }` was accepted although 11.4p1
forbids naming the member on an object of the base. Every member operator form —
`+`, `[]`, `()`, `=` — was affected, and the plain member call beside each was
already right. 13.3 has chosen by the time the questions are asked, so they are
asked of the one declaration rather than of everything the lookup reached.

**4. A member `operator-` was encoded with the unary Itanium code.** `-` written
for one operand and `-` written for two are different terminals, told apart by
how many operands the declaration takes — and 9.3.1p3 had already made the
object one of them. `abi_symbol_of` passed the count of *written* parameters, so
`Date::operator-(const Period&) const` was named `_ZNK4DatengERK6Period` where
`300-member-vs-nonmember-operator-implicit-object-cv-rank.ref` and g++ write
`_ZNK4DatemiERK6Period`. `object=` is one of the fields the relaxed comparison
strips, so that fixture passed while claiming a symbol no other translation unit
could reach. `+`, `*` and `&` had the same shape. All sixteen unary and binary
member and non-member forms of the four now agree with g++ symbol for symbol.

**5. An out-of-class definition of a static member function declared a second,
non-static function.** 9.4.1p2 makes `static` a specifier of the declaration
written in the class and forbids repeating it outside, and
`with_object_parameter` read only the specifiers this declarator wrote — so
`int YB::f() {}` for `static int f();` was given an object parameter and became
a different entity from the one the class declares. The unit then emitted
`@YB__f(%this : ptr)` and called it with no argument, and where the definition
was also `inline` it was never emitted at all: the output called a function the
unit has the definition of and does not define, which is what
`300-lazy-nested-class-enclosing-alias-lookup` failed LowIR validation on. Which
kind of member a qualified declarator declares is now read from the declaration
in the class it redeclares, as a probe on the chain that name heads.

**6. 13.5p6 was written for one of its two halves.** A non-member operator
function with no operand of class or enumeration type was refused; a `static`
member one — which the clause leaves no room for either — was accepted.

**7. A pointer condition was branched on through a comparison the references do
not write.** `truth_for_branch` wrote `cmp ne ptr %p, 0` before every branch on
a pointer. Fifteen reference outputs branch on the pointer directly, and the one
`pa15` reference that writes the comparison writes it for a `!= 0` the source
spelled. 4.12p1's conversion to `bool` needs no instruction where a terminator
is the only thing that reads it. A floating value still compares, because its
zero is not the zero bit pattern the terminator tests.

### Left for a later checkpoint

- **13.5.6's `operator->` is not read as a call.** `q->a` where `q`'s class
  declares `YB* operator->()` is refused as "`->` is written on an operand that
  is not a pointer to a class". `[]`, `()` and `=` are each routed to 13.3.1.2
  from their own expression; `->` is the one member operator that is not, and
  its rule is the only one that applies itself again to what it returned. No
  fixture writes one — `300-overloaded-arrow-star-operator` is `->*`, an
  ordinary non-member binary operator, and passes. Recorded rather than fixed
  here: it belongs to the member-access path, with `x.YB::b`.
- **A static and a non-static member of one class can reach the same overload
  key.** `struct block { void unlink(); static void unlink(block*); };` is well
  formed — the parameter type lists 13.1 tells the two apart by are `()` and
  `(block*)` — but 9.3.1p3 put the object parameter in the type, so both reach
  `(block*)` and the second is refused as a redefinition.
  `200-static-nonstatic-same-pointer-signature` needs it. The fix is one move:
  which kind of member a declaration is belongs in the key the chain is indexed
  by, beside the parameter list. It predates C5 and is the one place the
  object-parameter decision does not pay for itself.
- **`x.YB::b` still names no member**, unchanged from the C4 audit, and
  **`alignas` on a member is still dropped**, which the plan's failure map owns
  with `alignof`.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 goes from 161 to
  163 / 243: no test that passed before fails after, and
  `200-reference-member-conditional-lvalue` and
  `300-lazy-nested-class-enclosing-alias-lookup` are new — the first needs
  finding 7 and the second needs 5 and 7 together. The other five change the
  verdict on no fixture, because no fixture writes the programs they are about
  — except the fourth, whose fixture passed while claiming a symbol on a field
  the comparison strips.
- Of the 141 passing fixtures with a reference output to compare, 34 differ from
  it only in the order the top-level definitions are written in, 2 only in the
  internal symbol name `lowir.md` makes a presentation tie-breaker, and 6 in the
  `unwind=no` the failure map already owns. Nothing else differs, in any field,
  stripped or not.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. Each refusal this audit added — a
  protected operator named on an object of the base, a static member declared an
  operator function — names the construct and the clause. The refusal it removed
  is one 11.2p5 grants.
- Demand-driven emission is unchanged and still monotonic: an unused hidden
  friend operator is not emitted, one reached only from a body no written use
  asks for is, and a definition a written body asked for still stands where that
  body asked for it.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and 47
  synthesized probes — operator, friend, reveal, ADL association, static-member
  definition, condition and sweep shapes.
- The file audit passes with the same two `bad-division` warnings the C1–C2, C3
  and C4 audits recorded, both the heuristic counting declarations rather than
  bodies in `sema_analyzer.h` and `lowir_lower.h`.

### Checked and left alone

- **`operator<<=` and `operator>>=` cannot be declared.** `is_operator_token`
  leaves both out, so `int operator<<=(const YQ&, int);` is not a translation
  unit. `pa16.gram`'s `operator-token` leaves them out too, and the README makes
  the grammar authoritative for source syntax — so the parser is right, and
  `sema_operator.cpp`'s table simply holds two spellings no declaration reaches.
- **`x = y` for a class with no user-declared `operator=` is lowered as a whole
  object load and store.** Copy assignment is out of scope for this milestone.
  It is the one built-in operator exit that accepts a class operand — `+`, `+=`,
  `-`, `++`, `[]`, `()`, `==`, `!`, `&&` each refuse one — and what it writes is
  what a trivially copyable class's implicit copy assignment does. PA17 owns the
  value semantics that say whether it should be written at all.
- **13.6's built-in operator candidates are never ranked against the declared
  ones.** Where nothing in the candidate set is viable, the caller reads the
  operator as the built-in one it would have been; where something is viable, it
  wins. Within this milestone that is the same answer 13.3 would reach, because
  no conversion function can make a built-in candidate the better match.
- **One class with n friend operator overloads used n times stays quadratic**, at
  n calls each ranking n candidates: 250 / 500 / 1000 / 2000 take
  0.05 / 0.15 / 0.62 / 2.99 s, unchanged by this audit.
- **Nested namespace depth is super-linear and was before this checkpoint.** 500
  / 1000 / 2000 / 4000 namespaces deep with one ADL call at the bottom take
  0.01 / 0.02 / 0.07 / 0.24 s, the same before the audit as after. The cost is
  the lookup walking enclosing regions; it belongs to the scope layer.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset zero, 10.2p2/p6 lookup through the chain, 11.2p2/p4 and 11.4p1 access, 12.6.2p5 base initialization and 12.4p8 base destruction, 12.1p5/12.4p3 triviality through the base, 4.10p3 / 8.5.3p4 / 5.2.9p11 as one `base-conversion` node with 13.3.3.1.4p1's rank and 13.3.3.2p4's order, 5.9p2 and 5.16p3, the object model split into `sema_class.cpp` | one derived-to-base conversion written as one node per link of the chain where the references write one, at n·d instructions for n accesses d deep; a chain access-checked at its first link only; the conditional's composite pointer type converting neither operand, so a private base was reachable through `?:` and not through `==`; 8.5.3p4's base half of reference-related missing, so `static_cast<Base&>` was refused; 5.2.9p11's reference downcast refused and its pointer downcast unchecked; an inaccessible destructor called for an object, a member and a base; 11.4p1's additional check on a protected member absent; every constructor and destructor named with the complete-object entry, where the references name a base-only one with the base-object entry and no alias; a reference member's binding claiming `projection=reference_field`; a member whose declaring class the walk never reached converted to the last base anyway; three dead helpers and a reordered initializer list left by the split | pa16 126 / 243 held, the same set passing and failing; five `.ref` files now byte for byte identical and the passing fixtures differing from the reference at all down from 39 to 34; pa1–pa15 1173 / 1173; valgrind clean over 243 fixtures and 88 probes; conversion, lifetime, protected-access, chain-depth and access axes all linear at 2.0–2.3× per doubling, and the n·d output blow-up gone (16 012 007 lines in 54.6 s → 20 007 in 2.0 s); file audit passes with the two recorded header-weight warnings; the stripped metadata — now including `projection=` — agrees with the refs but for `unwind=no` |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for, 13.5.7p1's `x++0`, 13.5.3/13.5.4/13.5.5's member-only `= () []`, 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and revealed by 7.3.1.2p3, 11.3p11's elaborated-type-specifier, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward; 3.2p3's uses read from the whole resolved tree | 3.4.2p2's base chain abandoned wherever the class was already associated, so a hidden friend of a base went unfound when a nested type of the derived class was named first; every class around a nested type associated where 3.4.2p2 associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither of the operator-call sites, so a protected member operator was refused where a friend of the derived class named it and accepted where the object was of the base; a member `operator- + * &` encoded with the unary Itanium terminal, because the arity counted the written parameters and 9.3.1p3 had already made the object an operand; an out-of-class definition of a static member function given an object parameter and declared a second function, so the unit called `@YB__f()` and defined `@YB__f(%this)` - and, where the definition was `inline`, defined nothing at all; 13.5p6 written for its non-member half and not its static-member one; a pointer condition branched on through a `cmp ne ptr` the references do not write | pa16 161 -> 163 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; the ADL association axes linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of all sixteen unary/binary forms of `+ - * &` agree with g++ |

## Durable architecture decisions

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
  `alias=` and `return=`, and canonicalizes internal symbol names and top-level
  order, so a passing suite says nothing about any of them. They are diffed
  against the references directly, once per audit.
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
  pattern the terminator tests.

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis doubles in about 2.0x-2.4x.

| axis | sizes | times |
| --- | --- | --- |
| one call, n arguments of n distinct associated classes | 500 / 1000 / 2000 / 4000 | 0.02 / 0.06 / 0.12 / 0.29 s |
| base-chain depth, one ADL call with two arguments of the deepest class | 500 / 1000 / 2000 / 4000 | 0.00 / 0.01 / 0.03 / 0.06 s |
| n ADL calls, each argument four classes above the friend | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.05 / 0.10 s |
| n calls of the shape finding 1 restored: a nested enum, then a class eight deep | 500 / 1000 / 2000 / 4000 | 0.01 / 0.03 / 0.06 / 0.11 s |
| that shape by chain depth, one call | 500 / 1000 / 2000 / 4000 | 0.00 / 0.01 / 0.03 / 0.08 s |
| chained `operator<<` | 500 / 1000 / 2000 / 4000 | 0.00 / 0.01 / 0.01 / 0.03 s |
| operator nesting depth | 500 / 1000 / 2000 / 4000 | 0.01 / 0.01 / 0.03 / 0.06 s |
| n namespaces each with a class and an ADL free function, one use each | 500 / 1000 / 2000 / 4000 | 0.04 / 0.10 / 0.21 / 0.43 s |
| n friend declarations of n names revealed by as many namespace-scope definitions | 500 / 1000 / 2000 / 4000 | 0.03 / 0.07 / 0.15 / 0.35 s |

- The two association findings were correctness rather than cost, and the walk
  they left is cheaper than the one they replaced: membership in the associated
  set and "this class's chain has been walked" are two probes where the first was
  a scan of a vector that grows with the classes the argument types reach. A call
  with 4000 arguments of 4000 distinct classes gathers its set in 0.29 s.
- Measured against the pre-audit binary at `1bd1885f`, no axis moved: nested
  namespace depth 4000 is 0.23 s before and 0.22 s after, 4000 associated
  namespaces 0.43 s and 0.43 s, chain depth 4000 0.06 s and 0.06 s, 4000 ADL
  calls four deep 0.09 s and 0.10 s.
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

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 163 / 243, from 161 before
  the audit: `200-reference-member-conditional-lvalue` and
  `300-lazy-nested-class-enclosing-alias-lookup` newly pass and nothing that
  passed before fails.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over all 243 pa16 fixtures and 47 synthesized
  probes — operator, friend, reveal, ADL association, static-member definition,
  condition and sweep shapes — no error.
- Scaling: nine single-axis series at four sizes each, one quadratic series at
  four sizes, and each of four axes measured against the pre-audit binary at
  `1bd1885f` built from a worktree.
- The whole stripped set — `object=`, `binding=`, `linkage=`, `role=`, `unwind=`,
  `projection=`, `effects=`, `capture=`, `access=`, `alias=`, `return=` and
  `keep_alias=` — diffed against the reference for all 141 passing fixtures whose
  reference output is not empty. What is left is the six `unwind=no` fixtures
  already recorded. The ABI names of every unary and binary member and
  non-member form of `+ - * &`, and of the twenty-six other overloadable
  operators the grammar admits, were diffed against g++ on the same source: all
  agree.
