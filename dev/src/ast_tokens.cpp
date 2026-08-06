#include "ast_tokens.h"

#include <cctype>
#include <utility>

#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

namespace
{

// True when `text` starts, or ends, with a character an identifier is made of.
// Two tokens need a separator between them exactly when the first ends and the
// second starts with one, which is what keeps `const From*` apart while
// leaving `sizeof(T)&&true` closed up.
bool starts_word(const std::string& text)
{
	const unsigned char first = text.empty() ? ' ' : text[0];
	return std::isalnum(first) != 0 || first == '_';
}

bool ends_word(const std::string& text)
{
	const unsigned char last = text.empty() ? ' ' : text[text.size() - 1];
	return std::isalnum(last) != 0 || last == '_';
}

}

const char* ast_token_type_name(unsigned type)
{
	switch (type)
	{
	case TT_IDENTIFIER: return "TT_IDENTIFIER";
	case TT_LITERAL: return "TT_LITERAL";
	case ST_EOF: return "ST_EOF";
	case ST_RSHIFT_1: return "ST_RSHIFT_1";
	case ST_RSHIFT_2: return "ST_RSHIFT_2";
	default: break;
	}
	return token_type_name(static_cast<ETokenType>(type));
}

AstTokenStream::AstTokenStream()
{
	intern(std::string());
}

std::uint32_t AstTokenStream::intern(const std::string& text)
{
	const std::unordered_map<std::string, std::uint32_t>::const_iterator found =
		interned_.find(text);
	if (found != interned_.end())
	{
		return found->second;
	}
	const std::uint32_t index = static_cast<std::uint32_t>(pool_.size());
	pool_.push_back(text);
	interned_.insert(std::make_pair(text, index));
	return index;
}

void AstTokenStream::append(unsigned type, const std::string& text)
{
	AstToken token;
	token.type = static_cast<std::uint16_t>(type);
	token.spelling = intern(text);
	tokens_.push_back(token);
}

std::string AstTokenStream::flatten(std::size_t begin, std::size_t end) const
{
	std::string result;
	for (std::size_t index = begin; index < end && index < tokens_.size(); ++index)
	{
		const std::string& text = spelling(index);
		if (!result.empty() && ends_word(result) && starts_word(text))
		{
			result.push_back(' ');
		}
		result.append(text);
	}
	return result;
}

void AstTokenStream::build(SourceFileTable& files, const PreprocessorOptions& options,
                           const std::string& path)
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
		switch (token.kind)
		{
		case PostTokenKind::Simple:
			if (token.simple_type == OP_RSHIFT)
			{
				append(ST_RSHIFT_1, ">");
				append(ST_RSHIFT_2, ">");
				break;
			}
			append(token.simple_type, token.source);
			break;

		case PostTokenKind::Identifier:
			append(TT_IDENTIFIER, token.source);
			break;

		case PostTokenKind::Literal:
		case PostTokenKind::LiteralArray:
		case PostTokenKind::UserDefinedLiteral:
			append(TT_LITERAL, token.source);
			break;

		default:
			throw SourceError(" a token of the file is not a token of C++");
		}
	}
	append(ST_EOF, std::string());
}
