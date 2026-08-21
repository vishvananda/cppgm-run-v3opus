#pragma once

#include <vector>

#include "sema_facts.h"
#include "sema_value.h"
#include "type_model.h"

class SemaAnalyzer;
struct AstNode;
struct DumpNode;
struct SemaContext;
struct SemaEntity;

// 5.16, the whole of it: what the result of a conditional-expression is.
//
// The operator is written as one expression and is three readings.  5.16p1
// converts the condition to `bool` and makes each of the other two operands a
// path of its own, so what one of them created is destroyed where that path
// ends and not where the full-expression does.  5.16p3 then asks the one
// question this clause exists for - whether either operand can be converted to
// match the other - and that is not a conversion the expression layer writes
// anywhere else: it is asked of *both* directions, it succeeds only where
// exactly one of them can be, and where the operand that matched is an lvalue
// the result is an lvalue naming what that operand named.  5.16p4 to p6 then
// say what a result no such match reached is: two operands of one type, the
// usual arithmetic conversions, or 5.9p2's composite pointer type.
//
// It is a reading of its own because those three are one walk of one node with
// state between them - the temporaries each arm opened, and which operand was
// the one converted - and because the conversion 5.16p3 applies is written back
// into the line the operand already wrote.
class ConditionalReading
{
public:
	explicit ConditionalReading(SemaAnalyzer& analyzer);

	// 5.16p1: the conditional-expression `node`, read into a line of its own
	// under `parent`.
	AnalyzedValue expression(const AstNode& node, const SemaContext& ctx,
	                         DumpNode& parent);

private:
	ConditionalReading(const ConditionalReading&);
	ConditionalReading& operator=(const ConditionalReading&);

	// 5.16p3: the operand that was converted to match the other by the first of
	// the clause's bullets - the object itself where the two named one type,
	// its base subobject where one class derives from the other, and the lvalue
	// a conversion function of its class handed back where nothing else
	// related them.
	void convert_arm_to_result(AnalyzedValue& arm, TypeId result,
	                           const SemaContext& ctx);
	// 5.16p3's last bullet: whether `arm` can be converted to match `other`
	// where neither of them bound a reference to the other and one of the two
	// is of class type.  `target` takes the type it would be converted to,
	// which is what the whole conditional is then worth.
	bool reaches_other(const AnalyzedValue& arm, const AnalyzedValue& other,
	                   TypeId& target);
	// 5.16p3: an operand of a conditional whose result is a prvalue of class
	// type, which copy-initializes the result object from that operand.
	void transfer_arm_to_result(AnalyzedValue& arm, TypeId result,
	                            const SemaContext& ctx,
	                            std::vector<SemaEntity*>& frame);
	// 12.2p3 and 5.16p1: what an arm created, ended under a node of the arm's
	// own, which is what lets the lowering write those ends at the end of the
	// block that arm is and on no other path out of the conditional.
	void end_arm_temporaries(const std::vector<SemaEntity*>& frame,
	                         DumpNode& line, FactKind arm, const char* text);

	SemaAnalyzer& analyzer_;
};
