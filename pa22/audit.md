# PA22 Audit — `cppgm++ --emit-lowir`, the template entity and specialization graph

A review of each landed checkpoint, in the order a template argument travels:
what a head's places are, what may stand at one, what a naming over an unsettled
place is, and which declaration a list selects.

## Checkpoint Audit Ledger

| # | reviewed at | blockers | what the review found |
| --- | --- | --- | --- |
| T | `598d0d4a` | 7 / 7 + 7 recorded | **the template a place accepts, asked at one of the two tiers that declare a place - and the pair 14.8.2.5p4 reads, written for one of the three things an argument list can be applied to.**  Group T made 14.1p2's template place an owner: a head per written clause, `TypeKind::TemplateName` for the template an argument named, `place_heads_` for the head a place stands for, and 14.3.3p1 asked of the two heads.  The rules are right and each was written at one exit.  A *function* template's places are declarations rather than entries of a `TemplateInfo`, and nothing there ever wrote `place_heads_`, so a template-name at a function-tier place was unchecked in both directions; 14.6.1p1's injected-class-name arm swallowed a template-*id* written at a place; 14.3.3p1's pack matched no run of none and compared only each place's kind; and `match_template_id` read `L<A…>` against a class alone, so 14.5.5.2p1 ordered no two patterns over a template place and 14.8.2.1's two allowances reached neither |
| A | `202f04ec` | 4 / 4 + 6 recorded | **the three things a template-id can be looked up in, with 14.2's own exit written at two of them - and the access 11p1 gives a member template, carried by one of the three tiers that make a declaration from an argument list.**  Checkpoint A landed 14.5.7p1's alias template and moved 14.2p4's keyword into `QualifiedName::part`.  The alias is right and swept clean: 7.1.3p2's transparency, 14.5.7p2's identity, the ABI name, the region a member alias's access travels from, the memo that keeps a nest naming the one below twice flat to depth 24 - 84 probes pass the assignment's own comparator against the reference.  What the keyword's move did not carry is the *reason* it was moved: the commit names `a.template f<A>()` as one of three refusals it ends, and `member_named` - 5.2.5p1's lookup, the third of the three - had no 14.2 exit at all, so every `a.template f<int>()`, `p->template f<int>()` and `this->template f<int>()` was still `no declaration of f<int> is in scope` where both oracles translate.  Beside it the move made `last()` shorter than the span it was taken from, and three readers took the nested-name-specifier off the spelling by that length: `d.B::template f<int>()` came out as `B::templat names no class`.  Under the new exit, 11p1's access is a fact `instantiate_class` and the alias arm both write onto the declaration one argument list makes and the two function-tier exits never did, so a *private* member template reached through `.` was accepted where both oracles refuse.  And `template_id_entity` may now answer with a typedef-name: 14.7.2p2's reader took `made.primary` without asking, so `template struct P<int>;` over an alias **segfaulted** where both oracles refuse |
| P | `0d1c75c9` | 4 / 4 + 6 recorded | **the two forms 14.7.2 writes one requirement in, with p2 asked at one of them - and the parse form the same commit opened, which reached no reading at all.**  Checkpoint P made every PA22 test parse: 14.2p4's keyword became optional at a member name, 14.2p1's operator template-id got one shared `skip_template_arguments`, and 14.7.2p1's declaration was let end at a `;` no out-of-class constructor otherwise may.  The member reading is right and is *narrower* than the reference's - `s.a < 1 > (0)` over two `int` members is two comparisons here and refused there - and 13.5p6 at the specialization agrees with `g++` on all five shapes probed.  What the commit's own comment claims and the code does not is that "the target is read either way": `explicit_instantiation` returned on `!owed` **before** reading anything, so `extern template struct P<int>;` over an alias, over a non-template, over a name no template declares and over a prefix naming no type were four programs `g++` refuses and this build accepted - the p9 twin of the segfault checkpoint A's audit had just fixed.  Beside it `is_explicit_instantiation_target` began accepting a `SpecialMemberDeclaration` that `explicit_instantiation` had no arm for: `extern template box<int>::box();` passed only because the early return got there first, and `template box<int>::box();` came out of the *class* arm as `an explicit instantiation defines a class`.  And the parser's `names_specialization_` was taken once and never put down, so every body under such a head read its statements as out-of-class special members: `template<> void f<int>(int) { A::A(); }` was refused where the same statement in an ordinary function is accepted |
| F | `6a9c1f3f` | 4 / 4 + 2 recorded | **the two readings one friend definition gets, with 11.3p6's class carried onto only the first - and the region 11.3p6 moved the declaration out of, which the second one reads the pattern against.**  Checkpoint F gave a friend declaration written under a head everything 11.3 already knew: `befriended` asks the pair with each side replaced by its `primary`, `record_template` gained 14.5.4p1's class tier under `templating()`, 11.3p1's granting class is taken before `StandingIn` moves the head, and 11.2p5's naming class is the class a qualified name was looked up in.  Those rules are right and swept clean - 15 friend-class shapes, 11 qualified-access shapes and 7 reveal orderings agree with `g++`, the four scaling dimensions are linear to 800 and flat to depth 256, and `valgrind` is clean over 36 inputs.  What none of it carried is that a friend declaration written in a class *template* is read **twice** and read somewhere else: 14.6p8's reading of the pattern and 14.7.1p1's reading for a specialization both reach `declare_function` with one namespace `where`, so the pattern reading's `defined` made every instantiation of a class defining a friend function template `unwrap is defined twice` - the shape F's own fixture writes in a non-template class - while `g++` accepts one instantiation and refuses two.  Under it, `record_function_template` recorded the *namespace* as the region 14.7.1p1 reads the pattern against, so `template<class U> friend T mixed(box<T>, U)` was `T does not name a type` the moment a call deduced it: 11.3p6 makes the declaration in the namespace and 3.4.1p10 reads the definition where it was written, which is the only region that binds what the class's own instantiation bound.  Beside them 11.3p6's other half - a friend declaration defines a function only where its declarator-id is unqualified - was unwritten, so `template<class T> friend int n::f(T) {}` was a program both oracles refuse and this build translated.  And `reveal_friend` dropped and rebuilt the whole hidden chain's index on every reveal because that index is keyed by the declaration heading the chain, which is the one leaving: n friend declarations of one name cost n walks, **15.64 s at n = 3200 against 0.78 s** once the chain is keyed by what it was made with |
| M | `d2e26a4d` | 6 / 6 + 6 recorded | **the two facts 12 writes about a special member, read off the template and never off the declaration one argument list makes - and the class 12.1's second entry point is owed by.**  Checkpoint M, M2 and the M audit gave a head over a constructor and over a conversion function the class's own declaration, at all four exits it can be written at, and 14.5.6.1p5 is what matches a definition to it.  The declaration side is right and swept clean: 30 shapes over those four exits - value places, packs, defaults, nests, `const`, a base's, a mem-initializer, an out-of-class definition that renames its place - translate and run the value `g++` gives them.  What none of it carried is what 12 says about the *declaration*: `specialize` copied `object_member`, `access` and M2's `special` and left 12.3.1p2's `explicit` and 8.4.3's `= delete` default-constructed, so `only_direct a = 3` and a call of a deleted constructor template were two programs both oracles refuse and this build accepted.  12.3.2p1's conversion function template was landed and **unreachable**: 13.3.1.5p1's candidate set held the template, whose result type is a place, so `int k = a;` was `an expression has no conversion to the type it initialises` where both oracles translate it - the ref binary's own refusal of `a.operator int()` had been recorded as the whole of the gap and it was the *named* exit alone.  The ABI wrote such a specialization's name from the substituted type, `_ZN1AcviIiEEv` against the `_ZN1AcvT_IiEEv` `g++` and the reference both write, at the one of `build_function_name`'s three readings `templated` had not been threaded to.  And a constructor template reached only through a base subobject wrote both of 12.1's entry points where the reference writes one: `writes_base_entry` asked whether the *function* is an instantiation, which for a special member is true on exactly this checkpoint's new surface and nowhere else |


## Current Checkpoint Review

Checkpoint F is where 14.5.4p1's friend template became a friend. A friend
declaration written under a head reached none of what 11.3 already knew:
`befriended` now asks the pair as the use spelled it and then with each side
replaced by its `primary`, `record_template` gained a tier for a head over a
friend elaborated-type-specifier, `function_definition` takes 11.3p1's granting
class off the *template* rather than off the regions a reading stands in,
`equivalent_template` is asked of 11.3p6's hidden chain, and 11.2p5's naming
class is the class a *qualified* name was looked up in as much as the one an
object expression writes.

Those rules are right and each was written at the exits it has. Fifteen friend
class-template shapes - declared before and after, qualified into a namespace,
written twice, tag-mismatched, nested, in a namespace, over a class template, at
arity two, a union, a member of the friend, a grant that reaches no one else -
agree with `g++` and with `pa22/cppgm++-ref`. Eleven qualified-access shapes
agree with `g++` on every one, including the four this build refuses and the
reference accepts. Seven reveal orderings - head first, middle first, tail
first, partial, and the same four over templates - translate and run.

What none of it carried is that a friend declaration written in a class
*template* is read **twice**, and that the second reading stands somewhere the
first did not: **11.3p6 makes the declaration in the namespace and 3.4.1p10
reads the definition where it was written, and the checkpoint answered the
first of those for both readings and the second for neither.**

### Findings

**1. 14.6p8's reading of the pattern counted as a definition.**
11.3p6 declares a friend into one namespace however many readings of the class
reach it, and F's own `equivalent_template` over the hidden chain is what now
pairs them - so 14.6p8's reading of the class template's own definition and
14.7.1p1's reading for a specialization arrived at one declaration, each with
`define` true:

```cpp
template<class T> struct box { T v; box(T x) : v(x) { }
  template<class U> friend U unwrap(box<U> b) { return b.v; } };
int main() { return unwrap(box<int>(9)) == 9 ? 0 : 1; }   // `unwrap is defined twice`
```

Both oracles translate it. The shape is the one F's own
`300-hidden-friend-function-template-adl` fixture writes, in a class that is not
a template - so the feature landed at the exit with one reading and nowhere
else. 14.6p8 describes what a declaration says and translates nothing, so its
reading is a declaration of the function and no definition of it; the
instantiation is what defines one, which is also what makes a *second*
instantiation of such a class the redefinition 14.5.4p1 leaves it as -
`unwrap(box<int>)` and `unwrap(box<char>)` is a program `g++` refuses and this
build now refuses with it.

**2. 14.7.1p1 read the pattern against the region 11.3p6 moved it out of.**
`record_function_template` recorded `where` - the namespace `declare_function`
declared into - as the region an instantiation opens its bindings inside. For
every other declaration that region is where the declaration was written; for a
friend it is the one place that binds nothing the class body had:

```cpp
template<class T> struct box { T v; box(T x) : v(x) { }
  template<class U> friend T mixed(box<T> b, U u) { return b.v + (T)u; } };
int main() { return mixed(box<int>(1), 2) == 3 ? 0 : 1; }  // `T does not name a type`
```

The class-body reading of the same definition already reads its body in the
lexical region, which is what 3.4.1p10 says about a friend defined inline - so
the two readings of one definition were two descriptions of it. `pattern_region`
is the head the definition was written under wherever the declaration is hidden,
which is the region `parameterised` already reads to decide the same question,
and it is the only one that reaches what the class's own instantiation bound.
The three spellings - the owner's parameter in the result, in the first
parameter and in the second - all translate now, and their two specializations
are `_Z4grabIiET_3boxIiES0_` and `_Z4grabIiET_3boxIcES0_`, which is what `g++`
writes.

**3. 11.3p6's other half was unwritten.** A friend declaration defines a
function only where the name it writes is unqualified; 11.3p10's qualified one
names a function the region already declared, and a declaration made elsewhere
is no place to write a body. F rewrote exactly the parameter this rule lives
next to - a qualified declarator-id on a definition now has to match a
declaration - and read it as 9.3p2 alone, so

```cpp
namespace n { template<class T> int f(T); }
struct host { template<class T> friend int n::f(T t) { return (int)t; } };
```

was a program `pa22/cppgm++-ref` and `g++ -pedantic-errors` both refuse and this
build translated. The refusal is written where `friend_target` has just
answered, so both the template and the non-template spelling reach it.

**4. `reveal_friend` re-keyed the whole chain on every reveal.** 13.1's index of
a chain is keyed by the declaration the name would bind, and 7.3.1.2p3's reveal
takes that declaration out - so the reader dropped and rebuilt every entry of
the chain, recomputing a signature per entry, and then walked it again for its
tail. n friend declarations of one name in one namespace cost n walks of n:

| n friend declarations of one name, each revealed | before | after |
|---|---|---|
| 800 | 0.82 s | 0.19 s |
| 1600 | 3.23 s | 0.38 s |
| 3200 | **15.64 s** | **0.79 s** |

`Scope::hidden_index` is the declaration a chain was *made* with, which is what
the entries of everything still hidden stay keyed by however many leave - so a
reveal drops one entry, unlinks one declaration and keeps the tail the chain
already had. It is one probe per reveal and the shape is linear. F is what made
this path reachable for a *template*: before it, two declarations of one friend
function template matched nothing and no reveal happened at all.

### What the review confirmed rather than found

- **`befriended`'s four probes are the four.** A grant is recorded between two
  classes or between a class and a template, never with a template on the
  granting side, and a grant recorded between two specializations reaches no
  other one: `friend struct peeker;` in `template<> struct owner<int>` leaves
  `owner<char>` refusing, and 14.5.4p1's grant between two templates answers
  every `late<A...>` at four hash probes and no walk.
- **The dialect gate is the dialect the rule has entities in.**
  `templating()` is `lowering() || checking_ > 0`, which is exactly the two
  readings a template entity exists in; PA11 and PA12 never reach
  `record_template` at all, so their dumps are what they were - verified
  identical to `pa12/cppgm++-ref` on a friend class template.
- **Scaling is linear in every dimension and flat in depth.** n
  specializations, n distinct friend class templates, n hidden friend function
  templates and n class templates each defining one are 0.02 → 0.28 s from 100
  to 800; a friend declared in a class nested 24 deep and a friend template
  named with an argument nested 24 deep are 0.00 s and 6 MB.
- **No gate and no skipped work.** Neither the checkpoint's diff nor this
  audit's holds a `getenv`, a fixture name, a dialect switch keyed on anything
  but a dialect, a timeout or an environment read. `valgrind -q
  --error-exitcode=9` is clean over 36 inputs, the four largest scaling ones
  among them. The unit `300-friend-function-template-in-class-template`,
  `300-friend-function-template-writes-owner-parameter` and
  `300-qualified-friend-template-definition-bad` were written for the first
  three findings and `pa22/cppgm++-ref` generated every one of their `.ref`s;
  all 18 `course/pa22` references regenerate byte for byte from it.
