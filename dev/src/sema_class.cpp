#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"

namespace
{

std::string decimal(unsigned long long value)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
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

// True for the nodes that hold the arguments an initializer wrote rather than
// one expression: the parenthesised forms an initializer, a call and a
// mem-initializer each spell, and the braced-init-list 8.5.4 writes.
bool is_initializer_list(AstKind kind)
{
	return kind == AstKind::ParenInitializer ||
		kind == AstKind::ParenArgumentList || kind == AstKind::ArgumentList ||
		kind == AstKind::BracedInitList;
}

// The argument list of a call or of a mem-initializer, in either of the two
// spellings PA10 writes one as.
const AstNode* call_arguments(const AstNode& node)
{
	const AstNode* list = child_of(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_of(node, AstKind::ParenArgumentList);
}

// 9.2p13: the next address at or after `value` that an object of alignment
// `unit` may begin at.
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

// What a class is, what its objects hold, and when their lifetimes end.
//
// This is the object-model half of the analysis: 10p1's base-clause and 9.2p13's
// layout say what an object of a class is made of; 11's access specifiers say
// who may name each part; 12.1 and 12.4 say which special member functions the
// class has and what their bodies do; 12.6.2 and 12.4p8 say in what order the
// subobjects of one object are constructed and destroyed; and 3.7.1 and 3.8p1
// say which region ends the lifetime of an object a declaration named.
//
// They belong together because each is settled from the same one fact - what
// the class's own region declares, in the order it declares it - and because a
// question about a subobject is asked in the same words wherever the subobject
// stands: a base, a member, or an object of its own.  The declaration walk in
// `sema_analyzer.cpp` reads the syntax and hands each class to this file once,
// where the class is complete.

// 7.6.2p1: the strictest alignment the class-head asked for, or zero when it
// asked for none.  An alignment-specifier whose operand is not a constant this
// translation knows asks for nothing it can act on.
unsigned long long SemaAnalyzer::requested_alignment(const AstNode& node,
                                                     const Context& ctx)
{
	unsigned long long wanted = 0;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::AlignmentSpecifier || child.children.empty())
		{
			continue;
		}
		const unsigned long long asked =
			evaluate(*child.children[0], ctx).bits;
		if (asked > wanted)
		{
			wanted = asked;
		}
	}
	return wanted;
}

void SemaAnalyzer::lay_out_class(SemaEntity& entity, Scope& scope, bool is_union,
                                 unsigned long long requested)
{
	unsigned long long size = 0;
	unsigned long long align = 1;
	bool empty = true;
	if (entity.base != nullptr)
	{
		// 10p1 and the course ABI: the direct base subobject begins where the
		// derived object does, and the members are laid out after it.  A base
		// that holds nothing is given no storage of its own, so the derived
		// class starts its members where the base did.
		align = types_.object_align(entity.base->type);
		if (!entity.base->empty_class)
		{
			size = types_.object_size(entity.base->type);
			empty = false;
		}
	}
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
		// 9p6: a class with a non-static data member holds something, whatever
		// its size comes to.
		empty = false;
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
	if (requested != 0)
	{
		// 7.6.2p5: an alignment-specifier may not ask for less than the class
		// would have had, because the members it holds still need theirs.
		if (requested < align)
		{
			throw std::runtime_error("a class asks for an alignment weaker than "
			                         "the one its members need");
		}
		align = requested;
	}
	entity.empty_class = empty;
	// 1.8p5: a complete object has a size of at least one byte.
	size = round_up(size, align);
	types_.complete_class(entity.type, size == 0 ? 1 : size, align);
}
// 10p1: the base-clause of a class definition, which says what every object of
// the class holds a subobject of.  The base is recorded on the class and on the
// region it declares, and every later question - 9.2p13 layout, 10.2 lookup,
// 11.2 access, 12.6.2 construction, 12.4 destruction, 4.10p3 conversion - reads
// that one fact rather than the syntax it was read from.
void SemaAnalyzer::read_base_clause(const AstNode& node, SemaEntity& entity,
                                    Scope& scope, const Context& ctx,
                                    const std::string& header)
{
	if (!lowering())
	{
		// The PA12 dump has no line for a base class, so a class with one would
		// be written as the class it would have been without the base-clause,
		// which is a different class.  PA11 only spells the declaration it was
		// given and needs none of this.
		if (semantics())
		{
			throw std::runtime_error(header + " has a base class, which PA12 "
			                         "does not describe");
		}
		return;
	}
	if (node.children.size() != 1)
	{
		// 10p1: this milestone lays out and initializes one direct base, so a
		// class with more than one is refused rather than described as a class
		// holding only the first of them.
		throw std::runtime_error(header + " has more than one direct base "
		                         "class, which this milestone does not lay out");
	}
	const AstNode& specifier = *node.children[0];
	// 11.2p2: a base-specifier with no access-specifier is `private` for a class
	// and `public` for a struct, which is what the class-key already decided for
	// the members.
	unsigned char access = types_.class_tag(entity.type) == ClassTag::Class
		? kPrivateAccess
		: kPublicAccess;
	std::string named;
	for (std::size_t index = 0; index < specifier.children.size(); ++index)
	{
		const AstNode& part = *specifier.children[index];
		if (part.kind == AstKind::Virtual)
		{
			throw std::runtime_error(header + " has a virtual base class, which "
			                         "this milestone does not lay out");
		}
		if (part.kind == AstKind::AccessSpecifier)
		{
			access = part.token == KW_PRIVATE
				? kPrivateAccess
				: (part.token == KW_PROTECTED ? kProtectedAccess
				                              : kPublicAccess);
			continue;
		}
		if (part.kind == AstKind::BaseName)
		{
			named = part.text;
		}
	}
	// 10p1: the base-specifier names a class, which a typedef-name may stand
	// for - so what it names is the class the type belongs to rather than the
	// declaration the name was bound to.
	SemaEntity& found = require(resolve(named, ctx, LookupKind::Type), named);
	if (!names_a_type(found) || !types_.is_class(types_.strip_cv(found.type)))
	{
		throw std::runtime_error(named + " is named as a base class and is not "
		                                 "a class");
	}
	SemaEntity* const base = model_.type_owner(types_.strip_cv(found.type));
	if (base == nullptr || !base->defined || base->scope == nullptr)
	{
		// 10p1: a base class shall be a complete class type, because the
		// derived class holds a subobject of it.
		throw std::runtime_error(named + " is named as a base class and is an "
		                                 "incomplete class");
	}
	if (base == &entity)
	{
		throw std::runtime_error(header + " is named as its own base class");
	}
	if (types_.class_tag(base->type) == ClassTag::Union ||
	    types_.class_tag(entity.type) == ClassTag::Union)
	{
		// 9.5p3: a union shall not have base classes and shall not be used as a
		// base class.
		throw std::runtime_error(header + " derives from or is a union, which "
		                                  "9.5p3 does not allow");
	}
	entity.base = base;
	entity.base_access = access;
	scope.base = base->scope;
}

// 12.1 and 12.4: a constructor or a destructor declared in a class body.  Both
// are functions of the class whose name no lookup reaches: an object of the
// class asks the class for them, so they are chained on the class rather than
// bound to a name in it.
void SemaAnalyzer::special_member(const AstNode& node, const Context& ctx)
{
	SemaEntity& owner = *ctx.scope->owner;
	const std::string written = node.text;
	const std::string spelled =
		QualifiedName(types_.user_name(owner.type)).last();
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string named = destructor ? written.substr(1) : written;
	if (QualifiedName(named).last() != spelled)
	{
		// 12.3.2: a conversion function, and 13.5 an operator function written
		// with no return type.  Neither is part of this milestone's slice, and
		// what the output would describe without it is not the class the
		// program wrote.
		throw std::runtime_error(spelled + " declares " + written +
		                         ", which is a special member function this "
		                         "milestone does not describe");
	}
	const AstNode* const declarator = child_of(node, AstKind::Declarator);
	std::vector<Parameter> parameters;
	bool variadic = false;
	const AstNode* const clause =
		declarator == nullptr ? nullptr
		                      : child_of(*declarator, AstKind::ParameterClause);
	if (clause != nullptr)
	{
		read_parameters(*clause, ctx, parameters, variadic);
	}
	std::vector<TypeId> types;
	// 12.1p1 and 12.4p1: neither has a return type, and both are called on the
	// object 9.3.1p3 makes the first parameter of a member function's type.
	// 12.4p12 lets a destructor be invoked for any cv-qualified version of its
	// class, which no cv-qualifier-seq of its own says.
	types.push_back(types_.pointer_to(
		destructor ? types_.qualified(owner.type, kCvConst | kCvVolatile)
		           : owner.type));
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		// 8.3.5p5: an array or function parameter contributes a pointer, and
		// top level cv-qualification is dropped.
		types.push_back(types_.adjust_parameter(parameters[index].type));
	}
	if (destructor && parameters.size() != 0)
	{
		throw std::runtime_error("a destructor is declared with parameters");
	}
	const TypeId type =
		types_.function_of(types_.fundamental(FT_VOID), types, variadic);
	SemaEntity* entity = nullptr;
	if (destructor)
	{
		if (owner.destructor != nullptr)
		{
			throw std::runtime_error(spelled + " declares two destructors");
		}
		entity = &model_.create(SemaKind::Function, written, type);
		owner.destructor = entity;
		entity->special = kDestructorFunction;
		model_.bind(*ctx.scope, written, *entity);
	}
	else
	{
		// 13.1: the constructors of a class are its declarations of one name,
		// so a second one joins the chain the first heads and 13.3.1.3 walks it.
		for (SemaEntity* at = owner.constructor; at != nullptr; at = at->next)
		{
			if (at->type == type)
			{
				throw std::runtime_error(spelled +
				                         " declares one constructor twice");
			}
		}
		entity = &model_.create(SemaKind::Function, written, type);
		entity->special = kConstructorFunction;
		if (owner.constructor == nullptr)
		{
			owner.constructor = entity;
		}
		else
		{
			owner.constructor->tail->next = entity;
		}
		owner.constructor->tail = entity;
	}
	entity->tail = entity;
	entity->dump_name = ctx.scope->prefix + written;
	entity->object_member = true;
	// 7.1.2p3 and 9.3p2: a special member function *defined* in its class body
	// is inline, so its definition belongs to every translation unit that needs
	// one.  One the class only declares is defined elsewhere or nowhere, and
	// binds like any other function the program names once.
	entity->inline_function = node.kind == AstKind::SpecialMemberDefinition;
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		// 12.3.1p2: `explicit` says which initializations may choose this
		// constructor, which is a fact about the declaration.
		if (specifiers->children[index]->text == "explicit")
		{
			entity->explicit_function = true;
		}
	}
	record_default_arguments(*entity, parameters, ctx.scope);
	model_.declare_in(*ctx.scope, *entity);

	const AstNode* const initializer = child_of(node, AstKind::Initializer);
	if (initializer != nullptr && !initializer->children.empty())
	{
		// 8.4.2 and 8.4.3: `= default` asks for the definition 12.1p5 or
		// 12.4p3 would have given, and `= delete` for a declaration every use
		// of is ill formed.  Neither is a definition the program wrote, so
		// 8.5.1p1 leaves the class an aggregate.
		entity->deleted = initializer->children[0]->text == "delete";
		entity->defaulted = !entity->deleted;
		entity->defined = false;
		// 8.4.2p1: a function explicitly defaulted on its first declaration is
		// implicitly inline, so the definition the standard gives it belongs to
		// every unit that needs one, exactly as 12.1p5's does.
		entity->inline_function = true;
		return;
	}
	if (node.kind != AstKind::SpecialMemberDefinition)
	{
		return;
	}
	// 12.1p4 and 8.5.1p1: a body the program wrote is what makes the function
	// user-provided, which stops the class from being an aggregate.
	entity->user_provided = true;
	entity->defined = true;

	DumpScope& dump = model_.open_dump(*ctx.dump, "scope function " + written);
	Scope& inner = model_.open(ScopeKind::Function, *ctx.scope, entity, &dump);
	SemaEntity& self =
		model_.create(SemaKind::Parameter, "this", this_type(*entity));
	model_.bind(inner, self.name, self);
	model_.declare_in(inner, self);
	// 9.2p2: the body and the mem-initializers are read where the class is
	// complete, which is the end of the translation unit.
	Pending pending;
	pending.function = entity;
	pending.self = &self;
	pending.body = &node;
	pending.scope = &inner;
	pending.parameters = parameters;
	pending.initializers = child_of(node, AstKind::CtorInitializer);
	pending.members = ctx.scope;
	pending_.push_back(pending);
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
	constructor.special = kConstructorFunction;
	constructor.defaulted = true;
	model_.declare_in(scope, constructor);
	entity.constructor = &constructor;
}

// 12.4p3: a class with no declared destructor has one, declared where the
// definition of the class ends and taking the object it destroys as the only
// parameter 9.3.1p3 gives a member function.
void SemaAnalyzer::declare_destructor(SemaEntity& entity, Scope& scope)
{
	std::vector<TypeId> parameters;
	// 12.4p12: a destructor may be invoked for an object of any cv-qualified
	// version of its class, which the object parameter is what says.
	parameters.push_back(
		types_.pointer_to(types_.qualified(entity.type, kCvConst | kCvVolatile)));
	const std::string spelled =
		"~" + QualifiedName(types_.user_name(entity.type)).last();
	SemaEntity& destructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), parameters, false));
	destructor.dump_name = scope.prefix + spelled;
	destructor.object_member = true;
	destructor.inline_function = true;
	destructor.trivial = trivial_destruction(scope);
	destructor.tail = &destructor;
	destructor.special = kDestructorFunction;
	destructor.defaulted = true;
	model_.declare_in(scope, destructor);
	// 12.4p12 and 5.2.4: `x.~C()` names the destructor through the class, so
	// the one name a lookup can reach it by is bound where it is declared.
	model_.bind(scope, spelled, destructor);
	entity.destructor = &destructor;
}

// 8.5.1p1: whether an object of the class `scope` declares is initialized from
// a braced-init-list by initializing its members with the clauses.  A class
// with a base class is no aggregate, which the caller asks before this, and the
// PA16 slice has no virtual function - so what is left to ask is whether every
// non-static data member is public, none was written with a
// brace-or-equal-initializer, and the program provided no constructor - which
// 12.1p4 does not count `= default` or `= delete` as doing.
bool SemaAnalyzer::aggregate_class(Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.kind == SemaKind::Function)
		{
			if (member.special == kConstructorFunction && member.user_provided)
			{
				return false;
			}
			continue;
		}
		if (member.kind != SemaKind::Variable || !member.object_member)
		{
			continue;
		}
		if (member.access != kPublicAccess || member.default_initializer)
		{
			return false;
		}
	}
	return true;
}

// 11.2: whether a context in `from` may name `member`.  A member declared
// `public` is named from anywhere; any other is named only from inside the
// class that declared it, which 11.7 also gives to a class nested in it.
// 3.3.6: the innermost namespace a region is written in, which 7.3.1.2p3 makes
// a friend-declared function a member of however deeply the class that declared
// it is nested.
Scope& SemaAnalyzer::friend_namespace(Scope& scope)
{
	Scope* at = &scope;
	while (at->kind != ScopeKind::Namespace && at->parent != nullptr)
	{
		at = at->parent;
	}
	return *at;
}

// 11.3p1: the class a friend declaration written in it grants access to, which
// is the innermost class around the declaration.
SemaEntity* SemaAnalyzer::granting_class(const Context& ctx) const
{
	for (Scope* at = ctx.scope; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class)
		{
			return at->owner;
		}
	}
	return nullptr;
}

// 11.3p2: `friend C;` and `friend class C;` name a class and declare nothing.
// The specifiers already found or declared it, so what is left is the grant.
void SemaAnalyzer::grant_class_friendship(const Context& ctx,
                                          const Specifiers& specifiers)
{
	SemaEntity* const granting = granting_class(ctx);
	SemaEntity* friendly = specifiers.introduced;
	if (friendly == nullptr && specifiers.has_type_name)
	{
		friendly = model_.type_owner(types_.strip_cv(specifiers.type_name));
	}
	if (granting == nullptr || friendly == nullptr ||
	    friendly->kind != SemaKind::Class)
	{
		throw std::runtime_error("a friend declaration with no declarator names "
		                         "no class");
	}
	model_.befriend(*granting, *friendly);
}

// 11.3p6 and 7.3.1.2p3: a friend declaration declares its function in the
// innermost enclosing namespace rather than in the class it is written in, and
// - where the declarator-id is unqualified - binds its name nowhere.  A
// qualified declarator-id names a function that region already declares, which
// 11.3p10 is the only thing it may name.
SemaEntity* SemaAnalyzer::friend_target(const Context& ctx,
                                        const QualifiedName& spelled,
                                        Context& target)
{
	SemaEntity* const granting = granting_class(ctx);
	if (granting == nullptr)
	{
		throw std::runtime_error("a friend declaration is written outside a "
		                         "class definition");
	}
	if (!spelled.qualified())
	{
		target.scope = &friend_namespace(*ctx.scope);
		target.dump = target.scope->dump;
	}
	return granting;
}

// 11.3p1: the class `granting` gave `friendly` the reach its own members have.
// A friend is a function or a class, and both are entities a region a name is
// read in is owned by, so the question the access check asks of each region
// around it is the same one.
bool SemaAnalyzer::befriended(const Scope& granting, const Scope& from) const
{
	return granting.owner != nullptr && from.owner != nullptr &&
		model_.befriended(*granting.owner, *from.owner);
}

bool SemaAnalyzer::accessible(const SemaEntity& member, const Scope* from,
                              const Scope* naming_class) const
{
	if (member.access == kPublicAccess || member.region == nullptr)
	{
		return true;
	}
	const bool friends = model_.has_friends();
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return true;
		}
		// 11.3p1 and 11.3p2: a friend of the class reaches every member of it,
		// and so does a member of a befriended class, which is the same
		// question asked of the region that member is read in.
		if (friends && befriended(*member.region, *at))
		{
			return true;
		}
		if (member.access != kProtectedAccess)
		{
			continue;
		}
		// 11.4p1: a protected member is also named from a member of a class
		// derived from the one that declared it, and 11.7 gives that reach to a
		// class nested in such a class as well.
		if (at->kind == ScopeKind::Class && at->base != nullptr &&
		    derives_from(*at->base, *member.region))
		{
			return true;
		}
		// 11.2p5: where the member is named on an object, the access is also
		// granted by a base class of that object's class which itself grants
		// it - so the classes between the one the name was written on and the
		// one that declared the member are each asked in turn.  A private
		// member is not one of those: no class but its own ever reaches it.
		for (const Scope* n = naming_class;
		     friends && n != nullptr && n != member.region; n = n->base)
		{
			if (befriended(*n, *at))
			{
				return true;
			}
		}
	}
	return false;
}

SemaAnalyzer::Naming::Naming(SemaAnalyzer& owner, Scope* region)
	: owner(owner)
	, held(owner.naming_)
{
	if (region != nullptr)
	{
		owner.naming_ = region;
	}
}

SemaAnalyzer::Naming::~Naming()
{
	owner.naming_ = held;
}

// 11p6: a declaration written outside the class it names a member of checks
// every one of its names with the access that class gives, which is what lets
// the return type of `A::I A::f()` and the initializer of `A::I A::x` name a
// private nested type.  The class is the region the declarator-id's
// nested-name-specifier reached; a spelling that reaches none names no member,
// and the declaration that wrote it fails on its own where it is read.
Scope* SemaAnalyzer::naming_context(const std::string& written,
                                    const Context& ctx)
{
	const QualifiedName spelled(written);
	if (!spelled.qualified())
	{
		return nullptr;
	}
	try
	{
		Scope* const region = resolve_prefix(spelled, ctx);
		return region != nullptr && region->kind == ScopeKind::Class ? region
		                                                            : nullptr;
	}
	catch (const std::runtime_error&)
	{
		return nullptr;
	}
}

// 11.2p4 and 11.2p5: a base class reached through a chain of classes is
// accessible only where every base-specifier between the two is, so a
// conversion that spans the chain asks each link in turn rather than the first
// one alone.  One inaccessible link makes the whole conversion ill formed.  The
// caller has already found `base` in the chain, so the walk ends there.
void SemaAnalyzer::require_base_access(const SemaEntity* derived,
                                       const SemaEntity& base)
{
	for (const SemaEntity* at = derived; at != nullptr && at != &base;
	     at = at->base)
	{
		require_base_link(*at);
	}
}

// 11.2p1: the access one base-specifier gave the base it named, asked of where
// the conversion through it is written.  A public base reaches everywhere, a
// protected one reaches the classes derived from the one that named it, and a
// private one reaches that class alone.
void SemaAnalyzer::require_base_link(const SemaEntity& derived)
{
	if (derived.base_access == kPublicAccess || derived.scope == nullptr)
	{
		return;
	}
	const Scope* const from = naming_ != nullptr ? naming_ : reading_;
	const bool friends = model_.has_friends();
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == derived.scope)
		{
			return;
		}
		if (friends && befriended(*derived.scope, *at))
		{
			// 11.2p1 and 11.3p1: a friend of the derived class reaches what
			// the class itself reaches, which is the base its base-specifier
			// named however it named it.
			return;
		}
		if (derived.base_access != kProtectedAccess ||
		    at->kind != ScopeKind::Class)
		{
			continue;
		}
		if (at->base != nullptr && derives_from(*at->base, *derived.scope))
		{
			return;
		}
	}
	throw std::runtime_error("a conversion to a base class of " +
	                         types_.description(derived.type) +
	                         " is written where the access its base-specifier "
	                         "gave it does not reach");
}

// 12.4p11: an object's lifetime ends in a call of the destructor of its class,
// and a program that names an inaccessible function is ill formed - so the
// access 11 gave the destructor is asked for in the region that declares the
// object, which is where that call is written.  A class type reached through an
// array is the same question about each element.
void SemaAnalyzer::require_destruction_access(const SemaEntity& entity,
                                              const Scope* from)
{
	SemaEntity* const destructor = class_destructor(element_of(entity.type));
	if (destructor == nullptr || accessible(*destructor, from))
	{
		return;
	}
	throw std::runtime_error("an object of " + types_.description(entity.type) +
	                         " is declared where the access its class gave the "
	                         "destructor its lifetime ends with does not reach");
}

// 11.4p1: the additional check on a protected non-static member named on an
// object.  Where the access is granted only because the class it occurs in
// derives from the one that declared the member, that class may reach the
// member of its own objects and not of the base's - so the object expression
// has to name a class derived from the one the access occurs in.  A member the
// declaring class itself reaches is not the case 11.4 adds a check to.
void SemaAnalyzer::require_protected_object(
	const std::vector<SemaEntity*>& found, const SemaEntity& member,
	const Scope* from, const Scope* object_class)
{
	if (object_class == nullptr || member.region == nullptr)
	{
		return;
	}
	if (found.empty())
	{
		if (member.access != kProtectedAccess || !member.object_member)
		{
			return;
		}
	}
	else
	{
		// 13.3 has not chosen between the declarations of an overloaded name
		// yet, so the question is asked only where every one the lookup reached
		// answers it the same way: a protected non-static member of the one
		// class.  A name that reached a public declaration too is one whose
		// access the choice settles, and the choice is not made here.
		for (std::size_t index = 0; index < found.size(); ++index)
		{
			for (const SemaEntity* at = found[index]; at != nullptr;
			     at = at->next)
			{
				if (at->access != kProtectedAccess || !at->object_member ||
				    at->region != member.region)
				{
					return;
				}
			}
		}
	}
	if (naming_ != nullptr)
	{
		from = naming_;
	}
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return;
		}
		if (at->kind != ScopeKind::Class || !derives_from(*at, *member.region))
		{
			continue;
		}
		// `at` is the class the access occurs in, and 11.4p1 lets it reach the
		// member of that class and of the classes derived from it alone.
		if (derives_from(*object_class, *at))
		{
			return;
		}
		throw std::runtime_error(member.name + " is a protected member named on "
		                         "an object of a class the one the access is "
		                         "written in does not derive from");
	}
}

// 10p1: whether `derived` is `base` or derives from it, which is one walk of
// the chain the base-clauses left on the regions.
bool SemaAnalyzer::derives_from(const Scope& derived, const Scope& base) const
{
	for (const Scope* at = &derived; at != nullptr; at = at->base)
	{
		if (at == &base)
		{
			return true;
		}
	}
	return false;
}

void SemaAnalyzer::require_access(const SemaEntity& member, const Scope* from,
                                  const Scope* naming_class)
{
	if (naming_ != nullptr)
	{
		// 11p6: the entity being declared is what the access is checked for,
		// wherever in its declaration the name stands.
		from = naming_;
	}
	if (!accessible(member, from, naming_class))
	{
		throw std::runtime_error(member.name + " is named where the access its "
		                         "class gave it does not reach");
	}
}

// 5.2.5p1: whether evaluating this expression is something the program can
// observe.  A name, a constant and the operators that only read them are not;
// anything that calls, assigns or constructs is, and so is any expression
// holding one.  5.3.3p1 leaves the operand of `sizeof` and `alignof`
// unevaluated, so what is written there is never observed.
bool SemaAnalyzer::observable(const DumpNode& node) const
{
	switch (node.fact.kind)
	{
	case FactKind::Sizeof:
		return false;

	case FactKind::Literal:
	case FactKind::Id:
	case FactKind::Member:
	case FactKind::Unary:
	case FactKind::Binary:
	case FactKind::Conditional:
	case FactKind::Subscript:
	case FactKind::Cast:
		break;

	default:
		return true;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (observable(*node.children[index]))
		{
			return true;
		}
	}
	return false;
}

// 5.2.5p1: the object expression of a member access is evaluated whatever the
// member turns out to be.  Where the member is not part of the object the
// access denotes what the member alone does, and the object expression is left
// with nothing to do - which is only true where evaluating it does nothing.
// This milestone has no node that sequences a discarded operand before the
// member, so an object expression that does something is refused rather than
// dropped from the program the output describes.
void SemaAnalyzer::require_droppable(const DumpNode& object,
                                     const std::string& member)
{
	if (observable(object))
	{
		throw std::runtime_error("the object expression of " + member +
		                         " does something, and " + member +
		                         " is not a member of the object it names");
	}
}

// 12.1p5: whether default-initializing an object of the class this region
// declares does nothing at all.  It does nothing when no member asks for
// anything: a member with a brace-or-equal-initializer asks for what 12.6.2p8
// makes it, and a member of class type asks for whatever its own constructor
// is.  Layout has already run, so the class's members are exactly the
// declarations of this region.
bool SemaAnalyzer::trivial_default_construction(Scope& scope)
{
	if (scope.owner != nullptr && scope.owner->base != nullptr)
	{
		// 12.1p5: the base class subobject is constructed too, so what its own
		// constructor does is part of what constructing this class does.
		const SemaEntity* const base = scope.owner->base->constructor;
		if (base != nullptr && !base->trivial)
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &scope)
		{
			continue;
		}
		if (member.default_initializer)
		{
			return false;
		}
		const SemaEntity* const constructor =
			class_constructors(element_of(member.type));
		if (constructor != nullptr && !constructor->trivial)
		{
			return false;
		}
	}
	return true;
}

// 12.4p3: whether destroying an object of the class this region declares does
// nothing at all, which it does when every member's own destruction does.
bool SemaAnalyzer::trivial_destruction(Scope& scope)
{
	if (scope.owner != nullptr && scope.owner->base != nullptr)
	{
		// 12.4p8: the base class subobject is destroyed too.
		const SemaEntity* const base = scope.owner->base->destructor;
		if (base != nullptr && !base->trivial)
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &scope)
		{
			continue;
		}
		const SemaEntity* const destructor =
			class_destructor(element_of(member.type));
		if (destructor != nullptr && !destructor->trivial)
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
			decimal(span.begin) + "_" + decimal(span.end);
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
			construct_object(*storage, line, nullptr, ctx);
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
                                           const Context& target, bool is_static,
                                           const std::string& name,
                                           bool qualified)
{
	if (!semantics() || target.scope->kind != ScopeKind::Class || is_static ||
	    target.scope->owner == nullptr)
	{
		return type;
	}
	if (qualified && declares_static_member(*target.scope, name, type))
	{
		// 9.4.1p2: the definition of a static member function written outside
		// its class shall not repeat `static`, so which kind of member a
		// qualified declarator declares is a fact of the declaration in the
		// class it redeclares rather than of its own specifiers.
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

// 9.4p1: whether `where` declares `name` as a static member function whose
// declarator wrote `type`.  That declaration is the one a definition written
// outside the class redeclares, and 9.4.1p2 makes it the only place `static` is
// written - so without this question the definition declares a second,
// non-static function of the same name, which the call the program wrote does
// not name.  The chain the name heads is indexed by the parameter type list, so
// the question is a probe rather than a walk of the declarations already made.
bool SemaAnalyzer::declares_static_member(Scope& where, const std::string& name,
                                          TypeId type)
{
	SemaEntity* const head = model_.find(where, name, LookupKind::Any);
	if (head == nullptr || head->kind != SemaKind::Function)
	{
		return false;
	}
	const SemaEntity* const prior =
		model_.overload_of(*head, types_.signature(type));
	return prior != nullptr && !prior->object_member;
}

SemaEntity* SemaAnalyzer::class_constructors(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner == nullptr ? nullptr : owner->constructor;
}

// 8.5.1p1: whether an object of `type` is initialized from a braced-init-list
// by initializing its members with the clauses rather than by a constructor.
bool SemaAnalyzer::aggregate_type(TypeId type)
{
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner != nullptr && owner->aggregate;
}

SemaEntity* SemaAnalyzer::class_destructor(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner == nullptr ? nullptr : owner->destructor;
}

// The object a constructor-action runs on, written as its address, which is the
// argument 9.3.1p3 made the constructor's first parameter.
void SemaAnalyzer::write_constructed_object(SemaEntity& variable,
                                            DumpNode& call, Placement where,
                                            Value& object)
{
	if (where == Placement::Base)
	{
		// 12.6.2p5: the base class subobject of the object this constructor is
		// running on, whose address is what 4.10p3's conversion of `this`
		// already is - so no address is taken around it.
		object = base_value(this_value(call), variable, false);
		return;
	}
	// 5.3.1p3 writes the address around the object, so the object's own line
	// stands under the one the address takes rather than in place of it.
	DumpNode& node = model_.open_node(call, std::string());
	DumpNode& inner = model_.open_node(node, std::string());
	if (where == Placement::Member)
	{
		// 12.6.2: the subobject is a member of the object the constructor being
		// written was called on, so it is named through `this`.
		object = member_value(variable, implied_object(variable, inner),
		                      variable.name, inner);
	}
	else
	{
		object.type = object.spelled = variable.type;
		object.category = ValueCategory::LValue;
		object.what = "id-expression";
		object.entity = &variable;
		object.payload = variable.name;
		object.node = &inner;
		respell(object);
	}
	address_of_object(object, node, false);
}

// 8.5, 12.1 and 13.3.1.3: an object of class type is initialized by one of the
// constructors of its class, chosen from the arguments its initializer wrote.
// The action is one call like any other, written under the declaration of the
// object, and the definition of the constructor it names is asked for here.
void SemaAnalyzer::construct_object(SemaEntity& variable, DumpNode& line,
                                    const AstNode* written, const Context& ctx,
                                    Placement where, bool copied)
{
	const bool member = where != Placement::Named;

	if (!types_.is_class(types_.strip_cv(variable.type)))
	{
		// 8.5p6: default-initializing an object of any other type performs no
		// initialization, and there is nothing for the output to describe.
		return;
	}
	SemaEntity* const head = class_constructors(variable.type);
	if (head == nullptr)
	{
		// 3.9p6 and 9.2p2: an object needs a complete class, and 12.1p5 gives
		// every complete one the output describes a constructor, so a class
		// with none here is one this translation unit never defined.
		throw std::runtime_error("an object of the incomplete class type " +
		                         types_.description(variable.type) +
		                         " is declared");
	}
	// 8.5p15 and 8.5p16: which of the arguments the program wrote reach the
	// constructor, and whether 13.3.1.4 leaves out the ones declared `explicit`.
	const AstNode* list = nullptr;
	bool converting = false;
	// 8.5p14 and 8.5p16: only `= { ... }` is copy-list-initialization.  `= e`
	// is copy-initialization, which 13.3.1.4 answers by leaving the `explicit`
	// constructors out of the candidates rather than by refusing one, and
	// `= T(...)` is the direct-initialization 12.8p31 elides into.
	const bool copy_list =
		copied && written != nullptr && written->kind == AstKind::BracedInitList;
	if (written != nullptr)
	{
		if (is_initializer_list(written->kind))
		{
			list = written;
		}
		else
		{
			// 8.5p14: copy-initialization from one expression, which only a
			// converting constructor may answer.
			converting = true;
		}
	}
	if (converting && written->kind == AstKind::CallExpression &&
	    !written->children.empty() &&
	    written->children[0]->kind == AstKind::IdExpression)
	{
		// 12.8p31 and 5.2.3p1: a class object copy-initialized from a prvalue
		// of its own type is initialized by whatever makes that prvalue, so the
		// arguments of `T(...)` are the constructor's and no object of the type
		// stands between them.
		SemaEntity* const named =
			resolve(written->children[0]->text, ctx, LookupKind::Type);
		if (named != nullptr && names_a_type(*named) &&
		    types_.strip_cv(named->type) == types_.strip_cv(variable.type))
		{
			list = call_arguments(*written);
			converting = false;
		}
	}

	Value source;
	if (converting)
	{
		// 8.5p14: the initializer is read before anything is written for the
		// initialization, because 12.8p31 lets a value of the object's own type
		// be what initializes it, with no constructor standing between them.
		source = expression(*written, ctx, line);
		if (types_.strip_cv(source.type) == types_.strip_cv(variable.type) &&
		    !member)
		{
			return;
		}
		line.children.pop_back();
	}
	DumpNode& action = model_.open_node(line, std::string());
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = variable.type;
	DumpNode& call = model_.open_node(action, std::string());
	DumpNode& callee = model_.open_node(call, std::string());
	Value object;
	write_constructed_object(variable, call, where, object);
	std::vector<Value> arguments;
	if (list != nullptr)
	{
		for (std::size_t index = 0; index < list->children.size(); ++index)
		{
			arguments.push_back(expression(*list->children[index], ctx, call));
		}
	}
	if (source.node != nullptr)
	{
		// The one argument of a copy-initialization was read before the action
		// was opened, so its line moves into the place the call gives it.
		call.children.push_back(source.node);
		arguments.push_back(source);
	}

	std::vector<SemaEntity*> candidates(1, head);
	SemaEntity& constructor = *select_overload(candidates, arguments,
	                                           head->name, &object, converting);
	require_access(constructor, ctx.scope);
	if (copy_list && constructor.explicit_function)
	{
		// 8.5.4p3: copy-list-initialization that chooses an `explicit`
		// constructor is ill formed, which is not the same as leaving one out
		// of the candidates: the choice is made and then refused.
		throw std::runtime_error("a copy-list-initialization of " +
		                         types_.description(variable.type) +
		                         " chooses a constructor declared explicit");
	}
	if (constructor.deleted)
	{
		// 8.4.3p2: a program that names a deleted function is ill formed.
		throw std::runtime_error("a deleted constructor of " +
		                         types_.description(variable.type) +
		                         " is what initializes an object of it");
	}
	if (where != Placement::Base)
	{
		// 12.1 and the ABI: what this action creates is a complete object -
		// a named one, or the member subobject that is one of its own - so it
		// runs the complete-object entry of the constructor.  A base class
		// subobject is the one case that runs the base-object entry instead.
		constructor.complete_object_entry = true;
	}
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		const Match match = match_argument(arguments[index],
		                                   parameters[index + 1]);
		apply_conversion(arguments[index], parameters[index + 1], match);
	}
	for (std::size_t index = arguments.size() + 1; index < parameters.size();
	     ++index)
	{
		// 8.3.6p1: the constructor is called as if the default-argument had
		// been written where the argument is missing.
		write_default_argument(constructor, index, call);
	}
	action.text = "constructor-action " + constructor.dump_name;
	action.fact.entity = &constructor;
	call.text = spell("call-expression", ValueCategory::PRValue,
	                  types_.target(constructor.type), std::string());
	set_fact(call, FactKind::Call, types_.target(constructor.type),
	         ValueCategory::PRValue);
	callee.text = "callee " + constructor.dump_name + " " +
		types_.description(constructor.type);
	set_fact(callee, FactKind::Callee, constructor.type, ValueCategory::LValue);
	callee.fact.entity = &constructor;
	if (constructor.defined || constructor.deleted)
	{
		return;
	}
	// 12.1p5: the definition is what odr-using the constructor asks for, and
	// one use is what asks for it.  A constructor the program declared without
	// defining is one this unit has no body for, and nothing asks for one.
	if (!constructor.defaulted)
	{
		// A constructor the program declared without defining is one this unit
		// has no body for, so a use of it is a call of a definition elsewhere.
		return;
	}
	constructor.defined = true;
	Pending pending;
	pending.function = &constructor;
	pending.self = &model_.create(SemaKind::Parameter, "this", parameters[0]);
	pending.members = constructor.region;
	pending_.push_back(pending);
}

// 9.3.2p1: the type `this` has in the body of a member function, which is a
// pointer to the class qualified by the function's own cv-qualifier-seq.  For
// every member function but one that is the object parameter 9.3.1p3 gives the
// function's type.  12.4p1 gives a destructor no cv-qualifier-seq at all, and
// the const volatile 12.4p12 puts on its object parameter says which objects it
// may be called for rather than what its body may do to the one it is
// destroying, so a destructor's `this` drops it.
TypeId SemaAnalyzer::this_type(const SemaEntity& function)
{
	const TypeId object = types_.parameters(function.type)[0];
	if (function.special != kDestructorFunction)
	{
		return object;
	}
	return types_.pointer_to(types_.strip_cv(types_.target(object)));
}

// 12.4p3 and 3.8p1: the end of the lifetime of an object of class type is one
// call of the destructor of its class on it.  A destructor that does nothing is
// no action at all, so nothing is written for one.
void SemaAnalyzer::destructor_action(SemaEntity& entity, DumpNode& parent,
                                     Placement where)
{
	SemaEntity* const destructor = class_destructor(element_of(entity.type));
	if (destructor == nullptr)
	{
		return;
	}
	if (destructor->deleted)
	{
		// 8.4.3p2 and 12.4p11: a program that names a deleted function is ill
		// formed, and the end of an object's lifetime is what names its
		// destructor - so declaring the object is what is refused, rather than
		// its lifetime ending in a call of a definition nothing writes.
		throw std::runtime_error("an object of " +
		                         types_.description(entity.type) +
		                         " is declared, and the destructor its lifetime "
		                         "ends with is deleted");
	}
	if (destructor->trivial)
	{
		return;
	}
	if (where != Placement::Base)
	{
		// 12.4 and the ABI: the lifetime this ends is a complete object's, so
		// the call is of the complete-object entry of the destructor.  Only the
		// base class subobject of an object runs the base-object entry.
		destructor->complete_object_entry = true;
	}
	if (types_.kind(types_.strip_cv(entity.type)) == TypeKind::Array)
	{
		// 12.4p12 destroys the elements in reverse order, which this milestone
		// does not write yet, so the object is left rather than half destroyed.
		throw std::runtime_error("an array of a class with a destructor is "
		                         "declared, which this milestone does not end "
		                         "the lifetime of");
	}
	DumpNode& action = model_.open_node(
		parent, "destructor-action " + destructor->dump_name);
	action.fact.kind = FactKind::DestructorAction;
	action.fact.entity = destructor;
	action.fact.type = entity.type;
	if (where == Placement::Base)
	{
		// 12.4p8: the base class subobject of the object being destroyed, which
		// 4.10p3's conversion of `this` names.
		base_value(this_value(action), entity, false);
	}
	else if (where == Placement::Member)
	{
		DumpNode& node = model_.open_node(action, std::string());
		member_value(entity, implied_object(entity, node), entity.name, node);
	}
	else
	{
		DumpNode& node = model_.open_node(action, std::string());
		Value object;
		object.type = object.spelled = entity.type;
		object.category = ValueCategory::LValue;
		object.what = "id-expression";
		object.entity = &entity;
		object.payload = entity.name;
		object.node = &node;
		respell(object);
	}
	if (destructor->defined || !destructor->defaulted)
	{
		return;
	}
	// 12.4p6: the definition of an implicitly declared destructor is what
	// odr-using it asks for.
	destructor->defined = true;
	Pending pending;
	pending.function = destructor;
	pending.self =
		&model_.create(SemaKind::Parameter, "this", this_type(*destructor));
	pending.members = destructor->region;
	pending_.push_back(pending);
}

// 12.6.2p2: whether a mem-initializer-id spells the base class of the
// constructor's own class.  The base has a name of its own and every alias of
// it names it too, so the question is asked of what the name denotes rather
// than of the characters it was written with.
bool SemaAnalyzer::names_the_base(const std::string& written,
                                  const SemaEntity& base, const Context& ctx)
{
	try
	{
		SemaEntity* const found = resolve(written, ctx, LookupKind::Type);
		return found != nullptr && names_a_type(*found) &&
			types_.strip_cv(found->type) == types_.strip_cv(base.type);
	}
	catch (const std::runtime_error&)
	{
		// A name that reaches no region names no base either, and the
		// mem-initializer it was written in is refused where it is read.
		return false;
	}
}

// 12.6.2: what a constructor initializes before its body runs.  Every non-static
// data member of the class is initialized, in the declaration order 12.6.2p10
// gives them whatever order the mem-initializers were written in: by the
// mem-initializer that names it, else by the brace-or-equal-initializer its own
// declaration wrote (12.6.2p8), else by default-initialization, which for
// anything but a class type leaves it holding no value the program may read.
void SemaAnalyzer::write_member_initializations(const Pending& pending,
                                                DumpNode& line,
                                                const Context& inner)
{
	Scope& members = *pending.members;
	// 12.6.2p10: the members are initialized in declaration order and the
	// mem-initializers may be written in any, so which one names each member is
	// asked once per member rather than by a scan of the list per member.
	std::unordered_map<std::string, MemInitializer> named;
	for (std::size_t at = 0;
	     pending.initializers != nullptr &&
	     at < pending.initializers->children.size(); ++at)
	{
		const AstNode& one = *pending.initializers->children[at];
		const AstNode* const id = child_of(one, AstKind::MemInitializerId);
		if (id == nullptr)
		{
			continue;
		}
		// 12.6.2p2: the mem-initializer's arguments are read in the
		// constructor's own region, where its parameters stand.
		MemInitializer wrote;
		wrote.written = one.children.size() > 1 ? one.children[1] : nullptr;
		wrote.spelled = id->text;
		if (!named.insert(std::make_pair(QualifiedName(id->text).last(), wrote))
		         .second)
		{
			// 12.6.2p6: a ctor-initializer that writes more than one
			// mem-initializer for one member is ill formed, which is not the
			// same as the second one being the one that has no effect.
			throw std::runtime_error("a constructor of " +
			                         types_.description(pending.function->type) +
			                         " initializes " + id->text + " twice");
		}
	}
	// 12.6.2p10: the base class subobject is initialized first, whatever place
	// its mem-initializer was written in and whether or not one was.
	SemaEntity* const base =
		members.owner != nullptr ? members.owner->base : nullptr;
	if (base != nullptr)
	{
		const AstNode* written = nullptr;
		const std::string spelled =
			QualifiedName(types_.user_name(base->type)).last();
		std::unordered_map<std::string, MemInitializer>::iterator wrote =
			named.find(spelled);
		if (wrote == named.end())
		{
			// 12.6.2p2: the mem-initializer-id may be any name for the base
			// class, which a typedef-name is one of.  Its own name is asked
			// about first, so only a ctor-initializer that spelled the base some
			// other way costs one lookup per mem-initializer it wrote.
			for (wrote = named.begin(); wrote != named.end(); ++wrote)
			{
				if (names_the_base(wrote->second.spelled, *base, inner))
				{
					break;
				}
			}
		}
		if (wrote != named.end())
		{
			written = wrote->second.written;
			wrote->second.used = true;
		}
		if (written != nullptr || !trivially_constructed(base->type))
		{
			construct_object(*base, line, written, inner, Placement::Base);
		}
	}
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &members)
		{
			continue;
		}
		const AstNode* written = nullptr;
		Context where = inner;
		const std::unordered_map<std::string, MemInitializer>::iterator
			wrote = named.find(member.name);
		if (wrote != named.end())
		{
			written = wrote->second.written;
			wrote->second.used = true;
		}
		if (written == nullptr && member.default_initializer)
		{
			// 12.6.2p8 and 9.2p2: a brace-or-equal-initializer is read in the
			// class it was written in, which is a complete-class context.
			const std::unordered_map<std::uint32_t, Default>::const_iterator
				found = member_initializers_.find(member.id);
			if (found != member_initializers_.end())
			{
				written = found->second.written;
				where.scope = found->second.scope;
				where.dump = where.scope->dump;
			}
		}
		where.node = nullptr;
		const TypeId type = member.type;
		const bool braced =
			written != nullptr && written->kind == AstKind::BracedInitList;
		if (types_.is_class(types_.strip_cv(type)) &&
		    !(braced && aggregate_type(type)))
		{
			// 12.6.2p8 and 12.1p5: a member no initializer reaches is
			// default-initialized, and where that does nothing at all there is
			// no action to write and no subobject to name.
			if (written == nullptr && trivially_constructed(type))
			{
				continue;
			}
			// The action names the member through `this`, so it needs no line
			// of its own to say which subobject is being initialized.
			construct_object(member, line, written, where, Placement::Member);
			continue;
		}
		if (written == nullptr)
		{
			// 8.5p6 and 12.6.2p8: a member of any other type that no
			// initializer reaches is default-initialized, which does nothing.
			continue;
		}
		DumpNode& node = open_fact(line, "member-initialization " + member.name +
		                           " " + types_.description(type),
		                           FactKind::MemberInitialization);
		node.fact.entity = &member;
		node.fact.type = type;
		node.fact.spelled = type;
		// 9.2p13: where the member is, is where its class put it, so the tree
		// names the object it is part of and the member it is, and nothing
		// below has to read a member access to learn either.
		implied_object(member, node);
		if (written->kind != AstKind::BracedInitList &&
		    is_initializer_list(written->kind))
		{
			// 8.5p16: direct-initialization of a member of non-class type takes
			// the one expression written in the parentheses.
			if (written->children.empty())
			{
				// 8.5p10: `m()` value-initializes the member, which for these
				// types is the zero of it.
				DumpNode& zero = model_.open_node(
					node, spell("literal", ValueCategory::PRValue, type, "0"));
				set_fact(zero, FactKind::Literal, type, ValueCategory::PRValue);
				zero.fact.constant = true;
				continue;
			}
			if (written->children.size() != 1)
			{
				throw std::runtime_error("a mem-initializer of " + member.name +
				                         " passes more than one argument to a "
				                         "member of non-class type");
			}
			initialize(*written->children[0], type, where, node);
			continue;
		}
		initialize(*written, type, where, node);
	}
	// 12.6.2p2: a mem-initializer-id shall name a non-static data member of the
	// constructor's class or one of its bases, so one that named neither is
	// refused rather than left as arguments nothing evaluates.  Every member
	// was reached above, so what is left unused is what named nothing.
	for (std::unordered_map<std::string, MemInitializer>::const_iterator at =
	         named.begin(); at != named.end(); ++at)
	{
		if (!at->second.used)
		{
			throw std::runtime_error("a constructor of " +
			                         types_.description(pending.function->type) +
			                         " has a mem-initializer for " +
			                         at->second.spelled +
			                         ", which names neither a base class nor a "
			                         "non-static data member of the class");
		}
	}
}

// 12.4p8: after a destructor's body has run, the destructors of the class's
// members run, in the reverse of the order the members were constructed in.
void SemaAnalyzer::write_member_destructions(Scope& members, DumpNode& line)
{
	for (std::size_t index = members.declarations.size(); index-- > 0;)
	{
		SemaEntity& member = *members.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &members)
		{
			continue;
		}
		// 12.4p5 and 12.4p11: the destructor of the class is what names the
		// destructor of each of its members, so that is where the access 11 gave
		// the member's own is asked for.
		require_destruction_access(member, &members);
		destructor_action(member, line, Placement::Member);
	}
	if (members.owner != nullptr && members.owner->base != nullptr)
	{
		// 12.4p8: the base class subobject is destroyed after every member, in
		// the reverse of the order 12.6.2p10 constructed them in.
		require_destruction_access(*members.owner->base, &members);
		destructor_action(*members.owner->base, line, Placement::Base);
	}
}

// 3.7.1: which region ends the lifetime of the object a definition declares,
// which is what its storage duration says.  An object that is not local has
// static storage duration however it was declared - at namespace scope, or as
// the static data member 9.4.2p2 defines outside its class - and 3.6.3p1 ends
// it when the program does; a local one ends with the block that declared it.
void SemaAnalyzer::record_lifetime(SemaEntity& entity, const Context& target,
                                   bool is_static)
{
	// 12.4p11: whichever region ends it, the lifetime ends in a call of the
	// destructor of the object's class, and the region that declares the object
	// is where that call is named.
	require_destruction_access(entity, target.scope);
	if (target.scope->kind == ScopeKind::Namespace ||
	    target.scope->kind == ScopeKind::Class)
	{
		static_lifetimes_.push_back(&entity);
		return;
	}
	if (is_static)
	{
		// 3.7.1p3 and 6.7p4: a block-scope `static` object is one object of the
		// program, initialized the first time control passes through its
		// declaration and destroyed at 3.6.3p1's shutdown.  Writing it as the
		// automatic object of its block would describe a different program, and
		// the guard 6.7p4 asks for is not part of this milestone.
		throw std::runtime_error(
			"a block-scope static object of " + types_.description(entity.type) +
			" is declared, whose one initialization and shutdown destruction "
			"this milestone does not write");
	}
	if (!lifetimes_.empty())
	{
		lifetimes_.back().push_back(&entity);
		if (ends_in_call(entity))
		{
			++live_destructions_;
		}
	}
}

void SemaAnalyzer::open_lifetimes()
{
	lifetimes_.push_back(std::vector<SemaEntity*>());
}

void SemaAnalyzer::close_lifetimes(DumpNode& line)
{
	// 3.8p1 and 6.7p2: the objects a block declared are destroyed where control
	// leaves it, in reverse order of construction.
	std::vector<SemaEntity*>& frame = lifetimes_.back();
	for (std::size_t index = frame.size(); index-- > 0;)
	{
		if (ends_in_call(*frame[index]))
		{
			--live_destructions_;
		}
		destructor_action(*frame[index], line, Placement::Named);
	}
	lifetimes_.pop_back();
}

void SemaAnalyzer::leave_lifetimes(std::size_t depth, DumpNode& line)
{
	// 6.6p2 and 3.8p1: the blocks a jump leaves are the ones opened since the
	// statement it jumps out of, and the objects of each are destroyed in the
	// reverse of the order they were constructed in, innermost block first.
	for (std::size_t at = lifetimes_.size(); at-- > depth;)
	{
		const std::vector<SemaEntity*>& frame = lifetimes_[at];
		for (std::size_t index = frame.size(); index-- > 0;)
		{
			destructor_action(*frame[index], line, Placement::Named);
		}
	}
}

void SemaAnalyzer::unwind_lifetimes(DumpNode& line)
{
	leave_lifetimes(0, line);
}

// 12.1p5: whether default-initializing an object of `type` does nothing at all,
// which is the one question that says a subobject needs no action written for
// it.  12.6.2p8 leaves such a member with no initialization to describe.
bool SemaAnalyzer::trivially_constructed(TypeId type)
{
	for (const SemaEntity* at = class_constructors(element_of(type));
	     at != nullptr; at = at->next)
	{
		if (types_.parameters(at->type).size() == 1)
		{
			return at->trivial;
		}
	}
	return false;
}

// 12.4p3: whether the end of this object's lifetime is a call rather than
// nothing at all, which is the one question every count of live objects asks.
bool SemaAnalyzer::ends_in_call(const SemaEntity& entity)
{
	const SemaEntity* const destructor =
		class_destructor(element_of(entity.type));
	return destructor != nullptr && !destructor->trivial;
}

bool SemaAnalyzer::lifetimes_pending() const
{
	// The count is carried rather than recomputed: a jump asks this question
	// once per jump, and walking every open block for it would cost the depth
	// of the blocks around each one.
	return live_destructions_ != 0;
}