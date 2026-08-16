#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast_model.h"
#include "ast_names.h"
#include "ast_tokens.h"
#include "parse_depth.h"

// The PA10 parser: the token sequence of one translation unit as a syntax tree.
//
// Recursive descent with ordered choice and full backtracking, as the shared
// grammar is written.  A rule returns the node it built and leaves the cursor
// one past the last token it used, or returns null and leaves the cursor where
// it found it; a caller that wants to try the next alternative resets to the
// mark it took first.  Nodes an abandoned alternative built stay in the arena
// and are dropped with it, so failing costs no bookkeeping.
class AstParser
{
public:
	AstParser(const AstTokenStream& tokens, AstArena& arena);

	// The whole token sequence as a `translation-unit`, or null when it is not
	// one, which includes a file that nests deeper than the descent goes.
	AstNode* run();

private:
	// One level of the descent.  Every rule that can re-enter itself opens one,
	// so that a file which nests deeper than the machine stack allows is
	// refused rather than crashing.
	typedef ParseDepth::Frame Frame;

	// The parser state a failed alternative has to undo: the position, and the
	// one piece of bracket state 14.2.3 reads back, namely whether the
	// innermost open bracket pair is an angle bracket pair, which decides
	// whether the next `>` closes it or is an operator.
	struct Mark
	{
		std::size_t pos;
		bool angle;
	};

	// Whether a declarator may leave its declarator-id out, and, for the
	// abstract case, which node kind the dump names it with.
	enum class DeclaratorForm
	{
		Named,     // an init-declarator: the declarator-id is required
		Parameter, // a parameter-declaration: `declarator` either way
		Abstract,  // a type-id: `abstract-declarator` when there is no name
		New        // a new-declarator: pointers and array bounds only
	};

	// Which specifiers a specifier sequence admits and how the dump names
	// them.  The two sequences differ only in that, so they share a rule.
	enum class SpecifierMode
	{
		Decl,
		Type
	};

	// Holds the bracket state 14.2.3 reads for as long as one bracket pair is
	// open.  A pair is opened and closed inside one rule, so the state a `>`
	// is read against is restored by leaving that rule however it leaves.
	class BracketGuard
	{
	public:
		BracketGuard(AstParser& parser, bool angle)
			: parser_(parser)
			, saved_(parser.angle_)
		{
			parser_.angle_ = angle;
			++parser_.bracket_depth_;
		}
		~BracketGuard()
		{
			parser_.angle_ = saved_;
			--parser_.bracket_depth_;
		}

	private:
		BracketGuard(const BracketGuard&);
		BracketGuard& operator=(const BracketGuard&);
		AstParser& parser_;
		bool saved_;
	};

	typedef DeclaredNames::Scope ScopeGuard;
	typedef DeclaredNames::Prefix PrefixGuard;
	typedef DeclaredNames::Reached ReachedGuard;

	// Cursor (ast_parser.cpp).
	unsigned peek(std::size_t offset = 0) const { return tokens_.type(pos_ + offset); }
	bool at(unsigned type) const { return peek() == type; }
	const std::string& spelling(std::size_t offset = 0) const
	{
		return tokens_.spelling(pos_ + offset);
	}
	bool accept(unsigned type)
	{
		if (!at(type))
		{
			return false;
		}
		++pos_;
		return true;
	}
	Mark mark() const
	{
		Mark saved;
		saved.pos = pos_;
		saved.angle = angle_;
		return saved;
	}
	void reset(const Mark& saved)
	{
		pos_ = saved.pos;
		angle_ = saved.angle;
	}
	AstNode* fail(const Mark& saved)
	{
		reset(saved);
		return nullptr;
	}

	AstNode* make(AstKind kind) { return arena_.make(kind); }
	// A node whose payload is the terminal at the cursor, which the rule then
	// consumes.
	AstNode* make_terminal(AstKind kind);
	AstNode* make_text(AstKind kind, const std::string& text);
	// An expression a later assignment needs and this dump does not describe:
	// 7.1.6.2p1's decltype-specifier before `::` is one component of a name the
	// grammar leaves spelled, and the expression it holds is a fact carried
	// beside that name.  The wrapper is what keeps the dump the syntax it read.
	AstNode* carried(AstNode* expression);

	bool at_close_angle() const;
	bool accept_close_angle();
	bool at_template_argument_end() const;
	bool parse_template_argument();
	bool parse_template_argument_body();
	std::size_t template_id_memo_key(const Mark& start, bool qualified) const;
	std::size_t template_argument_memo_key(const Mark& start) const;
	// True where the table below still describes the position it was filled at,
	// which is what the names in scope changing takes away.
	bool memo_is_current();

	// Names (ast_parser_name.cpp).  Each spells the span it matched, because
	// every name the grammar leaves unresolved is dumped as it was written.
	bool skip_nested_name_specifier();
	bool skip_simple_template_id(bool qualified = false);
	bool skip_simple_template_id_body(bool qualified);
	bool skip_unqualified_id(bool qualified = false);
	bool skip_decltype_specifier();
	bool skip_type_name(bool qualified = false);
	// `template_name` takes the name a template-id was written on, with its
	// nested-name-specifier and without its argument list, which is the
	// spelling the names in scope are keyed by.
	bool skip_qualified_type_name(std::string* template_name = nullptr);
	// 12.3.2p1: `conversion` takes the `type-id` node a conversion-function-id
	// was written from, so that the semantic layer resolving the name has the
	// type rather than the spelling.  Null where the operator-function-id names
	// an operator instead.
	bool skip_operator_id(AstNode** conversion = nullptr);
	AstNode* parse_conversion_type_id();
	bool skip_balanced(unsigned closer);
	// 7.6.2: the attribute forms the grammar accepts.  `alignments`, when
	// given, takes an `alignment-specifier` node per `alignas` whose operand
	// is a constant-expression, because 7.6.2p5 gives that one a meaning the
	// declaration it is written on has to answer for.
	void skip_attributes(std::vector<AstNode*>* alignments = nullptr,
	                     bool leave_alignment = false);
	std::string spelled(const Mark& start) const;
	// 7.1.6.2p1: the decltype-specifier that opens at `from` and whose operand
	// the cursor now stands on the `)` of, kept where the parse owns it under
	// the spelling it was written as - so a reading that meets that spelling
	// inside a flattened name asks the tree back rather than the text.
	void keep_decltype(const Mark& from, AstNode* operand);
	AstNode* parse_id_expression();
	AstNode* parse_declarator_id();
	AstNode* parse_member_id();
	// 12.3.2p1: `conversion` takes the conversion-type-id, and `qualifier` the
	// nested-name-specifier the name was written with - which for a conversion
	// function is not the prefix `QualifiedName` reads out of the flattened
	// spelling, because the type after `operator` may hold a `::` of its own.
	std::string parse_special_member_name(AstNode** conversion = nullptr,
	                                      std::string* qualifier = nullptr);
	// The `carried-type-id` a conversion-function-id hands the semantic layer:
	// the nested-name-specifier as its text and the type-id under it.
	AstNode* carried_conversion(AstNode* type_id, const std::string& qualifier);

	// Declared names (ast_parser_name.cpp).
	void declare_template_name(const AstNode* declaration);
	NameKind take_declared_kind(NameKind fallback);

	// Declarations (ast_parser.cpp).
	AstNode* parse_declaration(bool in_class);
	AstNode* parse_declaration_seq(AstNode* parent, unsigned closer);
	AstNode* parse_namespace();
	AstNode* parse_using();
	AstNode* parse_linkage_specification();
	AstNode* parse_explicit_instantiation();
	AstNode* parse_static_assert();
	AstNode* parse_general_declaration(bool in_class);
	AstNode* parse_simple_or_function_declaration(AstNode* specs, bool in_class);
	AstNode* parse_member_specifiers();
	AstNode* parse_special_member(bool in_class);
	AstNode* parse_special_member_tail(AstNode* specifiers, AstNode* declarator,
	                                   AstNode* conversion);
	AstNode* parse_ctor_initializer();
	AstNode* parse_mem_initializer();

	// Classes, enums and templates (ast_parser_class.cpp).
	AstNode* parse_class_specifier();
	AstNode* parse_base_clause();
	AstNode* parse_base_specifier();
	AstNode* parse_class_member();
	AstNode* parse_bit_field_declaration(AstNode* specifiers);
	AstNode* parse_enum_specifier();
	AstNode* parse_template_declaration(bool in_class);
	AstNode* parse_template_parameter_clause();
	AstNode* parse_template_parameter();
	AstNode* parse_type_parameter();
	AstNode* parse_non_type_template_parameter();

	// Declarators and types (ast_parser_declarator.cpp).
	AstNode* parse_specifier_seq(SpecifierMode mode);
	bool parse_specifier(AstNode* seq, SpecifierMode mode, bool& seen_type);
	bool parse_name_specifier(AstNode* seq, SpecifierMode mode, bool& seen_type);
	AstNode* parse_type_id();
	bool is_definite_type_id(const AstNode* type_id) const;
	AstNode* parse_declarator(DeclaratorForm form);
	void parse_declarator_suffixes(AstNode* declarator, bool functions = true);
	void parse_function_suffixes(AstNode* declarator);
	AstNode* parse_parameter_clause();
	AstNode* parse_parameter_declaration();
	AstNode* parse_trailing_return_type();
	AstNode* parse_noexcept_qualifier();
	static bool declares_function(const AstNode* declarator,
	                              bool inherited = false);
	static bool declares_bare_function(const AstNode* declarator);
	AstNode* parse_init_declarator_list();
	AstNode* parse_initializer();
	AstNode* parse_braced_init_list();
	AstNode* parse_ptr_operator();
	void declare_init_declarators(const AstNode* specs, const AstNode* list);
	void declare_parameters(const AstNode* declarator);
	static const AstNode* declarator_identifier(const AstNode* declarator);
	// 3.4.1p8: the nested-name-specifier a declarator-id was written with, up
	// to and including its last `::`, or empty for an unqualified one.  It is
	// what says which region the rest of the declarator and the body after it
	// reach unqualified.
	static std::string declarator_qualifier(const AstNode* declarator);
	static std::string name_qualifier(const std::string& spelling);
	static bool has_declarator_identifier(const AstNode* declarator);

	// Statements (ast_parser_statement.cpp).
	AstNode* parse_statement();
	AstNode* parse_compound_statement();
	AstNode* parse_block_item();
	AstNode* parse_substatement();
	AstNode* parse_selection_statement();
	AstNode* parse_iteration_statement();
	AstNode* parse_for_statement();
	AstNode* parse_jump_statement();
	AstNode* parse_labeled_statement();
	AstNode* parse_try_block();
	AstNode* parse_exception_declaration();
	// 6.4p1: a condition, whose declaration arm is told from its expression
	// arm by what stands after it.  6.4's own `if`, `switch` and `while` close
	// the parentheses there, and 6.5.3p1's `for` writes the second of its
	// semicolons, so the token that ends it is the caller's to say rather than
	// this reader's to assume.
	AstNode* parse_condition(unsigned ends);
	AstNode* parse_parenthesized_condition();

	// Expressions (ast_parser_expression.cpp).
	AstNode* parse_expression();
	AstNode* parse_assignment_expression();
	AstNode* parse_conditional_expression();
	AstNode* parse_binary_expression(int level);
	AstNode* parse_unary_expression();
	AstNode* parse_postfix_expression();
	AstNode* parse_postfix_suffixes(AstNode* expression);
	AstNode* parse_primary_expression();
	AstNode* parse_cast_expression();
	AstNode* parse_parenthesized_operand(bool prefer_type);
	AstNode* parse_new_expression();
	AstNode* parse_new_type();
	AstNode* parse_delete_expression();
	AstNode* parse_lambda_expression();
	AstNode* parse_initializer_clause();
	bool parse_initializer_clause_list(AstNode* parent, unsigned closer);
	AstNode* parse_argument_suffix(AstKind kind);
	// 5.2.3p1 and 5.2.3p3: the operands of a functional-notation conversion,
	// parenthesized or the one braced-init-list standing for all of them.
	AstNode* parse_conversion_arguments();

	const AstTokenStream& tokens_;
	AstArena& arena_;
	std::size_t pos_;
	bool angle_;
	DeclaredNames names_;
	// The bracket nesting a template-argument may not open a template-id at.
	// A template-argument that cannot be completed with `<` read as the start
	// of a template-id is read again with the outermost `<` as the relational
	// operator it also spells, which is how `C<a, b < c>` closes.
	int bracket_depth_;
	int template_id_veto_depth_;
	// Set while the declaration a template-declaration introduces is being
	// parsed, so that the name it declares is a template-name inside its own
	// body: `template<class T> int f(T) { return f<int>(1); }` reads the
	// second `f` against the first.
	bool template_pending_;
	// What `simple-template-id` matched at a position, so that the readings of
	// a template-argument - a type-id, an expression, and an expression with
	// the veto above - descend into a nested template-id once between them
	// rather than once each.  Without it `TC1<TC2<...<int>...>>` costs `2^N`.
	// The key carries everything besides the position that the answer turns
	// on; the names in scope are the rest of it, and the version they were
	// read at says when the whole table stopped being true.
	std::unordered_map<std::size_t, std::size_t> template_id_memo_;
	// What one `template-argument` matched at a position, which is the same
	// question as the one above at the level below it: a list is read once per
	// attempt made at the list around it, and every attempt reads the same
	// arguments.  `W<0>::v < W<1>::v, W<1>::v < W<2>::v, ...` is the shape that
	// needs it - each `v` there reads as a template-name whose own list runs to
	// the end of the outer one, so the k-th argument's reading covers every
	// argument after it, and without a memo that is one whole re-reading of the
	// suffix, with the nodes of it built again, per argument.  The key and the
	// version are the template-id memo's, because the answer turns on the same
	// state.
	std::unordered_map<std::size_t, std::size_t> template_argument_memo_;
	unsigned long template_id_memo_version_;
	ParseDepth depth_;
};
