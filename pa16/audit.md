# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C3 — user-declared constructors and destructors, reviewed at `8c1e3fed`.** The
architecture holds. A constructor and a destructor are read where they are
written, chained on the class rather than bound to a name a lookup reaches, and
13.3.1.3 walks that chain from what the initializer wrote. 12.6.2p10's order is
one pass over `Scope::declarations` and 12.4p8 walks the same list backwards, so
neither order is a search. The mem-initializer that names each member is indexed
once per constructor rather than scanned per member. The lowering still reads
only resolved facts: `constructor-action`, `destructor-action` and
`member-initialization` are typed nodes, and no name is looked up twice. One body
stands under the complete-object and base-object ABI symbols, which is right
while no base is virtual.

What the review found is nine places where an owner answered one question and
not the one next to it, and they are almost all one question: **which region ends
an object's lifetime, and does every way out of that region run it.** C3 wrote
the answer for a block falling through and for a `return`; six of the nine are
the exits and the regions it did not write. Two more let the output describe a
program the source did not have, and one refused a program the milestone
supports.

**1. `break` ended no lifetime.** 6.6.1p1 passes control to the statement after
the loop or switch, so 3.8p1 destroys the objects of every block between. The
analysis wrote destructor actions for a block's fall-through and for a `return`
and for nothing else: `while (c) { YG g; if (...) break; ... }` destroyed `g` on
the path that fell out of the block and not on the one that broke, and
`switch (i) { case 0: { YG g; break; } }` destroyed it on no path at all. The
frames a jump leaves are now what the statement it jumps out of recorded when it
was entered, and one `leave_lifetimes(depth, line)` writes them, innermost block
first — the same walk `return` already used with a depth of zero.

**2. `continue` ended no lifetime.** 6.6.2p1 is the same rule with a different
landing place: control reaches the loop-continuation portion, which is inside the
loop, so the blocks the body opened are left and the loop's own region is not.
`while (c) { YG g; ++i; continue; }` destroyed `g` nowhere. It now leaves exactly
the frames opened since the loop began.

**3. `goto` ended no lifetime, and cannot be told which ones to end.** 6.6.4p2
makes the blocks a goto leaves a question about where its label is, and a label
6.1p1 puts anywhere in the function may not have been reached yet by a single
forward walk. `{ YG g; goto done; } done:` destroyed `g` nowhere. This milestone
does not model the answer, so the jump is refused where it is written, with the
clause and the construct named, rather than written as a jump that ends no
lifetime. A goto with no object of class type alive anywhere in the function is
untouched, which is every goto pa1–pa15 writes.

**4. The for-init-statement's objects belonged to the block around the loop.**
6.5.3p1 puts the for-init-statement in the region the for statement itself opens,
and that region had no lifetime frame, so `for (YG g; ...) { }` put `g` in the
enclosing block's frame: it outlived the loop, and `for (YG g; ...) {} YG h;`
destroyed `h` before `g` where the standard destroys `g` before `h` is
constructed. The for statement now opens its own frame and closes it after the
loop, which is also where a `break` lands — so a break leaves the body's blocks
and the loop's region is closed once, on every path out. The `if`, `while` and
`switch` condition regions are left alone: 6.4p4 contextually converts a
condition to `bool`, which for a class type needs the conversion function 12.3.2
this milestone is explicitly without, so no object with a destructor can stand
there.

**5. An object with static storage duration that a class declared was never
destroyed.** 3.7.1 gives every object that is not local static storage duration,
and C3 asked "is the region a namespace?" instead. The static data member 9.4.2p2
defines outside its class declares into the class, so `YG YH::g;` was constructed
in `@__cppgm_init` and destroyed nowhere. The same question, asked once, also
answers the block-scope `static`, which was landing in the block's frame and
being written as its automatic object — one object of the program, re-created and
destroyed on every call. 3.7.1p3's storage duration and 6.7p4's guard are not
part of this milestone, so that one is refused where it is declared.

**6. An aggregate was never destroyed.** 8.5.1's braced-init-list path skips the
constructor, and the lifetime was recorded inside the branch that calls one, so
`struct YO { YI a; }; YO o = {{1}};` — with `YI` holding a destructor — ran no
destructor at namespace scope or in a block. Which region ends an object's
lifetime is a fact about the object, not about the form its initializer took, so
it is now recorded for every definition of class type before either path writes
the initialization.

**7. `this` in a destructor was `const volatile C*`.** 12.4p12 lets a destructor
be invoked for an object of any cv-qualified version of its class, and C3 said
that by qualifying the object parameter — which is right, and is what lets a
`const` object be destroyed. But 9.3.2p1 gives `this` the type the function's own
cv-qualifier-seq says, and 12.4p1 gives a destructor none: `~C() { m = 0; }` was
refused with "the left operand of an assignment is not a modifiable lvalue". The
two are now separate facts — the parameter says which objects the destructor may
be called for, `this_type` says what its body may do to the one it is destroying.

**8. A deleted destructor was called anyway.** 8.4.3p2 makes naming a deleted
function ill formed and 12.4p11 makes declaring the object name its destructor,
but only the constructor side checked: `struct YG { ~YG() = delete; }; YG g;`
emitted a `declare function` for the deleted destructor and a call of it, so the
program would have linked against nothing. It is refused where the object is
declared.

**9. `= YC(1)` was refused when the constructor was `explicit`.** 8.5.4p3 refuses
a copy-list-initialization that chooses an `explicit` constructor, and C3 keyed
that on the `=` alone. But `=` writes three different initializations: `= {…}` is
the copy-list-initialization p3 is about, `= e` is the copy-initialization
13.3.1.4 answers by leaving the `explicit` constructors out of the candidates,
and `= T(…)` is the direct-initialization 12.8p31 elides into. Only the first is
now refused; `YC x = YC(1);` compiles, as it must.

### Also found, and fixed with them

- **A mem-initializer that named nothing was dropped.** 12.6.2p2 makes the
  mem-initializer-id name a member or a base, and the map from name to
  initializer was only ever read from the member side, so `YC() : b(1) {}` for no
  member `b` compiled to a constructor with no trace of `b(1)` in it — the same
  dropped-operand shape as C1's finding 4. Every entry is now required to have
  been reached, and one written twice is refused where 12.6.2p6 refuses it,
  rather than the second silently being the one with no effect.
- **A constructor the class only declared bound weakly.** 9.3p2 makes a member
  function inline when it is *defined* in the class body, and C3 set
  `inline_function` on every special member declaration. `Box(int, const char*,
  int);` with no definition emitted `declare function … [binding=weak]` where
  the reference emits `strong` — C1's finding 9 again, in the path C3 added.
  8.4.2p1's explicitly-defaulted-on-its-first-declaration case is what keeps
  `= default` inline.
- **Two names could flatten to one internal symbol.** C3 wrote `~` as `_` in
  `flatten_name` because dropping it left `C::C` and `C::~C` one symbol. Every
  other character an identifier cannot hold is still dropped, which leaves
  `C::operator+` and `C::operator-` one symbol too, and disagrees with the
  reference spelling (`@X__operator_`, `@X__operator__`). One `_` per character
  is now the rule, which is what the references write and what `~` was already
  a special case of.
- **A jump asked a question that walked every open block.** The goto refusal
  needs to know whether any object with a destructor is alive; asking it by
  walking `lifetimes_` costs the depth of the blocks around each goto. The count
  is carried instead. Measured before the change on 4000 nested blocks each
  holding a goto: the cost was the pre-existing nested-scope cost, not this
  question, and both binaries agree at 0.96 s — but the carried count is what
  keeps it that way when an object stands in each block.

### Left for a later checkpoint

**`unwind=no` still has no owner.** Diffing the fields the relaxed comparison
strips against the references leaves six passing fixtures differing, all in this
one field, and the sweep now separates them into two owners rather than one:

- 15.4p14's implicit exception-specification for the special members C3 declares:
  `struct X { int m = 1; };` writes no `noexcept` anywhere and its reference
  still gives the implicit constructor `unwind=no`. That is this checkpoint's
  subject, and it needs the exception-specification model 15.4 asks for, which
  is the same model the `eh_cleanup`/`eh_try`/`resume` regions in the failure map
  need. One checkpoint owns both.
- The direct `noexcept` on a declarator (`void touch() noexcept;`), which the
  README puts in the tested metadata path, and which needs a fact on the
  declaration carried out of the shared PA11 declarator reader.

Two of the six are `noexcept(true && !false)` and `noexcept(1)`, which the README
says explicitly may lower without `unwind=no`. Emitting nothing is silence rather
than a false claim — `CUM_DEFAULT` writes no field — which is why this is
recorded rather than worked around. Every other field the comparison ignores now
agrees with the reference on every passing fixture, including the `object=` names
and `alias object` lines C3 added.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 holds at 102 / 243: no
  test that passed before this audit fails after it, and none of the nine
  findings is a shape a fixture covers, which is why the count does not move.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. The three refusals this audit added —
  `goto` past a live object, a block-scope `static` object, an object whose
  destructor is deleted — each name the construct and the clause, and each
  replaces output that described a program the source did not write.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and 30
  synthesized deep, wide and jump-shaped probes.
- The file audit passes with the same two `bad-division` warnings recorded by the
  C1–C2 audit, both the heuristic counting declarations rather than bodies in
  `sema_analyzer.h` and `lowir_lower.h`. Neither grew a body this checkpoint.

### Checked and left alone

- A namespace-scope object of a class whose default constructor is trivial still
  emits an `@__cppgm_init` with nothing in it, because `100-global-class-zero.ref`
  and three others write the empty helper. Unchanged from the C1–C2 review.
- Aggregate initialization still names each subobject from the object again, at
  O(d²) index instructions for depth d, because that is the shape
  `spec/200-aggregate-brace-elision.ref` writes.
- Nested block scopes cost more than linearly in their depth: 1000 / 2000 / 4000
  nested blocks holding one scalar each take 0.02 / 0.06 / 0.23 s, and nested
  `for` statements the same. The pre-audit binary measures identically on both,
  and a control with no class object anywhere measures the same as one with them,
  so this is the PA11/PA12 lookup walking enclosing regions rather than anything
  the object model added. Recorded rather than changed: it belongs to the scope
  layer, not to this checkpoint.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |

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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis doubles in about 2.1x–2.3x.

| axis | sizes | times |
| --- | --- | --- |
| mem-initializers in one constructor | 1000 / 2000 / 4000 | 0.02 / 0.03 / 0.07 s |
| default member initializers in one class | 1000 / 2000 / 4000 | 0.01 / 0.03 / 0.06 s |
| locals with destructors in one block | 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.09 s |
| namespace-scope objects constructed and destroyed | 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.09 s |
| namespace-scope aggregates whose member has a destructor | 1000 / 2000 / 4000 | 0.02 / 0.04 / 0.09 s |
| loops, each with a `break` leaving one object | 1000 / 2000 / 4000 | 0.08 / 0.16 / 0.33 s |
| constructor overloads chosen between for one call | 500 / 1000 / 2000 | 0.01 / 0.02 / 0.06 s |
| nested blocks a single `break` unwinds | 200 / 400 / 800 | 0.00 / 0.01 / 0.04 s |

- The break-unwind depth series is the one the audit added: 3.8p1 makes one break
  out of d nested blocks d destructor calls, and d = 800 emits 800 of them in
  0.04 s. The block closes after the break are unreachable and the lowering drops
  them, so the emitted program has d calls rather than 2d.
- Nested block scopes are super-linear in their depth and were before this
  checkpoint: 1000 / 2000 / 4000 nested blocks holding one scalar each take
  0.02 / 0.06 / 0.23 s, nested `for` statements holding one class object each
  take 0.15 / 0.51 / 1.21 s, and the pre-audit binary measures 0.24 s and 1.20 s
  on the same two inputs. The cost is the lookup walking enclosing regions, not
  the object model, and is recorded rather than changed.
- Measured earlier and unchanged: 8000 members laid out and initialized twice,
  0.14 s; 4000 member accesses and calls in one body, 0.20 s; 2000 classes each
  with an object and a member call, 0.19 s; nested aggregate depth 800, 0.80 s
  for 323617 lines; a member function used n times lowered once; 8.5.1p7's
  zero-fill bound at 2^20 elements in under 0.01 s.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 102 / 243, the same set of
  tests as before the audit: none newly failing, none newly passing.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over 243 pa16 fixtures and 30 synthesized
  probes — deep, wide, jump-shaped and multi-unit — no error.
- Scaling: eight single-axis series at three sizes each, plus two depth series
  and a control pair measured against the pre-audit binary.
- `object=`/`binding=`/`linkage=`/`role=`/`unwind=`/`keep_alias=` and the
  `alias object` lines diffed against the reference for all 80 passing fixtures
  that have one — clean but for the six `unwind=no` ones recorded above.
