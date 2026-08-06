# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C4 — single inheritance, reviewed at `cc855b17`.** The architecture holds, and
it is the right one. What a class derives from is one fact on the declaration:
`SemaEntity::base` for layout, construction, destruction and conversion, and
`Scope::base` for the one edge 10.2's lookup follows. Every question reads that
fact rather than the base-clause syntax, so the base subobject is asked about in
the same words as a member or an object of its own. The object model moved into
`sema_class.cpp` and the split is clean: `sema_scope` owns lookup, `sema_class`
owns what a class is and what its objects hold, `sema_expression` writes one
`base-conversion` node, and the lowering reads only that node. Layout is one
pass at 9.2p2 completion, and a program with no inheritance pays one null test
per region for all of it.

What the review found is mostly one shape, in seven places: **a rule the
checkpoint wrote for the exit it had in hand was not written for the exit beside
it.** Five of the seven are the derived-to-base conversion itself — not written,
written once per link of the chain, or written without the check 11.2 asks for —
and two are the access checks around it. The eighth is what the output *claims*
about the ABI entry a body stands under, which the relaxed comparison strips and
which therefore no test result had ever said anything about.

**1. The chain was walked one node per link where the references write one.**
10.2 reaches a member declared in a grandparent through the classes between, and
`object_in_declaring_class` wrote a `base-conversion` for each of them, so
`x.v` at depth d emitted d `index [projection=base_subobject]` instructions.
The references write one:
`200-friend-intermediate-derived-protected-base-method.ref` reaches
`archive_base` from `archive` through `archive_impl` with a single
index, and no reference anywhere writes two in a row. A base subobject begins
where its derived object does, so the whole conversion is one node. The walk now
says which class was reached and 4.10p3 writes the one node every other
derived-to-base conversion writes. That was also an output-size blow-up: at
depth 4000 with 4000 accesses the old binary emitted 16 012 007 lines in 54.6 s
and the new one 20 007 lines in 2.0 s, with no test covering the shape because
no fixture names a member of a grandparent.

**2. A conversion through more than one link was access-checked at the first
link only.** 11.2p5 makes a base accessible only where every base-specifier
between the two classes is, and `require_base_access` asked the derived class's
own `base_access` and stopped. `struct YA{}; struct YB : private YA{}; struct
YC : YB{}; YA* g(YC* p){ return p; }` converted through a private base from
outside every class that could reach it. The question is now one walk from the
derived class to the base, asking each link, which is the same walk the
conversion itself is.

**3. The conditional's composite pointer type converted neither operand.**
5.16p6 brings both operands to that type, and 5.9p2's binary operators did it
while `?:` only computed the type: `c ? &d : pb` produced a `YB*` holding the
address of a `YD` with no conversion node and no 11.2p4 check, so a private base
was reachable through a conditional that a comparison of the same two operands
refused. Both operands are now converted by the same `convert_operand_to_base`
the comparison uses.

**4. `static_cast<Base&>(derived)` was refused.** 8.5.3p4 makes a reference
reference-related to its initializer when the referenced type is the operand's
own type *or a base class of it*, and `cast_to_reference` implemented only the
first half — so the base case fell through to "bind a temporary", which needs
the copy constructor PA17 owns, and the cast was refused. Every other context
that binds a base reference to a derived lvalue — an initializer, a parameter, a
return — already worked, which is what made the cast the odd one out. It now
writes the same `base-conversion` node they do.

**5. `static_cast<Derived&>(base)` was refused, and `static_cast<Derived*>(pb)`
was unchecked.** 5.2.9p11's downcast is one rule with two spellings. The pointer
spelling was accepted and the reference one refused; and the pointer one was
accepted without asking whether the base is accessible, which p11 requires. The
reference spelling now names the storage the operand named — the base subobject
begins where the derived object does, so there is nothing to write around it —
and both spellings ask 11.2p4 first.

**6. An inaccessible destructor was called anyway.** 12.4p11 makes a program
that names an inaccessible destructor ill formed, and C3 checked only the
deleted case: `struct YB { private: ~YB() {} }; YB b;` emitted a call of a
destructor no context could name, and so did a member of such a class and — new
in C4 — a base subobject of one. The access is now asked where the object is
declared, and for a member or a base in the class whose destructor names it,
which is the same rule with the same one owner.

**7. 11.4p1's additional check was missing.** A protected member is reachable
from a class derived from the one that declared it, and 11.4p1 then requires the
object to be of that derived class rather than of the base:
`struct YB { protected: int p; }; struct YD : YB { void n(YB& o) { o.p = 2; } };`
was accepted. The check is now asked at the two member-access sites, against the
class the object expression named.

**8. A class only ever used as a base carried the complete-object entry.** The
ABI gives a constructor and a destructor an entry point for a complete object
and one for a base subobject, and the lowering named every body with the
complete-object symbol and aliased the base-object one to it. The references do
not: `200-single-inheritance.ref` emits `YA`'s constructor as
`object=_ZN2YAC2Ev` with no alias, because nothing in that program constructs a
`YA` as anything but a base subobject, and `YB`'s as `object=_ZN2YBC1Ev` with
`alias object _ZN2YBC2Ev`. A member subobject counts as a complete object, which
`200-member-object-lifetime.ref` shows. Which entry a special member was run as
is now a fact the analysis records on the declaration, so the demand order
cannot change the answer, and the lowering names the body after it. Four
reference outputs that differed only here are now byte for byte identical.

### Also found, and fixed with them

- **A reference member's binding claimed to be a read through it.** `lowir.md`
  makes `projection=` a claim about what an address is for, and
  `member_storage` wrote `reference_field` for a member of reference type
  wherever it was named — including the store that binds it, where what is being
  addressed is the member's own storage. `200-reference-member-class-init.ref`
  writes `field` there and `reference_field` for the read, and the file is now
  identical. `projection` is one of the keys the relaxed comparison strips, so
  no test said anything about it; it is added to the fields this audit diffs.
- **A member whose declaring class the walk never reached was converted anyway.**
  The old loop ran while the class had a base, so a declaring region that is not
  in the chain left the object converted to the last base of it. It now returns
  the object as it stands.
- **The C4 split left three dead helpers and a reordered initializer list.**
  `round_up`, `call_arguments` and `is_initializer_list` moved to
  `sema_class.cpp` and stayed behind in `sema_analyzer.cpp`, and `reading_` was
  initialized out of declaration order. Each was a compiler warning on every
  build. The tree now builds clean.
- **`aggregate_class` still said a PA16 class has no base.** The caller asks
  8.5.1p1's base question before it, so the code was right and the comment was a
  checkpoint out of date.

### Left for a later checkpoint

- **`x.YB::b` names no member.** A qualified-id after `.` or `->` is not read at
  all — `x.YB::b` fails with "no declaration of YB::b is in scope", and so does
  `x.YB::m()` and even `x.YB::b` where `x` is a `YB`. It predates C4: it fails
  with no inheritance anywhere. C4 is what makes it matter, because 10.2p2's
  hiding leaves it the only way to name a base's member the derived class
  redeclared. No fixture writes one. Recorded rather than fixed here: it belongs
  to the member-access path, not to the base-clause.
- **`alignas` on a member is dropped.** `struct X { alignas(8) char v; };` lays
  out as if the specifier were absent, which is a silent wrong size rather than
  a refusal. `requested_alignment` is read for a class-head and for nothing
  else. `300-member-alignas-layout.t` needs it and `alignof` both, and `alignof`
  is the constant-expression gap the plan already records; one checkpoint owns
  the pair.
- **`static_cast` between unrelated object pointer types is accepted.** 5.2.9
  reaches an unrelated pointer only through `void*`, and `static_cast<YU*>(pb)`
  for unrelated `YU` and `YB` compiles. It is a gap in the cast rules rather
  than in the object model, and predates C4.
- **11's access is checked against the first declaration of an overloaded name,
  not the one 13.3 chooses.** `struct YB { private: void f(int); public: void
  f(); };` refuses `o.f()`, and writing the two declarations the other way round
  accepts it — the answer depends on the order they were written in. It predates
  C4 and belongs to the call path, where the choice is made; 11.4p1's new check
  is asked only where every declaration the name reached answers it the same
  way, so it adds nothing to this.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 holds at 126 / 243 with
  the same set of tests passing and the same set failing: no fixture writes a
  program any of the twelve findings changes the verdict on, which is why the
  count does not move — the two that fixtures do reach are in fields the relaxed
  comparison strips.
  Five `.ref` files the relaxed comparison had been forgiving are now matched
  byte for byte, and the passing fixtures whose output differs from the
  reference at all fall from 39 to 34 — of which 23 differ only in the order the
  top-level definitions are written in, which the README makes a presentation
  convention.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. The refusals this audit added — a
  conversion through an inaccessible link of a chain, a downcast to an
  inaccessible base, an object whose destructor is inaccessible, a protected
  member named on the wrong object — each name the construct and the clause, and
  each replaces output that described a program the source did not write.
- Demand-driven emission is unchanged and still monotonic: a translation unit
  that defines classes and uses none emits nothing but `@main`, and a base's
  helpers are reached only through the derived class's own.
- Valgrind clean (`-q --error-exitcode=99`) over all 243 pa16 fixtures and 88
  synthesized probes — conversion, access, chain-depth, lifetime and cast
  shapes.
- The file audit passes with the same two `bad-division` warnings recorded by
  the C1–C2 and C3 audits, both the heuristic counting declarations rather than
  bodies in `sema_analyzer.h` and `lowir_lower.h`. Neither grew a body this
  checkpoint; C4's split moved 1212 lines out of `sema_analyzer.cpp` into the
  new `sema_class.cpp`.

### Checked and left alone

- One `base-conversion` node carries offset zero because 9.2p13 puts every base
  subobject of this milestone at offset zero. The node carries the offset rather
  than assuming it, so PA18's multiple inheritance changes what is written into
  it and not who writes it.
- The `eh_cleanup` / `eh_try` / `resume` regions the references write around a
  partially constructed object are still absent, including the one
  `200-destructor-body-local-before-base-destruction.ref` writes around a base
  subobject. That is the 15.4 exception model the failure map already owns.
- Analysing a member of a class d deep from an object of the derived class costs
  d, and n such accesses cost n·d: three walks of the chain — 10.2's lookup,
  the walk to the declaring class, and 11.2p5's access — none of which is
  avoidable without caching a fact whose owner is the scope layer. Each axis
  alone is linear; the product is not, and the per-step cost grows with the
  chain because a 4000-long chain of regions does not fit in cache. Recorded
  rather than changed: a 4000-deep single-inheritance hierarchy is not a shape
  the milestone is for, and the output it produces is now linear either way.

## Checkpoint Audit Ledger

| # | Checkpoint | Findings | Result |
| --- | --- | --- | --- |
| C1–C2 | field offsets, `.`/`->`/implicit `this`, the implicit object argument in 13.3.1, demand-driven inline emission, member-function ABI names; 11 access control, 8.5.1 aggregate initialization, 8.5.4p7 narrowing, 7.6.2 `alignas` | `int C::s;` defining nothing, so a static data member had no storage; a brace-or-equal-initializer read and then dropped; `alignas(type-id)` asking for nothing; a member access dropping an object expression that calls; `f()` and `f() const` unordered by 13.3.3.2p3, and `f(T&)` against `f(const T&)` with it; a member call named `.` in its diagnostics; O(n²) slot naming over n blocks; 11p6 read as 11p2, refusing a member defined outside its class the names its class gave it; 9.3p2 read as "declares into a class", so a member defined outside it bound weakly and was emitted only where used | pa16 65 → 70 / 243; pa1–pa15 1173 / 1173; valgrind clean over 249 inputs; every axis linear, 4000 blocks 2.10 s → 0.11 s; file audit passes, two header-weight warnings recorded; the stripped metadata agrees with the refs but for `unwind=no` |
| C3 | 12.1/12.4 user-declared constructors and destructors chained on the class, 13.3.1.3 selection over 8.5's four initializer forms, 12.6.2 member initializations and 12.4p8 member destructions, 3.8p1 lifetime at block exit / `return` / `@__cppgm_fini`, 8.4.2/8.4.3, 12.8p31, 5.2.4, C1/C2 and D1/D2 ABI names | six ways out of a region that ended no lifetime — `break`, `continue`, `goto`, the for-init-statement's own region, a static data member's shutdown and the block-scope `static` written as an automatic object, and an aggregate whose lifetime was recorded only on the constructor path; `this` in a destructor carrying 12.4p12's `const volatile` so a destructor could not write its own member; a deleted destructor called and declared rather than refused; `= T(…)` refused as copy-list-initialization when the constructor is `explicit`; a mem-initializer that named nothing dropped, and one written twice accepted; a constructor the class only declared bound weakly; `operator+` and `operator-` flattening to one internal symbol; the goto check walking every open block | pa16 102 / 243 held, no test that passed before fails after; pa1–pa15 1173 / 1173; valgrind clean over 273 inputs; every axis linear at 2.1–2.3× per doubling; file audit passes with the two recorded header-weight warnings; the stripped metadata agrees with the refs but for `unwind=no`, whose two owners are now named |
| C4 | 10p1's base-clause on the class and its region, 9.2p13 layout with the base at offset zero, 10.2p2/p6 lookup through the chain, 11.2p2/p4 and 11.4p1 access, 12.6.2p5 base initialization and 12.4p8 base destruction, 12.1p5/12.4p3 triviality through the base, 4.10p3 / 8.5.3p4 / 5.2.9p11 as one `base-conversion` node with 13.3.3.1.4p1's rank and 13.3.3.2p4's order, 5.9p2 and 5.16p3, the object model split into `sema_class.cpp` | one derived-to-base conversion written as one node per link of the chain where the references write one, at n·d instructions for n accesses d deep; a chain access-checked at its first link only; the conditional's composite pointer type converting neither operand, so a private base was reachable through `?:` and not through `==`; 8.5.3p4's base half of reference-related missing, so `static_cast<Base&>` was refused; 5.2.9p11's reference downcast refused and its pointer downcast unchecked; an inaccessible destructor called for an object, a member and a base; 11.4p1's additional check on a protected member absent; every constructor and destructor named with the complete-object entry, where the references name a base-only one with the base-object entry and no alias; a reference member's binding claiming `projection=reference_field`; a member whose declaring class the walk never reached converted to the last base anyway; three dead helpers and a reordered initializer list left by the split | pa16 126 / 243 held, the same set passing and failing; five `.ref` files now byte for byte identical and the passing fixtures differing from the reference at all down from 39 to 34; pa1–pa15 1173 / 1173; valgrind clean over 243 fixtures and 88 probes; conversion, lifetime, protected-access, chain-depth and access axes all linear at 2.0–2.3× per doubling, and the n·d output blow-up gone (16 012 007 lines in 54.6 s → 20 007 in 2.0 s); file audit passes with the two recorded header-weight warnings; the stripped metadata — now including `projection=` — agrees with the refs but for `unwind=no` |

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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis doubles in about 2.0x-2.3x.

| axis | sizes | times |
| --- | --- | --- |
| derived-to-base conversions in one body, four deep | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.03 / 0.07 s |
| accesses to a base's member in one body, four deep | 500 / 1000 / 2000 / 4000 | 0.01 / 0.01 / 0.03 / 0.08 s |
| chain depth, one access to the root member | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.04 / 0.09 s |
| 11.4p1 protected accesses through a five-deep chain | 500 / 1000 / 2000 / 4000 | 0.00 / 0.00 / 0.01 / 0.02 s |
| locals with destructors in one block | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.04 / 0.08 s |
| classes in a chain, nothing used | 500 / 1000 / 2000 / 4000 | 0.01 / 0.02 / 0.04 / 0.09 s |

- The finding that mattered is the one the table cannot show, because it was an
  output size rather than an analysis cost. A chain d deep with n accesses to the
  root member emitted one `index [projection=base_subobject]` per link per
  access. Measured against the pre-audit binary at `cc855b17`, n = d:

  | n = d | pre lines | pre time | post lines | post time |
  | --- | --- | --- | --- | --- |
  | 500 | 251 507 | 0.72 s | 2 507 | 0.02 s |
  | 1000 | 1 003 007 | 2.86 s | 5 007 | 0.07 s |
  | 2000 | 4 006 007 | 12.42 s | 10 007 | 0.37 s |
  | 4000 | 16 012 007 | 54.55 s | 20 007 | 2.03 s |

- What is left on that shape is analysis, not output: with the depth fixed at
  4000, 500 / 1000 / 2000 / 4000 accesses take 0.42 / 0.69 / 1.09 / 2.39 s, which
  is linear in the accesses; with 4000 accesses fixed, depth 500 / 1000 / 2000 /
  4000 takes 0.15 / 0.23 / 0.64 / 2.35 s, which is the depth each of 10.2's
  lookups walks, with a per-step cost that grows once the chain of regions no
  longer fits in cache. n accesses d deep cost n*d, which is what 10.2 asks for.
- Nested block scopes are super-linear in their depth and were before this
  checkpoint: 1000 / 2000 / 4000 nested blocks holding one scalar each take
  0.02 / 0.06 / 0.23 s. The cost is the lookup walking enclosing regions, not the
  object model, and is recorded rather than changed.
- Measured earlier and unchanged: 4000 mem-initializers in one constructor,
  0.07 s; 4000 default member initializers in one class, 0.06 s; 4000
  namespace-scope objects constructed and destroyed, 0.09 s; 4000 loops each with
  a `break` leaving one object, 0.33 s; 2000 constructor overloads chosen between
  for one call, 0.06 s; a single `break` unwinding 800 nested blocks, 0.04 s;
  8000 members laid out and initialized twice, 0.14 s; nested aggregate depth
  800, 0.80 s; 8.5.1p7's zero-fill bound at 2^20 elements in under 0.01 s.

## Validation

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 126 / 243, the same set of
  tests as before the audit: none newly failing, none newly passing.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over 243 pa16 fixtures and 88 synthesized
  probes — conversion, access, chain-depth, lifetime and cast shapes — no error.
- Scaling: six single-axis series at four sizes each, two axis-separation series
  at depth 4000, and one series measured against the pre-audit binary at
  `cc855b17` built from a worktree.
- The whole stripped set — `object=`, `binding=`, `linkage=`, `role=`,
  `unwind=`, `projection=`, `keep_alias=` and the `alias object` lines — diffed
  against the reference for all 126 passing fixtures. What is left is the six
  `unwind=no` fixtures already recorded, and the `*-bad` fixtures whose
  reference output is empty because the run is required to fail rather than to
  write anything meaningful.
