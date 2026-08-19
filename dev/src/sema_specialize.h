#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "type_model.h"

struct AstNode;
class QualifiedName;
class Scope;
class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;
class TemplateId;
struct TemplateInfo;

// 14.5.5 and 14.5.1p1: the two declarations a template head writes that declare
// neither the primary template nor an ordinary specialization of it.
//
// 14.5.5p1's partial specialization is a template whose head declares places of
// its own and whose declarator-id writes an argument *pattern* over them.  So it
// is not one more declaration of the primary and it makes no specialization by
// itself: it stands beside the primary until an argument list arrives, and
// 14.5.5.1p1 then asks which pattern that list matches - which is 14.8.2's
// match, one list of entries against another, already owned by `sema_deduce.h`.
// What the chosen pattern gives back is the arguments *its* head took, and the
// body it wrote is read against those in place of the primary's.
//
// 14.5.1p1's variable template is the same three steps over an object rather
// than a class: a head, a pattern, and one declaration per argument list.  What
// differs is what an instantiation of it comes to.  A specialization of a
// variable template is used where 5.19 asks for a constant - a template
// argument, a `static_assert`, the value an instantiated body returns - so what
// the reading leaves behind is the constant its initializer evaluated to, and
// no object at all: nothing takes its address, so the object file writes none.
//
// The choice is a fact of the *template* rather than of any one use, so it is
// memoised on the primary's `TemplateInfo` under the interned argument list a
// naming already has in hand, and a template no head partially specialized -
// which is every template a program without a trait writes - pays one test of
// an empty vector.
class Specialization
{
public:
	explicit Specialization(SemaAnalyzer& analyzer);

	// 14.5.5p1 and 14.5.1p1: records what a template-declaration whose head
	// declared parameters wrote, where that is a partial specialization of a
	// class or variable template, or a variable template itself.  False where
	// the declaration is none of those, which leaves the ordinary walk to read
	// it as it did before.
	bool record(const AstNode& clause, const AstNode& declared,
	            const SemaContext& ctx);

	// 14.7.3p1 over 14.5.1p1's variable template: the declaration a
	// `template<>` head wrote for one argument list, held against that list so
	// that the reading of a specialization finds it in place of the pattern.
	bool record_explicit(const AstNode& declared, const SemaContext& ctx);

	// 14.7.3p11 with 14.8.2.6: which of several templates the declaration a
	// `template<>` head wrote is a specialization of, where one written
	// argument list fits more than one declaration of an overloaded name.  The
	// type the declarator wrote is what tells them apart, and 14.5.6.2's
	// ordering is what leaves one where the type fits two.  A type no candidate
	// in the set has is p11's "exactly one template" refused outright.  Null
	// where the set holds no candidate at all and where 14.5.6.2 leaves a pair
	// unordered - the second of which p11 also calls ill-formed and the
	// reference binary translates, so it is left to the ordinary walk to read
	// as it did before.
	SemaEntity* explicit_target(const AstNode& declared,
	                            const AstNode& declarator,
	                            const std::string& written,
	                            const SemaContext& ctx,
	                            const std::vector<SemaEntity*>& found);
	// 14.8.2.6p1 where the head wrote no argument list at all: the specialization
	// each declaration of the name deduces from that type, and how many of them
	// p11 could have been asked about.
	std::size_t gather_deduced(const std::string& written, TypeId declared_type,
	                           TypeId member_type, const SemaContext& ctx,
	                           const QualifiedName& spelled,
	                           std::vector<SemaEntity*>& out);

	// 14.7.3p1 over a member of a class template specialization: which of the two
	// definitions of it this unit holds.
	//
	// 14.7.1p1's reading of the pattern gives one to every argument list, and the
	// program may write one out for exactly one list - so what the clause needs is
	// not a second record beside `record_explicit`'s but the answer to "which one
	// is this declaration's", kept on the declaration itself.  `supersede` is what
	// a written definition does to a read one, `require_replaceable` is 3.2p1's
	// refusal of every other second definition, and `note_object` is 9.4.2p2's
	// static data member, whose initializer is a value 5.19p2 reads only while the
	// pattern's is the one definition the unit has.
	//
	// Which definition the unit holds is what the clause is about and not which
	// of the two the program wrote first, so each of the three doors asks the
	// question both ways round: `holds_written_definition` is the reading of the
	// pattern arriving *below* a definition the program already wrote out, which
	// 14.7.3p1 leaves unmade rather than refuses.  `require_replaceable` says so
	// by answering false, and the other two doors ask it themselves.
	bool holds_written_definition(const SemaEntity& member) const;
	void supersede(SemaEntity& function);
	bool require_replaceable(SemaEntity& member, const std::string& spelled);
	void note_object(SemaEntity& member, bool instantiated);

	// 7.1.3p2 with 14.5.7p1: the alias template a head over an
	// alias-declaration declares, bound in the region the head stands in.
	// False where the declaration is no alias-declaration or the head declares
	// a place this milestone gives no meaning to.
	bool record_alias(const AstNode& clause, const AstNode& declared,
	                  const SemaContext& ctx);

	// 7.1.3p2: the *type* a template-id over an alias template names, which is
	// the type-id it was declared with and the arguments substituted into it.
	SemaEntity& alias(SemaEntity& primary, const TemplateId& id,
	                  const SemaContext& ctx);

	// The same over a list already bound, which is what 14.7.1p1's substitution
	// has: the arguments a naming left standing, built again against its own.
	SemaEntity& alias_arguments(SemaEntity& primary,
	                            const std::vector<TypeId>& arguments);

	// 14.2p4: the stand-in one component of a name written after a prefix no
	// argument list has settled leaves, whose own argument list - where the
	// component is a template-id - is read here and kept beside the prefix and
	// the name.  It is this reading's because what such a list eventually names
	// is a specialization, and 14.1p4 has no head to ask which kind each
	// argument is: a type-id is read as a type and anything else as 5.19's
	// expression, which is the order every other spelled list settles.
	SemaEntity& member_component(TypeId prefix, const std::string& written,
	                             const SemaContext& ctx);

	// 7.1.3p2 with 14.8.2p8: what 14.7.1p1's substitution makes of a naming
	// that kept the arguments its type-id discarded.  They are built first -
	// which is where `typename T::x` over a `T` with no `x` refuses and
	// discards the candidate - and the type-id is read again over what they
	// came to.
	TypeId substituted_alias(TypeId naming,
	                         const std::unordered_map<TypeId, TypeId>& bindings,
	                         std::unordered_map<TypeId, TypeId>& memo);

	// 14.5.5.1p1: which partial specialization of `primary` the argument list
	// `arguments` matches, and the arguments *its* own head took.  `kNoPartial`
	// where the list matches none, which leaves the primary's own pattern.
	std::size_t chosen(SemaEntity& primary,
	                   const std::vector<TypeId>& arguments,
	                   std::vector<TypeId>& deduced);

	// 14.5.5p1 with 14.5.1.3p1: which pattern of `primary` an out-of-class
	// member definition is a member of, where `wrote` is the template-id its
	// declarator-id named that template by and `clause` is the definition's own
	// head.  A pattern is matched by 14.5.6.1p5's signature, because this head
	// spells the places its own way; `kNoPartial` where the arguments written
	// are the template's own places, which is a member of the primary.
	std::size_t member_pattern(SemaEntity& primary, const std::string& wrote,
	                           const AstNode& clause, const SemaContext& ctx);

	// 14.7.1p1 over a variable template: the declaration `arguments` makes of
	// `primary`, which is the constant its initializer evaluates to.
	SemaEntity& variable(SemaEntity& primary,
	                     const std::vector<TypeId>& arguments,
	                     const SemaContext& ctx);

	// 14.5.5p1: the place a list that matched no pattern stands at.
	static const std::size_t kNoPartial = static_cast<std::size_t>(-1);

private:
	Specialization(const Specialization&);
	Specialization& operator=(const Specialization&);

	// 14.5.5p2: the argument pattern a partial specialization's declarator-id
	// wrote, read in a region of its own binding the places its head declared.
	// False where that head declares a place this milestone gives no meaning
	// to, which leaves the pattern unrecorded and the primary answering.
	bool read_pattern(SemaEntity& primary, const TemplateId& id,
	                  const AstNode& clause, const SemaContext& ctx,
	                  TemplateInfo*& head, std::vector<TypeId>& pattern);

	// 14.5.5p2: the pattern held beside the primary, replacing the one an
	// earlier declaration of the same pattern left.
	void hold_pattern(TemplateInfo& info, TemplateInfo& head,
	                  std::vector<TypeId>& pattern, const AstNode& declared);

	// 14.5.6.1p5: the pattern with each place standing for its position, which
	// is what two heads that wrote one pattern share.
	std::uint32_t canonical_pattern(const TemplateInfo& head,
	                                const std::vector<TypeId>& pattern);

	// 14.5.1p1: the variable template a head over a plain declarator-id
	// declares, bound in the region the head stands in.
	bool declare_variable(const AstNode& clause, const AstNode& declared,
	                      const AstNode& init, const std::string& name,
	                      const SemaContext& ctx);

	// 8p1: the type one init-declarator of `declared` declares, read in `ctx`.
	TypeId declared_type(const AstNode& declared, const AstNode& init,
	                     const SemaContext& ctx);

	// 14.2: the template-id one argument list spells over `primary`, which is
	// what names the declaration that list made of it.
	std::string spelled(const SemaEntity& primary,
	                    const std::vector<TypeId>& arguments);

	// 7.1.3p2 with 14.8.2p8: the entry a naming of `primary` keeps where its
	// type-id named `type` and the list held an argument a substitution builds
	// and that type does not mention.  `kNoType` where the type-id named every
	// argument it was given and where the ones it threw away are places a list
	// binds rather than readings - 14.5.7p1 leaves those the type the type-id
	// named, which is what a second declaration of the same template may write
	// out longhand.
	TypeId discarded_arguments(SemaEntity& primary,
	                           const std::vector<TypeId>& arguments,
	                           TypeId type);

	// 14.7.1p1: whether a substitution builds `argument` again or looks it up,
	// which is what says whether throwing it away can lose a refusal.
	bool rebuilt(TypeId argument) const;

	// 14.5.5.1p1 with 14.8.2p8: whether the pattern at `index` matches
	// `arguments`, and what the places its own head declared were deduced to.
	// A substitution the read-back of the pattern refuses discards that pattern
	// rather than the program, which is the detector idiom.
	bool matches(const TemplateInfo& info, std::size_t index,
	             const std::vector<TypeId>& arguments,
	             std::vector<TypeId>& deduced);

	// 14.5.5p8.3: what the places `partial`'s own head declared were deduced
	// to, written out flat.  A place the match left unbound is a defect of that
	// declaration and refuses the program.
	bool took_places(const TemplateInfo& info, std::size_t index,
	                 const std::unordered_map<TypeId, TypeId>& bindings,
	                 std::vector<TypeId>& deduced);

	// 14.8.2.5p5: whether the pattern read back with each place standing for what
	// it was deduced to is the argument list itself, which is what settles a
	// pattern the match walked past a non-deduced context of.
	bool substitution_agrees(const std::vector<TypeId>& pattern,
	                         const std::unordered_map<TypeId, TypeId>& bindings,
	                         const std::vector<TypeId>& arguments);

	// 14.5.5.2p1: which of `matched` every other one is at least as general as.
	std::size_t most_specialized(const TemplateInfo& info,
	                             const std::string& name,
	                             const std::vector<std::size_t>& matched);

	// 14.5.5.2p1: whether every argument list `left` matches is one `right`
	// matches too, which is the rewriting of both as function templates that
	// clause asks for, read as the one match 14.8.2.5p4 already is.
	bool at_least_as_specialized(const TemplateInfo& info,
	                             std::size_t left, std::size_t right);

	// 14.5.1p1: the declaration a variable template's specialization is read
	// from, and the region binding what that declaration's own head took.
	struct Reading
	{
		Reading();

		const AstNode* declared;
		Scope* region;
	};
	Reading variable_reading(SemaEntity& primary,
	                         const std::vector<TypeId>& arguments,
	                         std::uint32_t list);

	// 14.5.1p1 and 5.19: the type one init-declarator of a variable template
	// declares and the constant its initializer evaluates to, read in
	// `reading`'s region.
	SemaEntity& read_variable(SemaEntity& primary, const Reading& reading,
	                          const std::vector<TypeId>& arguments,
	                          std::uint32_t list);

	SemaAnalyzer& analyzer_;
};
