#include "sema_analyzer.h"

#include <ostream>
#include <stdexcept>

#include "ast_model.h"

namespace
{

std::string decimal(unsigned long long value, bool negative)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	if (digits.empty())
	{
		digits = "0";
	}
	return negative ? "-" + digits : digits;
}

// The child of `node` of a kind, or null.
const AstNode* child_of(const AstNode& node, AstKind kind)
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

bool has_child(const AstNode& node, AstKind kind)
{
	return child_of(node, kind) != nullptr;
}

// 9p1: which class-key a class-specifier or elaborated-type-specifier wrote.
ClassTag tag_of(const AstNode& node)
{
	const AstNode* key = child_of(node, AstKind::ClassKey);
	if (key == nullptr)
	{
		return ClassTag::Struct;
	}
	if (key->token == KW_CLASS)
	{
		return ClassTag::Class;
	}
	return key->token == KW_UNION ? ClassTag::Union : ClassTag::Struct;
}

const char* tag_text(ClassTag tag)
{
	switch (tag)
	{
	case ClassTag::Class: return "class ";
	case ClassTag::Union: return "union ";
	default: return "struct ";
	}
}

unsigned long long round_up(unsigned long long value, unsigned long long unit)
{
	if (unit == 0)
	{
		return value;
	}
	const unsigned long long remainder = value % unit;
	return remainder == 0 ? value : value + (unit - remainder);
}

}

SemaAnalyzer::SemaAnalyzer()
	: anonymous_enums_(0)
{}

void SemaAnalyzer::write(std::ostream& out) const
{
	write_dump(out, model_.root(), 0);
}

void SemaAnalyzer::run(const AstNode& unit)
{
	Context ctx;
	ctx.scope = &model_.global();
	ctx.dump = model_.global().dump;
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index], ctx);
	}
}

void SemaAnalyzer::declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;

	switch (node.kind)
	{
	case AstKind::NamespaceDefinition:
		namespace_definition(node, ctx);
		return;

	case AstKind::NamespaceAliasDefinition:
		namespace_alias(node, ctx);
		return;

	case AstKind::UsingDirective:
		using_directive(node, ctx);
		return;

	case AstKind::UsingDeclaration:
		using_declaration(node, ctx);
		return;

	case AstKind::AliasDeclaration:
		alias_declaration(node, ctx);
		return;

	case AstKind::StaticAssertDeclaration:
		static_assert_declaration(node, ctx);
		return;

	case AstKind::TemplateDeclaration:
		template_declaration(node, ctx);
		return;

	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
		inject_union_members(
			&class_declaration(node, ctx, span,
			                   node.kind == AstKind::ClassSpecifier,
			                   std::string()),
			ctx);
		return;

	case AstKind::EnumSpecifier:
		enum_declaration(node, ctx, false, std::string());
		return;

	case AstKind::SimpleDeclaration:
		simple_declaration(node, ctx);
		return;

	case AstKind::FunctionDefinition:
		function_definition(node, ctx);
		return;

	case AstKind::LinkageSpecification:
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			declaration(*node.children[index], ctx);
		}
		return;

	default:
		// An access-specifier, an empty declaration and the member forms PA11
		// gives no meaning to declare nothing.
		return;
	}
}

void SemaAnalyzer::namespace_definition(const AstNode& node, const Context& ctx)
{
	// 7.3.1p2: a namespace-definition of a name already declared here as a
	// namespace extends it rather than declaring a second one.
	SemaEntity* entity = model_.find(*ctx.scope, node.text, LookupKind::Space);
	if (entity == nullptr)
	{
		if (model_.find(*ctx.scope, node.text, LookupKind::Any) != nullptr)
		{
			throw std::runtime_error("a namespace is declared with the name of "
			                         "another declaration");
		}
		entity = &model_.create(SemaKind::Namespace, node.text, kNoType);
		DumpScope& dump =
			model_.open_dump(*ctx.dump, "scope namespace " + node.text);
		entity->scope =
			&model_.open(ScopeKind::Namespace, *ctx.scope, entity, &dump);
		model_.bind(*ctx.scope, node.text, *entity);
		model_.declare_in(*ctx.scope, *entity);
		// 7.3.1p8 and 7.3.1.1p1: an inline or unnamed member's declarations
		// are also declarations of the namespace around it.
		if (has_child(node, AstKind::Inline))
		{
			model_.nominate(*ctx.scope, *entity->scope);
		}
	}
	else if (entity->kind != SemaKind::Namespace)
	{
		throw std::runtime_error("a namespace-definition names a namespace alias");
	}

	Context inner;
	inner.scope = entity->scope;
	inner.dump = entity->scope->dump;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		declaration(*node.children[index], inner);
	}
}

void SemaAnalyzer::namespace_alias(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	SemaEntity& space =
		require(resolve(target->text, ctx, LookupKind::Space), target->text);
	SemaEntity& entity = model_.create(SemaKind::NamespaceAlias, node.text, kNoType);
	entity.scope = model_.region_of(space);
	model_.bind(*ctx.scope, node.text, entity);
}

void SemaAnalyzer::using_directive(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	SemaEntity& space =
		require(resolve(target->text, ctx, LookupKind::Space), target->text);
	model_.nominate(*ctx.scope, *model_.region_of(space));
}

void SemaAnalyzer::using_declaration(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	const QualifiedName written(target->text);
	if (written.names_a_template_id())
	{
		// 7.3.3p5: a using-declaration shall not name a template-id.
		throw std::runtime_error("a using-declaration names a template-id");
	}
	SemaEntity& entity =
		require(resolve(target->text, ctx, LookupKind::Any), target->text);
	const std::string name = written.last();
	model_.bind(*ctx.scope, name, entity);
	model_.declare_in(*ctx.scope, entity);
	write_entity_line(*ctx.dump, entity);
}

void SemaAnalyzer::alias_declaration(const AstNode& node, const Context& ctx)
{
	const AstNode* type = child_of(node, AstKind::TypeId);
	const TypeId aliased = type_id_type(*type, ctx);
	SemaEntity& entity = model_.create(SemaKind::Typedef, node.text, aliased);
	model_.bind(*ctx.scope, node.text, entity);
	model_.declare_in(*ctx.scope, entity);
	write_line(*ctx.dump, "type-alias", node.text, aliased);
}

void SemaAnalyzer::static_assert_declaration(const AstNode& node,
                                             const Context& ctx)
{
	const Constant value = evaluate(*node.children[0], ctx);
	if (value.bits == 0)
	{
		throw std::runtime_error("a static_assert condition is false");
	}
}

void SemaAnalyzer::template_declaration(const AstNode& node, const Context& ctx)
{
	// 14.1p1 and 3.3.2p4: the template parameters are declared in a region of
	// their own that encloses the declaration they parameterise.
	DumpScope& dump = model_.open_dump(*ctx.dump, "scope template-parameters");
	Context inner;
	inner.scope =
		&model_.open(ScopeKind::TemplateParameters, *ctx.scope, nullptr, &dump);
	inner.dump = &dump;

	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::TemplateParameterClause)
		{
			const AstNode* list =
				child_of(child, AstKind::TemplateParameterList);
			for (std::size_t at = 0; list != nullptr && at < list->children.size();
			     ++at)
			{
				template_parameter(*list->children[at], inner);
			}
			continue;
		}
		declaration(child, inner);
	}
}

void SemaAnalyzer::template_parameter(const AstNode& node, const Context& ctx)
{
	if (node.kind != AstKind::TypeParameter)
	{
		// 14.1p2: a non-type parameter binds a value, which PA11 does not model.
		return;
	}
	const AstNode* id = child_of(node, AstKind::Identifier);
	if (id == nullptr)
	{
		return;
	}
	// 14.1p2: a parameter declared with `template` names a template rather
	// than a type; the parameters of its own clause belong to it alone.
	const bool is_template = has_child(node, AstKind::TemplateTemplateParameter);
	const TypeId type = types_.template_parameter_type(model_.type_entity_id(),
	                                                   is_template, id->text);
	SemaEntity& entity = model_.create(SemaKind::TemplateType, id->text, type);
	model_.bind(*ctx.scope, id->text, entity);
	model_.declare_in(*ctx.scope, entity);
	write_line(*ctx.dump, "type", id->text, type);
}

SemaEntity* SemaAnalyzer::redeclared(const Context& ctx, const std::string& name,
                                     SemaKind kind)
{
	SemaEntity* found = model_.find(*ctx.scope, name,
	                                kind == SemaKind::Class || kind == SemaKind::Enum
	                                    ? LookupKind::Type
	                                    : LookupKind::Any);
	if (found == nullptr || found->kind != kind)
	{
		return nullptr;
	}
	return found;
}

SemaEntity& SemaAnalyzer::class_declaration(const AstNode& node,
                                            const Context& ctx, const Span& span,
                                            bool define,
                                            const std::string& named_by)
{
	const ClassTag tag = tag_of(node);
	// 7.1.3p2: a class its specifiers left unnamed is named by the first
	// declarator of the declaration it belongs to, before its body is read, so
	// every line the body writes spells it the way the program will.
	const std::string written = node.text.empty() ? named_by : node.text;
	const std::string name = QualifiedName(written).last();

	SemaEntity* entity = name.empty() ? nullptr
	                                  : redeclared(ctx, name, SemaKind::Class);
	if (entity != nullptr)
	{
		// 9p3: two declarations of one class agree exactly when neither or
		// both wrote `union`.
		const bool was_union = types_.class_tag(entity->type) == ClassTag::Union;
		if (was_union != (tag == ClassTag::Union))
		{
			throw std::runtime_error("a class is redeclared with a class-key "
			                         "that does not agree with its definition");
		}
		if (define && entity->defined)
		{
			throw std::runtime_error("a class is defined twice");
		}
	}
	else
	{
		const std::uint32_t id = model_.type_entity_id();
		const TypeId type = types_.class_type(id, tag, name);
		entity = &model_.create(SemaKind::Class, name, type);
		model_.own_type(type, *entity);
		if (!name.empty())
		{
			model_.bind(*ctx.scope, name, *entity);
			model_.declare_in(*ctx.scope, *entity);
		}
	}

	if (!name.empty())
	{
		// The line is spelled as this declaration spells it, class-key and
		// nested-name-specifier included.
		ctx.dump->lines.push_back("type " + written + " " + tag_text(tag) + written);
	}
	if (!define)
	{
		return *entity;
	}

	// 9.5p2 and the shared convention: an unnamed class no declarator names is
	// named after the terminals its declaration was written from.
	const std::string header = name.empty()
		? std::string("__anonymous_") +
			(tag == ClassTag::Union ? "union" : "class") + "_type__" +
			decimal(span.begin, false) + "_" + decimal(span.end, false)
		: written;
	if (name.empty())
	{
		types_.rename(entity->type, header);
	}
	DumpScope& dump = model_.open_dump(*ctx.dump, "scope class " + header);
	Scope& scope = model_.open(ScopeKind::Class, *ctx.scope, entity, &dump);
	entity->scope = &scope;
	entity->defined = true;

	Context inner;
	inner.scope = &scope;
	inner.dump = &dump;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& member = *node.children[index];
		if (member.kind == AstKind::ClassKey || member.kind == AstKind::BaseClause)
		{
			continue;
		}
		declaration(member, inner);
	}
	lay_out_class(*entity, scope, tag == ClassTag::Union);
	return *entity;
}

void SemaAnalyzer::inject_union_members(SemaEntity* entity, const Context& ctx)
{
	// 9.5p1: a union with no name and no declarator declares its members in the
	// region it is written in rather than a region of its own.
	if (entity == nullptr || entity->scope == nullptr || !entity->name.empty() ||
	    !types_.is_class(entity->type) ||
	    types_.class_tag(entity->type) != ClassTag::Union)
	{
		return;
	}
	Scope& members = *entity->scope;
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (member.kind != SemaKind::Variable)
		{
			continue;
		}
		model_.bind(*ctx.scope, member.name, member);
		model_.declare_in(*ctx.scope, member);
		write_line(*ctx.dump, "variable", member.name, member.type);
	}
}

void SemaAnalyzer::lay_out_class(SemaEntity& entity, Scope& scope, bool is_union)
{
	unsigned long long size = 0;
	unsigned long long align = 1;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.kind != SemaKind::Variable)
		{
			continue;
		}
		const unsigned long long member_size = types_.object_size(member.type);
		const unsigned long long member_align = types_.object_align(member.type);
		if (member_align > align)
		{
			align = member_align;
		}
		if (is_union)
		{
			size = member_size > size ? member_size : size;
			continue;
		}
		size = round_up(size, member_align) + member_size;
	}
	// 1.8p5: a complete object has a size of at least one byte.
	size = round_up(size, align);
	types_.complete_class(entity.type, size == 0 ? 1 : size, align);
}

// 7.1.3p2: the declarator-id of the first declarator of a declaration, which
// is the name an unnamed class or enumeration in its specifiers takes.
std::string SemaAnalyzer::name_from_declarators(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& init = *node.children[index];
		if (init.children.empty())
		{
			continue;
		}
		const AstNode* id = declarator_id(*init.children[0]);
		if (id != nullptr)
		{
			return id->text;
		}
	}
	return std::string();
}

void SemaAnalyzer::write_line(DumpScope& dump, const char* what,
                              const std::string& name, TypeId type)
{
	dump.lines.push_back(std::string(what) + " " + name + " " +
	                     types_.description(type));
}

void SemaAnalyzer::write_entity_line(DumpScope& dump, const SemaEntity& entity)
{
	switch (entity.kind)
	{
	case SemaKind::Class:
	case SemaKind::Enum:
	case SemaKind::TemplateType:
		write_line(dump, "type", entity.name, entity.type);
		return;

	case SemaKind::Typedef:
		write_line(dump, "type-alias", entity.name, entity.type);
		return;

	case SemaKind::Function:
		write_line(dump, "function", entity.name, entity.type);
		return;

	case SemaKind::Parameter:
		write_line(dump, "parameter", entity.name, entity.type);
		return;

	case SemaKind::Enumerator:
		dump.lines.push_back("enumerator " + entity.name + " " +
		                     types_.description(entity.type) + " " +
		                     spell_value(entity.type, entity.value));
		return;

	case SemaKind::Variable:
		write_line(dump, "variable", entity.name, entity.type);
		return;

	default:
		// A namespace and an alias of one have no line of their own.
		return;
	}
}

SemaEntity& SemaAnalyzer::enum_declaration(const AstNode& node,
                                           const Context& ctx, bool elaborated,
                                           const std::string& named_by)
{
	const bool scoped = has_child(node, AstKind::EnumKey);
	const AstNode* base = child_of(node, AstKind::TypeId);
	const bool defines = has_child(node, AstKind::Enumerator);
	// 7.1.3p2: an unnamed enumeration is named by the first declarator of its
	// declaration, and one no declarator names is numbered.
	const bool unnamed = node.text.empty() && named_by.empty();
	const std::string written = unnamed
		? "__anonymous_enum" + decimal(++anonymous_enums_, false)
		: (node.text.empty() ? named_by : node.text);
	const QualifiedName spelled(written);
	const std::string name = spelled.last();
	const bool qualified = spelled.qualified();

	SemaEntity* entity = nullptr;
	if (elaborated || qualified)
	{
		// 3.4.4p2 and 7.2p5: an elaborated-type-specifier and an out-of-class
		// definition both name an enumeration that is already declared.
		SemaEntity* found = qualified
			? model_.lookup_in(*resolve_prefix(spelled, ctx), name,
			                   LookupKind::Type)
			: resolve(written, ctx, LookupKind::Type);
		entity = &require(found, written);
		if (entity->kind != SemaKind::Enum)
		{
			throw std::runtime_error("an elaborated enum specifier names " +
			                         written + ", which is not an enumeration");
		}
		if (elaborated)
		{
			return *entity;
		}
	}
	else
	{
		entity = redeclared(ctx, name, SemaKind::Enum);
	}

	// 7.2p2 and 7.2p5: an enumeration whose underlying type is not fixed
	// cannot be named before it is defined, and two declarations of one
	// enumeration fix the same underlying type.
	const TypeId underlying =
		base != nullptr ? type_id_type(*base, ctx) : types_.fundamental(FT_INT);
	if (entity == nullptr)
	{
		if (!defines && !scoped && base == nullptr)
		{
			throw std::runtime_error("an opaque declaration of an unscoped "
			                         "enumeration fixes no underlying type");
		}
		const std::uint32_t id = model_.type_entity_id();
		const TypeId type = types_.enum_type(id, scoped, name, underlying);
		entity = &model_.create(SemaKind::Enum, name, type);
		model_.own_type(type, *entity);
		model_.bind(*ctx.scope, name, *entity);
		model_.declare_in(*ctx.scope, *entity);
	}
	else if (types_.target(entity->type) != underlying)
	{
		throw std::runtime_error("an enumeration is redeclared with a different "
		                         "underlying type");
	}

	const std::string spelling = (scoped ? "enum class " : "enum ") + written;
	if (!unnamed)
	{
		ctx.dump->lines.push_back("type " + written + " " + spelling);
	}

	// A scoped enumeration writes a scope of its own for every declaration of
	// it; an unscoped one writes none, because 7.2p10 declares its enumerators
	// in the region around it and the dump writes them there.
	DumpScope* dump = ctx.dump;
	if (scoped)
	{
		dump = &model_.open_dump(*ctx.dump, "scope enum " + written);
	}
	if (entity->scope == nullptr)
	{
		entity->scope = &model_.open(ScopeKind::Enum, *ctx.scope, entity, dump);
	}
	if (defines)
	{
		if (entity->defined)
		{
			throw std::runtime_error("an enumeration is defined twice");
		}
		entity->defined = true;
	}
	enumerators(node, *entity, spelling, *dump);
	return *entity;
}

void SemaAnalyzer::enumerators(const AstNode& node, SemaEntity& entity,
                               const std::string& spelling, DumpScope& dump)
{
	Scope& scope = *entity.scope;
	const bool scoped = types_.is_scoped_enum(entity.type);
	unsigned long long next = 0;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::Enumerator)
		{
			continue;
		}
		unsigned long long value = next;
		if (!child.children.empty())
		{
			// 7.2p1: the constant-expression of an enumerator-definition.
			Context inner;
			inner.scope = &scope;
			inner.dump = &dump;
			value = evaluate(*child.children[0], inner).bits;
		}
		next = value + 1;

		SemaEntity& enumerator =
			model_.create(SemaKind::Enumerator, child.text, entity.type);
		enumerator.constant = true;
		enumerator.value = value;
		model_.bind(scope, child.text, enumerator);
		model_.declare_in(scope, enumerator);
		if (!scoped && scope.parent != nullptr)
		{
			// 7.2p10: an unscoped enumeration's enumerators are declared in the
			// region the enumeration is declared in, which is not where the
			// definition is written when 7.2p1 writes it outside its class.
			model_.bind(*scope.parent, child.text, enumerator);
			model_.declare_in(*scope.parent, enumerator);
		}
		// The enumeration is spelled as this declaration spells it, which for
		// a definition written outside its class is the qualified name.
		dump.lines.push_back("enumerator " + child.text + " " + spelling + " " +
		                     spell_value(entity.type, value));
	}
}

void SemaAnalyzer::simple_declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const AstNode* list = child_of(node, AstKind::InitDeclaratorList);
	Specifiers specifiers = read_specifiers(*node.children[0], ctx, span, true,
	                                        list == nullptr
	                                            ? std::string()
	                                            : name_from_declarators(*list));
	if (list == nullptr)
	{
		inject_union_members(specifiers.introduced, ctx);
		return;
	}
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& init = *list->children[index];
		init_declarator(*init.children[0], child_of(init, AstKind::Initializer),
		                specifiers, ctx);
	}
}

// 6.4p3: a condition declares its name in a region that encloses the statement's
// substatements, which is the region the statement itself is written in.
void SemaAnalyzer::condition_declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
	const AstNode* declarator = child_of(node, AstKind::Declarator);
	if (declarator != nullptr)
	{
		init_declarator(*declarator, child_of(node, AstKind::Initializer),
		                specifiers, ctx);
	}
}

void SemaAnalyzer::init_declarator(const AstNode& node,
                                   const AstNode* initializer,
                                   const Specifiers& specifiers,
                                   const Context& ctx)
{
	std::string written;
	TypeId type = declarator_type(node, specifier_type(specifiers), ctx, &written);
	const QualifiedName spelled(written);
	const std::string name = spelled.last();
	if (name.empty())
	{
		return;
	}

	// 3.4.3p3: a declarator-id with a nested-name-specifier declares into the
	// region that names, wherever the declaration is written.
	Context target = ctx;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
	}

	if (specifiers.is_typedef)
	{
		SemaEntity& entity = model_.create(SemaKind::Typedef, name, type);
		model_.bind(*target.scope, name, entity);
		model_.declare_in(*target.scope, entity);
		write_line(*target.dump, "type-alias", name, type);
		return;
	}
	if (types_.kind(type) == TypeKind::Function)
	{
		SemaEntity* prior = model_.find(*target.scope, name, LookupKind::Any);
		if (prior == nullptr || prior->kind != SemaKind::Function ||
		    prior->type != type)
		{
			prior = &model_.create(SemaKind::Function, name, type);
			model_.bind(*target.scope, name, *prior);
			model_.declare_in(*target.scope, *prior);
		}
		write_line(*target.dump, "function", name, type);
		return;
	}

	// 7.1.5p9: a constexpr object is a const object.
	if (specifiers.is_constexpr)
	{
		type = types_.qualified(type, kCvConst);
	}
	SemaEntity& entity = model_.create(SemaKind::Variable, name, type);
	if (initializer != nullptr && !initializer->children.empty() &&
	    (types_.cv(type) & kCvConst) != 0 && arithmetic_type(type) != kNoType)
	{
		// 5.19p3: a const object of integral type initialized by a constant
		// expression is one, and is what an array bound may be written with.
		// An initializer that is an ordinary expression leaves it an object
		// like any other; an initializer that is ill formed is still ill
		// formed, so only the one failure is caught.
		try
		{
			entity.value = convert(evaluate(*initializer->children[0], ctx),
			                       type).bits;
			entity.constant = true;
		}
		catch (const NotConstant&)
		{
			entity.constant = false;
		}
	}
	model_.bind(*target.scope, name, entity);
	model_.declare_in(*target.scope, entity);
	write_line(*target.dump, "variable", name, type);
}

void SemaAnalyzer::function_definition(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
	const AstNode& declarator = *node.children[1];
	const AstNode* id = declarator_id(declarator);
	const std::string written = id == nullptr ? std::string() : id->text;
	const QualifiedName spelled(written);
	const std::string name = spelled.last();

	// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
	// looked up in the region that name reaches.
	Context target = ctx;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
	}

	std::string ignored;
	std::vector<Parameter> parameters;
	const TypeId type = declarator_type(declarator, specifier_type(specifiers),
	                                    target, &ignored, &parameters);
	if (types_.kind(type) != TypeKind::Function)
	{
		throw std::runtime_error("a function definition declares " + name +
		                         ", which is not a function");
	}

	SemaEntity* entity = model_.find(*target.scope, name, LookupKind::Any);
	if (entity == nullptr || entity->kind != SemaKind::Function ||
	    entity->type != type)
	{
		entity = &model_.create(SemaKind::Function, name, type);
		model_.bind(*target.scope, name, *entity);
		model_.declare_in(*target.scope, *entity);
	}
	entity->defined = true;
	write_line(*target.dump, "function", name, type);

	DumpScope& dump = model_.open_dump(*target.dump, "scope function " + name);
	Context inner;
	inner.scope = &model_.open(ScopeKind::Function, *target.scope, entity, &dump);
	inner.dump = &dump;

	// 8.4.1p1: the parameters the declarator's own parameter-clause declared,
	// which the type it built already read.
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		SemaEntity& parameter = model_.create(
			SemaKind::Parameter, parameters[index].name, parameters[index].type);
		if (!parameter.name.empty())
		{
			model_.bind(*inner.scope, parameter.name, parameter);
		}
		model_.declare_in(*inner.scope, parameter);
		write_line(dump, "parameter", parameter.name, parameter.type);
	}

	for (std::size_t index = 2; index < node.children.size(); ++index)
	{
		statement(*node.children[index], inner);
	}
}

void SemaAnalyzer::statement(const AstNode& node, const Context& ctx)
{
	switch (node.kind)
	{
	case AstKind::CompoundStatement:
	{
		DumpScope& dump = model_.open_dump(*ctx.dump, "scope block");
		Context inner;
		inner.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, &dump);
		inner.dump = &dump;
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			statement(*node.children[index], inner);
		}
		return;
	}

	case AstKind::SimpleDeclaration:
	case AstKind::AliasDeclaration:
	case AstKind::UsingDeclaration:
	case AstKind::UsingDirective:
	case AstKind::NamespaceAliasDefinition:
	case AstKind::StaticAssertDeclaration:
	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
	case AstKind::EnumSpecifier:
		declaration(node, ctx);
		return;

	case AstKind::ConditionDeclaration:
		condition_declaration(node, ctx);
		return;

	case AstKind::IfStatement:
	case AstKind::SwitchStatement:
	case AstKind::WhileStatement:
	case AstKind::DoStatement:
	case AstKind::ForStatement:
	case AstKind::TryBlock:
	case AstKind::Handler:
	case AstKind::LabeledStatement:
	case AstKind::CaseStatement:
	case AstKind::DefaultStatement:
	case AstKind::Then:
	case AstKind::Else:
	case AstKind::Iteration:
	case AstKind::ForInitStatement:
	case AstKind::Condition:
		// 3.3.3: a statement with a substatement encloses it, and PA11 models
		// of a statement only the regions it opens.
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			statement(*node.children[index], ctx);
		}
		return;

	default:
		// An expression declares nothing PA11 describes.
		return;
	}
}
