# PA21 Audit — `cppgm++ --emit-lowir` with full `constexpr`

A review of each landed checkpoint, in the order a fact travels: the storage a
declaration asks the program for, the name the image gives it, the value the
image holds, and the initialization and destruction left for the program to run.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| L | `af299cb9` | 3 / 3 + 6 recorded | **the function part of a name that has to say *which* function, which two functions can spell the same way.**  3.7.1p3's object is named in the image by the body that declared it and where in that body it stands, and the where is a span of this unit's terminals only until 7.1.2p4 leaves a definition every unit may hold - then it is a counter per function, and the function part is a flattened qualified name that `f(int)` and `f(double)` share, and `value<1>` and `value<2>` with it.  So `template<int N> int value() { static int data = N; }` laid out *one* global for both instantiations, holding 2, which is what `300-nested-function-template-local-static-array` shows and what makes `value<1>()` answer 2.  Beside it, 3.5p3's internal linkage was left off both readings of "a definition every unit may hold", so `static inline int k() { static int t; }` gave the object a *weak* symbol and a counter place where the reference and 3.5p3 both give this unit's own object a `tokens` place and an internal one.  And 12.4p8 ends the lifetime of *each element* of an array where 3.6.3p3's runtime takes one function and one object, so a block-scope `static P p[2]` handed the runtime `~P` and the address of the array and ended one lifetime of the two - the same shortcut 3.7.2p2's hand-off already had, both now handing a body of the program's own that ends them all |

## Current Checkpoint Review

L is the one checkpoint PA21 has landed: `record_storage` records 3.7.1p3's
storage duration instead of refusing it, `record_lifetime` puts 12.4p11's
destruction under the declaration, `global_symbol` answers the name the image
gives storage the program spells no name for, and `lowir_local_static.cpp`
writes the image half, 6.7p4's guard and 3.6.3p3's hand-off.

The shape of it is sound and was traced end to end.  A block-scope `static` is
one fact on the variable - `SemaEntity::local_static` - and every reader that
had two answers for an object now has three: `record_lifetime` sends it to
`declared_lifetimes_` where a thread's object already stood, `write_initializer`
puts 3.6.2p1's constant in the image where a namespace-scope object's already
went, `local_variable` gives the block no slot and ends no lifetime of it in the
block, and `global_image` is the one reading of what an object with static
storage duration's image holds, now asked by both the definitions a namespace
writes and the ones a block does.  Nothing else reaches `local_static`: the only
writer is `record_storage` and the readers are the four above and
`demand_referenced`.

What the review found is on the other side of that: the *name*.  The checkpoint
made the name of the storage out of two facts - which function, and where in it -
and neither of them was asked in a way that answers for two.

### Findings

**1. Two functions flattened to one owner part, and the place beside it did not
tell them apart.**  `local_static_owner` wrote the qualified name of the
function where that name is spellable as an identifier, and `abi_qualified_name`
carries no signature and, for a function template's specialization, no argument
list - so `p(int)` and `p(double)` were both `p`, and `value<1>` and `value<2>`
both `value`.  For a definition this unit alone holds that is harmless, because
the place beside it is the span of terminals the init-declarator was written
from and no two declarations of one unit share one.  For 7.1.2p4's definition
every unit may hold it is not: the place is then `local<n>`, counted per
function so that two units reading one body agree, and two functions with one
owner part therefore agree on the whole symbol:

```cpp
template<int N> int value() { static int data = N; return data; }
int main() { return (value<1>() == 1 && value<2>() == 2) ? 0 : 1; }
```

laid out one `@__local_static__value__data__local0` holding 2, which both
instantiations read - `value<1>()` answers 2 where g++ and the reference both
answer 1.  `300-nested-function-template-local-static-array` is that program and
its diff is one global against the reference's two.  The object file already
answers "which function is this" - `LowirSymbolTable::function_symbol` gives a
second declaration of one name `__ov2` - so the owner part is now the spelling
only where the object file gives the function that very base name, and the
function's own symbol otherwise.  The spellings the reference writes for every
shape a fixture pins are unchanged.

**2. 3.5p3's internal linkage was missing from both readings of a shared
definition.**  `describe_symbol` and `writes_base_entry` each ask
`shared_definition(e) && !e.internal_linkage`, because a definition no other
unit may reach is this unit's own however it was written; the checkpoint's two
new readers asked `shared_definition` alone.  So `static inline int k() { static
int t = 8; }` and an `inline` function of 7.3.1.1p1's unnamed namespace gave
their objects `binding=weak` and a `local<n>` place, where the reference writes
`binding=internal` and a `tokens` place for both - a weak symbol for an object
no other unit can have, and the one place form that cannot be checked against
the terminals that name it.  Both readings now ask `local_static_shared`, which
is that clause written once.

**3. 12.4p8 over an array, handed to a runtime that takes one call.**
`local_static_destruction` gave `__cxa_atexit` the destructor and the address of
the object, and for `static P p[2]` that ends the lifetime of `p[0]` and leaves
`p[1]` standing - where the program's own shutdown body, reaching the same
`DestructorAction`, walks every element.  One rule with two implementations, and
3.7.2p2's `__cxa_thread_atexit` beside it had the same one.  Both now ask
`destruction_entry`, which is the destructor itself for every object but an
array and a body of the program's own for an array - `add_destruction`'s walk,
which is written out below `kArrayLoopLimit` elements and is a loop above it, so
a 100000-element array is one registration and eleven instructions.

### What the review confirmed rather than found

**The complexity is what the plan claims, re-measured.**  Every fact is asked
once per declaration and held: `entity_symbols_` holds the symbol,
`local_static_guards_` the guard, `local_static_places_` the counter, and
`destruction_entries_` the array body - so a name used *n* times costs one
flatten and *n* lookups.  400 / 1600 / 6400 image-initialized statics in one body
take 0.018 / 0.062 / 0.247 s and the same guarded take 0.026 / 0.096 / 0.423 s:
linear in both, against 0.584 / 0.743 / 1.403 s and 0.632 / 0.933 / 2.229 s for
`pa21/cppgm++-ref`.

**Two units reading one shared definition agree.**  A header holding an `inline`
function with three block-scope statics and a function template with two
instantiations, compiled as two units in one invocation, writes five globals and
each unit reaches the same one: the counter is per unit and per function symbol,
and both units walk the same body in the same order.

**Nothing else reads the span the parser now writes.**  `InitDeclarator::begin`
and `end` were unset before this checkpoint; the readers of an AST node's span
are the declaration, the parameter-declaration and the type-id, and none of them
is an init-declarator.

**Thirty shapes were swept for exit status through this compiler,
`pa21/cppgm++-ref` and g++**, and twenty-eight of the thirty through the
harness's own comparator: a static in a member function, a static member
function, a constructor, a local class, a loop, an `if`, a nested block, an
unnamed namespace, a friend definition; two declarators of one declaration; a
reference, a pointer, an array, an enum, a `double`, a string literal, a local
class type, a constexpr constructor, a constexpr call and a runtime call.  Every
exit status agrees but block-scope `thread_local`, and every LowIR difference is
one of the six recorded below.

### Recorded, not landed

**The place a shared definition's object is named by is still `local<n>`
where the reference writes the source position of the declaration**, hex-encoded
(` at file:line:col`).  Phases 1-7 keep no position: `IncludeTable` records only
whether a token was read from this unit's own file, which is the whole of what
survives phase 4.  The occurrence index is stable across the units of one
invocation and now unique across the program, which is what the object file
needs; it is not what the reference spells, so
`300-nested-function-template-local-static-array` and
`300-class-template-static-reference-dynamic-initialization` differ by their
symbol names alone.

**Two shapes where this compiler is the one that is right.**  The reference
never constructs an array of class type declared `static` in a block - `static P
p[2]` with `P() : a(3)` leaves the image zero and writes no call, so its program
returns 1 where ours and g++ both return 0.  And it guards a block-scope static
that holds an address: 3.6.2p1 makes `static int* p = &s;` and `static int& r =
s;` constant initialization and 6.7p4 has it done before the block is first
entered, which g++ writes as `.quad _ZZ1fvE1s` in `.data.rel.ro.local`; the
reference writes zero and a guard for both, and this compiler writes the image
value for the pointer and a pre-`main` initialization for the reference.

**The reference writes a dead `@__strlit__` for a string literal 8.5.2p1
consumed into an array element** and none for one consumed into the array
itself.  Nothing reads it; reproducing it would be writing a global the program
has no use for.  It is what `300-function-local-static-array-guard` differs by
after the decay below.

**A subscript of an array *element* that is itself an array emits one
`unary decay` the reference does not.**  It is not this checkpoint's: `nested[0][1]`
at namespace scope and `loc[0][1]` over an automatic array both write it, and
the reference indexes the element address directly.  It is the second blocker of
`300-function-local-static-array-guard`.

**Block-scope `thread_local` is still refused.**  Both oracles accept it.  The
storage the ABI gives it is reached through a wrapper of its own, which is a
different question from the one 6.7p4 asks, and the README's Assignment Boundary
names function-local `static` and not this.

**Two units that each define an internal function of one name write one
function.**  The reference does the same, and the local static inside it
collapses with it; it is a fact of the whole multi-unit model rather than of
this checkpoint.

## Changes

- **`lowir_local_static.cpp` — the owner part names one function.**
  `local_static_owner` keeps the qualified spelling only where
  `flatten_symbol_name(abi_qualified_name(owner))` is the function's own symbol,
  and carries `function_symbol_<hex>` otherwise.
- **`lowir_local_static.cpp` — `local_static_shared`** is 3.5p3's clause beside
  `shared_definition`, asked by `local_static_place` and `local_static_binding`
  alike.
- **`lowir_local_static.cpp`, `lowir_lower_object.cpp` — one hand-off.**
  `hand_to_runtime` is 3.6.3p3's and 3.7.2p2's registration written once, and
  `LowirUnitLowering::destruction_entry` is the function it hands over: the
  destructor, or a generated body that ends every element of an array.  The
  generated bodies stand in `pending_functions_` until no definition is left to
  ask for one, because a definition being lowered holds a reference into
  `program_.functions`.
- **`lowir_local_static.cpp` — no address is taken for an initialization that
  names a place per element**, which is 12.6p1's array of class type.

## Performance Evidence

Best of three per shape, alternating between the two binaries:

| shape | this build | `pa21/cppgm++-ref` |
| --- | --- | --- |
| 400 / 1600 / 6400 image-initialized statics in one body | 0.018 / 0.062 / **0.247 s** | 0.584 / 0.743 / **1.403 s** |
| 400 / 1600 / 6400 guarded statics in one body | 0.026 / 0.096 / **0.423 s** | 0.632 / 0.933 / **2.229 s** |
| a 100000-element array of class type declared `static` in a block | **0.005 s** | 0.004 s (constructs none of it) |

Both plan rows carried forward from the checkpoint re-measure on this build at
0.018 s and 0.026 s against the 0.016 s and 0.025 s recorded, and the sweep
above them shows both linear.  The three changes add no scan: the owner part
asks `function_symbol`, which is memoised against the entity, and the array body
is one per array and written once per program.

## Validation

- `make test-report-through-pa20` - **pass**, 2399 / 2399, 20 / 20 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa21'` - **38 / 129**, the
  turn-start baseline, with no fixture that passed at turn start failing.
- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src` - **pass**,
  with the five `bad-division` warnings the stage inherited and no sixth.
- 32 shapes swept through the harness's own comparator against
  `pa21/cppgm++-ref` in a scratch directory under `pa21/tests`, with g++ as the
  third oracle on each: 20 of them identical as canonicalized LowIR, and every
  one of the 12 differences is a recorded item above.
- Two programs built through `lowir2cy86` and `cy86` and run: a body whose two
  guarded statics call a counting function, returning 0 for `127, 127, 2`, and
  an array of class type declared `static` in a block, returning 0 where the
  reference's own program returns 1 and g++ returns 0.
- `valgrind --error-exitcode=99 -q` over 133 inputs - all 129 pa21 fixtures, the
  probe inputs and the two-unit compilation: **clean**.
