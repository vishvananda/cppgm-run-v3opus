#include "sema_analyzer.h"

#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"

// 9.2p13 and the course ABI: what an object of a class is laid out as.
//
// One walk of the class's own declarations, in the order it declares them,
// settles every byte of it at once: where the vpointer 10.3p1 asks for stands,
// where the base subobject begins, where each non-static data member begins,
// which storage unit 9.6p2's bit-fields share, how large and how strictly
// aligned the whole is, and which of the ABI's questions about carrying an
// object of it by value the answer to that walk is.  It is one pass because
// each of those facts is read off the same cursor, and it runs once per class,
// where 9.2p2 completes it - so every later use of a member, a base or an
// object of the class is a read and never a second walk.

namespace
{

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
		// 7.6.2p1: what the specifier asks for is an integral constant
		// expression, which 5.19p3 lets an object of class type reach through
		// 12.3.2p1's conversion function.
		const Constant value = ConstexprReading(*this).at_arithmetic_place(
			evaluate(*child.children[0], ctx), kNoType);
		const unsigned long long asked = ConstexprReading(*this).counted(value);
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
		// 9.6p1: the width is an integral constant expression, so a floating
		// value is no width however 4.9 would convert one.
		const Constant value = ConstexprReading(*this).at_arithmetic_place(
			evaluate(written, ctx), kNoType);
		if (is_signed(value.type) &&
		    (ConstexprReading(*this).counted(value) >> 63) != 0)
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
		// 9.6p1 and 14.6p8: which type a bit-field was declared with, an
		// argument list is what says where the declaration wrote a template
		// parameter - so a reading of the definition asks neither of 9.6p1's
		// two questions about the type, and the specialization asks both.
		const bool dependent = checking_ > 0 && types_.is_dependent(bare);
		if (!dependent && !types_.is_integral(bare) &&
		    types_.kind(bare) != TypeKind::Enum)
		{
			throw std::runtime_error("a bit-field does not have integral or "
			                         "enumeration type");
		}
		if (!dependent && value.bits > 8 * types_.object_size(bare))
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

// 9.5p1: the members of a union all begin at the same address, so the storage
// is one storage and at most one member holds an object in it.  Every rule that
// walks a class's members has to know it, and each of them wants a different
// consequence of it, so the fact itself is asked here and nowhere else.
bool SemaAnalyzer::one_storage(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	return types_.is_class(bare) && types_.class_tag(bare) == ClassTag::Union;
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
	// 12.8p12 and p25: a class with a virtual function has no trivial copy or
	// move member, because the vpointer of the object being written into is not
	// the one the bytes of the source hold wherever the two are of different
	// classes - so the copy is a call and the ABI carries an object of the
	// class by address.
	bool trivially_copied =
		!entity.polymorphic && !declares_copy_constructor(entity, scope);
	// 12.8p12 over the storage this class is laid out over, which is the same
	// walk with this class's own declaration left out of it: 5.2.2p4's boundary
	// carries an object of a derived class the way the subobjects it is made of
	// are carried, so what it reads is what the base and the members are.
	// 12.8p12 again over the one thing in that storage no subobject accounts
	// for: a class that dispatches holds a vpointer, and the pointer the bytes
	// of the source hold is the source's own class's - so the storage of a
	// polymorphic class is not carried by its bytes however its base and its
	// members are carried.
	bool subobject_bytes = !entity.polymorphic;
	// 8.5p8: what zero-initializing an object of this class writes is what its
	// base subobject and its non-static data members hold, so a class every
	// subobject of which holds nothing has no byte to write - which 1.8p5's
	// size for it cannot say, because a size it has either way.
	bool zeroed_storage = false;
	// 9.5p2: how many members of this union wrote a brace-or-equal-initializer,
	// which is at most one.  Zero for every class that is not a union.
	unsigned default_initializers = 0;
	if (entity.introduces_vptr)
	{
		// 10.3p1 and the ABI: the class that adds the vpointer gives it the
		// first eight bytes of every object of it, so the base subobject and
		// the members alike are laid out after it.  A class whose base already
		// carries one adds nothing here: the pointer stands at the start of the
		// base subobject, which stands at the start of this object.
		size = kVpointerBytes;
		align = kVpointerBytes;
		empty = false;
	}
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		// 10p1 and the course ABI: the direct base subobjects are allocated in
		// the order the base-specifier-list wrote them, the first of them where
		// the derived object begins - or, where this class put a vpointer
		// there, after it - and the members after all of them.  A base that
		// holds nothing is given no storage of its own, so the base after it
		// and the members alike start where it did.
		BaseClass& link = entity.bases[index];
		const TypeId base_type = link.entity->type;
		const unsigned long long base_align =
			packed_align(types_.object_align(base_type), packed);
		if (base_align > align)
		{
			align = base_align;
		}
		if (!types_.is_trivially_copied(types_.strip_cv(base_type)))
		{
			trivially_copied = false;
			subobject_bytes = false;
		}
		if (types_.has_zeroed_storage(base_type))
		{
			zeroed_storage = true;
		}
		link.offset = link.entity->empty_class ? 0 : round_up(size, base_align);
		// The ABI again: two subobjects of the same class may not begin at the
		// same byte, which an empty base standing where an earlier one already
		// put one of its class is exactly what would do.
		while (collides_with_empty(base_type, link.offset, holes))
		{
			link.offset = round_up(link.offset + 1, base_align);
		}
		if (!link.entity->empty_class)
		{
			size = link.offset + types_.object_size(base_type);
			empty = false;
		}
		// The base subobject begins where this class put it, so every empty
		// class subobject of it stands at the byte it stands at inside the
		// base - and no member of this class may be given one of those bytes
		// for a subobject of the same class.
		place_empty_subobjects(base_type, link.offset, holes);
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
			subobject_bytes = false;
		}
		if (types_.has_zeroed_storage(member.type))
		{
			zeroed_storage = true;
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
			if (member.default_initializer)
			{
				// 9.5p2: at most one variant member of a union may have a
				// brace-or-equal-initializer, because every one of them stands
				// in the one storage and a second would say what that storage
				// holds a second time.  The walk already has the members in
				// declaration order, so the second one is refused where it is
				// laid out rather than by a scan of its own.
				if (default_initializers)
				{
					throw std::runtime_error(
						"a union declares a brace-or-equal-initializer for "
						"more than one of its members");
				}
				++default_initializers;
			}
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
	                      trivially_copied, zeroed_storage, subobject_bytes);
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
