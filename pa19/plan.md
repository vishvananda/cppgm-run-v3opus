# PA19 Plan — `cppgm++ --emit-lowir` first-tier templates

PA19 **passes**: **391 / 391** (65 spec + 228 general + 98 course), with
pa1-pa18 at **1778 / 1778** and the file audit passing with the five
header-weight warnings it inherited.  The build prints nothing.

The milestone gives the PA16-PA18 object model its first template tier: a
template-declaration records a pattern instead of declaring anything, and
14.7.1p1's instantiation is that same pattern read once more against a region
that binds each parameter to its argument.  Nothing is substituted into syntax
and no text is replayed, so the ordinary PA11-PA18 machinery settles a
specialization exactly as it settles a class the program wrote out.

Three facts about the harness shape what has to be right, read out of
`scripts/compare_results_common.pl`:

- **Function symbols are paired, global symbols are not.**  `@name` is rewritten
  to `<fnN>` only for names a `function`/`declare function` line defines, and
  the pairing runs by `object=`, then by identical name+signature, then by
  masked body shape.
- **Top-level entries are sorted**, so emission order never matters;
  instruction order, global item order and vtable slot order do.
- **`object=`, `binding=`, `linkage=` and every `alias object` line are
  stripped before the comparison.**  The object file's own name for a
  specialization, and whether two units may both define it, are therefore
  requirements no fixture can fail on - they are checked by regenerating every
  `.ref` from `reference-binaries/cppgm++` and by sweeping the emitted symbols
  against it and against g++.

## Stage Design

**The tier is one file.**  `sema_template.cpp` owns the three steps 14 splits
the work into: a template-argument list is read where a name is turned back
into what was written, the bindings are a region, and the specialization is one
declaration however many times it is named.

- **A template is a pattern, not a declaration.**  `TemplateInfo`
  (`sema_template.h`) is the syntax the template-declaration parameterises, the
  region it was written in, the parameters its head declared with 14.1p9's
  defaults beside them, 14.5.1.3p1's out-of-class member definitions with the
  head each of them wrote, and the specializations an instantiation asked for.
  It hangs off the declaration (`SemaEntity::templated`), because the
  declaration is what a use finds.  `record_template` writes it under
  `SemaDialect::Lowering` only: PA11 and PA12 describe what a
  template-declaration *says* and instantiate nothing.
- **Instantiation is a second reading of the pattern.**  `instantiate_class`
  makes the declaration and `complete_specialization` reads the body against a
  `ScopeKind::TemplateParameters` region whose bindings are typedef-names of the
  argument types.  Declaration and completion are apart because their points
  are: 14.7.1p1 lets a specialization be named before its template is defined,
  and a dependent argument list (`TypeTable::is_dependent`) makes a declaration
  and no body at all.  A specialization is bound to no name - it is reached from
  the template-id that wrote its arguments - so ordinary lookup keeps finding
  the template, and 14.6.1p1's injected-class-name is the specialization.
- **14.6.1p1's current instantiation is one of those specializations**: the
  class the template's own definition declares, over a region binding each
  parameter to a type standing for itself.  It is the one specialization over a
  dependent argument list that *is* completed, and `TemplateInfo` keeps it
  beside its region because 14.5.1.3p1's out-of-class member definitions are
  read against the same two however long after the class.
- **Substitution belongs to the walk, not to the type table.**  Every category a
  type is only made of types is rebuilt by `TypeTable::substitute`; a
  specialization is the one that is not, because `A<T>` with `T` bound to `int`
  is a class only an instantiation can make.  `SemaAnalyzer::substituted` walks
  the type and delegates the rest.
- **Function templates take the same three steps.**  The pattern is recorded on
  the declaration the ordinary path makes; `specialize`/`deduce_specialization`
  make the declaration; `instantiate` reads the body against the bindings.
  14.5.6.1p5's two declarations of one template write types that differ, so each
  declaration's *signature* - its type with every parameter standing for the
  place its head declared it in - is computed once and the chain is walked by
  comparing types.
- **Deduction is over the P/A pairs the *call* wrote, and nothing else.**
  14.8.2.5p3 leaves a parameter written over no template parameter deducing
  nothing, so whether the argument reaches it is 13.3's question about a
  conversion; 8.3.6p1's unwritten trailing arguments deduce nothing either;
  13.3.1.2p4's first operand is a non-member operator candidate's own first
  argument; and 14.8.2.1p6's overload set is tried one declaration at a time,
  with two that both deduce leaving a non-deduced context rather than a failure.
  14.8.2.2's target type is the one pair a whole function type makes.
  14.8.2.1p3 makes a reference of the *argument*: an rvalue reference over an
  unqualified parameter takes an lvalue's type as an lvalue reference, and
  8.3.2p6 collapses the two.  14.8.1p2's partly written argument list is a
  declaration of its own (`SemaEntity::partial_of`), made once per template and
  written list, that a call, 13.4p1's target type and 14.7.2p1's explicit
  instantiation each deduce the rest from; 14.1p9's default fills a place the
  deduction leaves empty, read in a region its own head spelled.
- **14.5.6.2's ordering is one question with two readers.**  13.3.3p1's tie
  between two specializations and 13.4p1's target type both ask which template
  is more specialized, so `more_specialized` answers it once, memoised per pair;
  p9 orders by what p5 and p7 stripped, which is the only thing left to tell
  `f(T &)` from `f(const T &)` by.  13.3.3.2p3 carries two different clauses in
  one field, which is what leaves the pair to the ordering in the first place.
- **A definition is read where it stands as well as where it is used.**  14.6p8
  makes a template definition ill-formed where no valid specialization could be
  generated from it, so the body is read once at its own point - in the *PA11*
  dialect, because a type that depends on a parameter has no layout, no
  conversion and no overload set until an argument arrives - and again for each
  specialization.  `check_template_definition` reads a function template's body,
  `read_class_pattern` a class template's own definition as the current
  instantiation, `read_member_pattern` 14.5.1.3p1's out-of-class member
  definition against that same class.  What such a reading cannot answer is
  14.6.2p1's: a name written after a dependent prefix stands for a type of its
  own (`dependent_member_name`, one per prefix *and component*, because the ABI
  writes them apart), 14.6.2p3 leaves a dependent base off 3.4.1's chain - as a
  fact of the base-specifier (`dependent_bases_`) and for the specialization as
  well as for the pattern - and what a dependent type is *worth* the reading
  stands one value in place of.
- **An instantiation reads the declarations and leaves the definitions.**
  14.7.1p1 instantiates the *declarations* of a specialization's members and not
  their definitions, so a member body the reading arrives at is put aside on
  `held_definitions_`, keyed by the declaration it defines, and the use that
  names the member is what asks for it.  That is true of a body written in the
  class body and of one written *outside* it alike: `instantiate_member` reads
  the declarator and hands the body to `queue_definition`, with 14.1p2's own
  head recorded beside it (`PendingDefinition::stands_in`/`head`) so `EnclosedBy`
  can stand it over the class again wherever the body is finally read.
  `require_definition` is the ask, at the places a function becomes a use -
  `function_value`, where a callee, an `&` and a target type's chosen
  declaration all pass; the demand a constructor gets where its object is; the
  destruction entry every end of a lifetime notes - and it marks the function
  `definition_required` whether or not a body is waiting, because 14.6.4.1p1
  gives a specialization a second point of instantiation at the end of the unit
  and a definition written *below* the use that asked is one this unit still
  owes.  Two demands have no expression behind them: 10.3p10's table, asked over
  every class this instantiation made (`require_table_definitions`), and
  14.7.2's explicit instantiation, which asks for the held body as a call does.
- **One body's facts belong to that body.**  `FunctionReading` puts aside what
  the reading around it knew, because naming a specialization in the middle of a
  body is what asks for another body to be read; `DialectReading` does the same
  for which of the three dialects the walk is in.  9.2p2's complete-class
  context is the same idea over a class: a member body written in a class body
  is held on `held_bodies_` until the class-specifier closes, and the list is
  *drained* back to a mark rather than walked once, because reading a held body
  can hold another.
- **A head stands where the declaration it parameterises belongs.**  3.4.1p8
  reads the rest of a qualified declarator-id in the region that name reaches
  and 14.1p1 encloses the declaration in its head's own region, so the head is
  opened *inside* that region while the declarator and the body are read
  (`StandingIn`) and `declaring_region` still steps out for the declaration.
  14.1p2 lets each head spell the places as it likes, so `open_member_parameters`
  gives an out-of-class member definition a region of its own, standing between
  the class and the one it was completed against (`EnclosedBy`), and nothing it
  binds is standing when the next definition is read.  14.6.1p6's redeclared
  template parameter is a fact of those chained regions, asked at every
  declaration that binds a name.
- **An object-file name is walked, never split.**  `lowir_abi.cpp` builds the
  components of every encoded name from the *declaration's own regions*, and a
  specialization is named by two facts - the template's own qualified name and
  the argument `TypeId`s - because the ABI writes them apart.  `LocalContexts`
  hands the encoder one identifier per argument type, and the encoder keeps the
  substitution key each identifier settled on (`known_keys`), so a name that
  writes one argument twice writes `S_` without encoding it again.  A
  specialization's *spelling* is a name too: `canonicalize_lowir_for_compare`
  masks a function's LowIR symbol and leaves a global's, so a specialization
  writes its argument list the way a program does, comma and space alike.
  `LowirUnitLowering::shared_definition` answers one question for the three that
  follow from it: what the object file binds the symbol as, which of 12.1's
  entry points the definition owes, and whether 3.2p3 waits for a use.
- **A type-id's spelling is read as a declarator, not as a word list.**  14.2
  leaves a template argument as text, which is the one reason a type-id arrives
  as a spelling at all; `sema_type_id.cpp` answers what 8.1p1 says and knows
  nothing about templates.  `split_type_id` keeps a name whole and
  `type_id_words`/`abstract_declarator_words`/`suffix_words` read 8.1p1's
  type-specifier-seq and 8.3p1's abstract-declarator out of what is left.
- **A specialization is declared where it is named and defined where a complete
  type is required, and naming it is never that.**  `asked_specialization` only
  *marks*, however many times a name is written; `require_complete_type` is the
  one demand, read at 3.9p5's own list - qualified lookup, member access, a
  base, `sizeof`, `alignof`, `new`, the object being built, the declarator that
  *defines* an object, 8.3.5p6's return type and parameter objects of a
  *definition*, and 3.9p5 over an expression at the one place every expression
  the layer reads leaves.  14.6p8's reading asks for none of them.

The rules the tier inherits and had to widen, each recorded where the code
asks it: 8.3.5p5's two adjustments; 3.3.7p1's function prototype scope as a
region that dies with its declarator, with 5.1.1p3's `this` over the same span;
7.1.6.2p4's two entity arms and 14.6.2.2p1's type-dependent decltype-specifier,
keyed by 14.4p1's shape of the regions standing over it; 8.3p1's constructor
read from the level the declarator-id ends up at; 9.4.2p3's in-class
brace-or-equal-initializer as the value the definition's storage holds;
8.3.5p10's parameter name as a fact of the function; 12.8p12 settled again by an
out-of-class `= default` against a complete class (`settle_class_answers`);
12.2p1's temporary a reference binds given storage and a name; 6.4's condition
that is a literal lowered as the jump one of its edges is (`folded_edge`);
14.7.1p6's initialization left to the use that names the member; 13.3.1.2p1's
enumeration operand reaching 13.6's built-in candidates; 5.3.3p1's operand read
as the expression the grammar allows; 14.2 and 14.6.1p1 leaving a plain
template-name a type-specifier only where 9p2's injected-class-name stands; and
14.1p10's defaults merged from every declaration.

## Performance Model

The dominant operation is one reading of one pattern per specialization, which
is linear in the pattern, plus 14.6p8's one reading per template *definition*.
What is superlinear is superlinear in the *program*.

Measured on the shapes the tier makes scaling-sensitive, each timed twice,
`cppgm++ --emit-lowir -O0`:

| shape | 32 | 64 | 128 | 256 | 512 |
| --- | --- | --- | --- | --- | --- |
| n distinct class templates, each with a body, none instantiated | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.02 s |
| n distinct specializations of one class template, each with a member function | 0.00 s | 0.00 s | 0.01 s | 0.03 s | 0.05 s |
| n qualified dependent names in one class template's body | 0.00 s | 0.00 s | 0.00 s | 0.00 s | 0.00 s |
| n out-of-class member definitions of one class template | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.01 s |
| n member function bodies in one class template | 0.00 s | 0.00 s | 0.00 s | 0.00 s | 0.00 s |
| one specialization named n times | 0.00 s | 0.00 s | 0.01 s | 0.01 s | 0.03 s |
| n specializations of one class template over n classes, through a call | 0.00 s | 0.01 s | 0.02 s | 0.04 s | 0.08 s |
| n function templates, each an 8-statement body, none called | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.04 s |
| n nested blocks in a function template, each declaring a name | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.03 s |
| n declarations of one template name, none of them called | 0.00 s | 0.00 s | 0.00 s | 0.01 s | 0.01 s |
| n class templates, each deriving from the previous one's current instantiation | 0.00 s | 0.00 s | 0.01 s | 0.02 s | 0.04 s |
| **n out-of-class member definitions of a template with n specializations** | 0.01 s | 0.06 s | 0.23 s | 1.01 s | 3.91 s |
| **n function templates overloading one name, each called once** | 0.00 s | 0.01 s | 0.03 s | 0.09 s | 0.39 s |
| **n target types each choosing among n function templates** | 0.01 s | 0.01 s | 0.03 s | 0.06 s | 0.16 s |

The first eleven are linear: 14.6p8's reading is one walk of each definition's
syntax, one ordinary lookup per name it writes, and one hash probe per
declaration it makes, so it costs the *source* and not the specializations the
unit goes on to make.

The last three are quadratic, and the count each is quadratic in is the
*program's* own.  13.3p1 gathers every declaration of a name, so n calls over an
n-declaration chain are n^2 candidates however they are ranked, and n classes
each deriving from the one before it are n^2 base links however a name is looked
up through them.  n out-of-class member definitions over n specializations is
n^2 member *declarations*, which 14.7.1p1 requires: the same
class with n members declared in its body and no definition at all is 0.07 s at
n = 128 against this shape's 0.23 s, so what the out-of-class path costs above
the declarations themselves is about 3x, and it is the declarator each
definition is read through once per specialization.  The **bodies** cost
nothing at all now that they wait for a use - the shape is 0.24 s with empty
bodies and 0.23 s with eight statements in each, where it was 0.51 s and 1.15 s
before the final audit.  `reference-binaries/cppgm++` is 1.70 s and 10.92 s at
n = 128 and 256 against our 0.23 s and 1.01 s, 1.01 s against our 0.05 s on n
distinct specializations, 3.21 s against our 0.04 s on the derivation chain, and
**does not finish inside 300 s** where we take 0.39 s on n overloaded function
templates.

**Depth is quadratic in the spelling and nothing else.**  A specialization is
named by its written-out spelling, so a chain of n typedefs each naming the one
before it writes O(n^2) characters: 0.15 s at n = 1024 and 0.63 s at n = 2048,
against the reference's 22.26 s at n = 2048.  n-deep `S<S<...<int> > >` written
out as one name is the same shape, 0.79 s at n = 2048.

**One shape is exponential and it is that same spelling.**  `typedef P<t,t>`
repeated n times names a class whose written-out spelling *doubles* at every
level: 0.00 s, 0.04 s, 0.16 s and 0.63 s at n = 12, 16, 18 and 20 in 24 lines of
source, with peak RSS 376 MB at n = 20.  The final audit took the mangler's own
2^n walk off it - **2.83 s -> 0.63 s at n = 20**, and 0.87 s on the variant that
calls a member and so puts the deep name in the object file - by keeping the
substitution key each template-argument binder settled on.  What is left is the
O(2^n) *characters* the names themselves are, spread over the entity's name, the
ABI qualified name, the dump description and the class region's prefix, which is
why the memory does not move.  `reference-binaries/cppgm++` is 2.00 s and 2.30 s
on those two inputs, so this compiler is now 3x faster than it where it was 1.4x
slower; g++ is 0.10 s, because it never materialises the spelling.  Fixing the
rest means naming a specialization by `(template, argument TypeId list)` and
computing the spelling on demand, which is a change to `TypeTable`, `SemaModel`,
the dump and `lowir_abi.cpp` together, and is recorded rather than half-landed.

Valgrind is clean over all 391 fixtures.  A 20000-deep parenthesized expression
is refused by the parser at about 1000, so the definition-time walk's recursion
is bounded by the same limit the expression layer already is; a declarator's own
parenthesis nest is refused between 8000 and 16000.

The files that carry the tier sit under their limits with room:
`dev/src/sema_analyzer.h` at 2397 against 2400, `dev/src/sema_template.cpp` at
2957 against 3000, `dev/src/sema_lifetime.cpp` at 2998 against 3000.  Both
`sema_template.cpp` and `sema_lifetime.cpp` are near their limits, so the next
thing either grows by owes a split of its own.

## Architecture Review

Fourteen checkpoints landed the tier and each was reviewed after it landed.  One
shape accounts for most of what those reviews found, and it is the shape an
increment has: **a rule is landed at the one call site its fixture reached, and
the sibling readers of the same fact are left asking the old question.**
14.5.6.2's ordering reached 13.3.3p1's tie and not 13.4p1's target; the
deferral of an instantiated body reached the three places a *use* stands and
neither demand that has no expression behind it; the object parameter a member
template carries had four readers and got one; 6.4's fold asked the operand a
lowering wrote where the rule is the expression's; 14.7.1p1's demand was asked
at the naming rather than at 3.9p5's requirement, wrong in both directions at
once; C13's "which definitions the unit holds" went in as a clause of a
constructor-only path.  The reviews' standing answer is to sweep every reader of
a fact the moment the fact is widened, and to write the sweep down.

The second recurring shape is **the object file's own name, which the suite
cannot see**: `canonicalize_lowir_for_compare` strips `object=`, `binding=`,
`linkage=` and every `alias object`, so eleven fixtures once emitted symbols
containing `<`, `,` and spaces and nine of them passed.  Every checkpoint since
has regenerated every `.ref` and swept the emitted symbols against the reference
and against g++, which is the only way those requirements are checked at all.

## Final Architecture Review

An independent end-to-end review of the tier as it stands, reading the README,
the stage commits and the source rather than the checkpoint conclusions.

**The typed fact flow is sound and there is no text replay.**  A template
argument enters as text because 14.2 makes PA10 spell a template-id that way,
is read once by `sema_type_id.cpp` into a `TypeId`, and is a `TypeId` from there
to the object file: `TypeTable::type_list` interns the argument list,
`model_.specialization_of` keys the specialization by it, `TypeTable::substitute`
and `SemaAnalyzer::substituted` rebuild types from it, and `lowir_abi.cpp` hands
the encoder the argument `TypeId`s rather than a spelling.  Nothing between
those two points re-parses source.  The one place a *string* is still the
identity is the specialization's written-out name, which the LowIR comparison
requires for a global's symbol and which is what the exponential above is.

**Three readings of one body, and they are three dialects rather than three
implementations.**  `check_template_definition` (14.6p8, PA11 dialect),
`read_definition_body` (a definition read where it stands) and `write_definition`
(one the end of the unit writes) each read the same `AstNode` through
`semantic_statement`; what differs is the region, the dialect and the node the
lines are written under.  The final audit consolidated the first pair's
duplication: `function_definition` now builds one `PendingDefinition` and either
queues it or reads it, so the fields a body needs are described in one place.

**The demand graph is complete.**  Every path by which this unit can owe an
instantiated definition was traced: a callee, an `&`, a target type's chosen
declaration, a constructor where its object is built, a destruction entry,
10.3p10's table over every class the instantiation made, 14.7.2's explicit
instantiation of a class and of one member, and 14.6.4.1p1's second point at the
end of the unit.  The audit closed the last of these - a definition written
below the use that asked for it - by making the ask a fact of the function
(`definition_required`) rather than a probe of a table that was empty when it
was made.

**What the review found and fixed is in `audit.md` under Findings.**  Two are
architecture: 14.7.1p1's deferral had never reached the out-of-class member
definition, which was both a program this compiler refused and both oracles
compile and the tier's own n^2; and the mangler discovered a repeated template
argument by encoding it again, which is 2^n for a name n specializations deep.
One is convention: 26 fixtures written by earlier checkpoints were filed in the
handout's own suite.

**What is left is recorded and not landed**, in `audit.md` under Findings /
Recorded, not landed: the
exponential spelling above; 14.5.2's second template-parameter clause and a
template-id after a member access, which the parser refuses outright; the ABI's
`DT` encoding of a decltype return type; the base-entry rule the reference reads
that no statable rule reproduces; and the shapes where this compiler and g++
agree against the reference, which cannot become fixtures because the reference
is what writes a `.ref`.

## Completed Checkpoints

| # | checkpoint | result |
| --- | --- | --- |
| C1 | the class tier: `TemplateInfo` as the pattern, the template's name bound where a use looks for it, 14.7.1p1's instantiation as a second reading against a bindings region, 14.1p9's defaults, 14.6.1p1's injected-class-name, the ABI's `<template-args>`, and 8.1p1's type-id read as a declarator | 27 -> **114 / 293** |
| C2 | the function tier: the pattern recorded on the declaration the ordinary path makes and read again for the specialization that named it; the ABI's result type and `T_` signature; 14.5.1.3p1's out-of-class member definitions; a static data member of a specialization | 114 -> **172 / 293** |
| C2 completion | the two points a specialization has, and 14.6.2p1's dependent argument list making a declaration and no body; `SemaAnalyzer::substituted`; 14.8.2.1p2 and 14.8.2.5p4 | 172 -> **194 / 293** |
| tier audit | the object file's name for a specialization: every component walked out of the declaration's own regions, the ABI's `<template-param>` made a substitution candidate, and 14.7.1p1's shared definition answered once for its three readers | 194 -> **200 / 295**; `object=` differences 54 -> 9 tests |
| C3 | the call a template joins: 14.8.2.5p3, 14.8.2.1p4, 8.3.6p1, 13.3.1.2p4, 14.8.2.2's target type, 14.8.2.1p6's overload set, 3.4.2p2, 13.5p6 asked of the specialization, 14.5.6.2's ordering, 14.5.6.1p5's equivalence, 8.5.3p5's temporary | 200 -> **223 / 295** |
| C3 audit | 14.5.6.2p4 as one question with two readers, p9/p10 beneath p5/p7, 14.8.2.1p3 collapsed by 8.3.2p6, 14.6.4.1p1's point at the end of the unit, and the signature each declaration has on its own | 223 -> **227 / 299**; 512 overloads 0.36 s -> 0.04 s |
| C4 | 14.6p8's body read once at its own point and again per specialization; 14.7.1p1's naming made a declaration and not a use; 14.6.1p6; 9.2p1 against 7.1.3p3; `FunctionReading` and `DialectReading` | 227 -> **235 / 301** |
| C4 audit | the reading's own edges: the list of specializations an *instantiation* asked for, 14.6p8 over an initializer, 3.4.2p2's callee with no associated region, 14.6.1p6 wherever a declaration binds a name, 3.3.10p2's type-name | 235 -> **242 / 308** |
| C5 | 14.6.1p1's current instantiation read once against a kept region; 14.5.1.3p1 read against the same two; 14.6.2p1's dependent qualified name; 14.6.2p3's dependent base; 9.2p2's held bodies; 9.4.2p1's qualified class-head; 9.3.1p3 for a pattern; 15.4p1; 7.3.3p1 | 242 -> **254 / 308** |
| C5 audit | 14.1p2's out-of-class head read against a region of its own; 9.2p2's held bodies drained back to a mark; 14.6.2p1's name kept as prefix and component; 14.7.1p1's spelling written the way a program writes an argument list | 254 -> **256 / 310** |
| C6 | 9.4.2p3's in-class initializer as the value the storage holds; 8.3.5p10's parameter name as a fact of the function; 8.3.5p5's two adjustments; 8.5p8 read of the whole object | 256 -> **261 / 310** |
| C6 audit | 8.3.5p10's "any declaration" narrowed by 14.7.1p1, so a declaration of a *template* below the pattern renames no object a specialization already made | 262 -> **264 / 313** |
| C6 audit review | 14.7.1p1's spelling frozen at the template's *first* declaration; 8.3.5p10 asked where 12.8p28's and 12.9p8's definitions make their objects | 266 -> **272 / 321** |
| C7 | 8.3p1's constructor read from the level the id stands in; 14.2p3 asked of the overload set; 7.3.4p2's using-directive answered last; 14.7.2's explicit instantiation with `object_root=yes` | 272 -> **283 / 326** |
| C7 audit | 8.3p1's binding clause read at the declarator-id's own level; 14.7.2p1's simple-declaration target resolved; the definition form given a node of its own | 283 -> **288 / 331**; pa1-pa18 1777 -> **1778** |
| C8 | 14.6.2p3's dependent base left off the chain for the specialization too, as a fact of the base-specifier; 14.7.1p1's `held_definitions_` with the three places a use stands | 288 -> **294 / 331**; 512 specializations of a 16-member template 0.28 s -> **0.15 s** |
| C8 audit | 10.3p10's table asked of every class the instantiation made; 14.7.2p1's explicit instantiation asking for the held body; that clause's target read from every class region its prefix reaches | 294 -> **297 / 334** |
| C9 | 3.3.7p1's function prototype scope; 3.3.2p6; 5.1.1p3's `this` over a trailing-return-type; 14.6.2.2p1's dependent decltype-specifier answered by reading the expression again; 14.2's template-id carrying a cv-qualifier; the specialization taken before the definition's own specifiers | 297 -> **304 / 334** |
| C9 audit | the decltype-specifier's type made a fact of the expression under 14.4p1's key; 3.3.7p1 bounding the rebuilt region; 5.1.1p3 given only to member declarators; 7.1.6.2p4's member-access arm; `sema_type_id.cpp` split out | 304 -> **310 / 340** |
| C10 | 9.4.2p1 and 3.4.1p8 recording the pattern on the declaration the reached region has, with 14.1p1's head opened inside it; 3.4.3p1's prefix walked component by component; 14.5.2's member template given 9.3.1p3's object parameter; 14.8.1p2's partial list; 8.2p7's `T (X)` | 310 -> **322 / 346** |
| C10 audit | 9.4.1p2's static member template matched by signature; what a specialization is called on; 14.5.6.2p2's ordering places; 14.1p9's default filling an omitted trailing argument; 3.4.3p1's leading `::`; 14.8.1p2 at an explicit instantiation | 322 -> **327 / 351** |
| C11 | 14p1's template-declaration declaring no object; 14.7.1p6's initialization left to the use; 12.2p1's temporary given storage and a name; 6.4's literal condition and 5.14's folded operand; 12.1p5's subobject that holds nothing; 12.8p31's returned object; 4.10p1's null pointer value | 327 -> **343 / 358** |
| C11 audit | 6.4p4 asked of the node rather than of the operand a lowering wrote; 12.2p1 and 4.10p1 asked of the **image**; 3.6.3p1's registered end owing 3.6.2p2's entry; 14p1's pattern told from 14.5.1.3p1's static member by the region | 343 -> **348 / 363**; the value-path fold 0.22 s -> **0.17 s** |
| C12 | 14.6.2.1p9's nested class of the current instantiation made dependent; 3.4.1p8 and 3.3.2p5 over a member class's base-clause; 7.1.6.3p1's elaborated-type-specifier in a template argument; 3.4.3.1p2's second arm; 14.7.1p1 asked only where a completely-defined type is required; 5.3.6's two spellings | 348 -> **360 / 369** |
| C12 audit | 14.7.1p1's point read at the demand instead of at the naming: `asked_specialization` only marks and `require_complete_type` is the one demand, read at 3.9p5's own list | 360 -> **365 / 374** |
| C13 | 8.4.2p2's `= default` outside the class settling 12.8p12 again; 8.3.2p1's reference member ending 12.8p15's run; 4.10p3's base conversion made unobservable; 3.2p4 and 14.7.1p1 over which definitions the unit writes; both of the ABI's entry points where the use was the program's | 365 -> **374 / 378** |
| C13 audit | 9.3p2's member function this unit's source defined outside its class never deferred; 9.2p2's complete-class answers made one `settle_class_answers`; 12.8p12's base subobject carried as bytes naming no entry point | 374 -> **377 / 381** |
| C14 | 13.3.1.2p1's enumeration operand reaching 13.6's built-ins; 5.3.3p1's operand read as an expression wherever 5.19 asks for a constant; 14.2 and 14.6.1p1 leaving a plain template-name a type only at the injected-class-name; 14.1p10's merged defaults | 377 -> **388 / 388, the PA passing**; the base chain 0.59 s -> **0.30 s** |
| final audit | 14.7.1p1's deferral given to the out-of-class member definition, with 14.6.4.1p1's second point behind it; the mangler's repeated template argument named by the key its first writing settled; the 26 fixtures earlier checkpoints filed in the handout's suite moved to `cppgm.tests/course/pa19` | **391 / 391**; pa1-pa18 1778 / 1778; n out-of-class definitions over n specializations at n = 128 0.54 s -> **0.23 s** and at n = 512 9.43 s -> **3.91 s**, the doubling spelling 2.83 s -> **0.63 s**; valgrind clean over all 391 |
