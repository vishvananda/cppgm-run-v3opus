#include "sema_operator.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_access.h"
#include "sema_analyzer.h"
#include "sema_argument_lookup.h"

// 13.5 and 3.4.2: the calls ordinary lookup did not name.
//
// An operator written on an operand of class or enumeration type is a call of
// an operator function (13.5p1), and the declaration it calls may be one no
// lookup written where the expression stands can reach: 3.4.2 adds the
// namespaces the argument types belong to, and 11.3p6 puts a friend
// declaration in one of them without binding its name there.  This file owns
// both halves - which declarations a use of a name reaches, and how an
// operator expression becomes the call it stands for - and writes the same
// `call-expression` node the call path already writes, so nothing new reaches
// the lowering.

OperatorCall::OperatorCall(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 13.5: the operator an operator function overloads, as the source spelled it.
// The name a declaration of one is bound to is `operator` and this, closed up.
const char* OperatorCall::spelling(unsigned token)
{
	switch (token)
	{
	case OP_PLUS: return "+";
	case OP_MINUS: return "-";
	case OP_STAR: return "*";
	case OP_DIV: return "/";
	case OP_MOD: return "%";
	case OP_XOR: return "^";
	case OP_AMP: return "&";
	case OP_BOR: return "|";
	case OP_COMPL: return "~";
	case OP_LNOT: return "!";
	case OP_ASS: return "=";
	case OP_LT: return "<";
	case OP_GT: return ">";
	case OP_PLUSASS: return "+=";
	case OP_MINUSASS: return "-=";
	case OP_STARASS: return "*=";
	case OP_DIVASS: return "/=";
	case OP_MODASS: return "%=";
	case OP_XORASS: return "^=";
	case OP_BANDASS: return "&=";
	case OP_BORASS: return "|=";
	case OP_LSHIFT: return "<<";
	case OP_RSHIFT: return ">>";
	case OP_LSHIFTASS: return "<<=";
	case OP_RSHIFTASS: return ">>=";
	case OP_EQ: return "==";
	case OP_NE: return "!=";
	case OP_LE: return "<=";
	case OP_GE: return ">=";
	case OP_LAND: return "&&";
	case OP_LOR: return "||";
	case OP_INC: return "++";
	case OP_DEC: return "--";
	case OP_COMMA: return ",";
	case OP_ARROWSTAR: return "->*";
	case OP_ARROW: return "->";
	case OP_LSQUARE: return "[]";
	case OP_LPAREN: return "()";
	default: break;
	}
	return nullptr;
}

// 13.5p1: the operators an operator-function-id may be written with, which is
// what tells one from the allocation functions of 3.7.4 and the literal
// operators of 13.5.8, whose names begin the same way.
bool OperatorCall::overloadable(const std::string& name)
{
	if (name.compare(0, 8, "operator") != 0)
	{
		return false;
	}
	static const char* const kOverloadable[] = {
		"+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">",
		"+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "<<", ">>", "<<=",
		">>=", "==", "!=", "<=", ">=", "&&", "||", "++", "--", ",", "->*",
		"->", "[]", "()"
	};
	const std::string written = name.substr(8);
	for (std::size_t index = 0;
	     index < sizeof(kOverloadable) / sizeof(kOverloadable[0]); ++index)
	{
		if (written == kOverloadable[index])
		{
			return true;
		}
	}
	return false;
}

// 13.5.3p1, 13.5.4p1, 13.5.5p1 and 13.5.6p1: assignment, the function call, the
// subscript and `->` are each declared "by a non-static member function with no
// parameters" or with the ones the clause names - a program may not write a
// non-member one at all.  So 13.3.1.2p3's non-member half is left out of what
// these four reach, and gathering it would be a candidate no program may
// declare: `int operator[](C, int)` is ill-formed, and a set that held it would
// answer a subscript the expression layer refuses.
bool OperatorCall::member_only(unsigned token)
{
	return token == OP_ASS || token == OP_LPAREN || token == OP_LSQUARE ||
		token == OP_ARROW;
}

// 3.7.4p2 and 12.5p1: whether the name is one of the allocation and
// deallocation functions, which 13.5p1 leaves out of the operators a program
// may give a meaning to.  Written in a class they are static members of it
// whether or not `static` was written, because 12.5p1 says so and because there
// is no object for an implicit object argument to name: the storage this
// function is being asked for is what an object of the class would stand in.
bool SemaAnalyzer::allocation_function_name(const std::string& name)
{
	if (name.compare(0, 8, "operator") != 0)
	{
		return false;
	}
	const std::string written = name.substr(8);
	return written == "new" || written == "new[]" || written == "delete" ||
		written == "delete[]";
}

// 3.7.4p2 and 13.5p1: the same name with the whitespace an id-expression
// carried taken out of it.  `operator new` and `operator delete` are the two
// operator-function-ids whose operator is a keyword, so they are the two a use
// has to be written with a space in - and a declaration of one is bound under
// the spelling that has none.  Every other name is returned as it stands, which
// is what keeps this one probe of the tail rather than a rewriting of names.
std::string SemaAnalyzer::allocation_function_spelling(const std::string& written)
{
	// The two names are the only ones an id-expression writes a space in, so
	// one scan for a space answers every other name before any search runs.
	if (written.find(' ') == std::string::npos)
	{
		return written;
	}
	const std::string::size_type at = written.rfind("operator");
	if (at == std::string::npos || written.find(' ', at) == std::string::npos)
	{
		return written;
	}
	std::string packed;
	for (std::string::size_type index = at; index < written.size(); ++index)
	{
		if (written[index] != ' ')
		{
			packed.push_back(written[index]);
		}
	}
	return allocation_function_name(packed) ? written.substr(0, at) + packed
	                                        : written;
}

namespace
{

// Whether `what` is already in `where`, which a candidate set holds few enough
// of for a scan to answer.
template <typename T>
bool held(const std::vector<T*>& where, const T* what)
{
	for (std::size_t index = 0; index < where.size(); ++index)
	{
		if (where[index] == what)
		{
			return true;
		}
	}
	return false;
}

}  // namespace

// 13.5p6: an operator function shall be a non-static member function, or else a
// non-member function taking at least one parameter of class or enumeration
// type or a reference to one - so that 13.3.1.2p2 never lets a program give a
// new meaning to an operator on operands the language already gives one to.
// `member` says the declaration is written in a class and has no object
// parameter, which is the static member the clause leaves no room for.
void OperatorCall::require_operand(const std::string& name, TypeId type,
                                   bool member)
{
	if (!OperatorCall::overloadable(name))
	{
		return;
	}
	if (member)
	{
		throw std::runtime_error(name + " is declared a static member, and an "
		                         "operator function is a non-static member or "
		                         "a non-member");
	}
	const std::vector<TypeId>& parameters = analyzer_.types_.parameters(type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		TypeId at = parameters[index];
		if (analyzer_.types_.is_reference(at))
		{
			at = analyzer_.types_.target(at);
		}
		at = analyzer_.types_.strip_cv(at);
		if (analyzer_.types_.is_class(at) ||
		    analyzer_.types_.kind(at) == TypeKind::Enum)
		{
			return;
		}
		// 14p1 and 13.5p6: a parameter written over a template parameter is of
		// no type until an instantiation gives it one, so the clause is a
		// question about the specialization rather than about the template.
		// Every specialization this one makes has its own parameter types, and
		// the declaration each of them makes asks again.
		if (analyzer_.types_.is_dependent(at))
		{
			return;
		}
	}
	throw std::runtime_error(name + " is declared outside a class and takes no "
	                         "operand of class or enumeration type");
}

// 13.3.1.1.2p2: the surrogate call functions an object of `type` is called
// through, which is one declaration per non-explicit conversion function of its
// class that yields a pointer to a function - taking that pointer where the
// implied object argument stands and the function's own parameters after it, so
// 13.3 ranks it against the class's `operator()`s over one argument list.
//
// The set is a fact of the class and is built once: 12.3.2p1's conversions are
// already gathered in one walk of the classes that declare any, and a class
// that declares none - which is nearly every class - answers with an empty list
// it never walks again.  8.3.5p5's adjustment is what makes a conversion to a
// *reference* to a function one of these too: what the call reads is the
// pointer either way.
const std::vector<SemaEntity*>& OperatorCall::surrogates(TypeId type)
{
	const std::unordered_map<TypeId, std::vector<SemaEntity*> >::const_iterator
		found = analyzer_.surrogate_calls_.find(type);
	if (found != analyzer_.surrogate_calls_.end())
	{
		return found->second;
	}
	std::vector<SemaEntity*>& built = analyzer_.surrogate_calls_[type];
	SemaEntity* const owner = analyzer_.model_.type_owner(type);
	if (owner == nullptr || owner->conversions_above.empty())
	{
		return built;
	}
	std::vector<SemaEntity*> conversions;
	analyzer_.gather_conversions(*owner, conversions);
	for (std::size_t index = 0; index < conversions.size(); ++index)
	{
		SemaEntity& conversion = *conversions[index];
		if (conversion.explicit_function || conversion.deleted)
		{
			// 13.3.1.1.2p2: only a non-explicit conversion is one a call
			// written on the object reaches without naming it.
			continue;
		}
		const TypeId result = analyzer_.types_.target(conversion.type);
		TypeId pointer = analyzer_.types_.is_reference(result)
			? analyzer_.types_.strip_cv(analyzer_.types_.target(result))
			: analyzer_.types_.strip_cv(result);
		if (analyzer_.types_.kind(pointer) == TypeKind::Function)
		{
			pointer = analyzer_.types_.pointer_to(pointer);
		}
		if (analyzer_.types_.kind(pointer) != TypeKind::Pointer ||
		    analyzer_.types_.kind(
			    analyzer_.types_.strip_cv(
				    analyzer_.types_.target(pointer))) != TypeKind::Function)
		{
			continue;
		}
		const TypeId function =
			analyzer_.types_.strip_cv(analyzer_.types_.target(pointer));
		// 13.3.1.1.2p2: the surrogate takes the pointer the conversion yields
		// and then the parameters of the function it points to, and hands back
		// what that function does.
		std::vector<TypeId> parameters(1, pointer);
		const std::vector<TypeId>& written = analyzer_.types_.parameters(function);
		parameters.insert(parameters.end(), written.begin(), written.end());
		SemaEntity& surrogate = analyzer_.model_.create(
			SemaKind::Function, "call-function",
			analyzer_.types_.function_of(analyzer_.types_.target(function), parameters,
			                   analyzer_.types_.variadic(function)));
		surrogate.surrogate_for = &conversion;
		built.push_back(&surrogate);
	}
	return built;
}

// 13.3.1.2p3 with 13.3.1.1.2p2: the declarations of one operator these operands
// reach, which is the set 13.3 is then asked to choose from.
//
// The gathering is three lookups and no ranking at all, which is why it stands
// apart from either reader: the expression layer asks it for the call an
// operator expression stands for, and `ConstexprReading::operator_constant`
// asks it for the same call over the constants a fold's operands came to.  Each
// set is a local of the caller's rather than one of the model's kept overload
// sets, so an operator folded in a loop of n passes costs n lookups and nothing
// the model holds on to.
std::size_t OperatorCall::candidates(unsigned token, const SemaContext& ctx,
                                     const std::vector<AnalyzedValue>& operands,
                                     bool member_only,
                                     std::vector<SemaEntity*>& out)
{
	const char* const written = spelling(token);
	if (written == nullptr || operands.empty())
	{
		return 0;
	}
	const std::string name = std::string("operator") + written;
	std::size_t singles = 0;
	// 13.3.1.2p3: the member candidates are what a lookup of the name in the
	// class of the left operand finds, which 10.2 also searches its bases for.
	SemaEntity* const owner = operands[0].type == kNoType
		? nullptr
		: analyzer_.model_.type_owner(
			  analyzer_.types_.strip_cv(operands[0].type));
	if (owner != nullptr && owner->scope != nullptr)
	{
		std::vector<SemaEntity*> members;
		SemaEntity* const found = analyzer_.model_.lookup_in(
			*owner->scope, name, LookupKind::Any, &members);
		if (found != nullptr && found->kind == SemaKind::Function)
		{
			for (std::size_t index = 0; index < members.size(); ++index)
			{
				out.push_back(members[index]);
			}
			if (members.empty())
			{
				out.push_back(found);
			}
		}
	}
	if (!member_only && ctx.scope != nullptr)
	{
		// 13.3.1.2p3: the non-member candidates are what an unqualified lookup
		// of the name finds with every member function left out, together with
		// what 3.4.2 adds for the operand types.
		std::vector<SemaEntity*> reached;
		SemaEntity* const found = analyzer_.model_.lookup(
			*ctx.scope, name, LookupKind::Any, &reached);
		if (found != nullptr && found->kind == SemaKind::Function)
		{
			if (reached.empty())
			{
				reached.push_back(found);
			}
			for (std::size_t index = 0; index < reached.size(); ++index)
			{
				SemaEntity* const head = reached[index];
				if (head->region != nullptr &&
				    head->region->kind == ScopeKind::Class)
				{
					continue;
				}
				if (!held(out, head))
				{
					out.push_back(head);
				}
			}
		}
		singles = ArgumentLookup(analyzer_).candidates(name, operands, out);
	}
	if (token == OP_LPAREN && owner != nullptr)
	{
		// 13.3.1.1.2p2: a call written on an object of class type also reaches
		// the surrogate call function of every conversion its class has to a
		// pointer to function, so those stand in the one set 13.3 chooses from
		// beside the `operator()`s the lookup above found.  Each of them is one
		// declaration and no chain, so they are gathered last and counted among
		// the entries nothing is walked past.
		const std::vector<SemaEntity*>& called_through =
			surrogates(analyzer_.types_.strip_cv(operands[0].type));
		out.insert(out.end(), called_through.begin(), called_through.end());
		singles += called_through.size();
	}
	return singles;
}

// 13.3.1.2p1: an operator expression with an operand of class or enumeration
// type is a call of an operator function, and 13.3 chooses that function among
// the member operator functions 13.3.1.2p3 gathers from the first operand's
// class, the non-member ones ordinary lookup and 3.4.2 reach, and 13.6's
// built-in operators, whose meaning is the one the caller writes itself.
//
// `line` already holds one node per operand, in the order the operands were
// written.  Where a declaration is chosen the node becomes the call it stands
// for; where the candidate set is empty or nothing in it is viable, nothing is
// written and the caller reads the operands as it would have.
bool OperatorCall::expression(unsigned token, const SemaContext& ctx,
                              DumpNode& line,
                              std::vector<AnalyzedValue>& operands,
                              bool member_only, AnalyzedValue& value)
{
	const char* const spelling = OperatorCall::spelling(token);
	if (spelling == nullptr || operands.empty())
	{
		return false;
	}
	// 13.3.1.2p2: an operator whose operands are all of built-in type is the
	// built-in operator, and no lookup is done for it at all.
	bool overloadable = false;
	// 5.17p9 and 13.3.3.1.5p1: an operand written as a braced-init-list is no
	// operand a built-in operator reads, so an operator function is all that
	// can answer it and 13.6's candidates are not gathered beside them.
	bool listed = false;
	for (std::size_t index = 0; index < operands.size(); ++index)
	{
		if (operands[index].braced != nullptr)
		{
			listed = true;
			continue;
		}
		if (operands[index].type == kNoType &&
		    operands[index].functions != nullptr &&
		    operands[index].node != nullptr)
		{
			// 13.4p1: an overloaded name has no type of its own, so it says
			// nothing about whether 13.3.1.2p2 leaves this the built-in
			// operator - and an operator function's parameter is exactly the
			// target type that chooses one of its declarations.  So the
			// candidates are gathered and 13.3 asks each of them; what makes
			// the operator a call has to be another operand.
			continue;
		}
		if (operands[index].type == kNoType || operands[index].node == nullptr)
		{
			// 13.4p1: an operand that is still an overloaded name has no type
			// for 13.3.1.2p2 to ask about, and only a target type resolves it.
			return false;
		}
		const TypeId bare = analyzer_.types_.strip_cv(operands[index].type);
		overloadable = overloadable ||
			analyzer_.types_.is_class(bare) ||
			analyzer_.types_.kind(bare) == TypeKind::Enum;
	}
	if (!overloadable)
	{
		return false;
	}
	const std::string name = std::string("operator") + spelling;
	std::vector<SemaEntity*> reached;
	SemaEntity* const owner =
		operands[0].type == kNoType
			? nullptr
			: analyzer_.model_.type_owner(
				  analyzer_.types_.strip_cv(operands[0].type));
	const std::size_t singles =
		candidates(token, ctx, operands, member_only, reached);
	if (reached.empty())
	{
		// 13.3.1.2p2: no operator function is a candidate, so what is left is
		// 13.6's built-in operator, which an operand of class type reaches
		// through a conversion function of its class.
		if (!listed)
		{
			analyzer_.builtin_operands(token, ctx, operands);
		}
		return false;
	}

	// 13.3.1p3 and 13.3.1.2p4: the first operand is the implicit object
	// argument of a member candidate and the first argument of a non-member
	// one, so both are offered it and the rest of the operands are the
	// arguments either way.
	AnalyzedValue object = operands[0];
	object.type = object.spelled = analyzer_.types_.pointer_to(operands[0].type);
	// 13.3.1p4: the object parameter is a reference, so what a member
	// candidate's ref-qualifier binds by is the category the operand was
	// written with rather than the prvalue address that stands for it.
	object.object_category = operands[0].category;
	object.category = ValueCategory::PRValue;
	object.node = nullptr;
	std::vector<AnalyzedValue> rest(operands.begin() + 1, operands.end());
	bool unviable = false;
	SemaEntity* const chosen =
		analyzer_.select_overload(reached, rest, name, &object, false, singles,
		                &operands[0], &unviable);
	if (chosen == nullptr)
	{
		if (listed)
		{
			// 5.17p9: what a braced-init-list operand is written for is the
			// operator function, and there is no built-in operator to fall to.
			return false;
		}
		// 13.3.1.2p2: no operator function accepts these operands, so what is
		// left is the built-in operator the caller describes.
		analyzer_.builtin_operands(token, ctx, operands);
		return false;
	}
	if (chosen->surrogate_for != nullptr)
	{
		// 13.3.1.1.2p2: what the surrogate stands for is the conversion run on
		// the object and a call of what the pointer it handed back points to,
		// so that is what is written: the conversion takes the place the object
		// operand held, which is the place a callee stands in, and the operands
		// after it are the arguments 5.2.2p1 passes.
		AnalyzedValue callee = analyzer_.call_conversion(
			operands[0], *chosen->surrogate_for, ctx);
		const TypeId pointer = analyzer_.decayed(callee);
		value = analyzer_.finish_call(
			line, analyzer_.types_.target(pointer), rest, nullptr, ctx);
		return true;
	}
	if (!listed && analyzer_.better_builtin(*chosen, object, operands))
	{
		// 13.6 and 13.3.3p1: the built-in operators are candidates beside the
		// operator functions, and one of them reads these operands better than
		// the declaration 13.3 just chose - which is what an operand that
		// reaches a built-in operator through one conversion does against a
		// declaration that would take two.
		analyzer_.builtin_operands(token, ctx, operands);
		return false;
	}
	// 11.2p5 and 11.4p1: a member operator function is a member named on an
	// object, so the object asks the same two questions here that it asks at
	// `.` and `->` - which class the name was written on, and, for a protected
	// member the access reaches only through a derived class, whether the
	// object is of that class rather than of the base that declared it.  13.3
	// has chosen by now, so the question is asked of the one declaration.
	Scope* const naming =
		chosen->object_member && owner != nullptr ? owner->scope : nullptr;
	Access(analyzer_).require_access(*chosen, ctx.scope, naming);
	if (naming != nullptr)
	{
		const std::vector<SemaEntity*> settled;
		Access(analyzer_).require_protected_object(settled, *chosen, ctx.scope,
		                                           naming);
	}

	// 7.3.3p1: 13.3 ranked the declaration the class made, and what an operator
	// expression calls is the one it names - reached through the base subobject
	// of the object the expression named, which 11.2p5 leaves the
	// base-specifier's own access unasked about because the naming class is the
	// one the using-declaration was written in.
	SemaEntity& run = analyzer_.declared_member(*chosen);
	std::vector<AnalyzedValue> arguments;
	if (chosen->object_member)
	{
		// 9.3.1p3: the object the member is called on is its first parameter,
		// and the call passes its address.
		AnalyzedValue self = operands[0];
		analyzer_.address_of_object(
			self,
			analyzer_.model_.wrap_node(*operands[0].node, std::string()),
			false);
		self.through_using = chosen->shadowed != nullptr;
		arguments.push_back(self);
	}
	else
	{
		arguments.push_back(operands[0]);
	}
	for (std::size_t index = 1; index < operands.size(); ++index)
	{
		arguments.push_back(operands[index]);
	}

	// The callee stands before the arguments, as it does in a call the program
	// wrote, and the operands already hold the places after it.
	DumpNode& named = analyzer_.model_.open_node(line, std::string());
	line.children.pop_back();
	line.children.insert(line.children.begin(), &named);
	AnalyzedValue callee;
	callee.node = &named;
	analyzer_.name_function(callee, run, "callee");
	value = analyzer_.finish_call(line, run.type, arguments, &run, ctx);
	return true;
}
