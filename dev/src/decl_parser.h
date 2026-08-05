#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "entity_model.h"
#include "init_semantics.h"
#include "name_table.h"
#include "parse_depth.h"
#include "program_model.h"
#include "sema_token.h"
#include "type_model.h"
#include "value_model.h"

// One `ptr-operator` or `noptr-declarator-suffix` of a declarator, as written.
struct DeclaratorOperator
{
	enum Kind
	{
		kPointer,
		kLValueReference,
		kRValueReference,
		kArray,
		kFunction
	};

	DeclaratorOperator()
		: kind(kPointer)
		, cv(kCvNone)
		, bounded(false)
		, variadic(false)
		, bound(0)
	{}

	Kind kind;
	unsigned cv;                     // kPointer
	bool bounded;                    // kArray
	bool variadic;                   // kFunction
	unsigned long long bound;        // kArray
	std::vector<TypeId> parameters;  // kFunction, already adjusted by 8.3.5p5
};

// A `declarator` or `abstract-declarator` as written.
//
// 8.3 derives a type from the outside in, but a declarator is written from the
// inside out: in `int (*f(char))[3]` the parameter list binds before the `*`
// whose pointee it builds, and both bind before the parentheses around them.
// Recording the declarator as written and walking it once afterwards is what
// keeps the deepest nesting at one pass rather than one pass per level.
struct DeclaratorNode
{
	DeclaratorNode()
		: inner(nullptr)
	{}

	std::vector<DeclaratorOperator> prefix;
	std::vector<DeclaratorOperator> suffix;
	DeclaratorNode* inner;
};

// The `declarator-id` of a declarator, with the namespace a qualified one
// names.
struct DeclaratorId
{
	DeclaratorId()
		: qualifier(nullptr)
		, name(kNoName)
	{}

	Namespace* qualifier;
	NameId name;
};

// What a tool that builds a program image lends the parser.
//
// Its presence is what selects `pa8.gram` over `pa7.gram` and the diagnostics
// that come with it: PA7 leaves an ill-formed program undefined and describes
// what it can, while PA8 has to refuse one, so the two cannot share a single
// answer to "is this program well formed".
struct ImageContext
{
	const std::vector<LiteralValue>* literals;
	ProgramImage* image;
	InitSemantics* init;
};

// The declaration parser: `pa7.gram` or `pa8.gram` with the semantic actions
// that build the object model of one translation unit and, for PA8, the
// program image the whole run shares.
//
// Unlike the PA6 recognizer this parser cannot ask the grammar alone what a
// name is - `simple-type-specifier` admits an identifier only when a scope
// says it is a typedef-name - so lookup runs while parsing.  Lookup only
// reads, and a declaration is entered into the model only once its declarator
// has parsed, so the two places the grammar needs to backtrack cost nothing
// more than a cursor reset.
class DeclParser
{
public:
	DeclParser(const std::vector<SemaToken>& tokens, TypeTable& types,
	           TranslationUnitModel& model, const ImageContext* image = nullptr);

	// Parses `translation-unit`.  Throws SemanticError for a token sequence
	// the assignment gives no meaning to.
	void run();

private:
	// The simple-type-specifiers of 7.1.6.2, counted rather than remembered in
	// order, because Table 10 names a type by which of them appear and `long`
	// is the only one that may appear twice.
	enum SimpleTypeSpecifier
	{
		kSpecChar,
		kSpecChar16,
		kSpecChar32,
		kSpecWchar,
		kSpecBool,
		kSpecShort,
		kSpecInt,
		kSpecLong,
		kSpecSigned,
		kSpecUnsigned,
		kSpecFloat,
		kSpecDouble,
		kSpecVoid,
		kSimpleTypeSpecifierCount
	};

	// A `decl-specifier-seq` or `type-specifier-seq` as read.
	struct Specifiers
	{
		Specifiers();

		bool is_typedef;
		// 7.1.1 storage class, 7.1.2 function and 7.1.5 constexpr specifiers,
		// which decide linkage, what a declaration defines, and whether its
		// initializer has to be a constant expression.
		bool is_static;
		bool is_extern;
		bool is_thread_local;
		bool is_inline;
		bool is_constexpr;
		bool has_type_name;
		unsigned cv;
		TypeId type_name;
		std::size_t builtins;
		std::size_t count;
		unsigned counted[kSimpleTypeSpecifierCount];
	};

	struct Mark
	{
		std::size_t pos;
	};

	unsigned peek(std::size_t offset = 0) const
	{
		const std::size_t index = pos_ + offset;
		return index < tokens_.size() ? tokens_[index].type : unsigned(ST_EOF);
	}

	bool at(unsigned type) const { return peek() == type; }

	// The token, or the end of file token when the cursor has run past it.
	const SemaToken& token_at(std::size_t offset = 0) const
	{
		const std::size_t index = pos_ + offset;
		return index < tokens_.size() ? tokens_[index] : tokens_.back();
	}

	NameId name_at(std::size_t offset = 0) const { return token_at(offset).name; }

	void advance() { ++pos_; }

	bool accept(unsigned type)
	{
		if (!at(type))
		{
			return false;
		}
		++pos_;
		return true;
	}

	void expect(unsigned type);

	Mark mark() const
	{
		Mark saved;
		saved.pos = pos_;
		return saved;
	}

	void reset(const Mark& saved) { pos_ = saved.pos; }

	// Declarations.
	void parse_declaration_seq(Namespace& where, unsigned closer);
	void parse_declaration(Namespace& where);
	void parse_namespace_definition(Namespace& where, bool is_inline);
	void parse_namespace_alias_definition(Namespace& where);
	void parse_using_declaration(Namespace& where);
	void parse_using_directive(Namespace& where);
	void parse_alias_declaration(Namespace& where);
	void parse_simple_declaration(Namespace& where);
	Entity& declare(Namespace& where, const Specifiers& specifiers, const DeclaratorId& id,
	                TypeId type);
	static EntityKind declared_kind(const Specifiers& specifiers, TypeId type,
	                                const TypeTable& types);
	// 7.1.1p1: the storage class specifiers a declaration may combine.
	static void check_storage_class(const Specifiers& specifiers);

	// Objects and initialization, which only PA8 has (decl_parser_object.cpp).
	void parse_static_assert_declaration(Namespace& where);
	// Gives `entity` its object in the program image, applies what this
	// declaration says about it, and reads and analyses its initializer.
	void analyse_object(Namespace& where, const Specifiers& specifiers,
	                    const DeclaratorId& id, Entity& entity, TypeId type,
	                    bool is_function_body);
	Symbol& link_object(Namespace& home, const Specifiers& specifiers, Entity& entity,
	                    NameId name, TypeId type);
	// 3.1p2: whether this declaration defines the entity, with the checks
	// 8.5p6, 8.5.3p3 and 7.1.5p9 make of one that does.
	bool defines_object(const Specifiers& specifiers, TypeId type, bool has_initializer,
	                    bool is_function_body);
	void set_initial_value(Symbol& symbol, const Specifiers& specifiers, TypeId& type,
	                       const ExprValue& source);

	// Expressions (decl_parser_expression.cpp).
	bool parse_expression(ExprValue& out);
	bool parse_id_expression(ExprValue& out);
	ExprValue literal_expression();
	ExprValue entity_expression(const Entity& entity);
	ExprValue keyword_expression(unsigned token);

	// Names.
	bool parse_nested_name_specifier(Namespace& where, Namespace*& out);
	bool parse_type_name(Namespace& where, TypeId& out);
	Entity& resolve_namespace_name(Namespace& where);

	// Specifiers and types.
	static int builtin_specifier(unsigned token);
	// Table 10 of 7.1.6.2, read off the simple-type-specifiers that appeared.
	static EFundamentalType table_10_type(const unsigned* counted);
	// Whether Table 10 has a row for the specifiers that appeared at all.
	static bool table_10_names_a_type(const unsigned* counted);
	bool parse_specifier_seq(Namespace& where, bool declaration, Specifiers& out);
	TypeId specifier_type(const Specifiers& specifiers);
	bool parse_type_id(Namespace& where, TypeId& out);

	// Declarators.
	DeclaratorNode* parse_declarator(Namespace& where, bool allow_name, DeclaratorId& id);
	bool parse_declarator_id(Namespace& where, DeclaratorId& id);
	void parse_ptr_operators(DeclaratorNode& node);
	unsigned parse_cv_qualifiers();
	void parse_declarator_suffixes(Namespace& where, DeclaratorNode& node);
	bool parse_array_suffix(DeclaratorOperator& op);
	bool parse_parameter_clause(Namespace& where, DeclaratorOperator& op);
	bool parse_parameter(Namespace& where, TypeId& type, bool& named);
	// 8.2p3: at an `(`, whether what follows is a parameter-declaration-clause
	// rather than a parenthesized declarator.
	bool starts_parameter_clause(Namespace& where);
	// 8.3: the type the declarator derives from `base`.  `function_declarator`
	// says whether the outermost thing it derived is a function, which 8.4p1
	// asks of a function-definition and which a typedef-name cannot answer
	// for.
	TypeId build_type(const DeclaratorNode& node, TypeId base,
	                  bool* function_declarator = nullptr);
	// `written_reference` carries "this operand was made a reference by this
	// declarator" along the chain, because 8.3.2p6 collapses a reference only
	// when the inner one was denoted by a typedef.
	TypeId apply(const DeclaratorOperator& op, TypeId type, bool& written_reference);

	const std::vector<SemaToken>& tokens_;
	std::size_t pos_;
	TypeTable& types_;
	TranslationUnitModel& model_;
	// Null for a tool that only describes declarations; otherwise the program
	// image being built, which selects `pa8.gram`.
	const std::vector<LiteralValue>* literals_;
	ProgramImage* image_;
	InitSemantics* init_;
	// 3.4.3p3: the scope a name written after a qualified declarator-id is
	// looked up in, which is the namespace that declarator-id named.
	Namespace* value_scope_;
	// Whether the expression being analysed initializes an object of the
	// program.  A `static_assert` condition and an array bound are only
	// evaluated, so a string literal in one names nothing the image holds.
	bool initializing_;
	ParseDepth depth_;
	// The declarator nodes of the declaration being parsed.  A declaration is
	// the largest construct a declarator can span, so the nodes are dropped
	// together when the next one starts rather than one allocation at a time.
	std::deque<DeclaratorNode> arena_;
};
