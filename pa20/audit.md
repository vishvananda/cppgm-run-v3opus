# PA20 Audit — `cppgm++ --emit-lowir` compile-time metaprogramming

A review of each landed checkpoint, in the order a fact travels: the place a
head declares, the spelling an argument arrives as, the value it converts to,
the specialization it names, and the definition a program wrote for one.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| C1, C2 | `0cda3f77` | 6 / 6 + 1 perf | **the spelling a value argument arrives as, which C1 widened and no reader of one was told.**  Making an argument a *value* let 5.9's `<` and 5.8's `<<` into a name, and all three scans that split a spelling counted every `<` as opening 14.2's list - so `b<(1<2)>::n` found no `::` and `Box<0 < 1, int>` no `,`; 3.4.3p1's rooted name was read by an exit of its own that took no argument list with it; 4.12p1 was missing from the conversion 14.3.2p5 makes the argument a *converted* constant by, so `template<bool>` had one specialization for 3 and another for `true`; and 5.2.3's functional notation and 8.5p16's direct initialization - the other spellings of the cast and the constant object this milestone already folds - folded nowhere |
| C3 | `603dcb83` | 4 / 4 | **the two kinds of settled pack, and the list the object file writes for either.**  A pack is a *run an argument list bound* or *the places one expansion of a function parameter pack declared*, and each of the four readings that meets one knew a different subset: the spelling reading walked the elements of a type that holds none and crashed; one reading of a pattern replaced the pack with an element, so a nested expansion and `sizeof...` inside that pattern found no pack at all; a run of no elements declared no place and so declared nothing, leaving `sizeof...(args)` naming nothing in `f()`; and the ABI, handed the flattened argument list 14.4p1 keys the tier by, wrote `f<int,int>(int,int)` as `_Z1fIiiEiv` - the arguments unpacked and the parameter list *void* - where g++ and the reference both write `_Z1fIJiiEEiDpT_` |

## Current Checkpoint Review

C3 gave the tier 14.5.3's pack in three landings: the place and the run at the
class tier, 14.8.2.1p1's deduction and 8.3.5p10's places at the function tier,
and 14.5.3p4 over a call's argument list.  The type model is right and was
traced end to end: `TypeKind::Pack` is one kind in two states - a run interned
by its elements and an expansion interned by its pattern - so `is_dependent`,
`substitute`, `append_description` and the specialization key each answer one
question about it, two places bound to one run read one entry, and `sizeof...`
is a vector length.  An expansion costs one reading of its pattern per element
and rewrites no syntax, which the measurements below hold to 4096 elements.

What the review found is that *which* of the two states a pack is in was
answered separately by every reader, and the flattened argument list the tier is
keyed by cannot be split back into the run the object file has to write.

### Findings

**1. A pack expansion written as a spelling crashed where the pack it named was
a function parameter pack.**  `PackReading::expand` opened its own element
region and read each element out of `types_.pack_elements(pack.type)`;
`element_region` - the same reading over a tree - answers both kinds.  So a
`...` written inside a template-argument-list over 8.3.5p10's places indexed the
parameter list of a type that holds none:

```cpp
template<unsigned long N> struct b { static const int n = (int)N; };
template<class... Ts> int f(Ts... args) { return b<sizeof(args)...>::n; }
int main() { return f(1) == 4 ? 0 : 1; }   // SIGSEGV; 0 in the reference and g++
```

It is one rule and now has one implementation: `expand` asks `element_region`
for the region of each element, exactly as the tree reading does.

**2. One reading of a pattern hid the pack from the pattern.**  The element is
bound under the pack's own name, which is what makes `f<Ts>` read as this
element - and 14.5.3p4's pattern may name that pack *as a pack* again.  A nested
expansion is written over the whole run and 5.3.3p5's `sizeof...` counts it, so
both found one element and refused:

```cpp
template<class... T> int sum(T... t) { return add(t...); }
template<class... T> int nested(T... t) { return sum(sum(t...) + t...); }
```

An element binding now carries the declaration its run is read off
(`pack_element_of`), which `note_name` and `length` both ask before anything
else; and the first place of a function parameter pack is the pack's own
declaration rather than a lookup an enclosing reading has already shadowed.
`200-nested-call-pack-expansion-same-pack` passes.

**3. A run of no elements declared no place, and so declared nothing.**
8.3.5p10 gives the pack's name to the *first* place its expansion made, and a
run of none has no first place - so `sizeof...(args)` and `args...` in the body
of `f()` said `args` named nothing, which both other compilers accept:

```cpp
template<class... Ts> int f(Ts... args) { return sink(args...) + sizeof...(args); }
int main() { return f() == 5 ? 0 : 1; }
```

The clause now declares the run itself where it declared no place: one entry of
the parameter list that is no place of the function.  The three walks that map
entries onto the function type's places - `parameter_types`,
`declare_parameters` and `record_declared_parameters` - count the two apart, so
a member function's implicit object place is still found where a clause holds
one.

**4. The object file named every pack specialization wrong.**  14.5.3's run is
*one* `<template-arg>` - `J...E` - and a place written `P...` is `Dp` of its
pattern.  The argument list is flattened, because 14.4p1 makes `f<int, char>`
one list however it was written, so nothing downstream could split it back: the
ABI wrote the arguments one by one, and a parameter whose type is an expansion
fell to the encoder's default and came out as `void`.

| shape | before | after, g++ and the reference |
| --- | --- | --- |
| `f<int,int>(int,int)` from `f(Ts... args)` | `_Z1fIiiEiv` | `_Z1fIJiiEEiDpT_` |
| `f<int,char>()` from `f()` | `_Z1fIicEiv` | `_Z1fIJicEEiv` |
| `box<int,char>::m()` | `_ZN3boxIicE1mEv` | `_ZN3boxIJicEE1mEv` |
| `s<1,2>::m()` over `int... Ns` | `_ZN1sILi1ELi2EE1mEv` | `_ZN1sIJLi1ELi2EEE1mEv` |
| `box<>::m()` | `_ZN3boxIE1mEv` | `_ZN3boxIJEE1mEv` |

The place the run begins at is the one fact that splits the list, so it is
recorded beside the arguments (`set_template_arguments`) for a class and read
off the head's declarations for a function template, and the three sites that
write an argument list ask one helper.  `object=` is stripped before the LowIR
comparison, so no fixture in either suite could have seen any of this.

### What the review confirmed rather than found

The typed ownership holds.  `TypeKind::Pack` was swept at every reader a new
kind has - `substitute`, `dependent_walk`, `append_description`, `key_of`,
`type_spelling`, `bind_argument`, `argument_of` and `abi_type` - and each
answers it.  The two states are told apart by one question (`is_pack_expansion`
against the new `is_settled_run`) rather than by each caller's own test, which
is what finding 3's first attempt got wrong: an unsettled `Ts...` place *is* a
`Pack` and is one place.  `packs_in` walks a type DAG, whose shared shapes have
2^depth paths, and now asks each type once; the shape that would show it cannot
be built here for an unrelated reason - one `typedef p<t22,t22> t23;` already
costs 1 s of PA19's recorded exponential spelling - so the memo is a bound
rather than a measured fix.

The complexity is what the plan claims.  A pack of 4096 elements bound, expanded
into a base and counted, and a call forwarding 1024 places, both compile inside
this machine's 0.11 s process floor, where `reference-binaries/cppgm++` takes
0.41 s and 0.31 s.  Valgrind is clean over ten pack shapes with no finding of
any kind, and a two-unit `--emit-lowir` run over a class and a function template
with packs is identical to the reference.

The differential sweep is 21 pack shapes through this compiler, through
`reference-binaries/cppgm++` and through g++: both kinds of pack at 0, 1, 2, 3,
64, 512 and 4096 elements; a run bound to a base-specifier, a call's argument
list, an explicit argument list and a member defined outside its template; a
nested expansion over the same pack and over two; `sizeof...` in each position;
and every mangled name above.  All are identical as canonicalized LowIR, and
identical to g++'s symbol names.

### Recorded, not landed

- **The reference writes an empty pack's *function template* name as if it were
  no template at all** - `f<>()` is `_Z1fv` there and `_Z1fIJEEiDpT_` here and
  in g++.  Its own class tier writes `_ZN3boxIJEE1mEv`, so the two disagree
  inside the reference; the standard's reading and g++ decide it.
- **`sizeof...` inside an argument *spelling*** - `value<I + sizeof...(I)>()...`
  - is still refused: the spelling reader `sema_value_expression.cpp` has no
  `sizeof...` terminal, which is a reader of its own and not this rule.
  Finding 2 was the other half of that test and is landed.
- **A generated place name that collides with a written one** - a parameter
  actually named `args__pack2` beside a pack named `args` - is renamed
  `args__pack2__shadow2` here and `args__pack2__pack2` in the reference.  Local
  names are compared literally, so a fixture writing one would part ways; none
  in either suite does.
- **A pack name written without `...` is not diagnosed** where 14.5.3p4 makes
  the program ill-formed, and the reference refuses it.  No valid program is
  answered differently.
- **10p1 over a base pack of more than one element** is refused rather than laid
  out, which is this milestone's one-direct-base limit and not the pack's.
- **A static data member's definition is written on a different demand here than
  in the reference**, in both directions, and neither is 14.7.1p1's odr-use.  It
  is PA19's member-demand model and is what
  `100-nontype-template-argument-static-member-no-storage` fails on.
- **PA19's recorded items are unchanged**: the exponential spelling of a
  specialization whose arguments double, the out-of-class member path's
  residual, 12.1's two constructor entry points, and the ABI's decltype return
  type.  A metafunction with no terminating specialization still overflows the
  machine stack rather than being diagnosed.

## Changes

- **`sema_pack.cpp` — one element region for both readings.**  `expand` asks
  `element_region`, which answers a bound run and 8.3.5p10's places alike; the
  first place of a function parameter pack is taken rather than looked up; and
  `packs_in` asks each type of a pattern once.
- **`sema_scope.h`, `sema_pack.cpp`, `sema_function.cpp` — an element carries
  its pack.**  `SemaEntity::pack_element_of` is what `note_name` and `length`
  read, so a nested expansion and `sizeof...` inside a pattern are written over
  the run and not over the element standing for it.
- **`sema_declarator.cpp`, `sema_function.cpp`, `sema_analyzer.cpp` — a run of
  no elements is declared.**  `bind_pack` declares it where a place would have
  been bound, and the walks that map clause entries onto the function type's
  places count the two apart.
- **`type_model.h` — `is_settled_run`**, so the two states of a pack are one
  question with one answer.
- **`lowir_abi.cpp`, `type_model.*`, `sema_template.cpp` — 14.5.3 in the object
  file.**  `argument_refs` writes the run as one `J...E` argument, `abi_type`
  writes an expansion as `Dp` of its pattern, and the place the run begins at is
  recorded on the specialization and read off a function template's head.
- **Three fixtures** under `cppgm.tests/course/pa20`, one per finding a fixture
  can pin, each refused or crashed by the `603dcb83` build, each with a `.ref`
  generated from `reference-binaries/cppgm++`, and each returning 0 under g++.

## Performance Evidence

Best of five, `-O0`.  This machine has a 0.11 s process floor - an empty
translation unit measures 0.11 s through both binaries - so a row at the floor
is a shape that costs nothing measurable.

| shape | here | `reference-binaries/cppgm++` |
| --- | --- | --- |
| a pack of 0 / 1 / 64 / 512 elements: bound, expanded into a base, counted | 0.11 s | - |
| the same at 1024 / 2048 / 4096 elements | 0.11 s | 0.41 s at 4096 |
| a call forwarding a parameter pack of 0 / 2 / 128 / 384 places | 0.11 s | - |
| the same at 1024 places | 0.11 s | 0.31 s |
| 2080 expansions over 64 nested `pack_of<id<T>::type...>` | 0.11 s | - |
| 512 / 4096 distinct value arguments over two templates | 0.11 / 0.62 s | - |
| `fac<200>` / `fac<800>` metafunction chain | 0.11 s | - |
| a 2000-deep chain instantiated but not evaluated | 0.11 s | **SIGSEGV** |
| 256- / 1024-deep `s< s< ... <int> > >` spelling | 0.11 s | **> 60 s** at 256 |
| one template-id of 1024 arguments | 0.11 s | - |

Nothing in the model moved against the `603dcb83` build, and the two shapes
finding 1 and finding 3 are about went from a crash and a refusal to the floor.

## Validation

- `make test-report-through-pa19`: **2169 / 2169**, 19 / 19 stages.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa20'`: **127 / 172**, from a
  turn-start **123 / 169** - one checked-in fixture newly passes, the three
  added here pass, and no fixture that passed at turn start fails.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: passes with
  the five inherited `bad-division` warnings.  The build prints nothing.
- **Valgrind clean** over ten pack shapes and the three added fixtures.
- The five fixtures the C1/C2 audit added were regenerated from
  `reference-binaries/cppgm++` and are unchanged.
