#include "sema_analyzer.h"

#include "sema_constexpr.h"

#include <stdexcept>
#include <string>

#include "ast_model.h"
#include "ast_tokens.h"

// 7.2: the enumeration a declaration writes, and the enumerators its
// definition declares.
//
// The two are one owner because 7.2p5 to p8 settle them together: what an
// enumerator is worth is read where it stands, from the value before it, and
// what type the enumeration and each of its enumerators have is settled from
// every one of them at once - so the walk that declares them is also the walk
// that widens the type they are read as.  7.1.3p2's unnamed enumeration is
// named by the first declarator of its declaration, which is a fact about the
// declaration rather than about the enumerators, so it reaches here as the
// name that declaration already found.

namespace
{

// 7.1.3p2's numbering of an enumeration no declarator names, as digits.
std::string counted(unsigned long long value)
{
	std::string digits;
	for (unsigned long long rest = value; rest != 0; rest /= 10)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
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

bool has_child(const AstNode& node, AstKind kind)
{
	return child_of(node, kind) != nullptr;
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
		? "__anonymous_enum" + counted(++anonymous_enums_)
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
		own_type(type, *entity);
		note_nested_in_dependent(type, *ctx.scope);
		declare_type_name(name, *ctx.scope);
		model_.bind(*ctx.scope, name, *entity);
		model_.declare_in(*ctx.scope, *entity);
		// 9.8p1 read of an enumeration: a function's body declares it too, and
		// the object file names it after that function for the same reason.
		if (unnamed)
		{
			// 7.2p1 gave this one no name, so the spelling bound above is one
			// this unit counted for itself; the ABI's `<unnamed-type-name>` is
			// what the object file names it by, in the one sequence the region
			// counts its classes and its enumerations in.
			model_.settle_unnamed_local_name(*ctx.scope, *entity);
		}
		types_.set_local_name(type, entity->local_function,
		                      entity->local_occurrence,
		                      entity->local_unnamed);
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
			// 7.2p5: the value is an *integral* constant expression, which for
			// an object of class type is 12.3.2p1's conversion function and not
			// the identifier the constant holds - and which no value of
			// 3.9.1p8's floating types is, however 4.9 would convert one.
			const Constant written = ConstexprReading(*this).at_arithmetic_place(
				evaluate(*child.children[0], inner), kNoType);
			value = ConstexprReading(*this).counted(written);
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
		require_no_template_parameter(child.text, scope);
		model_.bind(scope, child.text, enumerator);
		model_.declare_in(scope, enumerator);
		if (!scoped && scope.parent != nullptr)
		{
			// 7.2p10: an unscoped enumeration's enumerators are declared in the
			// region the enumeration is declared in, which is not where the
			// definition is written when 7.2p1 writes it outside its class.
			require_no_template_parameter(child.text, *scope.parent);
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
