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

// 7.6.2p1: the strictest alignment the class-head or the decl-specifier-seq
// asked for, or zero when it asked for none.  An alignment-specifier whose
// operand is not a constant this translation knows asks for nothing it can act
// on.
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
		const Constant value = evaluate(*child.children[0], ctx);
		const unsigned long long asked = value.bits;
		// 7.6.2p3: what an alignment-specifier asks for is a fundamental
		// alignment - a power of two no greater than the widest one the
		// implementation gives an object - and zero, which asks for nothing.
		// Anything else is no alignment at all, so it is refused here rather
		// than laid out as though a class could begin every sixth byte.
		if (is_signed(value.type) && (asked >> 63) != 0)
		{
			throw std::runtime_error("an alignment-specifier asks for a "
			                         "negative alignment");
		}
		if (asked != 0 && (asked & (asked - 1)) != 0)
		{
			throw std::runtime_error("an alignment-specifier asks for an "
			                         "alignment that is not a power of two");
		}
		if (asked > wanted)
		{
			wanted = asked;
		}
	}
	return wanted;
}

// 9.6p2: the type the storage unit holding a bit-field is read and written at.
// The unit is a run of bytes rather than an object, so what names it is its
// width - the width the declared type gave it - and not what the member was
// declared with: the bits it holds beside the field's own belong to the other
// members of the unit, and shifting and masking them is arithmetic on a pattern
// of bits.  It is spelled signed at that width, and what the field is worth
// keeps the type the member was declared with, which is what leaves the
// conversion above a read the one that type asks for.
TypeId SemaAnalyzer::bit_field_access_type(TypeId declared)
{
	const TypeId bare = types_.strip_cv(declared);
	switch (types_.object_size(bare))
	{
	case 1: return types_.fundamental(FT_SIGNED_CHAR);
	case 2: return types_.fundamental(FT_SHORT_INT);
	case 4: return types_.fundamental(FT_INT);
	default: break;
	}
	return types_.fundamental(FT_LONG_INT);
}

// 9.6p1: a member declaration that writes a width declares a bit-field, whose
// type shall be integral or enumeration and whose width shall be a constant
// expression no wider than the storage that type holds.  The declarators are
// read through the ordinary member path, so a bit-field is a data member like
// any other but for the four facts the width settles; the unnamed form declares
// a member no name reaches, which is there only so that layout counts its bits.
void SemaAnalyzer::bit_field_declaration(const AstNode& node,
                                         const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		const AstNode& field = *node.children[index];
		const AstNode& written = *field.children[field.children.size() - 1];
		const Constant value = evaluate(written, ctx);
		if (is_signed(value.type) && (value.bits >> 63) != 0)
		{
			throw std::runtime_error("a bit-field has a negative width");
		}
		const std::size_t before = ctx.scope->declarations.size();
		if (field.children.size() > 1)
		{
			init_declarator(*field.children[0], nullptr, specifiers, ctx);
		}
		else
		{
			// 9.6p2: an unnamed bit-field is not a member the program can name,
			// but the bits it asks for are part of the object all the same, so
			// it is declared into the region and bound to nothing.
			SemaEntity& unnamed = model_.create(SemaKind::Variable,
			                                    std::string(),
			                                    specifier_type(specifiers));
			model_.declare_in(*ctx.scope, unnamed);
			unnamed.region = ctx.scope;
			unnamed.object_member = true;
		}
		if (ctx.scope->declarations.size() != before + 1)
		{
			throw std::runtime_error("a bit-field declarator declares no "
			                         "member");
		}
		SemaEntity& member = *ctx.scope->declarations[before];
		if (!member.object_member)
		{
			throw std::runtime_error("a bit-field is declared `static`, which "
			                         "9.6p1 does not allow");
		}
		const TypeId bare = types_.strip_cv(member.type);
		if (!types_.is_integral(bare) && types_.kind(bare) != TypeKind::Enum)
		{
			throw std::runtime_error("a bit-field does not have integral or "
			                         "enumeration type");
		}
		if (value.bits > 8 * types_.object_size(bare))
		{
			throw std::runtime_error("a bit-field is wider than the type it "
			                         "was declared with");
		}
		if (value.bits == 0 && !member.name.empty())
		{
			// 9.6p2: only an unnamed bit-field may have a width of zero.
			throw std::runtime_error("a named bit-field has a width of zero");
		}
		member.bit_field = true;
		member.bit_width = static_cast<unsigned>(value.bits);
		member.bit_access = bit_field_access_type(member.type);
	}
}

// 9.6p2: where one bit-field's bits go, given the byte the class has reached
// and the storage unit it is filling.  A bit-field is allocated into a whole
// storage unit of its declared type - the unit is what the object gives it, and
// the field owns a run of bits inside it - and the fields that follow share that
// unit while they were declared with the same type and their bits still fit.
// Anything else, a field of another type or an ordinary member, begins after the
// unit ends rather than inside it, which is what keeps one member's storage out
// of another's and every access one load and one mask.  An unnamed field of
// width zero allocates nothing: it ends the open unit, which is what 9.6p2's
// separator is for.
void SemaAnalyzer::lay_out_bit_field(SemaEntity& member,
                                     unsigned long long& at, BitUnit& unit)
{
	const TypeId bare = types_.strip_cv(member.type);
	const unsigned long long size = types_.object_size(bare);
	const unsigned long long boundary = types_.object_align(bare);
	if (member.bit_width == 0)
	{
		unit.open = false;
		at = round_up(at, boundary);
		member.offset = at;
		member.bit_offset = 0;
		return;
	}
	if (!unit.open || unit.type != bare ||
	    unit.used + member.bit_width > 8 * size)
	{
		unit.open = true;
		unit.type = bare;
		unit.at = round_up(at, boundary);
		unit.used = 0;
		at = unit.at + size;
	}
	member.offset = unit.at;
	member.bit_offset = static_cast<unsigned>(unit.used);
	unit.used += member.bit_width;
}

// 12.8p2: the class a member is copied as, which for an array of class type is
// its element - 12.8p15 copies an array member element by element.
TypeId SemaAnalyzer::member_copy_type(TypeId type)
{
	TypeId at = types_.strip_cv(type);
	while (types_.kind(at) == TypeKind::Array)
	{
		at = types_.strip_cv(types_.target(at));
	}
	return at;
}

// 12.8p1 and 12.8p6: whether the program wrote a copy constructor of this
// class - a constructor taking one argument beside the object, of the class
// itself or of a reference to it, that is neither implicitly declared nor
// defaulted or deleted.  That is what says a copy of an object of the class is
// what the program wrote rather than the copy of its bytes.
bool SemaAnalyzer::declares_copy_constructor(const SemaEntity& entity,
                                             Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& declared = *scope.declarations[index];
		if (declared.kind != SemaKind::Function || declared.region != &scope ||
		    declared.shadowed != nullptr || declared.name != entity.name ||
		    declared.defaulted || declared.deleted)
		{
			continue;
		}
		const std::vector<TypeId>& parameters = types_.parameters(declared.type);
		// 9.3.1p3 put the object 12.1 constructs in the type, so a constructor
		// that copies takes exactly one argument beside it.
		if (parameters.size() != 2)
		{
			continue;
		}
		const TypeId wanted = parameters[1];
		const TypeId bare = types_.strip_cv(
			types_.is_reference(wanted) ? types_.target(wanted) : wanted);
		if (bare == types_.strip_cv(entity.type))
		{
			return true;
		}
	}
	return false;
}

void SemaAnalyzer::lay_out_class(SemaEntity& entity, Scope& scope, bool is_union,
                                 unsigned long long requested)
{
	// 9.2p13 allocates each member at the next address its alignment allows,
	// and 9.6p2 gives a bit-field a share of a storage unit instead.  The walk
	// carries both: how far into the object it has reached, and the unit the
	// bit-fields declared so far are filling.  A class with no bit-field never
	// opens one, so it is laid out exactly as it was before.
	unsigned long long size = 0;
	BitUnit unit;
	unsigned long long align = 1;
	bool empty = true;
	// 12.8p25 and 9p6: whether a copy of an object of this class is the copy of
	// its bytes.  It is not where the program wrote a copy constructor of its
	// own, nor where any subobject's copy is not, because 12.8p15 makes the
	// copy memberwise and each member's own copy constructor is what copies it.
	bool trivially_copied = !declares_copy_constructor(entity, scope);
	if (entity.base != nullptr)
	{
		// 10p1 and the course ABI: the direct base subobject begins where the
		// derived object does, and the members are laid out after it.  A base
		// that holds nothing is given no storage of its own, so the derived
		// class starts its members where the base did.
		align = types_.object_align(entity.base->type);
		if (!types_.is_trivially_copied(types_.strip_cv(entity.base->type)))
		{
			trivially_copied = false;
		}
		if (!entity.base->empty_class)
		{
			size = types_.object_size(entity.base->type);
			empty = false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		const unsigned long long member_size = types_.object_size(member.type);
		// 7.6.2p1 and 7.6.2p5: an alignment-specifier on the member asks for an
		// alignment at least as strict as its type's own, and the member is
		// allocated at the next address that one allows.
		const unsigned long long declared_align =
			types_.object_align(member.type);
		if (member.requested_align != 0 && member.requested_align < declared_align)
		{
			throw std::runtime_error("a member asks for an alignment weaker "
			                         "than the one its type needs");
		}
		const unsigned long long member_align =
			member.requested_align > declared_align ? member.requested_align
			                                        : declared_align;
		if (!types_.is_trivially_copied(member_copy_type(member.type)))
		{
			trivially_copied = false;
		}
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
			member.bit_offset = 0;
			size = member_size > size ? member_size : size;
			continue;
		}
		if (member.bit_field)
		{
			lay_out_bit_field(member, size, unit);
			continue;
		}
		// 9.2p13: the members are allocated in declaration order, each at the
		// next address its own alignment allows, and a member that is not a
		// bit-field takes storage of its own rather than a share of the unit
		// the fields before it were given.
		unit.open = false;
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
	types_.complete_class(entity.type, size == 0 ? 1 : size, align, empty,
	                      trivially_copied);
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
		entity->tail = entity;
		owner.destructor = entity;
		entity->special = kDestructorFunction;
		model_.bind(*ctx.scope, written, *entity);
	}
	else
	{
		// 13.1: the constructors of a class are its declarations of one name,
		// so a second one joins the chain the first heads and 13.3.1.3 walks it.
		// A constructor has no name a lookup binds, so the chain the class holds
		// is what the parameter list indexes - and a class with n of them is
		// declared in n steps rather than n^2 comparisons.
		SemaEntity* const prior =
			owner.constructor == nullptr
				? nullptr
				: model_.overload_of(*owner.constructor, types_.signature(type));
		if (prior != nullptr && prior->inherited == nullptr)
		{
			throw std::runtime_error(spelled +
			                         " declares one constructor twice");
		}
		if (prior != nullptr)
		{
			// 12.9p1: a constructor this class declares itself is what an
			// object of it is initialized by, and the base's is not inherited
			// beside it - so the declaration standing here is the one an
			// earlier using-declaration made, and this is what it declares.
			prior->inherited = nullptr;
			prior->defaulted = false;
			prior->inline_function = false;
			prior->deleted = false;
			prior->explicit_function = false;
			constructor_parameters_.erase(prior->id);
			defaults_.erase(prior->id);
			entity = prior;
		}
		else
		{
			entity = &model_.create(SemaKind::Function, written, type);
			entity->special = kConstructorFunction;
			entity->tail = entity;
			if (owner.constructor == nullptr)
			{
				owner.constructor = entity;
			}
			else
			{
				owner.constructor->tail->next = entity;
			}
			owner.constructor->tail = entity;
			model_.hold_overload(*owner.constructor, types_.signature(type),
			                     *entity);
		}
	}
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
	if (entity->region == nullptr)
	{
		// 12.9p1: the declaration this makes may be the one an inheriting
		// using-declaration already put in this region, which this one is the
		// declaration of - so the region records it once.
		model_.declare_in(*ctx.scope, *entity);
	}
	if (!destructor)
	{
		// 12.9p8: an inheriting using-declaration in a class derived from this
		// one declares a constructor from this one, and the definition it is
		// given writes the parameters this declaration named.
		std::vector<Parameter> named = parameters;
		constructor_parameters_[entity->id].swap(named);
	}

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

// 12.9p1: a using-declaration that names the constructors of a direct base
// class declares a constructor of this class from each of them, taking the same
// parameters.  12.9p3 leaves out the two an object of this class already has of
// its own - the base's default constructor and its copy and move constructors -
// and 12.9p1 leaves out one whose parameters a constructor this class declared
// itself already takes.  What each of them does is 12.9p8's initialization of
// the base subobject, so what the declaration has to carry is the base's
// constructor and the names its parameters were declared with.
void SemaAnalyzer::inherit_constructors(SemaEntity& base, Scope& where)
{
	SemaEntity& derived = *where.owner;
	const std::string spelled =
		QualifiedName(types_.user_name(derived.type)).last();
	for (SemaEntity* at = base.constructor; at != nullptr; at = at->next)
	{
		const std::vector<TypeId>& written = types_.parameters(at->type);
		if (written.size() == 1)
		{
			// 12.9p3: a constructor with no parameters is not inherited, and
			// 12.1p5 gives this class one of its own.
			continue;
		}
		if (written.size() == 2 &&
		    bare_type(types_.is_reference(written[1])
		                  ? types_.target(written[1])
		                  : written[1]) == types_.strip_cv(base.type))
		{
			// 12.9p3: neither is the base's copy or move constructor, which
			// would make an object of this class out of a base subobject.
			continue;
		}
		std::vector<TypeId> parameters;
		// 9.3.1p3: the object the constructor runs on is one of this class.
		parameters.push_back(types_.pointer_to(derived.type));
		for (std::size_t index = 1; index < written.size(); ++index)
		{
			parameters.push_back(written[index]);
		}
		const TypeId type = types_.function_of(types_.fundamental(FT_VOID),
		                                       parameters,
		                                       types_.variadic(at->type));
		// 12.9p1: a base constructor whose parameters a constructor of this
		// class already takes is not inherited.  13.1's index of the chain
		// answers that in one probe, so a base with n constructors costs n.
		if (derived.constructor != nullptr &&
		    model_.overload_of(*derived.constructor,
		                       types_.signature(type)) != nullptr)
		{
			continue;
		}
		SemaEntity& entity = model_.create(SemaKind::Function, spelled, type);
		entity.dump_name = where.prefix + spelled;
		entity.object_member = true;
		entity.special = kConstructorFunction;
		entity.inherited = at;
		entity.explicit_function = at->explicit_function;
		entity.deleted = at->deleted;
		// 12.9p6 and 7.1.2p3: the definition is one the standard gives it and
		// this unit generates where a use asks for one, as 12.1p5's is, so it
		// belongs to every translation unit that needs one.
		entity.defaulted = true;
		entity.inline_function = true;
		entity.tail = &entity;
		if (derived.constructor == nullptr)
		{
			derived.constructor = &entity;
		}
		else
		{
			derived.constructor->tail->next = &entity;
		}
		derived.constructor->tail = &entity;
		model_.hold_overload(*derived.constructor, types_.signature(type),
		                     entity);
		model_.declare_in(where, entity);
		// 12.9p8: the definition writes the parameters, so the names the base's
		// declaration gave them are part of what is inherited; 8.3.6 leaves the
		// default-arguments where the base's declaration put them, and a call
		// that omits an argument reads them from this declaration.
		const std::unordered_map<std::uint32_t,
		                         std::vector<Parameter> >::const_iterator names =
			constructor_parameters_.find(at->id);
		if (names != constructor_parameters_.end())
		{
			std::vector<Parameter> taken = names->second;
			constructor_parameters_[entity.id].swap(taken);
		}
		const std::unordered_map<std::uint32_t,
		                         std::vector<Default> >::const_iterator given =
			defaults_.find(at->id);
		if (given != defaults_.end())
		{
			std::vector<Default> taken = given->second;
			defaults_[entity.id].swap(taken);
		}
	}
}

// 12.1p5 and 12.9p3: whether this class declares a constructor of its own,
// which an inherited one is not - 12.9 declares it from the base's rather than
// from anything written here, and leaves the class the default constructor it
// would have had.
bool SemaAnalyzer::declares_own_constructor(const SemaEntity& entity)
{
	for (const SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		if (at->inherited == nullptr)
		{
			return true;
		}
	}
	return false;
}

// 9.2p2: the special members a class has where its definition ends, which is
// where the class is complete and where every subobject they act on is known.
// 12.1p5 gives a class with no constructor of its own a default one, 12.4p3
// gives one with no destructor a destructor, 12.9p4 settles what access the
// constructors a using-declaration inherited have, and 12.1p5/12.4p3 say which
// of them do nothing at all.
void SemaAnalyzer::declare_special_members(SemaEntity& entity, Scope& scope)
{
	if (!declares_own_constructor(entity))
	{
		declare_constructor(entity, scope);
	}
	if (entity.destructor == nullptr)
	{
		declare_destructor(entity, scope);
	}
	for (SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		if (at->inherited != nullptr)
		{
			// 12.9p4: an inherited constructor has the access the one it was
			// declared from has, whatever access-specifier the using-declaration
			// that brought it in stood under.
			at->access = at->inherited->access;
		}
		// 12.1p5 and 8.4.2p1: a special member written `= default` does what
		// the implicitly declared one would, and what that is, is known only
		// here, where the class is complete.
		if (at->defaulted && types_.parameters(at->type).size() == 1)
		{
			at->trivial = trivial_default_construction(scope);
		}
	}
	if (entity.destructor->defaulted)
	{
		entity.destructor->trivial = trivial_destruction(scope);
	}
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
	// 12.9p3: an inheriting using-declaration may already have put constructors
	// on this chain, and none of them is the one this declares, so it joins
	// them rather than replacing them.
	if (entity.constructor == nullptr)
	{
		entity.constructor = &constructor;
	}
	else
	{
		entity.constructor->tail->next = &constructor;
	}
	entity.constructor->tail = &constructor;
	model_.hold_overload(*entity.constructor,
	                     types_.signature(constructor.type), constructor);
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
		if (member.shadowed != nullptr)
		{
			// 7.3.3p1: the declaration is of a member of a base class, and a
			// class with a base class is no aggregate anyway.
			continue;
		}
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
		if (!declares_subobject(member, scope))
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
		if (!declares_subobject(member, scope))
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
		if (member.kind != SemaKind::Variable || member.shadowed != nullptr)
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

// 8.4.2p1 and 8.5p7: whether the program wrote a constructor of this class -
// one that is neither implicitly declared nor explicitly defaulted or deleted
// on its first declaration.  That is what 8.5p7 asks before it zero-initializes
// a value-initialized object, and it is one walk of the declarations of the
// name rather than a search.
bool SemaAnalyzer::user_provided_constructor(const SemaEntity& head)
{
	for (const SemaEntity* at = &head; at != nullptr; at = at->next)
	{
		if (!at->defaulted && !at->deleted)
		{
			return true;
		}
	}
	return false;
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
                                            Value& object, TypeId object_type)
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
	// 12.6p1: what the constructor runs on is one object of its own class,
	// which for an array of class type is an element rather than the array the
	// line names.  The line keeps the array it was written from - that is where
	// the element is - and the object 9.3.1p3 gives the constructor is the
	// element's.
	object.type = object.spelled = object_type;
	address_of_object(object, node, false);
}

// 12.6p1 and 8.5p7: whether what is declared is an array of class type whose
// elements are created by constructing each of them, which is what a
// declaration that wrote no clause for any element asks for - no initializer at
// all, and the empty `()` or `{}` that value-initializes every element.  A
// clause of its own initializes the element it reached, which 8.5.1 writes
// where the clauses are read.
bool SemaAnalyzer::element_constructed(TypeId type, const AstNode* written)
{
	if (types_.kind(types_.strip_cv(type)) != TypeKind::Array ||
	    !types_.is_class(element_of(type)))
	{
		return false;
	}
	return written == nullptr ||
		(is_initializer_list(written->kind) && written->children.empty());
}

// 8.5, 12.1 and 13.3.1.3: an object of class type is initialized by one of the
// constructors of its class, chosen from the arguments its initializer wrote.
// The action is one call like any other, written under the declaration of the
// object, and the definition of the constructor it names is asked for here.
void SemaAnalyzer::construct_object(SemaEntity& variable, DumpNode& line,
                                    const AstNode* written, const Context& ctx,
                                    Placement where, bool copied,
                                    const Value* given, bool value_init,
                                    const std::vector<SemaEntity*>* forwarded)
{
	const bool member = where != Placement::Named;
	// 12.6p1: an array of class type is initialized element by element, and
	// each element is one object of the element's class.  The one constructor
	// every element is given is chosen once, here, from the type of an element;
	// the action names the array, so how many objects it creates is what the
	// declared type says rather than a count written beside it.
	const TypeId object_type = element_of(variable.type);

	if (!types_.is_class(types_.strip_cv(object_type)))
	{
		// 8.5p6: default-initializing an object of any other type performs no
		// initialization, and there is nothing for the output to describe.
		return;
	}
	SemaEntity* const head = class_constructors(object_type);
	if (head == nullptr)
	{
		// 3.9p6 and 9.2p2: an object needs a complete class, and 12.1p5 gives
		// every complete one the output describes a constructor, so a class
		// with none here is one this translation unit never defined.
		throw std::runtime_error("an object of the incomplete class type " +
		                         types_.description(object_type) +
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
			// 8.5p7: `()` and `{}` value-initialize the object rather than
			// naming an argument for a constructor.
			value_init = list->children.empty();
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
		    types_.strip_cv(named->type) == types_.strip_cv(object_type))
		{
			list = call_arguments(*written);
			converting = false;
			// 5.2.3p2: `T()` value-initializes what it makes, and the grammar
			// writes no argument-list node for one that has no arguments.
			value_init = list == nullptr || list->children.empty();
		}
	}

	Value source;
	if (given != nullptr)
	{
		// 13.3.3.1.2p1: the one argument was analysed where the program wrote
		// it, so it is taken as it stands.  Its line is not held by this one
		// yet, so nothing is taken back out of what this line already holds.
		source = *given;
		converting = true;
	}
	else if (converting)
	{
		// 8.5p14: the initializer is read before anything is written for the
		// initialization, because 12.8p31 lets a value of the object's own type
		// be what initializes it, with no constructor standing between them.
		source = expression(*written, ctx, line);
		if (types_.strip_cv(source.type) == types_.strip_cv(object_type) &&
		    !member)
		{
			return;
		}
		line.children.pop_back();
	}
	DumpNode& action = model_.open_node(line, std::string());
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = variable.type;
	action.fact.base_subobject = where == Placement::Base;
	DumpNode& call = model_.open_node(action, std::string());
	DumpNode& callee = model_.open_node(call, std::string());
	Value object;
	write_constructed_object(variable, call, where, object, object_type);
	std::vector<Value> arguments;
	if (forwarded != nullptr)
	{
		// 12.9p8: the arguments are this constructor's own parameters, each
		// named as the program naming it would be, in declaration order.
		for (std::size_t index = 0; index < forwarded->size(); ++index)
		{
			arguments.push_back(parameter_value(*(*forwarded)[index], call));
		}
	}
	else if (list != nullptr)
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
	else
	{
		constructor.base_object_entry = true;
	}
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		const Match match = match_argument(arguments[index],
		                                   parameters[index + 1]);
		apply_conversion(arguments[index], parameters[index + 1], match, ctx,
		                 Requested::Argument);
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
	// 8.5p7: a non-union class with no user-provided constructor is
	// zero-initialized before its default constructor - which is the one this
	// initialization chose - runs on it.  The zero is what the object holds
	// wherever that constructor leaves a member alone, so it is written even
	// where the constructor itself does nothing at all.
	action.fact.zero_initialized = value_init && !user_provided_constructor(*head);
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
	if (constructor.inherited != nullptr)
	{
		// 12.9p8: the definition writes the parameters it inherited and names
		// them in the call it makes on the base subobject, so unlike 12.1p5's
		// it has a region of its own for them to be declared in.
		DumpScope& dump = model_.open_dump(*constructor.region->dump,
		                                   "scope function " + constructor.name);
		pending.scope = &model_.open(ScopeKind::Function, *constructor.region,
		                             &constructor, &dump);
		const std::unordered_map<std::uint32_t,
		                         std::vector<Parameter> >::const_iterator named =
			constructor_parameters_.find(constructor.id);
		if (named != constructor_parameters_.end())
		{
			pending.parameters = named->second;
		}
		else
		{
			// A base whose constructor this unit only saw declared through
			// another inheriting one still says what its parameters are; a name
			// is what it may not have said, and the output gives an unnamed
			// parameter one of its own.
			for (std::size_t index = 1; index < parameters.size(); ++index)
			{
				Parameter written;
				written.type = parameters[index];
				pending.parameters.push_back(written);
			}
		}
	}
	pending_.push_back(pending);
}

// 12.2p1: a prvalue of class type denotes an object, and no declaration named
// it, so the function it was written in has to give it storage.  The object is
// declared here, the constructor 8.5/13.3.1.3 chooses runs on it exactly as it
// would on one a declaration named, and what the expression is worth from then
// on is that object.
//
// 12.2p3's destruction at the end of the full-expression is not written here:
// the lowering marks no full-expression boundary yet, so a temporary of a class
// whose destructor does something is refused rather than left alive past the
// point 12.2p3 ends it.
SemaAnalyzer::Value SemaAnalyzer::materialize_temporary(TypeId type,
                                                        const AstNode* written,
                                                        const Context& ctx,
                                                        DumpNode& parent,
                                                        const char* prefix,
                                                        bool value_init)
{
	DumpNode& line = model_.open_node(parent, std::string());
	return build_temporary(type, line, written, nullptr, ctx, prefix,
	                       value_init);
}

SemaAnalyzer::Value SemaAnalyzer::build_temporary(TypeId type, DumpNode& line,
                                                  const AstNode* written,
                                                  const Value* given,
                                                  const Context& ctx,
                                                  const char* prefix,
                                                  bool value_init)
{
	const TypeId object_type = types_.strip_cv(type);
	SemaEntity& object = model_.create(SemaKind::Variable, std::string(),
	                                   object_type);
	object.object_member = false;
	set_fact(line, FactKind::TemporaryObject, object_type,
	         ValueCategory::PRValue);
	line.fact.entity = &object;
	line.fact.spelling = prefix;
	construct_object(object, line, written, ctx, Placement::Named, false, given,
	                 value_init);
	const SemaEntity* const destructor = class_destructor(object_type);
	if (destructor != nullptr && !destructor->trivial)
	{
		// 12.2p3 ends the temporary's lifetime at the end of the
		// full-expression, which is a place this milestone does not mark.
		throw std::runtime_error(
			"a temporary of the class type " + types_.description(object_type) +
			" is created, whose destructor 12.2p3 runs at a point this "
			"milestone does not mark");
	}
	line.text = spell("temporary-object", ValueCategory::PRValue, object_type,
	                  std::string());
	Value value;
	value.type = object_type;
	value.spelled = object_type;
	value.category = ValueCategory::PRValue;
	value.what = "temporary-object";
	value.entity = &object;
	value.node = &line;
	return value;
}

// 8.5.3p5 and 13.3.3.1.2: the storage a temporary takes is named after the
// argument that asked for it.  A temporary something already read as the object
// it is - a base subobject of it, a member of it - keeps the name it was given
// where it was written, because the argument is no longer what made it.
void SemaAnalyzer::name_argument_temporary(const Value& value,
                                           const char* prefix)
{
	if (value.node != nullptr &&
	    value.node->fact.kind == FactKind::TemporaryObject)
	{
		value.node->fact.spelling = prefix;
	}
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
	else
	{
		destructor->base_object_entry = true;
	}
	DumpNode& action = model_.open_node(
		parent, "destructor-action " + destructor->dump_name);
	action.fact.kind = FactKind::DestructorAction;
	action.fact.entity = destructor;
	action.fact.type = entity.type;
	action.fact.base_subobject = where == Placement::Base;
	// 12.4p8: an array of class type is as many objects as it has elements and
	// the destructor runs on each of them.  The action names the array, which
	// says how many they are; a member subobject is destroyed in the reverse of
	// the order 12.6.2p10 created it in, and the elements of one an enclosing
	// block declared are left in the order the references end them in.
	action.fact.reverse_elements =
		where != Placement::Named &&
		types_.kind(types_.strip_cv(entity.type)) == TypeKind::Array;
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
	if (base != nullptr && pending.function->inherited != nullptr)
	{
		// 12.9p8: an inheriting constructor initializes the base subobject by
		// calling the constructor it was declared from, with its own parameters
		// as the arguments, and writes no ctor-initializer of its own.  The
		// parameters are the declarations the definition just made, in the
		// order the declaration wrote them.
		std::vector<SemaEntity*> forwarded;
		for (std::size_t index = 0;
		     index < pending.scope->declarations.size(); ++index)
		{
			SemaEntity& parameter = *pending.scope->declarations[index];
			if (parameter.kind == SemaKind::Parameter)
			{
				forwarded.push_back(&parameter);
			}
		}
		construct_object(*base, line, nullptr, inner, Placement::Base, false,
		                 nullptr, false, &forwarded);
	}
	else if (base != nullptr)
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
		if (!declares_subobject(member, members))
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
		if ((types_.is_class(types_.strip_cv(type)) &&
		     !(braced && aggregate_type(type))) ||
		    element_constructed(type, written))
		{
			// 12.6.2p8 and 12.1p5: a member no initializer reaches is
			// default-initialized, and where that does nothing at all there is
			// no action to write and no subobject to name.  An array member is
			// 12.6p1's elements, each constructed by the one constructor the
			// action names.
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
		if (!declares_subobject(member, members))
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