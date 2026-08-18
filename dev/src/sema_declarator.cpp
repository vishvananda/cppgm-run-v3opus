#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_access.h"
#include "sema_constexpr.h"
#include "sema_pack.h"

// Specifiers, declarators and names: what a declaration says the type of the
// thing it declares is, and what an identifier written in it denotes.

namespace
{

const AstNode* child_kind(const AstNode& node, AstKind kind)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->kind == kind)
		{
			return node.children[index];
		}
	}
	return nullptr;
}

// The declarator of a type-id or parameter-declaration, named or not.
const AstNode* declarator_of(const AstNode& node)
{
	const AstNode* found = child_kind(node, AstKind::Declarator);
	return found != nullptr ? found : child_kind(node, AstKind::AbstractDeclarator);
}

// 8.3p1: whether this declarator level leaves the constructor its declarator-id
// ends up under to the level around it.  A ptr-operator or a suffix of its own
// is what a level takes the id for itself with; parentheses alone are not - so
// `T (f)(P)` has its places spelled by the clause standing outside it, and
// `T (*f(P))(Q)` and `T (&f(P))[2]` by the clause their own level wrote.
bool takes_enclosing_suffix(const AstNode& declarator)
{
	std::size_t core = declarator.children.size();
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstKind kind = declarator.children[index]->kind;
		if (kind == AstKind::PtrOperator)
		{
			return false;
		}
		if (kind == AstKind::Identifier || kind == AstKind::NestedDeclarator)
		{
			core = index;
			break;
		}
	}
	if (core == declarator.children.size())
	{
		return false;
	}
	for (std::size_t index = core + 1; index < declarator.children.size();
	     ++index)
	{
		const AstKind kind = declarator.children[index]->kind;
		if (kind == AstKind::ParameterClause || kind == AstKind::ArraySuffix)
		{
			return false;
		}
	}
	const AstNode& node = *declarator.children[core];
	return node.kind != AstKind::NestedDeclarator ||
		(!node.children.empty() && takes_enclosing_suffix(*node.children[0]));
}

}

DeclSpecifiers::DeclSpecifiers()
	: builtins(0)
	, cv(kCvNone)
	, type_name(kNoType)
	, has_type_name(false)
	, is_typedef(false)
	, is_constexpr(false)
	, is_extern(false)
	, is_static(false)
	, is_inline(false)
	, is_virtual(false)
	, is_thread_local(false)
	, is_friend(false)
	, is_mutable(false)
	, is_auto(false)
	, alignment(0)
	, introduced(nullptr)
{
	for (std::size_t index = 0; index < kSimpleTypeSpecifierCount; ++index)
	{
		counted[index] = 0;
	}
}

SemaAnalyzer::Specifiers SemaAnalyzer::read_specifiers(const AstNode& seq,
                                                       const Context& ctx,
                                                       const Span& span,
                                                       bool declaration,
                                                       const std::string& named_by)
{
	Specifiers out;
	for (std::size_t index = 0; index < seq.children.size(); ++index)
	{
		read_type_specifier(*seq.children[index], out, ctx, span, named_by);
	}
	// 7.6.2p1: the strictest alignment the sequence asked for, which is a fact
	// about what the declaration declares.
	out.alignment = requested_alignment(seq, ctx);
	if (!declaration && out.is_typedef)
	{
		throw std::runtime_error("a type-specifier-seq holds a storage class");
	}
	return out;
}

void SemaAnalyzer::read_type_specifier(const AstNode& node, Specifiers& out,
                                       const Context& ctx, const Span& span,
                                       const std::string& named_by)
{
	if (node.kind == AstKind::AlignmentSpecifier)
	{
		// 7.6.2p1: an alignment-specifier names no type; what it asked for is
		// read from the sequence as a whole.
		return;
	}
	if (node.kind == AstKind::ClassSpecifier ||
	    node.kind == AstKind::ClassForwardDeclaration)
	{
		// 3.4.4p2: within a specifier sequence a class-key with a name and no
		// body is an elaborated-type-specifier, which finds a class that an
		// ordinary declaration of the same name hides, and declares one only
		// when the name reaches no class at all.
		SemaEntity* entity = nullptr;
		if (node.kind == AstKind::ClassForwardDeclaration)
		{
			SemaEntity* found = resolve(node.text, ctx, LookupKind::Type);
			entity = found != nullptr && found->kind == SemaKind::Class ? found
			                                                            : nullptr;
		}
		if (entity == nullptr)
		{
			// 11.3p11 and 3.3.2p6: a class an elaborated-type-specifier in a
			// friend declaration reaches nowhere is declared in the innermost
			// enclosing namespace, not as a member of the class the friend
			// declaration is written in.  3.3.2p6 again: one written in a
			// parameter-declaration-clause is declared in the smallest namespace
			// or block scope around the declaration and not in 3.3.7p1's region
			// the clause's own places stand in, so `void f(int, struct S *)`
			// declares the `S` a later declaration redeclares.
			Context where = ctx;
			if (where.scope != nullptr &&
			    (out.is_friend ||
			     where.scope->kind == ScopeKind::Prototype))
			{
				where.scope = out.is_friend ? &friend_namespace(*where.scope)
				                            : &declaring_region(*where.scope);
				where.dump = where.scope->dump;
			}
			entity = &class_declaration(node, where, span,
			                            node.kind == AstKind::ClassSpecifier,
			                            named_by);
		}
		out.introduced = entity;
		out.has_type_name = true;
		out.type_name = entity->type;
		return;
	}
	if (node.kind == AstKind::EnumSpecifier)
	{
		if (ctx.scope != nullptr && ctx.scope->kind == ScopeKind::Prototype)
		{
			// 3.3.2p6 again, over 7.2p3's elaborated form.
			Context where = ctx;
			where.scope = &declaring_region(*where.scope);
			where.dump = where.scope->dump;
			read_type_specifier(node, out, where, span, named_by);
			return;
		}
		// An enum-specifier with no enumerator-list inside a declaration is
		// the elaborated form: 7.2p3 makes it name an enumeration that exists.
		const bool elaborated = child_kind(node, AstKind::Enumerator) == nullptr;
		SemaEntity& entity = enum_declaration(node, ctx, elaborated, named_by);
		out.introduced = &entity;
		out.has_type_name = true;
		out.type_name = entity.type;
		return;
	}

	if (node.token == kNoAstToken)
	{
		if (!node.children.empty())
		{
			// 7.1.6.2p4: a decltype-specifier names the type of an expression,
			// and 7.1.6.2p1 lets one stand as the first component of a
			// nested-name-specifier - where the rest of the name is looked up
			// in the region that type names.
			out.has_type_name = true;
			out.type_name = QualifiedName(node.text).qualified()
				? decltype_qualified_type(node, ctx)
				: decltype_type(node, ctx);
			return;
		}
		if (node.text.empty())
		{
			return;
		}
		out.has_type_name = true;
		out.type_name = require(resolve(node.text, ctx, LookupKind::Type),
		                        node.text).type;
		return;
	}

	switch (node.token)
	{
	case TT_IDENTIFIER:
		out.has_type_name = true;
		out.type_name = require(resolve(node.text, ctx, LookupKind::Type),
		                        node.text).type;
		return;

	case KW_TYPEDEF:
		out.is_typedef = true;
		return;

	case KW_CONSTEXPR:
		out.is_constexpr = true;
		return;

	case KW_EXTERN:
		// 3.1p2: an `extern` declaration with no initializer declares the
		// object without defining it.
		out.is_extern = true;
		return;

	case KW_STATIC:
		// 9.4p1: a static member is a member of the class rather than of an
		// object of it, which is the one storage class a type depends on.
		out.is_static = true;
		return;

	case KW_INLINE:
		// 7.1.2p2: the definition may be written in more than one translation
		// unit, so the one the program has is not this unit's to own.
		out.is_inline = true;
		return;

	case KW_THREAD_LOCAL:
		// 3.7.2p1: the object the declaration declares belongs to a thread
		// rather than to the program, so every thread that names it names one
		// of its own.
		out.is_thread_local = true;
		return;

	case KW_VIRTUAL:
		// 10.3p1: a call of the member function this declares runs the final
		// overrider the object's own class has, so the class the declaration is
		// written in dispatches through a table rather than through the name.
		out.is_virtual = true;
		return;

	case KW_FRIEND:
		// 11.3p1: the declaration grants this class's access rather than
		// declaring a member of it, so what it declares belongs to the region
		// around the class.
		out.is_friend = true;
		return;

	case KW_MUTABLE:
		// 7.1.1p10: the specifier nullifies the const an object of the class
		// carries into the member, which is a fact about the member's own
		// declaration and not about the type it was declared with.
		out.is_mutable = true;
		return;

	case KW_AUTO:
		// 7.1.6.4p1: the specifier names no type.  8.3.5p2 gives the one it
		// stands for to a trailing-return-type, which the declarator writes
		// after its declarator-id and which the declarator walk reads.
		out.is_auto = true;
		return;

	case KW_CONST:
		out.cv |= kCvConst;
		return;

	case KW_VOLATILE:
		out.cv |= kCvVolatile;
		return;

	default:
		break;
	}

	const int builtin = builtin_specifier(node.token);
	if (builtin >= 0)
	{
		++out.counted[builtin];
		++out.builtins;
	}
	// 7.1.1 storage classes and 7.1.2 function specifiers change no type PA11
	// describes, so a sequence that holds one is read as if it did not.
}

TypeId SemaAnalyzer::specifier_type(const Specifiers& specifiers)
{
	if (specifiers.is_auto)
	{
		// 7.1.6.4p1 and 8.3.5p2: `auto` shall be the whole of the sequence, and
		// what it stands for is whatever the trailing-return-type after the
		// declarator-id writes - so the walk of the declarator is handed a type
		// that is no type at all, and 8.3.5p2 is what it has to become.
		if (specifiers.has_type_name || specifiers.builtins != 0 ||
		    specifiers.cv != kCvNone)
		{
			throw std::runtime_error("a declaration writes `auto` beside "
			                         "another type-specifier");
		}
		return kNoType;
	}
	if (specifiers.has_type_name)
	{
		return types_.qualified(specifiers.type_name, specifiers.cv);
	}
	if (specifiers.builtins == 0 || !table_10_names_a_type(specifiers.counted))
	{
		throw std::runtime_error("the specifiers of a declaration name no type");
	}
	return types_.qualified(types_.fundamental(table_10_type(specifiers.counted)),
	                        specifiers.cv);
}

TypeId SemaAnalyzer::type_id_type(const AstNode& node, const Context& ctx)
{
	const AstNode* seq = child_kind(node, AstKind::TypeSpecifierSeq);
	if (seq == nullptr)
	{
		throw std::runtime_error("a type-id names no type");
	}
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	Specifiers specifiers = read_specifiers(*seq, ctx, span, false, std::string());
	TypeId type = specifier_type(specifiers);
	const AstNode* declarator = declarator_of(node);
	if (declarator != nullptr)
	{
		std::string name;
		type = declarator_type(*declarator, type, ctx, &name);
	}
	return type;
}

const AstNode* SemaAnalyzer::declarator_id(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::Identifier)
		{
			return &child;
		}
		if (child.kind == AstKind::NestedDeclarator && !child.children.empty())
		{
			const AstNode* found = declarator_id(*child.children[0]);
			if (found != nullptr)
			{
				return found;
			}
		}
	}
	return nullptr;
}

// 5.1.1p3, declared in `sema_declaration.h` with the rest of what a
// decl-specifier-seq says.
bool declares_object_member(const DeclSpecifiers& specifiers)
{
	return !specifiers.is_static && !specifiers.is_friend &&
		!specifiers.is_typedef;
}

// 8.3: a declarator is read from the declarator-id outwards.  The pointer
// operators written before the id apply to what the specifiers named, the
// suffixes written after it apply to that from the last one inwards, and a
// nested declarator is then read against the type that comes out.
TypeId SemaAnalyzer::declarator_type(const AstNode& node, TypeId base,
                                     const Context& ctx, std::string* name,
                                     std::vector<Parameter>* declared,
                                     bool member_object)
{
	std::size_t index = 0;
	TypeId type = base;
	// 8.3.1p1: the cv-qualifier-seq of a ptr-operator is written after it, so a
	// declarator holds the two as siblings and `* const &` is a reference to a
	// const pointer rather than three unrelated parts.
	while (index < node.children.size() &&
	       (node.children[index]->kind == AstKind::PtrOperator ||
	        node.children[index]->kind == AstKind::CvQualifier))
	{
		if (type == kNoType)
		{
			// 8.3.5p2: the declarator-id of a declaration written `auto` is
			// reached through the parameter-clause the trailing-return-type
			// belongs to and through nothing else, so `auto *f() -> int`
			// derives a type from a type-specifier that names none.
			throw std::runtime_error("a declarator written `auto` derives a "
			                         "type 8.3.5p2 leaves unwritten");
		}
		const AstNode& part = *node.children[index];
		type = part.kind == AstKind::CvQualifier
			? types_.qualified(type, part.token == KW_CONST ? kCvConst
			                                               : kCvVolatile)
			: apply_pointer(part, type, ctx);
		++index;
	}

	if (index < node.children.size() &&
	    node.children[index]->kind == AstKind::ParameterPack)
	{
		// 8.3.5p3 and 14.5.3p4: the `...` a declarator writes before its
		// declarator-id says what the place is and derives no type of its own,
		// so what names the place is the component after it.
		++index;
	}

	const AstNode* core = nullptr;
	if (index < node.children.size() &&
	    (node.children[index]->kind == AstKind::Identifier ||
	     node.children[index]->kind == AstKind::NestedDeclarator))
	{
		core = node.children[index];
		++index;
	}

	// 8.3p1: the places a *definition* spells are the ones of the
	// parameter-clause its declarator-id ends up under, which is the first
	// suffix at the id's own level - so a level whose nested declarator takes
	// the id for itself hands `declared` inward rather than spending it on the
	// clause standing beside it.  `T (*f(P))(Q)` is the function `(P)` makes
	// and binds `P`'s names; `(Q)`'s belong to the type it returns and to
	// nothing the body can name.
	//
	// Only 8.4p1's function definition has places to spell, and the level that
	// spells them takes `declared` away from the ones inside it - so the walk
	// is asked once per definition and not once per declarator level.
	const bool spells_here =
		declared == nullptr || core == nullptr ||
		core->kind != AstKind::NestedDeclarator || core->children.empty() ||
		takes_enclosing_suffix(*core->children[0]);

	// 8.3.5p2 and 3.3.7p1: a trailing-return-type is part of the function
	// declarator whose parameter-clause it follows, and the places that clause
	// wrote stand to the end of that declarator - so the clause is read for the
	// names it declares *before* the type is read, however the walk from the
	// last suffix inwards would reach the two.  What comes out is kept beside
	// it: the type is still built from the last suffix inwards, and the clause
	// is read once.
	std::vector<Parameter> read;
	std::vector<Parameter>* ahead = nullptr;
	bool ahead_variadic = false;
	std::size_t ahead_at = node.children.size();
	Context reading = ctx;
	for (std::size_t suffix = node.children.size(); suffix-- > index;)
	{
		const AstKind kind = node.children[suffix]->kind;
		if (kind == AstKind::TrailingReturnType)
		{
			ahead = spells_here && declared != nullptr ? declared : &read;
			continue;
		}
		if (ahead != nullptr && kind == AstKind::ParameterClause)
		{
			ahead_at = suffix;
			read_parameters(*node.children[suffix], ctx, *ahead, ahead_variadic,
			                &reading);
			break;
		}
	}

	// 8.3.5p7 and 8.3.5p1: the cv-qualifier-seq and the ref-qualifier of a
	// function declarator are written after its parameter-clause, so the walk
	// from the last suffix inwards reaches them before the clause they qualify
	// and hands both on.
	unsigned function_cv = kCvNone;
	RefQualifier function_ref = RefQualifier::None;
	for (std::size_t suffix = node.children.size(); suffix-- > index;)
	{
		const AstNode& part = *node.children[suffix];
		if (part.kind == AstKind::CvQualifier)
		{
			function_cv |= part.token == KW_CONST ? kCvConst : kCvVolatile;
			continue;
		}
		if (part.kind == AstKind::FunctionQualifier &&
		    (part.text == "&" || part.text == "&&"))
		{
			// The grammar spells an exception-specification with the same node,
			// so which one this is is what it was written as.
			function_ref = part.text == "&" ? RefQualifier::LValue
			                                : RefQualifier::RValue;
			continue;
		}
		if (part.kind == AstKind::TrailingReturnType)
		{
			// 8.3.5p2: the type the function returns is the one written after
			// the declarator-id rather than the one the decl-specifier-seq
			// named, and 3.4.1p8 has already put `ctx` on the region that
			// declarator-id reaches - which is what lets an out-of-class
			// definition name a type its own class declares.  3.3.7p1 puts the
			// clause's own places over it.
			if (type != kNoType)
			{
				throw std::runtime_error(
					"a declarator writes a trailing-return-type where its "
					"specifiers already name a type, which 8.3.5p2 requires to "
					"be `auto`");
			}
			// 5.1.1p3: `this` stands from the cv-qualifier-seq of a member
			// function's declarator to the end of that declarator, so the type
			// is read the way its body will be read - and the reading around
			// this one is put aside, because a declarator may be read while
			// another is.  Which declarators those are is the
			// decl-specifier-seq beside them and no part of the declarator, so
			// the caller says: a static member, 11.3p1's friend and a typedef
			// each write a declarator in a class and declare no member function
			// of it, and every other declarator is read where `this` is nothing
			// at all - so the reading put aside is the one thing all of them
			// need.
			const FunctionReading object(
				*this,
				member_object ? declarator_object(node, index, ctx) : nullptr,
				kNoType);
			type = type_id_type(*part.children[0], reading);
			continue;
		}
		if (type == kNoType)
		{
			throw std::runtime_error("a declarator written `auto` derives a "
			                         "type 8.3.5p2 leaves unwritten");
		}
		type = suffix == ahead_at
			? types_.function_of(type, parameter_types(*ahead), ahead_variadic)
			: apply_suffix(part, type, ctx, spells_here ? declared : nullptr);
		if (part.kind == AstKind::ParameterClause)
		{
			// 8.3.5p1 and 8.3.5p7: both qualifiers are part of the function
			// type the declarator wrote, so both are written onto it here -
			// which is what a typedef, a pointer to function and the function
			// type a pointer to member points to each go on holding, and what
			// the description of any of them spells.  9.3.1p3's lowering is
			// what moves the cv-qualifier-seq onto the object parameter; the
			// ref-qualifier stays on the type, because 13.1 has `f() &` and
			// `f() &&` to tell apart.
			type = types_.ref_qualified_function(
				types_.qualified_function(type, function_cv), function_ref);
			function_cv = kCvNone;
			function_ref = RefQualifier::None;
			if (spells_here)
			{
				declared = nullptr;
			}
		}
	}

	if (type == kNoType)
	{
		// 7.1.6.4: the placeholder is still one, so the declaration wrote
		// `auto` and no trailing-return-type - which is the deduced form this
		// milestone does not have.
		throw std::runtime_error("a declaration written `auto` writes no "
		                         "trailing-return-type");
	}
	if (core == nullptr)
	{
		return type;
	}
	if (core->kind == AstKind::Identifier)
	{
		*name = core->text;
		return type;
	}
	// 8.3p1: the nested declarator is read against the type this level built,
	// and it carries the places to spell wherever it is the level the
	// declarator-id ends up at - which `spells_here` has already answered.
	return core->children.empty()
		? type
		: declarator_type(*core->children[0], type, ctx, name, declared,
		                  member_object);
}

TypeId SemaAnalyzer::apply_pointer(const AstNode& node, TypeId type,
                                   const Context& ctx)
{
	unsigned cv = kCvNone;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::CvQualifier)
		{
			cv |= child.token == KW_CONST ? kCvConst : kCvVolatile;
		}
	}
	if (node.token == kNoAstToken)
	{
		// 8.3.3p1: a `nested-name-specifier *` declares a pointer to a member
		// of the class that name reaches.  PA10 hands the operator on as it was
		// written, so the class is what stands before the `*`, and one lookup
		// where the operator is read is what it takes.  The class need not be
		// complete: 8.3.3p3 asks only that the name reach one.
		std::string named = node.text.substr(0, node.text.size() - 1);
		if (named.size() >= 2 && named.compare(named.size() - 2, 2, "::") == 0)
		{
			named.erase(named.size() - 2);
		}
		const SemaEntity& owner =
			require(resolve(named, ctx, LookupKind::Type), named);
		if (!types_.is_class(owner.type))
		{
			throw std::runtime_error(named + " is written before `::*` and does "
			                         "not name a class");
		}
		return types_.qualified(types_.member_pointer_to(owner.type, type), cv);
	}
	if (node.token == OP_AMP || node.token == OP_LAND)
	{
		// 8.3.2p5: there are no cv-qualified references, and 8.3.2p6 collapses
		// a reference to a reference to one reference.
		return types_.reference_to(type, node.token == OP_LAND);
	}
	if (types_.is_reference(type))
	{
		// 8.3.2p4: there are no pointers to references.
		throw std::runtime_error("a declarator declares a pointer to a reference");
	}
	return types_.qualified(types_.pointer_to(type), cv);
}

TypeId SemaAnalyzer::apply_suffix(const AstNode& node, TypeId type,
                                  const Context& ctx,
                                  std::vector<Parameter>* declared)
{
	if (node.kind == AstKind::ArraySuffix)
	{
		if (node.children.empty())
		{
			return types_.array_of(type, false, 0);
		}
		TypeId place = kNoType;
		const unsigned long long bound = ConstexprReading(*this).array_bound(
			*node.children[0], ctx, place);
		return types_.array_of(type, true, bound, place);
	}
	if (node.kind == AstKind::ParameterClause)
	{
		std::vector<Parameter> read;
		std::vector<Parameter>& parameters = declared != nullptr ? *declared : read;
		bool variadic = false;
		read_parameters(node, ctx, parameters, variadic, nullptr);
		return types_.function_of(type, parameter_types(parameters), variadic);
	}
	// A cv-qualifier, ref-qualifier, exception-specification or virt-specifier
	// on a declarator changes no type PA11 describes.
	return type;
}

// 5.1.1p3: the object a declarator's own trailing-return-type names.
//
// The declaration has not been made yet and 9.3.1p3's object parameter is built
// out of the type this walk is still building, so what stands for the object
// here is made from the two facts the declarator already has: the class its
// declarator-id reaches, and the cv-qualifier-seq written after its
// parameter-clause, which the walk from the last suffix inwards has not reached
// either.  Null where the declarator-id reaches no class, which is every
// declaration but a member's.
SemaEntity* SemaAnalyzer::declarator_object(const AstNode& node,
                                            std::size_t suffixes,
                                            const Context& ctx)
{
	if (ctx.scope == nullptr || ctx.scope->kind != ScopeKind::Class ||
	    ctx.scope->owner == nullptr)
	{
		return nullptr;
	}
	unsigned cv = kCvNone;
	for (std::size_t index = suffixes; index < node.children.size(); ++index)
	{
		const AstNode& part = *node.children[index];
		if (part.kind == AstKind::CvQualifier)
		{
			cv |= part.token == KW_CONST ? kCvConst : kCvVolatile;
		}
	}
	return &model_.create(
		SemaKind::Parameter, "this",
		types_.pointer_to(types_.qualified(ctx.scope->owner->type, cv)));
}

// 8.3.5p5: an array or function parameter contributes a pointer, and top level
// cv-qualification is dropped, so two declarations that differ only in those
// declare one function.  PA11 describes the declarator as written, so only PA12
// asks for it.
std::vector<TypeId> SemaAnalyzer::parameter_types(
	const std::vector<Parameter>& parameters)
{
	std::vector<TypeId> types;
	types.reserve(parameters.size());
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		if (types_.is_settled_run(parameters[index].type))
		{
			// 14.5.3p4: a pack whose run holds no elements declared no place,
			// so the function takes no argument for it.  An expansion the
			// reading has not settled is one place and stays one.
			continue;
		}
		types.push_back(semantics()
			? types_.adjust_parameter(parameters[index].type)
			: parameters[index].type);
	}
	return types;
}

// 3.3.7p1: the region a place stands in from the declarator-id that named it to
// the end of the function declarator, opened where a name could reach it and
// nowhere else.  The last place of a clause nothing follows has nothing left to
// name it, so an ordinary one-parameter declaration costs one comparison; a
// clause that binds is one region and one declaration per named place, dropped
// with the declarator because 8.3.5p10 leaves the name out of the function's
// type and 8.4.1p1's definition declares its own objects again.
void SemaAnalyzer::bind_place(Context& reading, const Context& ctx,
                              const Parameter& parameter)
{
	if (reading.scope == ctx.scope)
	{
		reading.scope = &model_.open(ScopeKind::Prototype, *ctx.scope, nullptr,
		                             ctx.dump);
	}
	// 8.3.5p5 again: the object a name in the declarator reaches is the pointer
	// the clause made of an array or a function, which is what `decltype` over
	// it names.
	SemaEntity& place = model_.create(
		SemaKind::Parameter, parameter.name,
		semantics() ? types_.parameter_object(parameter.type) : parameter.type);
	model_.bind(*reading.scope, parameter.name, place);
	model_.declare_in(*reading.scope, place);
}

// 14.5.3p4: the pack a clause declared where its run holds no elements, bound
// in the same region a place would have been - so what the rest of the
// declarator writes for it reaches the run and not nothing.
void SemaAnalyzer::bind_pack(Context& reading, const Context& ctx,
                             const Parameter& parameter)
{
	if (reading.scope == ctx.scope)
	{
		reading.scope = &model_.open(ScopeKind::Prototype, *ctx.scope, nullptr,
		                             ctx.dump);
	}
	SemaEntity& pack = model_.create(SemaKind::Typedef, parameter.name,
	                                 parameter.type);
	model_.bind(*reading.scope, parameter.name, pack);
}

// 8.2p7: `T (X)` written as a parameter-declaration is a parameter of function
// type where `X` names a type and a parameter named `X` in redundant
// parentheses where it does not.
//
// The grammar cannot tell the two apart, so the parse writes the
// parameter-clause both spellings share and the resolution is 3.4's: what `X`
// was declared as is what says which of the two was written.  Nothing is
// reparsed - the declarator-id and whatever the parentheses wrote after it are
// already nodes, and the clause is read back as the declarator they make.
bool SemaAnalyzer::parenthesized_place(const AstNode& declarator,
                                       const Context& ctx, std::string& name,
                                       const AstNode*& rest)
{
	if (declarator.children.size() != 1 ||
	    declarator.children[0]->kind != AstKind::ParameterClause)
	{
		return false;
	}
	const AstNode& clause = *declarator.children[0];
	if (clause.children.size() != 1 ||
	    clause.children[0]->kind != AstKind::ParameterDeclaration)
	{
		// 8.3.5p3's ellipsis and a second parameter both leave a clause no
		// declarator-id could have been written as.
		return false;
	}
	const AstNode& inner = *clause.children[0];
	const AstNode* const seq = child_kind(inner, AstKind::DeclSpecifierSeq);
	if (seq == nullptr || seq->children.size() != 1 ||
	    seq->children[0]->kind != AstKind::DeclSpecifier ||
	    seq->children[0]->token != TT_IDENTIFIER ||
	    !seq->children[0]->children.empty() ||
	    child_kind(inner, AstKind::DefaultArgument) != nullptr)
	{
		return false;
	}
	// 8.3.5p10 and 3.4.3p1: a place is named by an identifier and by nothing
	// else, so a spelling no unqualified name has was written as a type.
	const std::string& written = seq->children[0]->text;
	if (written.empty() || QualifiedName(written).qualified())
	{
		return false;
	}
	const SemaEntity* const found = resolve(written, ctx, LookupKind::Any);
	if (found != nullptr && names_a_type(*found))
	{
		return false;
	}
	name = written;
	// 8.3p1: what the parentheses wrote after the declarator-id belongs to the
	// place, so `T (X[10])` declares the array `X` and not a function.
	rest = declarator_of(inner);
	return true;
}

void SemaAnalyzer::read_parameters(const AstNode& clause, const Context& ctx,
                                   std::vector<Parameter>& out, bool& variadic,
                                   Context* reading)
{
	// 3.3.7p1: the places are declared as they are read, so what the rest of
	// the declarator writes names them.  `inner` is the region the next of them
	// is read against, which is `ctx`'s until one of them binds a name.
	Context inner = ctx;
	std::size_t places = 0;
	for (std::size_t index = 0; index < clause.children.size(); ++index)
	{
		places += clause.children[index]->kind == AstKind::ParameterDeclaration
			? 1u : 0u;
	}
	std::size_t at = 0;
	for (std::size_t index = 0; index < clause.children.size(); ++index)
	{
		const AstNode& child = *clause.children[index];
		if (child.kind == AstKind::ParameterPack || child.kind == AstKind::Ellipsis)
		{
			variadic = true;
			continue;
		}
		if (child.kind != AstKind::ParameterDeclaration)
		{
			continue;
		}
		++at;
		const AstNode* seq = child_kind(child, AstKind::DeclSpecifierSeq);
		if (seq == nullptr)
		{
			seq = child_kind(child, AstKind::TypeSpecifierSeq);
		}
		if (seq == nullptr)
		{
			throw std::runtime_error("a parameter declares no type");
		}
		Span span;
		span.begin = child.begin;
		span.end = child.end;
		const AstNode* declarator = declarator_of(child);
		PackReading packs(*this);
		PackReading::Run over;
		if (declarator != nullptr &&
		    child_kind(*declarator, AstKind::ParameterPack) != nullptr)
		{
			// 14.5.3p4: the clause wrote a *pattern* and a `...`, so the packs
			// it is over are the ones its decl-specifier-seq and its declarator
			// name - the same question the spelling reading and the tree
			// reading ask, asked of the two trees a parameter-declaration is.
			packs.note_node(*seq, inner, over);
			packs.note_node(*declarator, inner, over);
		}
		if (over.found && over.settled)
		{
			packs.read_places(child, *seq, *declarator, over, inner, ctx,
			                  at < places || reading != nullptr, out);
			continue;
		}
		Specifiers specifiers =
			read_specifiers(*seq, inner, span, true, std::string());
		Parameter parameter;
		parameter.type = specifier_type(specifiers);
		const AstNode* parenthesized = nullptr;
		if (declarator != nullptr &&
		    parenthesized_place(*declarator, inner, parameter.name,
		                        parenthesized))
		{
			// 8.2p7: the parentheses stand around this place's own name, so
			// what they hold after it is the rest of its declarator.
			if (parenthesized != nullptr)
			{
				std::string ignored;
				parameter.type = declarator_type(*parenthesized, parameter.type,
				                                 inner, &ignored);
			}
		}
		else if (declarator != nullptr)
		{
			const bool dots =
				child_kind(*declarator, AstKind::ParameterPack) != nullptr;
			parameter.type = declarator_type(*declarator, parameter.type, inner,
			                                 &parameter.name);
			std::vector<TypeId> run;
			if (dots && PackReading(*this).expand_type(parameter.type, run))
			{
				// 14.5.3p4: the clause wrote one place standing for however many
				// the run holds, and 8.3.5p10 names each of them after the pack.
				// The first keeps the pack's own name, so a use of it reaches
				// the first place and the run is read off that declaration.
				// 14.6.2p1: a run the reading has not settled is one entry that
				// stands for itself, so the place it declares is one place and
				// `sizeof...` over it is a value only the instantiation knows.
				const bool settled = !(run.size() == 1 &&
				                       types_.is_pack_expansion(run[0]));
				if (settled && run.empty() && !parameter.name.empty())
				{
					// 14.5.3p4 over a run of none: the clause declared no place
					// at all, and the pack is still declared - so what its name
					// names is the run itself, which `sizeof...` reads as zero
					// and an expansion of it comes to no arguments.  It is no
					// place, so no function type holds it and no object is laid
					// out for it.
					Parameter none = parameter;
					none.type = types_.pack_type(std::vector<TypeId>());
					none.initializer = nullptr;
					if (at < places || reading != nullptr)
					{
						bind_pack(inner, ctx, none);
					}
					out.push_back(none);
					continue;
				}
				for (std::size_t element = 0; element < run.size(); ++element)
				{
					Parameter one = parameter;
					one.type = run[element];
					one.name = pack_element_name(parameter.name, element);
					one.pack_run = settled && element == 0
						? static_cast<unsigned>(run.size())
						: 0u;
					if (!one.name.empty() && (at < places || reading != nullptr))
					{
						bind_place(inner, ctx, one);
					}
					out.push_back(one);
				}
				continue;
			}
			// 8.3.5p3: `f(int...)` writes the ellipsis without a comma, so it
			// arrives inside the last parameter's declarator rather than beside
			// it, and it still says the function takes further arguments.
			variadic = variadic || dots;
		}
		parameter.initializer = child_kind(child, AstKind::DefaultArgument);
		if (!parameter.name.empty() && (at < places || reading != nullptr))
		{
			bind_place(inner, ctx, parameter);
		}
		out.push_back(parameter);
	}
	if (reading != nullptr)
	{
		*reading = inner;
	}
	// 8.3.5p4: a parameter list of one unnamed `void` parameter is an empty
	// parameter list, and the function takes no arguments.
	if (out.size() == 1 && out[0].name.empty() && types_.is_plain_void(out[0].type))
	{
		out.clear();
	}
}

SemaEntity& SemaAnalyzer::require(SemaEntity* entity, const std::string& name)
{
	if (entity == nullptr)
	{
		throw std::runtime_error("no declaration of " + name + " is in scope");
	}
	return *entity;
}

SemaEntity* SemaAnalyzer::resolve(const std::string& spelling, const Context& ctx,
                                  LookupKind filter,
                                  std::vector<SemaEntity*>* found,
                                  Scope** naming_region)
{
	if (spelling.empty())
	{
		return nullptr;
	}
	// 3.7.4p2: `operator new` and `operator delete` are the only names an
	// operator-function-id needs a space in, so a use written as an
	// id-expression carries the space the tokens had and the declaration does
	// not.  One is the name of the other.
	const std::string name = allocation_function_spelling(spelling);
	const QualifiedName written(name);
	if (!written.qualified())
	{
		// 14.2p4: `a.template f<A>` reaches this walk as one component, and the
		// keyword it wrote is no part of the name a lookup asks for.
		const std::string alone = written.last();
		SemaEntity* const here = model_.lookup(*ctx.scope, alone, filter, found);
		// 14.2: a name no declaration bound may still be a template-id, which
		// names the specialization its arguments make of the template the
		// program did declare.
		return here != nullptr ? here
		                       : template_id_entity(alone, ctx, nullptr, filter);
	}
	// 14.6.2p1: a name written after a prefix that depends on a template
	// parameter is a member of a class no argument list has named yet.  While
	// the template that writes it is being read there is nothing to look it up
	// in, so what the name stands for is the one thing the reading knows about
	// it: that it is a type, and the same one wherever this definition writes
	// this spelling.
	TypeId dependent = kNoType;
	std::size_t unresolved = 0;
	Scope* const region = resolve_prefix(written, ctx, nullptr,
	                                     templating() ? &dependent : nullptr,
	                                     &unresolved);
	if (region == nullptr)
	{
		// 14.6.2p1: every component from the one the prefix stopped at stands
		// after a type that depends on a parameter, so each is a member of the
		// one before it and the last of them is what the name stands for.
		SemaEntity* member = nullptr;
		for (std::size_t index = unresolved; index < written.size(); ++index)
		{
			member = &dependent_member_name(dependent, written.part(index));
			dependent = member->type;
		}
		return member;
	}
	if (naming_region != nullptr)
	{
		// 11.2p5: the region the prefix reached is the naming class of whatever
		// the lookup below finds in it, which is a fact about the *name* and
		// not about the declaration it reaches - a member declared in a base
		// is named through this class however far up it was declared.
		*naming_region = region;
	}
	SemaEntity* named =
		model_.lookup_in(*region, written.last(), filter, found);
	if (named == nullptr)
	{
		named = template_id_entity(written.last(), ctx, region, filter);
	}
	if (named == nullptr)
	{
		// 14.6.2.1p6: the prefix named a class whose definition this reading
		// *has* - the current instantiation, or any class an argument list has
		// still to settle a base of - and the lookup in it found nothing.  What
		// the name stands for is then a member of a class no list has named
		// yet: 14.6.2p3 leaves such a base off the chain 3.4.3 searches here,
		// because which class it is only an argument list says, so the member
		// is the same stand-in a prefix that named no region at all leaves.
		SemaEntity* const unknown =
			member_of_unknown_specialization(*region, written.last());
		if (unknown != nullptr)
		{
			return unknown;
		}
	}
	if (named != nullptr)
	{
		// 11.2: a qualified name reaches a member of the class it names, which
		// is where the access that class gave the member is asked about.
		// 11.2p5: that class is the naming class as much as the one an object
		// expression writes - the member is reached through the bases between
		// it and the class that declared the member, and a base that grants the
		// access is what a member declared further up is reached through.
		Access(*this).require_access(
			*named, ctx.scope,
			region->kind == ScopeKind::Class ? region : nullptr);
	}
	return named;
}

// 3.4.3: each component of a nested-name-specifier is looked up in the region
// the one before it named, the first in the scopes around the declaration.
Scope* SemaAnalyzer::resolve_prefix(const QualifiedName& name,
                                    const Context& ctx, Scope* first_region,
                                    TypeId* dependent, std::size_t* unresolved)
{
	// 3.4.3p1: a name written `::x` is looked up in the global namespace.
	const std::string first = name.part(0);
	Scope* region = first_region;
	// 14.6.2p1: the component that named no region, whose type is what the
	// name written after it depends on.
	SemaEntity* named = nullptr;
	if (region == nullptr && first.empty())
	{
		region = &model_.global();
	}
	// 7.1.6.2p1: a nested-name-specifier may begin with a decltype-specifier,
	// which no lookup of its spelling can answer - what says which region it
	// names is the tree the parse read for its operand, exactly as it does
	// where the same specifier stands among a declaration's type specifiers.
	if (region == nullptr && written_ != nullptr &&
	    first.compare(0, 9, "decltype(") == 0)
	{
		const AstNode* const held = written_->spelled(first);
		if (held != nullptr)
		{
			const TypeId type = types_.strip_cv(decltype_type(*held, ctx));
			if (dependent != nullptr && types_.is_dependent(type))
			{
				// 14.6.2p1: which class this specifier names, an argument list
				// is what says, so every component behind it is a member of a
				// class no list has named yet - the same answer this walk
				// gives a prefix written as a name.
				*dependent = type;
				if (unresolved != nullptr)
				{
					*unresolved = 1;
				}
				return nullptr;
			}
			require_settled_type(type);
			named = model_.type_owner(type);
			region = named == nullptr ? nullptr : model_.region_of(*named);
		}
	}
	if (region == nullptr)
	{
		// 14.2: a component of a nested-name-specifier is a template-id where
		// the region it names is a specialization, and the lookup that finds
		// the template is the one 3.4.3p1 makes.
		SemaEntity* head = model_.lookup(*ctx.scope, first, LookupKind::Region);
		if (head == nullptr)
		{
			head = template_id_entity(first, ctx, nullptr, LookupKind::Region);
		}
		named = &require(head, first);
		// 3.4.3p1 and 14.7.1p1: a name is looked up *in* the region this
		// component reached, which for a class template specialization is a
		// context requiring it to be completely defined.
		require_settled_type(named->type);
		region = model_.region_of(*named);
	}

	for (std::size_t index = 1; index + 1 < name.size(); ++index)
	{
		const std::string part = name.part(index);
		if (region == nullptr)
		{
			if (dependent != nullptr && named != nullptr &&
			    types_.is_dependent(named->type))
			{
				*dependent = named->type;
				if (unresolved != nullptr)
				{
					*unresolved = index;
				}
				return nullptr;
			}
			throw std::runtime_error(name.part(index - 1) + " names no region");
		}
		SemaEntity* next = model_.lookup_in(*region, part, LookupKind::Region);
		if (next == nullptr)
		{
			// 14.2: a component that is a template-id names the specialization
			// its arguments make, and 11.2 is asked about the *template* the
			// lookup found rather than about the class the arguments made - the
			// access-specifier the program wrote stands over the declaration.
			Access(*this).require_component_access(part, ctx, *region);
			next = template_id_entity(part, ctx, region, LookupKind::Region);
			if (next == nullptr && dependent != nullptr &&
			    member_of_unknown_specialization(*region, part) != nullptr)
			{
				// 14.6.2.1p6 at a component of the prefix rather than at the
				// name it stands before: the class the component was looked up
				// in has a base an argument list has still to settle, so this
				// component and every one after it is a member of a class no
				// list has named yet.  The walk stops here and leaves them to
				// the same stand-in a prefix that named no region leaves.
				*dependent = region->owner->type;
				if (unresolved != nullptr)
				{
					*unresolved = index;
				}
				return nullptr;
			}
		}
		else
		{
			// 11.2: every component of a nested-name-specifier is a name written
			// on the region the one before it named, so each is asked the
			// question the last component is asked.  A path whose leading
			// components are private is one no context outside them may write,
			// however public the member it ends at.
			Access(*this).require_access(
				*next, ctx.scope,
				region->kind == ScopeKind::Class ? region : nullptr);
		}
		named = &require(next, part);
		require_settled_type(named->type);
		region = model_.region_of(*named);
	}
	if (region == nullptr)
	{
		if (dependent != nullptr && named != nullptr &&
		    types_.is_dependent(named->type))
		{
			*dependent = named->type;
			if (unresolved != nullptr)
			{
				*unresolved = name.size() - 1;
			}
			return nullptr;
		}
		throw std::runtime_error(name.last() + " is written after a name that is "
		                                       "not a namespace, class or "
		                                       "enumeration");
	}
	return region;
}

// 14.6.2p1: what a name written after a prefix that depends on a template
// parameter stands for while the template writing it is read.
//
// The member itself is a member of a class an argument list has yet to name, so
// there is nothing to look up: what the reading knows is that the name is used
// as a type and that the same name behind the same prefix is the same type
// wherever this definition writes it.  So one declaration is made per prefix
// and component, over a type standing for itself - which is dependent, so every
// type built from it is dependent too and nothing an instantiation reads is
// built from this one.
//
// 14.2 and the ABI's `<unresolved-name>`: the object file writes such a type as
// the prefix and then the name, so the type is given the two as the facts they
// are rather than as the one spelling it is diagnosed by - which is what makes
// `typename T::car_type` the `NT_8car_typeE` every other compiler writes rather
// than the `T_` a parameter standing alone would be.
SemaEntity& SemaAnalyzer::dependent_member_name(TypeId prefix,
                                                const std::string& component)
{
	const std::string key = std::to_string(prefix) + "|" + component;
	const std::unordered_map<std::string, SemaEntity*>::const_iterator held =
		dependent_names_.find(key);
	if (held != dependent_names_.end())
	{
		return *held->second;
	}
	const std::string spelling =
		types_.user_name(prefix) + "::" + component;
	const TypeId type = types_.template_parameter_type(model_.type_entity_id(),
	                                                   false, spelling);
	types_.set_dependent_member(type, prefix, component);
	SemaEntity& entity = model_.create(SemaKind::Typedef, component, type);
	own_type(type, entity);
	dependent_names_.insert(std::make_pair(key, &entity));
	return entity;
}

// 14.6.2.1p6: a qualified name whose prefix named a class this reading does
// have a definition of, and which that definition does not declare.
//
// The definition is not the whole class: 14.6.2p3 leaves a base-specifier whose
// type depends on a template parameter off the chain 10.2 searches, because
// which class that base is only an argument list says.  So a name the lookup in
// such a class does not find is not a name the program failed to declare - it
// is a member of a class no argument list has named yet, exactly as a name
// written after a prefix that named no region at all is, and it stands in the
// same way until the substitution looks it up in the class the arguments made.
//
// A class with no such base answers for itself, and so does every region that
// is not a class, so both give null here and the lookup's own refusal stands.
SemaEntity* SemaAnalyzer::member_of_unknown_specialization(
	const Scope& region, const std::string& component)
{
	if (!templating() || region.kind != ScopeKind::Class ||
	    !region.dependent_base || region.owner == nullptr ||
	    !types_.is_dependent(region.owner->type))
	{
		return nullptr;
	}
	return &dependent_member_name(region.owner->type, component);
}

// 7.1.6.2p1: a nested-name-specifier whose first component is a
// decltype-specifier.  The spelling cannot say what region that component
// names, so the expression the parser kept beside the name is what answers it;
// every component after it is looked up the way 3.4.3 looks one up behind any
// other prefix.
SemaEntity* SemaAnalyzer::decltype_qualified_name(
	const AstNode& node, const Context& ctx, LookupKind filter,
	std::vector<SemaEntity*>* found)
{
	return qualified_in_type(types_.strip_cv(decltype_type(node, ctx)),
	                         QualifiedName(node.text), ctx, filter, found);
}

// 3.4.3: the components a name writes after a prefix that named a type rather
// than a region the spelling can be looked up in, which 7.1.6.2p1's
// decltype-specifier is the one form of.
SemaEntity* SemaAnalyzer::qualified_in_type(TypeId head,
                                            const QualifiedName& written,
                                            const Context& ctx,
                                            LookupKind filter,
                                            std::vector<SemaEntity*>* found)
{
	if (templating() && types_.is_dependent(head))
	{
		// 14.6.2p1: a decltype-specifier an argument list has yet to settle
		// names no region either, so every component written after it is a
		// member of the one before it - the same stand-in `resolve` leaves a
		// prefix written as a name with, and the same one the substitution
		// settles.
		SemaEntity* member = nullptr;
		TypeId prefix = head;
		for (std::size_t index = 1; index < written.size(); ++index)
		{
			member = &dependent_member_name(prefix, written.part(index));
			prefix = member->type;
		}
		return member;
	}
	// 3.4.3p1 and 14.7.1p1: the name is looked up *in* the region this prefix
	// named, which for a class template specialization an argument list has
	// already settled is a context requiring it to be completely defined - the
	// same demand `resolve_prefix` makes of a prefix written as a name, and the
	// one 14.6p8's reading has to answer in earnest rather than with nothing.
	require_settled_type(head);
	SemaEntity* const owner = model_.type_owner(head);
	Scope* const region = owner == nullptr ? nullptr : model_.region_of(*owner);
	if (region == nullptr)
	{
		throw std::runtime_error("a decltype-specifier written before `::` "
		                         "names no class or enumeration");
	}
	Scope* const naming = resolve_prefix(written, ctx, region);
	SemaEntity* const named =
		model_.lookup_in(*naming, written.last(), filter, found);
	if (named == nullptr)
	{
		// 14.6.2.1p6: the same answer a prefix written as a name gets - the
		// class this one named has a base an argument list has still to settle,
		// so the name is a member of a class no list has named yet.
		SemaEntity* const unknown =
			member_of_unknown_specialization(*naming, written.last());
		if (unknown != nullptr)
		{
			return unknown;
		}
	}
	if (named != nullptr)
	{
		// 11.2 and 11.2p5: the name reaches a member of the class the prefix
		// named, which is where the access that class gave the member is asked
		// about and which is the naming class the bases between are read from.
		Access(*this).require_access(
			*named, ctx.scope,
			naming->kind == ScopeKind::Class ? naming : nullptr);
	}
	return named;
}

TypeId SemaAnalyzer::decltype_qualified_type(const AstNode& node,
                                             const Context& ctx)
{
	const QualifiedName written(node.text);
	return require(decltype_qualified_name(node, ctx, LookupKind::Type),
	               written.last()).type;
}
