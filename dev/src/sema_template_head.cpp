#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast_model.h"
#include "sema_pack.h"
#include "sema_template.h"

// 14.1p2's template-parameter-clause and 14.3p1's template-argument-list.
//
// A template head declares *places* rather than declarations: what each place
// is - a type or a value of a written type - is settled once, in 14.6.1p1's own
// region, and every argument list read afterwards substitutes its own bindings
// into what was settled there rather than reading the head's syntax again.
// This file owns both halves of that: the head, and the reading of one written
// argument list against it.
//
// 14.2 writes an argument list inside a name, so an argument arrives as text.
// A type argument is turned back into what was written by `sema_type_id.cpp`
// and a value argument by `sema_value_expression.cpp`; what belongs here is
// which of the two a place asked for, and what the answer is bound as.

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
TypeId SemaAnalyzer::explicit_argument(const std::vector<SemaEntity*>& places,
                                       std::size_t index,
                                       const std::vector<TypeId>& before,
                                       const std::string& written,
                                       const Context& ctx)
{
	const TypeId place = place_type(places, index, before);
	if (place == kNoType)
	{
		return template_argument_type(written, ctx);
	}
	return template_argument_value(written, place, ctx);
}

// 14.1p4: the name a non-type parameter's declarator gave it, empty where it
// wrote none.
std::string SemaAnalyzer::non_type_parameter_name(const AstNode& parameter)
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

// 14.1p2: the parameters a template-parameter-clause declared, in the order it
// wrote them, and 14.1p9's default arguments beside them.  A parameter this
// milestone gives no meaning to leaves the head unsupported rather than
// refusing it here: 14p1 lets a program declare a template it never names, and
// the declaration says nothing about a type until an instantiation asks.
void SemaAnalyzer::read_template_head(const AstNode& clause, TemplateInfo& info)
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
			place.name = non_type_parameter_name(parameter);
		}
		else if (parameter.kind != AstKind::TypeParameter ||
		         first_child(parameter, AstKind::TemplateTemplateParameter) != nullptr)
		{
			// 14.1p1's template parameter belongs to a later milestone.
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
		}
		if (place.pack && index + 1 != list->children.size())
		{
			// 14.1p11: a pack in a primary template's head is the last place it
			// declares, because every argument after the ones the places before
			// it take belongs to the pack.  14.5.3's other arrangement -
			// deducing a pack from a call and taking the places after it from
			// the arguments - is a later milestone's.
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
TypeId SemaAnalyzer::non_type_parameter_type(const AstNode& parameter,
                                             const Context& ctx)
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
	Span span;
	span.begin = parameter.begin;
	span.end = parameter.end;
	const Specifiers specifiers =
		read_specifiers(*seq, ctx, span, true, std::string());
	TypeId type = specifier_type(specifiers);
	const AstNode* const declarator =
		first_child(parameter, AstKind::Declarator);
	if (declarator != nullptr)
	{
		std::string ignored;
		type = declarator_type(*declarator, type, ctx, &ignored);
	}
	// 14.1p4: the type shall be integral or an enumeration, or one of the forms
	// this milestone leaves out; a dependent one is whatever the argument makes
	// of it, so it is checked where the argument is bound.
	if (!types_.is_dependent(type) && integral_type(type) == kNoType)
	{
		throw std::runtime_error("a non-type template parameter of " +
		                         types_.description(type) + " is outside the "
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
void SemaAnalyzer::bind_template_arguments(
	SemaEntity& primary, const std::vector<std::string>& written,
	const Context& ctx, std::vector<TypeId>& out)
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
	open_parameter_region(info);
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
		PackReading(*this).expand(
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
		(static_cast<std::uint64_t>(primary.id) << 32) | types_.type_list(out);
	const std::unordered_map<std::uint64_t, std::vector<TypeId> >::const_iterator
		held = default_arguments_.find(key);
	if (held != default_arguments_.end())
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
		Context inner;
		inner.scope = &model_.open(ScopeKind::TemplateParameters, *info.region,
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
			bind_argument(*inner.scope, fill.spelled[before], out[before],
			              SemaKind::Typedef);
		}
		if (!info.parameters[index].value)
		{
			out.push_back(type_id_type(*fill.written, inner));
			continue;
		}
		// 14.1p9 at a value place: the default is an expression, read against
		// the same region and converted to the type this place declared.
		const TypeId place = place_type(info, index, out);
		bool dependent = types_.is_dependent(place);
		Constant value;
		if (!dependent)
		{
			value = evaluate(*fill.written, inner);
		}
		out.push_back(dependent
			              ? dependent_value(primary.name + "#" +
			                                std::to_string(index))
			              : types_.value_type(place,
			                                  convert(value, place).bits));
	}
	default_arguments_.insert(std::make_pair(key, out));
}

// 14.1p4: the type the place at `index` declares, with the arguments the places
// before it took substituted into it - which is what `template<class T, T v>`
// needs and what leaves every other head's answer the type it already had.
TypeId SemaAnalyzer::place_type(const TemplateInfo& info, std::size_t index,
                                const std::vector<TypeId>& before)
{
	const TypeId written = info.parameters[index].type;
	if (written == kNoType || !types_.is_dependent(written))
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
	return substituted(written, bindings, memo);
}

// 14.1p4 at the function tier, where each place is a declaration of its own
// rather than an entry of a `TemplateInfo`: the same type, over the same
// arguments, keyed by the type each place stands for.  A type place answers
// `kNoType`, which is what says the argument written there is 8.1p1's type-id.
TypeId SemaAnalyzer::place_type(const std::vector<SemaEntity*>& places,
                                std::size_t index,
                                const std::vector<TypeId>& before)
{
	if (index >= places.size())
	{
		return kNoType;
	}
	const TypeId written = types_.parameter_value_type(places[index]->type);
	if (written == kNoType || !types_.is_dependent(written))
	{
		return written;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t at = 0; at < index && at < before.size(); ++at)
	{
		bindings.insert(std::make_pair(places[at]->type, before[at]));
	}
	std::unordered_map<TypeId, TypeId> memo;
	return substituted(written, bindings, memo);
}

// 14.3p1: the argument the list wrote at `index`, read as the place says.
TypeId SemaAnalyzer::bound_argument(const TemplateInfo& info, std::size_t index,
                                    const std::string& written,
                                    const std::vector<TypeId>& before,
                                    const Context& ctx)
{
	// 14.5.3p1: every argument past the places before the pack is an argument
	// of the pack, so they all read as the one place it declared.
	const std::size_t places = pack_place(info);
	const std::size_t at = index < places ? index : places;
	if (at >= info.parameters.size() || !info.parameters[at].value)
	{
		return template_argument_type(written, ctx);
	}
	return template_argument_value(written, place_type(info, at, before), ctx);
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

Scope& SemaAnalyzer::open_template_bindings(const TemplateInfo& info,
                                            const std::vector<TypeId>& arguments)
{
	Scope& bindings = model_.open(ScopeKind::TemplateParameters, *info.region,
	                              nullptr, info.dump);
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		// 14.5.3p1: a pack's name stands for the whole run the list left it,
		// which is one entry of the type table and not one binding per element
		// - so `sizeof...` and every expansion of it read the same fact.
		const TypeId took =
			place_argument(types_, arguments, index, info.parameters.size(),
			               info.parameters[index].pack);
		if (took == kNoType || info.parameters[index].name.empty())
		{
			continue;
		}
		bind_argument(bindings, info.parameters[index].name, took,
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
SemaEntity& SemaAnalyzer::bind_argument(Scope& region, const std::string& name,
                                        TypeId argument, SemaKind kind)
{
	if (types_.is_pack_expansion(argument))
	{
		// 14.6.1p1: the current instantiation names a pack place by the
		// expansion `Ts...`, and what a definition read against it binds is the
		// place itself - the run is what an argument list settles, and until
		// then the name stands for the pack the head declared.
		argument = types_.target(argument);
	}
	if (!types_.is_value(argument))
	{
		// 14.1p4 and 14.6.1p1: a place that binds a value is bound as one even
		// where the argument is the place standing for itself, which is what
		// the current instantiation puts at a non-type place - otherwise a
		// definition read against it finds a type where its own head wrote a
		// value, and 5.1.1p8 refuses every use of the name.
		SemaEntity& bound = model_.create(
			types_.parameter_value_type(argument) != kNoType
				? SemaKind::TemplateValue : kind,
			name, argument);
		model_.bind(region, bound.name, bound);
		model_.declare_in(region, bound);
		return bound;
	}
	SemaEntity& bound = model_.create(SemaKind::TemplateValue, name,
	                                  types_.target(argument));
	bound.constant = true;
	bound.value = types_.value_bits(argument);
	model_.bind(region, bound.name, bound);
	model_.declare_in(region, bound);
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
Scope* SemaAnalyzer::open_member_parameters(
	Scope& enclosing, const AstNode& clause,
	const std::vector<TypeId>& arguments, SemaKind kind, DumpScope* dump)
{
	TemplateInfo head;
	read_template_head(clause, head);
	// 14.5.3p1: a head whose last place is a pack takes every argument past the
	// places before it, so what has to match is those places and not the count.
	const std::size_t places = pack_place(head);
	const bool packed = places < head.parameters.size();
	if (packed ? arguments.size() < places
	           : head.parameters.size() != arguments.size())
	{
		return nullptr;
	}
	Scope& region = model_.open(ScopeKind::TemplateParameters, enclosing,
	                            nullptr, dump);
	for (std::size_t index = 0; index < places; ++index)
	{
		if (head.parameters[index].name.empty())
		{
			// 14.1p3: a parameter with no identifier binds nothing, and still
			// stands for an argument.
			continue;
		}
		bind_argument(region, head.parameters[index].name, arguments[index],
		              kind);
	}
	if (packed && !head.parameters[places].name.empty())
	{
		bind_argument(region, head.parameters[places].name,
		              bound_run(types_, arguments, places), kind);
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
void SemaAnalyzer::open_parameter_region(TemplateInfo& info)
{
	if (info.parameter_region != nullptr)
	{
		return;
	}
	info.reading_dump = &model_.detached_dump();
	Scope& region = model_.open(ScopeKind::TemplateParameters, *info.region,
	                            nullptr, info.reading_dump);
	info.parameter_region = &region;
	Context inner;
	inner.scope = &region;
	inner.dump = info.reading_dump;
	inner.node = nullptr;
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		TemplateInfo::Parameter& place = info.parameters[index];
		// 14.1p2 and the ABI's `<template-param>`: a parameter stands for the
		// place its head declared it in, which is what a name encoded from the
		// current instantiation would be written by.
		place.self = types_.template_parameter_type(
			model_.type_entity_id(), false,
			place.name.empty() ? "#" + std::to_string(index) : place.name);
		types_.set_template_index(place.self, static_cast<unsigned>(index));
		if (place.pack)
		{
			// 14.5.3p1: what the place stands for is a run, which is what makes
			// a name written for it one an expansion has to settle.
			types_.set_template_pack(place.self, true);
		}
		if (place.value)
		{
			place.type = non_type_parameter_type(*place.written, inner);
			types_.set_parameter_value_type(place.self, place.type);
		}
		if (place.name.empty())
		{
			// 14.1p3: a parameter with no identifier binds nothing, and still
			// stands for an argument.
			continue;
		}
		SemaEntity& bound = model_.create(
			place.value ? SemaKind::TemplateValue : SemaKind::TemplateType,
			place.name, place.self);
		model_.bind(region, bound.name, bound);
		model_.declare_in(region, bound);
	}
}

// 14.1p2: the definition's own names for the places an earlier declaration
// already named, taken after a region has been opened over them.
void SemaAnalyzer::rename_template_parameters(
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
			model_.find(*info.parameter_region, named, LookupKind::Any);
		if (bound != nullptr)
		{
			model_.bind(*info.parameter_region, place.name, *bound);
		}
	}
}
