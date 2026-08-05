#include "posttokenizer.h"

#include <algorithm>
#include <utility>

#include "literal_scan.h"
#include "token_model.h"

PostTokenizer::PostTokenizer(std::string source)
	: lexer_(std::move(source))
	, has_held_(false)
	, previous_is_operator_(false)
	, finished_(false)
{}

bool PostTokenizer::next(PostToken& token)
{
	if (!deferred_identifier_.empty())
	{
		token.reset(PostTokenKind::Identifier);
		token.source.swap(deferred_identifier_);
		deferred_identifier_.clear();
		previous_is_operator_ = false;
		return true;
	}
	if (finished_)
	{
		return false;
	}

	while (true)
	{
		if (has_held_)
		{
			std::swap(current_, held_);
			has_held_ = false;
		}
		else if (!lexer_.next(current_))
		{
			finished_ = true;
			return false;
		}

		if (current_.type == PPTokenType::WhitespaceSequence ||
		    current_.type == PPTokenType::NewLine)
		{
			continue;
		}
		if (current_.type == PPTokenType::StringLiteral ||
		    current_.type == PPTokenType::UserDefinedStringLiteral)
		{
			sequence_.add(current_.spelling);
			continue;
		}

		if (!sequence_.empty())
		{
			// The sequence is maximal, so this token belongs to the next one.
			std::swap(current_, held_);
			has_held_ = true;
			flush_sequence(token);
			return true;
		}
		if (current_.type == PPTokenType::EndOfFile)
		{
			token.reset(PostTokenKind::EndOfFile);
			token.source.clear();
			finished_ = true;
			return true;
		}

		convert(token);
		previous_is_operator_ = token.kind == PostTokenKind::Simple &&
			token.simple_type == KW_OPERATOR;
		return true;
	}
}

void PostTokenizer::flush_sequence(PostToken& token)
{
	if (previous_is_operator_ && sequence_.is_reserved_empty_suffix())
	{
		sequence_.build_literal_operator_id(token, deferred_identifier_);
	}
	else
	{
		sequence_.build(token);
	}
	previous_is_operator_ = false;
}

void PostTokenizer::convert(PostToken& token)
{
	switch (current_.type)
	{
	case PPTokenType::Identifier:
	case PPTokenType::PreprocessingOpOrPunc:
	{
		ETokenType simple_type = kTokenTypeCount;
		if (lookup_simple_token(current_.spelling, simple_type))
		{
			token.reset(PostTokenKind::Simple);
			token.simple_type = simple_type;
		}
		else
		{
			// The only operators that are not simple tokens are the four the
			// assignment reserves for preprocessing: `#`, `##`, `%:`, `%:%:`.
			token.reset(current_.type == PPTokenType::Identifier
				? PostTokenKind::Identifier
				: PostTokenKind::Invalid);
		}
		break;
	}
	case PPTokenType::PPNumber:
		scan_pp_number(current_.spelling, token);
		break;
	case PPTokenType::CharacterLiteral:
	case PPTokenType::UserDefinedCharacterLiteral:
		scan_character_literal(current_.spelling, token);
		break;
	default:
		// A header-name or a non-whitespace-character has no token form.
		token.reset(PostTokenKind::Invalid);
		break;
	}
	token.source.swap(current_.spelling);
}
