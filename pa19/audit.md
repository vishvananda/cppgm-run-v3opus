# PA19 Audit — `cppgm++ --emit-lowir` first-tier templates

A review of each landed checkpoint, in the order a fact travels: declare,
settle, instantiate, name, lower - and, at the end, one independent review of
the whole tier against the README and the source rather than against those
conclusions.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| C1, C2, C2 completion | `aa6fb90f` | 6 / 6 + 1 perf | **the object file's name for a specialization, which this suite cannot see.**  `canonicalize_lowir_for_compare` strips `object=`, `binding=` and `alias object`, so eleven fixtures emitted symbols containing `<`, `,` and spaces and **nine passed**: a name split at every `::` made `api::pair<const api::text<char>,api::tag>::pair` five components; `owning_classes` walked the region a definition was *written* in; the ABI's `<template-param>` was never a substitution candidate, so **every** function-template specialization differed from g++ and the reference alike; and an instantiated definition bound `strong` would have been a duplicate symbol at link over 19 symbols, none of which failed |
| C3 | `e67acde3` | 5 / 5 + 1 perf | **the readers a landed rule was not given.**  14.5.6.2's ordering reached 13.3.3p1's tie and not 13.4p1's target; 14.8.2.1p3's reference collapse was landed at one arm; a specialization named above its template's definition got no body |
| C4 | `fa07d078` | 5 / 5 | **the reading's own edges.**  A template-id written in a definition being read joined the inventory of what exists; 14.6p8 asked a statement's operand and not a declaration's initializer; 14.6.1p6 was asked at some of the places a declaration binds a name and not the rest |
| C5 | `277a48bb` | 3 / 3 + 1 | **what the reading kept and what it did not.**  14.1p2 leaves each declaration of one template free to spell its parameters as it likes, and an out-of-class member definition's head names were bound into the class's own region, so the *second* definition's names collided with the first's; 9.2p2's held bodies were walked once where reading one can hold another |
| C6 | `955dce9f` | 1 / 1 | a declaration written below a *template's* definition renamed the parameter objects every specialization had already made |
| C6 audit | `d6700f4a` | 2 / 2 | **the same fact asked at fewer places than have it.**  14.7.1p1's spelling was frozen where the pattern's *definition* is read, where the reference freezes it at the template's first *declaration* - 16 of 120 declaration orderings told them apart |
| C7 | `c3f2411f` | 3 / 3 | **a rule landed at the question and not at the walk that answers it**: `declares_function` walked in to the declarator-id and `declarator_type` did not; and a fact carried as a terminal in a dump pa10 is graded on |
| C8 | `43aa2aa0` | 2 / 2 + 1 | **the deferral was landed at the three places a *use* stands and neither demand with no expression behind it was given it** - 10.3p10's table and 14.7.2's explicit instantiation - over an output where a body nobody grants is a `declare function` and no definition, which does not link |
| C9 | `1b135271` | 3 / 3 + 1 | **the type a decltype-specifier stands for was made a fact of the *reading* and not of the expression**, keyed by the AST node and the region's id, so one function written twice had two return types and a declaration and its definition never met |
| C10 | `0b3f72b8` | 7 / 7 | **the object parameter a member template now carries has four readers and got one**: 9.4.1p2's out-of-class definition matched by 13.1's index rather than by 14.5.6.1p5's signature, what a specialization is called on, and 14.5.6.2p2's ordering places; 14.8.1p2 had a third arm at an explicit instantiation |
| C11 | `52c679e1` | 6 / 6 | **a rule landed at one reader of a fact and not the others.**  The fold asked the operand a lowering wrote where the rule is the expression's; 12.2p1's temporary and 4.10p1's null pointer value were answered for a body and not for the **image** |
| C12 | `7afd0f26` | 1 / 1 | **the checkpoint's own rule read at the wrong question, wrong in both directions at once**: 14.7.1p1 instantiates a specialization where a completely-defined type is required *and nowhere else*, and it was asked at the naming |
| C13 | `b60697fa` | 4 / 4 | **one rule landed at the one place its fixture reached**: "a definition the program wrote outside its class is this unit's" went in as a clause of `owe_internal_definition`, which only a *constructor* whose call was elided reaches, so six of the seven kinds 9.3p2 covers were dropped |
| C14 | `fb39e2f1` | 0 / 0 | the four rules C14 landed were swept at their sibling call sites - 13.6's table under `?:`, `[]` and compound assignment, 5.19's other operand contexts, `is_definite_type_id` over a plain template-name, and a function template's merged defaults - and each was already answered where the sweep asked |
| final audit | `dec4feb4` | 3 / 3 + 1 convention | below |

## Findings

**1. 14.7.1p1's deferral had never reached the out-of-class member definition.**
C8 landed "instantiating a class instantiates the declarations of its members
and not their definitions" for a body written *in* the class body and nowhere
else.  `instantiate_member` read the whole definition - declarator and body -
once per specialization, so

```cpp
template<class T> struct S { void f(); };
template<class T> void S<T>::f() { T x; x.member(); }
S<int> o;
int main() { return 0; }
```

was `a member is named of an operand that is not of class type` here and
compiles in `reference-binaries/cppgm++` and in **g++**, which is 14.7.1p1
outright: nothing names `f`, so its definition is not instantiated.  The same
body written inside the class already compiled, which is what says the rule was
landed at one of its two sites.  It is also the tier's one quadratic that is not
the program's own count: n out-of-class definitions over n specializations read
n^2 bodies for the members a program calls.

**2. The mangler discovered a repeated template argument by encoding it again.**
`Encoder::close_candidate` is speculative - it encodes a candidate, computes its
canonical key, and rolls the encoding back when the key repeats - and
`reuse_known_slot` exists to short-circuit that for a *named* type whose key is
already known.  A template argument reached `emit_argument` as an
`AbiDefinitionRecord` the caller hands out once per type, which is exactly such
a binder, and it was never consulted: `P<P<t,t>, P<t,t>>` encoded its second
argument in full before discovering it was `S_`, so a name n specializations
deep cost 2^n walks of what it had already written.

**3. `abi_mangle::decimal` built an `ostringstream` per source name**, and a
locale with it, for the length in front of every component of every mangled
name.  It was 10% of the profile on the shape finding 2 is about and about 1%
on ordinary ones.

**4. 26 fixtures were filed in the handout's own suite.**  AGENTS.md puts every
test this project adds under `cppgm.tests/course/paN`; C6 through C10 wrote 26
under `pa19/tests/general` instead.  Nothing about them is wrong - they carry
refs generated from `reference-binaries/cppgm++` through `make ref-test` - but
`pa19/tests` is the starter kit's and had grown from the 293 it shipped to 319.

### What the review confirmed rather than found

The typed fact flow was traced end to end and holds: a template argument is text
only because 14.2 makes PA10 spell it that way, is read once into a `TypeId` by
`sema_type_id.cpp`, and is a `TypeId` through interning, specialization identity,
substitution and the ABI encoder alike.  The three readings of one body are three
dialects rather than three implementations of one rule.  The demand graph is
complete: every path by which this unit can owe an instantiated definition was
enumerated and probed.

44 shapes covering the README's Assignment Boundary - default arguments that name
earlier parameters, dependent and current-instantiation names, `typename`,
explicit template-ids, function-type arguments, deduction from references,
pointers and arrays, local classes, dependent and non-dependent bases,
using-declarations, injected names, member typedef hiding, out-of-class nested
classes, distinct nested types, declaration/definition in either order, virtual
destructors, rvalue-reference returns, qualified declarator-ids, unused-body
checking, bit-fields, trailing and `decltype` return types, and template-backed
operators - were compiled through this compiler, `reference-binaries/cppgm++` and
g++.  **43 are identical to the reference as canonicalized LowIR**; the
forty-fourth is `W<struct Made>`, which the reference alone refuses and g++
accepts, and which is C12's landed reading of 3.4.4p2 and 3.3.2p6.

### Recorded, not landed

- **The spelling a specialization is named by is exponential in a nest whose
  arguments double**, and quadratic in the depth of one that does not.  A
  specialization's identity is `(template, argument TypeId list)` everywhere but
  its name, and the LowIR comparison does not mask a *global's* symbol, so the
  written-out name is a requirement rather than a convenience.  Fixing it means
  computing that name on demand out of the interned argument list, across
  `TypeTable`, `SemaModel`, the dump and `lowir_abi.cpp` together.  The audit
  took the mangler's own 2^n off it and left the names.
- **The out-of-class path still costs about 3x the declarations it makes.**  Its
  bodies are free now, and what is left is the declarator each definition is read
  through once per specialization, where the member it defines was already
  declared by the class body's own instantiation.  Mapping a definition to the
  member it defines once per *template* and then by position per specialization
  would take the residual off; it is not obviously safe over overloads, nested
  classes, static members and heads that spell their places differently, and the
  shape is quadratic either way because 14.7.1p1 makes n specializations of an
  n-member class n^2 declarations.
- **14.5.2's second template-parameter clause, a template-id after a member
  access (`s.f<int>(2)`), and 12.3.2p1's conversion function template** are
  accepted by both oracles and reached by no fixture.  `parse_member_id` accepts
  a bare identifier after `.` and never tries 14.2's argument list, so
  14.8.1p2's partial list is unreachable through a member access.
- **9.2p2 at the parse.**  A plain class-name a member function declares *below*
  the use hides: `close_impl(which);` where `close_impl` is an ordinary class at
  namespace scope reads as a declaration here and as the member's call in both
  oracles, because the parse fills its name table in source order.  14.6.1p1's
  arm of the same question is landed.
- **14.6p8 over a non-dependent base.**  A non-dependent specialization named as
  a base class in a template's own definition is not completed by the reading, so
  `template<class K> struct P { struct N : adaptor<int> {}; };` is refused where
  both oracles accept it.  Completing it there means suspending the reading.
- **12.1's two entry points for a constructor only a base subobject ever ran.**
  The reference emits both and this compiler the base-object name alone.
  Widening `writes_base_entry` to every base subobject the program wrote
  regressed 43 tests across pa16, pa17 and pa18, so the reference's rule is
  narrower than that and is not one this compiler can state.
- **Which definitions a use that nothing calls makes.**  Where a transfer this
  unit carries as bytes is named inside an instantiated body, the reference
  writes the definition only when the specialization is one the *signature* of
  that body's own function names; we write it for every such naming, which is
  3.2p3 read of 12.8p31, and the definition is weak.  The other way round is
  12.8p31's *returned* object, where the reference writes the copy constructor
  and the elision leaves us nothing to name.
- **The ABI writes a function template's decltype return type as the
  expression** - `_Z3addIiEDTplfp_fp0_ET_S1_` in the reference and in g++ - where
  this compiler writes the type the substitution made.  The encoding needs 5's
  whole expression grammar, the two oracles disagree with each other on two of
  the three shapes swept, and `object=` is stripped before the comparison.
- **A member class's table is asked for where the specialization is completed**
  rather than where an object of it is built, which is 10.3p10's broad reading.
  It is flat in the member count and quadratic in the lexical *depth* of the
  nest, which is 3.4.1p1's own quadratic: a 512-deep class nest with a virtual
  member at every level is 0.40 s against 0.06 s with none.
- **One region is rebuilt per decltype-specifier** rather than per region and
  bindings: a clause of n places each typed by a decltype over the first is
  0.19 s and 99.9 MB at n = 512.
- **Two units of one weak global**, and the `object_root` of a dropped duplicate:
  the program builder keeps the first unit's definition of a symbol and drops the
  second, so a root the dropped copy carried is lost.  It is the program
  builder's rather than this tier's and no fixture is multi-unit.
- **Shapes where this compiler and g++ agree against the reference**, each
  decided against it and none of which can become a fixture because the reference
  is what writes a `.ref`: 13.6p8's unary `-` and `~` over an enumeration;
  5.3.3p1 over a bit-field; 6.4.2p2's case label converted to the promoted type;
  8.2p7's parenthesized place in three spellings; 14.8.1p2's partial argument
  list at an explicit instantiation in three forms; 8.3p1's parameter name in the
  body of a function whose declarator-id stands under a nested clause, in eight
  shapes; `template int t<int>::v;`; a member class's members rooted at an
  explicit instantiation; 14.6.2p3 applied to every unqualified name rather than
  only the one a call writes; 9.4.2p3's static data member holding the value its
  class gave it; a folded read of a `volatile` or `double` member; 5.16p4's
  conditional a reference binds; 12.9p1's inherited constructor keeping its
  arity, where **both** other compilers disagree with us and no fixture writes
  one.
- **Out of scope and still named.**  14.8.2's substitution failure ends the
  translation unit rather than dropping the candidate, which is SFINAE and on the
  Out Of Scope list; a variable template's partial specialization is written into
  the object file as `_Z6v<T,T>`, which is not an ABI name; a non-type template
  parameter is refused, which the README puts on PA20.
- **Run evidence needs scalars.**  The pa13 LowIR -> CY86 path is the only way to
  run what this milestone emits and it hands a by-value class parameter garbage,
  from our LowIR and from the reference's alike.

## Changes

- **`sema_template.cpp`, `sema_function.cpp`, `sema_analyzer.cpp`,
  `sema_declaration.h`, `sema_scope.h`/`.cpp` — 14.7.1p1's held definition given
  to the out-of-class member.**  `instantiate_member` reads the definition under
  `ReadingDepth instantiating_class_`, and `function_definition` routes an
  out-of-class definition read for a specialization through `queue_definition`
  instead of reading its body where it stands.  Three things the deferral would
  otherwise have lost:
  - **14.1p2's own head.**  `EnclosedBy` stands an out-of-class definition's head
    region over the class only while the definition is read, and the body is now
    read long after that.  `PendingDefinition` carries the two scopes and
    `write_definition` puts the link back; `EnclosedBy` moved from
    `sema_template.cpp` to `sema_declaration.h` beside `ReadingDepth`.
  - **10.3p10's table.**  `require_table_definitions` moved to *after*
    `complete_specialization`'s loop over the out-of-class definitions, so a
    virtual member defined outside the class is held before the table asks.
  - **14.6.4.1p1's second point of instantiation.**  `require_definition` now
    marks the function `definition_required` whether or not a body is waiting,
    and `queue_definition` writes rather than holds a definition read below a use
    that already asked.  That is also what keeps a definition arriving late from
    costing a second walk of the specialization's members - asking
    `require_table_definitions` again per definition per specialization would
    have made the shape n^3.
  - **14.7.2p8's explicit instantiation** asks `require_definition` for each
    member it roots, where before it only marked `explicitly_instantiated` and
    relied on the body having been written eagerly.
- **`abi_mangle.cpp` — the argument a name has already written.**
  `emit_argument` resolves the template argument's own `AbiDefinitionRecord`,
  tries `reuse_known_slot` on it first, and records the key its encoding settled
  on, exactly as `emit_type` already did for a named type.  `resolve_argument`
  became dead and was removed.  `decimal` writes its digits directly.
- **`sema_function.cpp` — 8.4p1's body read where it stands became
  `read_definition_body`.**  `function_definition` reached 256 lines against the
  audit's limit of 240; it now builds one `PendingDefinition` and either queues
  it or reads it, so the fields a body needs are described in one place.  The
  header paid for the declaration by making three single-file statics free
  functions of the files that ask them (`fact_kind` and `category_name` in
  `sema_expression.cpp`, `declares_own_constructor` in `sema_class.cpp`).
- **`cppgm.tests/course/pa19` — the 26 fixtures move**, with their `.ref`,
  `.ref.exit_status` and `.ref.stdout` sidecars.  `pa19/tests` is back to the
  293 the starter kit shipped and no test is lost.
- **Three new fixtures** pin the rule and guard the two regressions it could have
  shipped: `100-out-of-class-member-definition-left-to-its-use` (refused by the
  `ac87d88d` pre-change build), `100-out-of-class-member-definition-below-the-use-that-asks`
  and `100-explicit-instantiation-roots-an-out-of-class-member-body`.

## Performance Evidence

**The tier's one quadratic, n out-of-class member definitions over n
specializations**, against the `ac87d88d` pre-change build, each timed twice,
`-O0`:

| n | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| before | 0.03 s / 14.3 MB | 0.13 s / 37.4 MB | 0.54 s / 126.8 MB | 2.27 s / 482.4 MB | 9.43 s / 1892.7 MB |
| after | 0.01 s / 11.4 MB | 0.06 s / 24.8 MB | 0.23 s / 76.7 MB | 1.01 s / 279.8 MB | 3.9 s / 1081.0 MB |

It is still quadratic, and it is quadratic because 14.7.1p1 makes n
specializations of an n-member class n^2 member *declarations*: the same class
with its members only declared and no definition at all is 0.07 s at n = 128
against this shape's 0.23 s.  What the change removes is the **bodies** - the
shape is 0.24 s with empty bodies and 0.23 s with eight statements in each,
where it was 0.51 s and 1.15 s before.  In the reverse order, n specializations
written above n out-of-class definitions, n = 128 is 0.55 s -> 0.25 s where
nothing is virtual and 0.83 s -> 0.85 s where every member is, which is the
shape that can defer nothing at all.  `reference-binaries/cppgm++` is 1.70 s and
10.92 s at n = 128 and 256.

**The 2^n mangle**, `typedef P<t,t>` repeated n times, in 23 lines of source:

| n | 12 | 16 | 18 | 20 |
| --- | --- | --- | --- | --- |
| before | 0.01 s | 0.17 s | 0.70 s | 2.83 s / 376 MB |
| after | 0.00 s | 0.04 s | 0.16 s | 0.63 s / 376 MB |
| `reference-binaries/cppgm++` | - | 0.20 s | 0.50 s | 2.00 s |

The variant that *calls* a member, so that the deep name reaches the object
file, is 0.87 s here and 2.30 s in the reference at n = 20; g++ is 0.10 s on
both.  The residue is the O(2^n) characters the names themselves are, which is
why the memory does not move; it is recorded above.

**Nothing else moves.**  Fourteen shapes in the plan's Performance Model, at
n = 32 to 512, are each where the tier left them or better, with the three the
plan calls quadratic quadratic in the *program's* own count: n calls over an
n-declaration overload chain (0.39 s at 512, where the reference does not
finish inside 300 s), n target types choosing among n templates (0.18 s), and the shape
above.  Ten further shapes were swept for depth and multiplicity to n = 2048 -
n-deep `S<S<...>>`, an n-typedef chain, an n-deep base chain, an n-deep
dependent-name chain, n nested classes in a template, n calls of one template, n
distinct function specializations, one head of n parameters, n typedefs of one
specialization, and n members of one specialization each called - and every one
is linear or, for the two whose *spelling* grows with depth, quadratic in the
characters they write.  Peak RSS moves only where the work does: the quadratic
shape holds 43% less at every size and nothing else changes by more than 1%.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages, pa1-pa18 at
  **1778 / 1778** and pa19 at **391 / 391**.
- `perl scripts/cppgm_file_audit.pl --stage pa19 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  `sema_analyzer.h` is at 2397
  against 2400, `sema_template.cpp` at 2957 against 3000.  The build prints
  nothing.
- Every `.ref` regenerates byte-identically through `make ref-test` over all 293
  fixtures under `pa19/tests` and all 98 under `cppgm.tests/course/pa19`.
- **44 boundary shapes** through this compiler, `reference-binaries/cppgm++` and
  g++: 43 identical to the reference as canonicalized LowIR, the forty-fourth
  the one the reference alone refuses.
- **18 out-of-class member shapes** - explicit instantiation of the class and of
  one member, virtual members defined outside, static data members, constructors
  and destructors, conversion and operator members, nested classes, heads
  spelling their places differently, a definition between two specializations, a
  member used above every definition - identical to the reference in every one
  the pre-change build did not already differ on, and each checked against the
  `ac87d88d` build to tell a fix from a regression.
- **10 demand-site shapes** - a copy constructor named by a by-value argument, a
  destructor at the end of a lifetime, a constructor at a `new`, an `&`, a target
  type, a transfer named inside an instantiated body, a virtual member of a
  nested class - all identical to the reference, and a two-unit program identical
  and order-free in both orders.
- **10 mangling shapes** over nested, repeated and cv-qualified arguments,
  function-type and array arguments, and members of members: `object=` for
  `object=` the reference's in all ten, and every symbol g++ emits for them
  byte-identical to ours, including `_ZN1PIS_IS_IS_IS_I1tS0_ES1_ES2_ES3_ES4_E1mE`.
- The three new fixtures run through `lowir2cy86` to 6, 10 and 0, which is what
  g++ builds them to return.
- Valgrind is clean over all 391 fixtures.
