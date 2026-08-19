#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_builtin.h"
#include "sema_name.h"

// 14.6p8: the names a template definition writes, looked up where the
// definition stands.
//
// A template is read twice - once as the pattern it is, and again for each
// argument list an instantiation gives it - and 14.6p8 splits the lookups
// between those two readings: a name that does not depend on a template
// parameter is looked up here, at the point of definition, and the rest at the
// point of instantiation.  This file is the first of the two readings.  It
// owns nothing the second one needs, so every answer it reaches is a refusal
// or nothing at all.

namespace
{

const AstNode* child_of(const AstNode& node, AstKind kind)
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

// 5.2.3p1 and Table 10: whether a spelling standing where a callee does is a
// simple-type-specifier written out of keywords, which the parser leaves as the
// text of an id-expression.  An explicit type conversion written that way names
// a type rather than a function, so no lookup answers for it.
bool names_a_builtin_type(const std::string& spelling)
{
	static const char* const kSpecifiers[] = {
		"char", "char16_t", "char32_t", "wchar_t", "bool", "short", "int",
		"long", "signed", "unsigned", "float", "double", "void"
	};
	std::string::size_type at = 0;
	bool any = false;
	while (at < spelling.size())
	{
		const std::string::size_type end = spelling.find(' ', at);
		const std::string word = spelling.substr(
			at, end == std::string::npos ? end : end - at);
		bool found = false;
		for (std::size_t index = 0;
		     !found && index < sizeof(kSpecifiers) / sizeof(kSpecifiers[0]);
		     ++index)
		{
			found = word == kSpecifiers[index];
		}
		if (!found)
		{
			return false;
		}
		any = true;
		at = end == std::string::npos ? spelling.size() : end + 1;
	}
	return any;
}

}

// 14.6p8 and 3.4p1: the names an expression written in a template definition
// writes, looked up where the definition stands.
//
// Only what the definition can answer is asked.  A name written after `.`,
// `->` or `::` is a member of whatever the prefix turned out to be, and
// 14.6.2p1 leaves that to the instantiation; a template-id names a
// specialization no argument list has been given yet; and 3.4.2p2 lets the
// name of a called function be one only the arguments' own namespaces
// declare.  What is left is the unqualified name of a value, and 3.4p1 settles
// it here: it names something, it names one thing, and what it names is not a
// type.
void SemaAnalyzer::check_expression_names(const AstNode& node,
                                          const Context& ctx)
{
	switch (node.kind)
	{
	case AstKind::IdExpression:
	{
		check_value_name(node, ctx);
		return;
	}

	case AstKind::MemberExpression:
		// 5.2.5p2 and 14.6.2p1: the object expression is an expression of this
		// definition; the member name belongs to the class it turns out to
		// have, which an argument is what settles.
		if (!node.children.empty())
		{
			check_expression_names(*node.children[0], ctx);
			check_member_template_keyword(node, ctx);
		}
		return;

	case AstKind::CallExpression:
	{
		// 3.4.2p2: an unqualified name written as the callee of a call is
		// looked up in the namespaces its arguments are associated with as
		// well, which are classes an argument list has yet to name - so
		// 14.6.2p1 leaves the callee to the instantiation wherever an argument
		// can carry a namespace this reading cannot see.  A literal cannot:
		// 3.4.2p2 associates nothing with a fundamental type, so a call written
		// with no arguments, or with none but literals, reaches exactly the
		// declarations ordinary lookup does and its callee is settled here.
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			const AstNode& child = *node.children[index];
			if (index == 0 && child.kind == AstKind::IdExpression)
			{
				if (arguments_associate_nothing(node))
				{
					check_callee_name(child, ctx);
				}
				continue;
			}
			check_expression_names(child, ctx);
		}
		return;
	}

	case AstKind::TypeId:
	case AstKind::TypeSpecifierSeq:
	case AstKind::DecltypeSpecifier:
	case AstKind::LambdaExpression:
		// A type-id's own names are the declarator layer's to read, and
		// 7.1.6.2p1's decltype-specifier holds an expression that says what
		// type it is rather than what value.  A cast, a `sizeof`, an `alignof`
		// and a `new` each write one of those beside an expression, so the walk
		// below reaches the expression and stops at the type-id - 5.3.3p1
		// leaves the operand of `sizeof` unevaluated and 3.4p1 still looks its
		// names up.
		return;

	default:
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			check_expression_names(*node.children[index], ctx);
		}
		return;
	}
}

// 3.4.2p2: whether the arguments of a call name any namespace ordinary lookup
// has not already read.
//
// The associated namespaces and classes of an argument are its *type's*, and a
// fundamental type has none - so a call written with no arguments, or with none
// but literals, is one 3.4.2p2 adds nothing to.  Anything else may turn out to
// be a class an argument list has yet to name, which is what 14.6.2p1 leaves
// the callee to the instantiation for.
bool SemaAnalyzer::arguments_associate_nothing(const AstNode& call) const
{
	for (std::size_t index = 1; index < call.children.size(); ++index)
	{
		const AstNode& written = *call.children[index];
		for (std::size_t at = 0; at < written.children.size(); ++at)
		{
			const AstKind kind = written.children[at]->kind;
			if (kind != AstKind::Literal && kind != AstKind::KeywordLiteral)
			{
				return false;
			}
		}
	}
	return true;
}

// 3.4p1: what an unqualified name written in a template definition names where
// that definition stands.
//
// `answered` is false where this reading is not the one that settles the name:
// 3.4.3p1's qualified name reaches a region a template parameter may stand for,
// 14.2's template-id names a specialization this reading makes none of, and
// 1.4p8's reserved name is declared where it is used rather than where the
// program wrote it.  Otherwise the answer is what the name names, and null says
// nothing declares it.
SemaEntity* SemaAnalyzer::definition_time_name(const AstNode& node,
                                               const Context& ctx,
                                               bool& answered)
{
	answered = false;
	if (node.text.empty() ||
	    child_of(node, AstKind::CarriedExpression) != nullptr)
	{
		return nullptr;
	}
	const QualifiedName written(node.text);
	if (written.qualified() || node.text.find('<') != std::string::npos ||
	    names_a_builtin_type(node.text))
	{
		// 5.2.3p1: a simple-type-specifier written where a callee stands is an
		// explicit type conversion, and the type it names is a keyword no
		// declaration bound.
		return nullptr;
	}
	SemaEntity* const named =
		model_.lookup(*ctx.scope, node.text, LookupKind::Any);
	if (named == nullptr && (BuiltinReading(*this).reserved(node.text,
	                                                        nullptr) != nullptr ||
	                         BuiltinReading::answers(node.text)))
	{
		return nullptr;
	}
	answered = true;
	return named;
}

// 3.4p1 and 3.4.2p2: the callee of a call this reading can look up, which shall
// name something.  5.2.3p1's explicit type conversion is written the same way,
// so a type-name stands here and only the first of 5.1.1p8's questions is asked.
void SemaAnalyzer::check_callee_name(const AstNode& node, const Context& ctx)
{
	bool answered = false;
	if (definition_time_name(node, ctx, answered) != nullptr || !answered)
	{
		return;
	}
	throw std::runtime_error(node.text + " names nothing where the template "
	                         "that calls it is defined, and its arguments "
	                         "associate no namespace 3.4.2p2 could find it in");
}

// 14.2p4: the keyword a member template-id is written with where the object
// expression is type-dependent.
//
// Without it the name is assumed to name a non-template, so `box.cast<int>(4)`
// written on a `Box<Tag>` is 5.9's two operators and this definition compares a
// member function with a type.  The parse cannot tell the two readings apart -
// what says which a `<` is is a lookup in the object's class, and 14.6.2p1
// leaves that class to the instantiation - so the reading it took is the one
// this rule is asked about here, where the definition still knows which of its
// names a template parameter stands in the way of.
//
// Only the object expression this reading settles is asked: an id-expression
// naming a declaration in scope, whose type says whether an argument is still
// to come.  A member of the current instantiation is a name this definition
// does find, and 14.2p4 asks for no keyword on one.
void SemaAnalyzer::check_member_template_keyword(const AstNode& node,
                                                 const Context& ctx)
{
	if (node.children.size() < 2)
	{
		return;
	}
	// Nearly every member name is one identifier, and the question is only
	// about a template-id - so one scan of the spelling settles it before any
	// of the readings below is made.
	const AstNode& id = *node.children[1];
	if (id.text.find('<') == std::string::npos ||
	    !QualifiedName(id.text).names_a_template_id() ||
	    without_template_keyword(id.text) != id.text)
	{
		return;
	}
	const AstNode& object = *node.children[0];
	if (object.kind != AstKind::IdExpression)
	{
		return;
	}
	bool answered = false;
	const SemaEntity* const named = definition_time_name(object, ctx, answered);
	if (!answered || named == nullptr ||
	    (named->kind != SemaKind::Variable && named->kind != SemaKind::Parameter))
	{
		return;
	}
	if (!types_.is_dependent(named->type))
	{
		return;
	}
	// 14.6.2.1p1: a member of the current instantiation is a name the
	// definition itself finds - a member written on an object of its own class
	// is looked up in the class the definition stands in, and no argument has
	// to arrive first - and 14.2p4 asks for no keyword on one.
	const std::string member = QualifiedName(id.text).last();
	if (model_.lookup(*ctx.scope, member.substr(0, member.find('<')),
	                  LookupKind::Any) != nullptr)
	{
		return;
	}
	throw std::runtime_error(id.text + " is written on an object of a "
	                         "dependent type, where 14.2p4 reads it as a "
	                         "member that is not a template unless the "
	                         "`template` keyword says it is one");
}

void SemaAnalyzer::check_value_name(const AstNode& node, const Context& ctx)
{
	bool answered = false;
	SemaEntity* const named = definition_time_name(node, ctx, answered);
	if (!answered)
	{
		return;
	}
	if (named == nullptr)
	{
		throw std::runtime_error(node.text + " names nothing where the "
		                         "template that writes it is defined");
	}
	switch (named->kind)
	{
	case SemaKind::Class:
	case SemaKind::Enum:
	case SemaKind::Typedef:
	case SemaKind::TemplateType:
	case SemaKind::Namespace:
		// 5.1.1p8: an id-expression names a variable, a data member, a
		// function or an enumerator, and a type-name written where one of
		// those belongs names no value however the arguments come out.
		throw std::runtime_error(node.text + " names a type where the template "
		                         "that writes it uses it as a value");

	default:
		return;
	}
}
