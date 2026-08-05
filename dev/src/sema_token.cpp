#include "sema_token.h"

#include <cstring>

#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

namespace
{

void append(std::vector<SemaToken>& out, unsigned type, NameId name)
{
	SemaToken token;
	token.type = type;
	token.name = name;
	out.push_back(token);
}

// The literal's index in the pool.  A user-defined literal is kept as a hole,
// because `pa8.gram` gives it no meaning but the token it stands for still has
// to be counted.
NameId keep_literal(std::vector<LiteralValue>& literals, const PostToken& token,
                    bool array, bool defined)
{
	const NameId index = static_cast<NameId>(literals.size());
	literals.push_back(LiteralValue());
	LiteralValue& value = literals.back();
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
                  std::vector<LiteralValue>& literals)
{
	switch (token.kind)
	{
	case PostTokenKind::Simple:
		append(out, token.simple_type, kNoName);
		return;

	case PostTokenKind::Literal:
		append(out, TT_LITERAL, keep_literal(literals, token, false, true));
		return;

	case PostTokenKind::LiteralArray:
		append(out, TT_LITERAL, keep_literal(literals, token, true, true));
		return;

	case PostTokenKind::UserDefinedLiteral:
		append(out, TT_LITERAL, keep_literal(literals, token, false, false));
		return;

	case PostTokenKind::Identifier:
		append(out, TT_IDENTIFIER, names.intern(token.source));
		return;

	default:
		throw SourceError(" a token of the file is not a token of C++");
	}
}

}

unsigned long long LiteralValue::integer() const
{
	const std::size_t size = data.size() < sizeof(unsigned long long)
		? data.size()
		: sizeof(unsigned long long);
	unsigned long long value = 0;
	for (std::size_t index = size; index-- > 0; )
	{
		value = (value << 8) | static_cast<unsigned char>(data[index]);
	}
	const std::size_t bits = size * 8;
	if (bits != 0 && bits < 64 && fundamental_type_is_signed(type) &&
	    (value >> (bits - 1)) != 0)
	{
		value |= ~((1ULL << bits) - 1);
	}
	return value;
}

long double LiteralValue::real() const
{
	if (type == FT_FLOAT && data.size() >= sizeof(float))
	{
		float value = 0;
		std::memcpy(&value, data.data(), sizeof(value));
		return value;
	}
	if (type == FT_DOUBLE && data.size() >= sizeof(double))
	{
		double value = 0;
		std::memcpy(&value, data.data(), sizeof(value));
		return value;
	}
	long double value = 0;
	const std::size_t width = data.size() < sizeof(value) ? data.size() : sizeof(value);
	std::memcpy(&value, data.data(), width);
	return value;
}

void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out, std::vector<LiteralValue>& literals)
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
	append(out, ST_EOF, kNoName);
}
