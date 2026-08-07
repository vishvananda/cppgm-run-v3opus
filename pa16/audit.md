# PA16 Audit — `cppgm++ --emit-lowir` object model

A review of each landed checkpoint, in the order a fact travels: parse, declare,
lay out, resolve, lower.

## Current Checkpoint Review

**C9 — 7.3.3p1's using-declaration in a class and 12.9's inherited
constructors, reviewed at `0dfe758c`.** The architecture is the right one. What
a using-declaration writes in a class is a declaration *of that class*, carrying
11p1's access and answering 7.3.3p14's hiding, and naming the base's declaration
through `SemaEntity::shadowed`; every use of it — the body a call runs, the
offset a member access reads, the symbol the object file holds — is a fact about
the one it names. 10.2's lookup then finds it in the class itself and walks no
base chain for it, and 13.3 ranks it beside the class's own with 13.3.3.1p4's
object parameter naming the derived class. 12.9's inheriting form is the same
idea for a name no lookup binds: a constructor of this class per constructor of
the base, whose definition is 12.9p8's call on the base subobject. And the ABI's
two entry points, written as the two definitions the references write where a
complete object and a base subobject both asked for one, is the right shape for
a milestone with no virtual base.

What the review found is one thing said twice. **The rule was written at the
places this checkpoint's own fixtures name and left at the sibling places, and a
question about the complete class was answered where the class body had got
to.** `declared_member` was threaded through `.`, `->`, an unqualified name and
a written call, and not through the operator expression, the address of an
overloaded name, or 8.3.6's default arguments — each of which then named a
symbol no unit defines or refused a program everyone accepts. And 7.3.3p14's
hiding and 12.9p1's "unless the class declares one with the same signature" were
both settled against the members read so far, which made the answer depend on
where in the body the using-declaration stood, cost n² for n declarations, and
gave a constructor the access of the section the using-declaration was in rather
than of its own.

**1. An operator expression called the declaration rather than the one it
names.** 13.3.1.2's call is the other place a member function is named on an
object, and it did not read through `shadowed`: `d + 1` for a class that brought
`operator+` in from its base wrote `call @YD__operator_` with
`object=_ZN2YDplEi` — a symbol no translation unit defines — passed the derived
object where the base subobject was wanted, and asked for no definition, so the
base's body was never emitted. Naming a function is now one description in one
place: `name_function` resolves through `shadowed`, and the operator path takes
the base's type and the object conversion `call_expression` already had, with
11.2p5 left unasked because the naming class is the one that wrote the
using-declaration.

**2. 13.4's address of an overloaded brought-in name did the same.**
`int (*p)(int) = YD::f;` for a static member function a using-declaration
brought in wrote `addr @YD__f` against `_ZN2YD1fEi` and emitted no body; a
functional cast and an argument reaching a function pointer went the same way.
The one-place fix covers all three, because each of them ends in
`name_function`.

**3. 8.3.6's default arguments were unreachable from what the class declared.**
`struct YB { int f(int x = 3); }; struct YD : YB { using YB::f; }; d.f();` was
refused. A default-argument belongs to the declaration that wrote it, and a
later declaration of the base's function may add one, so a copy taken where the
using-declaration stood would have been wrong as well as absent: `accepts_arity`
and `write_default_argument` read the declaration `shadowed` names.

**4. A hidden declaration stayed in 13.1's index of the chain.** A declaration a
using-declaration brought in is one 7.3.3p14 *hides* rather than one 13.1
redeclares, so it never belonged in the index that says which declaration a
declarator redeclares — but it entered it whenever the head of the chain was
rebuilt. A class that hid two of three brought-in overloads and then declared
the third was told "f is defined twice", for a program g++ and the references
accept.

**5. 7.3.3p14 compared a parameter no declarator wrote.** 9.3.1p3's object
parameter is one this milestone puts in a non-static member function's type; it
is no part of 8.3.5p4's parameter-type-list, and a static member function has
none. Comparing it meant a static `f(int)` brought in from a base was never
hidden by the class's own non-static `f(int)` — which 9.4.1p2 does not let
overload — so g++ accepted the program and we refused it as ambiguous. What is
compared now is the written parameters with 8.3.5p7's cv-qualifier-seq beside
them, which a static member function wrote none of.

**6. That hiding was asked once per declaration, at n² cost.** 7.3.3p14 is a
question about the complete class, and it was asked of the body so far — once at
the using-declaration for what the class already had, and again at every later
member declaration, each walking the declarations the name already held. A class
bringing in n overloads of one name and declaring n of its own took 2.53 s at
n = 4000. It is one pass over each brought-in name where 9.2p2 completes the
class now — 0.27 s at 4000, linear at 2.0–2.2x per doubling — and the two orders
are one rule rather than two.

**7. 12.9p1's candidate set was one constructor per declaration, carrying its
default arguments.** 12.9p2's constructor characteristics hold no
default-argument: the candidate set holds the base's own parameter-type-list and
the shorter ones that result from omitting its defaulted parameters from the
end, and the base's default-argument is what fills the call 12.9p8 writes.
Carrying it instead made `struct YB { YB(int = 1); }; struct YD : YB { using
YB::YB; }; YD d;` ambiguous between the inherited `YD(int = 1)` and 12.1p5's
`YD()` — a program g++ accepts and this unit refused.

**8. Which constructors 12.9p1 inherits was settled where the using-declaration
stood.** 12.9p1 leaves out a base constructor whose signature the *complete*
class declares, wherever among the members that declaration stands. C9 answered
it at the using-declaration and repaired the answer afterwards, by repurposing
the inherited declaration in place — which left it at the position, and so under
the access, of the using-declaration rather than of the declaration the program
wrote. Both directions were wrong: `private: using YB::YB; public: YD(int);`
made `YD d(1)` inaccessible, and the same pair the other way round made a
private constructor reachable. It is settled at 9.2p2's completion now, beside
the hiding, and `special_member` declares what the program wrote and nothing
else.

**9. The base-object entry a call names was declared nowhere.** A constructor or
destructor a complete object and a base subobject both asked for stands under
both of the ABI's entry points, and C9 wrote the second definition where this
unit holds a body. Where it does not — `struct YB { YB(int); }; struct YD : YB
{ YD(int x) : YB(x) {} }; YB b(1); YD d(2);` — the calls named
`@YB__YB__base_entry` and the unit declared only `@YB__YB`. A name a call writes
is one the unit owes the program a declaration of; both entries are declared
now, with `_ZN2YBC1Ei` and `_ZN2YBC2Ei` as the references write them.

**10. A constructor declared with an ellipsis read one type past its parameter
list.** `struct YB { YB(int, ...); }; YB b(1, 2);` read `parameters[index + 1]`
for an argument no parameter names — in the analysis and again in the lowering,
two out-of-bounds reads valgrind reports for a program 13.3.1.3 accepts. How an
argument is passed is one description now, shared by a call the program wrote
and the call 12.6.2 writes for a constructor: 5.2.2p7's ellipsis argument goes
through the default argument promotions in both.

### Left for a later checkpoint

- **A subobject of class type an aggregate clause initializes is refused.**
  8.5.1p2 copy-initializes each subobject from its clause, which for one of class
  type is 13.3.1.7's list-initialization or 12.8p31's construction in place:
  `Entry table[] = {{"a", 1}, {"b", 2}};` and `Tok tok{1, 2};`, two fixtures.
- **The exception cleanup regions around a partly built object.** An array member
  half constructed, and a destructor part way through its members, want
  `eh_try` / `eh_cleanup` / `resume` regions the milestone does not write. Two
  fixtures, and the only thing separating our base-subobject destructor calls
  from theirs.
- **An aggregate an array element is, written as a constructor.** The references
  synthesize a constructor per aggregate reached as an array clause, taking its
  members as parameters, and call it per element; we write the stores. Three
  fixtures, unchanged by this checkpoint.
- **A member named through a qualified-id on an object.** `d.YD::f()` and
  `d.YB::f()` are refused — "no declaration of YD::f is in scope" — with and
  without a using-declaration and with and without a base, so it is 5.2.5p1's
  qualified-id rather than anything C9 touched. No fixture reaches it.

### Confirmed intact

- pa1–pa15 hold at 1173 / 1173 from a clean tree. pa16 held at 210 / 247 across
  every step of this audit — the same fixtures passing and the same failing as
  at `0dfe758c` — and is 220 / 257 with the ten regression tests this audit
  adds. Not one of the ten findings changes the verdict on a checked-in fixture,
  because no fixture brings an operator or an overloaded static into a class,
  gives a brought-in member a default argument, hides a static member function,
  inherits a constructor that has one, declares a constructor after an
  inheriting using-declaration, calls a constructor this unit has no body for
  from a base subobject, or writes an ellipsis on a constructor — which is what
  made this checkpoint worth sweeping shape by shape rather than fixture by
  fixture.
- Of the 197 passing fixtures with a reference output to compare, 143 are byte
  for byte identical. What is left differs only in the order the top-level
  definitions are written in, in the internal symbol name `lowir.md` makes a
  presentation tie-breaker, and in two metadata keys the comparison ignores —
  `unwind` on nine of them and `trivial_lifecycle` on two. `pass=` is not on the
  ignore list and differs on none of them.
- No fallback success path, skipped work, timeout workaround, source-specific
  gate, dummy output or file-audit bypass. Every refusal this audit leaves names
  the construct and the clause it comes from.
- Valgrind clean (`-q --error-exitcode=99`) over all 257 pa16 fixtures and 460
  synthesized inputs, after the two out-of-bounds reads above were closed.
- The file audit passes with the same two `bad-division` warnings the C1–C2, C3,
  C4, C5, C6, C7 and C8 audits recorded, both the heuristic counting
  declarations rather than bodies in `sema_analyzer.h` and `lowir_lower.h`.
  `construct_object` crossed the 240-line limit while this audit was under way
  and gave up the definition a use of a constructor asks for, which is a unit of
  its own.

### Checked and left alone

- **12.9p1's candidate set against the references' one inherited declaration.**
  They declare one constructor per declaration of the base and carry its
  default-arguments onto it; we declare the candidate set 12.9p1 describes and
  none. Every program both accept means the same thing either way; where they
  part, g++ agrees with this unit — `YB(int = 1)` inherited beside 12.1p5's
  default constructor is accepted here and refused there, and a class that
  declares the truncated signature itself is accepted here and refused there.
  The shape the output takes differs on any base constructor with a
  default-argument, and no fixture writes one.
- **The references refuse three shapes g++ and this unit accept**: a constructor
  the class declares *after* an inheriting using-declaration, a static member
  function hidden by another static one a using-declaration brought in, and
  `operator()` brought in from a protected base. All three are recovery on their
  side rather than judgment.
- **A variadic inherited constructor is accepted here and by neither oracle.**
  The references refuse it; g++ says "sorry, unimplemented: passing arguments to
  ellipsis of inherited constructor". 12.9p1 allows it, the calls this unit
  writes are the ones the source asks for, and the ellipsis reads are the ones
  this audit closed.
- **A destructor whose body does nothing is still called**, **8.5p7's zero
  before a non-trivial default constructor**, **12.4's destructor for an object a
  prvalue initialized**, **a conditional over two class lvalues as an lvalue**,
  **`static_cast<const YA&>(YA(4))`'s `tmpobj`**, **the zero of a class object
  past 64 bytes as one `zeroinit`**, **the untouched tail of a namespace-scope
  array as one `zero` item**, and **`pass=by_address` for a class holding a
  bit-field** — all recorded at the C7 and C8 audits and all unchanged.
- **A block-scope array of class type is destroyed front to back**, **the
  references constant-initialize an array of class type and we do not**, **a
  bit-field clause is evaluated before the unit it joins is loaded**, and
  **`alignas(0)` / `alignas(1) int` / `alignof(void)` / a class-head-name naming
  what no region declared** — all recorded at the C8 audit and all unchanged.
- **A pointer to member of a class a using-declaration brought the member into.**
  5.3.1p3 gives `&YD::f` the base's class, which is what `shadowed` names; `.*`
  and `->*` are outside this milestone's operator subset, so no program here can
  read one.
- **An array subscript converts its index and evaluates it twice**, **a subscript
  of a multi-dimensional array decays the row it reached**, and **nested
  namespace depth is super-linear** — all owned elsewhere.
- **One class with n friend operator overloads used n times stays quadratic**, at
  n calls each ranking n candidates.
- **A base constructor with n defaulted parameters is inherited super-linearly**,
  and is meant to be: 12.9p1's candidate set is n constructors whose parameter
  lists hold n²/2 types between them. 500 / 1000 / 2000 / 4000 defaulted
  parameters take 0.02 / 0.05 / 0.16 / 0.56 s for 8040 lines of output at the
  end. Declaring the same n constructors with no using-declaration at all grows
  the same way, so it is the type layer's cost and not the inheritance's.

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

## Performance Evidence

Measured with `cppgm++ --emit-lowir -O0` on synthesized inputs, this host, at the
end of the audit. Every axis is linear in its size.

| axis | sizes | times |
| --- | --- | --- |
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

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` — 210 / 247 at every step of
  this audit, the same set passing and the same failing as at `0dfe758c`, and
  220 / 257 with the ten regression tests it adds.
- `make test-report-through-pa15` — 1173 / 1173.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src` — passes, with
  the two `bad-division` warnings accounted for above.
- `valgrind -q --error-exitcode=99` over all 257 pa16 fixtures and 460
  synthesized inputs — the cross product of base access, member access and
  member kind brought in by a using-declaration; the base's constructors
  inherited against every constructor shape, every derived declaration and every
  member the derived class adds; 7.3.3p14's hiding in both orders over four base
  overload sets, five derived declarations and four calls; and the scaling axes.
  The two out-of-bounds reads a constructor declared with an ellipsis made are
  the only errors it found, and both are closed.
- Differential against `pa16/cppgm++-ref` on 458 synthesized programs: 335 agree
  after the relaxed comparison, 53 are refused by both, 18 differ only in
  12.9p1's candidate set or in a divergence recorded before this checkpoint, and
  52 are refused by one. Every one of those 52 was put to g++, which agrees with
  this unit on all of them but the 5 that need `.*` or a qualified-id on an
  object — neither of which this milestone reads — and the 12 that write an
  ellipsis on an inherited constructor, which g++ says it has not implemented.
- Scaling: ten single-axis series at four sizes each for this checkpoint's own
  paths, with the output line counts at the ends recorded beside the times, plus
  the C4-C8 axes re-measured and unchanged.
- The whole stripped set — `object=`, `binding=`, `linkage=`, `role=`, `unwind=`,
  `projection=`, `effects=`, `capture=`, `access=`, `alias=`, `return=`,
  `keep_alias=`, `trivial_lifecycle=`, `tls_for=`, `prefer_local=` and
  `storage=` — diffed against the reference for all 197 passing fixtures whose
  reference output is not empty. 143 are identical without any stripping; what is
  left differs only in top-level order, in an internal symbol name, and in
  `unwind=` on nine of them and `trivial_lifecycle=` on two. `pass=`, which the
  comparison does not strip, agrees on every one of them.
