#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "type_model.h"

#include "sema_template.h"

struct AstNode;
class Scope;
class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;

// 14.6p8 with 14.6.1p1: the readings a template definition gets where it
// stands, and the class each of them is read as.
//
// 14.7.1p1 makes an instantiation a second reading of the same syntax, so every
// template definition is read at least twice: once where it was written, and
// once per argument list that reaches it.  The first of those is what this owns.
// 14.6p8 says what it can ask - the declarations the definition makes and 3.4p1's
// lookup of the names it writes, and nothing an argument list settles - and
// 14.6.1p1 says what it reads them *as*: the class the definition's own name over
// its own places denotes, with each place standing for itself.  That class is the
// current instantiation, and 14.5.5p1 leaves one per body a naming may be read
// from: the primary's, and each pattern written beside it.
//
// 14.5.1.3p1's out-of-class member definition is the same reading arriving later.
// The class it declares into is the current instantiation of whichever body its
// declarator-id named, and the names its own head wrote stand for that body's
// places however that body spelled them - so the definition travels with the
// pattern it belongs to and is read again for each specialization made from it.
class PatternReading
{
public:
	explicit PatternReading(SemaAnalyzer& analyzer);

	// 14.6.1p1: the current instantiation of the primary template `primary` -
	// the class its own definition declares - beside the kept region binding
	// each place to a type standing for itself.  Both are made once, because
	// every later out-of-class member definition is read against the same two.
	SemaEntity& current(SemaEntity& primary);

	// 14.6.1p1 with 14.5.5p1: the same for the partial specialization at `at`,
	// which is a body of its own over places of its own - so the class it
	// declares is the specialization its *pattern* names and the region is its
	// own head's.  Null where that body declared no class to read.
	SemaEntity* current_pattern(SemaEntity& primary, std::size_t at);

	// 14.6p8: the reading a class template's own definition gets where it
	// stands, which is the current instantiation completed from its body.
	void read_class(SemaEntity& primary);

	// 14.6p8 and 14.5.1.3p1: the reading one out-of-class member definition
	// gets where it stands, against the current instantiation of the body at
	// `at` - `Specialization::kNoPartial` for the primary's own.
	void read_member(SemaEntity& primary, const AstNode& clause,
	                 const AstNode& pattern, std::size_t at);

	// 14.5.2p3: what one such definition declares, which for a member template
	// is a head of its own standing over the declaration.
	void read_declaration(const AstNode& node, const SemaContext& ctx);

	// 14.5.1.3p1: the class template a definition written outside its class is
	// a member of, found from the template-id its declarator-id was qualified
	// with, and that template-id's own spelling in `wrote`.  Null for a
	// declaration that is a member of no such template.
	SemaEntity* owner(const AstNode& node, const SemaContext& ctx,
	                  std::string* wrote);

	// 14.5.2p3: the member template a second head's declarator-id declares a
	// member of, where the classes the specifier writes before it are ones the
	// reading that reached here already settled, and that component's own
	// spelling in `wrote`.  Null where it names none.
	SemaEntity* nested_owner(const AstNode& node, const SemaContext& ctx,
	                         std::string* wrote);

	// 14.5.1.3p1: holds `declared` as an out-of-class member definition of the
	// body at `at` of `primary`, reads it where it stands, and reads it again
	// for every specialization already made from that body.
	void record(SemaEntity& primary, std::size_t at, const AstNode& clause,
	            const AstNode& declared);

	// 14.5.1.3p1: reads the member definition `member` of the body at `at`
	// against the arguments `made` was made from.  A specialization 14.5.5.1p1
	// read from another body holds no member of this one, so it is skipped
	// rather than read.
	void instantiate(SemaEntity& made, const TemplateInfo::Member& member,
	                 std::size_t at);

	// 14.6.1p1: the argument list `head`'s own places make - each standing for
	// itself, and a pack place standing for the expansion a definition writes.
	std::vector<TypeId> places(const TemplateInfo& head);

private:
	// 14.5.1.3p1: the name one out-of-class definition wrote - a class-head-name,
	// a constructor's declarator-id, or the declarator-id of the one declarator
	// the declaration writes.  Empty where the declaration names nothing.
	static std::string member_spelling(const AstNode& node);

	// 14.7.1p1: the class the template-id `component` names in the region `in` -
	// null for one whose arguments that region cannot settle, which is what says
	// the head standing over this declaration declares them.
	SemaEntity* settled(const std::string& component, const SemaContext& ctx,
	                    Scope* in);

	PatternReading(const PatternReading&);
	PatternReading& operator=(const PatternReading&);

	SemaAnalyzer& analyzer_;
};
