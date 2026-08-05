// (C) 2013 CPPGM Foundation. All Rights Reserved. www.cppgm.org

#pragma once

#include <cstddef>
#include <iostream>
#include <string>

#include "IPPTokenStream.h"

// One line per preprocessing-token, in the format the assignment specifies.
//
// Lines are gathered into a buffer and handed to std::cout a block at a time:
// a token is only a few dozen bytes, and a formatted insertion per token costs
// more than the tokenizing does.
struct DebugPPTokenStream : IPPTokenStream
{
	~DebugPPTokenStream()
	{
		flush();
	}

	void emit_whitespace_sequence()
	{
		write_line("whitespace-sequence 0 ");
	}

	void emit_new_line()
	{
		write_line("new-line 0 ");
	}

	void emit_header_name(const std::string& data)
	{
		write_token("header-name", data);
	}

	void emit_identifier(const std::string& data)
	{
		write_token("identifier", data);
	}

	void emit_pp_number(const std::string& data)
	{
		write_token("pp-number", data);
	}

	void emit_character_literal(const std::string& data)
	{
		write_token("character-literal", data);
	}

	void emit_user_defined_character_literal(const std::string& data)
	{
		write_token("user-defined-character-literal", data);
	}

	void emit_string_literal(const std::string& data)
	{
		write_token("string-literal", data);
	}

	void emit_user_defined_string_literal(const std::string& data)
	{
		write_token("user-defined-string-literal", data);
	}

	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		write_token("preprocessing-op-or-punc", data);
	}

	void emit_non_whitespace_char(const std::string& data)
	{
		write_token("non-whitespace-character", data);
	}

	void emit_eof()
	{
		write_line("eof");
		flush();
		std::cout.flush();
	}

private:

	static const std::size_t kBlockSize = 1 << 16;

	// The token type is always a literal, so its length is known here and the
	// buffer never has to measure it.
	template <std::size_t N>
	void write_line(const char (&text)[N])
	{
		buffer_.append(text, N - 1);
		buffer_ += '\n';
		flush_block();
	}

	template <std::size_t N>
	void write_token(const char (&type)[N], const std::string& data)
	{
		buffer_.append(type, N - 1);
		buffer_ += ' ';
		append_decimal(data.size());
		buffer_ += ' ';
		buffer_ += data;
		buffer_ += '\n';
		flush_block();
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

	void flush_block()
	{
		if (buffer_.size() >= kBlockSize)
		{
			flush();
		}
	}

	void flush()
	{
		if (!buffer_.empty())
		{
			std::cout.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
			buffer_.clear();
		}
	}

	std::string buffer_;
};
