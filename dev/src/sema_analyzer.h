#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "name_table.h"
#include "parse_depth.h"
#include "sema_declaration.h"
#include "sema_name.h"
#include "sema_reading.h"
#include "sema_scope.h"
#include "sema_template.h"
#include "sema_value.h"
#include "type_model.h"

struct AstNode;
struct PostToken;
class AstArena;
class ArgumentLookup;
class PackTable;
class IncludeTable;

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

	// 16.6: what `#pragma pack` asked for, by position in the token stream the
	// tree was parsed from.  Borrowed rather than copied, and left null by a
	// caller with no such table, which is what says no class is packed.
	void set_packing(const PackTable& packs) { packs_ = &packs; }

	// 2.2p1: which positions of that stream this unit's own source wrote.
	// Borrowed the same way, and left null by a caller with no such table,
	// which is what says every definition read is this unit's own.
	void set_sources(const IncludeTable& sources) { sources_ = &sources; }

	// 7.1.6.2p1: the trees the parse read for the decltype-specifiers it then
	// flattened into a name, borrowed the same way.  Null says no spelling
	// holds one, which is every mode that reads no template-argument-list.
	void set_expressions(const AstArena& written) { written_ = &written; }

	// Analyses `unit`, a PA10 `translation-unit`.  Throws for a program the
	// assignment gives no meaning to.
	void run(const AstNode& unit);

	void write(std::ostream& out) const;

	// The resolved program of the unit just analysed, for a caller that lowers
	// it.  The tree of lines is the resolved procedural tree, each line
	// carrying the typed facts of `SemaFact`; the table answers what a type is.
	const DumpNode& resolved() const { return model_.unit(); }
	TypeTable& types() { return types_; }
	// 5.19p2: the pool the identifier a constant of pointer type carries
	// indexes into, which is what says *which object* an address constant
	// designates.
	AddressTable& addresses() { return model_.addresses(); }

private:
	// 6.6.1, 6.6.3, 12.2p3 and 14.6p8: what one reading of a function body and
	// one reading of a template's pattern put aside, which `sema_declaration.h`
	// owns beside the other records the declaration layer passes between its
	// steps.  Each saves this walk's own state, so each is a friend of it.
	friend class FunctionReading;
	friend class DialectReading;
	// 14.5.3: the reading that turns one pack expansion into the run it stands
	// for, which `sema_pack.h` owns because it is a reading of its own.
	friend class PackReading;
	// 14.8.2: the match that turns a use of a function template into the
	// argument list it deduces, which `sema_deduce.h` owns for the same reason.
	friend class Deduction;
	friend class Derivation;
	friend class StringInitialization;
	// 14.5.5 and 14.5.1p1: the two declarations a template head writes that are
	// neither the primary template nor a specialization of it, which
	// `sema_specialize.h` owns because each changes one step of the three.
	friend class Specialization;
	// 14.6p8 and 14.6.1p1: the reading a template definition gets where it
	// stands and the current instantiation it is read as, which
	// `sema_pattern.h` owns because it is a reading of its own.
	friend class PatternReading;
	// 7.1.5p2: the call of a constexpr function 5.19p2 folds, which walks a
	// function-body and is `sema_constexpr.h`'s for that reason.
	friend class ConstexprReading;
	// 7.1.5: what a declaration written `constexpr` shall be, which is the
	// other half of that header's subject and no part of any one fold.
	friend class ConstexprRequirement;
	// 3.4.2: the lookup that follows the argument types rather than the
	// regions, which `sema_argument_lookup.h` owns for the same reason.
	friend class ArgumentLookup;
	// 14.5.6.1p5's canonical form, which four tiers compare two declarations
	// by - and 13.1's own index keys every declaration of a name with.
	friend class TemplateSignature;
	// 13.3.1.2p3: the candidate set an operator reaches, which the expression
	// layer and a fold ask alike - `sema_operator.h`'s for that reason.
	friend class OperatorCall;

	// 3.3, 7p1, 8.3.5p4, 12.6.2p1 and 5.19p3: the records the declaration
	// layer passes between its steps, which `sema_declaration.h` defines.  The
	// walk spells them by their short names, as it did while they stood here.
	typedef SemaContext Context;
	typedef SemaSpan Span;
	typedef DeclSpecifiers Specifiers;
	typedef DeclaredParameter Parameter;
	typedef PendingDefinition Pending;
	typedef HeldTemplateBody HeldPatternBody;
	typedef BitFieldUnit BitUnit;
	typedef InitializerClauses Clauses;
	typedef WrittenMemInitializer MemInitializer;
	typedef SemaConstant Constant;
	typedef AssociatedRegions Associated;


	// 5p1 and 13.3: one analysed expression and one ranked candidate, which
	// the expression layer owns and `sema_value.h` defines.  The walk spells
	// them by their short names, as it did while they stood here.
	typedef AnalyzedValue Value;
	typedef OverloadMatch Match;

	// Where the object an initialization or a destruction acts on stands.
	// 8.5p1's placement and 12.2p1's asking place, which `sema_declaration.h`
	// defines with the rest of the declaration layer's vocabulary.
	typedef ObjectPlacement Placement;
	typedef TemporaryRequest Requested;

	// The prefix `Requested` gives a temporary an argument conversion made.
	// `reference` says the place binds a reference to it rather than holding
	// the object itself, which 8.5.3p5 makes storage of the argument and not an
	// object the call is passed.  `passed` is the type the object standing there
	// has, which 5.2.2p4 reads to say whether the boundary carries it as bytes
	// or as the address of the one object the caller and the callee share.
	const char* requested_prefix(Requested by, bool reference, TypeId passed);


	// Declarations (sema_analyzer.cpp).
	void declaration(const AstNode& node, const Context& ctx);
	void namespace_definition(const AstNode& node, const Context& ctx);
	void namespace_alias(const AstNode& node, const Context& ctx);
	void using_directive(const AstNode& node, const Context& ctx);
	void using_declaration(const AstNode& node, const Context& ctx);
	// 12.9p1 over 3.4.3.1p2: the using-declaration that names the constructors
	// of the class its nested-name-specifier nominates.
	void inheriting_declaration(const QualifiedName& written,
	                            const Context& ctx);
	// 7.3.3p1: the members a using-declaration written in a class declares -
	// one per declaration `named` heads - and 7.3.3p14's rule that a
	// declaration the class itself made of the same name and parameter list
	// hides the one the base made, so it is not brought in at all.
	void declare_using_members(SemaEntity& named, Scope& where,
	                           const std::string& name);
	// One of them: a declaration of this class naming what the base declared.
	SemaEntity& declare_using_member(SemaEntity& target, Scope& where,
	                                 const std::string& name);
	// 12.9p1: the constructors a using-declaration naming a direct base class's
	// constructors declares in `where`, one per member of the base's candidate
	// set of inherited constructors other than the ones 12.9p3 leaves out.
	void inherit_constructors(SemaEntity& base, Scope& where);
	// One of that set: the first `taken` parameters of `from`, which is the
	// whole of its parameter-type-list when `whole`.
	void inherit_constructor(SemaEntity& from, const SemaEntity& base,
	                         std::size_t taken, bool whole, SemaEntity& derived,
	                         Scope& where, const std::string& spelled);
	// 12.9p1 and 8.3.6p4: how many parameters a call of `function` has to write,
	// which is where its default-arguments begin - the shortest parameter list
	// 12.9p1's candidate set takes from this declaration.
	std::size_t required_parameters(const SemaEntity& function) const;
	// 13.1 and 7.3.3p14: what tells two declarations of one name in one class
	// apart - 8.3.5p4's parameter-type-list and 8.3.5p7's cv-qualifier-seq.
	// 9.3.1p3 put an object parameter in a non-static member function's type
	// that no declarator wrote, so it is left out of the list and its
	// qualifiers stand beside it; a static member function wrote no
	// cv-qualifier-seq and declares the list its declarator wrote.  That is
	// what makes a static and a non-static declaration of one class hide each
	// other rather than overload, which 9.4.1p2 does not allow.
	std::uint32_t member_signature(const SemaEntity& function);
	std::uint32_t member_signature(TypeId type, bool object_member);
	// 10.3p2's key: that signature beside the interned name, which is what tells
	// an overriding declaration from one that merely reuses a name.
	std::uint64_t override_key(const SemaEntity& member);
	// 10.3p2: the slot a declaration of `key` already has in the table of the
	// class `from` or of one it derives from, or `kNoVtableIndex` where none of
	// them gave it one.
	unsigned inherited_slot(const SemaEntity* from, std::uint64_t key) const;
	// 8.3.5p1 and 9.3.1p3: how a function's type is spelled in the output,
	// which for the lowered form spells the object parameter rather than the
	// qualifiers written after the parameter-clause.
	TypeId function_description_type(TypeId type, bool object_member);
	std::string function_description(TypeId type, bool object_member);
	// 13.1p2: a class shall not declare one member function both with and
	// without a ref-qualifier where the two agree in everything else.
	void require_uniform_ref_qualifiers(const SemaEntity& head,
	                                    const std::string& name, TypeId type,
	                                    const Scope* written_under);
	// 7.3.3p1: the declaration a name a using-declaration brought into a class
	// reaches, which is the one the base made.  11p1's access and 7.3.3p14's
	// hiding are facts about the declaration the class made; everything a use
	// of the member needs is a fact about the one it names.
	static SemaEntity& declared_member(SemaEntity& entity);
	static const SemaEntity& declared_member(const SemaEntity& entity);
	// 8.3.6p4 and 14.7.1p1: the declaration a default-argument stands on.  A
	// specialization is a declaration nothing wrote, so what wrote its
	// defaults is the template it was made of; every other declaration is the
	// one 7.3.3p1 reaches through a using-declaration.
	static const SemaEntity& wrote_defaults(const SemaEntity& entity);
	// 14.6.1p6: a template-parameter shall not be redeclared within its scope,
	// which is every region nested in the one its head declared it in.  Asked
	// where a name is bound, so that a typedef, an alias-declaration, an object
	// and a using-declaration each reach it.
	void require_no_template_parameter(const std::string& name,
	                                   const Scope& where);
	// 7.1.3p3 and 9.2p1: declares the typedef-name `name` for `aliased` in
	// `where`.  A class may not declare one it already declares, however the
	// two spell the type; a namespace may.
	SemaEntity& declare_type_alias(const std::string& name, TypeId aliased,
	                               Scope& where);
	// 3.3.10p2, 9.2p1 and 14.6.1p6: the same two questions asked where a
	// class-name or an enum-name is bound.  7.1.3p3's leniency belongs to a
	// typedef-name alone, so a type-name declared where the region already
	// declares a typedef-name of that spelling is refused.
	void declare_type_name(const std::string& name, Scope& where);
	void alias_declaration(const AstNode& node, const Context& ctx);
	void static_assert_declaration(const AstNode& node, const Context& ctx);
	void template_declaration(const AstNode& node, const Context& ctx);
	// 14.1p1: the head's own region, opened over the declaration it
	// parameterises.  It is the whole of `template_declaration` where nothing
	// is recorded, and 14.5.2p3's second head reaches it on its own.
	void read_template_head(const AstNode& node, const Context& ctx);
	// 14.7.2p1: a specialization named where no use of it stands.
	void explicit_instantiation(const AstNode& node, const Context& ctx);
	// 14.7.2p1's simple-declaration target, which names a function or an
	// object rather than a class.  `owed` is p8's form, which asks this unit
	// for the definitions; p9's asks for none and names the same
	// specialization.
	void explicit_instantiation_declarator(const AstNode& target,
	                                       const Context& ctx, bool owed);
	// 14.7.2p2: the class template specialization the elaborated name either
	// form wrote names, which both forms ask for alike.
	SemaEntity* instantiated_class(const std::string& written,
	                               const Context& ctx);
	// 14.7.2p2: the specialization the type such a declaration wrote names.
	SemaEntity* instantiation_named(const std::string& written,
	                                const std::string& name, TypeId declared,
	                                TypeId member, const Context& ctx,
	                                bool instantiated_region);
	void template_parameter(const AstNode& node, const Context& ctx);
	void non_type_template_parameter(const AstNode& node, const Context& ctx);
	void simple_declaration(const AstNode& node, const Context& ctx);
	void condition_declaration(const AstNode& node, const Context& ctx);
	// One declarator of a declaration, with the initializer written for it and
	// the init-declarator the two stand under - whose span 3.7.1p3's
	// block-scope object takes the name of its storage from.
	void init_declarator(const AstNode& node, const AstNode* initializer,
	                     const Specifiers& specifiers, const Context& ctx,
	                     const AstNode* whole = nullptr);
	// 8.3.5 and 13.1: one declarator of a declaration that declares a function,
	// from the point its type is known.  `granting` is the class a friend
	// declaration gives this declaration the access of, and null for every
	// other declaration.
	// 7.1.5p1, 10.3p1, 11.3p5, 3.5p3 and 7.1.2p2: what a definition's own
	// declaration says about the function, which its body does not - and which
	// stands for the function however its other declarations were written.
	void record_definition_binding(SemaEntity& entity, const AstNode& node,
	                               const Specifiers& specifiers,
	                               const Context& ctx, const Context& target,
	                               const QualifiedName& spelled,
	                               SemaEntity* granting, TypeId type,
	                               TypeId written_type);
	void declare_function_declarator(const AstNode& node,
	                                 const std::string& name, TypeId type,
	                                 const QualifiedName& spelled,
	                                 const Specifiers& specifiers,
	                                 const Context& target,
	                                 SemaEntity* granting,
	                                 std::vector<Parameter>& spelled_parameters,
	                                 const AstNode* initializer);
	void function_definition(const AstNode& node, const Context& ctx);
	void statement(const AstNode& node, const Context& ctx);
	// 14.7.1p1: a specialization is read from the same class-specifier the
	// template wrote, so `as` and `spelled_as` are what an instantiation hands
	// in - the declaration it already made for the specialization, and the
	// name the template-id gave it.  Both are null for every class the program
	// declared itself, which is the one the class-head names.
	SemaEntity& class_declaration(const AstNode& node, const Context& ctx,
	                              const Span& span, bool define,
	                              const std::string& named_by,
	                              SemaEntity* as = nullptr,
	                              const std::string* spelled_as = nullptr);
	// 9.1p2: the declaration a class-head names - one an earlier declaration in
	// the region already made, or one this class-head makes.
	// `declaring` is the region the class-head-name reaches, which is `ctx`'s
	// own where no nested-name-specifier was written.
	SemaEntity* class_head_entity(const Context& ctx, ClassTag tag,
	                              const QualifiedName& spelled,
	                              const std::string& written, bool define,
	                              Scope* declaring);
	// 14.6.2.1p9: a class or enumeration the current instantiation declares is
	// a dependent type, which is a fact of the region it was declared in.
	void note_nested_in_dependent(TypeId type, const Scope& where);
	SemaEntity& enum_declaration(const AstNode& node, const Context& ctx,
	                             bool elaborated, const std::string& named_by);
	void enumerators(const AstNode& node, SemaEntity& entity,
	                 const std::string& spelling, DumpScope& dump);
	// 7.1.3p2: the name the first declarator of a declaration gives a class or
	// enumeration its specifiers left unnamed.
	static std::string name_from_declarators(const AstNode& node);
	// 9.5p1: the members of an anonymous class are declared in the region the
	// class is declared in, and are members of the object that class declared.
	// The standard writes it for a union and the README puts the anonymous
	// struct in this milestone's subset; the two differ in what the class's own
	// layout does with its members and in nothing here.  Anything else `entity`
	// may be declares nothing there, so every declaration that introduces a
	// class asks.
	// `is_static` is what 9.5p3 asks for at namespace scope, where the object
	// the class declares is one this unit alone holds.
	void inject_anonymous_members(SemaEntity* entity, const Context& ctx,
	                              const Span& span, bool is_static);
	// 12.1 and 12.4: a constructor or destructor a class body declares, which
	// is a member declaration with no decl-specifier-seq whose name is the
	// class's own.  It declares into the class and, where a body is written,
	// leaves the definition for the end of the translation unit as 9.2p2 does
	// for every member function defined in its class.
	void special_member(const AstNode& node, const Context& ctx);
	// 9.3p2 and 3.4.3p3: a constructor or destructor defined outside its class,
	// whose declarator-id names the class the definition belongs to.  It
	// defines the declaration that class already made rather than declaring a
	// second one.
	void special_member_definition(const AstNode& node, const Context& ctx);
	// 12.1p1 and 12.4p1: the type a special member declarator writes, read
	// against the class it belongs to wherever the declarator was written.
	TypeId special_member_type(const AstNode& node, const Context& ctx,
	                           const SemaEntity& owner, bool destructor,
	                           std::vector<Parameter>& parameters,
	                           bool& variadic);
	// 12.1p1: the class name a special member declarator wrote, checked against
	// the class it belongs to.
	std::string special_member_name(const std::string& written,
	                                const SemaEntity& owner);
	// 12.3.2: a conversion function declared or defined in a class body, and
	// one defined outside it.  Both read the conversion-type-id the parse
	// carried beside the name, because two spellings of one type declare one
	// function and only the resolved type says so.
	// 14.5.2p1: `specializing` is 14.7.1p1's declaration a reading of the
	// pattern for one argument list is giving the type and the body to, and
	// null wherever this declares a conversion function of its own.
	void conversion_function(const AstNode& node, const Context& ctx,
	                         const AstNode& carried, SemaEntity* specializing);
	bool conversion_function_definition(const AstNode& node,
	                                    const Context& ctx);
	// 12.3.2p1: the declaration `carried` makes in the class `target` is the
	// region of, declared or found among the ones already there.
	SemaEntity& declare_conversion(const AstNode& node, const Context& target,
	                               const AstNode& carried,
	                               SemaEntity* as = nullptr);
	// 12.3.2p1: the name a member access looked a conversion-function-id up
	// under, which is the type it named rather than the tokens it spelled -
	// `a.operator T()` and `a.operator U()` name one function where `T` and `U`
	// are one type.  Any other member id is the name the program wrote.
	std::string member_id_name(const AstNode& id, const Context& ctx);
	// 12.3.2p1: the name a region binds a conversion to `type` under, which is
	// the type and never the tokens the declaration spelled it with.
	std::string conversion_name(TypeId type) const;
	// 12.3.2p1 and 13.3.1.5: the conversion functions an object of `entity` has
	// - its own, then its base's that its own do not hide - settled where 9.2p2
	// completes the class.
	void collect_conversions(SemaEntity& entity, Scope& scope);
	// 13.3.1.5p1: the candidate set a conversion of an object of `owner` chooses
	// from, written into `out`.
	void gather_conversions(const SemaEntity& owner,
	                        std::vector<SemaEntity*>& out);
	// 9.2p2: the body a special member's definition gives it, left for the end
	// of the translation unit whether the definition was written in the class
	// body or outside it.
	// 14.5.2p1: `head` is the region a template head standing over the
	// definition declared its places in, which makes this the pattern of a
	// member template - 14.6p8 reads such a body where it stands and leaves the
	// output nothing to write.  Null for every definition of a function.
	void open_special_member_body(const AstNode& node, SemaEntity& entity,
	                              const Context& ctx,
	                              const std::string& written,
	                              const std::vector<Parameter>& parameters,
	                              Scope* head = nullptr);
	// 12.1p5 and 12.4p3: the constructor and the destructor a class with no
	// declared one has, declared into the class where its definition ends.
	// 9.2p2: the special members the class has where its definition ends.
	void declare_special_members(SemaEntity& entity, Scope& scope);
	void declare_constructor(SemaEntity& entity, Scope& scope);
	void declare_destructor(SemaEntity& entity, Scope& scope);
	// 12.8p2/p3/p17/p19: which of the four value-transfer special members one
	// declaration of a class declares, and the same asked of every declaration
	// the class holds.
	unsigned char transfer_kind(const SemaEntity& function,
	                            const SemaEntity& owner);
	void note_transfers(SemaEntity& entity, Scope& scope);
	void note_transfer(SemaEntity& entity, SemaEntity& function);
	// 13.5.3p1: the declarations of `operator=` one class's own region holds,
	// which is what 12.8 asks about - a base's or an enclosing class's are
	// another class's members.
	SemaEntity* own_assignments(Scope& scope);
	// 12.8p7/p9/p18/p20: the value-transfer members the standard rather than
	// the program declares, added where 9.2p2 completes the class.
	void declare_transfer_members(SemaEntity& entity, Scope& scope,
	                              bool wrote_destructor);
	void declare_transfer_member(SemaEntity& entity, Scope& scope,
	                             unsigned char kind, bool deleted);
	// 12.8p11/p12/p23/p25: whether each of them is one the standard can define
	// and whether its definition is the copy of an object's bytes - and 8.4.2p2
	// the same again, where a definition outside the class is what said so.
	void settle_transfers(SemaEntity& entity, Scope& scope);
	// 9.2p2 and 8.4.2p2: the answers a complete class carries, settled where its
	// class-specifier closes and asked again wherever a definition written later
	// changes one - over that class, and then over every class settled before it.
	void settle_class_answers(SemaEntity& entity, Scope& scope);
	void resettle_defaulted_member(SemaEntity& function);
	void resettle_completed_classes();
	// 10.3p1 read before 9.2p13 lays the class out: whether an object of the
	// class carries a vpointer, and whether this class is the one that adds it.
	void note_polymorphism(SemaEntity& entity, Scope& scope);
	// 10.3p2/p10 and 10.4p2, settled after the class has every member it will
	// have: which of them are virtual, which slot each has, what the class's
	// table holds, and whether a final overrider of it is still pure.
	void settle_virtual_members(SemaEntity& entity, Scope& scope);
	void settle_virtual_destructor(SemaEntity& entity, SemaEntity& destructor);
	// The ABI, read once the table is settled: which unit owes the program this
	// class's table, and what the deleting entry of its destructor gives the
	// storage back to.
	void settle_vtable_ownership(SemaEntity& entity, Scope& scope);
	// 10.3p4, 10.3p5 and 10.3p7: what an overriding declaration has to agree
	// with, and what a declaration that overrides nothing may not have written.
	void require_overridable(const SemaEntity& member,
	                         const SemaEntity& overridden);
	// 7.1.2p1, 9.2p8 and 8.4p2: whether this declaration may say anything about
	// dispatch at all, which only the one a class body makes may.
	static void require_virtual_placement(bool wrote_virtual,
	                                      const AstNode* declarator,
	                                      const Scope& where, bool qualified,
	                                      const std::string& name);
	void require_special_virtual_placement(const AstNode& node,
	                                       const Scope& where, bool qualified,
	                                       const std::string& name);
	bool covariant_return(TypeId overriding, TypeId overridden, Scope* where);
	// 10.4p2/p3: whether an object of the type can be created at all, and the
	// two boundaries of a function declaration where one may not stand.
	bool abstract_class_type(TypeId type);
	void require_no_abstract_boundary(TypeId type, const std::string& name);
	void require_creatable_object(TypeId type, const std::string& name);
	// 9.2 and 10.3: `override`, `final` and 10.4p2's pure-specifier, which a
	// member function's declarator writes rather than its specifiers.
	void read_virt_specifiers(SemaEntity& function, const AstNode& declarator,
	                          const AstNode* initializer);
	// 12.8p15/p28: the value-transfer member one subobject of class type is
	// carried by, which for a move falls back on the class's copy member.
	SemaEntity* selected_transfer(TypeId type, unsigned char kind);
	// 12.8p32: the constructor a copy 12.8p31 elided would have called, asked
	// for where the program wrote the initialization it elided from.
	void require_elided_transfer(TypeId type, const Context& ctx);
	// 8.3.6p1: whether the parameter at `index` of this function's type has a
	// default argument, which is what lets 12.8p2 count a constructor with more
	// parameters as a copy constructor.
	bool has_default_argument(const SemaEntity& function, std::size_t index);
	// 11.2p1: whether a member of another class may be named from `scope`.
	bool accessible(const SemaEntity& member, Scope& scope);
	// 12.1p5 and 8.5p6: the constructors of the class `type`, as the chain
	// 13.3.1.3 chooses from, and the destructor 12.4p3 gives it.  Null for
	// anything that is not a class type this unit completed.
	SemaEntity* class_constructors(TypeId type);
	// 8.5p7: whether the program wrote a constructor of the class whose
	// declarations `head` begins, which is what says a value-initialized object
	// of it is not zero-initialized first.
	static bool user_provided_constructor(const SemaEntity& head);
	// 13.3.3.1.2p1: the converting constructor of `target` that a user-defined
	// conversion sequence for `argument` calls, or null where none does or
	// where two do equally well.
	SemaEntity* converting_constructor(const Value& argument, TypeId target);
	// 12.3.2p1 and 13.3.1.5p1: the user-defined conversion sequence a conversion
	// function of the argument's class makes to `parameter`, or a sequence that
	// is not viable where no one of them answers.  `direct` is 12.3.2p2's
	// question - a direct-initialization, an explicit cast and a contextual
	// conversion may choose one declared `explicit`, and a copy-initialization
	// may not.  The sequence the call's result takes to the parameter is the
	// second, standard one 13.3.3.2p3 orders two of these by.
	Match conversion_match(const Value& argument, TypeId parameter, bool direct);
	// 5.2.9p4, 5.4p4 and 8.5p16: an operand of class type where the program
	// asked for a value of another type by a direct-initialization or an
	// explicit cast, which 12.3.2p2 lets choose a conversion function declared
	// `explicit`.  True when a conversion was written.
	bool explicit_conversion(Value& value, TypeId target, const Context& ctx);
	// 5.2.9p4 and 5.4p4: the same question a cast asks, with the refusal that
	// an operand of class type no conversion function carries to the target is
	// a cast that reads nothing rather than one that reads the object's bytes.
	void cast_conversion(Value& source, TypeId target, const Context& ctx);
	// 5.2.2p3: the value a call of `chosen` on an object of its class hands
	// back, as the analysis reads it.
	Value conversion_result(const SemaEntity& chosen) const;
	// 4p3: a value of class type written where the language itself needs a
	// `bool` - a condition, `!`, `&&`, `||`, `?:` - becomes what a conversion
	// function of its class hands back, chosen as a direct-initialization of
	// `bool`, which 12.3.2p2 lets be one declared `explicit`.  A value of any
	// other type is left as it stands and the caller's own check refuses what no
	// conversion reaches.
	void contextual_bool(Value& value, const Context& ctx);
	// 6.4.2p2: the same question for a switch condition, whose class shall have
	// one conversion to an integral or enumeration type and no `explicit` one
	// answers it.
	void contextual_integral(Value& value, const Context& ctx);
	// 13.6: the type a built-in operator reads an operand of class type as,
	// which is the one type that class converts to that a built-in operator has
	// an operand of.  `kNoType` where the class converts to no such type or to
	// more than one, which leaves the built-in operators with no one candidate.
	// `reference` asks 13.6p3 and p5's question instead - which lvalue the
	// class hands back, for the operators written over one.
	TypeId builtin_conversion_type(const Value& value, bool reference = false);
	// 13.6 and 13.3.1.2p2: the operands a built-in operator reads, where one of
	// them is of class type and reaches that operator through a conversion
	// function.  True when any operand was converted.
	bool builtin_operands(unsigned token, const Context& ctx,
	                      std::vector<Value>& operands);
	// 13.6 and 13.3.3p1: whether the built-in operator these operands reach
	// reads them better than the operator function 13.3 chose.  The built-in
	// candidates are candidates of the same set, so the two are compared on the
	// same argument list; what tells them apart in practice is how many
	// user-defined conversions each operand takes.
	bool better_builtin(const SemaEntity& chosen, const Value& object,
	                    const std::vector<Value>& operands);
	// 12.3.2 and 5.2.2: the call of a conversion function, written around the
	// operand in the place that operand already had.
	Value call_conversion(const Value& object, SemaEntity& chosen,
	                      const Context& ctx);
	SemaEntity* class_destructor(TypeId type);
	// 8.5.1p1: whether an object of `type` is initialized from a
	// braced-init-list by initializing its members with the clauses.
	bool aggregate_type(TypeId type);
	// 12.1p5: whether default-initializing or destroying an object of the class
	// `scope` declares does nothing, so that no call has to be made for one.
	bool trivial_default_construction(Scope& scope);
	bool trivial_destruction(Scope& scope);
	// 12.4p3: whether any subobject of the class holds a destructor the program
	// itself declared, which is the subobject half of the class's own answer.
	bool subobject_declares_destruction(SemaEntity& entity, Scope& scope);
	// 15.4p14 and 12.4p3: whether the destructor of the class `scope` declares
	// throws nothing, which is the same walk asked of what each subobject's own
	// destructor allows rather than of what running it comes to.
	bool destruction_nonthrowing(Scope& scope);
	// 15.4p14 and 12.1p5: the same reading of the default constructor - what the
	// default constructor of the base subobject and of each member of class type
	// allows, which is what the definition the standard gives it invokes.
	bool default_construction_nonthrowing(Scope& scope);
	// 12.6.2: the member initializations of one constructor, in the declaration
	// order 12.6.2p10 gives them whatever order the mem-initializers were
	// written in.  Each is written under the constructor's own definition.
	void write_member_initializations(const Pending& pending, DumpNode& line,
	                                  const Context& inner);
	// 12.6.2p10 and 12.9p8: what the base class subobject is initialized with,
	// which is settled before any member is however the ctor-initializer was
	// ordered.
	void write_base_initialization(
		const Pending& pending, DumpNode& line,
		std::unordered_map<std::string, MemInitializer>& named,
		const Context& inner);
	void write_one_base_initialization(
		const Pending& pending, DumpNode& line,
		std::unordered_map<std::string, MemInitializer>& named,
		const Context& inner, SemaEntity& base);
	// 12.6.2p10 and 12.6.2p6: which mem-initializer names each member, as one
	// index built once per definition and keyed by the unqualified name.
	void read_mem_initializers(
		const Pending& pending,
		const Context& inner,
		std::unordered_map<std::string, MemInitializer>& named);
	// 12.6.2p2: the name one entry of that index is held under, and the holding
	// itself - which 12.6.2p6 refuses a second entry of one name at.
	std::string base_key(const std::string& written, const Context& where,
	                     const SemaEntity* owner);
	bool names_direct_base(const SemaEntity& owner, TypeId type);
	void hold_mem_initializer(
		const Pending& pending, const std::string& key,
		const MemInitializer& wrote,
		std::unordered_map<std::string, MemInitializer>& named);
	// 12.6.2p6: whether this ctor-initializer delegates - whether one of its
	// mem-initializer-ids names the constructor's own class rather than a base
	// or a member of it - and, where it does, the arguments that one wrote.  The
	// class's own name answers it in one probe; a name that reaches the class
	// some other way is looked up only where the list holds the one entry p6
	// allows a delegating ctor-initializer and that entry names no member and
	// no base by its own name.  Null where nothing delegates.
	const AstNode* delegating_initializer(
		const Pending& pending,
		std::unordered_map<std::string, MemInitializer>& named,
		const Context& inner);
	// 12.6.2p6: what a delegating constructor does before its body - one call of
	// the constructor 13.3 chose, on the object this one is already running on.
	// No base and no member of its own is initialized here: the constructor it
	// delegates to initializes every one of them.
	void write_delegating_initialization(const Pending& pending, DumpNode& line,
	                                     const AstNode* written,
	                                     const Context& inner);
	// 12.6.2p6: a constructor shall not delegate to itself, directly or
	// indirectly.  Every edge is known once the unit has been read, and each
	// constructor has at most one, so the whole check is one colouring of the
	// constructors that delegate - each visited once however many chains run
	// through it.
	void check_delegation_cycles();
	// 8.5.1p2: what the constructor an aggregate class was given does - each
	// member initialized with the parameter of the same name.
	void write_member_parameters(const Pending& pending, DumpNode& line,
	                             const Context& inner);
	// 12.8p15 and p28: what a value-transfer member the standard defines does -
	// each subobject carried from the corresponding subobject of its parameter,
	// with the leading run whose bytes a copy carries exactly carried in one
	// piece.
	void write_transfer_steps(const Pending& pending, DumpNode& line,
	                          const Context& inner);
	void write_transfer_assignment(SemaEntity& subobject, const Value& source,
	                               DumpNode& line, const Context& inner,
	                               Placement where, bool elements = false);
	void write_storage_transfer(SemaEntity& parameter, DumpNode& line,
	                            unsigned long long offset,
	                            unsigned long long span, TypeId scalar);
	// 12.8p15: whether a subobject of this type is carried by a copy of its
	// bytes rather than by a call of a member of its own class.
	bool carried_as_storage(TypeId type, unsigned char kind);
	// 9p6: whether carrying a subobject of this type comes to nothing at all.
	bool carries_nothing(TypeId type, unsigned char kind);
	// 12.8p15: a move reads its source subobject as an xvalue, which is what
	// makes 13.3 choose the subobject's own move member.
	void transfer_source(Value& source, unsigned char kind);
	// 12.8p28: the definition a use of a value-transfer member the standard
	// gives a class asks this unit for, where a name rather than an object
	// initialization is what named it.
	void demand_transfer_definition(SemaEntity& function);
	// 12.1p5: whether the definition the standard would give this class's
	// default constructor is one it cannot write, which deletes the
	// constructor.
	bool undefinable_default(Scope& scope);
	// 12.6.2p2 and p6: whether a mem-initializer-id denotes the class `named` -
	// the base for p2's base subobject, the constructor's own class for p6's
	// delegation - rather than a non-static data member or some other class.
	bool names_the_class(const std::string& written, const SemaEntity& named,
	                     const Context& ctx);
	// 12.4p8: the destructor calls for the members of the class a destructor
	// belongs to, in reverse declaration order, written after its body.
	void write_member_destructions(Scope& members, DumpNode& line);
	// 3.8p1 and 12.4p3: the destructor call that ends the lifetime of the
	// object `entity` names, or nothing where its class has no destructor to
	// run.  `member` says the object is a member of the one being destroyed
	// rather than an object a declaration named.
	void destructor_action(SemaEntity& entity, DumpNode& parent,
	                       Placement where);
	// 12.1, 12.4 and the ABI: which of the two entry points a use of a
	// constructor or a destructor names, and the definition 12.4p6 gives an
	// implicitly declared one.  Every beginning and end of a lifetime asks it,
	// whether a statement writes the call or 15.2p2 leaves it to a cleanup.
	void note_construction_entry(SemaEntity& constructor, bool base);
	void note_destruction_entry(SemaEntity& destructor, bool base);
	// 15.2p2: records the destructor of a subobject a constructor step has just
	// built, for the question the caller asks once every step has been read.
	void record_unwind_subobject(TypeId type);
	// 9.3.2p1: the type of `this` in the body of the member function `function`.
	TypeId this_type(const SemaEntity& function);
	// 3.7.1: records the object a definition declares with whichever region
	// 3.8p1 makes the end of its lifetime an action of, which its storage
	// duration is what says.
	void record_lifetime(SemaEntity& entity, const Context& target,
	                     DumpNode& line);
	// 3.5, 3.7.1 and 3.7.2: the linkage a variable's name has and the storage
	// duration its object has, settled from this declaration's specifiers and
	// from the declaration of the same variable this region already holds.
	void record_storage(SemaEntity& entity, const SemaEntity* prior,
	                    const Specifiers& specifiers, const Context& target,
	                    TypeId type, const AstNode* whole = nullptr);
	// The objects an open block has declared whose destructors run when control
	// leaves it, innermost frame last.
	std::vector<std::vector<SemaEntity*> > lifetimes_;
	// 5.2.2p4: the parameters of class type of the function being read whose
	// end of lifetime the boundary makes the function's own, in declaration
	// order.  They stand outside `lifetimes_` because no block declared them
	// and because every block's objects end before they do.
	std::vector<SemaEntity*> parameter_objects_;
	// 3.8p1: writes the end of each of them, in the reverse of the order the
	// parameter-declaration-clause wrote them.  Reached where control leaves
	// the function: at a return, once the blocks it left have ended their own
	// objects, and where the body falls off its end.
	void end_parameter_lifetimes(DumpNode& line);
	// 5.2.2p4 and 12.4p5: which of a definition's parameters those are - one of
	// class type whose destructor is a call.  Read once per definition, off the
	// parameter lines the definition already wrote.
	void open_parameter_lifetimes(DumpNode& line);
	// 12.2p3: the temporaries each open full-expression has created, whose
	// lifetimes end where that full-expression does.  The frames nest for the
	// same reason 5.16's arms and 5.14's right operand need their own: an
	// operand that may not run is one whose temporary may not exist, so what it
	// created is ended where it ends rather than where the whole expression
	// does.
	std::vector<std::vector<SemaEntity*> > temporaries_;
	void open_full_expression();
	void close_full_expression(DumpNode& line);
	// The same two halves apart, for 5.14p1 and 5.16p1's operands that are
	// evaluated only where control reaches them: the frame is taken where the
	// operand ends, and what it holds is either ended there - which is what
	// makes a temporary an arm created one the arm destroys - or handed back to
	// the enclosing full-expression where the operator turned out to be a call
	// and every operand of it ran.
	std::vector<SemaEntity*> take_full_expression();
	// 12.2p3: a read that answers a question rather than writing one of the
	// program's initializations - 5.3.3p1 and 7.1.6.2p4's unevaluated operands,
	// and 8.5.1p11's probe of what a clause initializes - is written into a
	// node nothing keeps, so 12.2p1's temporaries it creates are held in a
	// frame of its own and dropped with that node.  Left in the enclosing
	// full-expression they are ends of lifetimes of objects the lowering was
	// never asked to give storage to.
	Value probe_expression(const AstNode& node, const Context& ctx,
	                       DumpNode& scratch);
	void end_temporaries(const std::vector<SemaEntity*>& frame, DumpNode& line);
	void keep_temporaries(const std::vector<SemaEntity*>& frame);
	// 5.16p1's arm, whose ends stand under a node of their own so the lowering
	// writes them where that arm's own block ends and nowhere else.
	void end_arm_temporaries(const std::vector<SemaEntity*>& frame,
	                         DumpNode& line, FactKind arm, const char* text);
	// 12.2p1: the object a prvalue of class type standing in storage of its own
	// is, made where the node that produced it is first asked for one.  Null
	// where the node is worth no such object.
	SemaEntity* prvalue_object(DumpNode& node);
	// 12.2p3: `node`'s object joins the temporaries the open full-expression
	// ends, or - where 12.2p5's reference binding extended it - the objects the
	// enclosing block ends.  Answers the object, or null where the node is
	// worth none.
	// `demanded` says this temporary is one whose end names the destructor of
	// its class wherever the program declared one at all - which every
	// temporary but 13.3.3.1.5p5's is, because a temporary a braced-init-list
	// was written into demands no definition of its own and so ends only in a
	// destructor 12.4p8 leaves something to run.
	SemaEntity* register_temporary(DumpNode& node, const Scope* from,
	                               bool extended = false,
	                               bool demanded = true);
	// 8.5.3p5 and 12.2p5: the temporary a reference initializer bound, which is
	// the prvalue under whatever conversion the binding wrote over it.
	DumpNode* bound_temporary(DumpNode& node);
	// 12.2p5: a reference declared by `entity` binds a temporary the
	// initializer under `line` created, so that temporary is destroyed where
	// the reference goes out of scope rather than where its full-expression
	// ends.
	void extend_bound_temporary(TypeId declared, const Context& ctx,
	                            DumpNode& line);
	// 12.4p3: the end of a temporary's lifetime, written under `parent`.
	// `full_expression` says it is 12.2p3's end - the one 15.2p2's handler
	// stands around - rather than 3.8p1's end of an object a block declared,
	// which is what a temporary 12.2p5 moved into a block gets.
	void temporary_destruction(SemaEntity& object, DumpNode& parent,
	                           bool full_expression);
	// 5p11 and 12.2p1: the object a statement whose value nothing reads still
	// created, which the full-expression it stands in ends.  5.2.9p4's cast to
	// void is what a program writes to throw one away, so the object is looked
	// for under it as well as at the statement's own operand.
	void register_discarded_object(const Value& value, DumpNode& line,
	                               const Context& ctx);
	// 15.2p2: the destructors of the subobjects the constructor being read
	// builds, in the order its steps build them.  A step with a step after it
	// leaves its subobject standing wherever that later one throws, so all but
	// the last of these is odr-used - which is a question about the whole list
	// and is asked once it is complete.
	std::vector<SemaEntity*> unwind_subobjects_;
	// Holds one frame while a block is read, and writes its destructor actions
	// where the block ends.
	void open_lifetimes();
	void close_lifetimes(DumpNode& line);
	// 6.6p2 and 3.8p1: a jump leaves every block it jumps out of, so it runs
	// the destructors of the objects all of them declared, innermost first.
	// `depth` is the first frame the jump does not leave: 0 for a return, which
	// leaves the whole function, and the frame the statement itself opened for
	// a break or a continue.
	void leave_lifetimes(std::size_t depth, DumpNode& line);
	// 6.6.3p2: a return leaves every block between it and the function.
	void unwind_lifetimes(DumpNode& line);
	// 3.8p1: whether any block still open holds an object whose lifetime ends
	// with a call, which is what a jump this milestone cannot place those calls
	// for has to be refused for.
	bool lifetimes_pending() const;
	// 12.4p3: whether the end of this object's lifetime is a call.
	bool ends_in_call(const SemaEntity& entity);
	// 12.4p8 and 3.8p1: whether the end of the lifetime of an object of this
	// type comes to nothing at all.  12.4p5's trivial destructor is one; so is a
	// destructor the program wrote whose definition writes no statement and
	// whose class holds no subobject whose own destruction does something.  The
	// two differ in which clause said so and not in what running them comes to,
	// and the output writes an action only where there is one to write.
	bool vacuous_destruction(TypeId type);
	// 12.4p3: whether the program declared a destructor anywhere below this
	// type's class, which is the question every end of a lifetime the
	// *translation* wrote asks - a temporary, the object a reference extended,
	// and the object a delete-expression ends.
	bool declared_destruction(TypeId type);
	// 3.4.1p8 and 9.3p2: the out-of-class definitions the unit's syntax holds,
	// collected before any of it is read, and the one of them that defines this
	// class's destructor - which is what settles 12.4p8's question wherever the
	// definition was written.
	void collect_unit_definitions(const AstNode& node);
	void note_definition_body(SemaEntity& member, const SemaEntity& owner);
	// 12.4p8: whether the definition `node` gives a function writes any
	// statement, which is the one reading of a body both of those ask.
	static bool writes_no_statement(const AstNode& node);
	// 12.1p5: whether default-initializing an object of `type` does nothing at
	// all, so that a subobject of it needs no action written.
	bool trivially_constructed(TypeId type);
	// 12.6.2p6: the constructors of this unit whose ctor-initializer delegates,
	// in the order their definitions were read.  It is the vertex set of the
	// graph p6's cycle would be in - every other constructor is a chain's end -
	// so the whole unit is checked in one walk of it.
	std::vector<SemaEntity*> delegations_;
	// 3.6.3p1: the namespace-scope objects this unit constructed, whose
	// destructors run in reverse order when the program ends.
	std::vector<SemaEntity*> static_lifetimes_;
	// 3.7.2p2: the namespace-scope objects with thread storage duration this
	// unit constructed, and the line each was declared on.
	std::vector<DeclaredLifetime> declared_lifetimes_;

	// 8.5.1: `type` initialized from what is left of `clauses`, writing one
	// `subobject-initialization` per member or element under `parent`.  A
	// subobject no clause reaches is value-initialized, which 8.5p7 makes the
	// zero of its type and which the node with no clause under it says.
	void aggregate_subobject(TypeId type, Clauses& clauses, const Context& ctx,
	                         DumpNode& node);
	void aggregate_members(TypeId type, Clauses& clauses, const Context& ctx,
	                       DumpNode& parent);
	// 8.5.1p2 over an array whose elements are initialized where they stand:
	// one child per element under `line`, taken from the cursor until the
	// array's bound is reached - which is what 8.5.1p11's array member takes
	// out of the enclosing list.
	unsigned long long array_from_clauses(TypeId array, Clauses& clauses,
	                                      const Context& ctx, DumpNode& line,
	                                      bool image);
	// 8.5.1p11: one element of that array whose own braces were left out, so
	// that it takes a run of the enclosing list rather than one clause.
	void elided_element(TypeId element, Clauses& clauses, const Context& ctx,
	                    DumpNode& line, bool image);
	// 8.3.4p3: how many elements that walk fills, which is what an array
	// declared with no bound has.
	unsigned long long deduced_array_bound(TypeId array, const AstNode& list,
	                                       const Context& ctx);
	// 8.3.5p5: the same list read as the argument the parameter carrying an
	// array member is passed, which is an array object of the caller's own.
	void array_argument(TypeId array, Clauses& clauses, const Context& ctx,
	                    DumpNode& call);
	void aggregate_elements(TypeId array, Clauses& clauses, const Context& ctx,
	                        DumpNode& parent);
	// 8.5.1p2 and 8.5.1p7: a subobject of class type is copy-initialized from
	// the clause that reached it, and value-initialized where none did.  Either
	// is one object built where it stands, so what the node carries is the
	// `constructor-action` a declaration of the same object would carry - or,
	// where 12.8p31 elides it, the value it holds a copy of.
	// `elements` past zero makes the one action that many consecutive elements
	// of the array it stands in, which 8.5.1p7's tail is.
	void construct_subobject(TypeId type, const AstNode* written,
	                         const Context& ctx, DumpNode& node,
	                         bool value_init, unsigned long long elements = 0);
	// 12.8p31 and 5.2.3p3: the braced-init-list of an initializer written
	// `T{...}` for an object that is itself of `T`.  The prvalue and the object
	// it initializes are one, so what initializes the object is that list - and
	// then 8.5.1 gives its clauses to the members of an aggregate exactly as a
	// list written on the declarator would.  Null for anything else.
	const AstNode* braced_prvalue_of(const AstNode& written, TypeId type,
	                                 const Context& ctx);
	// 8.5.1p2 and 13.3.1.7: the constructor an object of the aggregate class
	// `type` is built by where it is an object of its own, declared once and
	// held on the class.  Null where the class is no aggregate, and where a
	// member of it is no object a by-value parameter can hold - which leaves
	// the initialization to the clauses themselves.
	SemaEntity* member_constructor(TypeId type);
	// 8.5.1p2: one call of that constructor, whose arguments are what the
	// clauses of `list` initialize the members with.
	void construct_from_members(SemaEntity& constructor, const AstNode& list,
	                            const Context& ctx, DumpNode& parent);
	// The same call, taking what it needs out of a walk the caller still holds,
	// which is what 8.5.1p11's elided braces make of a nested aggregate.
	void construct_from_clauses(SemaEntity& constructor, Clauses& clauses,
	                            const Context& ctx, DumpNode& parent);
	// 8.5.1p11: whether the member's own braces were left out, so that the
	// clauses standing next initialize its members rather than it.
	bool elides_its_braces(TypeId type, Clauses& clauses, const Context& ctx);
	// 8.5.1p11: that subaggregate, built where the parameter carrying it
	// stands, out of the clauses its own subobjects take.
	void elided_subaggregate(TypeId type, Clauses& clauses, const Context& ctx,
	                         DumpNode& call);
	// 8.5.1p11: whether the clause initializes the whole subaggregate rather
	// than the first of its members, which is what says the braces around it
	// were left out.  Only a value of the subaggregate's own class type - or of
	// one derived from it - initializes it on its own.
	bool clause_initializes_class(TypeId type, const AstNode& clause,
	                              const Context& ctx);
	// The value a braced-init-list standing where an expression does comes to.
	Value value_of_list(DumpNode& line, TypeId wanted, TypeId target);
	// 8.5.1p2: the clauses of a nested braced-init-list, which initialize this
	// subobject alone and must all be taken by it.
	void aggregate_from_list(TypeId type, const AstNode& list,
	                         const Context& ctx, DumpNode& node);
	// The `subobject-initialization` node of one member or one element.
	DumpNode& open_subobject(DumpNode& parent, TypeId type,
	                         const SemaEntity* member,
	                         unsigned long long index);
	// 11p6: the class a declaration written outside it names a member of, whose
	// access every name of that declaration is checked with - the leading return
	// type of an out-of-class member function and the initializer of a static
	// data member alike.  Null for a declaration that names no member.
	Scope* naming_context(const std::string& written, const Context& ctx);
	friend struct Naming;
	friend class Access;
	friend struct Written;
	// 5.2.5p1: the error a member access which turns out to name no subobject
	// gives where `observable_expression` says the object expression may not be
	// left out of the resolved tree.
	void require_droppable(const DumpNode& object, const std::string& member);
	// 12.6p1: whether a declaration of an array of class type creates its
	// elements by constructing each of them, which every form but one that
	// wrote a clause for an element does.
	bool element_constructed(TypeId type, const AstNode* written);
	// 8.5 and 13.3.1.3: the `constructor-action` an object of class type is
	// initialized by, and the definition of the constructor it calls.  What
	// each of its optional parameters says is written where it is read, at the
	// definition in `sema_lifetime.cpp`.
	void construct_object(SemaEntity& variable, DumpNode& line,
	                      const AstNode* written, const Context& ctx,
	                      Placement where = Placement::Named,
	                      bool copied = false, const Value* given = nullptr,
	                      bool value_init = false,
	                      const std::vector<SemaEntity*>* forwarded = nullptr,
	                      bool direct = false, bool into_temporary = false,
	                      bool boundary_object = false,
	                      SemaEntity** chosen = nullptr);
	// 8.5p15/p16 and 5.2.3p1: which of 8.5's forms the initializer an object of
	// class type was written with is - a list whose clauses are the
	// constructor's arguments, one expression a converting constructor answers,
	// or the `T(a, b)` and `T{...}` whose arguments are the object's own
	// because 12.8p31 leaves no prvalue standing between them.
	WrittenInitializer read_initializer(const AstNode* written,
	                                    TypeId object_type, const Context& ctx,
	                                    bool value_init);
	// 12.8p31 and 12.2p3: the prvalue's object and the object being
	// initialized are one, so the full-expression that was holding the end of
	// the first holds it no longer.  Reached through 5.2.9p4's cast, because
	// the object stands under it and not on it.
	void elide_created_object(DumpNode& node);
	// 5.2.9p4: the node the object a prvalue creates stands on, which is the
	// one under whatever cast to the same class was written over it.
	DumpNode& created_object_node(DumpNode& node);
	// 12.8p31: whether the transfer 13.3 chose for this initialization is one
	// whose argument creates the very object being initialized, which makes the
	// two objects one and leaves the transfer unwritten.  The initializer the
	// line then holds is the one that creates it.  `into_temporary` is what the
	// elision asks about the *destination*: a temporary the analysis made for a
	// prvalue is storage the enclosing initialization has not settled yet, so
	// the transfer into one is a call the program can watch run.
	bool elide_transfer(const SemaEntity& constructor,
	                    std::vector<Value>& arguments, TypeId object_type,
	                    DumpNode& line, DumpNode& action, bool into_temporary);
	// The three lines an initialization of an object of class type leaves once
	// 13.3 has chosen: the `constructor-action` that says the lifetime begins
	// here, the call that runs the constructor and the callee that names it.
	void write_constructor_action(DumpNode& action, DumpNode& call,
	                              DumpNode& callee, SemaEntity& constructor,
	                              const SemaEntity& head, bool value_init);
	// 5.1.1p1: a parameter named as the program would name it, written into a
	// node of its own under `parent`.
	Value parameter_value(SemaEntity& parameter, DumpNode& parent);
	// 12.1p5, 12.9p6 and 3.2p3: the definition a use of a constructor the
	// standard rather than the program gives a class asks this unit for, which
	// is written once however many uses ask.
	void demand_constructor_definition(SemaEntity& constructor);
	// The object a constructor-action runs on, as the address of it: an object
	// a declaration named, a member of the object being constructed, or that
	// object's base class subobject.
	void write_constructed_object(SemaEntity& variable, DumpNode& call,
	                              Placement where, Value& object,
	                              TypeId object_type);
	// 12.2p1 and 5.2.3p2: a prvalue of class type is an object, so the function
	// it is written in has to hold one.  This declares that object - which no
	// name reaches - runs the constructor `written` chooses on it, and hands
	// back the prvalue as the object it now stands for.  `prefix` names the
	// storage it is given, which is what says whether an argument asked for it.
	Value materialize_temporary(TypeId type, const AstNode* written,
	                            const Context& ctx, DumpNode& parent,
	                            const char* prefix, bool value_init = false);
	// The same, written into a line that already stands where the prvalue does,
	// which is what an argument conversion needs: the argument keeps the place
	// it had among the operands of the call and the temporary is written around
	// it rather than beside it.
	// `owned` says 12.2p3's end of the lifetime is the full-expression's to
	// write.  It is not for the two objects a temporary is written for that
	// something else already ends: 5.2.2p4's by-value parameter object, which
	// the function called is what destroys, and 5.16p3's result object, which
	// each arm builds and whoever reads the conditional holds.
	// `direct` is 13.3.1.4's question about the place that asked for the
	// temporary, passed through to the initialization.
	// `copy_list` says the place that asked wrote `= {...}` or passed the list
	// as an argument, which 8.5.4p3 refuses an `explicit` constructor for.
	// `boundary` says the object is one 5.2.2p4's or 6.6.3p2's boundary made to
	// carry a value across it rather than one the program's own expression
	// asked for, which is what says whether 12.8p12's bytes are the transfer
	// into it or the member 13.3 chose is a call.
	Value build_temporary(TypeId type, DumpNode& line, const AstNode* written,
	                      const Value* given, const Context& ctx,
	                      const char* prefix, bool value_init,
	                      bool owned = true, bool direct = false,
	                      bool copy_list = false, bool boundary = false);
	// 8.5.3p5 and 13.3.3.1.2: a temporary an argument conversion made is named
	// after the argument it was made for, unless something already read it as
	// the object it is.  `owned` says the same thing it says above: an argument
	// that becomes the parameter object is one the call rather than the
	// full-expression ends the lifetime of.
	void name_argument_temporary(const Value& value, const char* prefix,
	                             const Context& ctx, bool owned);
	// 12.2p3: takes `value`'s object back out of the open full-expression,
	// where the place that read the prvalue is what ends its lifetime.
	void release_temporary(const Value& value);
	void release_temporary(const Value& value,
	                       std::vector<SemaEntity*>& frame);
	// The typed facts of a node the analysis builds rather than reads out of an
	// expression the program wrote.
	static void set_fact(DumpNode& node, FactKind kind, TypeId type,
	                     ValueCategory category);
	// 9.3.1p3: the type of a member function, which is called on an object that
	// its declarator does not write and that the dump writes as its first
	// parameter.  Any other function is its own type.
	// 9.4.1p2: `static` stands on the declaration written in the class and is
	// not repeated on a definition written outside it, so a qualified
	// declarator is told which kind of member it declares by the declaration it
	// redeclares.
	TypeId with_object_parameter(TypeId type, const AstNode& declarator,
	                             const Context& target, bool is_static,
	                             const std::string& name, bool qualified);
	// 15.4p1: whether the exception-specification written after the
	// parameter-clause says the function throws nothing.  C++11 leaves it out
	// of the function type, so it is read off the declarator once - condition
	// and all - and held on the declaration 5.3.4p15 asks it of.
	bool declarator_nonthrowing(const AstNode& declarator, const Context& ctx);
	// 15.4p14: whether that declarator wrote an exception-specification at all,
	// which is what says the function keeps what it wrote rather than taking
	// the one an implicit declaration of it would have had.
	static bool declarator_writes_exception_specification(
		const AstNode& declarator);
	// 9.4p1: whether `where` declares `name` as a static member function whose
	// declarator wrote `type`.  `head`, where this declaration is a template's,
	// is the region its own head declared its parameters in - 14.5.6.1p5's
	// question rather than 13.1's index answers it there.
	bool declares_static_member(Scope& where, const std::string& name,
	                            TypeId type, Scope* head);
	// 3.7.4p2 and 12.5p1: whether the name is one of the allocation and
	// deallocation functions, which a class declares as a static member of
	// itself whether or not `static` was written.
	static bool allocation_function_name(const std::string& name);
	// 3.7.4p2: the same name with the space an id-expression written
	// `operator new` or `operator delete` carried taken out of it, which is the
	// spelling the declaration of one is bound under.  Every other name stands.
	static std::string allocation_function_spelling(const std::string& written);
	// 12.8p1: whether the program wrote a copy constructor of the class
	// `entity` whose members `scope` declares, which is what says a copy of an
	// object of it is not the copy of its bytes.
	bool declares_copy_constructor(const SemaEntity& entity, Scope& scope);
	// 12.8p2: the class one member is copied as, which for an array member is
	// its element type.
	TypeId member_copy_type(TypeId type);
	// The definitions the output writes at the end of the translation unit,
	// which are read in the order they were asked for.  Reading one may ask for
	// another, so the list is walked rather than iterated.
	void write_pending_definitions();
	void write_definition(Pending& pending);
	// 8.4p1: the body of a definition read where it stands, under the node the
	// declaration was read under - which is every definition but the two the
	// end of the unit writes, 9.2p2's member defined in its class and the one
	// 14.7.1p1 left to the use that names it.
	void read_definition_body(Pending& pending, DumpNode& into, TypeId type);
	// 9.5p1: whether the members of this class stand in one storage, so that at
	// most one of them holds an object at a time.  It is one question with one
	// owner because every walk of a class's members reads it and each reads it
	// for a different rule: 8.5.1p15's aggregate takes the first member alone,
	// 12.6.2p8's constructor initializes the one a mem-initializer designated,
	// 12.4p8's destructor destroys none of them, and 12.8p15/p28's transfer
	// carries the object representation rather than a member at a time.  False
	// for anything that is not a class.
	bool one_storage(TypeId type);
	// 9.2p2 and the course ABI: the size and alignment of a completed class,
	// with `requested` the alignment 7.6.2 asked for, or zero for none, and
	// `packed` the one 16.6 caps every subobject at, or zero for none.
	void lay_out_class(SemaEntity& entity, Scope& scope, bool is_union,
	                   unsigned long long requested, unsigned long long packed);
	// The ABI: where the empty class subobjects of an object of `type` standing
	// at `at` are, appended to `holes`, and whether putting one there would put
	// a subobject of some class where a subobject of that class already stands.
	// The ABI gives an empty base subobject offset zero and then forbids a
	// second subobject of its class from standing there, and an empty subobject
	// takes no storage to push the next one along - so the offsets alone do not
	// say it and the classes standing at them have to be carried.
	void place_empty_subobjects(
		TypeId type, unsigned long long at,
		std::vector<std::pair<TypeId, unsigned long long> >& holes);
	bool collides_with_empty(
		TypeId type, unsigned long long at,
		const std::vector<std::pair<TypeId, unsigned long long> >& holes);
	// 16.6: the packing alignment in force where `node` was written.
	unsigned long long packing_of(const AstNode& node) const;
	// 2.2p1: whether `node` was written in this unit's own source file rather
	// than in a file it included.
	bool own_source(const AstNode& node) const;
	// 9.6p2: the storage unit the bit-fields of a class are being allocated
	// into while it is laid out.  A unit is opened by the first field that
	// cannot share the one before it, and the fields declared with its own type
	// share it while their bits fit; anything else closes it.
	// 9.6p2: the share of a storage unit one bit-field takes, from the byte the
	// layout has reached and the unit it is filling.  `packed` is 16.6's cap on
	// where the unit may begin.
	void lay_out_bit_field(SemaEntity& member, unsigned long long& at,
	                       BitUnit& unit, unsigned long long packed);
	// 9.6p1: the members one member-declaration that wrote widths declares.
	void bit_field_declaration(const AstNode& node, const Context& ctx);
	// 9.6p2: the type the storage unit of a bit-field of this type is read and
	// written at, which is the signed integer of the unit's own width.
	TypeId bit_field_access_type(TypeId declared);
	// 7.6.2p1: the strictest alignment the class-head's alignment-specifiers
	// asked for.
	unsigned long long requested_alignment(const AstNode& node,
	                                       const Context& ctx);
	// 13.1 and 3.5: the declaration a function declarator makes, which is a
	// redeclaration of an earlier one in the same region exactly when their
	// parameter type lists agree.
	// 3.1p2 and 8.5: the object an init-declarator that is not a function
	// declares, once its type and the region it belongs to are settled.
	void declare_object_declarator(const AstNode* initializer,
	                               const Specifiers& specifiers,
	                               const Context& ctx, const Context& target,
	                               const QualifiedName& spelled,
	                               const std::string& written, TypeId type,
	                               const AstNode* whole = nullptr);
	// 8.5: what the dump says that object's initialization is, which is a
	// reading of its own because 9.4.2p3 leaves the declaration that wrote the
	// initializer and the one that defines the object two declarations.
	void describe_object_initialization(SemaEntity& entity, DumpNode& line,
	                                    TypeId type, const AstNode* initializer,
	                                    const Specifiers& specifiers,
	                                    const Context& ctx, const Context& target,
	                                    const SemaEntity* declared,
	                                    bool defines_object);
	// `redeclaration` is 9.3p2 and 3.4.3.2p1: a definition whose declarator-id
	// is qualified defines a declaration the region that name reaches has
	// already made, and declares nothing of its own.
	SemaEntity& declare_function(const std::string& name, TypeId type,
	                             const Context& target, bool define,
	                             bool hidden = false,
	                             bool object_member = false,
	                             bool redeclaration = false,
	                             SemaEntity* as = nullptr,
	                             bool* redeclares = nullptr);
	// 15.4p1: if any declaration of a function has an exception-specification,
	// every one of them - the definition included - shall have one, and they
	// shall say the same thing.  Asked of a declaration that redeclares one the
	// region already made, which is the only place two of them meet.
	void require_matching_exception_specification(const SemaEntity& declared,
	                                              bool wrote, bool nothrowing,
	                                              const std::string& name);
	// 15.4p1 with p3: what this declarator said about what the function throws,
	// put on the declaration and compared with what the ones before it said.
	// `compared` is false where 14.7.3p1's explicit specialization is what the
	// declarator declares, which redeclares nothing.
	void record_exception_specification(SemaEntity& entity,
	                                    const AstNode& declarator,
	                                    const Context& target,
	                                    const std::string& name, bool compared);
	// 13.1 and 9.3.1p3: the key the chain a name heads is indexed by, which is
	// the parameter-type-list the declarator wrote wherever the region is a
	// class - so a static and a non-static member function whose types agree
	// only because one carries the object parameter are two declarations.  A
	// declaration written under a head wrote its list in places that head
	// declared, so `written_under` is what 14.5.6.1p5 keys it by instead.
	std::uint32_t declaration_signature(const Scope& where, TypeId type,
	                                    bool object_member,
	                                    const Scope* written_under = nullptr);
	// 7.1.1p10: `mutable` says the const of the object holding a member stops
	// at it, which is a fact about a non-static data member and about nothing
	// else the declaration could declare.
	void require_mutable_data_member(const Specifiers& specifiers,
	                                 const Context& target,
	                                 const std::string& name, TypeId type);
	// 7.3.3p14: a member function this class declares hides the one a
	// using-declaration brought in from a base with the same name and
	// parameter-type-list rather than conflicting with it, so what was brought
	// in leaves the chain the name heads.  It is asked where 9.2p2 completes
	// the class, because it is a question about what the complete class
	// declares and not about which of the two the body wrote first - one pass
	// over the declarations each brought-in name has, and nothing at all for a
	// class that wrote no using-declaration.
	void hide_using_members(Scope& where);
	// 11.3p6 and 7.3.1.2p3: the declaration a friend declaration made visible,
	// which a later namespace-scope declaration of the same function is.  The
	// entity moves from the region's hidden chain into the one its name binds,
	// so the program has one function rather than two.
	void reveal_friend(Scope& where, const std::string& name,
	                   SemaEntity& entity);
	// 3.3.6 and 7.3.1.2p3: the innermost namespace a region is written in,
	// which is what a friend declaration declares into however deeply the class
	// it is written in is nested.
	static Scope& friend_namespace(Scope& scope);
	// The `parameter` lines of a function, and the declarations of them in the
	// region the definition opened.  `written` is how many parameters of the
	// function type the declarator did not write, which is the implicit object
	// parameter of a member function.
	// 6.6.4p1: every label a goto names is one the function writes, asked
	// where the body that wrote both of the lists ends.
	void require_labelled_gotos();
	void declare_parameters(const std::vector<Parameter>& parameters,
	                        TypeId type, const Context& inner, DumpNode* node,
	                        std::size_t implicit = 0);
	// The entity the declaration of a name reuses, or nothing when the name is
	// new to the region.
	SemaEntity* redeclared(const Context& ctx, const std::string& name,
	                       SemaKind kind);
	// A dump node that also says which construct it stands for, for the
	// statement and declaration lines whose kind is fixed where they are
	// written rather than carried up in a `Value`.
	DumpNode& open_fact(DumpNode& parent, const std::string& text,
	                    FactKind kind);
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
	// `member_object` says the decl-specifier-seq beside this declarator leaves
	// it declaring a non-static member function, which is 5.1.1p3's whole
	// question: `static`, 11.3p1's `friend` and 7.1.3's `typedef` each write a
	// declarator in a class and declare no member function of it, and no part of
	// the declarator says which of them was written.
	TypeId declarator_type(const AstNode& node, TypeId base, const Context& ctx,
	                       std::string* name,
	                       std::vector<Parameter>* declared = nullptr,
	                       bool member_object = false);
	TypeId apply_pointer(const AstNode& node, TypeId type,
	                     const Context& ctx);
	TypeId apply_suffix(const AstNode& node, TypeId type, const Context& ctx,
	                    std::vector<Parameter>* declared);
	std::vector<TypeId> parameter_types(const std::vector<Parameter>& parameters);
	// 5.1.1p3: the object `this` names while a member function's declarator is
	// read, whose class is the one the declarator-id reaches and whose
	// qualifiers are the ones written among the suffixes from `suffixes` on.
	SemaEntity* declarator_object(const AstNode& node, std::size_t suffixes,
	                              const Context& ctx);
	// 8.2p7: whether the parentheses a parameter's declarator wrote stand
	// around its declarator-id rather than around a parameter-clause of its
	// own.  `name` takes the declarator-id and `rest` what was written after
	// it, which is null where the parentheses held nothing else.
	bool parenthesized_place(const AstNode& declarator, const Context& ctx,
	                         std::string& name, const AstNode*& rest);
	// 3.3.7p1: `reading`, where given, takes the region the rest of the
	// declarator reads its names against, which is `ctx` where the clause bound
	// none.
	void read_parameters(const AstNode& clause, const Context& ctx,
	                     std::vector<Parameter>& out, bool& variadic,
	                     Context* reading);
	void bind_place(Context& reading, const Context& ctx,
	                const Parameter& parameter);
	void bind_pack(Context& reading, const Context& ctx,
	               const Parameter& parameter);
	// The declarator-id of a declarator, which a nested declarator holds.
	static const AstNode* declarator_id(const AstNode& node);
	// 3.4.3: `name`, which may be written with a nested-name-specifier,
	// resolved from `ctx`.  Null when nothing of the kind is declared.  `found`,
	// when given, takes the declarations the lookup associated with the name,
	// which 3.4p2 lets be more than one where they are all functions.
	// 11.2p5: `naming_region` takes the region the prefix reached.
	SemaEntity* resolve(const std::string& name, const Context& ctx,
	                    LookupKind filter,
	                    std::vector<SemaEntity*>* found = nullptr,
	                    Scope** naming_region = nullptr);
	// The region the nested-name-specifier of `name` reaches, for a declaration
	// that names an entity of another region.  14.6.2p1: `dependent`, when
	// given, takes the type of the component that names a region only an
	// instantiation opens, and the prefix answers null rather than refusing it;
	// `unresolved` then takes the place of the first component standing after
	// it, which is where the chain of member names begins.
	Scope* resolve_prefix(const QualifiedName& name, const Context& ctx,
	                      Scope* first_region = nullptr,
	                      TypeId* dependent = nullptr,
	                      std::size_t* unresolved = nullptr);
	// 14.6.2p1: the type one component written after a dependent prefix stands
	// for while the template writing it is read - one per prefix and component,
	// so one definition writes one type for one name.
	// 14.6.2p1 at a substitution: the type such a name stands for once the
	// prefix is a class an argument list has named, and the name itself where
	// it is not one yet.
	TypeId dependent_member_type(TypeId owner, const std::string& member,
	                             unsigned cv, TypeId written);
	SemaEntity& dependent_member_name(TypeId prefix,
	                                  const std::string& component);
	// 14.6.2.1p6: the same stand-in for a name looked up *in* a class whose
	// definition this reading has and whose base-clause an argument list has
	// still to settle - null where the region is no such class.
	SemaEntity* member_of_unknown_specialization(const Scope& region,
	                                             const std::string& component);
	// 7.1.6.2p1: the declaration a name whose nested-name-specifier begins with
	// a decltype-specifier reaches.  The expression the parser kept beside the
	// spelling is what says which region the rest of the name is looked up in,
	// which is the one thing no spelling can hold - so the type a declaration
	// writes and the id-expression a body writes ask it the same way.
	SemaEntity* decltype_qualified_name(const AstNode& node, const Context& ctx,
	                                    LookupKind filter,
	                                    std::vector<SemaEntity*>* found = nullptr);
	TypeId decltype_qualified_type(const AstNode& node, const Context& ctx);
	// 3.4.3 over a prefix that named a type rather than a region a spelling
	// reaches, which the two readings of a decltype-specifier share.
	SemaEntity* qualified_in_type(TypeId head, const QualifiedName& written,
	                              const Context& ctx, LookupKind filter,
	                              std::vector<SemaEntity*>* found);
	SemaEntity& require(SemaEntity* entity, const std::string& name);

	// Constant expressions and decltype (sema_constant.cpp).
	Constant evaluate(const AstNode& node, const Context& ctx);
	Constant literal_constant(const std::string& spelling);
	// 5.19p2: the element `index` of the string literal `spelling`, which is
	// the one object a constant expression reads out of storage.
	Constant string_element(const std::string& spelling,
	                        unsigned long long index);
	Constant convert(const Constant& value, TypeId type);
	Constant promote(const Constant& value);
	// 5p10: the type the usual arithmetic conversions bring two operands to.
	TypeId common_type(TypeId left, TypeId right);
	unsigned long long array_bound(const AstNode& node, const Context& ctx);
	TypeId decltype_type(const AstNode& node, const Context& ctx);
	// 5.19 read out of the spelling 14.2 left a template argument as, which
	// `sema_value_expression.cpp` owns: a walk over text rather than over the
	// tree, so it is a reader of its own that borrows this one.
	friend class TemplateArgumentReader;
	// 14.3.2p1: the argument `spelling` binds to a value place whose declared
	// type is `place`, and 14.6.2p2's stand-in for one no list has settled.
	TypeId template_argument_value(const std::string& spelling, TypeId place,
	                               const Context& ctx);
	TypeId dependent_value(const std::string& spelling);

	// 5.3.3 and 5.3.6 over a type-id, which is the whole of what PA11 needs: one
	// answer apiece, because neither p3's demand nor 14.6p8's stand-in is
	// `TypeTable`'s to make and three readings write each operator.
	unsigned long long size_of(TypeId type);
	unsigned long long align_of(TypeId type);
	bool is_signed(TypeId type) const;
	unsigned width_of(TypeId type) const;
	// 3.9.1p8: the arithmetic type a value of `type` is read as - an
	// enumeration's underlying type, a fundamental one itself - and the
	// narrower question 14.1p4 and 4.5 ask, which no floating type answers.
	// A value as the dump writes it; `real` is what a floating type is worth.
	TypeId arithmetic_type(TypeId type) const;
	TypeId integral_type(TypeId type) const;
	std::string spell_value(TypeId type, unsigned long long bits,
	                        long double real = 0) const;

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
	// 6.4p4 and 4p3: the value of a condition that declared an object of class
	// type, which is that object converted by a conversion function of its
	// class.  What it hands back is the type of the value the statement
	// branches on, which is `kNoType` where the declaration declared nothing.
	TypeId condition_of_declaration(DumpNode& declaration, const Context& ctx,
	                                bool integral);
	// 6.4p4 and 6.4.2p2: the type a condition has to be worth for the statement
	// it stands in - bool for a selection or iteration statement, an integral or
	// enumeration type for a switch.  One reading for both spellings of a
	// condition, because a class object no conversion function answered is
	// nothing either of them can branch on.
	void require_condition_type(TypeId type, bool integral) const;
	// The region a substatement that is not a compound-statement opens (6.4p1).
	Context substatement_scope(const Context& ctx);

	// Expressions (sema_expression.cpp).
	Value expression(const AstNode& node, const Context& ctx, DumpNode& parent);
	// The one form `node` is, read by the layer that reads that form.  It is
	// split from `expression` so that the depth guard and the recording of
	// what a line stands for are each written once.
	Value dispatch_expression(const AstNode& node, const Context& ctx,
	                          DumpNode& parent);
	Value id_expression(const AstNode& node, const Context& ctx,
	                    DumpNode& parent, bool addressed = false);
	// What an id-expression already resolved to denotes, which is where a
	// caller that had to look the name up to know what it was written for -
	// a call, which 5.2.3 lets name a type - spends the one lookup it takes.
	// `found` is the set that lookup found, which for a function name is what
	// 13.3 or 13.4 chooses from.
	// `addressed` is 5.3.1p3 asked before the name is read: whether what stands
	// here is the *object* the name denotes rather than what it is worth.  The
	// two part company at a constant a class declared: 3.2p2 makes reading its
	// value no odr-use, so a read is the value and `&` is the storage.
	// `gathering` is 3.4.2p3: the caller has candidates of its own still to
	// add, so what the lookup found is not yet the whole set and the name
	// denotes no one declaration however few declarations that set holds.
	Value named_value(const AstNode& node, SemaEntity& entity,
	                  const Context& ctx, DumpNode& parent,
	                  const std::vector<SemaEntity*>* found = nullptr,
	                  bool addressed = false, bool gathering = false);
	Value literal_expression(const AstNode& node, const Context& ctx,
	                         DumpNode& parent);
	// 2.14: the one line an analysed literal token is worth, which a literal
	// the program wrote and one 2.14.8 hands a literal operator share.
	Value literal_value(const PostToken& token, const std::string& payload,
	                    DumpNode& parent);
	// 2.14.8p2: the call of the literal operator a user-defined-literal's
	// ud-suffix names, with the arguments p3 to p6 give it.
	Value user_defined_literal(const PostToken& token,
	                           const std::string& spelling, const Context& ctx,
	                           DumpNode& parent);
	// 2.14.8p3 to p6: the declaration among `candidates` whose
	// parameter-type-list is `wanted`, which is what those paragraphs name
	// instead of a candidate set 13.3 ranks.  Null where none is.
	SemaEntity* literal_operator(const std::vector<SemaEntity*>& candidates,
	                             const std::vector<TypeId>& wanted);
	// 5.2.5: `E1.E2` and `E1->E2`, which name a member of the class the object
	// expression has.
	Value member_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 5.2.5p2: the region a member written after `.` or `->` is looked up in,
	// with `object` left denoting the object rather than a pointer to it.
	Scope& object_region(const AstNode& node, Value& object);
	// 5.2.5 and 13.3.1.1.1: a call whose callee names a member.  `line` is the
	// call's own node; the object the member is named on is written as the
	// call's first argument, because 9.3.1p3 already made the object parameter
	// the first parameter of a non-static member function's type.  A member that
	// is not a function leaves `object` denoting nothing and `target` denoting
	// whatever the member holds, which the call then reads as an ordinary one.
	void member_callee(const AstNode& callee, const Context& ctx, DumpNode& line,
	                   Value& target, Value& object);
	// 5.2.4: a call whose callee is `E1.~T` or `E1->~T` for a `T` that is not a
	// class.  True when the callee is that, with `out` the void prvalue 5.2.4p1
	// says the call is; false for every other member callee, including the
	// destructor of a class, which is an ordinary member function.
	bool pseudo_destructor_call(const AstNode& node, const AstNode& callee,
	                            const Context& ctx, DumpNode& parent, Value& out);
	// 5.2.5p1: what a member name written after `.` or `->` denotes in the
	// class of the object expression, with `found` taking what the lookup
	// associated with it.  Null where the class declares nothing of the name.
	SemaEntity* member_named(Scope& region, const std::string& id,
	                         const Context& ctx,
	                         std::vector<SemaEntity*>& found);
	// 12.4p12 and 5.2.4p2: the destructor of the class `region` declares, where
	// `id` is `~` and a type-name that names that class - which need not be the
	// name 12.4p1 bound the destructor under.  Null for every other name.
	SemaEntity* destructor_named(Scope& region, const std::string& id,
	                             const Context& ctx);
	// 9.3.2p1: the `this` a member function named with no object expression is
	// called on, written as the call's first argument.  `object` denotes nothing
	// outside a member function, where 13.3.1p4 leaves a non-static member no
	// candidate at all.
	void implicit_object_argument(const std::vector<SemaEntity*>& candidates,
	                              DumpNode& line, Value& object,
	                              const Scope* naming_region = nullptr);
	// 5.3.1p3: the address of the object a member call is made on, written into
	// `node` in place of the object expression already under it.
	void address_of_object(Value& object, DumpNode& node, bool through_pointer);
	// 9.3.1p3 and 9.5p1: a member named with no object expression, which is a
	// member of the object `this` points to or of the one an anonymous union
	// declared.  `payload` is what the dump writes after the type, which is the
	// member's name alone when no `.` or `->` was written.
	// `checked_base` is false where 11.2p5's naming class is the one the name
	// was written on rather than the one that declared the member, which is
	// what a using-declaration makes it: the subobject the member belongs to is
	// still named, and the base-specifier's own access is not asked about.
	// `wrote_arrow` is 5.2.5p1's `E1->E2`, whose step to a base subobject moves
	// the pointer `E1` holds rather than naming an object that is there.
	Value member_value(SemaEntity& member, const Value& object_written,
	                   const std::string& payload, DumpNode& node,
	                   bool checked_base = true, bool wrote_arrow = false);
	// 9.5p1: the objects an anonymous class declared that stand between an
	// object expression and `member`.  A class written inside another anonymous
	// one leaves a chain of them, and the access holds each in turn from the
	// outermost in - which is the order the offsets add up in.
	Value through_anonymous_storage(const SemaEntity& member, Value object,
	                                bool checked_base,
	                                bool wrote_arrow = false);
	// 5.16p3: an operand of a conditional whose result is an lvalue of a base
	// class of that operand's own class.
	void convert_arm_to_base(Value& arm, TypeId result);
	// 5.16p3: an operand of a conditional whose result is a prvalue of class
	// type, which copy-initializes the result object from that operand.
	void transfer_arm_to_result(Value& arm, TypeId result, const Context& ctx,
	                            std::vector<SemaEntity*>& frame);
	// 11.2: the region the expression being read was written in.
	Scope* reading_;
	// 10.2: the object a member found through a base class is a member of,
	// which is the base subobject of the class that declared it.
	Value object_in_declaring_class(const Value& object,
	                                const SemaEntity& member,
	                                bool checked = true,
	                                bool wrote_arrow = false);
	// The object a member named with no object expression is a member of.
	Value implied_object(const SemaEntity& member, DumpNode& line);
	// 5.3.1p3: `&C::x`, where `entity` is the member of a class the qualified-id
	// named, which the caller resolved to learn that it is one.
	Value member_address(const AstNode& node, const SemaEntity& entity,
	                     DumpNode& parent);
	// 5.1.1p3: the object the member function being read was called on.
	Value this_value(DumpNode& parent);
	Value call_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	Value functional_cast(const AstNode& node, const Context& ctx,
	                      DumpNode& parent, TypeId target);
	// 5.3.4: `new T`, which is the call of an allocation function for the
	// storage one object of `T` needs and the initialization 8.5p16 gives the
	// object at what that call returned.  The value is the pointer.
	Value new_expression(const AstNode& node, const Context& ctx,
	                     DumpNode& parent);
	// 3.7.4.1p2 and 5.3.4p9: the declarations of `operator new` a
	// new-expression chooses among.  A new-expression written with `::`, and
	// one whose allocated type is no class, looks the name up in the global
	// namespace alone; any other looks in the class first and falls back to the
	// global namespace where the class declares none.  `found` takes the whole
	// overload set, which 13.3 then chooses from.
	SemaEntity* allocation_function(bool global, TypeId created, bool array,
	                                std::vector<SemaEntity*>& found);
	// 5.3.4p15 and 18.6.1.1p3: whether a call of this allocation function says
	// it obtained no storage by handing back null, which 15.4's non-throwing
	// exception-specification and `std::nothrow_t` together are what say.
	bool nothrow_allocation(const SemaEntity& function);
	// 5.3.5: `delete p`, which is 12.4p3's end of the lifetime of the object
	// `p` points to and 3.7.4.2's return of the storage it stood in.
	Value delete_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 3.7.4.2p2, 5.3.5p9 and 12.5p4: the deallocation function a
	// delete-expression calls, or the one 5.3.4p18 pairs with an allocation.
	// The class of the object is searched first for a delete-expression written
	// without `::`, and 12.5p4's usual one is chosen from what was found.
	SemaEntity* deallocation_function(bool global, TypeId destroyed, bool array,
	                                  std::vector<SemaEntity*>& found);
	// 5.3.5p2: an operand of class type where a delete-expression asked for a
	// pointer, which its class shall have exactly one conversion function to.
	void contextual_pointer(Value& value, const Context& ctx);
	// 5.3.4p6: the type a new-expression creates, with the first array-suffix
	// of a new-declarator handed back as the expression that says how many
	// objects of it there are, or null where none was written.
	TypeId new_type(const AstNode& type_id, const Context& ctx,
	                const AstNode*& bound);
	// 5.3.4p8: one object's worth of bytes, as the literal 2.14.2p2 gives it.
	Value object_size_value(unsigned long long bytes, DumpNode& parent);
	// 5.3.4p8 for the array form: the bytes `bound` elements of `element` and
	// `cookie` bytes of count in front of them occupy, and how many objects
	// that is where the count is one the translation knows.
	Value array_new_size(const AstNode& bound, TypeId element,
	                     unsigned long long cookie, const Context& ctx,
	                     DumpNode& parent, unsigned long long& elements,
	                     bool& counted);
	// 8.3.4p1: how many objects of its ultimate element type one element of an
	// array whose element type is `element` holds.
	unsigned long long array_element_count(TypeId element);
	// 5.3.4p15: what the elements of an array a new-expression creates come to,
	// written as one description of one element beside the deallocation
	// function 5.3.4p18's cleanup gives the storage back to.
	void array_new_initialization(TypeId created, const AstNode* written,
	                              bool global, const Context& ctx,
	                              DumpNode& line);
	// 12.1p5: the constructor 8.5p6 default-initializes an object of `element`
	// with, or null where the class declares none taking no argument or leaves
	// 13.3 two to choose between.
	SemaEntity* default_constructor(TypeId element);
	// 8.5p6 and 12.1p5: whether default-initializing one object of `element`
	// comes to nothing, which is what the array form's loop is written over.
	// It is 12.4p8's walk of the subobject tree the other way round.
	bool vacuous_construction(TypeId element);
	// 8.5: the initializer written for a declarator, in each of the three forms
	// 8.5p1 gives it.  `image` says 3.6.2 gives the object its value rather than
	// a function building it, which is what an object with static storage
	// duration asks for.
	void write_initializer(const AstNode& initializer, TypeId type,
	                       const Context& ctx, DumpNode& line,
	                       bool image = false);
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
	// 5p9, 5.7p1 and 5.9p2: the one type both operands of a built-in binary
	// operator are converted to.  It is the operator's result type wherever the
	// two coincide, and is asked for separately because a comparison and a
	// pointer subtraction have result types their operands do not share.
	TypeId binary_operand_type(unsigned op, const Value& left, const Value& right);
	Value cast_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	// 5.2.9p1 and 5.4p4: what a cast to a reference type makes of its operand,
	// which is the same whether the cast named its type with a type-id or with
	// the name of the type.
	Value cast_to_reference(TypeId target, Value& source, DumpNode& parent,
	                        DumpNode& line, Value value, const Context& ctx);
	Value sizeof_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 5.3.6p1: the alignment an object of the operand's type requires.
	Value alignof_expression(const AstNode& node, const Context& ctx,
	                         DumpNode& parent);
	// 5.19 and the course builtins: a call the translation answers itself.
	bool builtin_call(const std::string& name, const AstNode& node,
	                  const Context& ctx, DumpNode& parent, Value& out);
	// 1.4p8 and 17.6.4.3.2p1: the declaration of a reserved function the
	// implementation provides, made where a call names one no declaration of the
	// program reached.  It is declared in the global namespace, so the first
	// call to name it declares it and every later one finds it by ordinary
	// lookup; a name the implementation reserves nothing for leaves it null.
	SemaEntity* reserved_function(const std::string& written,
	                              std::vector<SemaEntity*>* found);
	// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation functions
	// every translation unit declares in the global namespace whether or not it
	// wrote them, bound there before the unit is read so a definition the
	// program writes is a definition of one of them.
	void declare_allocation_functions(Scope& where);

	// Templates (sema_template.cpp).
	//
	// 14.2 and 14.8.1: the declarations a template-id names, which are the
	// specializations its argument list makes of each template of that name,
	// chained as one overload set for 13.3 or 13.4 to choose from.
	// The specializations are appended to `found`, each a declaration of its own
	// that no chain holds, and the first of them is returned.  `in` is the
	// class 5.2.5p1 looks a member named after `.` or `->` up in, and null
	// where the spelling itself says where to look.
	SemaEntity* template_specializations(const std::string& spelling,
	                                     const Context& ctx,
	                                     std::vector<SemaEntity*>& found,
	                                     Scope* in = nullptr);
	// 14.2 at the doors a reading that has no overload set to hand on looks one
	// name up: the same two doors `id_expression` asks in the same order,
	// because a template-id denotes the specializations its list makes and
	// ordinary lookup finds only the template it was written from.  Null where
	// neither door reaches a declaration.
	//
	// 13.4p1 chooses among several with a target type, which such a reading has
	// none of to defer to - `&f<int>` comes to an address where it stands and
	// 7.1.6.2p4 asks what the id-expression names - so a list that fits more
	// than one declaration of the name names none here.
	//
	// `used` is 3.2p2: naming the specialization in a potentially-evaluated
	// expression is what asks 14.7.1p1 for its body, and 5p8's unevaluated
	// operand asks for nothing - `decltype(f<int>)` names the entity and leaves
	// the definition to whatever else the program writes.
	SemaEntity* folded_name(const std::string& spelling, const Context& ctx,
	                        bool used = true);
	// The walk itself, over the declarations `head` chains: one reading of the
	// list per declaration, with a reading that refused handed back in
	// `refused` rather than ending the walk.  3.4.2p3's search reaches
	// declarations of its own, and the list is read against those the same way.
	void explicit_specializations(SemaEntity& head, const TemplateId& id,
	                              const Context& ctx,
	                              std::vector<SemaEntity*>& found,
	                              std::string& refused);
	// 14.7.1: the declaration `arguments` makes of `primary`, made once
	// however many times it is named.
	SemaEntity& specialize(SemaEntity& primary,
	                       const std::vector<TypeId>& arguments);
	// 14.8.1p2: the declaration a template-id that wrote only a leading part of
	// `primary`'s argument list stands for, made once per template and list.
	SemaEntity& partial_template(SemaEntity& primary,
	                             const std::vector<TypeId>& arguments);
	// The type a template-argument names, over the PA12 subset: a fundamental
	// type or a type name, with cv-qualifiers, pointers and references written
	// around it.
	TypeId template_argument_type(const std::string& spelling,
	                              const Context& ctx);
	// 8.1p1, 8.3p1, 8.3.4p1, 8.3.5p1 and 7.1.6.2p1 over the words that spelling
	// was split into, which `sema_type_id.cpp` owns: a walk over text rather
	// than over the tree, so it is a reader of its own that borrows this one.
	friend class SpelledTypeId;
	// 14.7.1p1: the declaration a specialization stands for, held for the end
	// of the translation unit, which is where the output writes it.  Naming
	// the same specialization again writes nothing more.
	void instantiate(SemaEntity& function);
	void write_instantiation(const Pending& pending);

	// 14p1: records what a template-declaration parameterises rather than
	// reading it, for the declarations this milestone instantiates.  False for
	// a template-declaration whose declaration is not one of them, which is
	// then read the way the earlier assignments read it.  `member` says the
	// head stands under one already bound, so 14.5.2p3's own tier is not asked.
	bool record_template(const AstNode& node, const Context& ctx,
	                     bool member = false);
	// 14.7.3p1: the declaration a `template<>` head wrote, which declares the
	// specialization itself.  False outside the supported slice, which leaves
	// the ordinary walk to read it as it did before.
	bool record_explicit_specialization(const AstNode& declared, const Context& ctx);
	bool record_explicit_function(const AstNode& declared, const Context& ctx);
	// 14.7.3p1: the declarator-id one `template<>` head stands over, whichever
	// of the three shapes of declaration it wrote.
	static std::string specialized_declarator_id(const AstNode& declared);
	// 14.7.3p5: the members of an explicitly specialized class are defined the
	// way a normal class's are, so a `template<>` head over one is refused.
	// `written` is the declarator-id the head stands over.
	void require_unspecialized_owner(const std::string& written,
	                                 const Context& ctx);
	// 14.1p2 and 14.3p1: what a head's places are, and what one written
	// argument list makes of them, which `sema_template_head.h` owns because
	// each is a reading of its own.
	friend class TemplateHead;
	// 14.2: the specialization a name written as a template-id denotes, or
	// null when `component` is no template-id or names no template this
	// milestone instantiates.  `in` is the region a qualified name looks into
	// and null for one looked up from `ctx`.
	SemaEntity* template_id_entity(const std::string& component,
	                               const Context& ctx, Scope* in,
	                               LookupKind filter);
	// 14.5.1p1: the same over a variable template, whose name is bound as an
	// ordinary declaration and so is reached by no lookup asking for a type.
	SemaEntity* variable_template_entity(const TemplateId& id,
	                                     const Context& ctx, Scope* in,
	                                     LookupKind filter);
	// 14.1p2: the head the template place `parameter` stands for wrote, null
	// for every type that is no such place.
	TemplateInfo* place_head(TypeId parameter) const;
	// 14.6.2p1: the declaration `C<A…>` stands for while `C` is still the place
	// its own head declared - one per place and argument list, because the same
	// spelling written twice in one pattern names the same type.
	SemaEntity& dependent_template_name(TypeId parameter,
	                                    const std::vector<TypeId>& arguments,
	                                    const std::string& spelling);
	// 14.3p1 and 14.7.1p1: the class `arguments` makes of the class template
	// `primary`, made once however many times it is named.  The pattern is
	// read against a region binding each parameter to its argument, so every
	// name in the body is looked up with the arguments already in hand.
	SemaEntity& instantiate_class(SemaEntity& primary,
	                              const std::vector<TypeId>& arguments);
	// The source spelling of a type, which is what a specialization is named
	// by: `Box<int>` rather than the dump's description of what it holds.
	std::string type_spelling(TypeId type) const;
	// 9.1p2: `entity` is the declaration `type` was made by.  The model
	// answers it for a walk that has the type in hand, and the type table
	// carries it for the object-file name a *use* of the type is encoded
	// into - so one call settles both and no site records only one of them.
	void own_type(TypeId type, SemaEntity& entity);
	// 14p1: records the pattern the template-declaration being read wrote onto
	// the function it just declared, so that 14.7.1p1 can read it again.
	// `reading` is 14.5.1.3p1's other region: the head standing outside this
	// declaration's own, where the names an out-of-class definition wrote for
	// the enclosing classes' places are bound.  Null for every definition
	// written where its declaration is made.
	void record_function_template(SemaEntity& entity, Scope& parameters,
	                              Scope& region, Scope* reading = nullptr);
	// 14.6p8: the reading a template definition's body gets where it stands,
	// against the parameters themselves - one this milestone makes ill-formed
	// where no valid specialization could be generated from it, leaving what
	// depends on a parameter to the instantiation.  `sema_template.cpp` holds
	// what the reading does and does not ask.  Nothing it finds reaches the
	// output.
	// `implicit` is how many of the type's parameters 9.3.1p3 wrote rather than
	// the declarator, which is one for a member template and none otherwise.
	void check_template_definition(const AstNode& node, const Context& inner,
	                               const std::vector<Parameter>& parameters,
	                               TypeId type, std::size_t implicit = 0);
	// 14.6.2.2p1: whether a name written in `scope` can reach a type an argument
	// list has yet to say, which is what leaves 7.1.6.2p4's question to the
	// instantiation; and the type the specifier stands for until then.
	bool dependent_reading(const Scope& scope);
	TypeId dependent_expression_type(const AstNode& node, const Context& ctx);
	// 14.7.1p1: `scope` with every name it and the regions like it around it
	// bind standing for what the substitution makes of it, for a reading that
	// has to happen again where the arguments are known.
	Scope& substituted_region(Scope& written,
	                          const std::vector<std::size_t>& reach,
	                          std::size_t level,
	                          const std::unordered_map<TypeId, TypeId>& bindings,
	                          std::unordered_map<TypeId, TypeId>& memo);
	// 14.6.2p3: records that `specifier` names a dependent base, and asks it.
	void note_dependent_base(const AstNode& specifier);
	bool wrote_dependent_base(const AstNode& specifier) const;
	// 9.2p2 and 14.7.1p1: the end of the unit writes this definition, unless an
	// instantiation is what made it - and then the use that names the member
	// is what asks for it, which `require_definition` is - or, for 3.2p3's use
	// with no call under it, `note_instantiated_transfer`.
	void queue_definition(Pending& pending);
	// The one door every entry the end of the unit walks goes through, which is
	// where 6.6.3p2's chain is stamped onto it.
	void hold_definition(Pending& pending);
	void require_definition(SemaEntity& function);
	void note_instantiated_transfer(SemaEntity& constructor);
	// 10.3p10: the virtual members every table this instantiation made names.
	void require_table_definitions(SemaEntity& made);
	// 9.2p2: a member function body read for a pattern is read where the class
	// is complete, so the reading holds each until the class-specifier closes.
	void hold_pattern_body(const AstNode& node, const Context& inner,
	                       const std::vector<Parameter>& parameters,
	                       TypeId type);
	void read_held_pattern_bodies(std::size_t from);
	// 14.6p8 and 3.4p1: looks up the names `node` writes that no template
	// parameter stands in the way of.  A member name, a template-id and the
	// callee of a call are left to the instantiation, which is what 14.6.2p1
	// and 3.4.2p2 leave them to.
	void check_expression_names(const AstNode& node, const Context& ctx);
	// 3.4.2p2: whether the arguments of `call` associate any namespace beyond
	// the ones ordinary lookup already read, which none but literals do.
	bool arguments_associate_nothing(const AstNode& call) const;
	// 3.4p1: what the unqualified name `node` wrote names where the definition
	// stands, with `answered` false for the names this reading does not settle.
	SemaEntity* definition_time_name(const AstNode& node, const Context& ctx,
	                                 bool& answered);
	// 3.4p1 and 5.1.1p8: the one unqualified name an id-expression wrote,
	// which shall name something, name one thing, and not name a type.
	void check_value_name(const AstNode& node, const Context& ctx);
	// 14.2p4: the keyword a member template-id shall be written with where the
	// object expression is type-dependent and the name is not a member of the
	// current instantiation.
	void check_member_template_keyword(const AstNode& node, const Context& ctx);
	// 3.4p1: the callee of a call 3.4.2p2 adds nothing to, which shall name
	// something - and 5.2.3p1 lets what it names be a type.
	void check_callee_name(const AstNode& node, const Context& ctx);
	// 14.7.1p1: the body the template's definition wrote, read again against
	// the arguments `function` was made from.  14.6.4.1p1 gives a
	// specialization named before that definition was written a second point of
	// instantiation at the end of the unit, so this is reached from there as
	// well as from the name that asked for it.
	void instantiate_body(SemaEntity& function);
	// 14.7.1p1: reads the template's class body for `made`, which is what
	// completes it.  A specialization named before its template was defined is
	// a declaration of an incomplete class until the definition arrives, and
	// this is what the definition then does to it.
	void complete_specialization(SemaEntity& made);
	// 14.7.1p1: an instantiation asks for `made`, which puts it on its
	// template's list of the specializations a later declaration is read for
	// and completes it.  14.6p8's reading of a template definition names a
	// specialization without asking for one, so what it makes stays off that
	// list until a use arrives.
	void require_specialization(SemaEntity& made);
	// 14.7.1p1: naming a specialization where the name stands, which asks for
	// it only where the context requires a completely-defined type; and the
	// demand every context that does require one makes of what it named.
	void asked_specialization(SemaEntity& made);
	void require_complete_type(TypeId type);
	// 10p1 and 14.6p8: a type a reading of a template definition requires to be
	// complete where the definition stands, which no argument list can change.
	void require_settled_type(TypeId type);
	// 14.3p1 and 14.8.2: `type` with every template parameter `bindings` names
	// replaced by the type bound to it.  The type table rebuilds every
	// category that is only made of types; a specialization is the one that is
	// not - `A<T>` with `T` bound to `int` is the class `A<int>` names, which
	// only an instantiation can make - so the walk belongs here and delegates
	// the rest.  `memo` holds the answers of one substitution.
	TypeId substituted(TypeId type,
	                   const std::unordered_map<TypeId, TypeId>& bindings,
	                   std::unordered_map<TypeId, TypeId>& memo);

	// 8.5: initialising an object of `target` from `node`, which is what a
	// variable, a condition, a return statement and an argument all do.
	// `listed` says the initializer is a clause of a braced-init-list, which
	// 8.5.4p7 will not let narrow the value it holds.
	Value initialize(const AstNode& node, TypeId target, const Context& ctx,
	                 DumpNode& parent, bool listed = false,
	                 Requested by = Requested::Written, bool direct = false);
	// 12.8p32: a return whose operand names an automatic object of the
	// function's own returned type reads that object as an rvalue, so 13.3
	// chooses the move member of its class where the class has one.
	void return_as_rvalue(Value& value, TypeId target);
	// 8.5.4p7: the error that a clause narrows to the type it initialises.
	void require_no_narrowing(const AstNode& written, const Value& value,
	                          TypeId target, const Context& ctx);
	// 4.8p1 and 8.5.4p7: whether an object of `to` has room for the floating
	// value a clause came to, which is the second bullet's exception - a range
	// and not an exactness, so a value it rounds is a clause it takes and a
	// value it overflows is not.
	bool floating_fits(long double held, TypeId to);
	// 8.5.4: a braced-init-list written where an expression initializes an
	// object.  Over the PA12 scalar subset it holds at most one clause, whose
	// value the object takes, and an empty one value-initializes it.
	Value list_initialize(const AstNode& node, TypeId target, const Context& ctx,
	                      DumpNode& parent, bool image = false);
	// The same reading, written into a line something else already opened -
	// which is what an argument needs, because 13.3.3.1.5 leaves the list
	// unread until 13.3 has chosen and the place it stands in among the
	// arguments was taken before that.
	Value list_initialize_into(const AstNode& node, TypeId target,
	                           const Context& ctx, DumpNode& line, bool image);
	// 13.3.3.1.5p1: an argument written as a braced-init-list is not an
	// expression, so it is carried rather than read: the line it will be
	// written on holds its place among the arguments until the parameter it
	// reaches is known.  Anything else is the expression it is.
	Value argument_expression(const AstNode& node, const Context& ctx,
	                          DumpNode& parent);
	// 13.3.3.1.5: the implicit conversion sequence of an argument written as a
	// braced-init-list, which is a fact of the parameter type alone.
	Match match_list(std::size_t clauses, TypeId parameter, TypeId listed_class);
	// 13.3.1.7 and 8.5.1: whether a braced-init-list of this many clauses
	// initializes an object of `type` at all, which is what 13.3.3.1.5 asks of
	// every candidate's parameter.  8.5.1 gives an aggregate's subobjects the
	// clauses; every other class is initialized by one of its constructors, so
	// what the list needs is one that accepts that many arguments.  The clauses
	// themselves are read once, where the initialization is written, rather
	// than once per candidate.
	bool list_initializes(TypeId type, std::size_t clauses);
	// 8.5.1p6 and 8.5.1p11: the most initializer-clauses an object of `type`
	// can take, which is the number of leaves its subobject tree has - a
	// nested aggregate contributes its own, because the braces around it may be
	// left out, and a union contributes one because 8.5.1p15 initializes it by
	// its first member alone.  A list with more clauses than that initializes
	// no object of the type, which is what keeps `f(One)` out of the candidates
	// of `f({1,2})`.  Held per type, so a class is walked once however many
	// lists ask.
	unsigned long long clause_capacity(TypeId type);
	// 8.5.1p11: what one subobject of that type takes out of the enclosing
	// list, which is its own capacity or the one clause its written braces are
	// - never nothing, however little the subobject holds.
	unsigned long long clauses_a_subobject_takes(TypeId type);
	std::unordered_map<TypeId, unsigned long long> clause_capacity_;
	// 8.5.4: what the list is worth once the type it initializes is known -
	// an object of a class 13.3.1.7 or 8.5.1 built, or the value 13.3.3.1.5p6
	// gives any other type.  Written into the line the argument already held.
	void initialize_from_list(Value& value, TypeId target, const Match& match,
	                          const Context& ctx, Requested by);
	// 13.3.3.1 over the PA12 conversion subset.
	Match match_argument(const Value& argument, TypeId parameter);
	// `direct` is 13.3.1.4p1's context: the parameter is a reference to the
	// class an object is being direct-initialized of, so the temporary this
	// converts into is one that initialization asked for and 12.3.2p2's
	// `explicit` conversion functions of the argument's class reach it.
	Match match_by_value(const Value& argument, TypeId parameter,
	                     bool direct = false);
	Match match_reference(const Value& argument, TypeId parameter);
	// 13.3.1p4 and p5: how the implied object argument reaches a member
	// candidate's implicit object parameter, which is the pointer conversion
	// 9.3.1p3's parameter asks for plus what 8.3.5p1's ref-qualifier says about
	// the category the object expression had.
	Match object_match(const Value& object, const SemaEntity& candidate,
	                   TypeId parameter);
	// 8.5.3p5: whether a reference of `parameter` binds `argument` itself rather
	// than a temporary a conversion made, which is what 5.16p3 asks of the two
	// operands of a conditional expression about each other.
	bool binds_reference(const Value& argument, TypeId parameter);
	// 4.4 and 4.10: whether a pointer of `from` converts to one of `to`.
	// 10p1: the base class of `derived` that `base` names, or null where
	// `derived` derives from no such class.
	bool pointer_convertible(TypeId from, TypeId to, int& rank, bool& exact);
	bool qualification_convertible(TypeId from, TypeId to);
	// 3.9.3p5: `type` with every top level cv-qualifier removed, reaching
	// through an array to its elements.
	TypeId bare_type(TypeId type);
	// 8.3.6p4: whether a call that gives `given` arguments can reach `function`,
	// which it can when every parameter beyond them was written with a default.
	bool accepts_arity(const SemaEntity& function, std::size_t given) const;
	// 8.3.6p9: the default-argument of one parameter, analysed in the region
	// the declaration that introduced it was written in.
	void write_default_argument(const SemaEntity& function, std::size_t index,
	                            DumpNode& parent);
	// 8.3.6p4 and 8.3.5p10: what this declaration says about each parameter of
	// the function it declares, whether or not it is the definition.  The
	// default-argument is the function's from this declaration on, and a
	// parameter already given one keeps the region that introduced it; the name
	// is no part of the type, so a parameter this declaration left unnamed takes
	// the name the first declaration that named it gave.
	void record_declared_parameters(const SemaEntity& function,
	                                std::vector<Parameter>& declared,
	                                Scope* region);
	// 8.3.5p10: the same fact given to a list of parameters copied out of one
	// declaration - 12.8p28's and 12.9p8's definitions are written from one -
	// so a place that declaration left unnamed is spelled with the name the
	// function has for it rather than with one of the output's own.
	void name_recorded_parameters(const SemaEntity& function,
	                              std::vector<Parameter>& taken,
	                              std::size_t implicit) const;
	// 13.3.3: the one candidate no other beats, or the error that there is not
	// one.  `candidates` are the declaration chains the lookup found, walked in
	// declaration order within each; `arguments` are the analysed argument
	// expressions in order.
	// `object`, when given, is 13.3.1p3's implicit object argument: it is matched
	// against the first parameter of every non-static member candidate and left
	// out of every other, and 13.3.1p4 makes a non-static member no candidate at
	// all where there is none.
	// `converting` leaves out every candidate declared `explicit`, which
	// 13.3.1.4 does for copy-initialization.
	// 5.2.2p4 and 5.2.2p10: the tail of a call once its declaration is known -
	// the conversions its arguments take, the default-arguments 8.3.6 supplies,
	// and the value the call is.  A call the program wrote and the call
	// 13.3.1.2p1 makes of an operator expression end the same way.
	Value finish_call(DumpNode& line, TypeId function,
	                  std::vector<Value>& arguments, const SemaEntity* chosen,
	                  const Context& ctx);
	SemaEntity* select_overload(const std::vector<SemaEntity*>& candidates,
	                            const std::vector<Value>& arguments,
	                            const std::string& name,
	                            const Value* object = nullptr,
	                            bool converting = false,
	                            std::size_t singles = 0,
	                            const Value* operand = nullptr,
	                            bool* unviable = nullptr);

	// 13.3.3.2: which of two conversions of one argument is better, as 1, 0
	// or -1.
	int compare_matches(const Match& left, const Match& right);
	// 13.3.3p1: whether one candidate beats another, which is its conversions
	// and, where those tie, whether it is a declaration the program wrote and
	// the other a specialization a deduction made.
	bool better_candidate(const Match* left, const Match* right,
	                      std::size_t count, bool left_written = false,
	                      bool right_deduced = false,
	                      SemaEntity* left_template = nullptr,
	                      SemaEntity* right_template = nullptr);
	// 14.5.6.2p2: whether every parameter type of `right` is deduced from the
	// one `left` wrote in the same place, which is what makes `left` at least
	// as specialized as `right`.  The answer is a fact of the two templates
	// and is kept, because a call over a large overload set asks it of the
	// same pair once per comparison.
	bool at_least_as_specialized(SemaEntity& left, SemaEntity& right);
	// 14.5.6.2p4: whether `left` is more specialized than `right`, which is the
	// one question 13.3.3p1's tie between two specializations and 13.4p1's
	// target type both ask of the templates a deduction made them from.
	bool more_specialized(SemaEntity& left, SemaEntity& right);
	// 14.5.6.2p9: which of two templates the places both of them deduce leave
	// ahead, which is what the references and the qualifiers 14.5.6.2p5 and p7
	// took off the types still say.  Positive for `left`, negative for `right`,
	// zero where the places do not agree or say nothing.
	int reference_order(SemaEntity& left, SemaEntity& right);
	// Rewrites what the dump wrote for `value` where a conversion is visible in
	// it: a null pointer constant, a resolved function name, and the temporary
	// a reference binds to.  Each rewrites the line the operand already wrote,
	// in the place it wrote it.
	void apply_conversion(Value& value, TypeId target, const Match& match,
	                      const Context& ctx,
	                      Requested by = Requested::Written);
	// 13.4: the declaration of an overloaded name a target type asks for.
	SemaEntity* resolve_target(const Value& value, TypeId target);
	// 3.2p2: what naming `selected` asks for - 14.7.1p1's instantiation, the
	// definition an instantiated class put aside, 12.8p28's implicit
	// definition - and the declaration 7.3.3p1 leaves every use reaching.  A
	// fold that has chosen a callee names it here too.
	SemaEntity& named_function(SemaEntity& selected);
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

	// The dump.
	//
	// One line of a resolved expression: the node kind, what the expression
	// denotes, its type, and the payload the format writes after it.  Every
	// line of the PA12 expression output is spelled here, so a conversion that
	// rewrites one writes the same shape the operand wrote.
	std::string spell(const char* what, ValueCategory category, TypeId type,
	                  const std::string& payload) const;
	std::string spell(const char* what, ValueCategory category, TypeId type,
	                  const AstNode* payload) const;
	// Spells `value`'s line again, from the category and type it now has.
	void respell(const Value& value) const;
	// Writes what `value` is onto the line it wrote, as typed facts.  Every
	// path that spells a line runs through `respell` or through the one place
	// that first spells it, so recording there is what keeps the facts and the
	// text one description of the same value.
	void record(const Value& value) const;
	// 2.14.4p1: the zero of a floating type, spelled as a literal of that type
	// so that the suffix says which of the three widths it is a value of.
	const char* floating_zero(TypeId type) const;
	static std::string payload_of(const AstNode& node);
	// The name the PA12 dump gives a declaration of `scope`.
	std::string dump_name(const Scope& scope, const std::string& name) const;
	std::string abi_name(const Scope& scope, const std::string& name) const;
	bool semantics() const { return dialect_ != SemaDialect::Types; }
	bool lowering() const { return dialect_ == SemaDialect::Lowering; }
	// 14p1 and 14.6p8: whether the template layer answers here.  A definition
	// read for what 14.6p8 says about it stands in the PA11 dialect - it
	// describes what its declarations say rather than translating them - and a
	// template-id it writes still names the specialization its arguments make.
	bool templating() const { return lowering() || checking_ > 0; }

	SemaDialect dialect_;
	// The dialect the unit itself is read in, which no reading changes.  14.6p8
	// puts one aside to read a pattern; an instantiation made in earnest under
	// that reading - 10p1's base class - is read in this one instead, because
	// what it owes is 12.1's members and a layout rather than a description.
	const SemaDialect unit_dialect_;
	// 16.6: the packing alignment by position in the token stream, borrowed
	// from the stream the tree was parsed from.  Null where the caller has
	// none, which is every mode but the lowering.
	const PackTable* packs_;
	// 2.2p1: which positions of that stream this unit's own source wrote,
	// borrowed the same way and null in the same modes.
	const IncludeTable* sources_;
	// 7.1.6.2p1: the trees the parse kept for the operands it flattened.
	const AstArena* written_;
	TypeTable types_;
	SemaModel model_;
	// The unnamed enumerations declared so far, which are numbered rather than
	// named after a token span because that is the convention the refs use.
	unsigned anonymous_enums_;
	// The unnamed classes defined in a function so far, which 9p1 leaves with
	// no name of their own and which the convention numbers.
	unsigned local_types_;
	// 10.3p2: the slots one class introduced, by the key of the declaration that
	// introduced each.
	typedef std::unordered_map<std::uint64_t, unsigned> SlotIndex;
	// 10.3p2's keys: the member names this settlement has compared, each stored
	// once so that a key is two words rather than a string built for the probe.
	NameTable override_names_;
	// 10.3p2 and 10.3p10: for each class that introduces virtual functions, the
	// slot each name and signature it introduced took.  A slot's index is fixed
	// where the name first took one, so a class below reads the record of the
	// class that introduced it rather than building an index over everything it
	// inherited - which is what keeps the settlement's records linear in the
	// declarations rather than quadratic in the derivation.  Kept here rather
	// than on the class because nearly every class introduces none.
	std::unordered_map<std::uint32_t, SlotIndex> introduced_slots_;
	// The definitions the end of the translation unit writes, in the order they
	// were asked for.  Writing one may ask for another, so the list grows while
	// it is being walked and what is being written has to stay where it is.
	std::deque<Pending> pending_;
	// 9.2p2: the member function bodies a pattern reading has yet to read.  A
	// reading can stand inside another, so each takes the entries above the
	// mark it recorded on the way in.
	std::vector<HeldTemplateBody> held_bodies_;
	// 14.6.2p1: the declarations the readings above made for names written
	// through a dependent prefix, keyed by that prefix and the spelling.
	std::unordered_map<std::string, SemaEntity*> dependent_names_;
	// 7.1.6.2p1: the same, for a decltype-specifier over an expression this
	// reading does not type, keyed by the specifier and the region it stands in
	// and read back by the substitution that has the arguments.
	std::unordered_map<std::string, TypeId> dependent_expressions_;
	std::unordered_map<TypeId, DependentDecltype> dependent_written_;
	// 14.7.1p1: the member bodies a class template specialization has not been
	// asked for, keyed by the declaration each defines.  Instantiating a class
	// instantiates the *declarations* of its members and not their definitions,
	// so a body waits here for the use that names the member - which is what
	// lets a specialization be laid out over an argument the bodies of the
	// members nothing calls could not have been read against.
	std::unordered_map<std::uint32_t, Pending> held_definitions_;
	// 14.7.3p1 at the object tier: the line each defined object's definition
	// wrote, keyed by the declaration it defines.  A body 14.7.1p1's reading
	// held is dropped from the map above where the program writes its own out
	// for these arguments; an object's definition is no held thing but a line
	// already in the dump, so what is dropped is that line's own claim to be
	// the definition.  One pointer per object a definition was written for.
	std::unordered_map<std::uint32_t, DumpNode*> object_definitions_;
	// 9.2p2 and 8.4.2p2: the classes this unit completed, in the order their
	// answers were first settled - which settles a subobject's class before
	// whatever holds it, so asking them again in it costs one pass.  The flag is
	// whether a later definition changed one of those answers.
	std::vector<SemaEntity*> settled_classes_;
	bool resettle_classes_;
	// 14.7.1p1: how many readings of a pattern for a specialization stand
	// around this walk, which says whose definitions are its own, and whether a
	// body one made stands around it - what 3.2p3 asks of an elided use.
	unsigned instantiating_class_;
	unsigned instantiated_body_;
	// 6.6.3p2: whether the chain of readings that made the body now being read
	// reaches the program's own code through returned objects alone, and what a
	// body queued while this one is read inherits of it.
	bool reading_chain_;
	bool queued_chain_;
	// 14.7.1p1 with 14.7.3p1: how many of those readings are of a *pattern*,
	// which is the narrower question "is the definition this reading makes one no
	// unit wrote out".  `complete_specialization` reads two kinds of body under
	// one instantiation - the template's pattern, and the class body `template<>`
	// wrote for exactly these arguments - and only the first makes definitions a
	// later written one replaces rather than redefines.
	unsigned instantiating_pattern_;
	// 14.6.2p3: the base-specifiers a reading found to name a dependent type.
	// It is the syntax that is dependent and not the class the arguments make
	// of it, so the fact belongs to the clause the program wrote once - which
	// is what lets every specialization of the template, and every class an
	// instantiated body declares, answer 3.4.1 the way the definition did.
	std::unordered_set<const AstNode*> dependent_bases_;
	// 14.1: the parameters each template's declarator wrote, which an
	// instantiation writes again with their types substituted.  It is keyed by
	// the entity the template-declaration declared because it is a fact about
	// how that one declaration was spelled rather than about the type it made,
	// and only a template is ever asked for, so an ordinary declaration adds
	// nothing to it.
	std::unordered_map<std::uint32_t, std::vector<Parameter> > templates_;
	// 14p1: the patterns the unit's template-declarations wrote, one per
	// declaration the milestone instantiates.  They are held here because a
	// pointer handed to an entity has to outlive every instantiation that
	// reads it, and a deque never moves what it already holds.
	std::deque<TemplateInfo> template_patterns_;
	// 14.1p2: the head one template-template place wrote inside another head,
	// keyed by the clause it was written as.  14.3.3p1 matches a written
	// argument's own places against it, and 14.5.6.1p5 lets a template be
	// declared many times over - so the clause is read once and every
	// declaration that spells it reaches the same places.
	std::unordered_map<const AstNode*, TemplateInfo*> parameter_heads_;
	// 14.1p2: that same head, keyed by the type the place stands for.  A
	// `C<A…>` written inside the pattern has the type and not the head that
	// declared `C`, and 14.3.3p1 is asked again wherever the list arrives.
	std::unordered_map<TypeId, TemplateInfo*> place_heads_;
	// 14.6.2p1: the declaration one `C<A…>` written over a template place
	// stands for, keyed by the place and the interned argument list - so one
	// spelling written n times in a pattern is one declaration.
	std::unordered_map<std::uint64_t, SemaEntity*> dependent_templates_;
	// 14.1p9: the whole argument list one list of explicit arguments makes of a
	// template, keyed by the template and that list.  A default is an
	// expression read in a region binding the parameters before it, so it is
	// read once rather than at every naming of the same specialization.
	std::unordered_map<std::uint64_t, std::vector<TypeId> > default_arguments_;
	// 5.19 and 14.2: the terminals one template-argument spelling splits into,
	// and 14.6.2p2's argument a spelling no argument list has settled stands
	// for.  Both are facts of the text alone, so a template-id written n times
	// costs one split and names one specialization.
	std::unordered_map<std::string, std::vector<std::string> > value_words_;
	std::unordered_map<std::string, TypeId> dependent_values_;
	// 14.5.6.2p2: which of two function templates is at least as specialized as
	// the other, keyed by the two declarations.  It is a fact of the pair, and
	// 13.3.3p1 asks it of the same pair once for every comparison a call makes.
	std::unordered_map<std::uint64_t, bool> specialization_order_;
	// 14.5.6.1p5: the stand-ins two template heads are compared through and the
	// signature each declaration was built into, which `sema_template.h` owns
	// beside the head they are built from.
	TemplateSignatures signatures_;
	// 14.8.1p2: the declaration a template and a leading part of its argument
	// list stand for, keyed by the two - so a name written twice reaches one
	// candidate and a use that deduces the rest makes one specialization.
	std::unordered_map<std::uint64_t, SemaEntity*> partial_templates_;
	// 14.1p9: the type-id a head wrote as one parameter's default, kept by that
	// parameter's own declaration because 14.8.1p2 leaves a trailing argument to
	// be deduced *or* taken from here, and a function template's pattern is
	// recorded from its definition rather than from its head.
	std::unordered_map<std::uint32_t, const AstNode*> parameter_defaults_;
	// 14p1: the declaration the template-declaration being read parameterises,
	// and the dump its lines stand in, while its declarators are read.  Null
	// wherever the walk is not inside one.
	const AstNode* template_pattern_;
	DumpScope* template_pattern_dump_;
	// 14.7.1p1: the specialization whose pattern is being read, which is the
	// declaration that reading is of rather than one it makes.  Null wherever
	// the walk is reading a declaration the program wrote.
	SemaEntity* instantiating_;
	// 14.6p8: the depth of a definition-time reading of a template's body.
	// Non-zero says the walk is checking a pattern rather than translating a
	// declaration the program has, so nothing it reads is declared into the
	// output and nothing it names demands an instantiation.
	unsigned checking_;
	// 14.6p8: how many times a reading has stood a value in the place of one an
	// argument list has yet to settle.  A constant expression that took one is
	// not the one the instantiation will evaluate, so 7p4's static_assert over
	// it asserts nothing where the pattern stands.  It is a count and not a
	// flag because one such reading stands inside another.
	unsigned stood_in_;
	// 5.3.3p1, 7.1.6.2p4 and 5.3.7p1: the depth of a reading of an
	// *unevaluated* operand, taken at each of the three doors that open one.
	// 5.1.1p13's third bullet is the one question that turns on it: an
	// id-expression naming a non-static data member may stand in such an
	// operand, where no object is named and the member's declared type is the
	// whole answer.  It is a depth and not a flag because one unevaluated
	// operand is written inside another, and it is taken at the operand's own
	// door so that a body read on the way - a member function of a class an
	// instantiation completed there - carries its own `self_` and not this.
	unsigned unevaluated_;
	// 14.7.1p1: how many specializations a name has left declared, waiting for
	// the first context that requires a completely-defined type.  A demand for
	// a definition costs the count being zero at every place the standard
	// requires one, which is what it is in a unit that named no specialization.
	unsigned declared_only_;
	// 12.9p8: the parameters each constructor was declared with, which the
	// inheriting constructor a using-declaration declares takes as its own -
	// their names as much as their types, because the definition this unit
	// generates for it writes them.  Only a constructor is ever inherited, so
	// only a constructor's declaration adds to this.
	std::unordered_map<std::uint32_t, std::vector<Parameter> >
		constructor_parameters_;
	// What the declarations of each function have said about its parameters so
	// far, in the order its type gives them (`sema_declaration.h`).  Keyed by
	// the function, because 8.3.6p4's default-argument and 8.3.5p10's name are
	// each facts of the function that any one of its declarations may be the
	// one to give.
	std::unordered_map<std::uint32_t, std::vector<ParameterRecord> > defaults_;
	// 12.6.2p8: the brace-or-equal-initializer each non-static data member was
	// declared with, and the region it was written in, which 9.2p2 makes the
	// complete-class context it is read in.  It is keyed by the member because
	// it is a fact about that one declaration, and it is read once by every
	// constructor whose mem-initializers do not name the member.
	std::unordered_map<std::uint32_t, HeldInitializer> member_initializers_;
	// 9.3.2p1: the implicit object parameter of the function whose body is
	// being read, which is what `this` and a member named with no object
	// expression denote.  Null outside a member function.
	SemaEntity* self_;
	// 11p6: the class the declaration being read names a member of, which is
	// the context its names are access-checked in.  Null while the declaration
	// names no member of a class.
	Scope* naming_;
	// 6.6.1 and 6.6.2: the statements a `break` or a `continue` may leave, and
	// 6.4.2 the switch a `case` may label, as the counts of the enclosing ones.
	unsigned breakable_;
	unsigned continuable_;
	unsigned switches_;
	// 3.8p1 with 6.6.1p1 and 6.6.2p1: how deep the lifetime frames were when
	// each of those statements was entered, so that a jump out of one knows
	// which blocks it leaves.  The statement's own frame is not one of them:
	// control lands where that frame is closed.
	std::vector<std::size_t> breakable_frames_;
	std::vector<std::size_t> continuable_frames_;
	// 3.8p1: how many objects the open blocks hold whose lifetimes end in a
	// call, carried so that a jump asking whether any does costs nothing per
	// block around it.
	std::size_t live_destructions_;
	// 6.1p1 and 6.6.4p1: the labels the function being read has written, and
	// the ones its goto statements name.  A label may be written after the
	// goto that names it, so the two are gathered and matched once the body
	// has been read.
	std::unordered_set<std::string> labels_;
	std::vector<std::string> gotos_;
	// 6.6.3: the return type of the function whose body is being read.
	TypeId returns_;
	// 12.4p8: the answer `vacuous_destruction` gave for a type, so a class whose
	// subobjects are n deep is walked once and not once per object of it whose
	// lifetime ends.
	std::unordered_map<TypeId, unsigned char> vacuous_;
	// 12.1p5: the answer `vacuous_construction` gave for a type, held for the
	// same reason - the array form of a new-expression asks it once per
	// expression and the walk below it is the same subobject tree.
	std::unordered_map<TypeId, unsigned char> vacuous_construction_;
	// 3.4.1p8: the unit's out-of-class special member definitions, keyed by the
	// unqualified name each defines, so the one that defines a given class's
	// destructor is found in one probe rather than a walk of the unit per
	// class.  Filled once from the syntax, before the first declaration is
	// read.
	std::unordered_map<std::string, std::vector<const AstNode*> >
		unit_definitions_;
	// 13.3.1.1.2p2: `OperatorCall::surrogates`' memo, one entry per class.
	std::unordered_map<TypeId, std::vector<SemaEntity*> > surrogate_calls_;
	// 13.3.3.1.2p1: whether the sequence being measured is the second, standard
	// conversion sequence of a user-defined one, which holds no user-defined
	// conversion of its own.  One flag says it for both directions - a
	// converting constructor's parameter and a conversion function's result -
	// so a class whose conversion reaches another class whose conversion
	// reaches it back is one probe rather than a walk that does not end.
	bool standard_only_;
	// 13.3.1.4p1: the class an object is being direct-initialized of, while
	// 13.3.1.3's candidates are being measured over its one written argument.
	// A temporary bound to the first parameter of a constructor of that class,
	// where the parameter is a reference to it, is initialized in the context
	// of that direct-initialization - so 12.3.2p2's `explicit` conversion
	// functions of the argument's own class are candidates for it, exactly as
	// they are for a cast.  `kNoType` everywhere else, which is every
	// copy-initialization and every call with any other number of arguments.
	TypeId direct_initialized_;
	// 7.5p1 and 7.5p4: the language linkage of the linkage-specification the
	// declaration being read is written inside, which is the innermost one.
	bool c_linkage_;
	// How deep the walk of one expression is.
	//
	// 5.6 makes `a + b + c + ...` a tree as deep as it is long while nesting no
	// rule, so the PA10 parse guard does not bound it: a file that writes a long
	// enough chain asks this walk for a machine stack as deep as the file is.
	// The same counter bounds it here, and a file that goes deeper is refused
	// rather than overrunning the stack.  Every other shape the walk recurses on
	// nests the parse as well, and so is already bounded where it is read.
	ParseDepth depth_;
};
