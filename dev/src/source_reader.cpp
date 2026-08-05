#include "source_reader.h"

#include <utility>

#include "source_charset.h"

namespace
{

// See C++ standard 2.4 Trigraph sequences.
int trigraph_replacement(char third)
{
	switch (third)
	{
	case '=': return '#';
	case '/': return '\\';
	case '\'': return '^';
	case '(': return '[';
	case ')': return ']';
	case '!': return '|';
	case '<': return '{';
	case '>': return '}';
	case '-': return '~';
	default: return 0;
	}
}

} // namespace

const std::size_t SourceReader::kLookaheadCapacity;
const std::size_t SourceReader::kMaxRunLength;
const std::size_t SourceReader::kBufferCapacity;

SourceReader::SourceReader(std::string bytes)
	: bytes_(std::move(bytes))
	, cursor_(0)
	, head_(0)
	, count_(0)
	, raw_mode_(false)
{
	// A leading UTF-8 byte order mark is an encoding signature rather than a
	// source character, so it never reaches the token grammar.
	static const char kByteOrderMark[] = "\xEF\xBB\xBF";
	if (bytes_.compare(0, 3, kByteOrderMark, 3) == 0)
	{
		bytes_.erase(0, 3);
	}
}

int SourceReader::peek(std::size_t ahead)
{
	fill(ahead + 1);
	if (ahead >= count_)
	{
		return kEndOfFile;
	}
	return lookahead_[(head_ + ahead) % kBufferCapacity].code_point;
}

void SourceReader::advance()
{
	fill(1);
	if (count_ == 0)
	{
		return;
	}
	const Fetched& front = lookahead_[head_];
	if (front.code_point == kEndOfFile)
	{
		return;
	}
	if (front.invalid_escape_value)
	{
		throw SourceError(position_text() + ":Invalid unicode escape value");
	}
	head_ = (head_ + 1) % kBufferCapacity;
	--count_;
}

std::size_t SourceReader::mark() const
{
	return count_ == 0 ? cursor_ : lookahead_[head_].begin;
}

void SourceReader::rewind(std::size_t offset)
{
	cursor_ = offset;
	head_ = 0;
	count_ = 0;
}

bool SourceReader::raw_mode() const
{
	return raw_mode_;
}

void SourceReader::set_raw_mode(bool raw)
{
	if (raw == raw_mode_)
	{
		return;
	}
	rewind(mark());
	raw_mode_ = raw;
}

std::string SourceReader::position_text() const
{
	return position_text_at(mark());
}

void SourceReader::fill(std::size_t wanted)
{
	if (wanted > kLookaheadCapacity)
	{
		wanted = kLookaheadCapacity;
	}
	while (count_ < wanted)
	{
		if (count_ > 0)
		{
			const std::size_t last = (head_ + count_ - 1) % kBufferCapacity;
			if (lookahead_[last].code_point == kEndOfFile)
			{
				return;
			}
		}
		if (raw_mode_)
		{
			push(decoded_at(cursor_));
			continue;
		}
		const Fetched next = trigraph_at(cursor_);
		if (next.code_point != '\\')
		{
			push(next);
			continue;
		}
		push_backslash_run();
	}
}

void SourceReader::push(const Fetched& fetched)
{
	lookahead_[(head_ + count_) % kBufferCapacity] = fetched;
	++count_;
	cursor_ = fetched.end;
}

std::string SourceReader::position_text_at(std::size_t offset) const
{
	std::size_t line = 1;
	std::size_t column = 1;
	std::size_t index = 0;
	while (index < offset && index < bytes_.size())
	{
		const Utf8Decoded decoded = decode_utf8(bytes_.data() + index, bytes_.size() - index);
		if (bytes_[index] == '\n')
		{
			++line;
			column = 1;
		}
		else
		{
			++column;
		}
		index += decoded.length == 0 ? 1 : decoded.length;
	}
	return std::to_string(line) + ":" + std::to_string(column);
}

void SourceReader::push_backslash_run()
{
	std::size_t offset = cursor_;
	while (true)
	{
		const Fetched backslash = trigraph_at(offset);
		if (backslash.code_point != '\\')
		{
			push(backslash);
			return;
		}
		const Fetched marker = trigraph_at(backslash.end);
		if (marker.code_point == '\n')
		{
			offset = marker.end;
			continue;
		}
		push_universal_character_name(backslash, marker);
		return;
	}
}

void SourceReader::push_universal_character_name(const Fetched& backslash, const Fetched& marker)
{
	int digits = 0;
	if (marker.code_point == 'u')
	{
		digits = 4;
	}
	else if (marker.code_point == 'U')
	{
		digits = 8;
	}

	Fetched spelled[8];
	std::size_t spelled_count = 0;
	unsigned long value = 0;
	std::size_t position = marker.end;
	bool complete = digits != 0;
	for (int index = 0; index < digits; ++index)
	{
		const Fetched digit = trigraph_at(position);
		if (!is_hex_digit(digit.code_point))
		{
			// The character that ends a malformed name is spelled out with it,
			// so it cannot go on to start a line splice of its own.
			if (digit.code_point != kEndOfFile)
			{
				spelled[spelled_count++] = digit;
			}
			complete = false;
			break;
		}
		spelled[spelled_count++] = digit;
		value = value * 16 + static_cast<unsigned long>(hex_digit_value(digit.code_point));
		position = digit.end;
	}

	if (complete)
	{
		// A syntactically complete name that designates no Unicode scalar value
		// is reported when it is consumed, not when it is merely looked at:
		// lookahead can reach past the start of an untranslated raw string.
		Fetched name = { '\\', backslash.begin, position, true };
		if (value <= static_cast<unsigned long>(kMaxCodePoint) &&
		    is_valid_code_point(static_cast<int>(value)))
		{
			name.code_point = static_cast<int>(value);
			name.invalid_escape_value = false;
		}
		push(name);
		return;
	}

	push(backslash);
	if (marker.code_point != kEndOfFile)
	{
		push(marker);
	}
	for (std::size_t index = 0; index < spelled_count; ++index)
	{
		push(spelled[index]);
	}
}

SourceReader::Fetched SourceReader::decoded_at(std::size_t offset) const
{
	Fetched result = { kEndOfFile, offset, offset, false };
	if (offset > bytes_.size())
	{
		return result;
	}
	if (offset == bytes_.size())
	{
		// Phase 2 appends a new-line to a non-empty file that lacks one.
		if (bytes_.empty() || bytes_[bytes_.size() - 1] == '\n')
		{
			return result;
		}
		result.code_point = '\n';
		result.end = offset + 1;
		return result;
	}

	const Utf8Decoded decoded = decode_utf8(bytes_.data() + offset, bytes_.size() - offset);
	if (decoded.length == 0)
	{
		throw SourceError(position_text_at(offset) + ":Invalid utf-8 character");
	}
	result.code_point = decoded.code_point;
	result.end = offset + decoded.length;
	return result;
}

SourceReader::Fetched SourceReader::trigraph_at(std::size_t offset) const
{
	const Fetched first = decoded_at(offset);
	if (first.code_point != '?' || offset + 2 >= bytes_.size())
	{
		return first;
	}
	if (bytes_[offset + 1] != '?')
	{
		return first;
	}
	const int replacement = trigraph_replacement(bytes_[offset + 2]);
	if (replacement == 0)
	{
		return first;
	}
	const Fetched result = { replacement, offset, offset + 3, false };
	return result;
}
