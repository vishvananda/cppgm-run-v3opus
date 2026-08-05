#include "string_literal.h"

#include <cstring>

#include "source_charset.h"

namespace
{

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

} // namespace

StringLiteralSequence::StringLiteralSequence()
{
	clear();
}

void StringLiteralSequence::add(const std::string& spelling)
{
	if (!source_.empty())
	{
		source_ += ' ';
	}
	const std::size_t base = source_.size();
	source_ += spelling;

	Part part;
	part.raw = false;
	LiteralEncoding encoding = LiteralEncoding::Ordinary;

	std::size_t pos = base;
	if (spelling[0] == 'u')
	{
		const bool utf8 = spelling.size() > 1 && spelling[1] == '8';
		encoding = utf8 ? LiteralEncoding::Utf8 : LiteralEncoding::Char16;
		pos += utf8 ? 2 : 1;
	}
	else if (spelling[0] == 'U')
	{
		encoding = LiteralEncoding::Char32;
		pos += 1;
	}
	else if (spelling[0] == 'L')
	{
		encoding = LiteralEncoding::Wide;
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

	note_encoding(encoding);
	note_suffix(close + 1, source_.size());
}

void StringLiteralSequence::note_encoding(LiteralEncoding encoding)
{
	if (encoding == LiteralEncoding::Ordinary)
	{
		return;
	}
	if (!prefixed_)
	{
		encoding_ = encoding;
		prefixed_ = true;
	}
	else if (encoding != encoding_)
	{
		conflict_ = true;
	}
}

void StringLiteralSequence::note_suffix(std::size_t begin, std::size_t end)
{
	if (begin == end)
	{
		return;
	}
	if (suffix_begin_ == suffix_end_)
	{
		// The first ud-suffix of the sequence, which is the one the token
		// takes.  A reserved one is still recorded, because after `operator`
		// it is the identifier of a literal-operator-id.
		suffix_begin_ = begin;
		suffix_end_ = end;
	}
	else if (end - begin != suffix_end_ - suffix_begin_ ||
	         std::memcmp(source_.data() + begin, source_.data() + suffix_begin_,
	                     end - begin) != 0)
	{
		conflict_ = true;
	}
	if (source_[begin] != '_')
	{
		conflict_ = true;
	}
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
		// A raw-string body has no escape sequences: every character stands for
		// itself, including a `\`.
		LiteralElement element;
		pos = part.raw
			? decode_source_character(source_, pos, element)
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

void StringLiteralSequence::clear()
{
	source_.clear();
	parts_.clear();
	encoding_ = LiteralEncoding::Ordinary;
	prefixed_ = false;
	conflict_ = false;
	suffix_begin_ = 0;
	suffix_end_ = 0;
}

void StringLiteralSequence::take_source(PostToken& token)
{
	token.source.swap(source_);
	clear();
}

void StringLiteralSequence::build(PostToken& token)
{
	token.reset(PostTokenKind::Invalid);
	if (conflict_)
	{
		take_source(token);
		return;
	}

	const std::size_t unit_size = literal_code_unit_size(encoding_);
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

	token.type = literal_element_type(encoding_);
	token.element_count = data.size() / unit_size;
	if (suffix_begin_ == suffix_end_)
	{
		token.kind = PostTokenKind::LiteralArray;
	}
	else
	{
		token.kind = PostTokenKind::UserDefinedLiteral;
		token.ud_kind = UserDefinedLiteralKind::String;
		token.ud_suffix.assign(source_, suffix_begin_, suffix_end_ - suffix_begin_);
	}
	take_source(token);
}

bool StringLiteralSequence::is_reserved_empty_suffix() const
{
	if (parts_.size() != 1)
	{
		return false;
	}
	return !parts_[0].raw &&
		!prefixed_ &&
		parts_[0].body_begin == parts_[0].body_end &&
		suffix_begin_ != suffix_end_ &&
		source_[suffix_begin_] != '_';
}

void StringLiteralSequence::build_literal_operator_id(PostToken& token,
                                                      std::string& identifier)
{
	identifier.assign(source_, suffix_begin_, suffix_end_ - suffix_begin_);

	token.reset(PostTokenKind::LiteralArray);
	token.source.assign("\"\"");
	token.type = FT_CHAR;
	token.element_count = 1;
	token.data.assign(1, '\0');
	clear();
}
