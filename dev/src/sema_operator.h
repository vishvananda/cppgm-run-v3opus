#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sema_declaration.h"
#include "sema_value.h"
#include "type_model.h"

class SemaAnalyzer;
struct DumpNode;
struct SemaEntity;

// 13.5 and 13.3.1.2: the declarations an *operator* written on an operand of
// class or enumeration type reaches, as the set 13.3 chooses from.
//
// 13.3.1.2p3 gathers that set from three places at once - the member operator
// functions a lookup in the left operand's class finds, the non-member ones an
// unqualified lookup and 3.4.2 reach, and, for a call written on an object,
// 13.3.1.1.2p2's surrogate call functions - and none of the three is any one
// expression's business.  The expression layer asks it for the call an operator
// expression stands for, and a fold of a constant expression asks it for the
// same call over the constants its operands came to; keeping the gathering here
// is what makes those one question and not two, exactly as `sema_argument_
// lookup.h` keeps 3.4.2's own walk and `sema_deduce.h` keeps 14.8.2's match.
class OperatorCall
{
public:
	explicit OperatorCall(SemaAnalyzer& analyzer);

	// 13.5: the operator an operator function overloads, as the source spelled
	// it - the name a declaration of one is bound to is `operator` and this,
	// closed up.  Null where the token is no operator a program may overload.
	static const char* spelling(unsigned token);
	// 13.5p1: whether an operator-function-id may be written with the operator
	// this name ends in, which is what tells one from the allocation functions
	// of 3.7.4 and the literal operators of 13.5.8, whose names begin alike.
	static bool overloadable(const std::string& name);
	// 13.5.3p1, 13.5.4p1, 13.5.5p1 and 13.5.6p1: the four operators a program
	// gives a meaning to only by declaring a non-static *member* function, so
	// the non-member half of 13.3.1.2p3's set is no part of what they reach.
	// It is a fact of the operator alone, which is why both readers ask it of
	// the one answer rather than each spelling it at every door it opens.
	static bool member_only(unsigned token);
	// 13.5.6p1: whether the arrow has already been written on an operand of
	// this type, which is what says the clause's process ends.  `stepped` is
	// the operand types one walk down a chain has been written on, and this
	// appends to it where the answer is no.
	static bool stepped_through(std::vector<TypeId>& stepped, TypeId operand);

	// 13.3.1.2p3 with 13.3.1.1.2p2: the declarations of the operator `token`
	// that these operands reach, appended to `out`.  Returns how many of them
	// stand alone - the single friend declarations 11.3p6 made and the
	// surrogates - which is what a ranking is told not to walk a chain from.
	//
	// `member_only` leaves the non-member half out, which is 13.3.1.2p3's own
	// reading for an operator a class alone may declare.  Every set gathered is
	// a local of the caller's, so an operator folded in a loop costs no vector
	// the model keeps.
	// `associated` takes how many of the entries 3.4.2 and 13.3.1.1.2p2
	// contributed, which are the trailing ones and the ones 14.6.4.2p1 asks no
	// question of.  Null where the caller does not draw that line.
	std::size_t candidates(unsigned token, const SemaContext& ctx,
	                       const std::vector<AnalyzedValue>& operands,
	                       bool member_only,
	                       std::vector<SemaEntity*>& out,
	                       std::size_t* associated = nullptr);

	// 13.3.1.2p1: the operator expression `line` holds the operands of, read as
	// the call of an operator function it stands for.  False - and nothing
	// written - where no operand has class or enumeration type, where the
	// candidate set is empty, or where 13.3 finds nothing in it viable, all of
	// which leave the built-in operator the caller's own to describe.
	bool expression(unsigned token, const SemaContext& ctx, DumpNode& line,
	                std::vector<AnalyzedValue>& operands, bool member_only,
	                AnalyzedValue& value);

	// 13.3.1.1.2p2: the surrogate call functions an object of `type` is called
	// through - one per non-explicit conversion function of its class that
	// yields a pointer to a function - which 13.3 ranks beside the class's own
	// `operator()` declarations over the one argument list.  The set is a fact
	// of the class and not of the call, so it is held per class: a class that
	// declares no such conversion - all but a few - answers with a list it
	// never builds a second time.
	const std::vector<SemaEntity*>& surrogates(TypeId type);

	// 13.5p6: an operator function is a non-static member function, or else a
	// non-member one taking at least one operand of class or enumeration type.
	// `member` is whether the declaration is written in a class and has no
	// object parameter, which is the static member neither half allows.
	void require_operand(const std::string& name, TypeId type, bool member);

private:
	OperatorCall(const OperatorCall&);
	OperatorCall& operator=(const OperatorCall&);

	SemaAnalyzer& analyzer_;
};
