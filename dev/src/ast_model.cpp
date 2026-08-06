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
