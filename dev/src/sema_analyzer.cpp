#include "sema_analyzer.h"

#include <ostream>
#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

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

}

SemaAnalyzer::SemaAnalyzer(SemaDialect dialect)
	: reading_(nullptr)
	, dialect_(dialect)
	, packs_(nullptr)
	, anonymous_enums_(0)
	, local_types_(0)
	, self_(nullptr)
	, naming_(nullptr)
	, breakable_(0)
	, continuable_(0)
	, switches_(0)
	, live_destructions_(0)
	, returns_(kNoType)
	, standard_only_(false)
	, c_linkage_(false)
{}

SemaAnalyzer::Pending::Pending()
	: function(nullptr)
	, self(nullptr)
	, body(nullptr)
	, scope(nullptr)
	, initializers(nullptr)
	, members(nullptr)
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
	// 13.3.1p4: an object argument no member call built names an object, and
	// every object a name reaches is an lvalue.
	, object_category(ValueCategory::LValue)
	, through_using(false)
	, braced(nullptr)
	, listed_class(kNoType)
{}

SemaAnalyzer::Match::Match()
	: viable(false)
	, rank(0)
	, to_bool(false)
	, reference(false)
	, binds_rvalue_ref(false)
	, binds_lvalue(false)
	, qualified(kNoType)
	, materialized(kNoType)
	, to_base(nullptr)
	, converting(nullptr)
	, converted(nullptr)
	, second_rank(0)
	, list_class(kNoType)
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

// 3.5p4 and 7.3.1.1p1: the same name with the one region the dump leaves out
// written in - the unnamed namespace, which belongs to this translation unit
// and which the object file has to spell so that another unit's is a different
// entity.  Where no unnamed namespace stands around the region, the two names
// are the same string and only one of them is kept.
std::string SemaAnalyzer::abi_name(const Scope& scope,
                                   const std::string& name) const
{
	if (scope.abi_prefix.empty())
	{
		return dump_name(scope, name);
	}
	return scope.kind == ScopeKind::Namespace || scope.kind == ScopeKind::Class
		? scope.abi_prefix + name
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
		// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation
		// functions are declared in the global namespace of every translation
		// unit whether or not the unit wrote them, so they are bound before it
		// is read.  A program that writes one of the four writes another
		// declaration of *this* function, which is what lets 17.6.4.6's
		// replacement be a definition of it rather than a second function of
		// the same name.  Nothing is emitted for a declaration no use reaches.
		declare_allocation_functions(*ctx.scope);
		// 3.4.1p8 and 9.3p2: a member defined outside its class settles facts a
		// body written before it already asks about, and the syntax of the
		// whole unit is in hand here.
		collect_unit_definitions(unit);
	}
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index], ctx);
	}
	// 3.6.3p1: the objects with static storage duration this unit constructed
	// are destroyed when the program ends, in the reverse order of their
	// construction.  Asking for the destructors here is what makes their
	// definitions part of the run of pending ones written below.
	for (std::size_t index = static_lifetimes_.size(); index-- > 0;)
	{
		destructor_action(*static_lifetimes_[index], *ctx.node, Placement::Named);
	}
	// 3.7.2p2: an object with thread storage duration is destroyed when its own
	// thread ends, which is a point the program hands to the runtime where the
	// object is initialized.  The action stands under the declaration, after
	// everything else that declaration wrote, and in declaration order: each
	// thread ends its own objects in the reverse of the order it began them,
	// which is what the runtime it was handed them in that order does.
	for (std::size_t index = 0; index < thread_lifetimes_.size(); ++index)
	{
		destructor_action(*thread_lifetimes_[index].entity,
		                  *thread_lifetimes_[index].line, Placement::Named);
	}
	write_pending_definitions();
	// 12.6.2p6: every delegation this unit wrote is settled now, so the chain
	// each one heads is walked here rather than at each definition, where a
	// constructor further along may not have been read yet.
	check_delegation_cycles();
}

bool SemaAnalyzer::accepts_arity(const SemaEntity& function,
                                 std::size_t given) const
{
	const std::size_t declared = types_.parameters(function.type).size();
	if (given >= declared)
	{
		return true;
	}
	// 8.3.6p4 and 7.3.3p1: a default-argument belongs to the declaration that
	// wrote it, which for a member a using-declaration brought into a class is
	// the base's - and a later declaration of the base's function may add one,
	// so the question is asked of that declaration rather than of a copy made
	// where the using-declaration stood.
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(declared_member(function).id);
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
	// 7.3.3p1: the default-argument stands on the declaration the base wrote,
	// which is the one this call runs.
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(declared_member(function).id);
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
	// 8.3.6p1: the call is made as if the default-argument stood where the
	// argument is missing, so a temporary it makes is storage that argument
	// asked for, named like every other argument's.
	initialize(*written.children[0]->children[0],
	           types_.parameters(function.type)[index], where, parent, false,
	           Requested::Argument);
}

// 12.4 and the ABI: which of the destructor's two entry points a use of it
// names - the base-object one for a base class subobject, and the
// complete-object one for every other object - and, for an implicitly declared
// one, the definition 12.4p6 says odr-using it asks for.  Every end of a
// lifetime asks the same two things, whether a statement writes the call or
// 15.2p2 leaves it to the cleanup around a partly built object, so both are
// settled here rather than beside each of the places that end one.
void SemaAnalyzer::note_destruction_entry(SemaEntity& destructor, bool base)
{
	if (base)
	{
		destructor.base_object_entry = true;
	}
	else
	{
		destructor.complete_object_entry = true;
	}
	if (destructor.defined || !destructor.defaulted)
	{
		return;
	}
	destructor.defined = true;
	Pending pending;
	pending.function = &destructor;
	pending.self =
		&model_.create(SemaKind::Parameter, "this", this_type(destructor));
	pending.members = destructor.region;
	pending_.push_back(pending);
}

// 15.2p2: the destructor an exception out of a later step of the constructor
// would run on the subobject a step has just built.  Which steps have one after
// them is a question about the whole list, so the steps are collected in order
// and the caller asks it once the list is complete.  An array is as many steps
// as it has elements, and each element but the last is left standing by the one
// after it, so an array of more than one element stands for two.
void SemaAnalyzer::record_unwind_subobject(TypeId type)
{
	SemaEntity* const ends = class_destructor(element_of(type));
	if (ends == nullptr || ends->trivial || ends->deleted)
	{
		return;
	}
	const TypeId bare = types_.strip_cv(type);
	unwind_subobjects_.push_back(ends);
	if (types_.kind(bare) == TypeKind::Array &&
	    types_.object_size(bare) >
	        types_.object_size(types_.strip_cv(element_of(bare))))
	{
		unwind_subobjects_.push_back(ends);
	}
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
	                           function_description(function.type,
	                                                function.object_member),
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
	Context inner;
	// 12.1p5: a definition no declaration wrote has no region of its own that
	// its names are read in, and the class it belongs to is what its member
	// initializations are read against.
	inner.scope = pending.scope != nullptr ? pending.scope : pending.members;
	if (inner.scope != nullptr)
	{
		inner.dump = inner.scope->dump;
		inner.node = &model_.unit();
	}
	if (pending.scope != nullptr)
	{
		declare_parameters(pending.parameters, function.type, inner, &line,
		                   pending.self != nullptr ? 1 : 0);
	}
	std::vector<SemaEntity*> enclosing_parameters;
	enclosing_parameters.swap(parameter_objects_);
	// 5.2.2p4: a definition read here owes the end of a parameter of class type
	// exactly as one read where it was written does - a constructor, a member
	// function defined in its class body and a member the standard defined are
	// all handed an object the caller built - so the lines just written are read
	// for those parameters here too.
	open_parameter_lifetimes(line);

	// 9.2p2: the body is read where the class is complete, which is here, so
	// what the walk of the class left behind is put back for it.
	SemaEntity* const enclosing_self = self_;
	const TypeId enclosing_return = returns_;
	const unsigned breakable = breakable_;
	const unsigned continuable = continuable_;
	const unsigned switches = switches_;
	std::vector<std::vector<SemaEntity*> > enclosing_lifetimes;
	enclosing_lifetimes.swap(lifetimes_);
	// The frames a break or a continue leaves are depths into `lifetimes_`, so
	// they belong to the body that opened them and not to this one.
	std::vector<std::size_t> enclosing_breaks;
	std::vector<std::size_t> enclosing_continues;
	enclosing_breaks.swap(breakable_frames_);
	enclosing_continues.swap(continuable_frames_);
	const std::size_t enclosing_live = live_destructions_;
	live_destructions_ = 0;
	self_ = pending.self;
	returns_ = types_.target(function.type);
	breakable_ = 0;
	continuable_ = 0;
	switches_ = 0;
	if (function.special == kConstructorFunction && pending.members != nullptr)
	{
		// 12.6.2p10: the members are initialized before the body runs.
		std::vector<SemaEntity*> enclosing_subobjects;
		enclosing_subobjects.swap(unwind_subobjects_);
		write_member_initializations(pending, line, inner);
		for (std::size_t index = 0; index + 1 < unwind_subobjects_.size();
		     ++index)
		{
			// 15.2p2: a step with a step after it leaves a built subobject
			// behind wherever that later one throws, so its destructor is
			// odr-used here.  The last step leaves nothing behind: the body
			// after it is not a region the references write a handler around.
			note_destruction_entry(*unwind_subobjects_[index], false);
		}
		unwind_subobjects_.swap(enclosing_subobjects);
	}
	else if (function.transfer != kNotTransfer && function.defaulted &&
	         pending.members != nullptr)
	{
		// 12.8p28: an assignment operator the standard defines assigns each
		// subobject of the object it is called on from the corresponding
		// subobject of the one it is passed, before its body - which is empty -
		// runs.
		write_transfer_steps(pending, line, inner);
	}
	if (pending.body != nullptr)
	{
		semantic_statement(*pending.body->children.back(), inner, line);
	}
	else
	{
		// 12.1p5 and 12.4p3: a definition no declaration wrote has a body that
		// does nothing beyond what the standard already said it does.
		open_fact(line, "compound-statement", FactKind::Compound);
	}
	if (function.transfer != kNotTransfer && function.defaulted &&
	    function.special != kConstructorFunction && pending.members != nullptr)
	{
		// 12.8p28: the assignment hands back the object it wrote into, which is
		// the lvalue `*this` denotes.
		DumpNode& returned = open_fact(line, "return-statement", FactKind::Return);
		Value self = this_value(returned);
		DumpNode& held = model_.wrap_node(*self.node, std::string());
		held.text = spell("unary-expression", ValueCategory::LValue,
		                  types_.target(self.type), "*");
		set_fact(held, FactKind::Unary, types_.target(self.type),
		         ValueCategory::LValue);
		held.fact.op = OP_STAR;
	}
	if (function.special == kDestructorFunction && pending.members != nullptr)
	{
		// 12.4p8: after the body, the members are destroyed.
		write_member_destructions(*pending.members, line);
	}
	// 6.6.3p2 and 3.8p1: control reaching the end of the definition leaves the
	// function as a return does, and what the parameters the boundary handed it
	// owe is owed after everything else the definition writes.
	end_parameter_lifetimes(line);
	parameter_objects_.swap(enclosing_parameters);
	self_ = enclosing_self;
	returns_ = enclosing_return;
	breakable_ = breakable;
	continuable_ = continuable;
	switches_ = switches;
	lifetimes_.swap(enclosing_lifetimes);
	breakable_frames_.swap(enclosing_breaks);
	continuable_frames_.swap(enclosing_continues);
	live_destructions_ = enclosing_live;
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
		// A class-specifier that is a whole declaration wrote no
		// decl-specifier-seq, so 9.5p3's `static` is not among what it says.
		inject_anonymous_members(
			&class_declaration(node, ctx, span,
			                   node.kind == AstKind::ClassSpecifier,
			                   std::string()),
			ctx, span, false);
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

	case AstKind::SpecialMemberDefinition:
	case AstKind::SpecialMemberDeclaration:
		// 9.3p2: a constructor or a destructor defined outside its class, whose
		// declarator-id names the class it belongs to.  A definition written in
		// a class body is read where that body is, so one reaching here is
		// written outside every class.  8.4.2p2's `= default` is a definition
		// like any other: what differs is that the standard writes the body.
		if (semantics())
		{
			special_member_definition(node, ctx);
		}
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
		const bool unnamed = is_unnamed_namespace(node);
		entity->scope->prefix = unnamed
			? ctx.scope->prefix
			: ctx.scope->prefix + node.text + "::";
		// 3.5p4 and the ABI: what the dump leaves out the object file cannot,
		// because two units each writing an unnamed namespace declare two
		// entities and the names have to differ.  The ABI's name for the region
		// is `_GLOBAL__N_1`, and every region inside one carries it on.
		entity->scope->unnamed_region =
			unnamed || ctx.scope->unnamed_region;
		if (entity->scope->unnamed_region)
		{
			const std::string& outer = ctx.scope->abi_prefix.empty()
				? ctx.scope->prefix
				: ctx.scope->abi_prefix;
			entity->scope->abi_prefix =
				outer + (unnamed ? "_GLOBAL__N_1" : node.text) + "::";
		}
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
	if (ctx.scope->kind == ScopeKind::Class && ctx.scope->owner != nullptr)
	{
		// 7.3.3p1 read in a class: what the using-declaration makes is a
		// declaration of this class, not a second name for the base's.  The
		// access 11p1 gave it and the hiding 7.3.3p14 asks about are facts
		// about this class's declaration, and the base's is what a use of it
		// reaches.
		if (entity.kind == SemaKind::Class && written.qualified() &&
		    entity.scope == resolve_prefix(written, ctx))
		{
			// 12.9p1: the unqualified-id names the class the
			// nested-name-specifier named, so what it names is that class's
			// constructors rather than a member of it.
			if (ctx.scope->owner->base != &entity)
			{
				throw std::runtime_error(
					"a using-declaration names the constructors of a class "
					"that is not a direct base of the one it is written in");
			}
			// 12.9p1: which of the base's constructors are inherited is a
			// question about the complete class - it leaves out the ones this
			// class declares itself, and a member declaration standing after
			// this one declares them just as one standing before it does.  So
			// the class records that it asked, and 9.2p2's completion settles
			// it.
			ctx.scope->inheriting_constructors = true;
			return;
		}
		declare_using_members(entity, *ctx.scope, name);
		write_entity_line(*ctx.dump, entity);
		return;
	}
	model_.bind(*ctx.scope, name, entity);
	model_.declare_in(*ctx.scope, entity);
	write_entity_line(*ctx.dump, entity);
}

std::uint32_t SemaAnalyzer::member_signature(const SemaEntity& function)
{
	return member_signature(function.type, function.object_member);
}

// 8.3.5p1, 8.3.5p7 and 9.3.1p3: how a function's type is spelled in the output.
//
// The two qualifiers a declarator writes after its parameter-clause are part of
// the function type it wrote, which is what a typedef, a pointer to function and
// the function type a pointer to member points to each go on holding and what
// their descriptions spell.  Where 9.3.1p3 has already made the object the first
// parameter, the cv-qualifier-seq is spelled as that parameter's own - and the
// ref-qualifier, which the type goes on carrying because 13.1 tells `f() &` from
// `f() &&` by it, is not spelled a second time beside it.
TypeId SemaAnalyzer::function_description_type(TypeId type, bool object_member)
{
	return object_member
		? types_.ref_qualified_function(type, RefQualifier::None)
		: type;
}

std::string SemaAnalyzer::function_description(TypeId type, bool object_member)
{
	return types_.description(function_description_type(type, object_member));
}

std::uint32_t SemaAnalyzer::member_signature(TypeId type, bool object_member)
{
	const std::vector<TypeId>& written = types_.parameters(type);
	std::vector<TypeId> list;
	list.reserve(written.size());
	// 9.3.1p3: the object parameter is one this milestone put in the type and
	// no declarator wrote, so it is no part of 8.3.5p4's parameter-type-list -
	// which is why a static member function and a non-static one declare the
	// same list when their declarators wrote the same parameters.  What the
	// object parameter says about the declaration itself is 8.3.5p7's
	// cv-qualifier-seq, and a static member function wrote none, so that stands
	// beside the list as its own step rather than in place of a parameter.
	for (std::size_t index = object_member ? 1u : 0u; index < written.size();
	     ++index)
	{
		list.push_back(written[index]);
	}
	// Where 9.3.1p3's lowering has not run - PA11 describes what the declarator
	// wrote and adds no object parameter - the cv-qualifier-seq is still on the
	// function type itself, so the one step reads it from wherever it stands.
	TypeId object = types_.qualified(
		types_.fundamental(FT_VOID),
		object_member ? types_.object_cv(types_.target(written[0]))
		              : types_.cv(type));
	// 8.3.5p1 and 13.1: a ref-qualifier is as much a part of what tells two
	// declarations of one member apart as the cv-qualifier-seq beside it, so it
	// stands in the same step: `f() &` and `f() &&` are two declarations of one
	// name, and a definition written outside the class matches the declaration
	// whose qualifiers it repeats rather than colliding with it.
	const RefQualifier ref = types_.function_ref_qualifier(type);
	if (ref != RefQualifier::None)
	{
		object = types_.reference_to(object, ref == RefQualifier::RValue);
	}
	list.push_back(object);
	return (types_.type_list(list) << 1) | (types_.variadic(type) ? 1u : 0u);
}

void SemaAnalyzer::hide_using_members(Scope& where)
{
	for (std::size_t index = 0; index < where.using_names.size(); ++index)
	{
		const std::string& name = where.using_names[index];
		SemaEntity* const head = model_.find(where, name, LookupKind::Any);
		if (head == nullptr || head->kind != SemaKind::Function)
		{
			continue;
		}
		// 7.3.3p14: what this class declared itself hides what the base
		// declared with the same name and parameter-type-list rather than
		// conflicting with it, whichever of the two the class body wrote
		// first.  The declarations of the name are what the source wrote, so
		// one pass says which signatures the class has of its own and one more
		// takes the brought-in declarations of those signatures off the chain.
		std::unordered_set<std::uint32_t> declared;
		bool brought_in = false;
		for (SemaEntity* at = head; at != nullptr; at = at->next)
		{
			if (at->shadowed != nullptr)
			{
				brought_in = true;
			}
			else
			{
				declared.insert(member_signature(*at));
			}
		}
		if (!brought_in || declared.empty())
		{
			continue;
		}
		SemaEntity* kept = nullptr;
		SemaEntity* tail = nullptr;
		for (SemaEntity* at = head; at != nullptr;)
		{
			SemaEntity* const next = at->next;
			at->next = nullptr;
			if (at->shadowed == nullptr ||
			    declared.count(member_signature(*at)) == 0)
			{
				if (kept == nullptr)
				{
					kept = at;
				}
				else
				{
					tail->next = at;
				}
				tail = at;
			}
			at = next;
		}
		kept->tail = tail;
		if (kept == head)
		{
			continue;
		}
		// What the name was bound to was one of the declarations hidden, so
		// the name now binds what is left - and 13.1's index of the chain is
		// keyed by the declaration the name binds, so what stays is keyed
		// again under it.  A declaration a using-declaration brought in is one
		// 7.3.3p14 hides rather than one 13.1 redeclares, so it stays out of
		// that index exactly as it was when the using-declaration made it.
		model_.bind(where, name, *kept);
		for (SemaEntity* at = kept; at != nullptr; at = at->next)
		{
			if (at->shadowed == nullptr)
			{
				model_.hold_overload(
					*kept, declaration_signature(where, at->type,
					                             at->object_member), *at);
			}
		}
	}
}

void SemaAnalyzer::declare_using_members(SemaEntity& named, Scope& where,
                                         const std::string& name)
{
	// 7.3.3p14: a member function this class declared itself hides the one the
	// base declared with the same name and parameter list rather than
	// conflicting with it, so what is brought in is what the class does not
	// already declare.  The declarations of the name in each region are what
	// the source wrote, so this costs one pass over them and no search.
	std::unordered_set<std::uint32_t> declared;
	for (SemaEntity* at = model_.find(where, name, LookupKind::Any);
	     at != nullptr && at->kind == SemaKind::Function; at = at->next)
	{
		declared.insert(member_signature(*at));
	}
	for (SemaEntity* at = &named; at != nullptr; at = at->next)
	{
		if (at->kind != SemaKind::Function)
		{
			// A name bound to anything else is bound to one declaration, so
			// there is no chain to walk and nothing of the class's own that it
			// could be hiding.
			declare_using_member(*at, where, name);
			return;
		}
		if (declared.insert(member_signature(*at)).second)
		{
			declare_using_member(*at, where, name);
		}
	}
}

SemaEntity& SemaAnalyzer::declare_using_member(SemaEntity& target, Scope& where,
                                               const std::string& name)
{
	SemaEntity& shadow = model_.create(target.kind, name, target.type);
	const std::uint32_t id = shadow.id;
	// The declaration says the same thing about the entity the base declared as
	// the base's own does - it is the same member of the same class, laid out
	// where the base laid it out - so it is copied whole, and what is a fact
	// about this declaration alone is written over it.
	shadow = target;
	shadow.id = id;
	shadow.name = name;
	shadow.next = nullptr;
	shadow.tail = &shadow;
	shadow.region = nullptr;
	shadow.access = kPublicAccess;
	shadow.shadowed =
		target.shadowed != nullptr ? target.shadowed : &target;
	name_in_region(shadow, where, name);
	if (shadow.kind == SemaKind::Function && shadow.object_member &&
	    where.owner != nullptr)
	{
		// 13.3.3.1p4: a non-conversion function a using-declaration brought into
		// a derived class is a member of that class where the type of the
		// implicit object parameter is concerned, which is what ranks it against
		// the class's own declarations on an object of the class.  9.3.1p3 put
		// that parameter in the type, so this is where the rule is written; what
		// the call passes is still the base subobject, because the function it
		// runs is the base's.
		const std::vector<TypeId>& written = types_.parameters(target.type);
		std::vector<TypeId> parameters;
		parameters.push_back(types_.pointer_to(types_.qualified(
			where.owner->type, types_.object_cv(types_.target(written[0])))));
		for (std::size_t index = 1; index < written.size(); ++index)
		{
			parameters.push_back(written[index]);
		}
		// 8.3.5p1: only the implicit object parameter is the derived class's;
		// the ref-qualifier written after the parameter-clause is part of the
		// function type the base declared, so it travels with the rest of it -
		// which is what 13.3.1p4 reads to say the brought-in declaration is
		// viable, and what 7.3.3p14 reads to say the derived class's own
		// declaration of the same spelling hides it.
		shadow.type = types_.ref_qualified_function(
			types_.function_of(types_.target(target.type), parameters,
			                   types_.variadic(target.type)),
			types_.function_ref_qualifier(target.type));
	}
	SemaEntity* const head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind == SemaKind::Function &&
	    shadow.kind == SemaKind::Function)
	{
		// 13.1: the declarations of one name in one region are one chain, and
		// 13.3 ranks what a using-declaration brought in beside the class's own.
		head->tail->next = &shadow;
		head->tail = &shadow;
	}
	else
	{
		model_.bind(where, name, shadow);
	}
	if (shadow.kind == SemaKind::Function)
	{
		// 7.3.3p14's hiding is asked of this name where the class is complete,
		// and this is what says the class has one to ask about.  A class writes
		// the using-declarations it writes, so holding them is what the source
		// wrote and no search.
		bool held = false;
		for (std::size_t index = 0;
		     !held && index < where.using_names.size(); ++index)
		{
			held = where.using_names[index] == name;
		}
		if (!held)
		{
			where.using_names.push_back(name);
		}
	}
	model_.declare_in(where, shadow);
	return shadow;
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
	const QualifiedName spelled(written);
	const std::string name = spelled.last();

	SemaEntity* entity = nullptr;
	if (spelled.qualified())
	{
		// 9.1p2 and 3.4.3p3: a class-head-name with a nested-name-specifier
		// defines the class that region already declared, wherever the
		// definition is written, rather than declaring a second class of that
		// name where it stands.
		entity = &require(model_.lookup_in(*resolve_prefix(spelled, ctx), name,
		                                   LookupKind::Type),
		                  written);
		if (entity->kind != SemaKind::Class)
		{
			throw std::runtime_error("a class definition names " + written +
			                         ", which is not a class");
		}
	}
	else if (!name.empty())
	{
		entity = redeclared(ctx, name, SemaKind::Class);
	}
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
		// 3.5p4: the object file names it by the regions the ABI writes, which
		// is the same string but for 7.3.1.1p1's unnamed namespace - so the
		// type carries both, and every name encoded from a use of it reads the
		// second.
		const TypeId type = types_.class_type(
			id, tag, semantics() ? qualified : name, abi_name(*ctx.scope, name));
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
		types_.rename(entity->type, header, abi_name(*ctx.scope, header));
	}
	DumpScope& dump = model_.open_dump(*ctx.dump, "scope class " + header);
	Scope& scope = model_.open(ScopeKind::Class, *ctx.scope, entity, &dump);
	entity->scope = &scope;
	entity->defined = true;
	// 9.1p2: a member is named through its class, so the dump spells a member
	// declaration with the class before it, and the class with the named
	// namespaces around it, which is what its type already carries.
	scope.prefix = types_.user_name(entity->type) + "::";
	// 3.5p4: the object file names a member by its class and the class by the
	// regions around it, one of which - 7.3.1.1p1's unnamed namespace - the
	// dump leaves out.  The type is what carries that second spelling, so a
	// member is named from the class rather than from the region the
	// definition of the class happens to stand in.
	if (scope.unnamed_region)
	{
		scope.abi_prefix = types_.user_qualified_name(entity->type) + "::";
	}

	Context inner;
	inner.scope = &scope;
	inner.dump = &dump;
	// 9.2p2: the members of a class are declarations of it rather than of the
	// region it is written in, and the PA12 output describes what a function
	// body means, so a member declaration writes no line of its own.
	inner.node = nullptr;
	// 9p2: the class's own name is declared in the class, so a member may name
	// the class it belongs to and a qualified name may reach it through a class
	// derived from it.
	if (!name.empty())
	{
		model_.bind(scope, name, *entity);
	}
	// 10p1: the base-clause is read before the members, because from here on
	// the class holds what its base declares - a type the base named, a member
	// a member declaration uses - and the members are read against that.
	const AstNode* const bases = child_of(node, AstKind::BaseClause);
	if (bases != nullptr)
	{
		read_base_clause(*bases, *entity, scope, ctx, header);
	}
	// 11p2: what a member with no access-specifier before it is declared under,
	// which the class-key decides and each access-specifier changes from there.
	unsigned char access =
		tag == ClassTag::Class ? kPrivateAccess : kPublicAccess;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& member = *node.children[index];
		if (member.kind == AstKind::ClassKey)
		{
			continue;
		}
		if (member.kind == AstKind::AlignmentSpecifier)
		{
			// 7.6.2p1: the alignment-specifiers of a class-head are read where
			// the class is, and 9.2p2 applies the strictest of them when the
			// class is laid out.
			continue;
		}
		if (member.kind == AstKind::AccessSpecifier)
		{
			// 11p1: the specifier holds until the next one or the end of the
			// class, so it is the state the member declarations are read in.
			access = member.token == KW_PRIVATE
				? kPrivateAccess
				: (member.token == KW_PROTECTED ? kProtectedAccess
				                                : kPublicAccess);
			continue;
		}
		if (member.kind == AstKind::BaseClause)
		{
			// Read above, before the members, because they are read against
			// what it added to the class.
			continue;
		}
		if (semantics() && !lowering() &&
		    member.kind == AstKind::BitFieldDeclaration)
		{
			// 9.6p1: a bit-field is a member whose width its declaration writes,
			// which the layout and every use of it read.  PA12 has no rule for
			// either, so the member would be missing from the class the output
			// describes.
			throw std::runtime_error(header + " declares a bit-field, which PA12 "
			                         "does not describe");
		}
		// 11p1: the access a declaration was written under is a fact about the
		// declaration, so it is written onto whatever this member declared.
		// One declaration declares few names, and each is reached once.
		const std::size_t before = scope.declarations.size();
		if (lowering() && member.kind == AstKind::BitFieldDeclaration)
		{
			// 9.6p1: the width is part of what the declaration declares, so the
			// declarators are read against it rather than through the ordinary
			// path, which would leave the member an object with an address.
			bit_field_declaration(member, inner);
		}
		else if (semantics() &&
		         (member.kind == AstKind::SpecialMemberDeclaration ||
		          member.kind == AstKind::SpecialMemberDefinition))
		{
			// 12.1 and 12.4: a constructor or a destructor is a member whose
			// declaration writes no decl-specifier-seq and whose name is the
			// class's own, so it is read here rather than through the ordinary
			// declaration path, which would look that name up as a type.
			special_member(member, inner);
		}
		else
		{
			declaration(member, inner);
		}
		for (std::size_t at = before; at < scope.declarations.size(); ++at)
		{
			scope.declarations[at]->access = access;
		}
	}
	// 9.2p2: the class is complete here, so 7.3.3p14 can be asked of what it
	// declares rather than of what the body had written so far.
	hide_using_members(scope);
	lay_out_class(*entity, scope, tag == ClassTag::Union,
	              requested_alignment(node, inner), packing_of(node));
	// 8.5.1p1: a class with a base class is not an aggregate, so a
	// braced-init-list initializing an object of it chooses a constructor.
	entity->aggregate = entity->base == nullptr && aggregate_class(scope);
	if (semantics())
	{
		declare_special_members(*entity, scope);
	}
	return *entity;
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

// 16.6: the packing alignment the definition `node` is laid out under.
//
// The directive is a fact of a position in the source and the layout is a fact
// of a class, so the one thing that joins them is a position the class has.
// That position is the `}` the definition ends at, because 9.2p2 completes the
// class there and the layout is settled once, from every member at once - which
// is also what a directive written between two members means for the class it
// is written in.  It is the class-specifier's own `completed` and not the end
// of its span, because the span of a class-specifier that is a whole
// declaration reaches past the `;`, and a directive written between the `}` and
// that `;` is one the class was already complete before.  A class defined with
// no such directive anywhere in the unit asks an empty table, which answers
// without a search.
unsigned long long SemaAnalyzer::packing_of(const AstNode& node) const
{
	return packs_ == nullptr || packs_->empty()
		? 0
		: packs_->at(node.completed);
}

void SemaAnalyzer::simple_declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const AstNode* list = child_of(node, AstKind::InitDeclaratorList);
	const std::string declared =
		list == nullptr ? std::string() : name_from_declarators(*list);
	// 11p6: the access every name here is checked with is the one the entity
	// being declared has, which for a static data member defined outside its
	// class reaches what the class declared private.
	const Naming naming(*this, naming_context(declared, ctx));
	Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, declared);
	if (list == nullptr)
	{
		if (specifiers.is_friend)
		{
			// 11.3p2: a friend declaration with no declarator names a class,
			// and what it does is grant rather than declare.
			grant_class_friendship(ctx, specifiers);
			return;
		}
		inject_anonymous_members(specifiers.introduced, ctx, span,
			                         specifiers.is_static);
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

// 8.3.5 and 13.1: one declarator that declares a function, from the point its
// type is known.  A friend declaration reaches here too: 11.3p6 already put
// `target` on the region around the class, and `granting` is the class whose
// access the declaration carries.
void SemaAnalyzer::declare_function_declarator(
	const AstNode& node, const std::string& name, TypeId type,
	const QualifiedName& spelled, const Specifiers& specifiers,
	const Context& target, SemaEntity* granting,
	std::vector<Parameter>& spelled_parameters, const AstNode* initializer)
{
	// 9.3.1p3: a member function is called on an object, which is a
	// parameter of it that the declarator does not write.
	const TypeId written_type = type;
	type = with_object_parameter(type, node, target, specifiers.is_static, name,
	                             spelled.qualified());
	SemaEntity& function =
		declare_function(name, type, target, false,
		                 granting != nullptr && !spelled.qualified(),
		                 type != written_type);
	// 15.4p1: one declaration written with a non-throwing
	// exception-specification is what says the function throws nothing,
	// however the others were written.
	function.nonthrowing = function.nonthrowing || declarator_nonthrowing(node);
	function.wrote_exception_specification =
		function.wrote_exception_specification ||
		declarator_writes_exception_specification(node);
	function.object_member = type != written_type;
	if (!function.object_member)
	{
		require_operator_operand(name, type,
		                         target.scope->kind == ScopeKind::Class);
	}
	if (granting != nullptr)
	{
		// 11.3p1 and 3.4.2p2: the class grants this declaration its access,
		// and holds it as one of the declarations a lookup that reaches the
		// class finds even where no lookup written in a region does.
		model_.befriend(*granting, function);
		if (!spelled.qualified() && granting->scope != nullptr)
		{
			granting->scope->friend_functions.push_back(&function);
		}
	}
	// 3.5p4: and so does every name 7.3.1.1p1's unnamed namespace declares,
	// whether the declaration wrote `static` or not.
	function.internal_linkage = function.internal_linkage ||
		target.scope->unnamed_region ||
		(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
	// 7.1.2p2: one declaration of a function with `inline` makes it inline,
	// so the fact accumulates over the declarations of one entity.
	function.inline_function =
		function.inline_function || specifiers.is_inline;
	if (initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::SpecialInitializer)
	{
		// 8.4.2 and 8.4.3: `= default` asks for the definition 12.8 would have
		// given this special member, and `= delete` for a declaration every use
		// of is ill formed.  8.4.2p1 makes the first of them implicitly inline,
		// so the definition it stands for belongs to every translation unit
		// that needs one rather than to the one that wrote the class.
		function.deleted = initializer->children[0]->text == "delete";
		function.defaulted = !function.deleted;
		function.defined = false;
		function.inline_function = true;
		// 12.8p28: the definition the standard gives this declaration names the
		// object it reads from throughout, and the declarator is what said what
		// that name is - so the parameters it wrote travel to the definition,
		// exactly as a constructor's do.  A declaration that named none leaves
		// the definition to give it a name of its own.
		std::vector<Parameter> named = spelled_parameters;
		constructor_parameters_[function.id].swap(named);
	}
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

// 3.5, 3.7.1 and 3.7.2: where the object a declaration declares lives - which
// linkage its name has and which region ends its lifetime.  Both are facts of
// the variable rather than of the declaration that happened to spell them, so
// they are settled against the declaration of the same variable this region
// already holds as well as against this one's own specifiers.
void SemaAnalyzer::record_storage(SemaEntity& entity, const SemaEntity* prior,
                                  const Specifiers& specifiers,
                                  const Context& target, TypeId type)
{
	// 3.5p3: at namespace scope a name declared `static` has internal linkage,
	// and so does a `const` object no declaration wrote `extern`.
	// 3.5p4: a name an unnamed namespace declares has internal linkage however
	// the declaration was written.
	entity.internal_linkage = entity.internal_linkage ||
		target.scope->unnamed_region ||
		(target.scope->kind == ScopeKind::Namespace &&
		 (specifiers.is_static ||
		  (!specifiers.is_extern &&
		   (types_.object_cv(type) & kCvConst) != 0)));
	// 7.1.1p1: `thread_local` is a fact of the variable rather than of one
	// declaration of it, so every declaration of one variable writes it or none
	// does.  A declaration that disagrees with the one this region already
	// holds names a storage duration the variable does not have, whether it is
	// an `extern` declaration and the definition that follows it or a static
	// data member declared in its class and defined outside it.
	if (prior != nullptr && prior->thread_storage != specifiers.is_thread_local)
	{
		throw std::runtime_error(
			specifiers.is_thread_local
				? "a declaration of " + entity.name +
				      " writes thread_local where an earlier declaration of it "
				      "does not"
				: "a declaration of " + entity.name +
				      " leaves out the thread_local an earlier declaration of "
				      "it wrote");
	}
	// 3.7.2p1: `thread_local` on a declaration of a variable gives that
	// variable thread storage duration, so the fact belongs to the variable and
	// not to the declaration that happened to write the keyword - a static data
	// member is declared in its class and defined outside it, and 7.1.1p1 above
	// makes either spelling of it say the same thing.
	entity.thread_storage = entity.thread_storage || specifiers.is_thread_local;
	if (!semantics())
	{
		return;
	}
	// 3.7.1p3 and 3.7.2p1: a block-scope object declared `static` is one object
	// of the program and one declared `thread_local` is one per thread; neither
	// is an object of the block that declares it.  6.7p4 initializes it the
	// first time control passes through the declaration, and 3.6.3p1 or the end
	// of its thread destroys it.  Writing it as the automatic object of its
	// block would describe a different program, and the guard 6.7p4 asks for is
	// not part of this milestone - so it is refused wherever it is declared and
	// whatever its type, rather than only where a class of its own ends the
	// lifetime.
	if (entity.object_definition &&
	    (specifiers.is_static || entity.thread_storage) &&
	    target.scope->kind != ScopeKind::Namespace &&
	    target.scope->kind != ScopeKind::Class)
	{
		throw std::runtime_error(
			std::string("a block-scope ") +
			(entity.thread_storage ? "thread_local" : "static") + " object of " +
			types_.description(type) +
			" is declared, whose one initialization and its destruction this "
			"milestone does not write");
	}
}

void SemaAnalyzer::init_declarator(const AstNode& node,
                                   const AstNode* initializer,
                                   const Specifiers& specifiers,
                                   const Context& ctx)
{
	// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
	// looked up in the region that name reaches, so the region is settled from
	// the declarator-id before the declarator around it is read - which is what
	// lets `auto A::f() -> nested` and `void A::g(nested)` name a type the
	// class declares.
	const AstNode* const id = declarator_id(node);
	const std::string written_id = id == nullptr ? std::string() : id->text;
	const QualifiedName spelled(written_id);
	// 3.4.3p3: a declarator-id with a nested-name-specifier declares into the
	// region that names, wherever the declaration is written.
	Context target = ctx;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
	}
	std::string written;
	// 14.1: a template's declarator is the pattern its instantiations write
	// their own parameters from, and 8.3.6p4 makes a function declaration's
	// default-arguments the function's from that declaration on, whether or not
	// it is the one with the body.  Both read the parameter clause the
	// declarator already spelled, so it is captured here rather than read again.
	std::vector<Parameter> spelled_parameters;
	TypeId type = declarator_type(node, specifier_type(specifiers),
	                              spelled.qualified() ? target : ctx, &written,
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
	const std::string name = spelled.last();
	if (name.empty())
	{
		return;
	}
	require_mutable_data_member(specifiers, target, name, type);
	// 11.3p6: what a friend declaration declares belongs to the region around
	// the class, so the declarator is read against that region and the class
	// gets the grant.
	SemaEntity* const granting =
		specifiers.is_friend ? friend_target(ctx, spelled, target) : nullptr;

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
		declare_function_declarator(node, name, type, spelled, specifiers,
		                            target, granting, spelled_parameters,
		                            initializer);
		return;
	}
	if (initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::SpecialInitializer)
	{
		throw std::runtime_error(name + " is not a function and is declared "
		                                "with `= default` or `= delete`");
	}

	declare_object_declarator(initializer, specifiers, ctx, target, spelled,
	                          written, type);
}

// 3.1p2 and 8.5: the object a declarator that is not a function declares, from
// the point its type and the region it belongs to are known.  The declaration
// is what says whether it is also a definition, what its lifetime is, and which
// of 8.5's initializations builds it.
void SemaAnalyzer::declare_object_declarator(const AstNode* initializer,
                                             const Specifiers& specifiers,
                                             const Context& ctx,
                                             const Context& target,
                                             const QualifiedName& spelled,
                                             const std::string& written,
                                             TypeId type)
{
	const std::string name = spelled.last();
	// 7.1.5p9: a constexpr object is a const object.
	if (specifiers.is_constexpr)
	{
		type = types_.qualified(type, kCvConst);
	}
	// 3.3.2 and 9.4.2p2: a declarator-id with a nested-name-specifier defines
	// the object that region already declares - a static data member is
	// declared in its class and defined outside it - rather than declaring a
	// second one there, so what its first declaration said about it stands.
	// The declaration that region already has is also what 7.1.1p1's
	// `thread_local` has to agree with, wherever this one is written, so it is
	// found before this declaration is bound over it.
	SemaEntity* const prior = redeclared(target, name, SemaKind::Variable);
	SemaEntity* const declared = spelled.qualified() ? prior : nullptr;
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
		name_in_region(entity, *target.scope, name);
		// 9.2p1: a data member is part of an object of its class and is reached
		// through one, which 9.4p2 makes untrue of a member declared `static`:
		// that one is a variable the class names.
		entity.object_member =
			target.scope->kind == ScopeKind::Class && !specifiers.is_static;
		// 7.1.1p10: the const of the object holding this member stops at it,
		// which the declaration is where 5.2.5p4 reads.
		entity.mutable_member = specifiers.is_mutable;
		// 7.6.2p1: what an alignment-specifier on the declaration asked for is
		// a fact about what it declares, which 9.2p13's layout reads.
		entity.requested_align = specifiers.alignment;
		// 12.6.2p8: a brace-or-equal-initializer on a non-static data member is
		// what initializes it wherever a constructor does not say otherwise, and
		// 8.5.1p1 makes a class that has one no aggregate.
		entity.default_initializer = entity.object_member &&
			initializer != nullptr && !initializer->children.empty();
		if (entity.default_initializer)
		{
			// 12.6.2p8 and 9.2p2: the initializer is read by every constructor
			// that does not name the member, in the complete-class context the
			// member was declared in rather than where the constructor is.
			Default& held = member_initializers_[entity.id];
			held.written = initializer->children[0];
			held.scope = target.scope;
		}
	}
	// 3.1p2: an `extern` declaration with no initializer declares the object
	// and does not define it; every other declaration of one at namespace scope
	// does, and a later definition of the same object says so once.  9.4.2p2
	// makes the declaration a static data member's class writes no definition
	// of it however it was written, so the one that defines it is the one
	// written outside the class, which is the one whose declarator-id carries
	// the nested-name-specifier that named the class.
	const bool defines_object =
		(target.scope->kind != ScopeKind::Class || spelled.qualified()) &&
		(!specifiers.is_extern ||
		 (initializer != nullptr && !initializer->children.empty()));
	entity.object_definition = entity.object_definition || defines_object;
	record_storage(entity, prior, specifiers, target, type);
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
	line.fact.object_definition = line.fact.object_definition || defines_object;
	const AstNode* const clause =
		initializer == nullptr || initializer->children.empty()
			? nullptr
			: initializer->children[0];
	// 12.8p31 and 5.2.3p3: `T x = T{...}` creates `x` itself, so the braced
	// list is what initializes it and the initialization is the one the same
	// list written on the declarator would be - which for an aggregate is
	// 8.5.1's and not a constructor's.  The prvalue was direct-list-initialized
	// where it stands, so 8.5.4p3 asks nothing of the list here either.
	const AstNode* const elided = clause == nullptr
		? nullptr
		: braced_prvalue_of(*clause, type, ctx);
	const AstNode* const value = elided != nullptr ? elided : clause;
	const bool copied =
		elided == nullptr && initializer != nullptr && initializer->copied;
	if (types_.is_class(element_of(types_.strip_cv(type))) &&
	    entity.object_definition)
	{
		// 3.8p1: the end of the lifetime of an object of class type is an
		// action of the region that declared it, whatever form its initializer
		// took.  An aggregate initialized from a braced-init-list is written
		// below rather than by a constructor, and its lifetime still ends.
		record_lifetime(entity, target, line);
	}
	if (types_.is_class(types_.strip_cv(type)) && entity.object_definition &&
	    !(value != nullptr && value->kind == AstKind::BracedInitList &&
	      aggregate_type(type)))
	{
		// 8.5 and 12.1: an object of class type is initialized by one of the
		// constructors of its class, whatever form the initializer took, unless
		// 8.5.1 makes the class an aggregate initialized from the clauses of a
		// braced-init-list.
		construct_object(entity, line, value, ctx, Placement::Named, copied);
		return;
	}
	if (entity.object_definition && element_constructed(type, value))
	{
		// 12.6p1: an array of class type is initialized by constructing each of
		// its elements, and where no clause named one the constructor every
		// element is given is the same one.  The action names the array, so
		// there is one of it however many elements there are.
		construct_object(entity, line, value, ctx, Placement::Named, copied);
		return;
	}
	// 5.19p3 and 7.1.5p9: a constexpr object is initialized by a constant
	// expression, and the dump writes the value it stands for rather than the
	// expression that computed it.
	if (value == nullptr)
	{
		// 8.5p6: an object of any other type with no initializer holds no value
		// the program may read, and there is nothing to describe.
		return;
	}
	if (entity.constant && specifiers.is_constexpr)
	{
		model_.open_node(line, spell("literal", ValueCategory::PRValue, type,
		                             spell_value(type, entity.value)));
		return;
	}
	// 3.6.2p2 and 3.7.1p1: an object at namespace scope, and the static data
	// member 9.4.2p2 defines there, is given its value by the program image
	// rather than built where its declaration stands.
	write_initializer(*value, type, ctx, line,
	                  entity.object_definition &&
	                  (target.scope->kind == ScopeKind::Namespace ||
	                   target.scope->kind == ScopeKind::Class));
	// 12.2p5: where that initializer bound this reference to a temporary, the
	// temporary's lifetime is the reference's from here on.
	extend_bound_temporary(type, ctx, line);
}

void SemaAnalyzer::write_initializer(const AstNode& initializer, TypeId type,
                                     const Context& ctx, DumpNode& line,
                                     bool image)
{
	if (initializer.kind == AstKind::ParenInitializer)
	{
		// 8.5p16: direct-initialization from one expression, which differs from
		// copy-initialization in one thing only - 12.3.2p2 lets it choose a
		// conversion function declared `explicit`.
		if (!initializer.children.empty())
		{
			initialize(*initializer.children[0], type, ctx, line, false,
			           Requested::Written, true);
		}
		return;
	}
	if (initializer.kind == AstKind::BracedInitList)
	{
		// 8.5.1: an aggregate is initialized from the clauses of its list, each
		// initializing one subobject.  3.6.2p2 makes that initialization the
		// value of the object's own storage where the object has static storage
		// duration, so no element of it is an object a function builds.
		list_initialize(initializer, type, ctx, line, image);
		return;
	}
	// The same reading a list standing where an expression initializes an
	// object gets.
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
	const AstNode& declarator = *node.children[1];
	const AstNode* id = declarator_id(declarator);
	const std::string written = id == nullptr ? std::string() : id->text;
	// 11p6: a member function defined outside its class names, in its leading
	// return type as much as in its body, what that class gave itself.
	const Naming naming(*this, naming_context(written, ctx));
	Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
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
	// 11.3p6: a friend function defined in a class body is a member of the
	// region around the class.  3.4.1p9 still reads the names in its body as a
	// member function's are read, so the region its parameters and body are
	// written in is enclosed by the class while the declaration is not.
	Scope* const lexical = ctx.scope;
	SemaEntity* const granting =
		specifiers.is_friend ? friend_target(ctx, spelled, target) : nullptr;

	std::string ignored;
	std::vector<Parameter> parameters;
	// 3.4.1p8 and 3.4.1p9: the rest of a declarator is read where the
	// declarator-id names it - in the region a qualified one reaches, and
	// otherwise where the declaration stands, which for a friend declaration is
	// the class it is written in and not the namespace it declares into.
	TypeId type = declarator_type(declarator, specifier_type(specifiers),
	                              spelled.qualified() ? target : ctx, &ignored,
	                              &parameters);
	if (types_.kind(type) != TypeKind::Function)
	{
		throw std::runtime_error("a function definition declares " + name +
		                         ", which is not a function");
	}
	require_mutable_data_member(specifiers, target, name, type);
	// 9.3.1p3: a member function is called on an object its declarator does not
	// write, whether it is defined in its class or after it.
	const TypeId written_type = type;
	type = with_object_parameter(type, declarator, target, specifiers.is_static,
	                             name, spelled.qualified());

	SemaEntity& entity =
		declare_function(name, type, target, true,
		                 granting != nullptr && !spelled.qualified(),
		                 type != written_type,
		                 spelled.qualified() && granting == nullptr);
	entity.nonthrowing =
		entity.nonthrowing || declarator_nonthrowing(declarator);
	entity.wrote_exception_specification =
		entity.wrote_exception_specification ||
		declarator_writes_exception_specification(declarator);
	entity.object_member = type != written_type;
	if (!entity.object_member)
	{
		require_operator_operand(name, type,
		                         target.scope->kind == ScopeKind::Class);
	}
	if (granting != nullptr)
	{
		model_.befriend(*granting, entity);
		if (!spelled.qualified() && granting->scope != nullptr)
		{
			granting->scope->friend_functions.push_back(&entity);
			// 11.3p5: the definition declares a member of the enclosing
			// namespace and is written where no ordinary lookup finds its
			// name, so the class that wrote it is where this unit reads it.
			entity.friend_definition =
				entity.friend_definition || ctx.scope->kind == ScopeKind::Class;
		}
	}
	// 3.5p3: one declaration written `static` gives the name internal linkage,
	// however the others were written.
	// 3.5p4: so does 7.3.1.1p1's unnamed namespace, which the definition may
	// stand in without any declaration of it writing a specifier.
	entity.internal_linkage = entity.internal_linkage ||
		target.scope->unnamed_region ||
		(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
	// 7.1.2p2 and 9.3p2: `inline` says so, and so does defining a member
	// function inside the class definition - which is where the definition is
	// written, not the region it declares into.  A member defined outside its
	// class declares into that class and is a definition this unit owns like
	// any other: it binds strongly and is emitted whether or not this unit uses
	// it.
	entity.inline_function = entity.inline_function || specifiers.is_inline ||
		ctx.scope->kind == ScopeKind::Class;
	record_default_arguments(entity, parameters, target.scope);

	DumpScope& dump = model_.open_dump(*target.dump, "scope function " + name);
	Context inner;
	inner.scope = &model_.open(ScopeKind::Function,
	                           granting != nullptr ? *lexical : *target.scope,
	                           &entity, &dump);
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
	                           function_description(type, entity.object_member),
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
	std::vector<std::size_t> enclosing_breaks;
	std::vector<std::size_t> enclosing_continues;
	enclosing_breaks.swap(breakable_frames_);
	enclosing_continues.swap(continuable_frames_);
	const std::size_t enclosing_live = live_destructions_;
	live_destructions_ = 0;
	std::vector<SemaEntity*> enclosing_parameters;
	enclosing_parameters.swap(parameter_objects_);
	// 5.2.2p4: the parameters of class type this definition has to end are read
	// once, off the lines just written, before the body that may return.
	open_parameter_lifetimes(line);
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
	// 6.6.3p2 and 3.8p1: control reaching the end of the body leaves the
	// function as a return does, so what a return would end is ended there too.
	end_parameter_lifetimes(line);
	parameter_objects_.swap(enclosing_parameters);
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
	breakable_frames_.swap(enclosing_breaks);
	continuable_frames_.swap(enclosing_continues);
	live_destructions_ = enclosing_live;
}

// 7.1.1p10: `mutable` may be written only on a non-static data member whose
// type is neither const-qualified nor a reference.  What it says is a fact about
// what the declaration declares, so every declaration is asked and not only the
// one that goes on to declare an object: a member function - declared or
// defined - a typedef, a static data member and a declaration of no class at
// all each declare something the specifier says nothing about.
void SemaAnalyzer::require_mutable_data_member(const Specifiers& specifiers,
                                               const Context& target,
                                               const std::string& name,
                                               TypeId type)
{
	if (!specifiers.is_mutable)
	{
		return;
	}
	if (target.scope->kind != ScopeKind::Class || specifiers.is_static ||
	    specifiers.is_typedef || specifiers.is_friend ||
	    types_.kind(type) == TypeKind::Function ||
	    (types_.cv(type) & kCvConst) != 0 || types_.is_reference(type))
	{
		throw std::runtime_error(
			name + " is declared `mutable`, which 7.1.1p10 allows only for a "
			"non-static data member of neither const-qualified nor reference "
			"type");
	}
}

// 9.3.1p3 put the object parameter of a non-static member function in its type,
// and 8.3.5p4's parameter-type-list is what a declarator wrote - so the two
// declarations `void unlink();` and `static void unlink(block*);` of one class
// have one function type and are two functions.  13.1's index is keyed by the
// list the declarator wrote wherever a class is what declares the name, with
// 8.3.5p7's cv-qualifier-seq beside it, which is the same key 7.3.3p14's hiding
// already asks with.  A namespace declares no function with an object
// parameter, so there the type's own list is the list and costs no rebuild.
std::uint32_t SemaAnalyzer::declaration_signature(const Scope& where,
                                                  TypeId type,
                                                  bool object_member)
{
	return where.kind == ScopeKind::Class
		? member_signature(type, object_member)
		: types_.signature(type);
}

// 13.1p2: a class shall not declare a member function with a ref-qualifier and
// one without where the two have the same name and the same parameter-type-list,
// because 13.3.1p5's rule that an unqualified member binds an rvalue too would
// leave a call on an rvalue with no way to choose between them.
//
// 8.3.5p4's parameter-type-list is the types the declarator wrote, which
// 8.3.5p7's cv-qualifier-seq is no part of - so `f() const` and `f() &&` are a
// pair this refuses just as `f()` and `f() &&` are, and the declaration asked
// about may have written any of the four qualifications.  The chain the name
// heads is indexed by both qualifiers along with the rest of the signature, so
// each is one further probe of that index rather than a walk of the
// declarations already made.
void SemaAnalyzer::require_uniform_ref_qualifiers(const SemaEntity& head,
                                                  const std::string& name,
                                                  TypeId type)
{
	static const unsigned kQualifications[] = {
		kCvNone, kCvConst, kCvVolatile, kCvConst | kCvVolatile
	};
	static const RefQualifier kSpellings[] = {
		RefQualifier::LValue, RefQualifier::RValue
	};
	// Two ref-qualified declarations are two functions 13.3.1p4 tells apart by
	// the category the object argument has, so what a declaration that wrote one
	// asks about is the unqualified spelling alone, and what a declaration that
	// wrote none asks about is either of the two.
	const bool qualified =
		types_.function_ref_qualifier(type) != RefQualifier::None;
	const std::vector<TypeId>& written = types_.parameters(type);
	std::vector<TypeId> parameters(written);
	const TypeId object = types_.strip_cv(types_.target(written[0]));
	for (std::size_t index = 0; index < 4; ++index)
	{
		parameters[0] =
			types_.pointer_to(types_.qualified(object, kQualifications[index]));
		const TypeId probe = types_.function_of(types_.target(type), parameters,
		                                        types_.variadic(type));
		for (std::size_t spelling = 0; spelling < (qualified ? 1u : 2u);
		     ++spelling)
		{
			const TypeId other = types_.ref_qualified_function(
				probe, qualified ? RefQualifier::None : kSpellings[spelling]);
			if (model_.overload_of(head, member_signature(other, true)) !=
			    nullptr)
			{
				throw std::runtime_error(
					"a class declares " + name +
					" both with and without a ref-qualifier, which 13.1p2 does "
					"not allow");
			}
		}
	}
}

SemaEntity& SemaAnalyzer::declare_function(const std::string& name, TypeId type,
                                           const Context& target, bool define,
                                           bool hidden, bool object_member,
                                           bool redeclaration)
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
	const std::uint32_t signature = declaration_signature(where, type,
	                                                      object_member);
	// 1.3.11 and 13.1: two declarations declare the same function exactly when
	// their parameter type lists agree, which 8.3.5p5 has already normalised.
	// The chain the name heads is indexed by that list, so the question is a
	// probe rather than a walk of the declarations already made.
	SemaEntity* prior =
		head == nullptr ? nullptr : model_.overload_of(*head, signature);
	// 11.3p6: a friend declaration declared this function into this region
	// without binding its name, so the chain the name heads is not the only
	// place a declaration of it can be.
	const std::unordered_map<std::string, SemaEntity*>::iterator concealed =
		where.hidden.empty() ? where.hidden.end() : where.hidden.find(name);
	if (prior == nullptr && concealed != where.hidden.end())
	{
		prior = model_.overload_of(*concealed->second, signature);
		if (prior != nullptr && !hidden)
		{
			// 7.3.1.2p3: a matching declaration at namespace scope is what
			// makes the friend's name visible, and the two declare one
			// function.
			reveal_friend(where, name, *prior, signature);
		}
	}
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
	if (redeclaration)
	{
		// 9.3p2 and 3.4.3.2p1: a definition written with a qualified
		// declarator-id defines the declaration that region already made, so a
		// declarator that matches none of them names a member the region does
		// not have however nearly it spells one - which is what tells `int
		// X::f() &&` from the `int X::f() &` the class declared, and equally
		// what tells a mistyped parameter list or cv-qualifier-seq from the one
		// the class wrote.
		throw std::runtime_error("a definition of " + name +
		                         " matches no declaration of it");
	}
	if (object_member && where.kind == ScopeKind::Class && head != nullptr)
	{
		require_uniform_ref_qualifiers(*head, name, type);
	}

	SemaEntity& entity = model_.create(SemaKind::Function, name, type);
	name_in_region(entity, where, name);
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
	if (hidden)
	{
		// 11.3p6: the declaration is a member of this region whose name no
		// lookup written in it finds, so it joins the region's hidden chain and
		// binds nothing.  3.4.2p2 reaches it through the class that wrote it.
		SemaEntity*& concealed_head = where.hidden[name];
		if (concealed_head == nullptr)
		{
			concealed_head = &entity;
		}
		else
		{
			concealed_head->tail->next = &entity;
			concealed_head->tail = &entity;
		}
		model_.hold_overload(*concealed_head, signature, entity);
		model_.declare_in(where, entity);
		return entity;
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

// 7.3.1.2p3: a namespace-scope declaration that matches a friend declaration
// declares the same function, and is what first makes its name visible.  The
// declaration leaves the hidden chain for the one the name binds; the other
// friend declarations of that name stay where they are, because each is made
// visible by a declaration of its own.
void SemaAnalyzer::reveal_friend(Scope& where, const std::string& name,
                                 SemaEntity& entity, std::uint32_t signature)
{
	const std::unordered_map<std::string, SemaEntity*>::iterator held =
		where.hidden.find(name);
	SemaEntity* concealed = held->second;
	// The chain is indexed by the declaration the name would be bound to, and
	// that is the declaration that may be leaving, so the whole index of this
	// chain is dropped and rebuilt.  A chain holds the friend declarations of
	// one name in one namespace, which is what the source wrote.
	for (SemaEntity* at = concealed; at != nullptr; at = at->next)
	{
		model_.drop_overload(
			*concealed,
			declaration_signature(where, at->type, at->object_member));
	}
	SemaEntity* before = nullptr;
	for (SemaEntity* at = concealed; at != &entity; at = at->next)
	{
		before = at;
	}
	if (before == nullptr)
	{
		concealed = entity.next;
	}
	else
	{
		before->next = entity.next;
	}
	entity.next = nullptr;
	entity.tail = &entity;
	if (concealed == nullptr)
	{
		where.hidden.erase(held);
	}
	else
	{
		SemaEntity* last = concealed;
		while (last->next != nullptr)
		{
			last = last->next;
		}
		concealed->tail = last;
		held->second = concealed;
		for (SemaEntity* at = concealed; at != nullptr; at = at->next)
		{
			model_.hold_overload(
				*concealed,
				declaration_signature(where, at->type, at->object_member), *at);
		}
	}
	SemaEntity* head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind == SemaKind::Function)
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
