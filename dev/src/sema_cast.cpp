#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

// 5.2.9, 5.2.11, 5.4 and 5.2.3p1: what a cast is written over and what it makes
// of it.
//
// A cast is one question asked of two very different answers, which is why it
// owns a file of its own: to a type that is not a reference it is 5.2.9p4's
// direct-initialization of a temporary of that type, and to a *reference* it is
// 8.5.3's binding - and the binding is where the readings part company.  A
// reference-related target binds the object the operand already named and the
// cast's own line goes away; a reference that may bind a temporary binds the one
// holding the conversion of the operand, and which node stands for that object
// says who names its storage; and a reference that may bind neither is 5.4p4's
// reinterpretation of the storage the operand named, which a named static_cast
// has no reading of at all.
//
// Nothing here writes an object of its own: it asks 13.3 for the conversion,
// `sema_lifetime.cpp` for the temporary that holds one, and 12.2p3's open
// full-expression for the end of that temporary's lifetime.

namespace
{

// The one child of `node` the parse gave this kind, which is how a cast reaches
// the type-id it was written over.
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

}  // namespace

// The operand's line takes the place the cast's would have had, in the order
// the parent already holds: 5.2.9p1 gives the two the same one node wherever
// the cast converts nothing the output describes.
void SemaAnalyzer::lift_operand(DumpNode& parent, DumpNode& line)
{
	parent.children.pop_back();
	for (std::size_t index = 0; index < line.children.size(); ++index)
	{
		parent.children.push_back(line.children[index]);
	}
}

SemaAnalyzer::Value SemaAnalyzer::cast_expression(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent)
{
	const AstNode* type_id = child_kind(node, AstKind::TypeId);
	if (type_id == nullptr)
	{
		throw std::runtime_error("a cast expression names no type");
	}
	const TypeId target = type_id_type(*type_id, ctx);
	DumpNode& line = model_.open_node(parent, std::string());
	const AstNode& operand = *node.children[node.children.size() - 1];
	Value source = expression(operand, ctx, line);
	if (source.type == kNoType && source.functions != nullptr)
	{
		// 13.4p1: a cast is one of the contexts whose target type chooses one
		// declaration of an overloaded name, whether or not it is a reference.
		SemaEntity* chosen = resolve_target(source, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type a cast asks for");
		}
		name_function(source, *chosen, "id-expression");
	}

	Value value;
	value.type = target;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	value.what = "cast-expression";
	value.op = node.token == kNoAstToken ? 0u : node.token;
	if (node.token != kNoAstToken)
	{
		value.payload = payload_of(node);
	}
	if (types_.is_reference(target))
	{
		return cast_to_reference(target, source, parent, line, value, ctx);
	}
	if (types_.kind(types_.strip_cv(target)) == TypeKind::Pointer &&
	    types_.kind(types_.strip_cv(source.type)) == TypeKind::Pointer)
	{
		const TypeId to = types_.target(types_.strip_cv(target));
		const TypeId from = types_.target(types_.strip_cv(source.type));
		// 5.2.9p11 and 4.10p3: a cast to a pointer to a base class of the
		// operand's class is that conversion, so it names the base subobject
		// rather than reinterpreting the address - and 11.2p4 asks here whether
		// the base-specifier's access reaches this expression.
		SemaEntity* const base = derived_from(from, to);
		if (base != nullptr)
		{
			source = base_value(source, *base);
			lift_operand(parent, line);
			source.type = source.spelled = target;
			source.category = ValueCategory::PRValue;
			return source;
		}
		// 5.2.9p11 the other way round: a pointer to a base class casts to a
		// pointer to a class derived from it, which is well formed only where
		// that base is accessible.  The subobject begins where the derived object
		// does, so the address is the one the operand held and the cast writes no
		// conversion around it - but the access is asked for all the same.
		SemaEntity* const from_base = derived_from(to, from);
		if (from_base != nullptr)
		{
			require_base_access(model_.type_owner(types_.strip_cv(to)),
			                    *from_base);
		}
	}
	if (types_.kind(types_.strip_cv(target)) == TypeKind::MemberPointer &&
	    source.type == types_.strip_cv(target))
	{
		// 5.2.9p2: a cast to the type the operand has converts nothing, and a
		// pointer to member holds which member it names rather than an address,
		// so there is nothing for the output to write around the operand.
		lift_operand(parent, line);
		return source;
	}
	// 5.2.9p4 and 5.4p4: the cast is a direct-initialization of the target from
	// the operand, so an operand of class type reaches it through a conversion
	// function of its class - one 12.3.2p2 lets be declared `explicit`, and
	// none of which leaves the cast with nothing to read.
	cast_conversion(source, target, ctx);
	line.text = spell(value.what, value.category, target, value.payload);
	value.node = &line;
	record(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::cast_to_reference(TypeId target, Value& source,
                                                    DumpNode& parent,
                                                    DumpNode& line, Value value,
                                                    const Context& ctx)
{
	{
		// 5.2.9p1: a cast to an lvalue reference is an lvalue and one to an
		// rvalue reference to an object type is an xvalue.
		const TypeId referenced = types_.target(target);
		value.type = referenced;
		value.category = types_.kind(target) == TypeKind::LValueReference
			? ValueCategory::LValue
			: ValueCategory::XValue;
		// 8.5.3p5: an lvalue reference that is not to a non-volatile const binds
		// only an lvalue, and 5.4p4 offers a cast no reading that would bind one
		// to a value naming no object.
		const bool const_lvalue = (types_.cv(referenced) & kCvConst) != 0 &&
			(types_.cv(referenced) & kCvVolatile) == 0;
		if (types_.kind(target) == TypeKind::LValueReference && !const_lvalue &&
		    source.category != ValueCategory::LValue)
		{
			throw std::runtime_error("a cast to a non-const lvalue reference is "
			                         "written on an operand that names no object");
		}
		// 8.5.3p4: reference-related is the referenced type and the operand's
		// being one type up to cv-qualification, and a related reference binds
		// the value it was given rather than a conversion of it - which is what
		// 5.4p4 leaves to const_cast where a static_cast would convert nothing.
		// So the operand's own line is what says what the cast made of it, and
		// the cast writes no node.
		if (bare_type(source.type) == bare_type(referenced) &&
		    source.node != nullptr && source.what != nullptr)
		{
			// 8.5.3p5: the operand is a prvalue, so what the reference binds is
			// a temporary - and the cast is what asked for its storage, which
			// is what 12.2p1 names that storage after.  An argument written
			// around this cast binds the object the cast already named rather
			// than one of its own, so nothing later renames it.
			if (source.category == ValueCategory::PRValue &&
			    types_.is_class(types_.strip_cv(source.type)))
			{
				name_argument_temporary(source, "refcall", ctx, false);
			}
			source.category = value.category;
			source.type = referenced;
			source.spelled = target;
			respell(source);
			lift_operand(parent, line);
			return source;
		}
		// 8.5.3p4: reference-related is also the referenced type being a base
		// class of the operand's, and such a reference binds the base subobject
		// of the object the operand names rather than a conversion of its value -
		// which is the one `base-conversion` node 4.10p3 writes everywhere else.
		SemaEntity* const to_base =
			source.node != nullptr && source.what != nullptr
				? derived_from(source.type, referenced)
				: nullptr;
		if (to_base != nullptr)
		{
			source = base_value(source, *to_base);
			source.category = value.category;
			source.type = referenced;
			source.spelled = target;
			respell(source);
			lift_operand(parent, line);
			return source;
		}
		// 5.2.9p11: an lvalue of a base class casts to a reference to a class
		// derived from it, and what the result names is the object that base
		// subobject is part of.  The subobject begins where the derived object
		// does, so the storage the operand named is the storage the cast names,
		// and 11.2p4 asks here whether the base-specifier's access reaches.
		SemaEntity* const from_base = derived_from(referenced, source.type);
		if (from_base != nullptr)
		{
			require_base_access(model_.type_owner(types_.strip_cv(referenced)),
			                    *from_base);
			value.payload.clear();
			value.node = &line;
			respell(value);
			return value;
		}
		// 8.5.3p5: the operand is converted to the referenced type and the
		// reference binds the temporary that holds the conversion, which is an
		// object of the function like any other - so the conversion is applied
		// here rather than being left as a spelling on the operand's own line,
		// and what the cast is worth is that object.  The temporary is written
		// around the operand in the place the operand already had, which is the
		// same thing 13.3.3.1.2's converting constructor does for an argument.
		// 8.5.3p5: the conversion below is 5.2.9p4's `T t(e);`, and there are
		// two ways for the cast not to be one.  The reference may bind no
		// temporary for the conversion to stand in - an lvalue reference that is
		// not to a non-volatile const - and 5.2.10p11's reinterpret_cast, which
		// refers to the object the operand named under another type, is that
		// reading written by name.  Or nothing converts the operand to the
		// referenced type at all: `(const E&)i` over an enumeration, and
		// `(const W&)i` over a class, each write `T t(e);` that is ill formed.
		//
		// 5.4p4 then falls to reinterpreting the storage the operand named,
		// which is the cast's own line below - the reading `(const E&)i` over
		// an enumeration and `(const W&)i` over a class each take, and which
		// g++ and the reference binary both write for them.  A named
		// static_cast has nothing to fall to, so that is where it is refused
		// rather than where the reinterpretation would be written; 5.2.11p4's
		// const_cast has a reading of its own over a similar pointer type,
		// which is that same naming of the operand's storage.
		const Match match = match_by_value(source, referenced);
		const bool binds_temporary = match.viable &&
			(types_.kind(target) != TypeKind::LValueReference || const_lvalue) &&
			value.op != KW_REINTERPET_CAST;
		if (!binds_temporary && value.op == KW_STATIC_CAST)
		{
			throw std::runtime_error("a static_cast to a reference type is "
			                         "written on an operand of another type, "
			                         "which no `T t(e);` binds");
		}
		if (binds_temporary && types_.is_class(types_.strip_cv(referenced)))
		{
			apply_conversion(source, referenced, match, ctx,
			                 Requested::Written);
			// 8.5.3p5, 12.2p1 and 12.2p3: the conversion made an object of this
			// class and nothing but the cast asked for it - so the cast is what
			// names the storage that object stands in, and the full-expression
			// the cast was written in is what holds the end of its lifetime,
			// exactly as it holds the end of a `T(e)` written in the same place.
			// **Which node stands for that object is what says who names the
			// storage**, and 12.2p3's end is written on the object either way.
			if (source.node != nullptr &&
			    source.node->fact.kind == FactKind::TemporaryObject)
			{
				// 13.3.3.1.2's converting constructor built that object *where
				// the operand stood*, so the node it wrote is the one thing that
				// stands for it - with storage of its own already named after
				// the place that asked for it - and the cast is one more
				// spelling of the same place, which is what lets the cast's own
				// line go away.
				if (source.category == ValueCategory::PRValue)
				{
					register_temporary(*source.node, ctx.scope);
				}
				source.category = value.category;
				source.type = referenced;
				source.spelled = target;
				respell(source);
				lift_operand(parent, line);
				return source;
			}
			// 12.3.2p2's conversion function hands its object back as the
			// prvalue of a *call*, which no place has named storage for yet - so
			// the cast is what names it, exactly as it names the storage of a
			// prvalue operand of the referenced class above.
			if (source.category == ValueCategory::PRValue)
			{
				name_argument_temporary(source, "refcall", ctx, true);
			}
			// 12.3.2p2's conversion function hands its object back as the
			// prvalue of a *call*, and a prvalue of class type is what asks the
			// lowering for the storage it stands in - so the call goes on being
			// one and the cast's own line is what stands for the lvalue the
			// reference is.  Spelling that call as the lvalue instead leaves the
			// object with no storage asked for anywhere and 12.2p3's end of its
			// lifetime naming an object nothing was ever asked to build.
			value.payload.clear();
			value.node = &line;
			respell(value);
			return value;
		}
		if (match.converted != nullptr)
		{
			// 12.3.2p2: the operand is of class type and reaches the referenced
			// type through a conversion function of its own class, so what the
			// reference binds is a temporary holding what that call returned -
			// and the call is written where the operand stood.
			apply_conversion(source, referenced, match, ctx,
			                 Requested::Written);
		}
		// **Which of 5.4p4's two readings a cast is, is a fact of the node.**
		// The cast's own line is what stands for the lvalue either way and the
		// two spell the same two types, so the reader below cannot tell an
		// 8.5.3p5 binding from 5.2.10p11's reinterpretation without being told:
		// `reinterpret_cast<const float&>(i)` names the `int`'s own storage
		// under another type, and `(const int&)d` over a `double` is
		// `const int t(d);` and the address of `t`.  Handing on the operand's
		// address for the second reads the bytes of another type's
		// representation through a reference the program may keep.
		//
		// A temporary of a type that is not a class begins no lifetime the
		// program can watch, so nothing but the storage is owed for it and
		// 12.2p3 has no end to hold - which is why the fact stands on the cast's
		// own line rather than being an object of the analysis.
		//
		// 4.4p4's qualification conversion is the one conversion that leaves
		// nothing for a temporary to hold: it is between two pointers to the
		// same type, so the storage the operand named already holds the value
		// the reference binds and 5.2.11p4's const_cast is the reading 5.4p4
		// reaches first.  That is the same exception `match_reference` already
		// makes for an argument, and it is an exception only where the operand
		// named storage at all.
		const TypeId from = types_.strip_cv(source.type);
		const bool qualification_only =
			types_.kind(from) == TypeKind::Pointer &&
			source.category == ValueCategory::LValue &&
			qualification_convertible(from, types_.strip_cv(referenced));
		line.fact.binds_temporary = binds_temporary && !qualification_only;
		// A conversion to any other type is a value, and the cast's own line is
		// the whole of what stands for it, as it always was.
		value.payload.clear();
		value.node = &line;
		respell(value);
		return value;
	}
}
