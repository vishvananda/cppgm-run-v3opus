# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C1 + C2, reviewed at `bb4ca83d`.** The architecture holds. A member's place in
its object is settled once, where 9.2p2 completes the class, and every later use
of the member is one read of `SemaEntity::offset` rather than a walk. 9.3.1p3's
object parameter lives in the function's type, so 13.3 ranks the object with the
rest of the arguments and the lowering writes a member call as the call it is.
The lowering reads only resolved facts: it never looks a name up again and never
reads syntax. Demand-driven emission is monotonic and drained between top level
declarations, so no `lowir_model::Function&` is alive while `program_.functions`
grows — checked on mutual recursion between two in-class member functions, which
terminates with each lowered once.

What the review found is nine places where an owner answered one question and
not the one next to it. Five let the output describe a program the source does
not have, two refused a program the milestone supports, one named the wrong
thing in its diagnostics, and one was quadratic.

**1. `int C::s;` defined nothing.** 9.4.2p2 makes the declaration a class writes
of a static data member no definition of it, and C1 wrote that rule as "a
declaration whose region is a class defines nothing". The definition outside the
class declares into that same region, so it fell under the same rule: every
program that defined a static data member emitted `declare global @C__s` and no
storage at all, and would have linked against nothing. What tells the two apart
is the declarator-id, not the region it reaches: a nested-name-specifier is what
9.4.2p2 makes the definition, and that is now what the fact is built from.

**2. A brace-or-equal-initializer on a data member was read and then dropped.**
12.6.2p8 makes it an action of every constructor that does not name the member,
and this milestone writes no action into the constructor 12.1p5 gives a class:
`struct X { int m = 1; }; X x;` compiled to a program that never wrote 1, with
`x.m` read out of storage nothing had initialized. The fact was recorded — 8.5.1p1
reads it, which is what keeps such a class from being an aggregate — but nothing
acted on it, and `trivial_default_construction` said the constructor had nothing
to do. It is now refused where the member is declared, next to the base clause
and the bit-field, named in its diagnostic. The checkpoint that writes
constructor bodies is what lifts it.

**3. `alignas` asked for nothing when its operand was a type.** 7.6.2p1 writes
the operand either way round, and only the constant-expression form was read:
`struct alignas(double) X { char c; };` laid X out with alignment 1, and
`alignas(Y)` for a class Y failed with "Y is not a constant expression" because
the name was parsed as an expression and evaluated. The type-id form asks for the
alignment `alignof` gives that type, so the parse now writes it as that
`alignof` — one expression for the layout to evaluate rather than two forms for
it to tell apart, over the ambiguity `parse_parenthesized_operand` already
resolves for `alignof` itself.

**4. A member access that named no subobject dropped its object expression.**
5.2.5p1 evaluates that expression whatever the member turns out to be, and three
places dropped its node when the member was a static member, an enumerator or a
nested type: the member expression, the callee of a member call, and the call
that found overload resolution had chosen a static member. `C& make(); make().s;`
and `make().f();` each compiled to a program with no call to `make` in it. Each
of the three now asks first whether evaluating the object expression does
anything a program can observe, and refuses rather than dropping it where it
does. The implicit `this` a member function names with no object expression
passes that question, which is what keeps 9.4p1's own case free.

**5. 13.3.1.1.1 could not tell `f()` from `f() const`.** With both declared, a
call on a non-const object was ambiguous and one on a const object worked by
accident, because the two implicit object arguments — `C*` into `C*` and `C*`
into `const C*` — both ranked as exact matches and nothing ordered them.
13.3.3.2p3 orders two sequences that differ only in the qualifiers they add by
whose are the proper subset. A `Match` now carries the type its sequence
qualified the argument into, and `compare_matches` orders two of them with the
4.4p4 rule the layer already had. The same bullet is what orders `f(T&)` above
`f(const T&)`, which was equally ambiguous and now is not.

**6. A member call named `.` in its diagnostics.** The callee of a member call is
a member expression, and every question the call asked — which declaration the
arguments choose, what to say when none does — was asked about the access rather
than about the member. 5.2.5p1 makes the call a call of the member, so that is
what names it.

**7. Naming n slots after one identifier cost n².** 3.3.3p4 lets two slots of one
function be named after one identifier, and the second and later take a suffix.
The suffix was searched from the first every time, so the k-th `x` walked every
`x__shadow` before it. This is PA15 code that C1 made routine: the object model
puts a class object in every block. 4000 blocks each declaring one name took
2.10 s and now take 0.11 s, with the names it chooses unchanged.

**8. 11p6 was read as 11p2 alone.** Access control checked every name against the
scope the name was written in, which is right for a use and wrong for a
declaration: 11p6 checks the names of a declaration with the access the entity
being declared has. The leading return type of `A::I A::f()` and the initializer
of `A::I A::x` were refused where the standard's own example accepts them, and
both are shapes the assignment names. The class the declarator-id reached is now
the context the declaration's names are checked in, and is put back when the
declaration ends. A use written anywhere else is refused as before.

**9. A member function defined outside its class was emitted only if this unit
used it.** 9.3p2 makes a member function inline when it is defined *within* the
class definition, and C1 wrote that as "the region it declares into is a class" —
which an out-of-class definition also satisfies, because 3.4.3p3 makes it declare
into that same class. So `class C { public: int f(); }; int C::f() { return 1; }`
bound weakly and, being weak, waited for a use: a unit that defined the member
and did not call it emitted no definition at all, and the program it belongs to
would have linked against nothing. Where the definition is *written* is what
9.3p2 asks about, and that is now what the fact is built from. This is the
finding the comparison could not have shown: `binding=` is one of the fields the
relaxed LowIR comparison strips, so four fixtures passed while emitting weak
where the reference emits strong.

### Found by the metadata diff and left for a later checkpoint

Diffing the `object=`, `binding=`, `linkage=`, `role=` and `unwind=` of every
passing fixture against its reference — the fields the comparison strips before
it compares — turned up finding 9 and one gap this checkpoint does not own:
**`noexcept` produces no `unwind=no`**. Twenty pa16 references and several pa15
ones carry it, nothing in `dev/src` ever sets `FunctionBoundaryMetadata::unwind`,
and three fixtures pass today while emitting a function boundary that does not
say what the source said. The AST already keeps it (`function-qualifier
noexcept` reaches the declarator), the writer already emits it, and what is
missing between them is a fact on the declaration — a different owner from
anything C1 or C2 touched, which is why it is recorded here rather than fixed.
After the sweep, no other field the comparison ignores differs on any passing
fixture.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 goes 65 / 243 to 70 / 243:
  the two cv-overload tests, the two private-nested-type-in-member-context tests,
  and the static data member address.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. Every refusal this audit added names
  the construct and the clause; three of them replace output that described a
  program the source did not write.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and the
  six synthesized deep and wide probes.
- The metadata the relaxed comparison strips agrees with the reference on every
  passing fixture but the three `noexcept` ones recorded above.
- The file audit passes with two `bad-division` warnings, both the heuristic
  counting declarations rather than bodies. `sema_analyzer.h` was already
  recorded by the PA12 audit; `lowir_lower.h` is new with this checkpoint and is
  171 declaration lines plus seven function bodies — two constructors' member
  init lists and four one-line accessors. Splitting either would divide one pass
  over one shared state across two headers. Recorded rather than worked around.

### Checked and left alone

Two shapes look wrong and are what the checked-in refs require, so neither was
changed:

- A namespace-scope object of a class whose default constructor is trivial emits
  an `@__cppgm_init` with nothing in it. 3.6.2p2 asks for no dynamic
  initialization there, but `100-global-class-zero.ref` and three others write
  the empty helper, so the emission follows the oracle.
- Aggregate initialization names each subobject from the object again, so a
  class nested d deep costs O(d²) index instructions. That is the shape
  `spec/200-aggregate-brace-elision.ref` writes, and the work per emitted line is
  flat (2.4 µs at every depth from 100 to 800).

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |

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
- The relaxed LowIR comparison strips `object=`, `binding=`, `linkage=`, `role=`
  and `unwind=`, so a passing suite says nothing about them. They are diffed
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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis doubles in about 2.1x.

| axis | sizes | times |
| --- | --- | --- |
| members in one class, laid out and aggregate-initialized twice | 2000 / 4000 / 8000 | 0.03 / 0.07 / 0.14 s |
| member accesses and member calls in one body | 1000 / 2000 / 4000 | 0.04 / 0.09 / 0.20 s |
| classes, each with an object and one in-class member call | 500 / 1000 / 2000 | 0.04 / 0.09 / 0.19 s |
| blocks, each declaring one class object | 1000 / 2000 / 4000 | 0.05 / 0.11 / 0.23 s |

- The last axis is the one the audit fixed: it was 0.18 / 0.61 / 2.28 s before,
  and the cost was the slot-name search rather than anything about classes — n
  blocks declaring an `int` were as quadratic as n declaring a class object.
- Nested aggregate depth 100 / 200 / 400 / 800 is 0.01 / 0.05 / 0.20 / 0.80 s for
  5467 / 20917 / 81817 / 323617 emitted lines: quadratic in the lines the
  reference shape asks for, flat per line.
- A member function used n times is lowered once: 1000 calls of one in-class
  member function emit one definition.
- 8.5.1p7's zero-fill bound holds: a struct holding `char buf[1 << 20]`,
  initialized at namespace scope and locally, compiles in under 0.01 s to a
  29-line program with one `zero 1048575` and one `zeroinit 1048575x1`.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 70 / 243, no test that
  passed before this audit fails after it.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over 243 pa16 fixtures and 6 synthesized deep
  and wide probes — no error.
- Scaling: four single-axis series at three sizes each, one depth series at four,
  and the zero-fill bound checked at 2^20 elements.
- `object=`/`binding=`/`linkage=`/`role=`/`unwind=` diffed against the reference
  for all 55 passing fixtures that have one — clean but for the three `noexcept`
  ones recorded above.
