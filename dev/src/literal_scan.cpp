#include "literal_scan.h"

#include <cstdlib>
#include <cstring>

#include "source_charset.h"

namespace
{

const unsigned long long kMaxUnsignedValue = ~0ULL;

// The assignment requires floating-literals to be scanned through these three
// functions, so that the output is bit-identical to the reference toolchain.
//
// They are the conversion the starter code's `istringstream >> x` performs:
// libstdc++ assembles the field and hands it to exactly these functions, and
// on 400k random in-range literals of each type the two agree bit for bit.
// They are used directly because the stream form no longer agrees on a value
// outside the range of its type: a current library reports the failure by
// storing the largest representable value, where the reference implementation
// and these functions both store an infinity.
//
// The whole spelling is handed over rather than a copy of the part before the
// floating-suffix: a suffix that reached here is one of `f`, `F`, `l` and `L`,
// none of which belongs to the C floating constant these functions scan, so
// each stops on it of its own accord.
float PA2Decode_float(const char* s)
{
	return std::strtof(s, 0);
}

double PA2Decode_double(const char* s)
{
	return std::strtod(s, 0);
}

long double PA2Decode_long_double(const char* s)
{
	return std::strtold(s, 0);
}

// See 2.14.3 Table 7.
int simple_escape_value(int code_point)
{
	switch (code_point)
	{
	case 'a': return 0x07;
	case 'b': return 0x08;
	case 'f': return 0x0C;
	case 'n': return 0x0A;
	case 'r': return 0x0D;
	case 't': return 0x09;
	case 'v': return 0x0B;
	default: return code_point;
	}
}

// What the numeric part of a pp-number is, before its suffix is considered.
struct NumericShape
{
	bool valid;
	bool floating;
	unsigned int base;
	std::size_t digits_begin;
	std::size_t suffix_begin;
};

// Scans the leading integer-literal or floating-literal.  The two grammars
// share a prefix, so one pass decides between them: what makes a pp-number
// floating is a `.` or an exponent, and an `e` is only an exponent when a
// digit, with an optional sign before it, follows.
NumericShape scan_numeric_shape(const std::string& text)
{
	NumericShape shape = { false, false, 10, 0, 0 };
	const std::size_t size = text.size();

	if (size >= 3 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
	    is_hex_digit(text[2]))
	{
		std::size_t index = 2;
		while (index < size && is_hex_digit(text[index]))
		{
			++index;
		}
		shape.valid = true;
		shape.base = 16;
		shape.digits_begin = 2;
		shape.suffix_begin = index;
		return shape;
	}

	std::size_t index = 0;
	std::size_t leading_digits = 0;
	while (index < size && is_digit(text[index]))
	{
		++index;
		++leading_digits;
	}
	std::size_t fraction_digits = 0;
	if (index < size && text[index] == '.')
	{
		shape.floating = true;
		++index;
		while (index < size && is_digit(text[index]))
		{
			++index;
			++fraction_digits;
		}
	}
	if (index < size && (text[index] == 'e' || text[index] == 'E'))
	{
		std::size_t exponent = index + 1;
		if (exponent < size && (text[exponent] == '+' || text[exponent] == '-'))
		{
			++exponent;
		}
		if (exponent < size && is_digit(text[exponent]))
		{
			shape.floating = true;
			index = exponent;
			while (index < size && is_digit(text[index]))
			{
				++index;
			}
		}
	}

	shape.suffix_begin = index;
	if (shape.floating)
	{
		shape.valid = leading_digits + fraction_digits != 0;
		return shape;
	}

	// An integer-literal that is not hexadecimal is octal when it starts with
	// `0`, which is also what makes `08` match no grammar at all.
	shape.base = (leading_digits != 0 && text[0] == '0') ? 8 : 10;
	shape.valid = leading_digits != 0;
	if (shape.base == 8)
	{
		for (std::size_t digit = 0; digit < index; ++digit)
		{
			shape.valid = shape.valid && is_octal_digit(text[digit]);
		}
	}
	return shape;
}

// See 2.14.2: `u` may appear once, `l` or `ll` may appear once, in either
// order, and `l` and `L` may not be mixed within one `ll`.
bool parse_integer_suffix(const std::string& text, std::size_t pos,
                          bool& is_unsigned, int& long_count)
{
	is_unsigned = false;
	long_count = 0;
	const std::size_t size = text.size();
	while (pos < size)
	{
		const char letter = text[pos];
		if (letter == 'u' || letter == 'U')
		{
			if (is_unsigned)
			{
				return false;
			}
			is_unsigned = true;
			++pos;
			continue;
		}
		if (letter != 'l' && letter != 'L')
		{
			return false;
		}
		if (long_count != 0)
		{
			return false;
		}
		if (pos + 1 < size && text[pos + 1] == letter)
		{
			long_count = 2;
			pos += 2;
		}
		else
		{
			long_count = 1;
			++pos;
		}
	}
	return true;
}

bool parse_floating_suffix(const std::string& text, std::size_t pos,
                           EFundamentalType& type)
{
	if (pos == text.size())
	{
		type = FT_DOUBLE;
		return true;
	}
	if (pos + 1 != text.size())
	{
		return false;
	}
	switch (text[pos])
	{
	case 'f':
	case 'F':
		type = FT_FLOAT;
		return true;
	case 'l':
	case 'L':
		type = FT_LONG_DOUBLE;
		return true;
	default:
		return false;
	}
}

bool compute_integer_value(const std::string& text, const NumericShape& shape,
                           unsigned long long& value)
{
	value = 0;
	const unsigned long long base = shape.base;
	// The largest value that can still take another digit, and the largest
	// digit it can take, so that the overflow test is a comparison per digit
	// rather than a division per digit.
	const unsigned long long value_limit = kMaxUnsignedValue / base;
	const unsigned long long digit_limit = kMaxUnsignedValue % base;
	for (std::size_t index = shape.digits_begin; index < shape.suffix_begin; ++index)
	{
		const unsigned long long digit =
			static_cast<unsigned long long>(hex_digit_value(text[index]));
		if (value > value_limit || (value == value_limit && digit > digit_limit))
		{
			return false;
		}
		value = value * base + digit;
	}
	return true;
}

unsigned long long type_max_value(EFundamentalType type)
{
	const std::size_t size = fundamental_type_size(type);
	const unsigned long long all = size >= 8
		? kMaxUnsignedValue
		: (1ULL << (size * 8)) - 1;
	switch (type)
	{
	case FT_INT:
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
		return all >> 1;
	default:
		return all;
	}
}

// See 2.14.2 Table 6: the list of types a literal may take, in order, given
// its suffix and whether it is decimal.
const EFundamentalType* integer_type_candidates(bool is_unsigned, int long_count,
                                                bool decimal, std::size_t& count)
{
	static const EFundamentalType kUnsigned[] =
		{ FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT };
	static const EFundamentalType kDecimal[] =
		{ FT_INT, FT_LONG_INT, FT_LONG_LONG_INT };
	static const EFundamentalType kOther[] =
		{ FT_INT, FT_UNSIGNED_INT, FT_LONG_INT, FT_UNSIGNED_LONG_INT,
		  FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT };

	// Each `l` in the suffix drops the narrower candidates: an unsigned list
	// loses one entry, a decimal list one, and a mixed list two.
	if (is_unsigned)
	{
		count = 3 - static_cast<std::size_t>(long_count);
		return kUnsigned + long_count;
	}
	if (decimal)
	{
		count = 3 - static_cast<std::size_t>(long_count);
		return kDecimal + long_count;
	}
	count = 6 - 2 * static_cast<std::size_t>(long_count);
	return kOther + 2 * long_count;
}

bool select_integer_type(unsigned long long value, bool is_unsigned,
                         int long_count, bool decimal, EFundamentalType& type)
{
	std::size_t count = 0;
	const EFundamentalType* candidates =
		integer_type_candidates(is_unsigned, long_count, decimal, count);
	for (std::size_t index = 0; index < count; ++index)
	{
		if (value <= type_max_value(candidates[index]))
		{
			type = candidates[index];
			return true;
		}
	}
	return false;
}

// x86-64 `long double` is the 80 bit extended precision type, and the ABI
// pads it to 16 bytes.  The padding is part of the object representation the
// assignment prints, so it is written as zero rather than left undefined.
const std::size_t kLongDoubleValueBytes = 10;

void set_floating_value(PostToken& token, EFundamentalType type, const char* text)
{
	token.type = type;
	token.data.assign(fundamental_type_size(type), '\0');
	char* bytes = &token.data[0];
	if (type == FT_FLOAT)
	{
		const float value = PA2Decode_float(text);
		std::memcpy(bytes, &value, sizeof value);
		return;
	}
	if (type == FT_DOUBLE)
	{
		const double value = PA2Decode_double(text);
		std::memcpy(bytes, &value, sizeof value);
		return;
	}
	const long double value = PA2Decode_long_double(text);
	std::memcpy(bytes, &value, kLongDoubleValueBytes);
}

void scan_integer_literal(const std::string& spelling, const NumericShape& shape,
                          PostToken& token)
{
	bool is_unsigned = false;
	int long_count = 0;
	if (!parse_integer_suffix(spelling, shape.suffix_begin, is_unsigned, long_count))
	{
		return;
	}
	unsigned long long value = 0;
	if (!compute_integer_value(spelling, shape, value))
	{
		return;
	}
	EFundamentalType type = FT_INT;
	if (!select_integer_type(value, is_unsigned, long_count, shape.base == 10, type))
	{
		return;
	}
	token.kind = PostTokenKind::Literal;
	token.set_integer_value(type, value);
}

void scan_floating_literal(const std::string& spelling, const NumericShape& shape,
                           PostToken& token)
{
	EFundamentalType type = FT_DOUBLE;
	if (!parse_floating_suffix(spelling, shape.suffix_begin, type))
	{
		return;
	}
	token.kind = PostTokenKind::Literal;
	set_floating_value(token, type, spelling.c_str());
}

} // namespace

EFundamentalType literal_element_type(LiteralEncoding encoding)
{
	switch (encoding)
	{
	case LiteralEncoding::Char16: return FT_CHAR16_T;
	case LiteralEncoding::Char32: return FT_CHAR32_T;
	case LiteralEncoding::Wide: return FT_WCHAR_T;
	default: return FT_CHAR;
	}
}

std::size_t literal_code_unit_size(LiteralEncoding encoding)
{
	return fundamental_type_size(literal_element_type(encoding));
}

std::size_t decode_literal_element(const std::string& text, std::size_t pos,
                                   LiteralElement& element)
{
	const std::size_t size = text.size();
	if (text[pos] != '\\')
	{
		return decode_source_character(text, pos, element);
	}

	element.numeric_escape = false;
	++pos;
	const int lead = pos < size ? static_cast<unsigned char>(text[pos]) : 0;
	if (is_octal_digit(lead))
	{
		unsigned long long value = 0;
		for (int digit = 0; digit < 3 && pos < size && is_octal_digit(text[pos]); ++digit)
		{
			value = value * 8 + static_cast<unsigned long long>(text[pos] - '0');
			++pos;
		}
		element.value = value;
		element.numeric_escape = true;
		return pos;
	}
	if (lead == 'x')
	{
		++pos;
		unsigned long long value = 0;
		while (pos < size && is_hex_digit(static_cast<unsigned char>(text[pos])))
		{
			if (value < kOversizedEscapeValue)
			{
				value = value * 16 +
					static_cast<unsigned long long>(hex_digit_value(text[pos]));
			}
			++pos;
		}
		element.value = value < kOversizedEscapeValue ? value : kOversizedEscapeValue;
		element.numeric_escape = true;
		return pos;
	}
	element.value = static_cast<unsigned long long>(simple_escape_value(lead));
	return pos + 1;
}

bool is_user_defined_suffix(const std::string& text, std::size_t pos)
{
	if (pos >= text.size() || text[pos] != '_')
	{
		return false;
	}
	for (++pos; pos < text.size(); )
	{
		const Utf8Decoded decoded =
			decode_utf8(text.data() + pos, text.size() - pos);
		if (decoded.length == 0 || !is_identifier_continue(decoded.code_point))
		{
			return false;
		}
		pos += decoded.length;
	}
	return true;
}

void scan_pp_number(const std::string& spelling, PostToken& token)
{
	token.reset(PostTokenKind::Invalid);

	const NumericShape shape = scan_numeric_shape(spelling);
	if (!shape.valid)
	{
		return;
	}

	if (is_user_defined_suffix(spelling, shape.suffix_begin))
	{
		// A user-defined-literal keeps its spelling: 2.14.8 leaves the value
		// to the literal operator, so no range check applies to the prefix.
		token.kind = PostTokenKind::UserDefinedLiteral;
		token.ud_kind = shape.floating
			? UserDefinedLiteralKind::Floating
			: UserDefinedLiteralKind::Integer;
		token.ud_prefix_size = shape.suffix_begin;
		token.ud_suffix.assign(spelling, shape.suffix_begin, std::string::npos);
		return;
	}

	if (shape.floating)
	{
		scan_floating_literal(spelling, shape, token);
	}
	else
	{
		scan_integer_literal(spelling, shape, token);
	}
}

namespace
{

// 2.14.3p1: what a multicharacter literal is worth.  The course ABI leaves the
// value implementation defined, and this one is its last four *code units*
// packed with the first of them most significant - so `'ab'` is `0x6162`,
// `'aé'` is `0x61c3a9` because 2.14.5p5 writes `é` as two of them, and
// `'abcde'` drops the `a`.  That is what the reference compiler and the host
// one both write.
//
// False where a c-char is no code point or no code unit run, and where the
// first of them is above the one code unit an ordinary `char` holds: the
// reference reads that first c-char as the whole literal before it counts the
// rest, and so refuses `'\xff\x41'` where it takes `'\x41\xff'`.
bool packed_multicharacter(const std::string& spelling, std::size_t at,
                           std::size_t close, unsigned long long& packed)
{
	std::string units;
	bool first = true;
	while (at < close)
	{
		LiteralElement element;
		const std::size_t next = decode_literal_element(spelling, at, element);
		if (next <= at || next > close ||
		    element.value > static_cast<unsigned long long>(kMaxCodePoint) ||
		    !is_valid_code_point(static_cast<int>(element.value)) ||
		    (first && element.value > 127) ||
		    !append_ordinary_units(element, units))
		{
			return false;
		}
		first = false;
		at = next;
	}
	packed = 0;
	for (std::size_t index = 0; index < units.size(); ++index)
	{
		packed = (packed << 8) | static_cast<unsigned char>(units[index]);
	}
	return true;
}

}

bool append_ordinary_units(const LiteralElement& element, std::string& units)
{
	if (element.numeric_escape)
	{
		if (element.value > 0xFF)
		{
			return false;
		}
		units.push_back(static_cast<char>(element.value));
		return true;
	}
	append_utf8(units, static_cast<int>(element.value));
	return true;
}

void scan_character_literal(const std::string& spelling, PostToken& token,
                            CharacterLiterals characters)
{
	token.reset(PostTokenKind::Invalid);

	std::size_t quote = 0;
	LiteralEncoding encoding = LiteralEncoding::Ordinary;
	switch (spelling[0])
	{
	case 'u': encoding = LiteralEncoding::Char16; quote = 1; break;
	case 'U': encoding = LiteralEncoding::Char32; quote = 1; break;
	case 'L': encoding = LiteralEncoding::Wide; quote = 1; break;
	default: break;
	}

	const std::size_t close = spelling.rfind('\'');
	if (close <= quote + 1)
	{
		// An empty c-char-sequence, which phase 3 leaves for phase 7.
		return;
	}
	if (close + 1 != spelling.size())
	{
		if (!is_user_defined_suffix(spelling, close + 1))
		{
			return;
		}
		token.ud_kind = UserDefinedLiteralKind::Character;
		token.ud_suffix.assign(spelling, close + 1, std::string::npos);
	}

	LiteralElement element;
	if (decode_literal_element(spelling, quote + 1, element) != close)
	{
		// 2.14.3p1: more than one c-char, which the language reads as one
		// value of type `int` and PA2's dump is course defined to refuse.
		// Only the ordinary encoding has such a value: an encoding prefix
		// names one code unit, and more than one of them is ill-formed.
		unsigned long long packed = 0;
		if (characters != CharacterLiterals::Language ||
		    encoding != LiteralEncoding::Ordinary ||
		    !packed_multicharacter(spelling, quote + 1, close, packed))
		{
			return;
		}
		token.kind = token.ud_suffix.empty()
			? PostTokenKind::Literal
			: PostTokenKind::UserDefinedLiteral;
		token.set_integer_value(FT_INT, packed);
		return;
	}
	if (element.value > static_cast<unsigned long long>(kMaxCodePoint) ||
	    !is_valid_code_point(static_cast<int>(element.value)))
	{
		return;
	}

	// `set_integer_value` keeps the low bytes of the type it is given, so a
	// code point wider than the type holds is narrowed there rather than here.
	EFundamentalType type = FT_CHAR;
	if (encoding == LiteralEncoding::Ordinary)
	{
		if (characters == CharacterLiterals::Language)
		{
			// 2.14.3p1: an ordinary character-literal holding one c-char has
			// type `char` whatever that c-char is, and one that no ordinary
			// code unit holds is worth what the implementation says - here
			// the low code unit of its code point, which is what the
			// reference compiler writes and is the same reading the run of
			// them above is packed by.
			type = FT_CHAR;
		}
		else
		{
			// Course defined: PA2's dump holds the code point itself, so an
			// ordinary character literal is a `char` there only when that
			// code point is one UTF-8 code unit.
			type = element.value <= 127 ? FT_CHAR : FT_INT;
		}
	}
	else
	{
		if (encoding == LiteralEncoding::Char16 && element.value > 0xFFFF)
		{
			return;
		}
		type = literal_element_type(encoding);
	}

	token.kind = token.ud_suffix.empty()
		? PostTokenKind::Literal
		: PostTokenKind::UserDefinedLiteral;
	token.set_integer_value(type, element.value);
}
