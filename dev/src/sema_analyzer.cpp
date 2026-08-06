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

// Whether `outer` is `inner` or a region `inner` is written in.
bool encloses(const Scope& outer, const Scope& inner)
{
	for (const Scope* at = &inner; at != nullptr; at = at->parent)
	{
		if (at == &outer)
		{
			return true;
		}
	}
	return false;
}

// 7.3.1.1p1: the namespace-definition PA10 wrote no name for, which it spells
// with the placeholder the dump uses.
bool is_unnamed_namespace(const AstNode& node)
{
	return node.text == "<unnamed>";
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

SemaAnalyzer::SemaAnalyzer(SemaDialect dialect)
	: dialect_(dialect)
	, anonymous_enums_(0)
	, local_types_(0)
	, self_(nullptr)
	, breakable_(0)
	, continuable_(0)
	, switches_(0)
	, returns_(kNoType)
	, c_linkage_(false)
{}

SemaAnalyzer::Pending::Pending()
	: function(nullptr)
	, self(nullptr)
	, body(nullptr)
	, scope(nullptr)
	, instantiation(false)
{}

SemaAnalyzer::Value::Value()
	: type(kNoType)
	, spelled(kNoType)
	, category(ValueCategory::PRValue)
	, node(nullptr)
	, functions(nullptr)
	, addressed(nullptr)
	, name(nullptr)
	, what(nullptr)
	, null_constant(false)
	, constant(false)
	, value(0)
	, entity(nullptr)
	, op(0)
	, operands(kNoType)
{}

SemaAnalyzer::Match::Match()
	: viable(false)
	, rank(0)
	, to_bool(false)
	, reference(false)
	, binds_rvalue_ref(false)
	, binds_lvalue(false)
	, materialized(kNoType)
{}

void SemaAnalyzer::write(std::ostream& out) const
{
	if (semantics())
	{
		write_nodes(out, model_.unit(), 0);
		return;
	}
	write_dump(out, model_.root(), 0);
}

std::string SemaAnalyzer::dump_name(const Scope& scope,
                                    const std::string& name) const
{
	// 3.4.3.1 and 3.4.3.2: a declaration of a namespace or of a class is named
	// from outside it by the regions it is written in, which is what the prefix
	// of the region holds.  A block has no such name, so a declaration of one
	// is spelled as it was written.
	return scope.kind == ScopeKind::Namespace || scope.kind == ScopeKind::Class
		? scope.prefix + name
		: name;
}

void SemaAnalyzer::run(const AstNode& unit)
{
	Context ctx;
	ctx.scope = &model_.global();
	ctx.dump = model_.global().dump;
	ctx.node = &model_.unit();
	if (semantics())
	{
		// 18.2p9: `std::nullptr_t` is the type of `nullptr`.  The course
		// declares it in the global namespace, and PA12 overloads on it, so
		// the name is bound before the unit is read.  It writes no line: the
		// dump describes what the unit declares.
		SemaEntity& entity = model_.create(SemaKind::Typedef, "nullptr_t",
		                                   types_.fundamental(FT_NULLPTR_T));
		model_.bind(*ctx.scope, entity.name, entity);
	}
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index], ctx);
	}
	write_pending_definitions();
}

bool SemaAnalyzer::accepts_arity(const SemaEntity& function,
                                 std::size_t given) const
{
	const std::size_t declared = types_.parameters(function.type).size();
	if (given >= declared)
	{
		return true;
	}
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(function.id);
	if (found == defaults_.end())
	{
		return false;
	}
	for (std::size_t index = given; index < declared; ++index)
	{
		if (index >= found->second.size() ||
		    found->second[index].written == nullptr)
		{
			return false;
		}
	}
	return true;
}

void SemaAnalyzer::record_default_arguments(
	const SemaEntity& function, const std::vector<Parameter>& declared,
	Scope* region)
{
	// 9.3.1p3: a member function's declarator does not write the object
	// parameter, so the parameters it did write begin after it.  The defaults
	// are held at the place the function type gives each parameter, which is
	// what every arity question asks about.
	const std::size_t total = types_.parameters(function.type).size();
	const std::size_t implicit =
		total > declared.size() ? total - declared.size() : 0;
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		if (declared[index].initializer == nullptr)
		{
			continue;
		}
		const std::size_t at = index + implicit;
		std::vector<Default>& held = defaults_[function.id];
		held.resize(at + 1 > held.size() ? at + 1 : held.size());
		if (held[at].written != nullptr)
		{
			// 8.3.6p4: a parameter's default-argument belongs to the
			// declaration that first gave it, which a later one does not move.
			continue;
		}
		held[at].written = declared[index].initializer;
		held[at].scope = region;
	}
}

void SemaAnalyzer::write_default_argument(const SemaEntity& function,
                                          std::size_t index, DumpNode& parent)
{
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(function.id);
	if (found == defaults_.end() || index >= found->second.size() ||
	    found->second[index].written == nullptr)
	{
		throw std::runtime_error("a call omits an argument the declaration "
		                         "gives no default for");
	}
	const AstNode& written = *found->second[index].written;
	if (written.children.empty() || written.children[0]->children.empty())
	{
		throw std::runtime_error("a default-argument is written with no value");
	}
	// 8.3.6p9: the default-argument is looked up and read in the region the
	// declaration that introduced it was written in, not the one the call is.
	Context where;
	where.scope = found->second[index].scope;
	where.dump = where.scope->dump;
	where.node = &parent;
	initialize(*written.children[0]->children[0],
	           types_.parameters(function.type)[index], where, parent);
}

void SemaAnalyzer::write_pending_definitions()
{
	// A body read here may itself default-initialize an object, and so ask for
	// a definition that is not in the list yet.  Walking by index is what lets
	// the list grow while it is being written, and each definition is added
	// once, so the walk ends.
	for (std::size_t index = 0; index < pending_.size(); ++index)
	{
		write_definition(pending_[index]);
	}
}

void SemaAnalyzer::write_definition(Pending& pending)
{
	SemaEntity& function = *pending.function;
	if (pending.instantiation)
	{
		write_instantiation(pending);
		return;
	}
	DumpNode& line = open_fact(model_.unit(), "function-definition " +
	                           function.dump_name + " " +
	                           types_.description(function.type),
	                           FactKind::FunctionDefinition);
	line.fact.entity = &function;
	line.fact.type = function.type;
	if (pending.self != nullptr)
	{
		DumpNode& self = open_fact(line, "parameter " + pending.self->name + " " +
		                           types_.description(pending.self->type),
		                           FactKind::Parameter);
		self.fact.entity = pending.self;
		self.fact.type = pending.self->type;
	}
	if (pending.body == nullptr)
	{
		// 12.1p5: the constructor a class has that no declaration wrote does
		// nothing the output describes.
		model_.open_node(line, "compound-statement");
		return;
	}

	Context inner;
	inner.scope = pending.scope;
	inner.dump = pending.scope->dump;
	inner.node = &model_.unit();
	declare_parameters(pending.parameters, function.type, inner, &line,
	                   pending.self != nullptr ? 1 : 0);

	// 9.2p2: the body is read where the class is complete, which is here, so
	// what the walk of the class left behind is put back for it.
	SemaEntity* const enclosing_self = self_;
	const TypeId enclosing_return = returns_;
	const unsigned breakable = breakable_;
	const unsigned continuable = continuable_;
	const unsigned switches = switches_;
	self_ = pending.self;
	returns_ = types_.target(function.type);
	breakable_ = 0;
	continuable_ = 0;
	switches_ = 0;
	for (std::size_t index = 2; index < pending.body->children.size(); ++index)
	{
		semantic_statement(*pending.body->children[index], inner, line);
	}
	self_ = enclosing_self;
	returns_ = enclosing_return;
	breakable_ = breakable;
	continuable_ = continuable;
	switches_ = switches;
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
			ctx, span);
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
	{
		// 7.5p4: linkage specifications nest, and the innermost one a
		// declaration is written in is the linkage it has.
		const bool enclosing = c_linkage_;
		c_linkage_ = node.text == "C";
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			declaration(*node.children[index], ctx);
		}
		c_linkage_ = enclosing;
		return;
	}

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
		// 7.3.1.1p1: an unnamed namespace has no name to write before its
		// members, so they are spelled by the namespace around it.
		entity->scope->prefix = is_unnamed_namespace(node)
			? ctx.scope->prefix
			: ctx.scope->prefix + node.text + "::";
		model_.bind(*ctx.scope, node.text, *entity);
		model_.declare_in(*ctx.scope, *entity);
		// 7.3.1p8 and 7.3.1.1p1: an inline or unnamed member's declarations
		// are also declarations of the namespace around it.
		if (has_child(node, AstKind::Inline) || is_unnamed_namespace(node))
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
	// The dump writes one node per namespace-definition, so a namespace opened
	// twice is two nodes over one region.
	inner.node = semantics()
		? &open_fact(*ctx.node, "namespace-definition " + node.text,
		             FactKind::Namespace)
		: ctx.node;
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
	// 7.3.4p2: the nominated namespace's declarations appear in the nearest
	// enclosing namespace that holds both it and the directive.  A directive
	// written in a block therefore does not put them in the block, so a name
	// an enclosing namespace declares still hides them.
	Scope* nominated = model_.region_of(space);
	Scope* where = ctx.scope;
	while (where->kind != ScopeKind::Namespace && where->parent != nullptr)
	{
		// A directive written in a namespace stays there, because 3.4.3.2p2
		// also looks through it for a qualified name.  One written in a block
		// is only ever read by unqualified lookup, so it is recorded where
		// 7.3.4p2 says its names appear.
		where = where->parent;
		while (where->parent != nullptr && !encloses(*where, *nominated))
		{
			where = where->parent;
		}
	}
	model_.nominate(*where, *nominated);
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
	if (semantics())
	{
		// 9.2p1: an alias a class declares is a member of it, and a member
		// declaration writes no line of its own.
		if (ctx.node != nullptr)
		{
			open_fact(*ctx.node, "type-alias " + node.text + " " +
			          types_.description(aliased), FactKind::TypeAlias);
		}
		return;
	}
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
	inner.node = ctx.node;

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
	// every line the body writes spells it the way the program will.  A class
	// defined in a function is named by the convention instead: 3.5p8 gives a
	// local class no linkage, so no other translation unit can name it and the
	// name a declarator would lend it says nothing about it.
	const bool local = ctx.scope->kind == ScopeKind::Block ||
		ctx.scope->kind == ScopeKind::Function;
	const std::string written =
		node.text.empty() ? (local ? std::string() : named_by) : node.text;
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
		// The dump spells a class by the named namespaces around it, which is
		// a fact about the declaration rather than about the use, so the type
		// carries it.
		const std::string qualified = dump_name(*ctx.scope, name);
		const TypeId type = types_.class_type(
			id, tag, semantics() ? qualified : name, qualified);
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
	// named after the terminals its declaration was written from, and one a
	// declarator in a function names is numbered among the classes the
	// translation unit defines in a function.
	std::string header = written;
	if (name.empty())
	{
		header = named_by.empty()
			? std::string("__anonymous_") +
				(tag == ClassTag::Union ? "union" : "class") + "_type__" +
				decimal(span.begin, false) + "_" + decimal(span.end, false)
			: "__local_type" + decimal(++local_types_, false);
		types_.rename(entity->type, header);
	}
	DumpScope& dump = model_.open_dump(*ctx.dump, "scope class " + header);
	Scope& scope = model_.open(ScopeKind::Class, *ctx.scope, entity, &dump);
	entity->scope = &scope;
	entity->defined = true;
	// 9.1p2: a member is named through its class, so the dump spells a member
	// declaration with the class before it, and the class with the named
	// namespaces around it, which is what its type already carries.
	scope.prefix = types_.user_name(entity->type) + "::";

	Context inner;
	inner.scope = &scope;
	inner.dump = &dump;
	// 9.2p2: the members of a class are declarations of it rather than of the
	// region it is written in, and the PA12 output describes what a function
	// body means, so a member declaration writes no line of its own.
	inner.node = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& member = *node.children[index];
		if (member.kind == AstKind::ClassKey)
		{
			continue;
		}
		if (member.kind == AstKind::BaseClause)
		{
			// 10p1 and 12.6.2p5: a base class is a subobject of every object of
			// this one, which its layout counts, a member name reaches through
			// and its constructor initializes.  PA12 models none of the three,
			// so a class with a base is refused rather than described as the
			// class it would be if the base-clause were not there.  PA11 only
			// spells the declaration and needs none of them.
			if (semantics())
			{
				throw std::runtime_error(header + " has a base class, which PA12 "
				                         "does not describe");
			}
			continue;
		}
		if (semantics() && member.kind == AstKind::BitFieldDeclaration)
		{
			// 9.6p1: a bit-field is a member whose width its declaration writes,
			// which the layout and every use of it read.  PA12 has no rule for
			// either, so the member would be missing from the class the output
			// describes.
			throw std::runtime_error(header + " declares a bit-field, which PA12 "
			                         "does not describe");
		}
		if (semantics() && (member.kind == AstKind::SpecialMemberDeclaration ||
		                    member.kind == AstKind::SpecialMemberDefinition))
		{
			// 12.1 and 12.4: a constructor, destructor or conversion function a
			// class declares is chosen by rules PA12 does not have, so the
			// output would describe neither the function nor the initialization
			// that calls it.  PA11 spells the declaration and needs neither.
			throw std::runtime_error(header + " declares " + member.text +
			                         ", which is a special member function PA12 "
			                         "does not describe");
		}
		declaration(member, inner);
	}
	lay_out_class(*entity, scope, tag == ClassTag::Union);
	if (semantics())
	{
		// 12.1p5: every class the output describes has the constructor no
		// declaration wrote, because a class that writes one is refused above.
		declare_constructor(*entity, scope);
	}
	return *entity;
}

// 12.1p5: a class with no declared constructor has a default one, which the
// course ABI gives the object it initializes as its only parameter.  It is
// declared where the definition of the class ends, because that is where 9.2p2
// makes the class complete and where every member it would initialize is known.
void SemaAnalyzer::declare_constructor(SemaEntity& entity, Scope& scope)
{
	std::vector<TypeId> parameters;
	parameters.push_back(types_.pointer_to(entity.type));
	// 9p1: a class is named by its own name wherever it is declared, so the
	// constructor of `N::C` is `N::C::C` and the constructor of a class no
	// declaration named is named after the name the convention gave it.
	const std::string spelled = QualifiedName(types_.user_name(entity.type)).last();
	SemaEntity& constructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), parameters, false));
	constructor.dump_name = scope.prefix + spelled;
	constructor.object_member = true;
	// 7.1.2p3 and 12.1p5: a constructor no declaration wrote is inline, so the
	// definition it is given belongs to every translation unit that needs one
	// rather than to the one that happened to write the class.
	constructor.inline_function = true;
	constructor.trivial = trivial_default_construction(scope);
	constructor.tail = &constructor;
	model_.declare_in(scope, constructor);
	entity.constructor = &constructor;
}

// 12.1p5: whether default-initializing an object of the class this region
// declares does nothing at all.  It does nothing when no member asks for
// anything: a member with a default member initializer is one action, and a
// member of class type is whatever its own constructor is.  Layout has already
// run, so the class's members are exactly the declarations of this region.
bool SemaAnalyzer::trivial_default_construction(Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &scope)
		{
			continue;
		}
		const SemaEntity* const constructor =
			default_constructor(element_of(member.type));
		if (constructor != nullptr && !constructor->trivial)
		{
			return false;
		}
	}
	return true;
}

// 8.3.4p1: the type one element of an array is, however many dimensions it
// has, which is the type whose construction the array's own asks for.
TypeId SemaAnalyzer::element_of(TypeId type)
{
	TypeId at = types_.strip_cv(type);
	while (types_.kind(at) == TypeKind::Array)
	{
		at = types_.strip_cv(types_.target(at));
	}
	return at;
}

void SemaAnalyzer::inject_union_members(SemaEntity* entity, const Context& ctx,
                                        const Span& span)
{
	// 9.5p1: a union with no name and no declarator declares its members in the
	// region it is written in rather than a region of its own.
	if (entity == nullptr || entity->scope == nullptr || !entity->name.empty() ||
	    !types_.is_class(entity->type) ||
	    types_.class_tag(entity->type) != ClassTag::Union)
	{
		return;
	}
	// 9.5p1: the union declares an object of itself that has no name either, so
	// a member is still a member of an object, and the convention names that
	// object after the terminals the declaration was written from, as it names
	// the union.
	SemaEntity* storage = nullptr;
	if (semantics())
	{
		const std::string name = "__anonymous_union_storage__" +
			decimal(span.begin, false) + "_" + decimal(span.end, false);
		storage = &model_.create(SemaKind::Variable, name, entity->type);
		model_.bind(*ctx.scope, name, *storage);
		model_.declare_in(*ctx.scope, *storage);
		storage->object_member = ctx.scope->kind == ScopeKind::Class;
		if (ctx.node != nullptr)
		{
			DumpNode& line = open_fact(*ctx.node, "variable " + name + " " +
			                           types_.description(entity->type),
			                           FactKind::Variable);
			line.fact.entity = storage;
			line.fact.type = entity->type;
			construct_object(*storage, line);
		}
		// 9.2p1: a union written in a class declares an object that is a member
		// of it, which the enclosing class initializes and which no line of its
		// own describes, as no other data member has one.
	}
	Scope& members = *entity->scope;
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (member.kind != SemaKind::Variable)
		{
			continue;
		}
		member.storage = storage;
		model_.bind(*ctx.scope, member.name, member);
		model_.declare_in(*ctx.scope, member);
		if (!semantics())
		{
			write_line(*ctx.dump, "variable", member.name, member.type);
		}
	}
}

// 9.3.1p3: a non-static member function is called on an object of its class,
// which is a parameter of it no declarator writes, cv-qualified as the
// cv-qualifier-seq written after its parameter-clause says.  Holding it in the
// type is what lets everything above read a member function as the function it
// is: its declaration, its definition and a pointer to it all see the same
// parameters.
TypeId SemaAnalyzer::with_object_parameter(TypeId type,
                                           const AstNode& declarator,
                                           const Context& target, bool is_static)
{
	if (!semantics() || target.scope->kind != ScopeKind::Class || is_static ||
	    target.scope->owner == nullptr)
	{
		return type;
	}
	// 8.3.5p5: the cv-qualifier-seq of a member function is written after its
	// parameter-clause, so it is a suffix of the declarator rather than one of
	// the qualifiers its specifiers wrote.
	unsigned cv = kCvNone;
	bool after_id = false;
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind == AstKind::Identifier ||
		    part.kind == AstKind::NestedDeclarator)
		{
			after_id = true;
			continue;
		}
		if (after_id && part.kind == AstKind::CvQualifier)
		{
			cv |= part.token == KW_CONST ? kCvConst : kCvVolatile;
		}
	}
	std::vector<TypeId> parameters;
	parameters.push_back(
		types_.pointer_to(types_.qualified(target.scope->owner->type, cv)));
	const std::vector<TypeId>& written = types_.parameters(type);
	parameters.insert(parameters.end(), written.begin(), written.end());
	return types_.function_of(types_.target(type), parameters,
	                          types_.variadic(type));
}

SemaEntity* SemaAnalyzer::default_constructor(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner == nullptr ? nullptr : owner->constructor;
}

// 8.5p6 and 12.1p5: an object of class type with no initializer is initialized
// by calling the default constructor of its class on it, which is one call like
// any other and which the dump writes under the declaration of the object.
void SemaAnalyzer::construct_object(SemaEntity& variable, DumpNode& line)
{
	if (!types_.is_class(types_.strip_cv(variable.type)))
	{
		// 8.5p6: default-initializing an object of any other type performs no
		// initialization, and there is nothing for the output to describe.
		return;
	}
	SemaEntity* const constructor = default_constructor(variable.type);
	if (constructor == nullptr)
	{
		// 3.9p6 and 9.2p2: an object needs a complete class, and 12.1p5 gives
		// every complete one the output describes a constructor, so a class
		// with none here is one this translation unit never defined.
		throw std::runtime_error("an object of the incomplete class type " +
		                         types_.description(variable.type) +
		                         " is declared");
	}
	if (!constructor->defined)
	{
		// 12.1p5: the definition is what odr-using the constructor asks for,
		// and one use is what asks for it.
		constructor->defined = true;
		Pending pending;
		pending.function = constructor;
		pending.self = &model_.create(
			SemaKind::Parameter, "this", types_.parameters(constructor->type)[0]);
		pending_.push_back(pending);
	}
	DumpNode& action =
		model_.open_node(line, "constructor-action " + constructor->dump_name);
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.entity = constructor;
	action.fact.type = variable.type;
	DumpNode& call = model_.open_node(
		action, spell("call-expression", ValueCategory::PRValue,
		              types_.target(constructor->type), std::string()));
	set_fact(call, FactKind::Call, types_.target(constructor->type),
	         ValueCategory::PRValue);
	DumpNode& callee =
		model_.open_node(call, "callee " + constructor->dump_name + " " +
		                 types_.description(constructor->type));
	set_fact(callee, FactKind::Callee, constructor->type,
	         ValueCategory::LValue);
	callee.fact.entity = constructor;
	const TypeId object = types_.pointer_to(variable.type);
	DumpNode& address = model_.open_node(
		call, spell("unary-expression", ValueCategory::PRValue, object, "OP_AMP:&"));
	set_fact(address, FactKind::Unary, object, ValueCategory::PRValue);
	address.fact.op = OP_AMP;
	DumpNode& named =
		model_.open_node(address, spell("id-expression", ValueCategory::LValue,
		                                variable.type, variable.name));
	set_fact(named, FactKind::Id, variable.type, ValueCategory::LValue);
	named.fact.entity = &variable;
}

// The typed facts of a node the analysis builds rather than reads: a
// constructor call has no expression in the source to be spelled from, and the
// lowering reads facts and never text.
void SemaAnalyzer::set_fact(DumpNode& node, FactKind kind, TypeId type,
                            ValueCategory category)
{
	node.fact.kind = kind;
	node.fact.type = type;
	node.fact.spelled = type;
	node.fact.category = category;
}

void SemaAnalyzer::lay_out_class(SemaEntity& entity, Scope& scope, bool is_union)
{
	unsigned long long size = 0;
	unsigned long long align = 1;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		// 9.4p2 makes a static data member a variable rather than part of an
		// object, and 9.5p1 records an anonymous union's members in this region
		// as well as in the union's; the object they are part of is the one the
		// union declared, which is counted here in their place.
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &scope)
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
			// 9.5p1: every member of a union begins where the union does.
			member.offset = 0;
			size = member_size > size ? member_size : size;
			continue;
		}
		// 9.2p13: the members are allocated in declaration order, each at the
		// next address its own alignment allows.
		member.offset = round_up(size, member_align);
		size = member.offset + member_size;
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

DumpNode& SemaAnalyzer::open_fact(DumpNode& parent, const std::string& text,
                                  FactKind kind)
{
	DumpNode& node = model_.open_node(parent, text);
	node.fact.kind = kind;
	return node;
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
		// 7.2: the dump spells an enumeration as its declaration wrote it, and
		// the regions around that declaration are what a name for it outside
		// them must carry, so the type holds both.
		const TypeId type = types_.enum_type(
			id, scoped, name, dump_name(*ctx.scope, name), underlying);
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
	unsigned long long widest = 0;
	bool signed_values = false;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::Enumerator)
		{
			continue;
		}
		unsigned long long value = next;
		bool negative = false;
		if (!child.children.empty())
		{
			// 7.2p1: the constant-expression of an enumerator-definition.
			Context inner;
			inner.scope = &scope;
			inner.dump = &dump;
			const Constant written = evaluate(*child.children[0], inner);
			value = written.bits;
			negative = is_signed(written.type) &&
				(value >> (width_of(written.type) - 1)) != 0;
		}
		next = value + 1;
		// 7.2p5: the range of the enumeration is the values its enumerators
		// have, which is what says which type represents them all.
		if (negative)
		{
			signed_values = true;
		}
		else if (value > widest)
		{
			widest = value;
		}

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
	// 4.5p3 and 7.2p5: the first of `int`, `unsigned int`, `long` and
	// `unsigned long` that represents every value the enumeration has, which is
	// what an operand of it is promoted to.
	EFundamentalType promotion = FT_INT;
	if (widest > 0x7FFFFFFFull)
	{
		if (widest <= 0xFFFFFFFFull && !signed_values)
		{
			promotion = FT_UNSIGNED_INT;
		}
		else if (widest <= 0x7FFFFFFFFFFFFFFFull)
		{
			promotion = FT_LONG_INT;
		}
		else
		{
			promotion = FT_UNSIGNED_LONG_INT;
		}
	}
	entity.promotion = types_.fundamental(promotion);
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
		inject_union_members(specifiers.introduced, ctx, span);
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
	// 14.1: a template's declarator is the pattern its instantiations write
	// their own parameters from, and 8.3.6p4 makes a function declaration's
	// default-arguments the function's from that declaration on, whether or not
	// it is the one with the body.  Both read the parameter clause the
	// declarator already spelled, so it is captured here rather than read again.
	std::vector<Parameter> spelled_parameters;
	TypeId type = declarator_type(node, specifier_type(specifiers), ctx, &written,
	                              &spelled_parameters);
	// 8.3.4p3: an array declared with no bound and initialized from a braced
	// list has as many elements as the list has clauses.
	if (types_.kind(type) == TypeKind::Array && !types_.bounded(type) &&
	    initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::BracedInitList)
	{
		type = types_.array_of(types_.target(type), true,
		                       initializer->children[0]->children.size());
	}
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
		if (semantics())
		{
			if (target.node != nullptr)
			{
				open_fact(*target.node, "type-alias " + name + " " +
				          types_.description(type), FactKind::TypeAlias);
			}
			return;
		}
		write_line(*target.dump, "type-alias", name, type);
		return;
	}
	if (types_.kind(type) == TypeKind::Function)
	{
		// 9.3.1p3: a member function is called on an object, which is a
		// parameter of it that the declarator does not write.
		const TypeId written_type = type;
		type = with_object_parameter(type, node, target, specifiers.is_static);
		SemaEntity& function = declare_function(name, type, target, false);
		function.object_member = type != written_type;
		function.internal_linkage = function.internal_linkage ||
			(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
		// 7.1.2p2: one declaration of a function with `inline` makes it inline,
		// so the fact accumulates over the declarations of one entity.
		function.inline_function =
			function.inline_function || specifiers.is_inline;
		record_default_arguments(function, spelled_parameters, target.scope);
		if (function.template_parameters != nullptr)
		{
			templates_[function.id].swap(spelled_parameters);
		}
		if (semantics())
		{
			// 14p1: a template is not a function; the unit has the ones its
			// instantiations declare, and the output describes those.
			if (target.node != nullptr &&
			    target.scope->kind != ScopeKind::TemplateParameters)
			{
				DumpNode& declared =
					open_fact(*target.node, "function-declaration " +
					          function.dump_name + " " +
					          types_.description(type),
					          FactKind::FunctionDeclaration);
				declared.fact.entity = &function;
				declared.fact.type = type;
			}
			return;
		}
		write_line(*target.dump, "function", name, type);
		return;
	}

	// 7.1.5p9: a constexpr object is a const object.
	if (specifiers.is_constexpr)
	{
		type = types_.qualified(type, kCvConst);
	}
	// 3.3.2 and 9.4.2p2: a declarator-id with a nested-name-specifier defines
	// the object that region already declares - a static data member is
	// declared in its class and defined outside it - rather than declaring a
	// second one there, so what its first declaration said about it stands.
	SemaEntity* const declared = spelled.qualified()
		? redeclared(target, name, SemaKind::Variable)
		: nullptr;
	SemaEntity& entity = declared != nullptr
		? *declared
		: model_.create(SemaKind::Variable, name, type);
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
	if (declared == nullptr)
	{
		entity.c_linkage = c_linkage_;
		model_.bind(*target.scope, name, entity);
		model_.declare_in(*target.scope, entity);
		// The qualified spelling a use of the name writes, built here as it is
		// for a function, because that is where the regions around it are known.
		entity.dump_name = dump_name(*target.scope, name);
		// 9.2p1: a data member is part of an object of its class and is reached
		// through one, which 9.4p2 makes untrue of a member declared `static`:
		// that one is a variable the class names.
		entity.object_member =
			target.scope->kind == ScopeKind::Class && !specifiers.is_static;
	}
	// 3.1p2: an `extern` declaration with no initializer declares the object
	// and does not define it; every other declaration of one at namespace scope
	// does, and a later definition of the same object says so once.  9.4.2p2
	// makes the declaration a static data member's class writes no definition
	// of it however it was written, so only one outside the class defines it.
	entity.object_definition = entity.object_definition ||
		(target.scope->kind != ScopeKind::Class &&
		 (!specifiers.is_extern ||
		  (initializer != nullptr && !initializer->children.empty())));
	// 3.5p3: at namespace scope a name declared `static` has internal linkage,
	// and so does a `const` object no declaration wrote `extern`.
	entity.internal_linkage = entity.internal_linkage ||
		(target.scope->kind == ScopeKind::Namespace &&
		 (specifiers.is_static ||
		  (!specifiers.is_extern &&
		   (types_.object_cv(type) & kCvConst) != 0)));
	// 9.4.2p2: a definition written with a nested-name-specifier declares
	// nothing where it names, so the line it writes is not one of that region's:
	// it stands where the definition is written, spelled the way it wrote it.
	// The region it names keeps the one line its own declaration wrote.
	const bool defines_elsewhere = declared != nullptr;
	const Context& where = defines_elsewhere ? ctx : target;
	const std::string& spelling = defines_elsewhere ? written : name;
	if (!semantics())
	{
		write_line(*where.dump, "variable", spelling, type);
		return;
	}
	if (where.node == nullptr)
	{
		// 9.2p1: a data member declares no object of its own; the object it is
		// part of is what a declaration of the class type declares.
		return;
	}
	DumpNode& line = open_fact(*where.node, "variable " + spelling + " " +
	                           types_.description(type), FactKind::Variable);
	line.fact.entity = &entity;
	line.fact.type = type;
	// 5.19p3 and 7.1.5p9: a constexpr object is initialized by a constant
	// expression, and the dump writes the value it stands for rather than the
	// expression that computed it.
	if (initializer == nullptr || initializer->children.empty())
	{
		// 8.5p6: an object with no initializer is default-initialized, which
		// for a class type is a call of its default constructor.
		construct_object(entity, line);
		return;
	}
	if (entity.constant && specifiers.is_constexpr)
	{
		model_.open_node(line, spell("literal", ValueCategory::PRValue, type,
		                             spell_value(type, entity.value)));
		return;
	}
	write_initializer(*initializer->children[0], type, ctx, line);
}

void SemaAnalyzer::write_initializer(const AstNode& initializer, TypeId type,
                                     const Context& ctx, DumpNode& line)
{
	if (initializer.kind == AstKind::ParenInitializer)
	{
		// 8.5p16: direct-initialization from one expression, which for the PA12
		// subset is the same conversion copy-initialization asks for.
		if (!initializer.children.empty())
		{
			initialize(*initializer.children[0], type, ctx, line);
		}
		return;
	}
	// 8.5.1: an aggregate is initialized from the clauses of its list, each
	// initializing one element, which is the same reading a list standing
	// where an expression initializes an object gets.
	initialize(initializer, type, ctx, line);
}

void SemaAnalyzer::declare_parameters(const std::vector<Parameter>& parameters,
                                      TypeId type, const Context& inner,
                                      DumpNode* node, std::size_t implicit)
{
	// 8.4.1p1: the parameters the declarator's own parameter-clause declared,
	// which the type it built already read.  The line writes the adjusted type
	// 8.3.5p5 put in the function type, while the object keeps the type it was
	// declared with.  9.3.1p3 put the implicit object parameter before them, so
	// the two lists start apart.
	const std::vector<TypeId>& adjusted = types_.parameters(type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		SemaEntity& parameter = model_.create(
			SemaKind::Parameter, parameters[index].name, parameters[index].type);
		if (!parameter.name.empty())
		{
			model_.bind(*inner.scope, parameter.name, parameter);
		}
		model_.declare_in(*inner.scope, parameter);
		const TypeId written = index + implicit < adjusted.size()
			? adjusted[index + implicit]
			: parameters[index].type;
		if (node != nullptr)
		{
			DumpNode& line = open_fact(*node, "parameter " + parameter.name + " " +
			                           types_.description(written),
			                           FactKind::Parameter);
			line.fact.entity = &parameter;
			line.fact.type = written;
			continue;
		}
		write_line(*inner.dump, "parameter", parameter.name, parameter.type);
	}
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
	TypeId type = declarator_type(declarator, specifier_type(specifiers),
	                              target, &ignored, &parameters);
	if (types_.kind(type) != TypeKind::Function)
	{
		throw std::runtime_error("a function definition declares " + name +
		                         ", which is not a function");
	}
	// 9.3.1p3: a member function is called on an object its declarator does not
	// write, whether it is defined in its class or after it.
	const TypeId written_type = type;
	type = with_object_parameter(type, declarator, target, specifiers.is_static);

	SemaEntity& entity = declare_function(name, type, target, true);
	entity.object_member = type != written_type;
	// 3.5p3: one declaration written `static` gives the name internal linkage,
	// however the others were written.
	entity.internal_linkage = entity.internal_linkage ||
		(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
	// 7.1.2p2 and 9.3p2: `inline` says so, and so does defining a member
	// function inside the class that declares it.
	entity.inline_function = entity.inline_function || specifiers.is_inline ||
		target.scope->kind == ScopeKind::Class;
	record_default_arguments(entity, parameters, target.scope);

	DumpScope& dump = model_.open_dump(*target.dump, "scope function " + name);
	Context inner;
	inner.scope = &model_.open(ScopeKind::Function, *target.scope, &entity, &dump);
	inner.dump = &dump;
	inner.node = ctx.node;

	if (!semantics())
	{
		write_line(*target.dump, "function", name, type);
		declare_parameters(parameters, type, inner, nullptr);
		for (std::size_t index = 2; index < node.children.size(); ++index)
		{
			statement(*node.children[index], inner);
		}
		return;
	}

	// 9.3.1p3 and 9.2p2: a member function is called on an object, which is
	// declared in the region its body reads names in, and its body is read
	// where the class is complete rather than where it is written.
	SemaEntity* self = nullptr;
	if (entity.object_member)
	{
		self = &model_.create(SemaKind::Parameter, "this",
		                      types_.parameters(type)[0]);
		model_.bind(*inner.scope, self->name, *self);
		model_.declare_in(*inner.scope, *self);
	}
	if (target.scope->kind == ScopeKind::TemplateParameters)
	{
		// 14p1 and 14.6: a template declares no function until it is
		// instantiated, so the output has no definition to write and the body
		// is not read against the types it has none of yet.
		return;
	}
	if (target.node == nullptr)
	{
		// 9.2p2: a member function defined in its class is read where the class
		// is complete, which is the end of the translation unit, and the output
		// writes it there.
		Pending pending;
		pending.function = &entity;
		pending.self = self;
		pending.body = &node;
		pending.scope = inner.scope;
		pending.parameters = parameters;
		pending_.push_back(pending);
		return;
	}

	DumpNode& line = open_fact(*target.node, "function-definition " +
	                           entity.dump_name + " " +
	                           types_.description(type),
	                           FactKind::FunctionDefinition);
	line.fact.entity = &entity;
	line.fact.type = type;
	if (self != nullptr)
	{
		// A member function defined after its class is written where it is
		// written, and the object it is called on is still its first parameter.
		DumpNode& object = open_fact(line, "parameter " + self->name + " " +
		                             types_.description(self->type),
		                             FactKind::Parameter);
		object.fact.entity = self;
		object.fact.type = self->type;
	}
	declare_parameters(parameters, type, inner, &line, self != nullptr ? 1 : 0);

	// 6.6.3, 6.6.1 and 6.6.2 are facts about the function being read, so the
	// walk of one body neither sees nor leaves behind what encloses it.
	SemaEntity* const enclosing_self = self_;
	self_ = self;
	const TypeId enclosing_return = returns_;
	const unsigned breakable = breakable_;
	const unsigned continuable = continuable_;
	const unsigned switches = switches_;
	returns_ = types_.target(type);
	breakable_ = 0;
	continuable_ = 0;
	switches_ = 0;
	labels_.clear();
	gotos_.clear();
	for (std::size_t index = 2; index < node.children.size(); ++index)
	{
		semantic_statement(*node.children[index], inner, line);
	}
	// 6.6.4p1: every label a goto names is one the function writes.
	for (std::size_t index = 0; index < gotos_.size(); ++index)
	{
		if (labels_.count(gotos_[index]) == 0)
		{
			throw std::runtime_error("a goto statement names " + gotos_[index] +
			                         ", which labels no statement of the "
			                         "function");
		}
	}
	self_ = enclosing_self;
	returns_ = enclosing_return;
	breakable_ = breakable;
	continuable_ = continuable;
	switches_ = switches;
}

SemaEntity& SemaAnalyzer::declare_function(const std::string& name, TypeId type,
                                           const Context& target, bool define)
{
	// 14.1p1: the region a template's parameters are declared in encloses only
	// the declaration they parameterise, so the function that declaration
	// declares is declared in the region around it, which is where a call of it
	// looks and where its other declarations are.
	Scope& where = declaring_region(*target.scope);
	SemaEntity* head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind != SemaKind::Function)
	{
		head = nullptr;
	}
	const std::uint32_t signature = types_.signature(type);
	// 1.3.11 and 13.1: two declarations declare the same function exactly when
	// their parameter type lists agree, which 8.3.5p5 has already normalised.
	// The chain the name heads is indexed by that list, so the question is a
	// probe rather than a walk of the declarations already made.
	SemaEntity* const prior =
		head == nullptr ? nullptr : model_.overload_of(*head, signature);
	if (prior != nullptr)
	{
		if (prior->type != type)
		{
			throw std::runtime_error("two declarations of " + name +
			                         " differ only in their return type");
		}
		if (define && prior->defined)
		{
			throw std::runtime_error(name + " is defined twice");
		}
		prior->defined = prior->defined || define;
		return *prior;
	}

	SemaEntity& entity = model_.create(SemaKind::Function, name, type);
	entity.dump_name = dump_name(where, name);
	entity.defined = define;
	entity.c_linkage = c_linkage_;
	entity.tail = &entity;
	if (target.scope->kind == ScopeKind::TemplateParameters)
	{
		// 14p1: this declares a template rather than a function, and the
		// parameters it is written over are what an instantiation of it
		// substitutes arguments for.
		entity.template_parameters = target.scope;
	}
	if (head != nullptr)
	{
		head->tail->next = &entity;
		head->tail = &entity;
	}
	else
	{
		head = &entity;
		model_.bind(where, name, entity);
	}
	model_.hold_overload(*head, signature, entity);
	model_.declare_in(where, entity);
	return entity;
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
