#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast_model.h"
#include "sema_name.h"
#include "sema_pack.h"
#include "sema_template.h"
#include "sema_template_head.h"

// 14.1p2's template-parameter-clause and 14.3p1's template-argument-list.
//
// A template head declares *places* rather than declarations: what each place
// is - a type, a value of a written type, or a template - is settled once, in
// 14.6.1p1's own region, and every argument list read afterwards substitutes its
// own bindings into what was settled there rather than reading the head's syntax
// again.  This file owns both halves of that: the head, and the reading of one
// written argument list against it.
//
// 14.2 writes an argument list inside a name, so an argument arrives as text.
// A type argument is turned back into what was written by `sema_type_id.cpp`,
// a value argument by `sema_value_expression.cpp` and a template argument by
// 3.4.3's ordinary lookup of the name; what belongs here is which of the three
// a place asked for, and what the answer is bound as.

namespace
{

std::string decimal_text(unsigned long long value)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
}

const AstNode* first_child(const AstNode& node, AstKind kind)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->kind == kind)
		{
			return node.children[index];
		}
	}
	return nullptr;
}

// The name a declarator declares, taken from the syntax alone.  14.1p2's
// parameter names are read before any region exists to look them up in, so the
// walk is over the tree and not over a declaration.
// 14.1p2 at a template place: the name the default argument wrote, taken from
// the type-id shape every default argument is written in.  A template-name has
// no type-specifier of its own, so what stands there is the one name.
std::string written_name(const AstNode& node)
{
	if (node.kind == AstKind::TypeName || node.kind == AstKind::Identifier ||
	    node.kind == AstKind::DeclSpecifier)
	{
		return node.text;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const std::string found = written_name(*node.children[index]);
		if (!found.empty())
		{
			return found;
		}
	}
	return std::string();
}

const AstNode* declarator_name(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& child = *declarator.children[index];
		if (child.kind == AstKind::Identifier)
		{
			return &child;
		}
		if (child.kind == AstKind::NestedDeclarator && !child.children.empty())
		{
			const AstNode* const inner = declarator_name(*child.children[0]);
			if (inner != nullptr)
			{
				return inner;
			}
		}
	}
	return nullptr;
}

}

// 14.3p1: the argument a written spelling makes at the place `places[index]`
// declared, which a function template's head declares as a declaration of its
// own rather than as an entry of a `TemplateInfo`.
TypeId TemplateHead::explicit_argument(const std::vector<SemaEntity*>& places,
                                       std::size_t index,
                                       const std::vector<TypeId>& before,
                                       const std::string& written,
                                       const SemaContext& ctx)
{
	if (index < places.size() &&
	    analyzer_.types_.is_parameter_template(places[index]->type))
	{
		// 14.3.3p1: a template place takes a template-name, which is neither
		// 8.1p1's type-id nor 5.19's constant expression.
		return template_argument(written, analyzer_.place_head(places[index]->type),
		                         ctx);
	}
	const TypeId place = place_type(places, index, before);
	if (place == kNoType)
	{
		return analyzer_.template_argument_type(written, ctx);
	}
	return analyzer_.template_argument_value(written, place, ctx);
}

// 14.1p9 at a template place: the default argument names a template.
//
// 8.1p1's type-id is the shape every default argument is written in, and a
// template-name has no type-specifier of its own - so what stands in that shape
// is the one name, read as an argument at the place rather than as a type.
TypeId TemplateHead::place_default(const AstNode& written,
                                   const TemplateInfo* head,
                                   const SemaContext& ctx)
{
	return template_argument(written_name(written), head, ctx);
}

// 14.3.3p1: the template a spelling written at a template place names.
//
// 14.2 writes the argument list inside a name, so the argument arrives as text
// and the answer is 3.4.3's ordinary lookup of it.  What comes back is one of
// three things: a template the program declared, which the argument *is*; a
// place another head declared, which stands for itself until an argument list
// settles it; or nothing a template-name may be, which is the program's error.
TypeId TemplateHead::template_argument(const std::string& written,
                                       const TemplateInfo* place,
                                       const SemaContext& ctx)
{
	// 14.2p4: a member template named through a class writes `template` in
	// front of its name, which says the name is a template and is no part of
	// the name a lookup asks for.
	const std::string spelling = without_template_keyword(written);
	if (QualifiedName(spelling).names_a_template_id())
	{
		// 14.3.3p1 leaves room at a template place for a template-name, and a
		// template-id is 14.3.1p1's type argument - the class one list already
		// made.  The two reach 3.4.3 as one spelling and the lookup answers
		// both, because 14.6.1p1's injected-class-name names the current
		// specialization *and* the template it was made of - so which of them
		// was written is a fact of the spelling and is read here.
		throw std::runtime_error(spelling + " names a specialization where "
		                         "14.3.3p1 leaves room for a template-name");
	}
	SemaEntity* named = analyzer_.resolve(spelling, ctx, LookupKind::Any);
	if (named != nullptr && analyzer_.types_.is_parameter_template(named->type))
	{
		// 14.6.2p1: a place of the head being read stands for itself, and what
		// 14.3.3p1 asks of it is asked again where the argument arrives.
		return named->type;
	}
	if (named != nullptr && named->templated == nullptr &&
	    analyzer_.types_.is_dependent(named->type))
	{
		// 14.6.2p1: a name written after a prefix no argument list has settled
		// says nothing this reading can answer - which template it names is the
		// instantiation's to find, so the naming stands as it was written.
		return named->type;
	}
	if (named != nullptr && named->templated == nullptr &&
	    named->primary != nullptr)
	{
		// 14.6.1p1: inside a specialization the injected-class-name names that
		// specialization, and a template place is written the *template* it was
		// made of.
		named = named->primary;
	}
	if (named == nullptr || named->templated == nullptr ||
	    (named->kind != SemaKind::Class && named->kind != SemaKind::Typedef))
	{
		throw std::runtime_error(spelling + " is written where a template-name "
		                         "stands and names no class or alias template");
	}
	// 14.3.3p1 asks what the argument's own places are, which is what
	// 14.6.1p1's region settles - so the template is asked the question every
	// instantiation of it would have asked first.
	open_region(*named->templated);
	if (place != nullptr && !argument_matches(*place, *named->templated))
	{
		throw std::runtime_error(spelling + " declares places the template "
		                         "parameter it is written at does not accept");
	}
	return name_argument(*named);
}

// 14.3.3p1: the entry standing for one template, made once per declaration -
// so two argument lists that named the same template are one list, which is
// what makes `holder<box>` written twice one specialization.
TypeId TemplateHead::name_argument(SemaEntity& named)
{
	// 3.4.3: the object file names the template from outside every region
	// around it, which for a class template is the spelling its own type
	// already carries - a name written through the regions the declaration
	// stands in rather than through the one that reached it.
	const std::string qualified =
		named.kind == SemaKind::Class && named.type != kNoType
			? analyzer_.types_.user_qualified_name(named.type)
			: abi_qualified_name(named);
	const TypeId made = analyzer_.types_.template_name_type(
		named.id, named.name, qualified);
	analyzer_.model_.own_type(made, named);
	return made;
}

SemaEntity* TemplateHead::named_template(TypeId argument) const
{
	return analyzer_.types_.is_template_name(argument)
		? analyzer_.model_.type_owner(argument)
		: nullptr;
}

// 14.1p4: the name a non-type parameter's declarator gave it, empty where it
// wrote none.
std::string TemplateHead::non_type_name(const AstNode& parameter)
{
	const AstNode* const declarator =
		first_child(parameter, AstKind::Declarator);
	if (declarator == nullptr)
	{
		return std::string();
	}
	const AstNode* const id = declarator_name(*declarator);
	return id == nullptr ? std::string() : id->text;
}

// 14.1p2: the head a template-template place wrote inside another head.
//
// It is a head like any other - 14.3.3p1 asks its places what a written
// argument's own places have to be - and 14.5.6.1p5 lets one template be
// declared many times, each spelling that clause again.  So the reading is
// keyed by the clause node: one head per clause, however many declarations
// reach it, and a nest of them is read once at each level.
TemplateInfo& TemplateHead::parameter_head(const AstNode* clause)
{
	const std::unordered_map<const AstNode*, TemplateInfo*>::const_iterator held =
		analyzer_.parameter_heads_.find(clause);
	if (held != analyzer_.parameter_heads_.end())
	{
		return *held->second;
	}
	analyzer_.template_patterns_.push_back(TemplateInfo());
	TemplateInfo& made = analyzer_.template_patterns_.back();
	analyzer_.parameter_heads_.insert(std::make_pair(clause, &made));
	if (clause != nullptr)
	{
		read(*clause, made);
	}
	return made;
}

// 14.3.3p1: whether a template declared with the head `argument` may stand at a
// place declared with the head `place`.
//
// 14.3.3p1 asks that each place A declares match the place P declared at the
// same position, and that two places match when they are of the same *kind* - a
// type place, a value place, a template place - with a template place asking the
// same question one level down.  A pack on either side is the sentence after it:
// it matches zero or more of the other head's places, whatever the other head
// wrote them as, so a head that stops at a pack fits every longer one.
//
// 14.1p9 answers the one asymmetry left: a place A declares past everything P
// wrote is one no naming through P can fill, so it stands only where A's own
// head gave it a default.
bool TemplateHead::argument_matches(const TemplateInfo& place,
                                    const TemplateInfo& argument)
{
	if (!argument.supported)
	{
		return false;
	}
	const std::vector<TemplateInfo::Parameter>& wanted = place.parameters;
	const std::vector<TemplateInfo::Parameter>& given = argument.parameters;
	std::size_t index = 0;
	for (; index < wanted.size() && index < given.size(); ++index)
	{
		if (wanted[index].pack || given[index].pack)
		{
			// 14.3.3p1: the pack matches every place the other head has left,
			// which is what makes `template<class...> class` stand for a head
			// of any length - and it is the *same* place matched against each
			// of them, so each pair is asked what a fixed pair is asked.
			const bool run_wanted = wanted[index].pack;
			const std::size_t rest = run_wanted ? given.size() : wanted.size();
			for (std::size_t at = index; at < rest; ++at)
			{
				if (!places_match(place, run_wanted ? index : at, argument,
				                  run_wanted ? at : index))
				{
					return false;
				}
			}
			return true;
		}
		if (!places_match(place, index, argument, index))
		{
			return false;
		}
	}
	if (index < wanted.size())
	{
		// 14.3.3p1: the pack P declared matches every place A has left, and A
		// having none left is a run of none rather than a place it is missing -
		// so `template<class, class...> class` takes a head of one.  A place P
		// declared that is no pack is one A has not got, and 14.1p11 leaves the
		// pack last, so nothing stands past it to be missing either.
		return wanted[index].pack && index + 1 == wanted.size();
	}
	for (; index < given.size(); ++index)
	{
		if (index >= argument.defaults.size() ||
		    argument.defaults[index].written == nullptr)
		{
			return false;
		}
	}
	return true;
}

// 14.3.3p1: whether the place `place` declared at `at` accepts the place
// `argument` declared at `index`.
//
// It is three questions - the kind, the type a value place names a value of,
// and the head a template place wrote one level down - and it is one reading,
// because a pack matches each of the other head's remaining places by exactly
// what a fixed place is matched by.
bool TemplateHead::places_match(const TemplateInfo& place, std::size_t at,
                                const TemplateInfo& argument,
                                std::size_t index)
{
	const TemplateInfo::Parameter& wanted = place.parameters[at];
	const TemplateInfo::Parameter& given = argument.parameters[index];
	if (given.value != wanted.value || given.templated != wanted.templated)
	{
		return false;
	}
	if (given.value &&
	    place_signature(place, at) != place_signature(argument, index))
	{
		// 14.3.3p1: two value places match only where the types they name a
		// value of are equivalent, so `template<class T, T>` does not stand at
		// a place written `template<class, unsigned long>`.
		return false;
	}
	return !given.templated || given.head == nullptr || wanted.head == nullptr ||
		argument_matches(*wanted.head, *given.head);
}

// 14.3.3p1 with 14.5.6.1p5: the type one value place names a value of, with
// each place of its own head standing for its position - which is what makes
// the question "are the two types equivalent" one integer compare, and what
// keeps two heads that spell `template<class T, T v>` one answer.
TypeId TemplateHead::place_signature(const TemplateInfo& head,
                                     std::size_t index)
{
	const TypeId written = head.parameters[index].type;
	if (written == kNoType || !analyzer_.types_.is_dependent(written))
	{
		return written;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t at = 0; at < head.parameters.size(); ++at)
	{
		bindings.insert(std::make_pair(
			head.parameters[at].self,
			analyzer_.canonical_parameter(at, head.parameters[at].pack)));
	}
	std::unordered_map<TypeId, TypeId> memo;
	return analyzer_.substituted(written, bindings, memo);
}

// 14.1p2: the parameters a template-parameter-clause declared, in the order it
// wrote them, and 14.1p9's default arguments beside them.  A parameter this
// milestone gives no meaning to leaves the head unsupported rather than
// refusing it here: 14p1 lets a program declare a template it never names, and
// the declaration says nothing about a type until an instantiation asks.
void TemplateHead::read(const AstNode& clause, TemplateInfo& info,
                        bool primary)
{
	const AstNode* const list =
		first_child(clause, AstKind::TemplateParameterList);
	if (list == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& parameter = *list->children[index];
		const AstNode* const id = first_child(parameter, AstKind::Identifier);
		TemplateInfo::Parameter place;
		place.pack = first_child(parameter, AstKind::ParameterPack) != nullptr;
		if (parameter.kind == AstKind::NonTypeTemplateParameter)
		{
			// 14.1p4: a non-type parameter names a value of the type its own
			// declaration writes, which is read where the place is bound - the
			// type may name the parameters before it.
			place.value = true;
			place.written = &parameter;
			place.name = non_type_name(parameter);
		}
		else if (parameter.kind != AstKind::TypeParameter)
		{
			info.supported = false;
			info.parameters.push_back(TemplateInfo::Parameter());
			info.defaults.push_back(TemplateInfo::Default());
			continue;
		}
		else
		{
			// 14.1p3: a type parameter with no identifier declares nothing a
			// dependent name can reach, but it still takes an argument.
			place.name = id == nullptr ? std::string() : id->text;
			if (first_child(parameter, AstKind::TemplateTemplateParameter) !=
			    nullptr)
			{
				// 14.1p2: `template<…> class C` declares a place that binds a
				// template.  Its own clause is a head of its own - the places
				// 14.3.3p1 matches a written argument's head against - and it
				// is read once per clause however many declarations of this
				// template spell it.
				place.templated = true;
				place.head = &parameter_head(
					first_child(parameter, AstKind::TemplateParameterClause));
			}
		}
		if (place.pack && primary && index + 1 != list->children.size())
		{
			// 14.1p11: a pack in a primary template's head is the last place it
			// declares, because every argument a list writes past the places
			// before it belongs to the pack.  14.5.5p1's head writes no such
			// list - every place of it is deduced from the pattern - so a pack
			// stands anywhere in one.
			info.supported = false;
		}
		info.parameters.push_back(place);
		const AstNode* const written =
			first_child(parameter, AstKind::DefaultTemplateArgument);
		TemplateInfo::Default fill;
		if (written != nullptr)
		{
			// 14.1p9: a type place's default is a type-id and a value place's
			// is an expression, which is the same node either way - what tells
			// them apart is the place it fills.
			fill.written = place.value
				? (written->children.empty() ? nullptr : written->children[0])
				: first_child(*written, AstKind::TypeId);
		}
		if (fill.written != nullptr)
		{
			// 14.1p9: a default argument may name the parameters written
			// before it, and 14.1p2 leaves the names *this* head gave those
			// places as the only ones it can have written - so they are kept
			// with it, however the declaration the merge leaves standing
			// spells them.
			for (std::size_t before = 0; before + 1 < info.parameters.size();
			     ++before)
			{
				fill.spelled.push_back(info.parameters[before].name);
			}
		}
		info.defaults.push_back(fill);
	}
}

// 14.1p4: the type a non-type template parameter names a value of, read in the
// region its own head opened - so `template<class T, T v>` reaches the place
// before it, and 14.6.2p1 leaves that type dependent until an argument arrives.
//
// 14.1p7 leaves the rest of 8.3's declarators to later milestones: a parameter
// of pointer, reference or class type is not part of the supported subset, and
// the place is refused where its type is neither integral nor an enumeration.
TypeId TemplateHead::non_type_type(const AstNode& parameter,
                                   const SemaContext& ctx)
{
	const AstNode* seq = first_child(parameter, AstKind::DeclSpecifierSeq);
	if (seq == nullptr)
	{
		seq = first_child(parameter, AstKind::TypeSpecifierSeq);
	}
	if (seq == nullptr)
	{
		throw std::runtime_error("a non-type template parameter declares no "
		                         "type");
	}
	SemaSpan span;
	span.begin = parameter.begin;
	span.end = parameter.end;
	const DeclSpecifiers specifiers =
		analyzer_.read_specifiers(*seq, ctx, span, true, std::string());
	TypeId type = analyzer_.specifier_type(specifiers);
	const AstNode* const declarator =
		first_child(parameter, AstKind::Declarator);
	if (declarator != nullptr)
	{
		std::string ignored;
		type = analyzer_.declarator_type(*declarator, type, ctx, &ignored);
	}
	// 14.1p4: the type shall be integral or an enumeration, or one of the forms
	// this milestone leaves out; a dependent one is whatever the argument makes
	// of it, so it is checked where the argument is bound.
	if (!analyzer_.types_.is_dependent(type) && analyzer_.integral_type(type) == kNoType)
	{
		throw std::runtime_error("a non-type template parameter of " +
		                         analyzer_.types_.description(type) + " is outside the "
		                         "PA20 subset");
	}
	return type;
}

// The source spelling of a type, which is what a specialization is named by.
// 14.7.1p1 makes one declaration of every naming of one argument list, so two
// spellings of one type - a typedef-name and what it names - have to reach the
// same name; the spelling is therefore written from the type rather than from
// the terminals a use wrote.
std::string SemaAnalyzer::type_spelling(TypeId type) const
{
	std::string out;
	TypeId at = type;
	std::string suffix;
	for (;;)
	{
		const unsigned cv = types_.cv(at);
		const TypeKind kind = types_.kind(at);
		if (kind == TypeKind::Pointer || kind == TypeKind::LValueReference ||
		    kind == TypeKind::RValueReference)
		{
			std::string mark = kind == TypeKind::Pointer
				? "*"
				: (kind == TypeKind::LValueReference ? "&" : "&&");
			if ((cv & kCvConst) != 0)
			{
				mark += "const";
			}
			if ((cv & kCvVolatile) != 0)
			{
				mark += "volatile";
			}
			suffix = mark + suffix;
			at = types_.target(at);
			continue;
		}
		if (kind == TypeKind::Array)
		{
			suffix = (types_.bounded(at)
				          ? "[" + decimal_text(types_.bound(at)) + "]"
				          : std::string("[]")) + suffix;
			at = types_.target(at);
			continue;
		}
		if ((cv & kCvConst) != 0)
		{
			out += "const ";
		}
		if ((cv & kCvVolatile) != 0)
		{
			out += "volatile ";
		}
		switch (kind)
		{
		case TypeKind::Class:
		case TypeKind::Enum:
		case TypeKind::TemplateParameter:
		// 14.3.3p1: an argument at a template place is spelled as the template
		// it named, which is the whole of what the place took.
		case TypeKind::TemplateName:
			out += types_.user_qualified_name(at);
			break;

		case TypeKind::Value:
		{
			// 14.3.2p1: an argument at a value place is spelled as what it is
			// worth, which is what a specialization's own name is built from -
			// so `Box<3>` is one name however the 3 was written.
			//
			// What it is worth includes the type it was converted to wherever
			// the digits alone would not say which value it is: 7.2p9 leaves an
			// enumeration's value no spelling of its own, so it is written as
			// 5.2.9p10's cast to the enumeration, and 2.14.6p1 gives `bool` two
			// literals of its own rather than the 0 and 1 it converts to.
			const TypeId of = types_.target(at);
			const unsigned long long bits = types_.value_bits(at);
			if (types_.kind(of) == TypeKind::Enum)
			{
				out += "(" + types_.user_qualified_name(of) + ")";
			}
			else if (types_.kind(of) == TypeKind::Fundamental &&
			         types_.fundamental_type(of) == FT_BOOL)
			{
				out += bits != 0 ? "true" : "false";
				break;
			}
			out += spell_value(of, bits);
			break;
		}

		case TypeKind::Function:
		{
			out += type_spelling(types_.target(at)) + "(";
			const std::vector<TypeId>& given = types_.parameters(at);
			for (std::size_t index = 0; index < given.size(); ++index)
			{
				if (index != 0)
				{
					out += ",";
				}
				out += type_spelling(given[index]);
			}
			if (types_.variadic(at))
			{
				out += given.empty() ? "..." : ",...";
			}
			out += ")";
			break;
		}

		case TypeKind::MemberPointer:
			out += type_spelling(types_.target(at)) + " " +
				type_spelling(types_.member_class(at)) + "::";
			suffix = "*" + suffix;
			break;

		default:
			out += fundamental_type_name(types_.fundamental_type(at));
			break;
		}
		break;
	}
	return out + suffix;
}

// 14.3p1: the arguments a template-argument-list binds to the parameters of
// `primary`, with 14.1p9's defaults filling in the ones the list stopped short
// of.  A default is read in a region that already binds the parameters before
// it, because 14.1p9 lets it name them.
void TemplateHead::bind_arguments(
	SemaEntity& primary, const std::vector<std::string>& written,
	const SemaContext& ctx, std::vector<TypeId>& out)
{
	TemplateInfo& info = *primary.templated;
	if (!info.supported)
	{
		throw std::runtime_error(primary.name + " is a template whose "
		                         "parameters PA20 does not instantiate");
	}
	// 14.5.3p1: a pack takes every argument the places before it did not, so
	// what bounds the list is the places up to the pack rather than all of them.
	const std::size_t places = pack_place(info);
	if (written.size() > places && places == info.parameters.size())
	{
		throw std::runtime_error("a template-argument-list gives " +
		                         primary.name + " more arguments than it has "
		                         "parameters");
	}
	// 14.1p4: what each place *is* - a type or a value of a written type - is
	// settled once, by the region the head opened, and every argument list read
	// after that substitutes its own bindings into what it found.
	open_region(info);
	out.reserve(info.parameters.size());
	for (std::size_t index = 0; index < written.size(); ++index)
	{
		std::string pattern;
		if (!written_pack_expansion(written[index], pattern))
		{
			out.push_back(bound_argument(info, out.size(), written[index], out,
			                             ctx));
			continue;
		}
		// 14.5.3p4: the expansion is not one argument but the run its packs
		// are bound to, which the places it lands on are the places of - a run
		// of two given to `select<A, B>` fills both.
		const std::size_t at = out.size() < places ? out.size() : places;
		PackReading(analyzer_).expand(
			pattern, ctx,
			at < info.parameters.size() && info.parameters[at].value
				? place_type(info, at, out)
				: kNoType,
			out);
	}
	if (out.size() >= places && places < info.parameters.size())
	{
		// 14.5.3p1: a pack the list stopped short of is bound to no arguments
		// at all, which is a run of none and not a missing argument.
		return;
	}
	if (out.size() == info.parameters.size())
	{
		return;
	}
	// The defaults of one list of explicit arguments are one answer however
	// many times the template is named that way, so the region they are read
	// in is opened once and the answer is kept.
	const std::uint64_t key =
		(static_cast<std::uint64_t>(primary.id) << 32) | analyzer_.types_.type_list(out);
	const std::unordered_map<std::uint64_t, std::vector<TypeId> >::const_iterator
		held = analyzer_.default_arguments_.find(key);
	if (held != analyzer_.default_arguments_.end())
	{
		out = held->second;
		return;
	}
	for (std::size_t index = out.size(); index < info.parameters.size(); ++index)
	{
		if (index == places)
		{
			// 14.5.3p1 again, reached by the defaults rather than by the list:
			// 14.1p11 lets a place written before the pack carry 14.1p9's own
			// argument, so filling those is what brings the reading here, and
			// the pack is bound to no arguments at all just as it is where the
			// list itself stopped short of it.
			break;
		}
		const TemplateInfo::Default& fill = info.defaults[index];
		if (fill.written == nullptr)
		{
			throw std::runtime_error("a template-argument-list gives " +
			                         primary.name + " too few arguments");
		}
		// 14.1p9 and 14.1p2: the default may name the places written before it,
		// under the names the head that wrote it gave them - which are not the
		// names the declaration this merge left standing spells those places
		// by.  So the region it is read in is its own head's, binding each
		// earlier place to what the list wrote there or an earlier default
		// already filled.
		SemaContext inner;
		inner.scope = &analyzer_.model_.open(ScopeKind::TemplateParameters, *info.region,
		                           nullptr, info.dump);
		inner.dump = info.dump;
		inner.node = nullptr;
		for (std::size_t before = 0;
		     before < index && before < fill.spelled.size(); ++before)
		{
			if (fill.spelled[before].empty())
			{
				// 14.1p3: a place its head left unnamed is one no default can
				// have written.
				continue;
			}
			bind(*inner.scope, fill.spelled[before], out[before],
			              SemaKind::Typedef);
		}
		if (info.parameters[index].templated)
		{
			out.push_back(place_default(*fill.written,
			                            info.parameters[index].head, inner));
			continue;
		}
		if (!info.parameters[index].value)
		{
			out.push_back(analyzer_.type_id_type(*fill.written, inner));
			continue;
		}
		// 14.1p9 at a value place: the default is an expression, read against
		// the same region and converted to the type this place declared.
		const TypeId place = place_type(info, index, out);
		bool dependent = analyzer_.types_.is_dependent(place);
		SemaConstant value;
		if (!dependent)
		{
			value = analyzer_.evaluate(*fill.written, inner);
		}
		out.push_back(dependent
			              ? analyzer_.dependent_value(primary.name + "#" +
			                                std::to_string(index))
			              : analyzer_.types_.value_type(place,
			                                  analyzer_.convert(value, place).bits));
	}
	analyzer_.default_arguments_.insert(std::make_pair(key, out));
}

// 14.1p4: the type the place at `index` declares, with the arguments the places
// before it took substituted into it - which is what `template<class T, T v>`
// needs and what leaves every other head's answer the type it already had.
TypeId TemplateHead::place_type(const TemplateInfo& info, std::size_t index,
                                const std::vector<TypeId>& before)
{
	const TypeId written = info.parameters[index].type;
	if (written == kNoType || !analyzer_.types_.is_dependent(written))
	{
		return written;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t at = 0; at < index && at < before.size(); ++at)
	{
		bindings.insert(
			std::make_pair(info.parameters[at].self, before[at]));
	}
	std::unordered_map<TypeId, TypeId> memo;
	return analyzer_.substituted(written, bindings, memo);
}

// 14.1p4 at the function tier, where each place is a declaration of its own
// rather than an entry of a `TemplateInfo`: the same type, over the same
// arguments, keyed by the type each place stands for.  A type place answers
// `kNoType`, which is what says the argument written there is 8.1p1's type-id.
TypeId TemplateHead::place_type(const std::vector<SemaEntity*>& places,
                                std::size_t index,
                                const std::vector<TypeId>& before)
{
	if (index >= places.size())
	{
		return kNoType;
	}
	const TypeId written = analyzer_.types_.parameter_value_type(places[index]->type);
	if (written == kNoType || !analyzer_.types_.is_dependent(written))
	{
		return written;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t at = 0; at < index && at < before.size(); ++at)
	{
		bindings.insert(std::make_pair(places[at]->type, before[at]));
	}
	std::unordered_map<TypeId, TypeId> memo;
	return analyzer_.substituted(written, bindings, memo);
}

// 14.3p1: the argument the list wrote at `index`, read as the place says.
TypeId TemplateHead::bound_argument(const TemplateInfo& info, std::size_t index,
                                    const std::string& written,
                                    const std::vector<TypeId>& before,
                                    const SemaContext& ctx)
{
	// 14.5.3p1: every argument past the places before the pack is an argument
	// of the pack, so they all read as the one place it declared.
	const std::size_t places = pack_place(info);
	const std::size_t at = index < places ? index : places;
	if (at < info.parameters.size() && info.parameters[at].templated)
	{
		// 14.3.3p1: a template place takes a template-name, and the head that
		// place wrote is what says which templates fit it.
		return template_argument(written, info.parameters[at].head, ctx);
	}
	if (at >= info.parameters.size() || !info.parameters[at].value)
	{
		return analyzer_.template_argument_type(written, ctx);
	}
	return analyzer_.template_argument_value(written, place_type(info, at, before), ctx);
}

// 14.1p11 and 14.5.3p1: the place a pack was declared at, or the number of
// places where the head declared none - which is how many arguments a written
// list fills one for one before the run begins.
std::size_t pack_place(const TemplateInfo& info)
{
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		if (info.parameters[index].pack)
		{
			return index;
		}
	}
	return info.parameters.size();
}

Scope& TemplateHead::open_bindings(const TemplateInfo& info,
                                   const std::vector<TypeId>& arguments)
{
	Scope& bindings = analyzer_.model_.open(ScopeKind::TemplateParameters, *info.region,
	                              nullptr, info.dump);
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		// 14.5.3p1: a pack's name stands for the whole run the list left it,
		// which is one entry of the type table and not one binding per element
		// - so `sizeof...` and every expansion of it read the same fact.
		const TypeId took =
			place_argument(analyzer_.types_, arguments, index, info.parameters.size(),
			               info.parameters[index].pack);
		if (took == kNoType || info.parameters[index].name.empty())
		{
			continue;
		}
		bind(bindings, info.parameters[index].name, took,
		              SemaKind::Typedef);
	}
	return bindings;
}

// 14.3p1: one place of a template bound to the argument its list gave it.
//
// A type argument is a typedef-name of the type - or, where 14.6.1p6 forbids a
// declaration of the parameter's name, a parameter standing for it.  A value
// argument is not a type at all: it is the constant 5.19 reads wherever the
// place's name is written, so the declaration it binds carries the value and
// the type the argument was converted to.
SemaEntity& TemplateHead::bind(Scope& region, const std::string& name,
                               TypeId argument, SemaKind kind)
{
	if (analyzer_.types_.is_pack_expansion(argument))
	{
		// 14.6.1p1: the current instantiation names a pack place by the
		// expansion `Ts...`, and what a definition read against it binds is the
		// place itself - the run is what an argument list settles, and until
		// then the name stands for the pack the head declared.
		argument = analyzer_.types_.target(argument);
	}
	SemaEntity* const templated = named_template(argument);
	if (templated != nullptr)
	{
		// 14.3.3p1: the argument *is* a template, so the place's name is a
		// second name for the declaration it named.  3.3p1 lets several names
		// be bound to one entity, and binding it here is what makes every
		// `C<A…>` written in the pattern the ordinary template-id path rather
		// than a second reading of it.
		analyzer_.model_.bind(region, name, *templated);
		return *templated;
	}
	if (!analyzer_.types_.is_value(argument))
	{
		// 14.1p4 and 14.6.1p1: a place that binds a value is bound as one even
		// where the argument is the place standing for itself, which is what
		// the current instantiation puts at a non-type place - otherwise a
		// definition read against it finds a type where its own head wrote a
		// value, and 5.1.1p8 refuses every use of the name.
		SemaEntity& bound = analyzer_.model_.create(
			analyzer_.types_.parameter_value_type(argument) != kNoType
				? SemaKind::TemplateValue : kind,
			name, argument);
		analyzer_.model_.bind(region, bound.name, bound);
		analyzer_.model_.declare_in(region, bound);
		return bound;
	}
	SemaEntity& bound = analyzer_.model_.create(SemaKind::TemplateValue, name,
	                                  analyzer_.types_.target(argument));
	bound.constant = true;
	bound.value = analyzer_.types_.value_bits(argument);
	analyzer_.model_.bind(region, bound.name, bound);
	analyzer_.model_.declare_in(region, bound);
	return bound;
}

// 14.5.1.3p1 and 14.1p2: the region one out-of-class member definition of a
// class template is read in.
//
// Each declaration of one template spells its parameters as it likes, and what
// two heads share is the *places* the argument list is in the order of - so the
// names this head wrote stand in a region of this definition's own, opened
// between the class and the region the class was completed against.  A name the
// head wrote then reaches the argument its own place took, whatever the class's
// head called that place, and nothing this definition binds is standing when
// the next one is read.
//
// Null where the head declares a different number of parameters than the class
// takes arguments: that is 14.5.5's partial specialization, which this
// milestone leaves out, and not a definition of a member of this template.
Scope* TemplateHead::open_member_parameters(
	Scope& enclosing, const AstNode& clause,
	const std::vector<TypeId>& arguments, SemaKind kind, DumpScope* dump)
{
	TemplateInfo head;
	read(clause, head);
	// 14.5.3p1: a head whose last place is a pack takes every argument past the
	// places before it, so what has to match is those places and not the count.
	const std::size_t places = pack_place(head);
	const bool packed = places < head.parameters.size();
	if (packed ? arguments.size() < places
	           : head.parameters.size() != arguments.size())
	{
		return nullptr;
	}
	Scope& region = analyzer_.model_.open(ScopeKind::TemplateParameters, enclosing,
	                            nullptr, dump);
	for (std::size_t index = 0; index < places; ++index)
	{
		if (head.parameters[index].name.empty())
		{
			// 14.1p3: a parameter with no identifier binds nothing, and still
			// stands for an argument.
			continue;
		}
		bind(region, head.parameters[index].name, arguments[index],
		              kind);
	}
	if (packed && !head.parameters[places].name.empty())
	{
		bind(region, head.parameters[places].name,
		              bound_run(analyzer_.types_, arguments, places), kind);
	}
	return &region;
}

// 14.6.1p1 and 14.1p4: the region binding each place of `info` to something
// standing for itself, which is where the head's own names are looked up from.
//
// It is opened once, by the first reading that needs it, and it is what settles
// what a place *is*: a type place stands for a type, and a value place stands
// for a value of the type its own decl-specifier-seq and declarator write - a
// type-id read in this region, so `template<class T, T v>` reaches the place
// before it.  Every later reading of an argument list substitutes into that
// type rather than reading the syntax again.
void TemplateHead::open_region(TemplateInfo& info)
{
	if (info.parameter_region != nullptr)
	{
		return;
	}
	info.reading_dump = &analyzer_.model_.detached_dump();
	Scope& region = analyzer_.model_.open(ScopeKind::TemplateParameters, *info.region,
	                            nullptr, info.reading_dump);
	info.parameter_region = &region;
	SemaContext inner;
	inner.scope = &region;
	inner.dump = info.reading_dump;
	inner.node = nullptr;
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		TemplateInfo::Parameter& place = info.parameters[index];
		// 14.1p2 and the ABI's `<template-param>`: a parameter stands for the
		// place its head declared it in, which is what a name encoded from the
		// current instantiation would be written by.
		place.self = analyzer_.types_.template_parameter_type(
			analyzer_.model_.type_entity_id(), place.templated,
			place.name.empty() ? "#" + std::to_string(index) : place.name);
		analyzer_.types_.set_template_index(place.self, static_cast<unsigned>(index));
		if (place.templated && place.head != nullptr)
		{
			// 14.1p2: what the place binds is a template, so the head it wrote
			// travels with the type it stands for - `C<A…>` written inside the
			// pattern reads it without the head that declared `C`.
			record_place(place.self, *place.head, region);
		}
		if (place.pack)
		{
			// 14.5.3p1: what the place stands for is a run, which is what makes
			// a name written for it one an expansion has to settle.
			analyzer_.types_.set_template_pack(place.self, true);
		}
		if (place.value)
		{
			place.type = non_type_type(*place.written, inner);
			analyzer_.types_.set_parameter_value_type(place.self, place.type);
		}
		if (place.name.empty())
		{
			// 14.1p3: a parameter with no identifier binds nothing, and still
			// stands for an argument.
			continue;
		}
		SemaEntity& bound = analyzer_.model_.create(
			place.value ? SemaKind::TemplateValue : SemaKind::TemplateType,
			place.name, place.self);
		analyzer_.model_.bind(region, bound.name, bound);
		analyzer_.model_.declare_in(region, bound);
	}
}

// 14.1p2: the head a template place wrote, settled and recorded against the
// type that place stands for.
//
// The two tiers read a head in two shapes - a class template's places are
// entries of one `TemplateInfo` and a function template's are declarations of
// their own - and 14.3.3p1 is one question either way, asked of the type the
// place stands for.  So the recording is one reading: the head's own places
// belong to it alone and are settled in a region of their own inside the one
// the place was declared in, which is what lets 14.3.3p1 compare the type a
// value place of it names a value of.
void TemplateHead::record_place(TypeId place, TemplateInfo& head,
                                Scope& enclosing)
{
	analyzer_.place_heads_.insert(std::make_pair(place, &head));
	if (head.region == nullptr)
	{
		head.region = &enclosing;
	}
	open_region(head);
}

// 14.1p2: the definition's own names for the places an earlier declaration
// already named, taken after a region has been opened over them.
void TemplateHead::rename_parameters(
	TemplateInfo& info, const std::vector<TemplateInfo::Parameter>& head)
{
	for (std::size_t index = 0;
	     index < info.parameters.size() && index < head.size(); ++index)
	{
		TemplateInfo::Parameter& place = info.parameters[index];
		const std::string named = place.name;
		place.name = head[index].name;
		place.value = head[index].value;
		place.written = head[index].written;
		if (info.parameter_region == nullptr || named == place.name ||
		    place.name.empty())
		{
			continue;
		}
		// The place is already bound under the earlier declaration's name; the
		// definition's body looks it up under its own, so the region answers to
		// both rather than being read a second time.
		SemaEntity* const bound =
			analyzer_.model_.find(*info.parameter_region, named, LookupKind::Any);
		if (bound != nullptr)
		{
			analyzer_.model_.bind(*info.parameter_region, place.name, *bound);
		}
	}
}
