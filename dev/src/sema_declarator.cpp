#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

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

}

SemaAnalyzer::Specifiers::Specifiers()
	: builtins(0)
	, cv(kCvNone)
	, type_name(kNoType)
	, has_type_name(false)
	, is_typedef(false)
	, is_constexpr(false)
	, is_extern(false)
	, is_static(false)
	, is_inline(false)
	, is_friend(false)
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
			// declaration is written in.
			Context where = ctx;
			if (out.is_friend && where.scope != nullptr)
			{
				where.scope = &friend_namespace(*where.scope);
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
			// 7.1.6.2p4: a decltype-specifier names the type of an expression.
			out.has_type_name = true;
			out.type_name = decltype_type(node, ctx);
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

	case KW_FRIEND:
		// 11.3p1: the declaration grants this class's access rather than
		// declaring a member of it, so what it declares belongs to the region
		// around the class.
		out.is_friend = true;
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

// 8.3: a declarator is read from the declarator-id outwards.  The pointer
// operators written before the id apply to what the specifiers named, the
// suffixes written after it apply to that from the last one inwards, and a
// nested declarator is then read against the type that comes out.
TypeId SemaAnalyzer::declarator_type(const AstNode& node, TypeId base,
                                     const Context& ctx, std::string* name,
                                     std::vector<Parameter>* declared)
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
		const AstNode& part = *node.children[index];
		type = part.kind == AstKind::CvQualifier
			? types_.qualified(type, part.token == KW_CONST ? kCvConst
			                                               : kCvVolatile)
			: apply_pointer(part, type, ctx);
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

	// 8.3.5p7: the cv-qualifier-seq of a function declarator is written after
	// its parameter-clause, so the walk from the last suffix inwards reaches it
	// before the clause it qualifies and hands it on.
	unsigned function_cv = kCvNone;
	for (std::size_t suffix = node.children.size(); suffix-- > index;)
	{
		const AstNode& part = *node.children[suffix];
		if (part.kind == AstKind::CvQualifier)
		{
			function_cv |= part.token == KW_CONST ? kCvConst : kCvVolatile;
			continue;
		}
		type = apply_suffix(part, type, ctx, declared);
		if (part.kind == AstKind::ParameterClause)
		{
			if (semantics())
			{
				// PA11 describes the declarator as written, and a
				// cv-qualifier-seq is part of the type only where 9.3.1p3 gives
				// it a meaning, which is what PA12 reads it for.
				type = types_.qualified_function(type, function_cv);
			}
			function_cv = kCvNone;
			declared = nullptr;
		}
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
	// 8.4.1p1: the parameter-clause of a function definition is written on the
	// declarator itself, so a nested one names nothing the definition binds.
	return core->children.empty()
		? type
		: declarator_type(*core->children[0], type, ctx, name);
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
		return types_.array_of(type, true, array_bound(*node.children[0], ctx));
	}
	if (node.kind == AstKind::ParameterClause)
	{
		std::vector<Parameter> read;
		std::vector<Parameter>& parameters = declared != nullptr ? *declared : read;
		bool variadic = false;
		read_parameters(node, ctx, parameters, variadic);
		std::vector<TypeId> types;
		types.reserve(parameters.size());
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			// 8.3.5p5: an array or function parameter contributes a pointer,
			// and top level cv-qualification is dropped, so two declarations
			// that differ only in those declare one function.  PA11 describes
			// the declarator as written, so only PA12 asks for it.
			types.push_back(semantics()
				? types_.adjust_parameter(parameters[index].type)
				: parameters[index].type);
		}
		return types_.function_of(type, types, variadic);
	}
	// A cv-qualifier, ref-qualifier, exception-specification or virt-specifier
	// on a declarator changes no type PA11 describes.
	return type;
}

void SemaAnalyzer::read_parameters(const AstNode& clause, const Context& ctx,
                                   std::vector<Parameter>& out, bool& variadic)
{
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
		Specifiers specifiers =
			read_specifiers(*seq, ctx, span, true, std::string());
		Parameter parameter;
		parameter.type = specifier_type(specifiers);
		const AstNode* declarator = declarator_of(child);
		if (declarator != nullptr)
		{
			// 8.3.5p3: `f(int...)` writes the ellipsis without a comma, so it
			// arrives inside the last parameter's declarator rather than beside
			// it, and it still says the function takes further arguments.
			if (child_kind(*declarator, AstKind::ParameterPack) != nullptr)
			{
				variadic = true;
			}
			parameter.type = declarator_type(*declarator, parameter.type, ctx,
			                                 &parameter.name);
		}
		parameter.initializer = child_kind(child, AstKind::DefaultArgument);
		out.push_back(parameter);
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

SemaEntity* SemaAnalyzer::resolve(const std::string& name, const Context& ctx,
                                  LookupKind filter,
                                  std::vector<SemaEntity*>* found)
{
	if (name.empty())
	{
		return nullptr;
	}
	const QualifiedName written(name);
	if (!written.qualified())
	{
		return model_.lookup(*ctx.scope, name, filter, found);
	}
	SemaEntity* const named = model_.lookup_in(
		*resolve_prefix(written, ctx), written.last(), filter, found);
	if (named != nullptr)
	{
		// 11.2: a qualified name reaches a member of the class it names, which
		// is where the access that class gave the member is asked about.
		require_access(*named, ctx.scope);
	}
	return named;
}

// 3.4.3: each component of a nested-name-specifier is looked up in the region
// the one before it named, the first in the scopes around the declaration.
Scope* SemaAnalyzer::resolve_prefix(const QualifiedName& name,
                                    const Context& ctx)
{
	// 3.4.3p1: a name written `::x` is looked up in the global namespace.
	const std::string first = name.part(0);
	Scope* region = first.empty()
		? &model_.global()
		: model_.region_of(
			require(model_.lookup(*ctx.scope, first, LookupKind::Region), first));

	for (std::size_t index = 1; index + 1 < name.size(); ++index)
	{
		const std::string part = name.part(index);
		if (region == nullptr)
		{
			throw std::runtime_error(name.part(index - 1) + " names no region");
		}
		SemaEntity* next = model_.lookup_in(*region, part, LookupKind::Region);
		region = model_.region_of(require(next, part));
	}
	if (region == nullptr)
	{
		throw std::runtime_error(name.last() + " is written after a name that is "
		                                       "not a namespace, class or "
		                                       "enumeration");
	}
	return region;
}
