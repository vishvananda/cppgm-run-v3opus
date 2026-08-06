#include "sema_analyzer.h"

#include <stdexcept>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"

// Calls, the PA12 standard-conversion subset, and overload resolution.
//
// The three belong together: 13.3.3.1 ranks an argument against a parameter,
// 13.3.3 picks the candidate no other beats, and 5.2.2 is what asks.  Keeping
// them in one place keeps one answer to "does this argument go here", which the
// four contexts that initialise an object - a variable, a condition, a return
// statement and an argument - all ask in the same words.
//
// Candidates are the declaration chain of one name, so collecting them is a
// walk of what has been declared rather than a search, and resolution costs one
// pass per candidate over the arguments already analysed.

namespace
{

// The ranks of 13.3.3.1.1 Table 12.
const int kExactMatch = 0;
const int kPromotion = 1;
const int kConversion = 2;
const int kEllipsis = 3;

const AstNode* child_kind(const AstNode& node, AstKind kind)
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

const AstNode* arguments_of(const AstNode& node)
{
	const AstNode* list = child_kind(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_kind(node, AstKind::ParenArgumentList);
}

}

// 4.4p4: a pointer to `cv1 T` converts to a pointer to `cv2 T` when every level
// of `cv2` holds every qualifier of the matching level of `cv1`, and `const`
// appears at every shallower level of `cv2` above the first that differs.
bool SemaAnalyzer::qualification_convertible(TypeId from, TypeId to)
{
	TypeId source = from;
	TypeId target = to;
	// Whether every level above this one is const in the target, which is what
	// 4.4p4's second condition asks about a level whose qualifiers differ.
	bool above_all_const = true;
	for (;;)
	{
		const bool descend = types_.kind(source) == TypeKind::Pointer &&
			types_.kind(target) == TypeKind::Pointer;
		if (!descend)
		{
			return types_.strip_cv(source) == types_.strip_cv(target) &&
				(types_.cv(source) & ~types_.cv(target)) == 0 &&
				(types_.cv(source) == types_.cv(target) || above_all_const);
		}
		source = types_.target(source);
		target = types_.target(target);
		if ((types_.cv(source) & ~types_.cv(target)) != 0)
		{
			return false;
		}
		if (types_.cv(source) != types_.cv(target) && !above_all_const)
		{
			return false;
		}
		above_all_const = above_all_const && (types_.cv(target) & kCvConst) != 0;
	}
}

// 4.10 and 4.4: whether a prvalue of pointer type `from` converts to `to`.
bool SemaAnalyzer::pointer_convertible(TypeId from, TypeId to, int& rank,
                                       bool& exact)
{
	if (from == to)
	{
		rank = kExactMatch;
		exact = true;
		return true;
	}
	if (types_.kind(from) != TypeKind::Pointer ||
	    types_.kind(to) != TypeKind::Pointer)
	{
		return false;
	}
	if (qualification_convertible(from, to))
	{
		rank = kExactMatch;
		exact = true;
		return true;
	}
	// 4.10p2: a pointer to a cv-qualified object type converts to a pointer to
	// the same cv-qualified `void`.
	const TypeId source = types_.target(from);
	const TypeId target = types_.target(to);
	if (!types_.is_void(target) || types_.kind(source) == TypeKind::Function)
	{
		return false;
	}
	if ((types_.cv(source) & ~types_.cv(target)) != 0)
	{
		return false;
	}
	rank = kConversion;
	exact = false;
	return true;
}

SemaAnalyzer::Match SemaAnalyzer::match_by_value(const Value& argument,
                                                 TypeId parameter)
{
	Match match;
	const TypeId target = types_.strip_cv(parameter);
	const TypeId source = decayed(argument);
	if (source == target)
	{
		match.viable = true;
		match.rank = kExactMatch;
		return match;
	}
	// 4.10p1: a null pointer constant converts to any pointer type and to
	// `std::nullptr_t`.
	const bool source_null = argument.null_constant ||
		types_.fundamental_type(source) == FT_NULLPTR_T;
	if (source_null && (types_.kind(target) == TypeKind::Pointer ||
	                    types_.fundamental_type(target) == FT_NULLPTR_T))
	{
		match.viable = true;
		match.rank = kConversion;
		return match;
	}
	if (types_.kind(source) == TypeKind::Pointer)
	{
		if (types_.fundamental_type(target) == FT_BOOL)
		{
			// 4.12: a pointer converts to bool, which 13.3.3.2p4 ranks below a
			// conversion that keeps the pointer.
			match.viable = true;
			match.rank = kConversion;
			match.to_bool = true;
			return match;
		}
		int rank = kConversion;
		bool exact = false;
		if (pointer_convertible(source, target, rank, exact))
		{
			match.viable = true;
			match.rank = rank;
			return match;
		}
		return match;
	}
	if (!types_.is_arithmetic(source) && types_.kind(source) != TypeKind::Enum)
	{
		return match;
	}
	if (types_.fundamental_type(target) == FT_BOOL)
	{
		match.viable = true;
		match.rank = kConversion;
		return match;
	}
	if (!types_.is_arithmetic(target))
	{
		return match;
	}
	// 4.13 and 13.3.3.1.1: a promotion is a better conversion than any of the
	// conversions that change the value.
	if (promoted(source) == target && !types_.is_scoped_enum(source))
	{
		match.viable = true;
		match.rank = kPromotion;
		return match;
	}
	if (types_.is_scoped_enum(source))
	{
		// 7.2p9: a scoped enumeration has no implicit conversion to an
		// integral type.
		return match;
	}
	match.viable = true;
	match.rank = kConversion;
	return match;
}

// 3.9.3p5 and 8.3.4p1: the type with every top level qualifier removed, which
// for an array is the array of unqualified elements, because an array is as
// cv-qualified as its elements are.
TypeId SemaAnalyzer::bare_type(TypeId type)
{
	if (types_.kind(type) != TypeKind::Array)
	{
		return types_.strip_cv(type);
	}
	return types_.array_of(bare_type(types_.target(type)), types_.bounded(type),
	                       types_.bound(type));
}

// 8.5.3 and 13.3.3.1.4: binding a reference parameter to an argument.
SemaAnalyzer::Match SemaAnalyzer::match_reference(const Value& argument,
                                                 TypeId parameter)
{
	Match match;
	match.reference = true;
	const bool rvalue_ref = types_.kind(parameter) == TypeKind::RValueReference;
	const TypeId referenced = types_.target(parameter);
	const TypeId source = argument.type;
	const bool is_lvalue = argument.category == ValueCategory::LValue;

	// 8.5.3p4: reference-compatible is reference-related plus a referenced type
	// at least as cv-qualified, and a compatible reference binds what it was
	// given rather than a conversion of it.
	const bool related = bare_type(source) == bare_type(referenced);
	const bool const_lvalue_ref = (types_.cv(referenced) & kCvConst) != 0 &&
		(types_.cv(referenced) & kCvVolatile) == 0;
	if (related &&
	    (types_.object_cv(source) & ~types_.object_cv(referenced)) == 0)
	{
		// 8.5.3p5: an rvalue reference binds no lvalue directly, and an lvalue
		// reference binds an rvalue only through a non-volatile const.
		if (rvalue_ref ? is_lvalue : (!is_lvalue && !const_lvalue_ref))
		{
			return match;
		}
		match.viable = true;
		match.rank = kExactMatch;
		match.binds_lvalue = is_lvalue;
		match.binds_rvalue_ref = rvalue_ref;
		return match;
	}
	if (related)
	{
		// 8.5.3p5: the temporary a reference-related initializer would be
		// converted into has to be at least as cv-qualified, so dropping a
		// qualifier binds nothing.
		return match;
	}
	if (!rvalue_ref && !const_lvalue_ref)
	{
		return match;
	}

	// The argument is converted to the referenced type and the reference binds
	// the temporary that holds it.
	const Match converted = match_by_value(argument, referenced);
	if (!converted.viable)
	{
		return match;
	}
	match = converted;
	match.reference = true;
	match.binds_lvalue = false;
	match.binds_rvalue_ref = rvalue_ref;
	// 4.4p4: a qualification conversion is between two pointers to the same
	// type, so the temporary 8.5.3p5 binds holds the value the argument already
	// had.  The output writes the conversions that change what an operand is,
	// and this one changes nothing an operand of it could tell apart.
	if (!(types_.kind(source) == TypeKind::Pointer &&
	      qualification_convertible(source, types_.strip_cv(referenced))))
	{
		match.materialized = referenced;
	}
	return match;
}

SemaAnalyzer::Match SemaAnalyzer::match_argument(const Value& argument,
                                                 TypeId parameter)
{
	if (argument.type == kNoType && argument.functions != nullptr)
	{
		// 13.4p1: the target type chooses one of the declarations, and doing so
		// is an exact match.
		Match match;
		match.viable = resolve_target(argument, parameter) != nullptr;
		match.rank = kExactMatch;
		return match;
	}
	if (types_.is_reference(parameter))
	{
		return match_reference(argument, parameter);
	}
	return match_by_value(argument, parameter);
}

TypeId SemaAnalyzer::member_pointer_of(const SemaEntity& function)
{
	if (!function.object_member || function.region == nullptr ||
	    function.region->owner == nullptr)
	{
		return kNoType;
	}
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	const TypeId object = types_.target(parameters[0]);
	const std::vector<TypeId> written(parameters.begin() + 1, parameters.end());
	const TypeId declared = types_.qualified_function(
		types_.function_of(types_.target(function.type), written,
		                   types_.variadic(function.type)),
		types_.cv(object));
	return types_.member_pointer_to(types_.strip_cv(object), declared);
}

SemaEntity* SemaAnalyzer::resolve_target(const Value& value, TypeId target)
{
	TypeId wanted = target;
	if (types_.is_reference(wanted))
	{
		wanted = types_.target(wanted);
	}
	if (types_.kind(types_.strip_cv(wanted)) == TypeKind::MemberPointer)
	{
		// 13.4p1 lists a pointer to member among the targets that choose one
		// declaration, and the declaration it chooses is the member whose
		// pointer type is the one asked for.
		wanted = types_.strip_cv(wanted);
		for (SemaEntity* at = value.functions; at != nullptr; at = at->next)
		{
			if (member_pointer_of(*at) == wanted)
			{
				return at;
			}
		}
		return nullptr;
	}
	if (types_.kind(wanted) == TypeKind::Pointer)
	{
		wanted = types_.target(wanted);
	}
	wanted = types_.strip_cv(wanted);
	if (types_.kind(wanted) != TypeKind::Function)
	{
		return nullptr;
	}
	for (SemaEntity* at = value.functions; at != nullptr; at = at->next)
	{
		if (at->type == wanted)
		{
			return at;
		}
	}
	return nullptr;
}

// 13.3.3p1: one candidate is better than another when its conversion for every
// argument is at least as good and for one is better.
SemaEntity* SemaAnalyzer::select_overload(SemaEntity* candidates,
                                          const std::vector<Value>& arguments,
                                          const std::string& name)
{
	std::vector<SemaEntity*> viable;
	// 13.3.3p1: which of the viable candidates is a specialization of a
	// template, which is what tells two apart whose conversions tie.
	std::vector<char> templated;
	std::vector<Match> matches;
	for (SemaEntity* candidate = candidates; candidate != nullptr;
	     candidate = candidate->next)
	{
		SemaEntity* at = candidate;
		if (candidate->template_parameters != nullptr)
		{
			// 14.8.3p1: a template is a candidate through the specialization
			// the arguments deduce, and no candidate at all when they deduce
			// none.
			at = deduce_specialization(*candidate, arguments);
			if (at == nullptr)
			{
				continue;
			}
		}
		const std::vector<TypeId>& parameters = types_.parameters(at->type);
		if (arguments.size() < parameters.size() ||
		    (arguments.size() > parameters.size() && !types_.variadic(at->type)))
		{
			continue;
		}
		bool ok = true;
		const std::size_t base = matches.size();
		for (std::size_t index = 0; ok && index < arguments.size(); ++index)
		{
			Match match;
			if (index >= parameters.size())
			{
				// 13.3.3.1.3: an argument matched by the ellipsis converts with
				// the worst rank there is.
				match.viable = true;
				match.rank = kEllipsis;
			}
			else
			{
				match = match_argument(arguments[index], parameters[index]);
			}
			ok = match.viable;
			matches.push_back(match);
		}
		if (!ok)
		{
			matches.resize(base);
			continue;
		}
		viable.push_back(at);
		templated.push_back(at->primary != nullptr ? 1 : 0);
	}
	if (viable.empty())
	{
		throw std::runtime_error("no declaration of " + name +
		                         " accepts the arguments of a call");
	}

	std::size_t best = 0;
	const std::size_t count = arguments.size();
	// One row of `count` matches per viable candidate, which for a call with no
	// arguments is no row at all, so the rows are addressed from the buffer
	// rather than from an element of it.
	const Match* const rows = matches.data();
	for (std::size_t index = 1; index < viable.size(); ++index)
	{
		if (better_candidate(rows + index * count, rows + best * count, count,
		                     templated[index] == 0, templated[best] != 0))
		{
			best = index;
		}
	}
	for (std::size_t index = 0; index < viable.size(); ++index)
	{
		if (index != best &&
		    !better_candidate(rows + best * count, rows + index * count, count,
		                      templated[best] == 0, templated[index] != 0))
		{
			throw std::runtime_error("a call of " + name +
			                         " has no best declaration");
		}
	}
	return viable[best];
}

// 13.3.3.2: whether one argument's conversion is better than another's.
int SemaAnalyzer::compare_matches(const Match& left, const Match& right)
{
	if (left.rank != right.rank)
	{
		return left.rank < right.rank ? 1 : -1;
	}
	// 13.3.3.2p4: a conversion that keeps a pointer beats one to bool.
	if (left.to_bool != right.to_bool)
	{
		return left.to_bool ? -1 : 1;
	}
	if (!left.reference || !right.reference)
	{
		return 0;
	}
	// 13.3.3.2p3: an rvalue reference binding an rvalue beats an lvalue
	// reference binding it, and an lvalue reference binding an lvalue beats an
	// rvalue reference that could not.
	if (left.binds_rvalue_ref != right.binds_rvalue_ref &&
	    !left.binds_lvalue && !right.binds_lvalue)
	{
		return left.binds_rvalue_ref ? 1 : -1;
	}
	return 0;
}

bool SemaAnalyzer::better_candidate(const Match* left, const Match* right,
                                    std::size_t count, bool left_written,
                                    bool right_deduced)
{
	bool strictly_better = false;
	for (std::size_t index = 0; index < count; ++index)
	{
		const int order = compare_matches(left[index], right[index]);
		if (order < 0)
		{
			return false;
		}
		strictly_better = strictly_better || order > 0;
	}
	// 13.3.3p1: a function the program declared beats a specialization of a
	// template whose conversions are no better than its own.
	return strictly_better || (left_written && right_deduced);
}

// The one place a conversion is visible in the dump: a null pointer constant
// takes the pointer type it is used as, and a reference that binds a converted
// temporary writes the cast that made it.
void SemaAnalyzer::apply_conversion(Value& value, TypeId target,
                                    const Match& match)
{
	if (value.type == kNoType && value.functions != nullptr)
	{
		SemaEntity* chosen = resolve_target(value, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type it is used as");
		}
		name_function(value, *chosen, "id-expression");
		return;
	}
	if (match.materialized != kNoType && value.node != nullptr)
	{
		// The operand's line moves under the cast that converted it, in the
		// place the operand already had: an argument is written where the call
		// passes it however many of the arguments beside it convert.
		model_.wrap_node(*value.node,
		                 spell("cast-expression", ValueCategory::PRValue,
		                       match.materialized, nullptr));
		value.type = match.materialized;
		value.spelled = match.materialized;
		return;
	}
	const TypeId wanted = types_.is_reference(target)
		? types_.target(target)
		: types_.strip_cv(target);
	const bool wants_pointer = types_.kind(wanted) == TypeKind::Pointer ||
		(types_.kind(wanted) == TypeKind::Fundamental &&
		 types_.fundamental_type(wanted) == FT_NULLPTR_T);
	if (value.null_constant && wants_pointer && value.node != nullptr &&
	    value.type != wanted)
	{
		value.node->text = spell("literal", ValueCategory::PRValue, wanted,
		                         nullptr) + " 0";
		value.type = wanted;
		value.spelled = wanted;
	}
}

SemaAnalyzer::Value SemaAnalyzer::initialize(const AstNode& node, TypeId target,
                                             const Context& ctx,
                                             DumpNode& parent)
{
	Value value = expression(node, ctx, parent);
	const Match match = match_argument(value, target);
	if (!match.viable)
	{
		throw std::runtime_error("an expression has no conversion to the type "
		                         "it initialises");
	}
	apply_conversion(value, target, match);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::call_expression(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent)
{
	const AstNode& callee = *node.children[0];
	SemaEntity* named = nullptr;
	if (callee.kind == AstKind::DecltypeSpecifier)
	{
		// 5.2.3 and 7.1.6.2p4: a call written on a decltype-specifier is an
		// explicit type conversion to the type the specifier names.
		return functional_cast(node, ctx, parent, decltype_type(callee, ctx));
	}
	if (callee.kind == AstKind::IdExpression)
	{
		// 5.2.3: a call whose callee names a type is an explicit type
		// conversion, which the grammar cannot tell from a call.
		const TypeId keyword = keyword_type(callee.text);
		if (keyword != kNoType)
		{
			return functional_cast(node, ctx, parent, keyword);
		}
		// 14.2: a callee written as a template-id names the specializations its
		// argument list makes, which 13.3 then chooses among.
		named = template_specializations(callee.text, ctx);
		if (named == nullptr)
		{
			named = resolve(callee.text, ctx, LookupKind::Any);
			if (named != nullptr && names_a_type(*named))
			{
				return functional_cast(node, ctx, parent, named->type);
			}
			Value builtin;
			if (named == nullptr &&
			    builtin_call(callee.text, node, ctx, parent, builtin))
			{
				return builtin;
			}
		}
	}

	DumpNode& line = model_.open_node(parent, std::string());
	// 3.4: what the callee names was looked up above to learn that it is not a
	// type, so the expression layer is handed the answer rather than asking
	// for it again.
	Value target = callee.kind == AstKind::IdExpression
		? named_value(callee, require(named, callee.text), line)
		: expression(callee, ctx, line);

	const AstNode* list = arguments_of(node);
	std::vector<Value> arguments;
	for (std::size_t index = 0; list != nullptr && index < list->children.size();
	     ++index)
	{
		arguments.push_back(expression(*list->children[index], ctx, line));
	}

	TypeId function = kNoType;
	if (target.functions != nullptr && target.addressed == nullptr)
	{
		// 13.3: the arguments choose one declaration, which the callee line is
		// then written from.
		SemaEntity& chosen =
			*select_overload(target.functions, arguments, callee.text);
		name_function(target, chosen, "callee");
		function = target.type;
	}
	else if (types_.kind(target.type) == TypeKind::Function)
	{
		function = target.type;
	}
	else
	{
		// 5.2.2p1: a call through a pointer to function calls what it points to.
		// 13.4p1 gives a call no target type of its own, so an overloaded name
		// that reached here through `&` is a callee that means nothing.
		require_complete_value(target);
		const TypeId pointer = decayed(target);
		if (types_.kind(pointer) != TypeKind::Pointer ||
		    types_.kind(types_.target(pointer)) != TypeKind::Function)
		{
			throw std::runtime_error("the callee of a call is neither a function "
			                         "nor a pointer to one");
		}
		function = types_.target(pointer);
	}

	const std::vector<TypeId>& parameters = types_.parameters(function);
	if (arguments.size() < parameters.size() ||
	    (arguments.size() > parameters.size() && !types_.variadic(function)))
	{
		throw std::runtime_error("a call passes the wrong number of arguments");
	}
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index >= parameters.size())
		{
			// 5.2.2p7: an argument matched by the ellipsis is passed as it is.
			require_complete_value(arguments[index]);
			continue;
		}
		const Match match = match_argument(arguments[index], parameters[index]);
		if (!match.viable)
		{
			throw std::runtime_error("an argument has no conversion to the type "
			                         "of the parameter it is passed to");
		}
		apply_conversion(arguments[index], parameters[index], match);
	}

	// 5.2.2p10: the result is a prvalue unless the function returns a
	// reference, and the dump writes the return type as declared.
	const TypeId result = types_.target(function);
	Value value;
	value.spelled = result;
	value.type = types_.is_reference(result) ? types_.target(result) : result;
	value.category = ValueCategory::PRValue;
	if (types_.kind(result) == TypeKind::LValueReference)
	{
		value.category = ValueCategory::LValue;
	}
	else if (types_.kind(result) == TypeKind::RValueReference)
	{
		value.category = ValueCategory::XValue;
	}
	value.node = &line;
	line.text = spell("call-expression", value.category, result, nullptr);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::functional_cast(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent,
                                                  TypeId target)
{
	const AstNode* list = arguments_of(node);
	const std::size_t count = list == nullptr ? 0 : list->children.size();
	Value value;
	value.type = target;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	if (count == 0)
	{
		// 5.2.3p2: `T()` is a prvalue of type T that is value-initialized,
		// which for the PA12 subset is the zero of that type.
		value.constant = true;
		value.node = &model_.open_node(
			parent, spell("literal", value.category, target, nullptr) + " 0");
		return value;
	}
	if (count != 1)
	{
		throw std::runtime_error("a functional cast is written with more than "
		                         "one operand");
	}
	DumpNode& line = model_.open_node(parent, std::string());
	Value source = expression(*list->children[0], ctx, line);
	if (source.type == kNoType && source.functions != nullptr)
	{
		SemaEntity* chosen = resolve_target(source, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type a cast asks for");
		}
		name_function(source, *chosen, "id-expression");
	}
	line.text = spell("cast-expression", value.category, target, nullptr);
	value.node = &line;
	return value;
}
