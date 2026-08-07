#include "ast_model.h"

#include <ostream>

#include "ast_tokens.h"

namespace
{

const char* const kAstKindNames[] = {
#define CPPGM_AST_KIND_NAME(name, text) text,
	CPPGM_AST_KINDS(CPPGM_AST_KIND_NAME)
#undef CPPGM_AST_KIND_NAME
};

}

const char* ast_kind_name(AstKind kind)
{
	return kAstKindNames[static_cast<unsigned>(kind)];
}

void write_ast(std::ostream& out, const AstNode& root, unsigned depth)
{
	if (root.kind == AstKind::CarriedExpression)
	{
		// 7.1.6.2p1: the expression a decltype-specifier written before `::`
		// holds is kept for the semantic layer that has to answer which region
		// it names, because no spelling holds it.  The dump writes the name as
		// the grammar left it - one component of a nested-name-specifier - so
		// the expression under it is a fact carried beside the syntax and not a
		// node of the syntax this dump describes.
		return;
	}
	if (root.kind == AstKind::CarriedTypeId)
	{
		// 12.3.2p1: the conversion-type-id a conversion function's name is
		// written from is a type, and only a semantic layer can say which one -
		// two spellings of one type name one function, and a typedef-name is
		// the usual way to write the second.  The dump writes the name as the
		// grammar flattened it, so the type-id under it is a fact carried
		// beside the syntax and not a node of the syntax this dump describes.
		return;
	}
	if (root.kind == AstKind::AlignmentSpecifier)
	{
		// 7.6.1 and 7.6.2: the syntax accepts an attribute wherever the grammar
		// writes one and the PA10 tree describes none of them.  The alignment a
		// class-head asked for is kept for the layout that has to answer for it
		// (7.6.2p5), which is a fact about the class and not a node of the
		// syntax the dump describes.
		return;
	}
	out << std::string(depth * 2, ' ') << ast_kind_name(root.kind);
	if (root.token != kNoAstToken)
	{
		out << ' ' << ast_token_type_name(root.token) << ':' << root.text;
	}
	else if (!root.text.empty())
	{
		out << ' ' << root.text;
	}
	out << '\n';
	for (std::size_t index = 0; index < root.children.size(); ++index)
	{
		write_ast(out, *root.children[index], depth + 1);
	}
}
