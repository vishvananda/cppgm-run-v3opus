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

// 16.6 and the course ABI: the alignment a subobject is allocated at once
// `#pragma pack` has capped it.  A cap of zero is no cap, and one wider than
// the subobject's own alignment asks for nothing, because the directive only
// ever weakens what 3.11 gives a type.
unsigned long long packed_align(unsigned long long natural,
                                unsigned long long packed)
{
	return packed != 0 && packed < natural ? packed : natural;
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
                                     unsigned long long& at, BitUnit& unit,
                                     unsigned long long packed)
{
	const TypeId bare = types_.strip_cv(member.type);
	const unsigned long long size = types_.object_size(bare);
	const unsigned long long boundary = packed_align(types_.object_align(bare),
	                                                 packed);
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

// `packed` is 16.6's cap in force where the definition of the class ends, which
// is where 9.2p2 completes it and where this settles its layout - so a
// directive written between two of its members reaches every one of them.
void SemaAnalyzer::lay_out_class(SemaEntity& entity, Scope& scope, bool is_union,
                                 unsigned long long requested,
                                 unsigned long long packed)
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
	// The ABI: where the empty class subobjects of this object stand, filled in
	// as they are placed.  It is what the members after them are checked
	// against, and what the classes that go on to hold one of these objects
	// read.
	std::vector<std::pair<TypeId, unsigned long long> >& holes =
		entity.empty_subobjects;
	holes.clear();
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
		align = packed_align(types_.object_align(entity.base->type), packed);
		if (!types_.is_trivially_copied(types_.strip_cv(entity.base->type)))
		{
			trivially_copied = false;
		}
		if (!entity.base->empty_class)
		{
			size = types_.object_size(entity.base->type);
			empty = false;
		}
		// The base subobject begins where this object does, so every empty
		// class subobject of it stands at the byte it stands at inside the
		// base - and no member of this class may be given one of those bytes
		// for a subobject of the same class.
		place_empty_subobjects(entity.base->type, 0, holes);
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
		// allocated at the next address that one allows.  16.6's cap is what
		// the type's own alignment comes to here, so an alignment-specifier
		// still only ever raises it.
		const unsigned long long declared_align =
			packed_align(types_.object_align(member.type), packed);
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
			lay_out_bit_field(member, size, unit, packed);
			continue;
		}
		// 9.2p13: the members are allocated in declaration order, each at the
		// next address its own alignment allows, and a member that is not a
		// bit-field takes storage of its own rather than a share of the unit
		// the fields before it were given.
		unit.open = false;
		member.offset = round_up(size, member_align);
		// The ABI: two subobjects of the same class may not begin at the same
		// byte, and an empty one takes no storage to push the next along - so
		// where this member would put one where an earlier subobject already
		// has one of its class, it is moved to the next address its alignment
		// allows and asked again.
		while (collides_with_empty(member.type, member.offset, holes))
		{
			member.offset = round_up(member.offset + 1, member_align);
		}
		place_empty_subobjects(member.type, member.offset, holes);
		size = member.offset + member_size;
		if (size < member.offset)
		{
			throw std::runtime_error("a class is larger than its object "
			                         "representation can address");
		}
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
	if (empty)
	{
		// 9p6 and the ABI: an object of this class holds nothing, so it is
		// itself an empty subobject wherever one of it stands - and the class
		// that goes on to hold one reads that from here.
		holes.push_back(std::make_pair(types_.strip_cv(entity.type), 0ull));
	}
	// 1.8p5: a complete object has a size of at least one byte.
	size = round_up(size, align);
	types_.complete_class(entity.type, size == 0 ? 1 : size, align, empty,
	                      trivially_copied);
}
// The ABI: where the empty class subobjects of an object of `type` standing at
// `at` are, appended to `holes`.  A type that is no class has none, and a class
// with none appends nothing - so the list is the size of the empty subobjects
// the source wrote and not of the members it wrote.
void SemaAnalyzer::place_empty_subobjects(
	TypeId type, unsigned long long at,
	std::vector<std::pair<TypeId, unsigned long long> >& holes)
{
	TypeId bare = types_.strip_cv(type);
	// 8.3.4p1: an array is its elements, and only the first of them can be
	// given a byte something already holds - the ones after it begin past
	// storage the layout has already counted.
	while (types_.kind(bare) == TypeKind::Array)
	{
		bare = types_.strip_cv(types_.target(bare));
	}
	if (!types_.is_class(bare))
	{
		return;
	}
	const SemaEntity* const owner = model_.type_owner(bare);
	if (owner == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < owner->empty_subobjects.size(); ++index)
	{
		holes.push_back(
			std::make_pair(owner->empty_subobjects[index].first,
			               at + owner->empty_subobjects[index].second));
	}
}

// The ABI: whether putting an object of `type` at `at` would put a subobject of
// some class where a subobject of that same class already stands.  A class with
// no empty subobject at all answers before asking anything, which is what keeps
// this one comparison per member for nearly every class.
bool SemaAnalyzer::collides_with_empty(
	TypeId type, unsigned long long at,
	const std::vector<std::pair<TypeId, unsigned long long> >& holes)
{
	if (holes.empty())
	{
		return false;
	}
	std::vector<std::pair<TypeId, unsigned long long> > wanted;
	place_empty_subobjects(type, at, wanted);
	for (std::size_t index = 0; index < wanted.size(); ++index)
	{
		for (std::size_t other = 0; other < holes.size(); ++other)
		{
			if (holes[other] == wanted[index])
			{
				return true;
			}
		}
	}
	return false;
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

// 12.1p1 and 12.4p1: the type a constructor or destructor declarator writes.
// Neither has a return type, and both take the object 9.3.1p3 makes the first
// parameter of a member function's type - so the declaration is read against
// the class it belongs to wherever it is written, which 3.4.1p8 is what puts
// that class in force for a definition written outside it.
TypeId SemaAnalyzer::special_member_type(const AstNode& node,
                                         const Context& ctx,
                                         const SemaEntity& owner,
                                         bool destructor,
                                         std::vector<Parameter>& parameters,
                                         bool& variadic)
{
	const AstNode* const declarator = child_of(node, AstKind::Declarator);
	const AstNode* const clause =
		declarator == nullptr ? nullptr
		                      : child_of(*declarator, AstKind::ParameterClause);
	if (clause != nullptr)
	{
		read_parameters(*clause, ctx, parameters, variadic);
	}
	if (declarator != nullptr)
	{
		// 12.1p4 and 12.4p2: neither a constructor nor a destructor writes a
		// cv-qualifier-seq or a ref-qualifier, because what 9.3.1p3's object
		// parameter names is an object whose lifetime is beginning or ending
		// rather than one an expression could have named a category for.
		if (declarator_ref_qualifier(*declarator) != RefQualifier::None &&
		    semantics())
		{
			throw std::runtime_error(
				owner.name + " declares a " +
				(destructor ? "destructor" : "constructor") +
				" with a ref-qualifier, which 8.3.5p6 does not allow");
		}
	}
	std::vector<TypeId> types;
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
	return types_.function_of(types_.fundamental(FT_VOID), types, variadic);
}

// 12.1p1 and 12.4p1: the unqualified name a special member declarator wrote,
// checked against the class it belongs to.  A name that is not the class's own
// names something else, and what this milestone describes is the class the
// program wrote.
std::string SemaAnalyzer::special_member_name(const std::string& written,
                                              const SemaEntity& owner)
{
	const std::string spelled =
		QualifiedName(types_.user_name(owner.type)).last();
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string named = destructor ? written.substr(1) : written;
	if (named != spelled)
	{
		// 12.3.2: a conversion function, and 13.5 an operator function written
		// with no return type.  Neither is part of this milestone's slice, and
		// what the output would describe without it is not the class the
		// program wrote.
		throw std::runtime_error(spelled + " declares " + written +
		                         ", which is a special member function this "
		                         "milestone does not describe");
	}
	return spelled;
}

// 12.3.2p1: the name a region binds a conversion function under.
//
// A conversion function is named by the type it converts to, so two
// declarations that spell that type differently declare one function -
// `operator adaptor_type` and `operator runtime_traits` where the first names a
// typedef of the second - and a use written either way names it.  The one
// spelling every one of them agrees on is the type's own, so that is what the
// region binds and what every later reader asks for.
std::string SemaAnalyzer::member_id_name(const AstNode& id, const Context& ctx)
{
	const AstNode* const carried = child_of(id, AstKind::CarriedTypeId);
	if (carried == nullptr || carried->children.empty())
	{
		return id.text;
	}
	// 3.4.5p1: the conversion-type-id is looked up in the class of the object
	// expression and in the region the whole expression stands in; the second
	// is what a typedef-name of an enclosing class or namespace is found by,
	// and it is the region every other name in this expression is read in.
	return conversion_name(type_id_type(*carried->children[0], ctx));
}

std::string SemaAnalyzer::conversion_name(TypeId type) const
{
	const std::string described = types_.description(type);
	std::string spelled = "operator";
	for (std::size_t index = 0; index < described.size(); ++index)
	{
		if (described[index] == ' ')
		{
			continue;
		}
		if (described[index] == ':' && index + 1 < described.size() &&
		    described[index + 1] == ':')
		{
			// A `::` inside the name would read as a nested-name-specifier
			// everywhere a qualified name is split - the region a member access
			// looks in, the regions the object file encodes - and this one is
			// part of one component and not a boundary between two.  No type is
			// spelled with a `.`, so writing one there keeps the name one
			// component and still tells two types apart.
			spelled += '.';
			++index;
			continue;
		}
		spelled += described[index];
	}
	return spelled;
}

// 12.3.2p1: the declaration a conversion function's declarator makes in the
// class `target` names.  9.3.1p3's object parameter carries the
// cv-qualifier-seq written after the parameter clause, so `operator int()
// const` and `operator int() const volatile` are two declarations of one name
// that 13.1 tells apart exactly as it tells any two member functions apart.
SemaEntity& SemaAnalyzer::declare_conversion(const AstNode& node,
                                             const Context& target,
                                             const AstNode& carried)
{
	const AstNode* const declarator = child_of(node, AstKind::Declarator);
	if (declarator == nullptr || carried.children.empty())
	{
		throw std::runtime_error("a conversion function is declared with no "
		                         "declarator");
	}
	const AstNode* const clause =
		child_of(*declarator, AstKind::ParameterClause);
	std::vector<Parameter> parameters;
	bool variadic = false;
	if (clause != nullptr)
	{
		read_parameters(*clause, target, parameters, variadic);
	}
	if (!parameters.empty() || variadic)
	{
		// 12.3.2p1: a conversion function takes no arguments, because the one
		// object it acts on is the one it is called on.
		throw std::runtime_error("a conversion function is declared with "
		                         "parameters, which 12.3.2p1 does not allow");
	}
	const TypeId converted = type_id_type(*carried.children[0], target);
	if (types_.kind(converted) == TypeKind::Function ||
	    types_.kind(converted) == TypeKind::Array)
	{
		// 12.3.2p1: the conversion-type-id shall not define a function or an
		// array type, which the grammar's ptr-operators alone cannot spell but
		// a typedef-name can.
		throw std::runtime_error("a conversion function converts to a function "
		                         "or array type, which 12.3.2p1 does not allow");
	}
	const std::string name = conversion_name(converted);
	// 12.3.2p1: a conversion function's declarator writes no type of its own,
	// so its function type is built here rather than by the walk that reads an
	// ordinary declarator - and 8.3.5p1's ref-qualifier is as much a part of it
	// as it is of any other member function's.
	const TypeId written = types_.ref_qualified_function(
		types_.function_of(converted, std::vector<TypeId>(), false),
		declarator_ref_qualifier(*declarator));
	const TypeId type = with_object_parameter(written, *declarator, target,
	                                          false, name, false);
	if (type == written)
	{
		// 9.3p1: a conversion function is a non-static member function, so a
		// declarator that reached no class declares nothing this milestone has
		// a meaning for.
		throw std::runtime_error(name + " is declared where no class declares "
		                         "a member");
	}
	SemaEntity& entity = declare_function(name, type, target, false, false,
	                                      true);
	entity.object_member = true;
	entity.conversion_function = true;
	return entity;
}

// 12.3.2: a conversion function declared in a class body.  It is a member
// function like any other - 9.3.1p3's object parameter, 13.1's overload chain,
// 11p1's access, 9.2p2's deferred body - and the one thing about it that is not
// ordinary is that its name is a type.
void SemaAnalyzer::conversion_function(const AstNode& node, const Context& ctx,
                                       const AstNode& carried)
{
	SemaEntity& entity = declare_conversion(node, ctx, carried);
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		// 12.3.2p2: `explicit` says which initializations may choose this
		// conversion, exactly as 12.3.1p2 says of a constructor.
		if (specifiers->children[index]->text == "explicit")
		{
			entity.explicit_function = true;
		}
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity.inline_function = true;
		}
	}
	// 7.1.2p3 and 9.3p2: a member function defined in its class body is inline.
	entity.inline_function = entity.inline_function ||
		node.kind == AstKind::SpecialMemberDefinition;
	const AstNode* const initializer = child_of(node, AstKind::Initializer);
	if (initializer != nullptr && !initializer->children.empty())
	{
		// 8.4.3: `= delete` declares a function every use of is ill formed.
		entity.deleted = initializer->children[0]->text == "delete";
		entity.defaulted = !entity.deleted;
		entity.defined = false;
		entity.inline_function = true;
		return;
	}
	entity.user_provided = true;
	if (node.kind != AstKind::SpecialMemberDefinition)
	{
		return;
	}
	const std::vector<Parameter> none;
	open_special_member_body(node, entity, ctx, entity.name, none);
}

// 12.3.2 and 9.3p2: a conversion function defined outside its class.  3.4.3p3
// makes the declarator-id name the class the definition belongs to, so the
// class is the region the conversion-type-id and the body alike are read
// against, and what it defines is the declaration that class already made.
bool SemaAnalyzer::conversion_function_definition(const AstNode& node,
                                                  const Context& ctx)
{
	const AstNode* const carried = child_of(node, AstKind::CarriedTypeId);
	if (carried == nullptr)
	{
		return false;
	}
	if (carried->text.empty())
	{
		throw std::runtime_error(node.text + " is defined where no class "
		                         "declares it");
	}
	Scope& region = *resolve_prefix(QualifiedName(carried->text + "operator"),
	                                ctx);
	if (region.kind != ScopeKind::Class || region.owner == nullptr)
	{
		throw std::runtime_error(node.text + " defines a conversion function of "
		                         "what is not a class");
	}
	Context target = ctx;
	target.scope = &region;
	target.dump = region.dump;
	// 13.1: the declaration this definition defines is the one of the class's
	// own whose name and object parameter agree, which the chain the name heads
	// answers in one probe.  A definition that matches none of them would
	// declare a second member of the class, which 9.2p1 does not allow written
	// outside its body - so what says the definition defines nothing is that
	// the class gained a declaration here.
	const std::size_t declared = region.declarations.size();
	SemaEntity& entity = declare_conversion(node, target, *carried);
	if (region.declarations.size() != declared)
	{
		throw std::runtime_error(node.text + " defines a conversion function "
		                         "its class does not declare");
	}
	if (entity.defined)
	{
		throw std::runtime_error(node.text + " is defined twice");
	}
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity.inline_function = true;
		}
	}
	entity.user_provided = true;
	const std::vector<Parameter> none;
	open_special_member_body(node, entity, target, entity.name, none);
	return true;
}

// 12.1 and 12.4: a constructor or a destructor declared in a class body.  Both
// are functions of the class whose name no lookup reaches: an object of the
// class asks the class for them, so they are chained on the class rather than
// bound to a name in it.
void SemaAnalyzer::special_member(const AstNode& node, const Context& ctx)
{
	const AstNode* const carried = child_of(node, AstKind::CarriedTypeId);
	if (carried != nullptr)
	{
		// 12.3.2p1: a conversion function is an ordinary member function whose
		// name is a type, so what it declares is read from the type and not
		// from the name the grammar flattened.
		conversion_function(node, ctx, *carried);
		return;
	}
	SemaEntity& owner = *ctx.scope->owner;
	const std::string written = QualifiedName(node.text).last();
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string spelled = special_member_name(written, owner);
	std::vector<Parameter> parameters;
	bool variadic = false;
	const TypeId type = special_member_type(node, ctx, owner, destructor,
	                                        parameters, variadic);
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
		if (owner.constructor != nullptr &&
		    model_.overload_of(*owner.constructor,
		                       types_.signature(type)) != nullptr)
		{
			throw std::runtime_error(spelled +
			                         " declares one constructor twice");
		}
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
	name_in_region(*entity, *ctx.scope, written);
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
		// 7.1.2p2: a declaration written with `inline` declares an inline
		// function, whether or not it is the declaration the body is written
		// on - so a constructor or destructor the class declares `inline` and
		// a later definition gives a body belongs to every unit that needs
		// one, exactly as one defined in the class body does.
		if (specifiers->children[index]->text == "inline")
		{
			entity->inline_function = true;
		}
	}
	record_default_arguments(*entity, parameters, ctx.scope);
	model_.declare_in(*ctx.scope, *entity);
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
	// 8.4.2p4 and 12.1p4: a special member the program declared and did not
	// default or delete on that declaration is user-provided, wherever the body
	// it is given is written - which is what stops the class from being an
	// aggregate, and what 8.5p7 asks before it zeroes a value-initialized
	// object.  The declaration says it, so a constructor defined outside the
	// class says it too.
	entity->user_provided = true;
	if (node.kind != AstKind::SpecialMemberDefinition)
	{
		return;
	}
	open_special_member_body(node, *entity, ctx, written, parameters);
}

// 12.6.2 and 9.2p2: the body a special member's definition gives it, read where
// the class is complete rather than where the definition stands.  One
// description serves the definition written in the class body and the one
// written outside it, because the only thing that differs is where the
// declarator-id was written.
void SemaAnalyzer::open_special_member_body(
	const AstNode& node, SemaEntity& entity, const Context& ctx,
	const std::string& written, const std::vector<Parameter>& parameters)
{
	entity.defined = true;
	// 12.4p8: whether the definition writes any statement is what says whether
	// running this function comes to anything.  It is read here for the
	// definition written in the class body; one written outside it stands
	// wherever the program put it, and `note_definition_body` asks the same
	// question of the same node out of the syntax, so a body reading earlier
	// gets the same answer.
	entity.empty_body = writes_no_statement(node);

	DumpScope& dump = model_.open_dump(*ctx.dump, "scope function " + written);
	Scope& inner = model_.open(ScopeKind::Function, *ctx.scope, &entity, &dump);
	SemaEntity& self =
		model_.create(SemaKind::Parameter, "this", this_type(entity));
	model_.bind(inner, self.name, self);
	model_.declare_in(inner, self);
	// 9.2p2: the body and the mem-initializers are read where the class is
	// complete, which is the end of the translation unit.
	Pending pending;
	pending.function = &entity;
	pending.self = &self;
	pending.body = &node;
	pending.scope = &inner;
	pending.parameters = parameters;
	pending.initializers = child_of(node, AstKind::CtorInitializer);
	pending.members = ctx.scope;
	pending_.push_back(pending);
}

// 9.3p2 and 12.1p1: a constructor or a destructor defined outside its class.
// 3.4.3p3 makes the declarator-id name the class the definition belongs to, so
// the class is the region the whole definition is read against - its
// parameters, its mem-initializers and its body alike - and what it defines is
// the declaration that class already made rather than a second one.  A class
// declares its special members once, where 9.2p2 completes it, so a definition
// that matches none of them defines nothing and is refused rather than dropped.
void SemaAnalyzer::special_member_definition(const AstNode& node,
                                             const Context& ctx)
{
	if (conversion_function_definition(node, ctx))
	{
		return;
	}
	const QualifiedName spelled(node.text);
	const std::string written = spelled.last();
	if (!spelled.qualified())
	{
		throw std::runtime_error(written + " is defined where no class declares "
		                         "it, which 12.1p1 gives no meaning");
	}
	Scope& region = *resolve_prefix(spelled, ctx);
	if (region.kind != ScopeKind::Class || region.owner == nullptr)
	{
		throw std::runtime_error(node.text + " defines a special member of what "
		                         "is not a class");
	}
	SemaEntity& owner = *region.owner;
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string spelled_class = special_member_name(written, owner);
	Context target = ctx;
	target.scope = &region;
	target.dump = region.dump;
	std::vector<Parameter> parameters;
	bool variadic = false;
	const TypeId type = special_member_type(node, target, owner, destructor,
	                                        parameters, variadic);
	// 13.1: the declaration this definition defines is the one of the class's
	// own whose parameter-type-list agrees, which is one probe of the chain the
	// class holds.  12.1p5 and 12.4p3's implicitly declared members are not
	// declarations the program wrote, so a definition never names one.
	SemaEntity* const entity =
		destructor ? owner.destructor
		           : (owner.constructor == nullptr
		              ? nullptr
		              : model_.overload_of(*owner.constructor,
		                                   types_.signature(type)));
	if (entity == nullptr || entity->defaulted || entity->deleted ||
	    entity->inherited != nullptr)
	{
		throw std::runtime_error(spelled_class + " declares no " +
		                         (destructor ? "destructor" : "constructor") +
		                         " this definition defines");
	}
	if (entity->defined)
	{
		throw std::runtime_error(node.text + " is defined twice");
	}
	// 7.1.2p3: a definition written outside the class is inline only where the
	// program says so, which is what makes it the one definition of the
	// function rather than one every unit that needs it may hold.
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity->inline_function = true;
		}
	}
	open_special_member_body(node, *entity, target, written, parameters);
}

// 12.9p1: how many parameters the shortest constructor in the candidate set a
// declaration contributes takes, which is the ones no default-argument has
// reached.  8.3.6p4 says a default-argument stands on every parameter after the
// first one that has one, so the count is where they begin.
std::size_t SemaAnalyzer::required_parameters(const SemaEntity& function) const
{
	const std::size_t declared = types_.parameters(function.type).size();
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(function.id);
	if (found == defaults_.end())
	{
		return declared;
	}
	for (std::size_t index = 0; index < declared; ++index)
	{
		if (index < found->second.size() &&
		    found->second[index].written != nullptr)
		{
			return index;
		}
	}
	return declared;
}

// 12.9p1: a using-declaration that names the constructors of a direct base
// class declares a constructor of this class from each constructor in the
// base's candidate set.  That set is the base's own constructors and, for each
// one a default-argument reached, the shorter parameter lists that result from
// omitting the ellipsis and then those parameters from the end - so what is
// inherited is a parameter-type-list rather than a declaration, and 12.9p2's
// constructor characteristics hold no default-argument of their own.  12.9p3
// leaves out the two an object of this class already has of its own - the
// base's default constructor and its copy and move constructors - and 12.9p1
// leaves out one whose parameters a constructor this class declared itself
// already takes.  What each of them does is 12.9p8's initialization of the base
// subobject, so what the declaration has to carry is the base's constructor and
// the names its parameters were declared with.
void SemaAnalyzer::inherit_constructors(SemaEntity& base, Scope& where)
{
	SemaEntity& derived = *where.owner;
	const std::string spelled =
		QualifiedName(types_.user_name(derived.type)).last();
	for (SemaEntity* at = base.constructor; at != nullptr; at = at->next)
	{
		const std::vector<TypeId>& written = types_.parameters(at->type);
		const std::size_t least = required_parameters(*at);
		for (std::size_t count = written.size(); count + 1 > least && count > 1;
		     --count)
		{
			inherit_constructor(*at, base, count, count == written.size(),
			                    derived, where, spelled);
		}
	}
}

// One constructor of that candidate set: the first `taken` parameters of the
// base's declaration, with the ellipsis kept only where none was omitted.
void SemaAnalyzer::inherit_constructor(SemaEntity& from, const SemaEntity& base,
                                       std::size_t taken, bool whole,
                                       SemaEntity& derived, Scope& where,
                                       const std::string& spelled)
{
	const std::vector<TypeId>& written = types_.parameters(from.type);
	if (taken <= 1)
	{
		// 12.9p3: a constructor with no parameters is not inherited, and
		// 12.1p5 gives this class one of its own.
		return;
	}
	if (taken == 2 &&
	    bare_type(types_.is_reference(written[1]) ? types_.target(written[1])
	                                              : written[1]) ==
		types_.strip_cv(base.type))
	{
		// 12.9p3: neither is the base's copy or move constructor, which would
		// make an object of this class out of a base subobject.
		return;
	}
	std::vector<TypeId> parameters;
	// 9.3.1p3: the object the constructor runs on is one of this class.
	parameters.push_back(types_.pointer_to(derived.type));
	for (std::size_t index = 1; index < taken; ++index)
	{
		parameters.push_back(written[index]);
	}
	const TypeId type =
		types_.function_of(types_.fundamental(FT_VOID), parameters,
		                   whole && types_.variadic(from.type));
	// 12.9p1: a base constructor whose parameters a constructor of this class
	// already takes is not inherited, and neither is one two members of the
	// candidate set agree on.  13.1's index of the chain answers both in one
	// probe, so a base with n constructors costs n.
	if (derived.constructor != nullptr &&
	    model_.overload_of(*derived.constructor,
	                       types_.signature(type)) != nullptr)
	{
		return;
	}
	SemaEntity& entity = model_.create(SemaKind::Function, spelled, type);
	name_in_region(entity, where, spelled);
	entity.object_member = true;
	entity.special = kConstructorFunction;
	entity.inherited = &from;
	entity.explicit_function = from.explicit_function;
	entity.deleted = from.deleted;
	// 12.9p6 and 7.1.2p3: the definition is one the standard gives it and this
	// unit generates where a use asks for one, as 12.1p5's is, so it belongs to
	// every translation unit that needs one.
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
	model_.hold_overload(*derived.constructor, types_.signature(type), entity);
	model_.declare_in(where, entity);
	// 12.9p8: the definition writes the parameters, so the names the base's
	// declaration gave them are part of what is inherited.  A parameter the
	// candidate set omitted is one this declaration does not have; the base's
	// own default-argument is what fills it where 12.9p8's call is written.
	const std::unordered_map<std::uint32_t,
	                         std::vector<Parameter> >::const_iterator names =
		constructor_parameters_.find(from.id);
	if (names != constructor_parameters_.end())
	{
		std::vector<Parameter> kept(
			names->second.begin(),
			names->second.begin() +
				static_cast<std::ptrdiff_t>(
					taken - 1 < names->second.size() ? taken - 1
					                                 : names->second.size()));
		constructor_parameters_[entity.id].swap(kept);
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
	if (scope.inheriting_constructors && entity.base != nullptr)
	{
		// 12.9p1: a using-declaration named the base's constructors, and which
		// of them are inherited is settled here, where the class the standard
		// calls complete holds every constructor it declares itself.
		inherit_constructors(*entity.base, scope);
	}
	// 12.8p2/p3/p17/p19: which of the four value-transfer members the program
	// itself declared, read before anything is added to the class - because
	// p9 and p20 make the answer decide whether the class has the other two at
	// all, and p7 and p18 whether the ones it does have are deleted.
	note_transfers(entity, scope);
	collect_conversions(entity, scope);
	const bool wrote_destructor = entity.destructor != nullptr;
	if (!declares_own_constructor(entity))
	{
		declare_constructor(entity, scope);
	}
	if (!wrote_destructor)
	{
		declare_destructor(entity, scope);
	}
	declare_transfer_members(entity, scope, wrote_destructor);
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
			// 12.1p5: a default constructor the standard writes has nothing to
			// initialize a member of const-qualified or reference type with, so
			// the class has none - which is what 8.5p6's default-initialization
			// of an object of the class then names.
			at->deleted = at->deleted || undefinable_default(scope);
		}
	}
	if (entity.destructor->defaulted)
	{
		entity.destructor->trivial = trivial_destruction(scope);
	}
	settle_transfers(entity, scope);
}

// 12.8p2, p3, p17 and p19: which of the four value-transfer special members a
// declaration of `owner` declares, read from the parameter list 9.3.1p3 gave
// the function's type rather than from the declarator that wrote it.  A
// constructor is one where its first written parameter is a reference to its
// own class and every parameter after it has a default argument; `operator=` is
// one where it takes exactly that one parameter.  Which of the copy and the
// move it is, is which reference the parameter is - and a parameter of the
// class itself is 12.8p17's copy assignment taking its argument by value.
unsigned char SemaAnalyzer::transfer_kind(const SemaEntity& function,
                                          const SemaEntity& owner)
{
	if (function.shadowed != nullptr || function.inherited != nullptr ||
	    !function.object_member)
	{
		// 12.9p3 and 7.3.3p3: a constructor inherited from a base and a member
		// a using-declaration brought in are declarations of the base's
		// function, which is a member of the base and not of this class.
		return kNotTransfer;
	}
	const bool constructor = function.special == kConstructorFunction;
	if (!constructor && function.name != "operator=")
	{
		return kNotTransfer;
	}
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	if (parameters.size() < 2)
	{
		return kNotTransfer;
	}
	for (std::size_t index = 2; index < parameters.size(); ++index)
	{
		// 12.8p2: a constructor whose later parameters all have default
		// arguments is still a copy constructor, because a call with one
		// argument reaches it.  12.8p17 gives an assignment operator exactly
		// one parameter, so any second one leaves it an ordinary overload.
		if (!constructor || !has_default_argument(function, index))
		{
			return kNotTransfer;
		}
	}
	const TypeId written = parameters[1];
	const bool rvalue = types_.kind(written) == TypeKind::RValueReference;
	const bool reference = types_.is_reference(written);
	if (constructor && !reference)
	{
		// 12.8p2: a constructor taking its own class by value is not a copy
		// constructor - it could never be called - and 12.1p6 makes it
		// ill formed, which the declaration path is where to say.
		return kNotTransfer;
	}
	const TypeId bare =
		types_.strip_cv(reference ? types_.target(written) : written);
	if (bare != types_.strip_cv(owner.type))
	{
		return kNotTransfer;
	}
	if (constructor)
	{
		return rvalue ? kMoveConstructorTransfer : kCopyConstructorTransfer;
	}
	return rvalue ? kMoveAssignmentTransfer : kCopyAssignmentTransfer;
}

// 8.3.6p1: whether the declarations of `function` gave the parameter at `index`
// in its type a default argument, which is what lets a call leave it out.
bool SemaAnalyzer::has_default_argument(const SemaEntity& function,
                                        std::size_t index)
{
	const std::unordered_map<std::uint32_t, std::vector<Default> >::const_iterator
		found = defaults_.find(function.id);
	return found != defaults_.end() && index < found->second.size() &&
		found->second[index].written != nullptr;
}

// 12.8: the four value-transfer members the program itself declared, recorded
// on the class.  Constructors are the chain the class already holds and
// assignment operators are the declarations of one name in its own region, so
// both are one walk and neither searches.
void SemaAnalyzer::note_transfers(SemaEntity& entity, Scope& scope)
{
	for (SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		note_transfer(entity, *at);
	}
	for (SemaEntity* at = own_assignments(scope); at != nullptr; at = at->next)
	{
		if (at->kind == SemaKind::Function)
		{
			note_transfer(entity, *at);
		}
	}
}

// 13.5.3p1: the declarations of `operator=` this class's own region holds.
//
// 12.8 asks about the declarations of one class, so what answers it is the
// binding that class's region has and not 3.4's lookup for the name: 10.2 would
// reach a base's `operator=` and 3.4.1 an enclosing class's, and both of those
// are declarations of another class, whose facts belong to that class alone.
SemaEntity* SemaAnalyzer::own_assignments(Scope& scope)
{
	return model_.find(scope, "operator=", LookupKind::Any);
}

// 12.3.2p1 and 13.3.1.5p1: the conversion functions an object of this class
// has.
//
// They are the ones this class declared and the ones a base declared that
// 10.2p2 does not hide - a conversion this class declares to the same type
// hides the base's, exactly as any member of that name would.  The list is
// built once, where 9.2p2 completes the class, out of this class's own
// declarations and the list its base already holds: a hierarchy of n classes
// each declaring one conversion costs one step per class rather than one walk
// of the chain per conversion asked for.
void SemaAnalyzer::collect_conversions(SemaEntity& entity, Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity* const at = scope.declarations[index];
		if (at->conversion_function)
		{
			entity.conversions.push_back(at);
		}
	}
	entity.conversions_above = !entity.conversions.empty()
		? &entity
		: (entity.base != nullptr ? entity.base->conversions_above : nullptr);
}

// 12.3.2p1 and 13.3.1.5p1: the conversion functions an object of `owner` has,
// which are the ones its class declares and the ones a base declares that
// 10.2p2 does not hide.
//
// The classes that declare any are a chain each class holds the head of, so
// this walks those and not every base - and a class writes its candidates once
// per question asked rather than once per class derived from it.
void SemaAnalyzer::gather_conversions(const SemaEntity& owner,
                                      std::vector<SemaEntity*>& out)
{
	out.clear();
	for (const SemaEntity* at = owner.conversions_above; at != nullptr;
	     at = at->base != nullptr ? at->base->conversions_above : nullptr)
	{
		const std::size_t nearer = out.size();
		for (std::size_t index = 0; index < at->conversions.size(); ++index)
		{
			SemaEntity* const candidate = at->conversions[index];
			bool hidden = false;
			for (std::size_t here = 0; here < nearer && !hidden; ++here)
			{
				// 10.2p2: a conversion to the same type declared in a class
				// nearer the object hides this one, exactly as any member of
				// that name would.
				hidden = out[here]->name == candidate->name;
			}
			if (!hidden)
			{
				out.push_back(candidate);
			}
		}
	}
}

void SemaAnalyzer::note_transfer(SemaEntity& entity, SemaEntity& function)
{
	const unsigned char kind = transfer_kind(function, entity);
	function.transfer = kind;
	if (kind != kNotTransfer && entity.transfers[kind - 1] == nullptr)
	{
		entity.transfers[kind - 1] = &function;
	}
}

// 12.8p7, p9, p18 and p20: the value-transfer members a class that declared
// none of its own has.  The copy constructor and the copy assignment operator
// are always there; the move constructor and the move assignment operator only
// where the program declared no copy member, no move member and no destructor -
// because a class that wrote any one of those said how its objects are carried,
// and the standard does not add a second answer beside it.
void SemaAnalyzer::declare_transfer_members(SemaEntity& entity, Scope& scope,
                                            bool wrote_destructor)
{
	const bool wrote_copy_constructor =
		entity.transfers[kCopyConstructorTransfer - 1] != nullptr;
	const bool wrote_move_constructor =
		entity.transfers[kMoveConstructorTransfer - 1] != nullptr;
	const bool wrote_copy_assignment =
		entity.transfers[kCopyAssignmentTransfer - 1] != nullptr;
	const bool wrote_move_assignment =
		entity.transfers[kMoveAssignmentTransfer - 1] != nullptr;
	// 12.8p9 and p20: the one condition both of the move members are declared
	// under, which any copy member, any move member of the other kind, or a
	// destructor the program wrote takes away.
	const bool moves = !wrote_copy_constructor && !wrote_copy_assignment &&
		!wrote_destructor;
	// 12.8p7 and p18: a class that said how its objects move is one whose
	// implicitly declared copy members are deleted.
	const bool deleted_copies = wrote_move_constructor || wrote_move_assignment;
	if (!wrote_copy_constructor)
	{
		declare_transfer_member(entity, scope, kCopyConstructorTransfer,
		                        deleted_copies);
	}
	if (!wrote_move_constructor && moves)
	{
		declare_transfer_member(entity, scope, kMoveConstructorTransfer, false);
	}
	if (!wrote_copy_assignment)
	{
		declare_transfer_member(entity, scope, kCopyAssignmentTransfer,
		                        deleted_copies);
	}
	if (!wrote_move_assignment && moves)
	{
		declare_transfer_member(entity, scope, kMoveAssignmentTransfer, false);
	}
}

// 12.8p8, p10, p19 and p21: one value-transfer member the standard rather than
// the program declares.  A constructor takes the object it builds and the one
// it is built from; an assignment operator takes the object it writes to and
// hands it back as an lvalue.  Both are inline, because a definition no
// declaration wrote belongs to every translation unit that needs one.
void SemaAnalyzer::declare_transfer_member(SemaEntity& entity, Scope& scope,
                                           unsigned char kind, bool deleted)
{
	const bool constructor = kind == kCopyConstructorTransfer ||
		kind == kMoveConstructorTransfer;
	const bool moving = kind == kMoveConstructorTransfer ||
		kind == kMoveAssignmentTransfer;
	std::vector<TypeId> parameters;
	parameters.push_back(types_.pointer_to(entity.type));
	// 12.8p8 and p19: the parameter is `const C&` for a copy and `C&&` for a
	// move.  The non-const `C&` form p8 allows is not written here: every
	// subobject this milestone copies is copied through a `const` reference.
	parameters.push_back(types_.reference_to(
		moving ? entity.type : types_.qualified(entity.type, kCvConst),
		moving));
	const TypeId result = constructor
		? types_.fundamental(FT_VOID)
		: types_.reference_to(entity.type, false);
	const std::string spelled = constructor
		? QualifiedName(types_.user_name(entity.type)).last()
		: std::string("operator=");
	SemaEntity& member = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(result, parameters, false));
	name_in_region(member, scope, spelled);
	member.object_member = true;
	member.inline_function = true;
	member.defaulted = true;
	member.deleted = deleted;
	member.transfer = kind;
	member.tail = &member;
	entity.transfers[kind - 1] = &member;
	if (constructor)
	{
		member.special = kConstructorFunction;
		// 13.1: the constructors of a class are the declarations of one name,
		// so this one joins the chain 13.3.1.3 walks rather than replacing it.
		if (entity.constructor == nullptr)
		{
			entity.constructor = &member;
		}
		else
		{
			entity.constructor->tail->next = &member;
		}
		entity.constructor->tail = &member;
		model_.hold_overload(*entity.constructor, types_.signature(member.type),
		                     member);
		model_.declare_in(scope, member);
		return;
	}
	// 13.5.3p1: `operator=` is a name an ordinary lookup in the class reaches,
	// so the declaration is bound in this class's own region as well as chained.
	// A base's or an enclosing class's declaration of the name is another
	// class's member, which this one neither joins nor hides.
	SemaEntity* const head = own_assignments(scope);
	if (head == nullptr)
	{
		model_.bind(scope, "operator=", member);
	}
	else
	{
		head->tail->next = &member;
		head->tail = &member;
		model_.hold_overload(*head, types_.signature(member.type), member);
	}
	model_.declare_in(scope, member);
}

// 12.8p15 and p28: the special member one subobject of class type is carried by
// where the enclosing class's is `kind`.  A class with no move member is moved
// by its copy member instead, because 12.8p15 direct-initializes the subobject
// from an xvalue and 13.3 binds that to a `const` lvalue reference where no
// rvalue one is declared.
SemaEntity* SemaAnalyzer::selected_transfer(TypeId type, unsigned char kind)
{
	SemaEntity* const owner = model_.type_owner(member_copy_type(type));
	if (owner == nullptr)
	{
		return nullptr;
	}
	SemaEntity* const chosen = owner->transfers[kind - 1];
	if (chosen != nullptr)
	{
		return chosen;
	}
	if (kind == kMoveConstructorTransfer)
	{
		return owner->transfers[kCopyConstructorTransfer - 1];
	}
	if (kind == kMoveAssignmentTransfer)
	{
		return owner->transfers[kCopyAssignmentTransfer - 1];
	}
	return nullptr;
}

// 12.8p12, p13, p25 and p26: whether a value-transfer member the standard gives
// this class does nothing but copy the bytes of an object, and 12.8p11 and p23
// whether the standard can give it a definition at all.  Both are one walk of
// the subobjects: a subobject of class type is carried by the member its own
// class has, and a subobject of any other type is carried by its storage - so
// what the enclosing member is, is what its parts are.
void SemaAnalyzer::settle_transfers(SemaEntity& entity, Scope& scope)
{
	for (unsigned char kind = 1; kind <= kTransferKinds; ++kind)
	{
		SemaEntity* const member = entity.transfers[kind - 1];
		if (member == nullptr || !member->defaulted)
		{
			// 8.4.2p1: a definition the program wrote is what the program says
			// it is, and nothing about the class makes it trivial.
			continue;
		}
		bool trivial = true;
		bool deleted = member->deleted;
		const bool assignment = kind == kCopyAssignmentTransfer ||
			kind == kMoveAssignmentTransfer;
		if (entity.base != nullptr)
		{
			SemaEntity* const carried = selected_transfer(entity.base->type, kind);
			if (carried == nullptr || carried->deleted)
			{
				deleted = true;
			}
			else
			{
				trivial = trivial && carried->trivial;
				if (!accessible(*carried, scope))
				{
					deleted = true;
				}
			}
		}
		for (std::size_t index = 0; index < scope.declarations.size(); ++index)
		{
			SemaEntity& field = *scope.declarations[index];
			if (!declares_subobject(field, scope))
			{
				continue;
			}
			const TypeId bare = member_copy_type(field.type);
			// 8.3.4p1: an array of const elements is const in each of them, so
			// what says whether a member can be written is the cv-qualification
			// of the element rather than of the array around it.
			TypeId element = field.type;
			while (types_.kind(types_.strip_cv(element)) == TypeKind::Array)
			{
				element = types_.target(types_.strip_cv(element));
			}
			if (assignment && (types_.is_reference(field.type) ||
			                   (types_.cv(element) & kCvConst) != 0))
			{
				// 12.8p23: a member of const-qualified or reference type is one
				// an assignment has nothing to write, so the standard gives the
				// class's own assignment no definition.
				deleted = true;
				continue;
			}
			if (!assignment &&
			    types_.kind(field.type) == TypeKind::RValueReference)
			{
				// 12.8p11: a member of rvalue reference type is one a copy has
				// nothing to bind.
				deleted = true;
				continue;
			}
			if (!types_.is_class(bare))
			{
				continue;
			}
			SemaEntity* const carried = selected_transfer(field.type, kind);
			if (carried == nullptr || carried->deleted ||
			    !accessible(*carried, scope))
			{
				deleted = true;
				continue;
			}
			trivial = trivial && carried->trivial;
		}
		member->trivial = trivial && !deleted;
		member->deleted = deleted;
	}
	// 12.8p11 and p12: what an object of this class is carried by is what its
	// copy constructor is, and the layout wrote a first answer for it before
	// this class had one.  Both halves are settled here so that the layout of a
	// class holding one of these, 5.2.2p4's argument and the lowering's copy of
	// an object all read the one fact rather than each asking the declarations
	// again: whether the bytes stand for the copy, and whether the program has
	// a copy of an object of this class at all.
	const SemaEntity* const copy = entity.transfers[kCopyConstructorTransfer - 1];
	const bool deleted_copy = copy == nullptr || copy->deleted;
	types_.settle_copy_facts(types_.strip_cv(entity.type),
	                         !deleted_copy && copy->trivial, deleted_copy);
}

// 11.2p1: whether a member of another class may be named from inside `scope`,
// which is what 12.8p11 and p23 ask of the transfer member of every subobject.
bool SemaAnalyzer::accessible(const SemaEntity& member, Scope& scope)
{
	if (member.access == kPublicAccess)
	{
		return true;
	}
	for (Scope* at = &scope; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return true;
		}
	}
	if (member.access != kProtectedAccess)
	{
		return false;
	}
	// 11.4p1: a protected member of a base class is one a class derived from it
	// may name, and 12.8p11's subobject is exactly the base subobject of the
	// class asking - so the derivation this class already holds is the answer.
	for (Scope* at = scope.base; at != nullptr; at = at->base)
	{
		if (at == member.region)
		{
			return true;
		}
	}
	return false;
}

// 12.1p5: whether the definition the standard would give a default constructor
// of this class is one it cannot write - a non-static data member of
// const-qualified or reference type that no brace-or-equal-initializer reaches
// is a subobject 8.5p6 leaves holding nothing and 8.5.3p1 leaves bound to
// nothing, and the constructor that would have to initialize it is deleted.
bool SemaAnalyzer::undefinable_default(Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope) || member.default_initializer)
		{
			continue;
		}
		// 8.3.4p1: an array of const elements is const in each of them, so the
		// element type is what says whether a member can be left alone - and
		// its own cv-qualification is exactly what the question is about.
		TypeId type = member.type;
		while (types_.kind(types_.strip_cv(type)) == TypeKind::Array)
		{
			type = types_.target(types_.strip_cv(type));
		}
		if (types_.is_reference(type) ||
		    (!types_.is_class(types_.strip_cv(type)) &&
		     (types_.cv(type) & kCvConst) != 0))
		{
			return true;
		}
	}
	return false;
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
	name_in_region(constructor, scope, spelled);
	constructor.object_member = true;
	// 7.1.2p3 and 12.1p5: a constructor no declaration wrote is inline, so the
	// definition it is given belongs to every translation unit that needs one
	// rather than to the one that happened to write the class.
	constructor.inline_function = true;
	constructor.trivial = trivial_default_construction(scope);
	// 12.1p5: a member of const-qualified or reference type that nothing
	// initializes leaves the standard no definition to give this constructor.
	constructor.deleted = undefinable_default(scope);
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
	name_in_region(destructor, scope, spelled);
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

bool SemaAnalyzer::observable(const DumpNode& node) const
{
	return observable_expression(node);
}

bool observable_expression(const DumpNode& node)
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
		if (observable_expression(*node.children[index]))
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

void SemaAnalyzer::inject_anonymous_members(SemaEntity* entity,
                                            const Context& ctx, const Span& span,
                                            bool is_static)
{
	// 9.5p1: a class with no name and no declarator declares its members in the
	// region it is written in rather than a region of its own.  The standard
	// writes the rule for a union; the anonymous struct the README puts in this
	// milestone's subset is the same rule over a class whose members are laid
	// out one after another instead of over one, which is what the class's own
	// layout already settled - so what the two share is that the members are
	// reached through an object no name reaches, and nothing here has to tell
	// them apart.
	if (entity == nullptr || entity->scope == nullptr || !entity->name.empty() ||
	    !types_.is_class(entity->type))
	{
		return;
	}
	// 9.5p3 asks a namespace-scope anonymous aggregate to be declared `static`,
	// and the object it declares is one no other unit has a name for either
	// way - so the object file gives it internal linkage whether or not the
	// specifier was written, rather than owing a name nothing can reach.  The
	// earlier milestones accept the form without it and their fixtures ask for
	// that, so the specifier changes nothing here.
	const bool at_namespace = ctx.scope->kind == ScopeKind::Namespace;
	(void)is_static;
	// 9.5p1 is written for the union, and the anonymous struct is the extension
	// the README puts in this milestone's subset - as a *member*, which is
	// where g++ and the references accept one and nowhere else.  Written
	// anywhere else it declares nothing, so nothing is what it is read as.
	if (types_.class_tag(entity->type) != ClassTag::Union &&
	    ctx.scope->kind != ScopeKind::Class)
	{
		throw std::runtime_error("an anonymous struct declares nothing outside "
		                         "a class");
	}
	// 9.5p1: the class declares an object of itself that has no name either, so
	// a member is still a member of an object, and the convention names that
	// object after the terminals the declaration was written from, as it names
	// the class.
	SemaEntity* storage = nullptr;
	if (semantics())
	{
		const std::string name =
			(types_.class_tag(entity->type) == ClassTag::Union
				 ? "__anonymous_union_storage__"
				 : "__anonymous_class_storage__") +
			decimal(span.begin) + "_" + decimal(span.end);
		storage = &model_.create(SemaKind::Variable, name, entity->type);
		model_.bind(*ctx.scope, name, *storage);
		model_.declare_in(*ctx.scope, *storage);
		storage->object_member = ctx.scope->kind == ScopeKind::Class;
		// 3.1p2 and 9.5p3: at namespace scope the class declared an object, and
		// this unit is the one that defines it - with the internal linkage
		// `static` gave it, which is what keeps two units that each write one
		// from naming the same object.
		storage->object_definition = at_namespace;
		storage->internal_linkage = storage->internal_linkage || at_namespace;
		if (ctx.node != nullptr)
		{
			DumpNode& line = open_fact(*ctx.node, "variable " + name + " " +
			                           types_.description(entity->type),
			                           FactKind::Variable);
			line.fact.entity = storage;
			line.fact.type = entity->type;
			line.fact.object_definition = at_namespace;
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
		if (member.storage == nullptr)
		{
			member.storage = storage;
		}
		// 9.5p1 through a class written inside this one: the member already
		// reaches the object that class declared, and what this one adds is
		// where *that* object stands - which is the object this class declared.
		// So the chain is left alone and the object of the inner class is given
		// its own place in the outer one, which the walk that names the member
		// then follows from the outside in.
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
	if (!semantics())
	{
		return type;
	}
	// 8.3.5p1: the ref-qualifier is already part of the function type the
	// declarator built, and 9.3.1p3's rebuild below is what carries it over.
	const RefQualifier ref = types_.function_ref_qualifier(type);
	// 8.3.5p5: the cv-qualifier-seq of a member function is written after its
	// parameter-clause, so it is a suffix of the declarator rather than one of
	// the qualifiers its specifiers wrote - and it qualifies the object rather
	// than the function, which is why it is read here and not there.
	const unsigned cv = declarator_function_cv(declarator);
	// 12.5p1: an allocation or deallocation function a class declares is a
	// static member of it even where `static` was not written.  The storage it
	// is asked for is what an object of the class would stand in, so there is no
	// object for 9.3.1p3's implicit parameter to name.
	// 9.4.1p2: the definition of a static member function written outside its
	// class shall not repeat `static`, so which kind of member a qualified
	// declarator declares is a fact of the declaration in the class it
	// redeclares rather than of its own specifiers - and a static member
	// function writes no ref-qualifier, so the declaration it is looked for
	// among is one that carries none.
	const bool object_member =
		target.scope->kind == ScopeKind::Class && !is_static &&
		target.scope->owner != nullptr && !allocation_function_name(name) &&
		!(qualified &&
		  declares_static_member(
			  *target.scope, name,
			  types_.ref_qualified_function(type, RefQualifier::None)));
	if (!object_member)
	{
		if (ref != RefQualifier::None)
		{
			// 8.3.5p6: a ref-qualifier shall be part of a function declarator
			// only where it declares a non-static member function, because
			// 13.3.1p4 gives it a meaning only where there is an implicit
			// object parameter for it to qualify.
			throw std::runtime_error(
				name + " is declared with a ref-qualifier where 8.3.5p6 allows "
				"one only on a non-static member function");
		}
		return type;
	}
	std::vector<TypeId> parameters;
	parameters.push_back(
		types_.pointer_to(types_.qualified(target.scope->owner->type, cv)));
	const std::vector<TypeId>& written = types_.parameters(type);
	parameters.insert(parameters.end(), written.begin(), written.end());
	return types_.ref_qualified_function(
		types_.function_of(types_.target(type), parameters,
		                   types_.variadic(type)),
		ref);
}

// 8.3.5p5: the cv-qualifier-seq a declarator wrote after its parameter-clause,
// which is what 9.3.1p3 qualifies the object parameter by.  The parser hangs it
// on the declarator beside the declarator-id, so what tells it from a qualifier
// the specifiers wrote is that it stands after that id.
unsigned SemaAnalyzer::declarator_function_cv(const AstNode& declarator)
{
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
	return cv;
}

// 8.3.5p1: the ref-qualifier written there, for the two declarations that build
// their own function type rather than taking the one an ordinary declarator
// walk built - 12.3.2p1's conversion function, whose declarator writes no type,
// and 12.1/12.4's constructor and destructor, which write no return type.
RefQualifier SemaAnalyzer::declarator_ref_qualifier(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		// The grammar spells an exception-specification with the same node, so
		// which one this is is what it was written as.
		if (part.kind != AstKind::FunctionQualifier)
		{
			continue;
		}
		if (part.text == "&")
		{
			return RefQualifier::LValue;
		}
		if (part.text == "&&")
		{
			return RefQualifier::RValue;
		}
	}
	return RefQualifier::None;
}

// 15.4p1: whether the exception-specification the declarator wrote says the
// function throws nothing.  The grammar spells it with the same node the
// ref-qualifier uses, so which one this is, is what it was written as: `throw`
// with an empty type-id-list, or `noexcept` with no constant-expression or with
// one that is the `true` the translation reads without evaluating anything.
// Any other spelling leaves the answer no, which is the reading that changes
// nothing about what a program written with it comes to.
bool SemaAnalyzer::declarator_nonthrowing(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind == AstKind::NestedDeclarator && declarator_nonthrowing(part))
		{
			return true;
		}
		if (part.kind != AstKind::FunctionQualifier)
		{
			continue;
		}
		if (part.text == "throw()")
		{
			return true;
		}
		if (part.text.compare(0, 8, "noexcept") != 0)
		{
			continue;
		}
		if (part.children.empty() || part.children[0] == nullptr)
		{
			return true;
		}
		if (part.children[0]->kind == AstKind::KeywordLiteral &&
		    part.children[0]->token == KW_TRUE)
		{
			return true;
		}
	}
	return false;
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
	// 13.1's index of a class's chain is keyed by the list the declarator wrote,
	// and a definition of a static member function writes the same list its
	// declaration did.
	const SemaEntity* const prior =
		model_.overload_of(*head, member_signature(type, false));
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
