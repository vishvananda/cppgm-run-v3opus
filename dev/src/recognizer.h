#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "memo_table.h"
#include "parse_cursor.h"
#include "parse_depth.h"
#include "parse_token.h"

// The rules whose result is remembered per position.
//
// A backtracking recursive descent parser re-enters the same rule at the same
// position whenever an enclosing rule tries its next alternative.  A rule
// belongs here when re-entering it costs more than a constant plus other
// remembered rules; for the template rules the descents nest, so `N` nested
// `TC1<` would cost `3^N` without a memo.  A rule whose body is a token test
// and a call to a rule that is already remembered is deliberately absent: the
// entry would cost more to store than the work it saves.
enum MemoRule
{
	kMemoNestedNameSpecifier,
	kMemoSimpleTemplateId,
	kMemoTemplateArgument,
	kMemoTypeId,
	kMemoDeclSpecifierSeq,
	kMemoTypeSpecifierSeq,
	kMemoTrailingTypeSpecifierSeq,
	kMemoDeclarator,
	kMemoAbstractDeclarator,
	kMemoParametersAndQualifiers,
	kMemoAssignmentExpression,
	kMemoConditionalExpression,
	kMemoInitializerClause,
	kMemoStatement,
	kMemoDeclaration,
	kMemoRuleCount
};

// Which specifiers a specifier sequence admits.  The three sequences of the
// grammar differ only in that, so they share one implementation.
enum class SpecifierMode
{
	Decl,     // decl-specifier-seq
	Type,     // type-specifier-seq
	Trailing  // trailing-type-specifier-seq
};

// The PA6 recognizer: does a token sequence match `translation-unit`?
//
// One function per nonterminal, with the interface discipline of the handout:
// a function that succeeds leaves the cursor one past the last token it used,
// and a function that fails leaves the cursor where it found it.  Nothing but
// the cursor it is built on and the memo table is state, so a rule's result is
// a function of position and angle bracket state and can be remembered.
class Recognizer : private ParseCursor
{
public:
	explicit Recognizer(const std::vector<ParseToken>& tokens);

	// True when the whole token sequence is a `translation-unit`.
	bool recognize();

private:
	typedef bool (Recognizer::*Rule)();
	typedef ParseDepth::Frame Frame;

	// The tokens that can follow a parameter-declaration, which is what tells
	// a declarator apart from an abstract-declarator.  A template parameter
	// list ends the same way a function parameter list does except that it
	// closes with an angle bracket.
	bool at_parameter_end() const
	{
		return at(OP_COMMA) || at(OP_RPAREN) || at(OP_DOTS) || at(OP_GT) ||
			at(ST_RSHIFT_1) || at(ST_RSHIFT_2);
	}

	// The tokens that can follow a template-parameter.  Unlike a function
	// parameter, a template parameter is never followed by `...`, so a
	// `class T1` that a trailing `...` belongs to is a parameter-declaration
	// with an abstract-pack-declarator rather than a type-parameter.
	bool at_template_parameter_end() const
	{
		return at(OP_COMMA) || at(OP_GT) || at(ST_RSHIFT_1) || at(ST_RSHIFT_2);
	}

	bool memoize(MemoRule rule, Rule body);

	// Names (recognizer_name.cpp).
	bool parse_type_name();
	bool parse_class_name();
	bool parse_simple_template_id();
	bool parse_simple_template_id_body();
	bool parse_close_angle_bracket();
	// The tokens that can follow a template-argument: an argument is only
	// accepted when the alternative that matched it ends where the list can
	// continue or close.
	bool at_template_argument_end() const
	{
		return at(OP_COMMA) || at(OP_DOTS) || at(OP_GT) || at(ST_RSHIFT_1) ||
			at(ST_RSHIFT_2);
	}
	bool parse_template_argument_list();
	bool parse_template_argument_dots();
	bool parse_template_argument();
	bool parse_template_argument_body();
	bool parse_id_expression();
	bool parse_unqualified_id();
	bool parse_operator_id();
	bool parse_operator_function_id();
	bool parse_literal_operator_id();
	bool parse_conversion_function_id();
	bool parse_nested_name_specifier();
	bool parse_nested_name_specifier_body();
	void nested_name_specifier_ends(std::vector<std::size_t>& ends);
	bool parse_nested_name_specifier_root();
	bool parse_nested_name_specifier_suffix();
	bool parse_decltype_specifier();
	bool parse_typename_specifier();
	bool parse_elaborated_type_specifier();

	// Expressions (recognizer_expression.cpp).
	bool parse_primary_expression();
	bool parse_postfix_expression();
	bool parse_postfix_root();
	bool parse_postfix_suffix();
	bool parse_pseudo_destructor_name();
	bool parse_unary_expression();
	bool parse_new_expression();
	bool parse_new_placement();
	bool parse_new_type_id();
	bool parse_new_declarator();
	bool parse_noptr_new_declarator();
	bool parse_new_initializer();
	bool parse_delete_expression();
	bool parse_cast_expression();
	bool parse_binary_expression(int level);
	bool accept_binary_operator(int level);
	bool parse_conditional_expression();
	bool parse_conditional_expression_body();
	bool parse_conditional_tail();
	bool parse_assignment_expression();
	bool parse_assignment_expression_body();
	bool accept_assignment_operator();
	bool parse_throw_expression();
	bool parse_expression();
	bool parse_constant_expression();
	bool parse_expression_list();
	bool parse_initializer_clause();
	bool parse_initializer_clause_body();
	bool parse_initializer_list();
	bool parse_braced_init_list();
	bool parse_lambda_expression();
	bool parse_lambda_introducer();
	bool parse_lambda_capture();
	bool parse_capture_list();
	bool parse_capture();
	bool parse_lambda_declarator();

	// Declarators and types (recognizer_declarator.cpp).
	bool parse_specifier_seq(SpecifierMode mode);
	bool parse_decl_specifier_seq();
	bool parse_decl_specifier_seq_body();
	bool parse_type_specifier_seq();
	bool parse_type_specifier_seq_body();
	bool parse_trailing_type_specifier_seq();
	bool parse_trailing_type_specifier_seq_body();
	bool parse_specifier(SpecifierMode mode, bool& seen_type);
	bool parse_simple_type_specifier();
	bool parse_declarator();
	bool parse_declarator_body();
	bool parse_ptr_declarator();
	bool parse_noptr_declarator();
	bool parse_noptr_declarator_root();
	bool parse_noptr_declarator_suffix();
	bool parse_declarator_id();
	std::size_t parse_ptr_operators();
	bool parse_ptr_operator();
	bool parse_cv_qualifiers();
	bool parse_parameters_and_qualifiers();
	bool parse_parameters_and_qualifiers_body();
	bool parse_parameter_declaration_clause();
	bool parse_parameter_declaration_list();
	bool parse_parameter_declaration();
	bool parse_trailing_return_type();
	bool parse_type_id();
	bool parse_type_id_body();
	bool parse_abstract_declarator();
	bool parse_abstract_declarator_body();
	bool parse_ptr_abstract_declarator();
	bool parse_noptr_abstract_declarator();
	bool parse_noptr_abstract_declarator_root();
	bool parse_abstract_pack_declarator();
	bool parse_exception_specification();
	bool parse_type_id_list();

	// Declarations (recognizer.cpp).
	bool parse_declaration_seq(unsigned closer);
	bool parse_declaration();
	bool parse_declaration_body();
	bool parse_block_declaration();
	bool parse_simple_declaration();
	bool parse_init_declarator_list();
	bool parse_initializer();
	bool parse_brace_or_equal_initializer();
	bool parse_alias_declaration();
	bool parse_static_assert_declaration();
	bool parse_asm_definition();
	bool parse_namespace_definition();
	bool parse_namespace_alias_definition();
	bool parse_using_declaration();
	bool parse_using_directive();
	bool parse_linkage_specification();
	bool parse_attribute_declaration();
	bool parse_function_definition();
	bool parse_function_body();
	bool parse_ctor_initializer();
	bool parse_mem_initializer_list();
	bool parse_mem_initializer();
	bool parse_virt_specifiers();

	// Attributes (recognizer.cpp).
	bool parse_attribute_specifier_seq();
	bool parse_attribute_specifier();
	bool parse_alignment_specifier();
	bool parse_attribute_list();
	bool parse_attribute();
	bool parse_attribute_argument_clause();
	bool parse_balanced_tokens(unsigned closer);

	// Statements (recognizer_statement.cpp).
	bool parse_statement();
	bool parse_statement_body();
	bool parse_compound_statement();
	bool parse_expression_statement();
	bool parse_labeled_statement();
	bool parse_selection_statement();
	bool parse_iteration_statement();
	bool parse_for_statement();
	bool parse_jump_statement();
	bool parse_condition();
	bool parse_condition_declaration();
	bool parse_try_block();
	bool parse_handler_seq();
	bool parse_exception_declaration();

	// Classes, enums and templates (recognizer_member.cpp).
	bool parse_class_specifier();
	bool parse_class_head();
	bool parse_class_head_name();
	bool parse_base_clause();
	bool parse_base_specifier();
	bool parse_class_or_decltype();
	bool parse_member_specification();
	bool parse_member_declaration();
	bool parse_member_declarator_list();
	bool parse_member_declarator();
	bool parse_enum_specifier();
	bool parse_enum_head();
	bool parse_enum_key();
	bool parse_enum_base(unsigned closer);
	bool parse_enumerator_list();
	bool parse_opaque_enum_declaration();
	bool parse_template_declaration();
	bool parse_template_parameter_list();
	bool parse_template_parameter();
	bool parse_type_parameter();
	bool parse_explicit_instantiation();
	bool parse_explicit_specialization();

	ParseDepth depth_;
	MemoTable memo_;
};
