#pragma once

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

#include "sema_name.h"
#include "sema_scope.h"
#include "type_model.h"

struct AstNode;

// An expression that is not one of the constant expressions 5.19 defines, or
// not one of the subset of them PA11 evaluates.
//
// This is not by itself a program the assignment refuses: 5.19p3 makes a const
// object of integral type a constant only when its initializer is one, and a
// declaration whose initializer is an ordinary expression is well formed and
// declares an object that is not a constant.  It is what separates that from
// the errors an expression can also hold - a name that is declared nowhere, a
// type-id that names no type - which are facts about the program rather than
// about the value, and which every caller lets through.
class NotConstant : public std::runtime_error
{
public:
	explicit NotConstant(const std::string& what)
		: std::runtime_error(what)
	{}
};

// The PA11 semantic pass: one walk of a PA10 syntax tree that builds the
// scopes, declarations and types of a translation unit, and the dump of them.
//
// The walk is in source order and visits each node once.  A declaration is
// resolved where it is reached, against the scopes already built, so nothing is
// deferred and no construct is read twice.  What a declaration established
// stays in the model; the tree is not consulted again once the walk is past it.
class SemaAnalyzer
{
public:
	SemaAnalyzer();

	// Analyses `unit`, a PA10 `translation-unit`.  Throws for a program the
	// assignment gives no meaning to.
	void run(const AstNode& unit);

	void write(std::ostream& out) const;

private:
	// Where a declaration is read: the region it declares into, and the dump
	// node its lines are written to.  The two part company where the standard
	// and the output format do: a member function defined outside its class
	// declares into the class and writes its lines there, while an enumeration
	// defined outside its class writes its lines where it is written.
	struct Context
	{
		Scope* scope;
		DumpScope* dump;
	};

	// The terminals a declaration was written from, which is what names an
	// unnamed class that no declarator names (9.5p2).
	struct Span
	{
		std::uint32_t begin;
		std::uint32_t end;
	};

	// A `decl-specifier-seq` or `type-specifier-seq` as read.
	struct Specifiers
	{
		Specifiers();

		unsigned counted[kSimpleTypeSpecifierCount];
		unsigned builtins;
		unsigned cv;
		TypeId type_name;
		bool has_type_name;
		bool is_typedef;
		bool is_constexpr;
		// The class or enumeration this sequence declared.
		SemaEntity* introduced;
	};

	// One parameter of a parameter-clause, before 8.3.5p4 drops a lone `void`.
	struct Parameter
	{
		std::string name;
		TypeId type;
	};

	// A value of the 5.19 subset: what it is worth, and the type that says how
	// wide it is and whether it is signed.
	struct Constant
	{
		Constant()
			: type(kNoType)
			, bits(0)
		{}

		TypeId type;
		unsigned long long bits;
	};

	// Declarations (sema_analyzer.cpp).
	void declaration(const AstNode& node, const Context& ctx);
	void namespace_definition(const AstNode& node, const Context& ctx);
	void namespace_alias(const AstNode& node, const Context& ctx);
	void using_directive(const AstNode& node, const Context& ctx);
	void using_declaration(const AstNode& node, const Context& ctx);
	void alias_declaration(const AstNode& node, const Context& ctx);
	void static_assert_declaration(const AstNode& node, const Context& ctx);
	void template_declaration(const AstNode& node, const Context& ctx);
	void template_parameter(const AstNode& node, const Context& ctx);
	void simple_declaration(const AstNode& node, const Context& ctx);
	void condition_declaration(const AstNode& node, const Context& ctx);
	// One declarator of a declaration, with the initializer written for it.
	void init_declarator(const AstNode& node, const AstNode* initializer,
	                     const Specifiers& specifiers, const Context& ctx);
	void function_definition(const AstNode& node, const Context& ctx);
	void statement(const AstNode& node, const Context& ctx);
	SemaEntity& class_declaration(const AstNode& node, const Context& ctx,
	                              const Span& span, bool define,
	                              const std::string& named_by);
	SemaEntity& enum_declaration(const AstNode& node, const Context& ctx,
	                             bool elaborated, const std::string& named_by);
	void enumerators(const AstNode& node, SemaEntity& entity,
	                 const std::string& spelling, DumpScope& dump);
	// 7.1.3p2: the name the first declarator of a declaration gives a class or
	// enumeration its specifiers left unnamed.
	static std::string name_from_declarators(const AstNode& node);
	// 9.5p1: the members of an anonymous union are declared in the region the
	// union is declared in.  Anything else `entity` may be declares nothing
	// there, so every declaration that introduces a class asks.
	void inject_union_members(SemaEntity* entity, const Context& ctx);
	// 9.2p2 and the course ABI: the size and alignment of a completed class.
	void lay_out_class(SemaEntity& entity, Scope& scope, bool is_union);
	// The entity the declaration of a name reuses, or nothing when the name is
	// new to the region.
	SemaEntity* redeclared(const Context& ctx, const std::string& name,
	                       SemaKind kind);
	void write_line(DumpScope& dump, const char* what, const std::string& name,
	                TypeId type);
	void write_entity_line(DumpScope& dump, const SemaEntity& entity);

	// Specifiers, declarators and names (sema_declarator.cpp).
	Specifiers read_specifiers(const AstNode& seq, const Context& ctx,
	                           const Span& span, bool declaration,
	                           const std::string& named_by);
	void read_type_specifier(const AstNode& node, Specifiers& out,
	                         const Context& ctx, const Span& span,
	                         const std::string& named_by);
	TypeId specifier_type(const Specifiers& specifiers);
	TypeId type_id_type(const AstNode& node, const Context& ctx);
	// 8.3: the type `node` derives from `base`, the name it declares, and - for
	// a caller that goes on to open the region of a function definition - the
	// parameters of its outermost parameter-clause, which 8.4.1p1 makes the one
	// that clause declares.  Reading them here is what keeps a parameter
	// clause read once rather than once for the type and again for the names.
	TypeId declarator_type(const AstNode& node, TypeId base, const Context& ctx,
	                       std::string* name,
	                       std::vector<Parameter>* declared = nullptr);
	TypeId apply_pointer(const AstNode& node, TypeId type);
	TypeId apply_suffix(const AstNode& node, TypeId type, const Context& ctx,
	                    std::vector<Parameter>* declared);
	void read_parameters(const AstNode& clause, const Context& ctx,
	                     std::vector<Parameter>& out, bool& variadic);
	// The declarator-id of a declarator, which a nested declarator holds.
	static const AstNode* declarator_id(const AstNode& node);
	// 3.4.3: `name`, which may be written with a nested-name-specifier,
	// resolved from `ctx`.  Null when nothing of the kind is declared.
	SemaEntity* resolve(const std::string& name, const Context& ctx,
	                    LookupKind filter);
	// The region the nested-name-specifier of `name` reaches, for a declaration
	// that names an entity of another region.
	Scope* resolve_prefix(const QualifiedName& name, const Context& ctx);
	SemaEntity& require(SemaEntity* entity, const std::string& name);

	// Constant expressions and decltype (sema_constant.cpp).
	Constant evaluate(const AstNode& node, const Context& ctx);
	Constant literal_constant(const std::string& spelling);
	Constant id_constant(const AstNode& node, const Context& ctx);
	Constant unary_constant(const AstNode& node, const Context& ctx);
	Constant binary_constant(const AstNode& node, const Context& ctx);
	Constant convert(const Constant& value, TypeId type) const;
	Constant promote(const Constant& value);
	// 5p10: the type the usual arithmetic conversions bring two operands to.
	TypeId common_type(TypeId left, TypeId right);
	unsigned long long array_bound(const AstNode& node, const Context& ctx);
	TypeId decltype_type(const AstNode& node, const Context& ctx);
	// 5.3.3 and 5.3.6 over a type-id, which is the whole of what PA11 needs.
	unsigned long long size_of(TypeId type) const;
	bool is_signed(TypeId type) const;
	unsigned width_of(TypeId type) const;
	// The integral type an enumeration's values are read as.
	TypeId arithmetic_type(TypeId type) const;
	// A value as the dump writes it, signed when its type is.
	std::string spell_value(TypeId type, unsigned long long bits) const;

	TypeTable types_;
	SemaModel model_;
	// The unnamed enumerations declared so far, which are numbered rather than
	// named after a token span because that is the convention the refs use.
	unsigned anonymous_enums_;
};
