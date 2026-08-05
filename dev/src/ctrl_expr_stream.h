#pragma once

#include <cstddef>
#include <iostream>
#include <ostream>
#include <string>

#include "ctrl_expr.h"

// The PA3 output format: one line per non-empty logical line, holding either
// the value of its controlling expression or `error`, then `eof`.
//
// Lines are gathered into a buffer and handed to the stream a block at a time,
// so a line costs an append rather than a formatted insertion.  The buffer is
// flushed from the destructor, so a file that is ill-formed in phases 1 to 3
// still reports the lines that were complete before it.
struct CtrlExprResultStream
{
	explicit CtrlExprResultStream(std::ostream& out = std::cout)
		: out_(&out)
	{}

	~CtrlExprResultStream()
	{
		flush();
	}

	// A decimal, with the suffix `u` when the value is unsigned.
	void emit(CtrlExprValue value)
	{
		unsigned long long magnitude = value.bits;
		if (value.is_signed && static_cast<long long>(value.bits) < 0)
		{
			buffer_ += '-';
			// Negating the pattern is also correct for the most negative
			// value, whose magnitude no signed type holds.
			magnitude = 0ULL - value.bits;
		}
		append_decimal(magnitude);
		if (!value.is_signed)
		{
			buffer_ += 'u';
		}
		end_line();
	}

	void emit_error()
	{
		buffer_ += "error";
		end_line();
	}

	void emit_eof()
	{
		buffer_ += "eof";
		end_line();
		flush();
		out_->flush();
	}

private:
	static const std::size_t kBlockSize = 1 << 16;

	void append_decimal(unsigned long long value)
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
