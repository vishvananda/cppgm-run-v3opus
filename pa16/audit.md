# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C6 — 9.6's bit-fields, reviewed at `3711103a`.** The architecture is the right
one and the layer split holds: the four facts a width settles are put on the
member's own declaration where 9.6p1 writes them, `sema_class.cpp` allocates
them once where 9.2p2 completes the class, and the lowering reads
`offset`, `bit_offset`, `bit_width` and `bit_access` and writes a fixed number
of instructions per access. Nothing walks a class to find a field, no second
pipeline appeared, and every use of a field — `.`, `->`, an implicit `this`, a
base's member, a union member, an array element, an operator's operand — reaches
the one lvalue that carries the field and the one read and the one write beside
it.

What the review found is one shape, six times. **The storage unit a field sits
in was read as the member's own object, where it is really a run of bytes the
class hands to several members at once.** A unit has a width, an alignment, a
type the fields sharing it were declared with, and a set of bits that belong to
the members beside this one — and the checkpoint used the member's declared type
for each of those in turn. The ten fixtures all write one run of same-typed
fields in a class with nothing else in it, which is the one shape where the two
readings agree; every shape off it disagreed with the references. Three more
findings stand beside the six, each an exit of a rule the bit-field write walks
through and none of them about bit-fields.

**1. 9.6p2's allocation unit was a bit cursor.** The layout counted bits and put
each field wherever bits stood open, so a field packed into the bytes of
whatever came before it: `struct { char c; int x : 3; }` was 4 bytes with `x`
inside `c`'s storage where the references give `x` a unit of its own at offset 4
and the class 8 bytes; `char a:3; int b:5; char c:2;` was 4 where they write 12;
and an ordinary member after a field began at the next free *byte* rather than
after the unit. The reference allocates a whole storage unit of the field's
declared type and lets the fields declared with that same type share what is
left of it — which `struct D : B { int x : 3; }` makes more than a layout
question: `x`'s unit had covered the base subobject, and because the unit was
this initialization's to claim, `D`'s constructor stored the whole unit over the
base after the base's constructor had run. A unit is now opened by the first
field that cannot share the one before it, and an ordinary member and a field of
another type each begin after it ends, so no member's storage overlaps another's
and 12.6.2p10's order cannot write over what it has already written.

**2. The unit was loaded and put back at 4.5p3's promoted type.** 4.5p3 says
what a *value* read out of a field is worth, which is a question about the
member; the load, the shift, the mask and the store are a question about the
unit, and the references write all four at the signed integer of the unit's own
width. So `unsigned a : 3; a = 1;` wrote `u32` where they write `i32`,
`unsigned char : 3` wrote `u8` for `i8`, `unsigned short : 5` `u16` for `i16`,
and `unsigned long : 5` — whose 4.5p3 promotion is `int`, four bytes narrower
than its unit — was read at a type that does not name its storage at all. What
the value is worth still keeps the declared type, which is what leaves the
conversion above a read the one that type asks for.

**3. An initialization joined the unit with raw operations at the member's
type.** 8.5.1 and 12.6.2 compute the unit as an expression, so a member narrower
than `int` is masked, shifted and joined at `int` with 4.5's promotion and 4.7's
conversion back — which is what the references write, down to the `cmp ne` a
`bool` field's every step converts through. The checkpoint wrote `and u8`,
`shl u8` and `or u8` instead.

**4. A field that owns every bit of its unit still wrote both masks.** Where the
width fills the unit there is nothing of the field to mask off and nothing
beside it to put back, and the references write neither mask nor the `or`: an
`unsigned a : 32` assignment is the load of the unit and a store of the value.

**5. 3.6.2p2's static data was folded into bytes.** A data item names a whole
object, and a bit-field owns a share of one, so the references give any
namespace-scope object whose bit-field a clause reached its value before the
program runs. The checkpoint gathered the fields' bits into `u8` items instead —
a different object image and a `zero`/`u8` shape no reference writes. A field no
clause reached is the zero of its unit, written once for every field in it,
which is what the references write for `S g = {};`.

**6. A clause did not reach an unnamed bit-field.** 8.5.1p1 reads as though it
does not, and the checkpoint stepped over unnamed fields for that reason — but
the references count them among the subobjects the clauses reach, so
`struct { int : 3; int b : 5; }` initialized `{1}` gives the 1 to the unnamed
field here and to `b` there, and `struct { int : 3; } s = {1};` is a translation
unit for them and was not for us. The comparison is byte for byte against their
output, so the clause reaches the field they give it to; a field of width zero
takes a clause and writes nothing.

**7. An assignment converted its value after naming the object it writes
into.** `s.n = v` wrote the address of `n` and then the conversion of `v`; the
references compute the value, conversion and all, and name the place after it.
This is the assignment path itself rather than the field one — a bit-field write
only made it visible, because it names the place twice.

**8. A constant initializer was converted by an instruction.** 8.5 gives an
object the value its initializer is worth *as that object's type*, so
`unsigned char c = 1;` stores `1` where we wrote `convert trunc u8 i32 1` and
stored the result. The same holds for every clause of an aggregate and for a
mem-initializer. An assignment is not an initialization and still converts,
except where the immediate already spells the value it converts to.

**9. 4.12's conversion to `bool` compared at the wrong width.** It was written
as 5.14p1's truth value, which LowIR materializes at `i64` whatever it compared;
the conversion compares at the type of what it converts and reads the result at
the one byte `bool` is stored in. The two share nothing but the `cmp ne`, and
pa15's `lhs && rhs` names the difference: its operand is already a truth value
and is compared at `i64` where a converted `int` is compared at `i32`.

### Left for a later checkpoint

- **The reference refuses `++` and `+=` on a bit-field narrower than `int`.**
  `unsigned char a : 4; ++a;` ends `cppgm++-ref` with "unsupported
  storage-to-value materialization in LowIR [source-lowir i8] [semantic-source
  unsigned char]" — its own unit type reaching a layer that will not read it as
  the member's. We accept it and write the shape every other width gets. Nothing
  can be matched here: there is no output to agree with.
- **A reference and a conditional's arm bind the unit rather than the field.**
  `const unsigned& r = s.a;` and `(c ? s.a : s.b) = 5` are accepted by the
  reference, which binds the address of the storage unit and then reads all of
  it — the wrong value for any field not at bit 0. 8.5.3p5 binds a temporary
  holding the field's value, which needs the materialization C7 is for, so both
  are refused here and named as 9.6p3 gives a bit-field no address. The comma
  operator is the third of these: the reference loses the field through it and
  stores the whole unit; we keep it.
- **A namespace-scope array of a class with a bit-field is refused**, where it
  had been folded into bytes. Its initialization is now the dynamic one the
  references write, and dynamic initialization of an array of class type is the
  gap the failure map already owns with the other four.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 goes from 173 to
  174 / 243: no test that passed before fails after, and
  `100-function-pointer-nested-param-name-shadow` is new, which finding 8 fixes.
  The other findings change the verdict on no fixture, because the ten
  bit-field fixtures all write the one shape the two readings agreed on — which
  is what made this checkpoint worth sweeping against the references shape by
  shape rather than fixture by fixture.
- Of the 152 passing fixtures with a reference output to compare, 110 are byte
  for byte identical, 34 differ only in the order the top-level definitions are
  written in, 2 only in the internal symbol name `lowir.md` makes a presentation
  tie-breaker, and 6 in the `unwind=no` the failure map already owns. Nothing
  else differs, in any field, stripped or not.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. The one refusal this audit adds names
  the construct and the clause; the one it removes is a clause the references
  let reach an unnamed field.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and 87
  synthesized inputs — every layout shape, every declared type, the read, the
  write, the initialization, the static image and the five scaling axes.
- The file audit passes with the same two `bad-division` warnings the C1–C2, C3,
  C4 and C5 audits recorded, both the heuristic counting declarations rather
  than bodies in `sema_analyzer.h` and `lowir_lower.h`.

### Checked and left alone

- **An anonymous `struct` member declares nothing.** `struct S { struct {
  unsigned a; unsigned b; }; unsigned c; };` compiles for the reference and
  `s.a` names nothing here: 9.5p1's injection is written for the anonymous union
  alone. The plan's failure map owns it with the layout half of the same gap,
  and `300-anonymous-bitfield-helper-member` passes only because it names no
  member of one.
- **A member of an anonymous union is addressed without the union's own step.**
  The references write `index` for the union subobject and `index` again for the
  member; we write one. It is the same anonymous-member group and is not about
  bit-fields — a union of ordinary members shows it too.
- **An array subscript converts its index and evaluates it twice.**
  `arr[i].a = 2` writes `convert sext i64 i32` where the references multiply the
  index as it stands, and the read form loads `i` once for the subscript and
  once again for nothing. Neither is about bit-fields — a class with no field
  shows both — and the second is a defect rather than a shape: an index with a
  side effect would take it twice. It owns
  `200-reference-indexed-pointer-member-access` and belongs to the subscript
  path.
- **A class copy is a whole-object load and store where the references write
  `copyobj`.** PA17 owns the value semantics; the shape is the same for a class
  with a bit-field and one without.
- **One class with n friend operator overloads used n times stays quadratic**, at
  n calls each ranking n candidates, unchanged by this audit.
- **Nested namespace depth is super-linear and was before this checkpoint**, and
  belongs to the scope layer.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset zero, 10.2p2/p6 lookup through the chain, 11.2p2/p4 and 11.4p1 access, 12.6.2p5 base initialization and 12.4p8 base destruction, 12.1p5/12.4p3 triviality through the base, 4.10p3 / 8.5.3p4 / 5.2.9p11 as one `base-conversion` node with 13.3.3.1.4p1's rank and 13.3.3.2p4's order, 5.9p2 and 5.16p3, the object model split into `sema_class.cpp` | one derived-to-base conversion written as one node per link of the chain where the references write one, at n·d instructions for n accesses d deep; a chain access-checked at its first link only; the conditional's composite pointer type converting neither operand, so a private base was reachable through `?:` and not through `==`; 8.5.3p4's base half of reference-related missing, so `static_cast<Base&>` was refused; 5.2.9p11's reference downcast refused and its pointer downcast unchecked; an inaccessible destructor called for an object, a member and a base; 11.4p1's additional check on a protected member absent; every constructor and destructor named with the complete-object entry, where the references name a base-only one with the base-object entry and no alias; a reference member's binding claiming `projection=reference_field`; a member whose declaring class the walk never reached converted to the last base anyway; three dead helpers and a reordered initializer list left by the split | pa16 126 / 243 held, the same set passing and failing; five `.ref` files now byte for byte identical and the passing fixtures differing from the reference at all down from 39 to 34; pa1–pa15 1173 / 1173; valgrind clean over 243 fixtures and 88 probes; conversion, lifetime, protected-access, chain-depth and access axes all linear at 2.0–2.3× per doubling, and the n·d output blow-up gone (16 012 007 lines in 54.6 s → 20 007 in 2.0 s); file audit passes with the two recorded header-weight warnings; the stripped metadata — now including `projection=` — agrees with the refs but for `unwind=no` |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for, 13.5.7p1's `x++0`, 13.5.3/13.5.4/13.5.5's member-only `= () []`, 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and revealed by 7.3.1.2p3, 11.3p11's elaborated-type-specifier, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward; 3.2p3's uses read from the whole resolved tree | 3.4.2p2's base chain abandoned wherever the class was already associated, so a hidden friend of a base went unfound when a nested type of the derived class was named first; every class around a nested type associated where 3.4.2p2 associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither of the operator-call sites, so a protected member operator was refused where a friend of the derived class named it and accepted where the object was of the base; a member `operator- + * &` encoded with the unary Itanium terminal, because the arity counted the written parameters and 9.3.1p3 had already made the object an operand; an out-of-class definition of a static member function given an object parameter and declared a second function, so the unit called `@YB__f()` and defined `@YB__f(%this)` - and, where the definition was `inline`, defined nothing at all; 13.5p6 written for its non-member half and not its static-member one; a pointer condition branched on through a `cmp ne ptr` the references do not write | pa16 161 -> 163 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; the ADL association axes linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of all sixteen unary/binary forms of `+ - * &` agree with g++ |
| C6 | 9.6p1's width and the four facts it settles on the member's own declaration, 9.6p2's allocation into storage units, the read as a load-shift-mask at the promoted type, the write as a read-modify-write and as a plain store where the initialization owns the unit, 8.5.1's unnamed field, 12.6.2 and 8.5.1's two instruction orders, 5.17 and 5.3.2 over a field, 5.3.1p3 and 5.3.3p1's refusals, 3.6.2p2's static data as the bytes the bits fall in | the allocation unit read as a bit cursor, so a field packed into the bytes of the member before it - `struct { char c; int x : 3; }` 4 bytes where the references write 8, `char a:3; int b:5; char c:2;` 4 where they write 12, and a derived class's field over the base subobject, which its constructor then stored over after the base had run; the unit loaded and put back at 4.5p3's promoted type rather than at the signed integer of its own width, so every `unsigned`, `unsigned char`, `unsigned short`, `bool` and `unsigned long` field wrote a type no reference writes; an initialization joining the unit with raw operations at a member type narrower than `int` where 4.5 promotes each step and 4.7 converts it back; both masks written for a field that owns every bit of its unit; 3.6.2p2's static image folded into `u8` items where a data item names a whole object and the references initialize it before the program runs; an unnamed bit-field stepped over by the clauses where the references let one reach it; an assignment converting its value after naming the object it writes into; a constant initializer converted by an instruction rather than spelled as the value it produces; 4.12's conversion to `bool` compared at 5.14p1's width rather than at the type of what it converts | pa16 173 -> 174 / 243, no test that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 87 probes; every bit-field shape the reference accepts now agrees with it byte for byte - 17 layout shapes, 15 declared types, the read, the write, the initialization and the static image; layout, field-count, nesting, access and static-object axes linear at 2.0-2.3x per doubling; file audit passes with the two recorded header-weight warnings; of the 152 passing fixtures with a reference 110 are byte for byte identical and the rest differ only in top-level order, the internal symbol name and `unwind=no` |

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
end of the audit. Every axis doubles in about 2.0x-2.3x.

| axis | sizes | times |
| --- | --- | --- |
| n one-bit fields in one class, aggregate-initialized then assigned one by one | 500 / 1000 / 2000 / 4000 | 0.02 / 0.05 / 0.10 / 0.23 s |
| n fields of four alternating declared types, so every field opens its own unit | 500 / 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.07 / 0.12 s |
| n classes nested one inside the next, each with two fields | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.06 / 0.13 s |
| n reads of two fields of one class in one body | 500 / 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.08 / 0.16 s |
| n namespace-scope objects of a class with fields, each dynamically initialized | 500 / 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.10 / 0.20 s |

- The layout finding made the walk no more expensive: what it carries is a byte
  cursor and the open unit's type, start and used bits, so a field costs one
  comparison and one addition and a class with no bit-field never opens a unit.
  The alternating-type axis is the worst case - every field opens a unit - and is
  the fastest of the five.
- Neither the access type nor the promoted arithmetic adds a walk: both are read
  off the declaration, and the operations a write emits are a fixed number per
  written field, at most two more than before where the member is narrower than
  `int`.
- Static objects with bit-fields are initialized before the program runs rather
  than folded, which moves work from the data image to `@__cppgm_init` and is
  linear in the fields written: 4000 objects of a two-field class, 0.20 s.
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

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 174 / 243, from 173 before
  the audit: `100-function-pointer-nested-param-name-shadow` newly passes and
  nothing that passed before fails.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over all 243 pa16 fixtures and 87
  synthesized inputs — layout, declared type, read, write, initialization,
  static image, base, union, enum, array and scaling shapes — no error.
- Differential against `pa16/cppgm++-ref` on 87 synthesized programs: every one
  the reference accepts and this unit accepts now agrees with it byte for byte,
  but for the four shapes named above — the anonymous member, the anonymous
  union's own step, the array subscript and the class copy — none of which is
  about bit-fields, and the three the reference and this unit disagree about
  what a program means: `++` on a narrow field, which the reference refuses; a
  reference or a conditional bound to a field, which it binds to the unit; and a
  field named through a comma, which it forgets is one.
- Scaling: five single-axis series at four sizes each.
- The whole stripped set — `object=`, `binding=`, `linkage=`, `role=`, `unwind=`,
  `projection=`, `effects=`, `capture=`, `access=`, `alias=`, `return=` and
  `keep_alias=` — diffed against the reference for all 152 passing fixtures whose
  reference output is not empty. 110 are identical without any stripping; what
  is left is the six `unwind=no` fixtures already recorded, 34 that differ only
  in top-level order and 2 in an internal symbol name.
