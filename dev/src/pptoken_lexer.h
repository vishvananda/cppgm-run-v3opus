#pragma once

#include <cstddef>
#include <string>

#include "IPPTokenStream.h"
#include "source_reader.h"

// See 2.5 Preprocessing tokens, plus the white space, new-line and end of file
// the assignments report.  Phase 4 extends this list with token kinds of its
// own, so it is written once and both enumerations are generated from it.
#define CPPGM_PP_TOKEN_TYPES(X) \
	X(WhitespaceSequence) \
	X(NewLine) \
	X(HeaderName) \
	X(Identifier) \
	X(PPNumber) \
	X(CharacterLiteral) \
	X(UserDefinedCharacterLiteral) \
	X(StringLiteral) \
	X(UserDefinedStringLiteral) \
	X(PreprocessingOpOrPunc) \
	X(NonWhitespaceChar) \
	X(EndOfFile)

enum class PPTokenType
{
#define CPPGM_PP_TOKEN_TYPE_ENUMERATOR(name) name,
	CPPGM_PP_TOKEN_TYPES(CPPGM_PP_TOKEN_TYPE_ENUMERATOR)
#undef CPPGM_PP_TOKEN_TYPE_ENUMERATOR
};

// One preprocessing-token, spelled in UTF-8 with translation phases 1 and 2
// already applied.  `spelling` is empty for whitespace-sequence, new-line and
// end of file.  `offset` is the byte offset of the token in the source file.
struct PPToken
{
	PPTokenType type;
	std::string spelling;
	std::size_t offset;

	PPToken()
		: type(PPTokenType::EndOfFile)
		, offset(0)
	{}
};

// A pull source of preprocessing-tokens, ending with one end of file token.
//
// Phase 3 is one implementation and phase 4 is another, which is what lets the
// phase 5 to 7 driver read a macro replaced file exactly as it reads a plain
// one.
class PPTokenSource
{
public:
	virtual ~PPTokenSource() {}

	// Produces the next token; returns false once end of file was reported.
	// Throws SourceError when the source file is ill-formed.
	virtual bool next(PPToken& token) = 0;
};

// Translation phase 3: decomposes a source file into preprocessing-tokens.
//
// `form` says whether the text still needs phases 1 and 2; phase 4 lexes the
// spelling a `##` produced, which has already been through them.
//
// The lexer is a pull interface so that later phases can drive it directly.
// It owns the two pieces of state phase 3 carries between tokens: the reader's
// consumption point, and where the current line stands relative to an
// `#include` directive, which is what makes a header-name recognizable.
class PPTokenLexer : public PPTokenSource
{
public:
	explicit PPTokenLexer(SourceText source,
	                      SourceForm form = SourceForm::Physical);
	explicit PPTokenLexer(std::string source,
	                      SourceForm form = SourceForm::Physical);

	bool next(PPToken& token) override;

private:
	enum class DirectiveState
	{
		LineStart,
		AfterHash,
		AfterInclude,
		Other
	};

	bool starts_whitespace_sequence();
	void scan_whitespace_sequence();
	void skip_line_comment();
	void skip_block_comment();

	void scan_token(PPToken& token);
	void scan_header_name(PPToken& token);
	void scan_identifier_or_literal(PPToken& token);
	void scan_identifier_text(std::string& text);
	void scan_pp_number(PPToken& token);
	void scan_character_literal(PPToken& token);
	void scan_string_literal(PPToken& token);
	void scan_raw_string_literal(PPToken& token);
	void scan_raw_string_delimiter(std::string& delimiter);
	void scan_escape_sequence(std::string& text);
	bool scan_ud_suffix(std::string& text);
	bool scan_op_or_punc(PPToken& token);

	int take(std::string& text);
	void update_directive_state(const PPToken& token);
	SourceError error(const std::string& message) const;

	SourceReader reader_;
	DirectiveState directive_;
	bool finished_;
};

// Reports a token on the assignment's output stream.
void emit_pp_token(IPPTokenStream& output, const PPToken& token);
