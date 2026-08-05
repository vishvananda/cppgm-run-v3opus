#include "string_literal.h"

#include <cstring>

#include "source_charset.h"

namespace
{

const std::size_t kNoPart = static_cast<std::size_t>(-1);

void append_code_unit(std::string& data, unsigned long long value, std::size_t size)
{
	for (std::size_t index = 0; index < size; ++index)
	{
		data.push_back(static_cast<char>(value & 0xFF));
		value >>= 8;
	}
}

// The largest value one code unit of `size` bytes can hold.
unsigned long long code_unit_limit(std::size_t size)
{
	return (1ULL << (size * 8)) - 1;
}

// A raw-string body has no escape sequences: every character stands for
// itself, including a `\`.
std::size_t decode_raw_element(const std::string& text, std::size_t pos,
                               LiteralElement& element)
{
	element.numeric_escape = false;
	const Utf8Decoded decoded = decode_utf8(text.data() + pos, text.size() - pos);
	if (decoded.length == 0)
	{
		element.value = static_cast<unsigned char>(text[pos]);
		return pos + 1;
	}
	element.value = static_cast<unsigned long long>(decoded.code_point);
	return pos + decoded.length;
}

} // namespace

StringLiteralSequence::StringLiteralSequence()
{}

void StringLiteralSequence::add(const std::string& spelling)
{
	if (!source_.empty())
	{
		source_ += ' ';
	}
	const std::size_t base = source_.size();
	source_ += spelling;

	Part part;
	part.encoding = LiteralEncoding::Ordinary;
	part.raw = false;

	std::size_t pos = base;
	if (spelling[0] == 'u')
	{
		const bool utf8 = spelling.size() > 1 && spelling[1] == '8';
		part.encoding = utf8 ? LiteralEncoding::Utf8 : LiteralEncoding::Char16;
		pos += utf8 ? 2 : 1;
	}
	else if (spelling[0] == 'U')
	{
		part.encoding = LiteralEncoding::Char32;
		pos += 1;
	}
	else if (spelling[0] == 'L')
	{
		part.encoding = LiteralEncoding::Wide;
		pos += 1;
	}
	if (source_[pos] == 'R')
	{
		part.raw = true;
		++pos;
	}

	// A ud-suffix is an identifier, so the last `"` of the buffer is the one
	// that closes the literal, in a raw-string body as well as after one.
	const std::size_t close = source_.rfind('"');
	part.suffix_begin = close + 1;
	part.suffix_end = source_.size();
	if (part.raw)
	{
		const std::size_t open = source_.find('(', pos + 1);
		const std::size_t delimiter = open - (pos + 1);
		part.body_begin = open + 1;
		part.body_end = close - delimiter - 1;
	}
	else
	{
		part.body_begin = pos + 1;
		part.body_end = close;
	}
	parts_.push_back(part);
}

bool StringLiteralSequence::same_suffix(const Part& left, const Part& right) const
{
	const std::size_t size = left.suffix_end - left.suffix_begin;
	if (size != right.suffix_end - right.suffix_begin)
	{
		return false;
	}
	return std::memcmp(source_.data() + left.suffix_begin,
	                   source_.data() + right.suffix_begin, size) == 0;
}

// See 2.14.5.13 and 2.14.8.8, as the course defines them: the parts may name
// at most one encoding-prefix and at most one ud-suffix between them.
bool StringLiteralSequence::resolve(LiteralEncoding& encoding,
                                    std::size_t& suffix_part) const
{
	encoding = LiteralEncoding::Ordinary;
	suffix_part = kNoPart;
	bool encoded = false;
	for (std::size_t index = 0; index < parts_.size(); ++index)
	{
		const Part& part = parts_[index];
		if (part.encoding != LiteralEncoding::Ordinary)
		{
			if (!encoded)
			{
				encoding = part.encoding;
				encoded = true;
			}
			else if (part.encoding != encoding)
			{
				return false;
			}
		}
		if (part.suffix_begin == part.suffix_end)
		{
			continue;
		}
		if (source_[part.suffix_begin] != '_')
		{
			return false;
		}
		if (suffix_part == kNoPart)
		{
			suffix_part = index;
		}
		else if (!same_suffix(parts_[suffix_part], part))
		{
			return false;
		}
	}
	return true;
}

// The execution character set is UTF-8, so an ordinary or `u8` body is already
// encoded: only escape sequences are rewritten, and the text between them is
// copied a block at a time.
bool StringLiteralSequence::encode_narrow_part(const Part& part, std::string& data) const
{
	const char* base = source_.data();
	if (part.raw)
	{
		data.append(base + part.body_begin, part.body_end - part.body_begin);
		return true;
	}

	std::size_t pos = part.body_begin;
	while (pos < part.body_end)
	{
		const void* escape = std::memchr(base + pos, '\\', part.body_end - pos);
		const std::size_t stop = escape != 0
			? static_cast<std::size_t>(static_cast<const char*>(escape) - base)
			: part.body_end;
		data.append(base + pos, stop - pos);
		pos = stop;
		if (pos == part.body_end)
		{
			break;
		}
		LiteralElement element;
		pos = decode_literal_element(source_, pos, element);
		if (element.numeric_escape)
		{
			if (element.value > 0xFF)
			{
				return false;
			}
			data.push_back(static_cast<char>(element.value));
		}
		else
		{
			append_utf8(data, static_cast<int>(element.value));
		}
	}
	return true;
}

bool StringLiteralSequence::encode_wide_part(const Part& part, std::size_t unit_size,
                                             std::string& data) const
{
	const unsigned long long limit = code_unit_limit(unit_size);
	std::size_t pos = part.body_begin;
	while (pos < part.body_end)
	{
		LiteralElement element;
		pos = part.raw
			? decode_raw_element(source_, pos, element)
			: decode_literal_element(source_, pos, element);
		if (element.numeric_escape)
		{
			if (element.value > limit)
			{
				return false;
			}
		}
		else if (unit_size == 2 && element.value > 0xFFFF)
		{
			const unsigned long long value = element.value - 0x10000;
			append_code_unit(data, 0xD800 + (value >> 10), unit_size);
			append_code_unit(data, 0xDC00 + (value & 0x3FF), unit_size);
			continue;
		}
		append_code_unit(data, element.value, unit_size);
	}
	return true;
}

bool StringLiteralSequence::encode_part(const Part& part, std::size_t unit_size,
                                        std::string& data) const
{
	return unit_size == 1
		? encode_narrow_part(part, data)
		: encode_wide_part(part, unit_size, data);
}

void StringLiteralSequence::take_source(PostToken& token)
{
	token.source.swap(source_);
	source_.clear();
	parts_.clear();
}

void StringLiteralSequence::build(PostToken& token)
{
	token.reset(PostTokenKind::Invalid);

	LiteralEncoding encoding = LiteralEncoding::Ordinary;
	std::size_t suffix_part = kNoPart;
	if (!resolve(encoding, suffix_part))
	{
		take_source(token);
		return;
	}

	const std::size_t unit_size = literal_code_unit_size(encoding);
	std::string& data = token.data;
	for (std::size_t index = 0; index < parts_.size(); ++index)
	{
		if (!encode_part(parts_[index], unit_size, data))
		{
			data.clear();
			take_source(token);
			return;
		}
	}
	append_code_unit(data, 0, unit_size);

	token.type = literal_element_type(encoding);
	token.element_count = data.size() / unit_size;
	if (suffix_part == kNoPart)
	{
		token.kind = PostTokenKind::LiteralArray;
	}
	else
	{
		const Part& part = parts_[suffix_part];
		token.kind = PostTokenKind::UserDefinedLiteral;
		token.ud_kind = UserDefinedLiteralKind::String;
		token.ud_suffix.assign(source_, part.suffix_begin,
		                       part.suffix_end - part.suffix_begin);
	}
	take_source(token);
}

bool StringLiteralSequence::is_reserved_empty_suffix() const
{
	if (parts_.size() != 1)
	{
		return false;
	}
	const Part& part = parts_[0];
	return !part.raw &&
		part.encoding == LiteralEncoding::Ordinary &&
		part.body_begin == part.body_end &&
		part.suffix_begin != part.suffix_end &&
		source_[part.suffix_begin] != '_';
}

void StringLiteralSequence::build_literal_operator_id(PostToken& token,
                                                      std::string& identifier)
{
	const Part& part = parts_[0];
	identifier.assign(source_, part.suffix_begin, part.suffix_end - part.suffix_begin);

	token.reset(PostTokenKind::LiteralArray);
	token.source.assign("\"\"");
	token.type = FT_CHAR;
	token.element_count = 1;
	token.data.assign(1, '\0');
	source_.clear();
	parts_.clear();
}
