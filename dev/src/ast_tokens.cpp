#include "ast_tokens.h"

#include <cctype>
#include <unordered_set>
#include <utility>

#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

namespace
{

// True when `text` starts, or ends, with a character an identifier is made of.
// Two tokens need a separator between them when the first ends and the second
// starts with one, which is what keeps `const From*` apart while leaving
// `sizeof(T)&&true` closed up.
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

// Every spelling phase 3 munches maximally.  Two punctuation tokens written
// next to each other close up into one of these exactly when one of them is
// longer than the first token and starts the pair, which is the only other way
// a name can come back out as something it was not written as.  The comment
// introducers are here because they close up the same way: `a / *p` would come
// back as `a` and a comment.
const std::unordered_set<std::string>& munched_spellings()
{
	static const char* const kSpellings[] = {
#define CPPGM_AST_PUNCTUATION_SPELLING(text, type) text,
		CPPGM_PUNCTUATION_OPERATORS(CPPGM_AST_PUNCTUATION_SPELLING)
#undef CPPGM_AST_PUNCTUATION_SPELLING
#define CPPGM_AST_PREPROCESSING_SPELLING(text) text,
		CPPGM_PREPROCESSING_ONLY_OPERATORS(CPPGM_AST_PREPROCESSING_SPELLING)
#undef CPPGM_AST_PREPROCESSING_SPELLING
		"//",
		"/*"
	};
	static const std::unordered_set<std::string> spellings(
		kSpellings, kSpellings + sizeof(kSpellings) / sizeof(kSpellings[0]));
	return spellings;
}

// The longest of them, which bounds how much of `next` phase 3 could take.
const std::size_t kLongestMunchedSpelling = 4;

// True when phase 3, reading `prev` immediately followed by `next`, would take
// more than `prev`.  It takes the longest spelling it can, so the question is
// whether `prev` and any prefix of `next` are together one of the spellings.
bool munches_together(const std::string& prev, const std::string& next)
{
	for (std::size_t take = 1;
	     take <= next.size() && prev.size() + take <= kLongestMunchedSpelling; ++take)
	{
		if (munched_spellings().count(prev + next.substr(0, take)) != 0)
		{
			return true;
		}
	}
	return false;
}

// Whether `prev` and `next`, written next to each other, would read back as
// something other than the two tokens they are.  `after` is the token that
// follows the pair, which one rule of 2.5 looks at.
bool needs_separator(const std::string& prev, const std::string& next,
                     const std::string& after)
{
	if (prev.empty() || next.empty())
	{
		return false;
	}
	if (ends_word(prev))
	{
		return starts_word(next);
	}
	// A pp-number may open with `.`, which is the one spelling that closes up
	// with a word rather than with more punctuation.
	if (prev == "." && std::isdigit(static_cast<unsigned char>(next[0])) != 0)
	{
		return true;
	}
	// No other spelling phase 3 munches holds a character a word is made of, so
	// punctuation and a word that follows it stay apart on their own.
	if (starts_word(next))
	{
		return false;
	}
	// 14.2.3: `>>` reaches the parser as two terminals so that it can close two
	// template-ids, and a name that closes two of them is spelled `>>` however
	// it was written.  This pair closes up on purpose.
	if (prev == ">" && next[0] == '>')
	{
		return false;
	}
	// 2.5.3: `<::` not followed by `:` or `>` is `<` and then `::`, so the
	// digraph `<:` does not form here and `TC<::N::T>` stays closed up.
	if (prev == "<" && next == "::" &&
	    (after.empty() || (after[0] != ':' && after[0] != '>')))
	{
		return false;
	}
	return munches_together(prev, next);
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
	const std::size_t last = end < tokens_.size() ? end : tokens_.size();
	std::string result;
	for (std::size_t index = begin; index < last; ++index)
	{
		if (index > begin &&
		    needs_separator(spelling(index - 1), spelling(index), spelling(index + 1)))
		{
			result.push_back(' ');
		}
		result.append(spelling(index));
	}
	return result;
}

void PackTable::add(std::size_t begin, unsigned long long alignment)
{
	Region region;
	region.begin = begin;
	region.alignment = alignment;
	regions_.push_back(region);
}

unsigned long long PackTable::at(std::size_t index) const
{
	// The regions are appended in stream order, so the one in force is the
	// last that begins at or before `index`: one binary search per class, and
	// none at all for the unit that writes no such directive.
	std::size_t low = 0;
	std::size_t high = regions_.size();
	while (low < high)
	{
		const std::size_t middle = low + (high - low) / 2;
		if (regions_[middle].begin <= index)
		{
			low = middle + 1;
		}
		else
		{
			high = middle;
		}
	}
	return low == 0 ? 0 : regions_[low - 1].alignment;
}

bool AstTokenStream::string_value(std::size_t index, std::string& out) const
{
	const std::unordered_map<std::size_t, std::string>::const_iterator found =
		string_values_.find(index);
	if (found == string_values_.end())
	{
		return false;
	}
	out = found->second;
	return true;
}

void AstTokenStream::build(SourceFileTable& files, const PreprocessorOptions& options,
                           const std::string& path)
{
	Preprocessor preprocessor(files, options, path);
	PostTokenizer tokenizer(preprocessor);

	PostToken token;
	unsigned long long pack_epoch = preprocessor.pack_epoch();
	while (tokenizer.next(token))
	{
		if (token.kind == PostTokenKind::EndOfFile)
		{
			break;
		}
		// 16.6: every directive before this token has run, so what the packing
		// alignment is here is what the token's own class definition asks.  The
		// test is one comparison per token and the record is one per directive
		// that changed it.
		if (preprocessor.pack_epoch() != pack_epoch)
		{
			pack_epoch = preprocessor.pack_epoch();
			packs_.add(tokens_.size(), preprocessor.pack_alignment());
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

		case PostTokenKind::LiteralArray:
			if (token.type == FT_CHAR && !token.data.empty())
			{
				string_values_[tokens_.size()] =
					token.data.substr(0, token.data.size() - 1);
			}
			append(TT_LITERAL, token.source);
			break;

		case PostTokenKind::Literal:
		case PostTokenKind::UserDefinedLiteral:
			append(TT_LITERAL, token.source);
			break;

		default:
			throw SourceError(" a token of the file is not a token of C++");
		}
	}
	append(ST_EOF, std::string());
}
