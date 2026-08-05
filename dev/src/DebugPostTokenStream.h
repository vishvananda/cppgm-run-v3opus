#pragma once

#include <cstddef>
#include <iostream>
#include <ostream>
#include <string>

#include "post_token.h"

// One line per token, in the format the assignment specifies.
//
// PA5 writes the same lines to an outfile rather than to standard output, so
// the destination is a constructor argument.  Lines are gathered into a buffer
// and handed to the stream a block at a time, so the cost of a token is an
// append rather than a formatted insertion.
struct DebugPostTokenStream
{
	explicit DebugPostTokenStream(std::ostream& out = std::cout)
		: out_(&out)
	{}

	~DebugPostTokenStream()
	{
		flush();
	}

	void emit(const PostToken& token)
	{
		switch (token.kind)
		{
		case PostTokenKind::Invalid:
			write_text("invalid", token.source);
			break;
		case PostTokenKind::Identifier:
			write_text("identifier", token.source);
			break;
		case PostTokenKind::Simple:
			write_text("simple", token.source);
			append_field(token_type_name(token.simple_type));
			break;
		case PostTokenKind::Literal:
			write_text("literal", token.source);
			append_value(token);
			break;
		case PostTokenKind::LiteralArray:
			write_text("literal", token.source);
			append_array_value(token);
			break;
		case PostTokenKind::UserDefinedLiteral:
			write_text("user-defined-literal", token.source);
			append_field(token.ud_suffix);
			append_user_defined_value(token);
			break;
		case PostTokenKind::EndOfFile:
			buffer_ += "eof";
			end_line();
			flush();
			out_->flush();
			return;
		}
		end_line();
	}

private:

	static const std::size_t kBlockSize = 1 << 16;

	// The token category is always a literal, so its length is known here.
	template <std::size_t N>
	void write_text(const char (&category)[N], const std::string& source)
	{
		buffer_.append(category, N - 1);
		buffer_ += ' ';
		buffer_ += source;
	}

	void append_field(const std::string& text)
	{
		buffer_ += ' ';
		buffer_ += text;
	}

	void append_field(const char* text)
	{
		buffer_ += ' ';
		buffer_ += text;
	}

	void append_value(const PostToken& token)
	{
		append_field(fundamental_type_name(token.type));
		buffer_ += ' ';
		append_hex(token.data);
	}

	void append_array_value(const PostToken& token)
	{
		buffer_ += " array of ";
		append_decimal(token.element_count);
		append_value(token);
	}

	void append_user_defined_value(const PostToken& token)
	{
		switch (token.ud_kind)
		{
		case UserDefinedLiteralKind::Integer:
			buffer_ += " integer ";
			buffer_.append(token.source, 0, token.ud_prefix_size);
			return;
		case UserDefinedLiteralKind::Floating:
			buffer_ += " floating ";
			buffer_.append(token.source, 0, token.ud_prefix_size);
			return;
		case UserDefinedLiteralKind::Character:
			buffer_ += " character";
			append_value(token);
			return;
		default:
			buffer_ += " string";
			append_array_value(token);
			return;
		}
	}

	// The buffer is sized for the whole hexdump once and then written in
	// place, because a literal's object representation can be as large as the
	// literal and its hexdump is twice that again.
	void append_hex(const std::string& data)
	{
		static const char kDigits[] = "0123456789ABCDEF";
		const std::size_t start = buffer_.size();
		buffer_.resize(start + 2 * data.size());
		char* out = &buffer_[start];
		for (std::size_t index = 0; index < data.size(); ++index)
		{
			const unsigned char byte = static_cast<unsigned char>(data[index]);
			out[2 * index] = kDigits[byte >> 4];
			out[2 * index + 1] = kDigits[byte & 0x0F];
		}
	}

	void append_decimal(std::size_t value)
	{
		char digits[24];
		std::size_t length = 0;
		do
		{
			digits[length++] = static_cast<char>('0' + value % 10);
			value /= 10;
		}
		while (value != 0);
		while (length != 0)
		{
			buffer_ += digits[--length];
		}
	}

	void end_line()
	{
		buffer_ += '\n';
		if (buffer_.size() >= kBlockSize)
		{
			flush();
		}
	}

	void flush()
	{
		if (!buffer_.empty())
		{
			out_->write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
			buffer_.clear();
		}
	}

	std::ostream* out_;
	std::string buffer_;
};
