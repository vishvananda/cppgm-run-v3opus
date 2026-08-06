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

// Which dump the walk writes.
//
// PA11 describes what a translation unit declares; PA12 describes what its
// function bodies mean.  Both read the same declarations from the same tree, so
// one walk serves both and the mode says only which tree of lines it fills and
// which of the two assignments' rules it holds the program to.
enum class SemaDialect
{
	Types,
	Semantics
};

// 3.10: what an expression denotes.
enum class ValueCategory
{
	LValue,
	XValue,
	PRValue
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
	explicit SemaAnalyzer(SemaDialect dialect = SemaDialect::Types);

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
		// The PA12 node a declaration read here writes under, which at block
		// scope is the `simple-declaration` the statement opened.
		DumpNode* node;
	};

	// One analysed expression.
	//
	// `type` is what the operators see, so a reference is already removed from
	// it (5p5).  `spelled` is what the dump writes, which parts company with
	// `type` exactly where the standard makes a reference visible in the
	// result of an expression: a call of a function returning a reference, and
	// a cast to one.
	struct Value
	{
		Value();

		TypeId type;
		TypeId spelled;
		ValueCategory category;
		DumpNode* node;
		// 13.4: the declarations an unresolved function name denotes, which a
		// target type or a call's arguments choose between.
		SemaEntity* functions;
		// What the dump writes after the type, and the node kind it writes
		// before it, so that a cast to a reference can spell the operand's own
		// line again with the category the cast gave it.
		const AstNode* payload;
		const char* what;
		// 4.10p1: an integral constant expression prvalue that evaluates to
		// zero, which is the only integral operand a pointer accepts.
		bool null_constant;
		bool constant;
		unsigned long long value;
	};

	// 13.3.3.1: how good the conversion of one argument is.  The ranks of
	// Table 12, plus what 13.3.3.2p3 and p4 need to tell two of one rank apart.
	struct Match
	{
		Match();

		bool viable;
		// 0 exact, 1 promotion, 2 conversion, 3 ellipsis.
		int rank;
		// 13.3.3.2p4: a conversion to `bool` from a pointer loses to one that
		// keeps the pointer.
		bool to_bool;
		// 13.3.3.2p3: how a reference parameter bound its argument.
		bool reference;
		bool binds_rvalue_ref;
		bool binds_lvalue;
		// The temporary a reference parameter had to convert its argument into,
		// which the dump writes as a cast.
		TypeId materialized;
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
	// 13.1 and 3.5: the declaration a function declarator makes, which is a
	// redeclaration of an earlier one in the same region exactly when their
	// parameter type lists agree.
	SemaEntity& declare_function(const std::string& name, TypeId type,
	                             const Context& target, bool define);
	// The `parameter` lines of a function, and the declarations of them in the
	// region the definition opened.
	void declare_parameters(const std::vector<Parameter>& parameters,
	                        TypeId type, const Context& inner, DumpNode* node);
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

	// Statements (sema_statement.cpp).
	//
	// A statement writes its node under `parent` and declares into `ctx`.  The
	// two are separate because 3.3.3 gives a substatement a region of its own
	// while the dump writes it under the statement that holds it.
	void semantic_statement(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	void block_statement(const AstNode& node, const Context& ctx,
	                     DumpNode& parent);
	void selection_statement(const AstNode& node, const Context& ctx,
	                         DumpNode& parent, const char* what);
	void loop_statement(const AstNode& node, const Context& ctx,
	                    DumpNode& parent, const char* what);
	void for_statement(const AstNode& node, const Context& ctx, DumpNode& parent);
	void case_statement(const AstNode& node, const Context& ctx,
	                    DumpNode& parent, bool is_default);
	void return_statement(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	// 6.4p3 and 6.5p2: the condition of a selection or iteration statement,
	// which is either an expression or a declaration whose name the statement's
	// substatements can see.
	// `integral` for the condition of a switch, which 6.4.2p2 converts to an
	// integral or enumeration type rather than to bool.
	void condition(const AstNode& node, const Context& ctx, DumpNode& parent,
	               bool integral);
	// The region a substatement that is not a compound-statement opens (6.4p1).
	Context substatement_scope(const Context& ctx);

	// Expressions (sema_expression.cpp).
	Value expression(const AstNode& node, const Context& ctx, DumpNode& parent);
	Value id_expression(const AstNode& node, const Context& ctx, DumpNode& parent);
	Value literal_expression(const AstNode& node, DumpNode& parent);
	Value call_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	Value functional_cast(const AstNode& node, const Context& ctx,
	                      DumpNode& parent, TypeId target);
	// 8.5: the initializer written for a declarator, in each of the three forms
	// 8.5p1 gives it.
	void write_initializer(const AstNode& initializer, TypeId type,
	                       const Context& ctx, DumpNode& line);
	// 7.1.6.2 Table 10: the type a one-token simple-type-specifier names, which
	// is how a functional cast written with a keyword finds its type.
	TypeId keyword_type(const std::string& spelling) const;
	Value unary_expression(const AstNode& node, const Context& ctx,
	                       DumpNode& parent);
	Value increment_expression(const AstNode& node, const Context& ctx,
	                           DumpNode& parent, bool postfix);
	Value binary_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	Value assignment_expression(const AstNode& node, const Context& ctx,
	                            DumpNode& parent);
	Value conditional_expression(const AstNode& node, const Context& ctx,
	                             DumpNode& parent);
	Value subscript_expression(const AstNode& node, const Context& ctx,
	                           DumpNode& parent);
	// 5.6 to 5.15: the type a built-in binary operator gives its operands.
	TypeId binary_result(unsigned op, const Value& left, const Value& right);
	// 5.17p7: the built-in operator a compound assignment is written from.
	static unsigned compound_operator(unsigned op);
	Value cast_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	Value sizeof_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 5.19 and the course builtins: a call the translation answers itself.
	bool builtin_call(const std::string& name, const AstNode& node,
	                  const Context& ctx, DumpNode& parent, Value& out);

	// 8.5: initialising an object of `target` from `node`, which is what a
	// variable, a condition, a return statement and an argument all do.
	Value initialize(const AstNode& node, TypeId target, const Context& ctx,
	                 DumpNode& parent);
	// 13.3.3.1 over the PA12 conversion subset.
	Match match_argument(const Value& argument, TypeId parameter);
	Match match_by_value(const Value& argument, TypeId parameter);
	Match match_reference(const Value& argument, TypeId parameter);
	// 4.4 and 4.10: whether a pointer of `from` converts to one of `to`.
	bool pointer_convertible(TypeId from, TypeId to, int& rank, bool& exact);
	bool qualification_convertible(TypeId from, TypeId to);
	// 3.9.3p5: `type` with every top level cv-qualifier removed, reaching
	// through an array to its elements.
	TypeId bare_type(TypeId type);
	// 13.3.3: the one candidate no other beats, or the error that there is not
	// one.  `arguments` are the analysed argument expressions in order.
	SemaEntity* select_overload(SemaEntity* candidates,
	                            const std::vector<Value>& arguments,
	                            const std::string& name);
	// 13.3.3.2: which of two conversions of one argument is better, as 1, 0
	// or -1.
	int compare_matches(const Match& left, const Match& right);
	// 13.3.3p1: whether one candidate's conversions beat another's.
	bool better_candidate(const Match* left, const Match* right,
	                      std::size_t count);
	// Rewrites what the dump wrote for `value` where a conversion is visible in
	// it: a null pointer constant, a resolved function name, and the temporary
	// a reference binds to.
	void apply_conversion(Value& value, TypeId target, const Match& match,
	                      DumpNode& parent);
	// 13.4: the declaration of an overloaded name a target type asks for.
	SemaEntity* resolve_target(const Value& value, TypeId target);
	// Writes the line of an id-expression once its overload set is resolved.
	void name_function(Value& value, SemaEntity& function, const char* what);
	// 4.1, 4.2 and 4.3: the type the value of `value` has.
	TypeId decayed(const Value& value);
	void require_complete_value(const Value& value);
	// 4.5: the type one operand of an integral operation is promoted to.
	TypeId promoted(TypeId type);
	// 5p9: the type the usual arithmetic conversions bring two operands to.
	TypeId arithmetic_result(TypeId left, TypeId right);
	// 5.9p2 and 5.10p1: the composite pointer type two pointer operands are
	// compared as, or kNoType when they have none.
	TypeId composite_pointer(const Value& left, const Value& right);
	bool is_arithmetic(TypeId type) const;
	bool is_integral(TypeId type) const;
	bool is_floating(TypeId type) const;
	bool is_scoped(TypeId type) const;
	bool is_object_pointer(TypeId type) const;
	// 4.12: whether `type` has a conversion to `bool`, which every condition
	// and logical operand needs.
	bool contextually_bool(TypeId type) const;

	// The dump.
	std::string spell(const char* what, ValueCategory category, TypeId type,
	                  const AstNode* payload) const;
	static std::string payload_of(const AstNode& node);
	static const char* category_name(ValueCategory category);
	// The name the PA12 dump gives a declaration of `scope`.
	std::string dump_name(const Scope& scope, const std::string& name) const;
	bool semantics() const { return dialect_ == SemaDialect::Semantics; }

	SemaDialect dialect_;
	TypeTable types_;
	SemaModel model_;
	// The unnamed enumerations declared so far, which are numbered rather than
	// named after a token span because that is the convention the refs use.
	unsigned anonymous_enums_;
	// 6.6.1 and 6.6.2: the statements a `break` or a `continue` may leave, and
	// 6.4.2 the switch a `case` may label, as the counts of the enclosing ones.
	unsigned breakable_;
	unsigned continuable_;
	unsigned switches_;
	// 6.6.3: the return type of the function whose body is being read.
	TypeId returns_;
};
