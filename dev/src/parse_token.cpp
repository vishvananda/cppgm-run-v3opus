#include "parse_token.h"

#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

namespace
{

// The identifiers 2.11.2 makes context-sensitive keywords.  They stay
// identifiers, so the parser needs both the terminal and the spelling fact.
unsigned context_keyword_flags(const std::string& identifier)
{
	if (identifier == "override")
	{
		return kIsOverride;
	}
	if (identifier == "final")
	{
		return kIsFinal;
	}
	return 0;
}

// `ST_EMPTYSTR` and `ST_ZERO` are named by spelling, so they are read off the
// source spelling the tokenizer kept rather than off the analysed value: a
// concatenated sequence such as `"" ""` is empty but is not spelled `""`.
unsigned literal_spelling_flags(const std::string& source)
{
	if (source == "\"\"")
	{
		return kIsEmptyString;
	}
	if (source == "0")
	{
		return kIsZero;
	}
	return 0;
}

void append(std::vector<ParseToken>& out, unsigned type, unsigned flags)
{
	ParseToken token;
	token.type = static_cast<std::uint16_t>(type);
	token.flags = static_cast<std::uint16_t>(flags);
	out.push_back(token);
}

// One analysed token as one or two terminals.
void append_token(std::vector<ParseToken>& out, const PostToken& token)
{
	switch (token.kind)
	{
	case PostTokenKind::Simple:
		if (token.simple_type == OP_RSHIFT)
		{
			append(out, ST_RSHIFT_1, 0);
			append(out, ST_RSHIFT_2, 0);
			return;
		}
		append(out, token.simple_type, 0);
		return;

	case PostTokenKind::Identifier:
		append(out, TT_IDENTIFIER,
		       parse_token_name_flags(token.source) | context_keyword_flags(token.source));
		return;

	case PostTokenKind::Literal:
	case PostTokenKind::LiteralArray:
	case PostTokenKind::UserDefinedLiteral:
		append(out, TT_LITERAL, literal_spelling_flags(token.source));
		return;

	default:
		throw SourceError(" a token of the file is not a token of C++");
	}
}

}

unsigned parse_token_name_flags(const std::string& identifier)
{
	unsigned flags = 0;
	for (std::size_t index = 0; index < identifier.size(); ++index)
	{
		switch (identifier[index])
		{
		case 'C': flags |= kNameClass; break;
		case 'T': flags |= kNameTemplate; break;
		case 'Y': flags |= kNameTypedef; break;
		case 'E': flags |= kNameEnum; break;
		case 'N': flags |= kNameNamespace; break;
		default: break;
		}
	}
	return flags;
}

void build_parse_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                        const std::string& path, std::vector<ParseToken>& out)
{
	Preprocessor preprocessor(files, options, path);
	PostTokenizer tokenizer(preprocessor, CharacterLiterals::Language);

	PostToken token;
	while (tokenizer.next(token))
	{
		if (token.kind == PostTokenKind::EndOfFile)
		{
			break;
		}
		append_token(out, token);
	}
	append(out, ST_EOF, 0);
}
