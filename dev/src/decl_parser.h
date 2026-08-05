#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "entity_model.h"
#include "name_table.h"
#include "parse_depth.h"
#include "sema_token.h"
#include "type_model.h"

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

// The PA7 parser: `pa7.gram` with the semantic actions that build the object
// model of one translation unit.
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
	           TranslationUnitModel& model);

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
	void declare(Namespace& where, const Specifiers& specifiers, const DeclaratorId& id,
	             TypeId type);

	// Names.
	bool parse_nested_name_specifier(Namespace& where, Namespace*& out);
	bool parse_type_name(Namespace& where, TypeId& out);
	Entity& resolve_namespace_name(Namespace& where);

	// Specifiers and types.
	static int builtin_specifier(unsigned token);
	// Table 10 of 7.1.6.2, read off the simple-type-specifiers that appeared.
	static EFundamentalType table_10_type(const unsigned* counted);
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
	TypeId build_type(const DeclaratorNode& node, TypeId base);
	TypeId apply(const DeclaratorOperator& op, TypeId type);

	const std::vector<SemaToken>& tokens_;
	std::size_t pos_;
	TypeTable& types_;
	TranslationUnitModel& model_;
	ParseDepth depth_;
	// The declarator nodes of the declaration being parsed.  A declaration is
	// the largest construct a declarator can span, so the nodes are dropped
	// together when the next one starts rather than one allocation at a time.
	std::deque<DeclaratorNode> arena_;
};
