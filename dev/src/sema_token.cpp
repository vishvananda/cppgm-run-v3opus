#include "sema_token.h"

#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

namespace
{

void append(std::vector<SemaToken>& out, unsigned type, NameId name, bool integral,
            unsigned long long value)
{
	SemaToken token;
	token.type = static_cast<std::uint16_t>(type);
	token.integral = integral;
	token.name = name;
	token.value = value;
	out.push_back(token);
}

void append_token(std::vector<SemaToken>& out, NameTable& names, const PostToken& token)
{
	switch (token.kind)
	{
	case PostTokenKind::Simple:
		append(out, token.simple_type, kNoName, false, 0);
		return;

	case PostTokenKind::Identifier:
		append(out, TT_IDENTIFIER, names.intern(token.source), false, 0);
		return;

	case PostTokenKind::Literal:
		// A `constant-expression` of the assignment is a literal, and the only
		// one an array bound can use is an integral or character one, whose
		// value 2.14.3 and 2.14.2 have already decided by this point.
		if (fundamental_type_is_integral(token.type))
		{
			append(out, TT_LITERAL, kNoName, true, token.integer_value());
			return;
		}
		append(out, TT_LITERAL, kNoName, false, 0);
		return;

	case PostTokenKind::LiteralArray:
	case PostTokenKind::UserDefinedLiteral:
		append(out, TT_LITERAL, kNoName, false, 0);
		return;

	default:
		throw SourceError(" a token of the file is not a token of C++");
	}
}

}

void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out)
{
	Preprocessor preprocessor(files, options, path);
	PostTokenizer tokenizer(preprocessor);

	PostToken token;
	while (tokenizer.next(token))
	{
		if (token.kind == PostTokenKind::EndOfFile)
		{
			break;
		}
		append_token(out, names, token);
	}
	append(out, ST_EOF, kNoName, false, 0);
}
