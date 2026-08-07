# PA16 Plan — `cppgm++ --emit-lowir` object model

## Stage Design

PA16 gives the PA11/PA12 class syntax semantic and lowering meaning. The
existing layers keep their jobs:

- `sema_scope.*` owns declarations, regions and lookup. Where a member sits in
  its object, which special member function a declaration declares, which class
  a class derives from and which class befriended it are facts about the
  declaration, so they live on its own `SemaEntity`; the region it declares
  carries the one edge 10.2's lookup follows.
- `sema_class.cpp` owns the object model: 10p1's base-clause, 9.2p13 layout, 11
  access, 11.3 friendship, 12.1/12.4/12.6.2 special members and subobject order,
  12.2p1's temporary, and 3.7.1/3.8p1 lifetime. Each is settled from the same one fact - what the
  class's own region declares, in the order it declares it - and a question
  about a subobject is asked in the same words whether it is a base, a member or
  an object of its own.
- `sema_operator.cpp` owns the calls ordinary lookup does not name: 13.3.1.2's
  candidate set for an operator expression, 13.5p6's rule on a non-member
  operator, and 3.4.2's associated namespaces and classes.
- `sema_analyzer.cpp` walks the syntax, resolves names and types, and hands each
  class to that owner once, where 9.2p2 makes it complete.
- `sema_overload.cpp` owns 13.3, including 4.10p3/8.5.3p4's derived-to-base
  sequences and 13.3.3.2p4's ordering of them.
- `lowir_lower*.cpp` reads only the resolved tree. It never re-resolves a name
  and never reads syntax. `lowir_lower_object.cpp` holds the part of it that is
  about one object: 12.1/12.4's lifetime calls, 12.2's temporary, 12.6.2's
  member initializations, 12.8p15's copy, 8.5p5's zero and 8.5.1's aggregate.
- `lowir_abi.cpp` turns one resolved declaration into its object-file name
  through PA14's encoder.

The object model is added as typed facts at those owners rather than as a second
pipeline: field offsets on members, a base class on the class, a friendship
relation between two entities, an implicit-object argument in 13.3,
`constructor-action` / `destructor-action` / `member-initialization` /
`base-conversion` / `temporary-object` nodes, and a demand-driven definition
worklist in the unit lowering.

## Current Failure Map

After the audit of C9: 220 / 257 — 206 / 243 of the checked-in fixtures, the
four C9 added and the ten this audit added. The 37 that remain, 18 refusing a
program the references accept and 19 accepting one and writing a different
shape:

| group | count | what is missing |
| --- | --- | --- |
| `thread_local` | 3 | `storage=thread_local` and the accessor function the references write |
| `__builtin_*` names | 3 | `__builtin_memcpy`, `__builtin_memmove`, `__builtin_strlen`, `__builtin_unreachable` |
| an aggregate an array element is, written as a constructor | 3 | the reference synthesizes a constructor per aggregate reached as an array clause, taking its members as parameters, and calls it per element |
| a subobject of class type an aggregate clause initializes | 2 | 8.5.1p2 copy-initializes it from its clause, which for a class needs 13.3.1.7's list-initialization or 12.8p31's construction in place: `Entry table[] = {{"a", 1}}` and `Tok tok{1, 2}` are both refused |
| placement `new` | 2 | 5.3.4's placement form |
| a trailing return type on a member function | 2 | 8.3.5p2 with `auto` |
| `#pragma pack` | 2 | a phase-4 fact that has to reach 9.2p13 in phase 7 |
| the exception cleanup regions around a partly built object | 2 | `eh_cleanup` / `eh_try` / `resume` around each element of a member array and in a destructor body |
| a program the parser does not read | 2 | a nested braced member initializer, a reference member named after its class |
| single defects, one fixture each | 16 | listed below |

The 16 singles, each its own fact: `mutable` under a `const` member function;
`(a.f)(x)`; a static and a non-static member function of one class reaching one
overload key, because 9.3.1p3 put the object parameter in the type; 5.2.4's
pseudo-destructor call on a scalar; a user-defined string-literal operator;
`decltype` in a qualified-id; `__builtin_expect` folded where the references
keep the definition; `store ptr nullptr` spelled `store ptr 0`; an incomplete
class as the return type of a declared function, written `obj<0x1>` where the
references write `void`; a defaulted constructor called where the references
elide it; an out-of-class constructor definition not matched to its in-class
declaration; an explicit destructor call through an enclosing namespace's type;
`std::nullptr_t` as a parameter type, written `i64`; the `_GLOBAL__N_1` name an
unnamed namespace gives what it declares; a `declare global` for an `extern`
object nothing uses; and 13.5.6's `operator->` not read as a call.

Defects no fixture reaches, kept here because a sweep found them and not a test:

- An anonymous *struct* member declares nothing, so `s.a` for
  `struct S { struct { unsigned a; unsigned b; }; unsigned c; };` names nothing
  here and 4 bytes are laid out where the references and g++ lay out 12; and a
  member of an anonymous *union* is addressed without the union's own `index`
  step. 9.5p1's injection is written for the anonymous union alone.
- 12.8p7's implicit copy constructor is declared by no class, so
  direct-initialization from an object of the class's own type is refused -
  `YA q(p);`, a mem-initializer `: m(v)`, `YB(YB(2))` - and so is 4.10p3's
  derived object passed to a base parameter by value. Copy-initialization
  reaches the same object through 12.8p31's elision, which is why only the
  direct forms show it.
- The parser reads no braced functional cast, `int{}` as well as `YA{}`, so
  `YA v = YA{};` is refused where the references accept it.
- A returned prvalue of class type is copied into its storage after the call
  rather than before it: the same three instructions in the other order, because
  the storage is decided in the lowering and not in the tree.
- A conditional over two lvalues of class type is an lvalue here and an object
  for the references, and `static_cast<const YA&>(YA(4))` names its temporary
  `tmpobj` where they name it `arg`.
- A subscript of a multi-dimensional array decays the row it reached, so
  `w[0][0]` writes one `unary decay ptr` the references do not.
- An array whose elements a braced list value-initializes - `E a[3] = {}` - is
  written here as 12.6p1's per-element construction reached through the
  subscript the source would write, and by the references as the byte cursor an
  aggregate initialization already carries, holding a call of the synthesized
  aggregate constructor above. The elements are given the same values, and at
  namespace scope neither writes anything at all.
- 3.6.2p2's constant initializer is not modelled for a constructor, so
  `struct YA { int a; YA() : a(1) {} }; YA g[3] = {};` is a startup call per
  element here and the folded image `i32 1` three times there - while `YA g;` of
  the same class is a startup call for both.
- A clause of an aggregate is evaluated before the storage unit a bit-field
  joins it into is loaded, where the references load and then evaluate. 1.9
  leaves the order open; it is 9.6p2's initialization path and predates C8.
- A cast written on a null pointer constant is one `copy ptr 0` the references
  do not write, so `f((char*)0)` passes a temporary where they pass the constant.
  It is 5.2.9's own path and has nothing to do with the class it is passed to.
- A member named through a qualified-id on an object is refused - `d.YD::f()`
  and `d.YB::f()` alike, with and without a base and with and without a
  using-declaration. It is 5.2.5p1's qualified-id and predates the object model.

Four shapes the references and this unit disagree about what a program means,
each resolved for the standard: 8.5p7 zero-initializes a value-initialized
object whose class wrote no constructor *before* the non-trivial one it was
given runs, which the references do not write, so
`struct YA { int a = 7; int b; }; YA()` leaves their `b` holding what the
storage held; 12.1/12.4 run a constructor and a destructor whose body is empty,
which the references elide along with the object's whole lifetime - `B() {}` in
a base and `~B() {}` alike, whether the derived class wrote its constructor or
inherited it; the references pass a class holding a bit-field by address
where they pass every other class of the same size by value; and 12.9p1's
candidate set of inherited constructors holds the base's parameter-type-list and
the shorter ones its defaulted parameters leave, with 12.9p2's characteristics
carrying no default-argument, where the references inherit one declaration and
copy its defaults onto it - which makes `struct YB { YB(int = 1); }` inherited
beside 12.1p5's default constructor ambiguous there and well formed here, as g++
reads it.

Three divergences are deliberate and named in the Performance Model: past 64
bytes the zero of a class object is one `zeroinit` where the references write
one eight-byte store however many there are - 512 of them for a 4 KB object; the
tail of a namespace-scope array no clause reached is one `zero` item where they
write one per element - 1 048 575 of them for a 1 MB array; and an array of a
class whose constructor does nothing, or whose whole initialization is 3.6.2p1's
zero, opens no `@__cppgm_init` at all.

15.4p14's `unwind=no` is separate and needs no test result: the relaxed
comparison strips the field, and emitting nothing is silence rather than a false
claim.

## Active Checkpoint

Done: **audit of C9 - the sibling places a brought-in declaration is used, and
the question a complete class answers**, 210 / 247 held with pa1-pa15 at
1173/1173, and ten regression tests added (220 / 257).

C9's architecture stands: a using-declaration in a class makes a declaration of
that class, and every use of it reaches the one `shadowed` names. What the audit
found is that the rule was written at the places C9's own fixtures name and left
at the siblings, and that two questions about the complete class were answered
where the body had got to. 13.3.1.2's operator expression called the class's own
declaration - `@YD__operator_` under `_ZN2YDplEi`, a symbol no unit defines - on
the derived object rather than the base subobject; 13.4's address of an
overloaded brought-in name did the same in three places; and 8.3.6's
default-arguments were unreachable from what the class declared. Naming a
function is one description in one place now, so all of them reach the base's
declaration. 7.3.3p14's hiding and 12.9p1's "unless the class declares one with
the same signature" are settled where 9.2p2 completes the class: the hiding was
n^2 in the declarations of one name (2.53 s at 4000, 0.27 s now) and left a
hidden declaration in 13.1's index, so a third overload was read as a
redeclaration; the inheriting rule repurposed a declaration in place, which gave
a constructor the access of the using-declaration's section rather than of its
own, in both directions. 12.9p1's candidate set is the base's parameter-type-list
and the shorter ones its defaulted parameters leave, with 12.9p2's
characteristics carrying no default-argument. 7.3.3p14 compares the parameters a
declarator wrote with 8.3.5p7's cv-qualifier-seq beside them, so a static and a
non-static member function of one class meet where 9.4.1p2 says they must. The
base-object entry a call names is declared where this unit holds no body. And a
constructor declared with an ellipsis read one type past its parameter list, in
the analysis and again in the lowering - two out-of-bounds reads, now one shared
description of how an argument is passed.

A 458-program differential sweep through `cppgm++-ref` - the cross product of
base access, member access and member kind; every constructor shape against
every derived declaration and every member the derived class adds; 7.3.3p14's
hiding in both orders over four base overload sets, five derived declarations
and four calls - found all ten, and every disagreement left standing was put to
g++, which agrees with this unit on all of them but the shapes this milestone
does not read.

Next: **C10 - the `thread_local` storage duration and the `__builtin_*` names**,
which is 6 of the remaining fixtures and the two largest groups left.

- owner: `sema_declarator.cpp` for 3.7.2's storage duration as a fact of the
  declaration, `lowir_abi.cpp` for the symbol family 3.7.2p2's accessor stands
  under, and `lowir_lower.cpp` for the `storage=thread_local` a global carries
  and the wrapper function a use of it calls; `sema_expression.cpp` for the
  small set of names 1.4p8 lets an implementation reserve.
- data flow: the declaration records the duration, the unit lowering writes one
  global with that storage and one accessor per object with a dynamic
  initializer, and a use of the name calls the accessor rather than naming the
  global.
- expected complexity: one flag per declaration and one accessor per object that
  needs one, so a unit with n thread-local objects writes n accessors and no
  work per use beyond the call.
- validation: `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`,
  `make test-report-through-pa15`,
  `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`, valgrind over
  the six fixtures, and a differential sweep of storage-duration and
  initializer shapes through `cppgm++-ref`.

## Performance Model

- 7.3.3p1's using-declaration in a class costs one pass over the declarations
  the name has in the base - which is what the source wrote - and one probe per
  declaration of 7.3.3p14's signature, so the members it brings in are declared
  in as many steps as there are of them. 10.2's lookup then finds them in the
  class itself and walks no base chain for them, and a use of one costs one
  pointer test to reach the declaration it names. Measured at
  500/1000/2000/4000, each doubling 1.9-2.2x: n members of one base each brought
  in by a using-declaration of its own, 0.01/0.03/0.06/0.13 s; one
  using-declaration bringing in n overloads, 0.02/0.03/0.07/0.14 s; n classes
  deep, each bringing in the one before it, 0.01/0.02/0.05/0.10 s; n calls of a
  brought-in member in one body, 0.01/0.03/0.05/0.11 s and 6 n + 17 lines.
- 7.3.3p14's hiding is asked where 9.2p2 completes the class, once for each name
  a using-declaration brought a function in under: one pass over the
  declarations that name has to say which signatures the class declared itself,
  and one more to take the brought-in declarations of those signatures off the
  chain. A class that wrote no using-declaration answers nothing at all, and one
  that did pays the declarations it has rather than their square - n brought-in
  overloads all hidden by n declarations of the class's own,
  0.03/0.06/0.13/0.27 s at 500/1000/2000/4000, where asking it once per member
  declaration had been 0.05/0.15/0.61/2.53 s.
- 12.9's inherited constructors are one pass over the base's chain with one
  probe of 13.1's index per member of 12.9p1's candidate set, so a base with n
  constructors is inherited in n steps and a chain n classes deep costs n per
  class and not n^2. Measured at 500/1000/2000/4000, each doubling 2.0-2.2x: n
  constructors of one base all inherited, 0.02/0.04/0.09/0.29 s and 48 lines
  whatever n is; n classes each inheriting the one before it,
  0.03/0.06/0.12/0.26 s and 13 n + 14 lines; n objects built through one
  inherited constructor, 0.03/0.05/0.10/0.21 s and 10 n + 36 lines. The first of
  those grows the same way with no using-declaration at all -
  0.02/0.03/0.07/0.22 s for the same n constructors - so the last doubling is
  the type layer interning n distinct parameter types and not the inheritance.
- 12.9p1's candidate set is one constructor per parameter list a base
  constructor's defaulted parameters leave, so one declaration with n of them is
  n inherited constructors holding n^2/2 parameters between them. That is what
  12.9p1 describes rather than a walk this unit adds: 500/1000/2000/4000
  defaulted parameters take 0.02/0.05/0.16/0.56 s, and the output stays 2 n + 40
  lines because only the constructor a use asks for is defined.
- 13.1's index of a class's constructors is keyed by the chain the class holds
  and the parameter list, so declaring the nth constructor, asking whether a
  base's is already declared, and 12.9p1's "unless this class declares one with
  the same signature" are each one probe rather than a walk. A declaration a
  using-declaration brought in is never in that index, because 7.3.3p14 hides it
  rather than letting a declarator redeclare it.
- The ABI's two entry points are two definitions of one body where a complete
  object and a base subobject both asked for one, and two declarations where
  this unit holds no body, so a program with n such classes writes 2 n of them
  and not 2^n or n^2: 500/1000/2000/4000 classes each constructed and destroyed
  both ways take 0.17/0.36/0.73/1.49 s for 92 n lines, each doubling 2.0-2.1x.

- 12.6p1's array of class type costs one constructor selection and one node in
  the tree however many elements it has: the action names the array, and the
  lowering writes the n calls the source asks for. The element's address is one
  `decay` and one step per dimension, so a declaration of n elements is n calls
  and no work quadratic in n anywhere. Measured at 500/1000/2000/4000, each
  doubling 1.9-2.1x: one local array of n elements constructed and destroyed,
  0.013/0.023/0.046/0.087 s and 10 n + 31 lines; n arrays of three elements in
  one body, 0.042/0.076/0.153/0.315 s; a namespace-scope array of n aggregate
  clauses as static data, 0.007/0.010/0.016/0.031 s and 2 n + 13 lines; the same
  array with a clause the translation does not know, so every element is written
  before the program runs, 0.024/0.044/0.091/0.180 s; a local array of n
  aggregate clauses, 0.012/0.019/0.036/0.075 s; and 250/500/1000/2000 rows of a
  two-dimensional array, 0.018/0.030/0.060/0.122 s. An array member value
  initialized by `m()` is one `zeroinit` past `kZeroSpanLimit` however large the
  bound, so 500 and 4000 elements are both 25 lines in 0.003 s. n classes each
  holding an array of two of the next cost 2 n calls and not 2^n: depth 16 is
  675 lines in 0.005 s.
- A class with n elements of a class whose constructor does nothing is written
  nothing at all - not the address of an element for a call that is not made.
- An object with static storage duration whose whole initialization is 3.6.2p1's
  zero opens no startup body: a namespace-scope array value-initialized by `{}`
  is 14 output lines at 500 elements and at 4000, where it had been 5 n + 18 -
  20 018 of them at 4000, each writing zero into storage the program image
  already holds.
- The tail of a namespace-scope array no clause reached is one `zero` item
  whatever it holds, so `char buf[1 << 20] = {1};` is 6 lines here and 1 048 580
  from the references. The object image is the same either way.
- 7.6.2's alignment is one constant evaluation and one comparison per
  alignment-specifier in the one 9.2p13 pass, so a class with n of them is laid
  out in n steps: 4000 costs 0.03 s and 23 lines of output. The alignment a
  declaration asks for is read from the specifiers written before the type -
  which is where 7p1 puts a fact about what is declared - so the sequence is
  walked once and nothing is re-read.
- An array of 2^d elements d dimensions deep costs one step per dimension per
  element, which is the elements it has times the subscripts one of them would
  be written with: 982 output lines at d = 6 and 110 614 at d = 12, 0.00 s and
  0.26 s.

- A copy of one object of class type is one `copyobj`, written where the object
  it goes into is known: an initialization writes into the storage the
  declaration already named, an assignment into the one the left operand names,
  an argument into `argobj__n` - written or defaulted - a return into
  `retobj__n`, an arm of a conditional into the object the conditional is, and
  5.2.9p4's cast into the temporary it makes. Nothing allocates a slot for a
  copy the place asking already owns storage for, so the frame of a function with
  n class copies holds n fewer slots than the checkpoint gave it: 4000
  `YA q = p;` are 40 010 lines and 4002 slots where they had been 52 010 and
  8002, and 4000 conditionals 100 012 and 8003 where they had been 136 012 and
  20 003. Measured at 500/1000/2000/4000: copies 0.04/0.06/0.10/0.17 s;
  by-value arguments 0.04/0.05/0.07/0.12 s; written temporaries passed by value
  0.04/0.06/0.09/0.15 s; 13.3.3.1.2 conversions 0.04/0.05/0.09/0.15 s; returned
  temporaries 0.05/0.09/0.16/0.29 s; value-initializations 0.04/0.07/0.11/0.21 s;
  conditionals 0.05/0.09/0.15/0.27 s; class nesting depth 0.03/0.05/0.07/0.13 s.
  Every one writes output exactly linear in its size.
- 12.8p25 - whether a copy of a class is the copy of its bytes - is one flag on
  the class, settled in the pass 9.2p13's layout already makes over
  `Scope::declarations`, from the class's own constructors, its base's flag and
  its members'. A copy costs one probe of it.
- 12.2p1's temporary costs one slot and one constructor selection, both of which
  a declaration of an object of the same class already costs, and the object is
  named once however many readers it has: the slot is made the first time the
  node is reached and the address it was constructed at is what every later
  reader uses, so a temporary bound to a reference and then read through it
  emits one `addr` and not two. Measured at 500/1000/2000/4000, each doubling
  1.9-2.3x: n written temporaries in one full-expression each bound to a
  `const T&`, 0.01/0.02/0.04/0.09 s; n by-value class arguments,
  0.01/0.01/0.03/0.07 s; n arguments reaching a class parameter through
  13.3.3.1.2's converting constructor, 0.01/0.02/0.03/0.08 s; n statements each
  calling a temporary functor, 0.01/0.02/0.05/0.11 s; n value-initialized
  32-byte class objects, 0.02/0.04/0.08/0.17 s. Nesting `f(T(...))` 25 to 200
  deep - the parser's depth limit - costs two output lines per level.
- 13.3.3.1.2's search is one walk of the declarations of the class's constructor
  name, so a class with n constructors costs n per argument that reaches it and
  a class the argument already has the type of never starts the walk.
- 12.8p15's copy is one `copyobj` of the class's own span rather than one store
  per member, and a class that holds nothing is written nothing at all - which
  is what `TypeTable::is_empty_class` answers in one probe, from the flag 9.2p13
  layout already computed.
- 8.5p5's zero of a class object is written as the widest stores that fit while
  the object is small enough for that to be a description of it, and as one
  `zeroinit` past `kZeroSpanLimit` (64 bytes). The references have no such
  limit: they write one eight-byte store per eight bytes however many there are,
  which is 512 instructions for a 4 KB object and 131072 for a one-megabyte
  member. No fixture reaches past 64 bytes, and the same limit already governs
  8.5.1p7's span of value-initialized array elements, so the two agree.
- 9.6p2's allocation unit is the same one pass: the layout carries a byte cursor
  and the open unit's type, first byte and used bits, so a bit-field costs one
  comparison and one addition and a class with no bit-field never opens a unit.
  Each access reads `bit_width`, `bit_offset` and `bit_access` off the member
  and emits a fixed number of instructions, so n accesses to a field cost n.
  Measured at 500/1000/2000/4000, each doubling 2.0-2.3x: n one-bit fields in
  one class, aggregate-initialized then assigned one by one,
  0.02/0.05/0.10/0.23 s; n fields of four alternating types, so every one opens
  its own unit, 0.02/0.03/0.07/0.12 s; n classes nested one inside the next,
  each with two fields, 0.01/0.02/0.06/0.13 s; n reads of two fields in one
  body, 0.02/0.04/0.08/0.16 s; n namespace-scope objects of a two-field class,
  each dynamically initialized, 0.02/0.04/0.10/0.20 s.
- Which storage units an initialization has already written is one carried byte
  count per open initialization - a bit-field may take its unit whole exactly
  when the unit begins at or after it - so a class with n bit-fields is
  initialized in n steps and not n^2. Under 9.6p2's unit model that count also
  says a unit can never reach back over a member the initialization has already
  written, which is what keeps a whole-unit store out of the base subobject and
  out of the members beside the field.
- Class layout is one pass over `Scope::declarations` at 9.2p2 completion and is
  never recomputed; `TypeTable::complete_class` already caches size/alignment.
  A base contributes its own cached size and alignment, so a chain of n derived
  classes is laid out in n passes and not n^2.
- Field access reads `SemaEntity::offset` — no walk of the class per access.
- 10.2's base chain is a pointer on the region, walked only where a class has
  one: a lookup in a program with no inheritance pays one null test per
  enclosing region, and one with inheritance pays the depth of the hierarchy
  rather than its size.
- 11.3's friendship is a set of entity pairs held by the model, and every access
  check asks `has_friends` first, so a program with no friend declaration pays
  one test per region it walks and nothing else. 11.2p5's naming class adds a
  walk of the object's base chain, taken only for a protected member the
  declaring class did not already grant.
- 13.3.1.2's candidate set is gathered once per operator expression: one lookup
  in the operand's class, one unqualified lookup, and one pass over the
  associated namespaces and classes the operand types reach. Which declarations
  the set already holds is a probe, so gathering costs the declarations there
  are and not their square.
- 3.4.2's association follows the type rather than searching: the depth of the
  type and of the base chain, both of which the source wrote. Whether a region is
  already in the set and whether a class's base chain has been walked are two
  separate probes, so a second argument of one type stops at once while a class
  the set holds without its bases does not stop the walk. One call with 4000
  arguments of 4000 distinct associated classes gathers its set in 0.29 s.
- A derived-to-base conversion is one `base-conversion` node however many
  classes it spans. n accesses to a member d classes up cost n*d to analyse and
  emit n instructions rather than n*d: 4000 accesses 4000 deep are 20 007 lines
  in 2.0 s, where writing a node per link was 16 012 007 lines in 54.6 s.
- Demand-driven emission is monotonic: `emitted_functions_` admits each symbol
  once, and 3.2p3's uses are read from the whole resolved tree in one walk, so a
  function used n times is lowered once and a body no written body asked for is
  asked for once, after them.
- The internal LowIR symbol of a function is indexed by the name the object file
  gives it, which is one probe and is what keeps two operator functions of one
  region that flatten to one base name with the same parameter types from
  collapsing onto one symbol.
- 12.6.2's mem-initializers are indexed by member name once per constructor, so
  a class with n members each named in the ctor-initializer costs n lookups
  rather than n^2 comparisons.
- 12.6.2p10's order is `Scope::declarations` with the base before it, so one pass
  writes every subobject initialization, and 12.4p8 walks the same list backwards
  with the base after it.
- 12.1p5: a subobject whose default-initialization does nothing gets no node at
  all, so an empty base or member costs nothing in the tree or in the output.
- A slot named after an identifier another slot already took starts from the
  suffix that identifier last used, so n blocks declaring one name cost n steps.
- A jump out of a set of blocks writes one destructor action per object those
  blocks hold, which is what 3.8p1 asks for. Whether any object is alive at all
  is a carried count, not a walk of the open blocks.
- Aggregate initialization is one node per subobject a clause reached, and one
  node for the whole tail of an array no clause reached (`kZeroFillLimit`, 64
  bytes). Measured: a struct holding `char buf[1 << 20]`, initialized `{{0}, 3}`
  at namespace scope and `{{1}, 2}` locally, compiles in under 0.01 s.
- Measured after the audit of C5, sizes 500/1000/2000/4000, each doubling
  2.0x-2.4x: one call with n arguments of n distinct associated classes,
  0.02/0.06/0.12/0.29 s; base-chain depth with one ADL call two arguments deep,
  0.00/0.01/0.03/0.06 s; n ADL calls each four classes above the friend,
  0.01/0.02/0.05/0.10 s; n calls whose first argument is a nested enum and whose
  second is a class eight deep, 0.01/0.03/0.06/0.11 s, and that shape by chain
  depth with one call, 0.00/0.01/0.03/0.08 s; a chained `operator<<`,
  0.00/0.01/0.01/0.03 s; operator nesting depth, 0.01/0.01/0.03/0.06 s; n
  namespaces each with a class and an ADL free function, one use each,
  0.04/0.10/0.21/0.43 s; n friend declarations revealed by as many
  namespace-scope definitions, 0.03/0.07/0.15/0.35 s.
- One class with n friend `operator<<` overloads used n times is quadratic and
  is meant to be - n calls each ranking n candidates: 250/500/1000/2000 take
  0.05/0.15/0.62/2.99 s. Scanning the candidates already gathered had made it
  cubic.
- Nested namespace depth is super-linear and belongs to the scope layer: 500 to
  4000 namespaces deep with one ADL call at the bottom take 0.01/0.02/0.07/0.24 s,
  unchanged by the object model.
- Measured after the audit of C4, sizes 500/1000/2000/4000, each doubling
  2.0x-2.3x: derived-to-base conversions in one body four deep,
  0.01/0.02/0.03/0.07 s; accesses to a base's member in one body four deep,
  0.01/0.01/0.03/0.08 s; chain depth with one access to the root member,
  0.01/0.02/0.04/0.09 s; 11.4p1 protected accesses through a five-deep chain,
  0.00/0.00/0.01/0.02 s; classes in a chain with nothing used,
  0.01/0.02/0.04/0.09 s; a chain of 100/200/400/800/1600 derived classes each
  with its own member and constructor, 0.00/0.01/0.02/0.05/0.10 s;
  500/1000/2000/4000 classes each deriving from one base, constructed and
  called, 0.04/0.09/0.20/0.43 s.
- Measured earlier and unchanged, each doubling about 2.1x-2.3x: 4000 default
  member initializers in one class, 0.06 s; 4000 mem-initializers in one
  constructor, 0.07 s; 4000 locals with destructors in one block, 0.09 s; 4000
  namespace-scope objects constructed and destroyed, 0.09 s; 4000 loops each with
  a `break` leaving one object, 0.33 s; 2000 constructor overloads chosen between
  for one call, 0.06 s; a single `break` unwinding 800 nested blocks, 0.04 s;
  8000 members laid out and initialized twice, 0.14 s; 4000 member accesses and
  calls in one body, 0.20 s; nested aggregate depth 800, 0.80 s.
- 3.8p1 makes a return destroy every object of every block it leaves, so n nested
  blocks each holding an object and a return emit n^2/2 calls: measured 400 deep
  at 0.36 s for 166425 lines. That is what the source asks for, not a re-walk.
- Nested block scopes cost more than linearly in their depth, and did before the
  object model: 4000 nested blocks holding one scalar each take 0.23 s. The cost
  is the lookup walking enclosing regions; it belongs to the scope layer.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | member function call + class object in LowIR: field offsets, `.`/`->`/implicit `this`, implicit object argument in 13.3.1, `constructor-action` lowering with trivial elision, demand-driven inline emission, 9.4.2p3 static-member folding, member-function ABI names | 33 -> 55 / 243; pa1-pa15 clean |
| C2 | 11 access control (per-member access, checked on `.`/`->`/qualified names), 8.5.1 aggregate initialization (brace elision, string-literal array members, value-initialized tails, static data for namespace-scope aggregates), 8.5.4p7 narrowing, 7.6.2 `alignas` on a class-head | 55 -> 65 / 243; pa1-pa15 clean; valgrind clean on the new paths |
| audit of C1-C2 | 9.4.2p2's definition told from its declaration by the declarator-id; 12.6.2p8 refused rather than dropped; 7.6.2p1's type-id form; 5.2.5p1's object expression kept or refused, never dropped; 13.3.3.2p3 ordering two sequences that differ only in qualifiers; 11p6's naming context; 9.3p2 read from where the definition is written; O(n^2) slot naming | 65 -> 70 / 243; pa1-pa15 1173/1173; valgrind clean over 249 inputs; every axis linear |
| C3 | 12.1/12.4 user-declared constructors and destructors in a class body, chained on the class; 13.3.1.3/13.3.1.4/8.5.4p3 constructor selection over 8.5's four initializer forms with `explicit`; 12.6.2 member initializations in declaration order, 12.6.2p8 default member initializers, 12.4p8 member destructions; 3.8p1 lifetime at block exit, at `return` and in 3.6.3p1's `@__cppgm_fini`; 8.4.2/8.4.3 `= default` and `= delete`; 12.8p31 copy elision from a value of the object's own type; 5.2.4 explicit destructor calls; C1/C2 and D1/D2 ABI names with the `alias object` line | 70 -> 102 / 243; pa1-pa15 1173/1173; valgrind clean on the new paths |
| audit of C3 | six ways out of a region that ended no lifetime - `break`, `continue`, `goto`, the for-init-statement's own region, a static data member at shutdown, an aggregate initialized from braces; 9.3.2p1's `this` in a destructor separated from 12.4p12's object parameter; a deleted destructor refused where the object is declared; `= T(...)` no longer refused; a mem-initializer that names nothing or is written twice refused; 9.3p2's inline read from where the definition is written; one `_` per character an identifier cannot hold; the goto check made a carried count | 102 / 243 held; pa1-pa15 1173/1173; valgrind clean over 273 inputs; every axis linear |
| C4 | 10p1's base-clause recorded on the class and its region; 9.2p13 layout with the base subobject at offset 0; 9p2's injected-class-name; 10.2p2/p6 lookup through the base chain; 11.2p2/p4 and 11.4p1's access; 12.6.2p5's base initialization first and 12.4p8's base destruction last; 12.1p5/12.4p3 triviality through the base; 4.10p3, 8.5.3p4 and 5.2.9p11's conversions as one `base-conversion` node with 13.3.3.1.4p1's rank; 5.9p2's composite pointer type; 5.16p3's conditional over a class and a base of it; the object model split out into `sema_class.cpp` | 102 -> 126 / 243; pa1-pa15 1173/1173; valgrind clean over 243 inputs and the sweeps |
| audit of C4 | one derived-to-base conversion written as one node however many classes it spans; 11.2p5 asked of every link; 5.16p6's operands brought to the composite pointer type; 8.5.3p4's base half of reference-related; 5.2.9p11's reference downcast written and its pointer downcast access-checked; 12.4p11's destructor access asked for an object, a member and a base; 11.4p1's additional check on a protected member named through an object; the ABI entry a constructor or destructor stands under made a fact the analysis records | 126 / 243 held; five `.ref` files newly byte-identical; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and 88 probes; the n*d output blow-up gone |
| C5 | 13.3.1.2p1 an operator on a class or enumeration operand read as the call it stands for - member candidates from 13.3.1.2p3, non-member ones from ordinary lookup with the member functions left out, 13.3.1.2p4's first operand offered to both, and the built-in operator left to the caller where nothing is viable; 13.5.7p1's `x++0`; 13.5.3/13.5.4/13.5.5's member-only `= () []`; 13.5p6's rule on a non-member operator; 11.3p6 a friend declared into the innermost enclosing namespace and bound nowhere, 7.3.1.2p3 revealed by a matching declaration there, 11.3p11's elaborated-type-specifier declared in that namespace too, 11.3p1/p2's grant and 11.2p5's naming class; 3.4.2p1/p2/p3's associated namespaces and classes and the friend declarations they make visible; 3.4.3's prefixes tried outward, without which `nnn::f(a)` parsed as a declaration; 3.2p3's uses read from the whole resolved tree; two operator functions of one region no longer collapsing onto one internal symbol | 126 -> 161 / 243; pa1-pa15 1173/1173; valgrind clean over the 42 operator, friend and ADL fixtures and the sweeps; chain, nesting, namespace-depth, ADL-multiplicity and reveal axes all linear, and the one quadratic axis - n overloads ranked by n calls - made quadratic rather than cubic |
| audit of C5 | 3.4.2p2's base chain abandoned wherever the class was already associated - which it is, without its bases, whenever it is the class a nested type is a member of; every class around a nested type associated where the clause associates the one it is a member of; 11.2p5's naming class and 11.4p1's additional check asked at neither operator-call site; a member `- + * &` given the unary Itanium terminal because the arity left out the operand 9.3.1p3 put in the type; an out-of-class definition of a static member function given an object parameter, declaring a second function the unit then called with no argument and, where `inline`, never defined; 13.5p6's static-member half; a pointer condition branched on through a `cmp ne ptr` the references do not write | 161 -> 163 / 243, nothing that passed before failing after; pa1-pa15 1173 / 1173; valgrind clean over 243 fixtures and 47 probes; every ADL association axis linear at 2.0-2.4x per doubling and the one quadratic axis unchanged; the stripped metadata agrees with the refs for all 141 passing fixtures with a reference but for `unwind=no`, and the ABI names of every unary and binary form of `+ - * &` agree with g++ |
| C6 | 9.6p1's width read as a constant expression and the four facts it settles put on the member's own declaration - `bit_field`, `bit_width`, `bit_offset` and 4.5p3's `bit_access`; 9.6p2's allocation into storage units by a layout cursor counted in bits, with the unnamed zero-width separator and the field that would straddle a unit moved on; a read that loads the unit at the promoted type, shifts the field down and masks it, while the value keeps the type the member was declared with; 9.6p2's write as a read-modify-write, and as a plain store where the initialization still owns every byte of the unit; 8.5.1's unnamed field stepped over by the clauses; 12.6.2 and 8.5.1 writing the two instruction orders the references write; 5.17 and 5.3.2 over a field, with 4.5p3's promoted type for the arithmetic; 5.3.1p3 and 5.3.3p1 refusing the address and the size of one; 3.6.2p2's static data written as the bytes the fields' bits fall in rather than as the units they are read through | 163 -> 173 / 243; pa1-pa15 1173/1173; all ten bit-field fixtures byte-identical; valgrind clean over the fixtures and 8 probes; layout and static-data bytes agree with g++ over 17 layout shapes and 9 value shapes; every axis linear |
| audit of C6 | 9.6p2's storage unit made a run of bytes with its own width, alignment and type, opened by the first field that cannot share the one before it and shared only by fields declared with its type - so a bit-field no longer packs into the bytes of the member before it and a derived class's field no longer covers the base subobject its constructor then stored over; the unit loaded and put back at the signed integer of its own width where 4.5p3's promoted type had named a type narrower or wider than the storage; an initialization joining the unit as an expression of the member's own type, with 4.5's promotion and 4.7's conversion at each step; both masks dropped for a field that owns every bit of its unit; 3.6.2p2's static image given to `@__cppgm_init` where a data item cannot name a share of a unit, and the zero of a unit written once for every field in it; a clause let through to an unnamed bit-field; an assignment converting its value before naming the object it writes into; a constant initializer spelled as the value it produces; 4.12's conversion to `bool` compared at the type of what it converts | 173 -> 174 / 243, nothing that passed before failing after; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and 87 probes; every bit-field shape the reference accepts agrees byte for byte over 17 layout shapes and 15 declared types; every axis linear |
| C7 | 12.2p1's prvalue of class type made an object the function holds - `T(args)` and `T()` declaring a temporary no name reaches, 8.5/13.3.1.3 choosing its constructor, and the storage named `tmpobj__n`, `arg__n` or `argobj__n` after what asked for it; 8.5.3p5's reference bound to that object; 13.3.3.1.2p1's user-defined conversion sequence as the same temporary made by a converting constructor, ranked below every standard conversion sequence and above the ellipsis, with 12.3.1p2's `explicit` left out and one user-defined conversion per sequence; 5.2.2p4's argument of class type copied into a generated `argobj__n` slot the call is passed, with 12.8p31 creating a prvalue argument in that slot rather than copying into it; 12.8p15's copy made memberwise, so a class that holds nothing moves nothing - at an argument and at a parameter's entry alike; 8.5p7's zero-initialization of a value-initialized class with no user-provided constructor, written as the zero of its bytes; 12.2p3's temporary of a class with a non-trivial destructor refused rather than left alive past the full-expression; the object model of the lowering split out into `lowir_lower_object.cpp` | 174 -> 186 / 243; pa1-pa15 1173/1173; valgrind clean over 243 fixtures and the probes; 18 of 21 synthesized prvalue, by-value, conversion and value-initialization shapes byte-identical to the reference, the other 3 differing only in the `zeroinit` limit; every axis linear at 1.9-2.3x per doubling |
| audit of C7 | 5.2.2p4's copy owned by the call taken out of `converted`, which every initialization, assignment, return, conditional arm and cast reaches, and written at each of those places instead - one `copyobj` into the object already known, and no call's slot allocated for a copy that place already owns storage for; the storage a temporary takes named by what asked for it, which only a call's argument and a return's value do, so a reference a declaration binds keeps `tmpobj__n` and 13.3.3.1.2's temporary reaching a by-value class parameter is the `argobj__n` 12.8p31 makes it; 12.2p1 given to the other prvalue of class type, so a call that returns one by value no longer hands back a value where `this` or a reference needs an object; 12.8p25 made a fact the class carries and asked at the one place a class object is copied, so a copy the program wrote is refused rather than written as the copy of the bytes; a declaration and the initialization under it naming one address rather than two | 186 / 243 held, the same set passing and failing; pa1-pa15 1173/1173; byte-identical passing fixtures 110 -> 116 of 164; valgrind clean over 243 fixtures and 130 probes; eight axes linear and the per-copy slot and instruction growth gone |
| C8 | 12.6p1's array of class type constructed element by element and 12.4p8's destroyed the same way, as one action naming the array that the lowering writes the calls of; an element addressed the way the source would name it, a dimension at a time, which 8.5p7's value-initialized array member, 3.6.2p2's dynamic initialization of a namespace-scope array and both lifecycle calls share; 3.6.2p2's static image of an array of class type, with an element's own padding and a fallback to `@__cppgm_init` for a clause the translation does not know; an aggregate clause's value computed before the address it is stored into; 7.6.2p1's alignment-specifier kept where a decl-specifier stands and read by 9.2p13's layout; 5.3.6p1's `alignof` as an expression; 9.1p2's qualified class-head-name defining the class that region declared | 186 -> 199 / 243; pa1-pa15 1173/1173; valgrind clean over the new fixtures and the probes; a 70-shape differential sweep against `cppgm++-ref` found and closed four defects no fixture reaches; every axis linear at 1.9-2.1x per doubling |
| audit of C8 | an alignment-specifier read from wherever it stood among the decl-specifiers, where 7p1 gives one written before them to what the declaration declares and one written after them to the type those specifiers named, so `struct S { char c; int alignas(8) x; };` laid `x` at 8 and made its class 16 where g++ and the references write 4 and 8; 7.6.2p3's fundamental alignment never asked for, so `alignas(6)` allocated a member at every sixth byte, `alignas(6)` on a class-head made the class six and `alignas(-4)` made it one, all three refused by g++ and by the references; 8.5p7's zero of an object with static storage duration written into a startup body 3.6.2p1 had already made unnecessary, one store per element - `YA g[4000] = {};` at 20 018 lines writing zero into storage the program image holds zero, and the empty `@__cppgm_init` for `YA g = YA();` with it | 199 / 243 held, the same set passing and failing; pa1-pa15 1173 / 1173; byte-identical passing fixtures 127 of 177, the rest differing only in top-level order, the internal symbol name and `unwind` / `trivial_lifecycle`, with `pass=` agreeing on every one; valgrind clean over 243 fixtures and 116 probes; nine axes linear at 2.0-2.2x per doubling and the per-element startup work gone - 14 lines at 500 elements and at 4000; file audit passes with the two recorded header-weight warnings |
| C9 | 7.3.3p1's using-declaration in a class made a declaration of that class per declaration the base has of the name, carrying 11p1's access and naming the base's through `shadowed`, with 13.3.3.1p4's object parameter naming the derived class and 11.2p5 leaving the base subobject `this` reaches unchecked; 7.3.3p14's hiding asked in both orders through a signature that leaves the object parameter out; 12.9's inheriting constructors declared from the base's with 12.9p4's access, the base's parameters and names, and 12.9p8's definition, with 12.1p5's default constructor still given to a class that only inherits; 13.3.3.2p3's cv tie-break kept through 4.10p3's derived-to-base conversion; the ABI's two entry points written as two definitions where a complete object and a base subobject both asked for one | 199 -> 206 / 243; pa1-pa15 1173/1173; valgrind clean over the seven fixtures and 72 probes; a 72-shape differential sweep against `cppgm++-ref` found and closed four defects no fixture reaches, each now a test, and g++ agrees with this unit on the well-formedness and the value of all 72; every C9 axis linear at 1.9-2.3x per doubling |
| audit of C9 | 13.3.1.2's operator expression and 13.4's address of an overloaded name read through `shadowed`, so what a using-declaration brought in is called and named as the base's declaration rather than as a symbol no unit defines, on the base subobject and with the base's definition emitted; 8.3.6's default-arguments read from the declaration that wrote them; a hidden declaration kept out of 13.1's index, so a third overload declared after two were hidden is a function of its own; 7.3.3p14's parameter-type-list made the one a declarator wrote, with 8.3.5p7's cv-qualifier-seq beside it, so a static and a non-static member function of one class hide each other where 9.4.1p2 does not let them overload; that hiding and 12.9p1's "unless the class declares one" both settled where 9.2p2 completes the class, which makes the order the body wrote them in irrelevant, gives a constructor the access of its own section, and takes the hiding from n^2 to one pass; 12.9p1's candidate set read as the shorter parameter lists a base constructor's defaulted parameters leave, with 12.9p2's characteristics carrying no default-argument; the ABI's base-object entry declared where this unit holds no body; a constructor declared with an ellipsis no longer reading one type past its parameter list, in the analysis or in the lowering | 210 / 247 held, the same set passing and failing, and 220 / 257 with ten regression tests added; pa1-pa15 1173 / 1173; byte-identical passing fixtures 143 of 197; valgrind clean over 257 fixtures and 460 synthesized inputs; a 458-program differential sweep against `cppgm++-ref` with every disagreement judged against g++; ten axes linear at 1.9-2.2x per doubling and the n^2 hiding gone (2.53 s -> 0.27 s at 4000) |
