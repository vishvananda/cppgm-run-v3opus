#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

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

namespace
{

// 13.5: the operator an operator function overloads, as the source spelled it.
// The name a declaration of one is bound to is `operator` and this, closed up.
const char* operator_spelling(unsigned token)
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
bool overloadable_operator(const std::string& name)
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
void SemaAnalyzer::require_operator_operand(const std::string& name,
                                            TypeId type, bool member)
{
	if (!overloadable_operator(name))
	{
		return;
	}
	if (member)
	{
		throw std::runtime_error(name + " is declared a static member, and an "
		                         "operator function is a non-static member or "
		                         "a non-member");
	}
	const std::vector<TypeId>& parameters = types_.parameters(type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		TypeId at = parameters[index];
		if (types_.is_reference(at))
		{
			at = types_.target(at);
		}
		at = types_.strip_cv(at);
		if (types_.is_class(at) || types_.kind(at) == TypeKind::Enum)
		{
			return;
		}
	}
	throw std::runtime_error(name + " is declared outside a class and takes no "
	                         "operand of class or enumeration type");
}

// 3.4.2p2: the namespaces and classes one argument type is associated with.
// A class is associated with itself, its base classes, the class it is a member
// of, and the innermost enclosing namespace of each of them; an enumeration
// with the class or namespace that declares it; a pointer, an array, a
// reference or a function with the types it is written over.  The walk follows
// the type rather than searching, so it costs the depth of the type and the
// depth of the base chain, both of which the source wrote.
void SemaAnalyzer::associate_type(TypeId type, Associated& out)
{
	const TypeId bare = types_.strip_cv(type);
	switch (types_.kind(bare))
	{
	case TypeKind::Pointer:
	case TypeKind::Array:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		associate_type(types_.target(bare), out);
		return;

	case TypeKind::Function:
	{
		associate_type(types_.target(bare), out);
		const std::vector<TypeId>& parameters = types_.parameters(bare);
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			associate_type(parameters[index], out);
		}
		return;
	}

	case TypeKind::MemberPointer:
		associate_type(types_.member_class(bare), out);
		associate_type(types_.target(bare), out);
		return;

	default:
		break;
	}
	SemaEntity* const owner = model_.type_owner(bare);
	if (owner == nullptr)
	{
		return;
	}
	if (owner->kind == SemaKind::Enum)
	{
		// 3.4.2p2: an enumeration associates the region that declares it, which
		// is its class where a class declares it and its namespace otherwise.
		associate_region(owner->region, out);
		return;
	}
	if (owner->kind != SemaKind::Class)
	{
		return;
	}
	// 10p1: the base classes of the class are associated with it, and so are
	// their own, which the chain the base-clauses left is one walk of.  What
	// stops the walk is having walked this class's chain before, not having the
	// class in the set: 3.4.2p2 also associates the class a nested type is a
	// member of, and puts that class in without its bases - so a set that holds
	// it says nothing about them.
	for (SemaEntity* at = owner; at != nullptr; at = at->base)
	{
		if (at->scope == nullptr || !out.walked.insert(at).second)
		{
			break;
		}
		if (out.held.insert(at->scope).second)
		{
			out.classes.push_back(at->scope);
		}
		associate_region(at->region, out);
	}
}

// 3.4.2p2: the region a class or enumeration is a member of, which associates
// that class where a class declares it and, either way, the innermost
// enclosing namespace.
void SemaAnalyzer::associate_region(Scope* region, Associated& out)
{
	// 3.4.2p2 associates the class the type is a member of, and not the classes
	// that class is in turn a member of - so only the innermost is taken.  The
	// walk still climbs past them, because what it is looking for beyond that
	// is the one namespace they all stand in.
	bool member_class = true;
	for (Scope* at = region; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class)
		{
			if (member_class && out.held.insert(at).second)
			{
				out.classes.push_back(at);
			}
			member_class = false;
			continue;
		}
		if (at->kind != ScopeKind::Namespace)
		{
			continue;
		}
		if (out.held.insert(at).second)
		{
			out.spaces.push_back(at);
		}
		return;
	}
}

// 3.4.2p2: the declarations of `name` that the types of `arguments` reach.  The
// namespaces associated with them are searched for a declaration bound there -
// 3.4.2p4 leaves the using-directives written in them out, and the search does
// not climb the regions around them - and each associated class contributes the
// friend declarations 11.3p6 gave it, which no region binds.  Returns how many
// of the entries appended are such single declarations, which stand at the end.
std::size_t SemaAnalyzer::argument_candidates(
	const std::string& name, const std::vector<Value>& arguments,
	std::vector<SemaEntity*>& candidates)
{
	// 13.3p1 puts each declaration in the set once, and a class may declare as
	// many friends of one name as the program writes - so which declarations
	// the set already holds is a probe rather than a scan of it, and gathering
	// one call's candidates costs what the declarations there are and not their
	// square.
	std::unordered_set<SemaEntity*> gathered(candidates.begin(),
	                                         candidates.end());
	Associated associated;
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (arguments[index].type != kNoType)
		{
			associate_type(arguments[index].spelled != kNoType
			               ? arguments[index].spelled
			               : arguments[index].type, associated);
		}
	}
	const std::vector<Scope*>& spaces = associated.spaces;
	const std::vector<Scope*>& classes = associated.classes;
	for (std::size_t index = 0; index < spaces.size(); ++index)
	{
		SemaEntity* const head = model_.find(*spaces[index], name,
		                                     LookupKind::Any);
		if (head != nullptr && head->kind == SemaKind::Function &&
		    gathered.insert(head).second)
		{
			candidates.push_back(head);
		}
	}
	std::size_t singles = 0;
	for (std::size_t index = 0; index < classes.size(); ++index)
	{
		const std::vector<SemaEntity*>& friends =
			classes[index]->friend_functions;
		for (std::size_t at = 0; at < friends.size(); ++at)
		{
			if (friends[at]->name == name &&
			    gathered.insert(friends[at]).second)
			{
				candidates.push_back(friends[at]);
				++singles;
			}
		}
	}
	return singles;
}

// 3.4.2p3: the lookup that named the callee suppresses the argument-dependent
// one when what it found is a member of a class, a declaration written in a
// block, or anything that is not a function.
bool SemaAnalyzer::allows_adl(const SemaEntity* named) const
{
	if (named == nullptr)
	{
		return true;
	}
	if (named->kind != SemaKind::Function || named->region == nullptr)
	{
		return false;
	}
	return named->region->kind == ScopeKind::Namespace;
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
bool SemaAnalyzer::operator_expression(unsigned token, const Context& ctx,
                                       DumpNode& line,
                                       std::vector<Value>& operands,
                                       bool member_only, Value& value)
{
	const char* const spelling = operator_spelling(token);
	if (spelling == nullptr || operands.empty())
	{
		return false;
	}
	// 13.3.1.2p2: an operator whose operands are all of built-in type is the
	// built-in operator, and no lookup is done for it at all.
	bool overloadable = false;
	for (std::size_t index = 0; index < operands.size(); ++index)
	{
		if (operands[index].type == kNoType || operands[index].node == nullptr)
		{
			// 13.4p1: an operand that is still an overloaded name has no type
			// for 13.3.1.2p2 to ask about, and only a target type resolves it.
			return false;
		}
		const TypeId bare = types_.strip_cv(operands[index].type);
		overloadable = overloadable ||
			types_.is_class(bare) || types_.kind(bare) == TypeKind::Enum;
	}
	if (!overloadable)
	{
		return false;
	}
	const std::string name = std::string("operator") + spelling;
	std::vector<SemaEntity*> candidates;
	std::size_t singles = 0;
	// 13.3.1.2p3: the member candidates are what a lookup of the name in the
	// class of the left operand finds, which 10.2 also searches its bases for.
	SemaEntity* const owner =
		model_.type_owner(types_.strip_cv(operands[0].type));
	std::vector<SemaEntity*>& members = model_.open_overloads();
	if (owner != nullptr && owner->scope != nullptr)
	{
		SemaEntity* const found =
			model_.lookup_in(*owner->scope, name, LookupKind::Any, &members);
		if (found != nullptr && found->kind == SemaKind::Function)
		{
			for (std::size_t index = 0; index < members.size(); ++index)
			{
				candidates.push_back(members[index]);
			}
			if (members.empty())
			{
				candidates.push_back(found);
			}
		}
	}
	if (!member_only)
	{
		// 13.3.1.2p3: the non-member candidates are what an unqualified lookup
		// of the name finds with every member function left out, together with
		// what 3.4.2 adds for the operand types.
		std::vector<SemaEntity*>& reached = model_.open_overloads();
		SemaEntity* const found =
			model_.lookup(*ctx.scope, name, LookupKind::Any, &reached);
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
				if (!held(candidates, head))
				{
					candidates.push_back(head);
				}
			}
		}
		singles = argument_candidates(name, operands, candidates);
	}
	if (candidates.empty())
	{
		return false;
	}

	// 13.3.1p3 and 13.3.1.2p4: the first operand is the implicit object
	// argument of a member candidate and the first argument of a non-member
	// one, so both are offered it and the rest of the operands are the
	// arguments either way.
	Value object = operands[0];
	object.type = object.spelled = types_.pointer_to(operands[0].type);
	object.category = ValueCategory::PRValue;
	object.node = nullptr;
	std::vector<Value> rest(operands.begin() + 1, operands.end());
	bool unviable = false;
	SemaEntity* const chosen =
		select_overload(candidates, rest, name, &object, false, singles,
		                &operands[0], &unviable);
	if (chosen == nullptr)
	{
		// 13.3.1.2p2: no operator function accepts these operands, so what is
		// left is the built-in operator the caller describes.
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
	require_access(*chosen, ctx.scope, naming);
	if (naming != nullptr)
	{
		const std::vector<SemaEntity*> settled;
		require_protected_object(settled, *chosen, ctx.scope, naming);
	}

	std::vector<Value> arguments;
	if (chosen->object_member)
	{
		// 9.3.1p3: the object the member is called on is its first parameter,
		// and the call passes its address.
		Value self = operands[0];
		address_of_object(self, model_.wrap_node(*operands[0].node,
		                                         std::string()), false);
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
	DumpNode& named = model_.open_node(line, std::string());
	line.children.pop_back();
	line.children.insert(line.children.begin(), &named);
	Value callee;
	callee.node = &named;
	name_function(callee, *chosen, "callee");
	value = finish_call(line, chosen->type, arguments, chosen);
	return true;
}
