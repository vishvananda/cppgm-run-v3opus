#include "sema_conditional.h"

#include <stdexcept>
#include <string>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_derivation.h"

ConditionalReading::ConditionalReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

AnalyzedValue ConditionalReading::expression(const AstNode& node,
                                             const SemaContext& ctx,
                                             DumpNode& parent)
{
	SemaAnalyzer& read = analyzer_;
	TypeTable& types = read.types_;
	DumpNode& line = read.model_.open_node(parent, std::string());
	AnalyzedValue condition_value = read.expression(*node.children[0], ctx, line);
	read.require_complete_value(condition_value);
	// 5.16p1: the first operand is contextually converted to bool, which for an
	// operand of class type is a conversion function of that class.
	read.contextual_bool(condition_value, ctx);
	if (!types.contextually_bool(condition_value.type))
	{
		throw std::runtime_error("the condition of ?: has no conversion to bool");
	}
	// 1.9p10 and 5.16p1: only one of the second and third operands is
	// evaluated, so each stands on a path of its own and a temporary it created
	// exists only there - 12.2p3's end of that temporary belongs to the arm and
	// not to the full-expression every path through the conditional reaches.
	read.open_full_expression();
	AnalyzedValue left = read.expression(*node.children[1], ctx, line);
	std::vector<SemaEntity*> left_temporaries = read.take_full_expression();
	read.open_full_expression();
	AnalyzedValue right = read.expression(*node.children[2], ctx, line);
	std::vector<SemaEntity*> right_temporaries = read.take_full_expression();
	read.require_complete_value(left);
	read.require_complete_value(right);

	AnalyzedValue value;
	value.category = ValueCategory::PRValue;
	// 5.16p3: an attempt is made to convert each operand to the type of the
	// other, and one can be converted to match an operand that is an *lvalue*
	// where it is implicitly convertible to an lvalue reference to that
	// operand's type and the reference binds directly.  The one conversion that
	// binds says what the result denotes.  Two lvalues of one type both bind,
	// which is 5.16p4; two whose types differ only in cv-qualification bind one
	// way only, and the result is the lvalue of the more qualified of the two;
	// a prvalue of another class binds where a conversion function of its class
	// hands back an lvalue of that type; and anything else binds neither way
	// and the clause falls to its last bullet below.
	//
	// Which operand is an lvalue is the question asked of *that* operand alone:
	// what the clause requires of the one being converted is a binding and not
	// a category, so `c ? make() : object` is the lvalue `object` names
	// wherever `make()`'s class reaches its type - and the operand that does
	// the reaching is written out by `convert_arm_to_result` below.
	const bool as_left = left.category == ValueCategory::LValue &&
		read.binds_reference(right, types.reference_to(left.type, false));
	const bool as_right = right.category == ValueCategory::LValue &&
		read.binds_reference(left, types.reference_to(right.type, false));
	TypeId to_left = kNoType;
	TypeId to_right = kNoType;
	if (as_left || as_right)
	{
		value.type = as_right ? right.type : left.type;
		value.category = ValueCategory::LValue;
		// 5.16p3 and 4.10p3: where the operand that bound is of a class derived
		// from the other's, what the result denotes is its base subobject, so
		// the arm that reached it names that subobject; where it reached the
		// type through a conversion function, the call is written there.
		convert_arm_to_result(as_right ? left : right, value.type, ctx);
	}
	else if (types.strip_cv(read.decayed(left)) ==
	         types.strip_cv(read.decayed(right)))
	{
		value.type = types.strip_cv(read.decayed(left));
	}
	else if ((types.is_arithmetic(read.decayed(left)) ||
	          types.kind(read.decayed(left)) == TypeKind::Enum) &&
	         (types.is_arithmetic(read.decayed(right)) ||
	          types.kind(read.decayed(right)) == TypeKind::Enum))
	{
		// 5.16p5: the usual arithmetic conversions bring two arithmetic
		// operands to one type.
		value.type = read.arithmetic_result(read.decayed(left),
		                                    read.decayed(right));
	}
	else if (reaches_other(right, left, to_left) !=
	         reaches_other(left, right, to_right))
	{
		// 5.16p3's last bullet, reached where neither operand bound a reference
		// to the other and one of the two is of class type.  What each operand
		// would be converted to is the type the *other* is worth as a prvalue,
		// and exactly one of the two directions has to reach it: two that both
		// reach leave the conditional ambiguous, and the refusal for that one
		// is the same "no common type" every other pair gets below.
		AnalyzedValue& moving = to_left != kNoType ? right : left;
		value.type = to_left != kNoType ? to_left : to_right;
		SemaEntity* const base =
			Derivation(read).base_in(moving.type, value.type);
		if (base != nullptr)
		{
			// 5.16p3: the operand is changed to a prvalue of the base class
			// copy-initialized from it - which is the base class subobject it
			// already holds, filled into the result object by the transfer
			// every other arm of a class-typed conditional makes below.
			// Writing a *second* object here instead would leave a class whose
			// bytes stand for it copied twice.
			moving = Derivation(read).base_value(moving, *base);
		}
		else
		{
			const SemaAnalyzer::Match match =
				read.match_argument(moving, value.type);
			if (match.viable)
			{
				read.apply_conversion(moving, value.type, match, ctx,
				                      TemporaryRequest::Written);
			}
		}
	}
	else
	{
		value.type = read.composite_pointer(left, right);
		if (value.type == kNoType)
		{
			throw std::runtime_error("the operands of ?: have no common type");
		}
		// 5.16p6 and 4.10p3: the operands are brought to that composite pointer
		// type, so the one that pointed at a derived class points at its own base
		// subobject - the same conversion 5.9p2 writes for a comparison of the
		// two, asked here of the same base-specifier's access.
		Derivation(read).convert_operand_to_base(left, value.type);
		Derivation(read).convert_operand_to_base(right, value.type);
	}
	if (value.category == ValueCategory::PRValue &&
	    types.is_class(types.strip_cv(value.type)))
	{
		// 5.16p3: the result object is one object however many operands could
		// have filled it, and each of them fills it where it stands.
		transfer_arm_to_result(left, value.type, ctx, left_temporaries);
		transfer_arm_to_result(right, value.type, ctx, right_temporaries);
	}
	// 12.2p3: what an arm created is destroyed where that arm ends, which is
	// the one path it was created on.
	end_arm_temporaries(left_temporaries, line, FactKind::Then, "then-ends");
	end_arm_temporaries(right_temporaries, line, FactKind::Else, "else-ends");
	value.spelled = value.type;
	value.node = &line;
	value.what = "conditional-expression";
	read.respell(value);
	return value;
}

void ConditionalReading::convert_arm_to_result(AnalyzedValue& arm,
                                               TypeId result,
                                               const SemaContext& ctx)
{
	if (arm.node == nullptr)
	{
		return;
	}
	SemaEntity* const base = Derivation(analyzer_).base_in(arm.type, result);
	if (base != nullptr)
	{
		arm = Derivation(analyzer_).base_value(arm, *base);
		return;
	}
	if (analyzer_.bare_type(arm.type) == analyzer_.bare_type(result))
	{
		return;
	}
	// Nothing related the two types, so what bound is a conversion function of
	// the operand's class handing back an lvalue - and the call to it is
	// written into the line the operand already wrote.
	const SemaAnalyzer::Match match = analyzer_.match_reference(
		arm, analyzer_.types_.reference_to(result, false));
	analyzer_.apply_conversion(arm, result, match, ctx, TemporaryRequest::Written);
}

// 5.16p3's last bullet, which is two rules and not one.
//
// Where both operands are of class type and one class is the other or a base of
// it, the conversion is a *slice* and is written one way only: the class the
// operand is converted to shall be the same as, or a base class of, its own,
// and shall be at least as cv-qualified - so a base operand does not reach a
// derived one however many constructors would carry it there.  Where anything
// else stands, the operand is asked for an ordinary implicit conversion to the
// type the other operand would have after 4.1, 4.2 and 4.3 - which is where a
// conversion function reaching a value, rather than a reference, comes in.
//
// The bullet is asked only where one of the two is of class type: two operands
// of other types are 5.16p5 and p6's arithmetic conversions and composite
// pointer type, which are read below and were read there before this bullet was
// written.
bool ConditionalReading::reaches_other(const AnalyzedValue& arm,
                                       const AnalyzedValue& other,
                                       TypeId& target)
{
	TypeTable& types = analyzer_.types_;
	const TypeId from = types.strip_cv(arm.type);
	const TypeId wanted = analyzer_.decayed(other);
	const TypeId to = types.strip_cv(wanted);
	if (arm.node == nullptr || (!types.is_class(from) && !types.is_class(to)))
	{
		return false;
	}
	if (types.is_class(from) && types.is_class(to) && from != to)
	{
		if (Derivation(analyzer_).base_in(from, to) != nullptr)
		{
			// 5.16p3: the cv-qualification of the type reached shall be the
			// same as, or greater than, the operand's own.
			if ((types.object_cv(arm.type) & ~types.object_cv(other.type)) != 0)
			{
				return false;
			}
			target = wanted;
			return true;
		}
		if (Derivation(analyzer_).base_in(to, from) != nullptr)
		{
			// The other direction of one derivation, which the clause does not
			// let a conversion carry.
			return false;
		}
	}
	if (!analyzer_.match_argument(arm, wanted).viable)
	{
		return false;
	}
	target = wanted;
	return true;
}

void ConditionalReading::transfer_arm_to_result(
	AnalyzedValue& arm, TypeId result, const SemaContext& ctx,
	std::vector<SemaEntity*>& frame)
{
	SemaAnalyzer& read = analyzer_;
	TypeTable& types = read.types_;
	if (arm.node == nullptr)
	{
		return;
	}
	if (arm.category == ValueCategory::PRValue)
	{
		// 12.8p31: an operand that is a prvalue creates its object where the
		// result stands, so what it made is the conditional's own object -
		// 12.2p3 ends that one where the full-expression ends and not this arm
		// as well.
		read.release_temporary(arm, frame);
		return;
	}
	if (types.bytes_stand_for_object(types.strip_cv(result)))
	{
		return;
	}
	const TypeId wanted = types.strip_cv(result);
	AnalyzedValue source = arm;
	if (source.category == ValueCategory::XValue &&
	    types.kind(types.strip_cv(source.spelled)) != TypeKind::RValueReference)
	{
		// 5.16p6: what stands between the arm and the result object is 4.1's
		// conversion, which *reads* the object the arm names - and reading an
		// object is not consuming it.  What the program wrote as an rvalue is
		// still one: a prvalue creates the result object outright and an
		// operand of rvalue reference type is an object the program already
		// said may be emptied.  5.2.5p4's subobject of a temporary is neither:
		// it is an xvalue because the object it is part of ends at the end of
		// the full-expression, which says when that object dies and not that
		// this arm is what empties it - so the transfer 13.3 chooses here reads
		// the lvalue the member access names.  What the program wrote is what
		// the *spelling* says, which is why the question is asked of it and not
		// of the category the operand ended up with.
		source.category = ValueCategory::LValue;
	}
	// The operand keeps the place it had among the conditional's children, so
	// the temporary is written around it rather than beside it, and the operand
	// becomes what constructs it.
	DumpNode& line = read.model_.wrap_node(*arm.node, std::string());
	source.node = line.children[0];
	line.children.clear();
	arm = read.build_temporary(wanted, line, nullptr, &source, ctx, "condobj",
	                           false, false);
}

void ConditionalReading::end_arm_temporaries(
	const std::vector<SemaEntity*>& frame, DumpNode& line, FactKind arm,
	const char* text)
{
	bool ends_in_something = false;
	for (std::size_t index = 0; index < frame.size(); ++index)
	{
		ends_in_something = ends_in_something ||
			(analyzer_.class_destructor(
				 analyzer_.types_.element_of(frame[index]->type)) != nullptr &&
			 analyzer_.declared_destruction(frame[index]->type));
	}
	if (!ends_in_something)
	{
		// 12.4p3: nothing the arm created comes to anything at its end, so the
		// arm needs no place to write one.
		return;
	}
	analyzer_.end_temporaries(frame, analyzer_.open_fact(line, text, arm));
}
