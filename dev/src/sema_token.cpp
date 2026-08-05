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

// The literal's index in the pool, or `kNoName` when no pool was asked for.
NameId keep_literal(std::vector<LiteralValue>* literals, const PostToken& token,
                    bool array, bool defined)
{
	if (literals == nullptr)
	{
		return kNoName;
	}
	const NameId index = static_cast<NameId>(literals->size());
	literals->push_back(LiteralValue());
	LiteralValue& value = literals->back();
	if (defined)
	{
		value.type = token.type;
		value.array = array;
		value.elements = static_cast<std::uint32_t>(token.element_count);
		value.data = token.data;
	}
	return index;
}

void append_token(std::vector<SemaToken>& out, NameTable& names, const PostToken& token,
                  std::vector<LiteralValue>* literals)
{
	switch (token.kind)
	{
	case PostTokenKind::Simple:
		append(out, token.simple_type, kNoName, false, 0);
		return;

	case PostTokenKind::Literal:
		// 2.14.2 and 2.14.3 have already decided the value of an integral or
		// character literal, which is the one an array bound can use and the
		// only fact PA7 reads back.
		if (fundamental_type_is_integral(token.type))
		{
			append(out, TT_LITERAL, keep_literal(literals, token, false, true), true,
			       token.integer_value());
			return;
		}
		append(out, TT_LITERAL, keep_literal(literals, token, false, true), false, 0);
		return;

	case PostTokenKind::LiteralArray:
		append(out, TT_LITERAL, keep_literal(literals, token, true, true), false, 0);
		return;

	case PostTokenKind::UserDefinedLiteral:
		append(out, TT_LITERAL, keep_literal(literals, token, false, false), false, 0);
		return;

	case PostTokenKind::Identifier:
		append(out, TT_IDENTIFIER, names.intern(token.source), false, 0);
		return;

	default:
		throw SourceError(" a token of the file is not a token of C++");
	}
}

}

void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out, std::vector<LiteralValue>* literals)
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
		append_token(out, names, token, literals);
	}
	append(out, ST_EOF, kNoName, false, 0);
}
