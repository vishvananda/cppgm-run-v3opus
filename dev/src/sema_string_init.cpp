#include "sema_string_init.h"

#include <cstddef>
#include <stdexcept>
#include <string>

#include "ast_model.h"
#include "sema_analyzer.h"

// 8.5.2, the whole of it: one reading of a string literal against an array of
// character type, asked from the three places a program can write one - a
// declaration, a clause of a braced-init-list, and the bound 8.3.4p3 leaves for
// the literal to settle.

StringInitialization::StringInitialization(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 8.5.2p1: the array of unknown bound a string literal gives its own length -
// the code units it holds, the terminating one among them.  The array is left
// as it was where the initializer is no string literal of this array's own
// width, which the initialization then refuses where it stands.
TypeId StringInitialization::deduced_bound(TypeId array,
                                           const AstNode& written,
                                           const SemaContext& ctx)
{
	TypeTable& types = analyzer_.types_;
	const TypeId element = types.strip_cv(types.target(array));
	const unsigned long long width = types.object_size(element);
	if (written.kind != AstKind::Literal ||
	    !types.is_integral(element) || width == 0)
	{
		// Nothing is read for an initializer that is no literal at all: a
		// probe of one registers the temporaries and demands the expression
		// itself would, and this question is asked before the declaration is
		// even made.
		return array;
	}
	DumpNode scratch;
	const SemaAnalyzer::Value literal = analyzer_.probe_expression(written, ctx, scratch);
	if (types.kind(types.strip_cv(literal.type)) != TypeKind::Array ||
	    literal.node == nullptr || literal.node->fact.spelling.empty() ||
	    types.object_size(
		    types.strip_cv(types.target(types.strip_cv(literal.type)))) !=
	        width)
	{
		return array;
	}
	return types.array_of(types.target(array), true,
	                       literal.node->fact.spelling.size() / width);
}

bool StringInitialization::as_subobject(TypeId array,
                                        InitializerClauses& clauses,
                                        const SemaContext& ctx,
                                        DumpNode& parent)
{
	TypeTable& types = analyzer_.types_;
	std::vector<unsigned long long> units;
	if (clauses.spent() ||
	    !units_of(array, clauses.next(), clauses.in(ctx), units))
	{
		return false;
	}
	// 8.5.2p1 as a subobject of an enclosing object: each element is a
	// subobject of its own, and the ones past the literal hold what any other
	// unreached element holds.
	const TypeId element = types.strip_cv(types.target(array));
	const unsigned long long bound =
		types.bounded(array) ? types.bound(array) : 0;
	for (unsigned long long index = 0; index < bound; ++index)
	{
		DumpNode& node =
			analyzer_.open_subobject(parent, element, nullptr, index);
		if (index < units.size())
		{
			write_unit(element, units[index], node);
		}
	}
	++clauses.at;
	return true;
}

// 8.5.2p1 as an object of its own: the elements stand under the one list every
// other array initialization writes, which is what the lowering reads the
// storage of a declared array out of.
bool StringInitialization::as_object(TypeId array, const AstNode& written,
                                     const SemaContext& ctx, DumpNode& line)
{
	std::vector<unsigned long long> units;
	std::string literal;
	if (!units_of(array, written, ctx, units, &literal))
	{
		return false;
	}
	DumpNode& list = analyzer_.model_.open_node(line, std::string());
	list.text = analyzer_.spell("braced-init-list", ValueCategory::LValue,
	                            array, std::string());
	list.fact.kind = FactKind::BracedInitList;
	list.fact.type = array;
	list.fact.spelled = array;
	list.fact.category = ValueCategory::LValue;
	// 2.14.5p8: the literal the units were copied out of is an object of its
	// own with static storage duration, and the copy leaves nothing else
	// naming it - so the list carries which literal it was read from, and the
	// places the image lays that object out beside the array ask this.  The
	// spelling is a fact of the list rather than of its elements, which hold
	// values and no longer say where they came from.
	list.fact.spelling = literal;
	write_units(array, units, list);
	return true;
}

// 8.5.2p1 where the list is already open: 8.5.4p1's `{"ab"}` initializes the
// array with the literal's own code units exactly as `"ab"` does, and the
// braces the program wrote are the list they stand under - so the elements go
// under that node and no second one is opened around them.
bool StringInitialization::units_into(TypeId array, const AstNode& written,
                                      const SemaContext& ctx, DumpNode& list)
{
	std::vector<unsigned long long> units;
	std::string literal;
	if (!units_of(array, written, ctx, units, &literal))
	{
		return false;
	}
	list.fact.spelling = literal;
	write_units(array, units, list);
	return true;
}

void StringInitialization::write_units(
	TypeId array, const std::vector<unsigned long long>& units, DumpNode& list)
{
	const TypeId element =
		analyzer_.types_.strip_cv(analyzer_.types_.target(array));
	for (std::size_t index = 0; index < units.size(); ++index)
	{
		write_unit(element, units[index], list);
	}
}

// 5.19 and 8.5.2p1: the one element a code unit of the literal initializes.
void StringInitialization::write_unit(TypeId element,
                                      unsigned long long bits, DumpNode& parent)
{
	SemaAnalyzer::Value unit;
	unit.type = element;
	unit.spelled = element;
	unit.category = ValueCategory::PRValue;
	unit.constant = true;
	unit.value = bits;
	unit.what = "literal";
	unit.payload = analyzer_.spell_value(element, bits);
	unit.node = &analyzer_.model_.open_node(
		parent, analyzer_.spell(unit.what, unit.category, unit.type,
		                        unit.payload));
	analyzer_.record(unit);
}

// 8.5.2p1: the code units a string literal initializes an array of character
// type with, or false where the initializer is no string literal whose own code
// units are as wide as this array's elements - which is every other initializer
// and every array of some other type, and is read as the expression it is.
bool StringInitialization::units_of(TypeId array, const AstNode& written,
                                    const SemaContext& ctx,
                                    std::vector<unsigned long long>& units,
                                    std::string* object)
{
	TypeTable& types = analyzer_.types_;
	if (written.kind != AstKind::Literal ||
	    types.kind(types.strip_cv(array)) != TypeKind::Array)
	{
		return false;
	}
	const TypeId element = types.strip_cv(types.target(array));
	const unsigned long long width = types.object_size(element);
	if (!types.is_integral(element) || width == 0)
	{
		return false;
	}
	DumpNode scratch;
	const SemaAnalyzer::Value literal = analyzer_.probe_expression(written, ctx, scratch);
	if (types.kind(types.strip_cv(literal.type)) != TypeKind::Array ||
	    literal.node == nullptr || literal.node->fact.spelling.empty() ||
	    types.object_size(
		    types.strip_cv(types.target(types.strip_cv(literal.type)))) !=
	        width)
	{
		return false;
	}
	const std::string& data = literal.node->fact.spelling;
	const unsigned long long bound =
		types.bounded(array) ? types.bound(array) : 0;
	if (data.size() / width > bound)
	{
		throw std::runtime_error("a string literal initializes an array that is "
		                         "too short to hold it");
	}
	if (object != nullptr)
	{
		*object = data;
	}
	for (std::size_t index = 0; index + width <= data.size(); index += width)
	{
		unsigned long long bits = 0;
		for (unsigned long long byte = 0; byte < width; ++byte)
		{
			bits |= static_cast<unsigned long long>(
				static_cast<unsigned char>(data[index + byte]))
				<< (8 * byte);
		}
		units.push_back(bits);
	}
	return true;
}
