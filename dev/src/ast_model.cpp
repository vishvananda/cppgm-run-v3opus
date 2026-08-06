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
