#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_argument_lookup.h"
#include "sema_derivation.h"
#include "sema_operator.h"
#include "sema_pack.h"
#include "sema_reading.h"
#include "sema_scope.h"

namespace
{

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

// 9.3.2p1: the name the fold binds the object a member call was written on
// under.  It is the keyword itself, which is no identifier a program may write,
// so the binding stands in the fold's own region beside 8.3.5p10's places and
// collides with nothing the body declares.
const char kFoldThis[] = "this";

bool add_overflows(long long left, long long right, long long& out)
{
	const unsigned long long sum = static_cast<unsigned long long>(left) +
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(sum);
	return ((left ^ out) & (right ^ out)) < 0;
}

bool subtract_overflows(long long left, long long right, long long& out)
{
	const unsigned long long difference = static_cast<unsigned long long>(left) -
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(difference);
	return ((left ^ right) & (left ^ out)) < 0;
}

bool multiply_overflows(long long left, long long right, long long& out)
{
	out = static_cast<long long>(static_cast<unsigned long long>(left) *
	                             static_cast<unsigned long long>(right));
	if (left == 0 || right == 0)
	{
		out = 0;
		return false;
	}
	const long long lowest = -1 - 0x7FFFFFFFFFFFFFFFLL;
	if (left == -1)
	{
		return right == lowest;
	}
	if (right == -1)
	{
		return left == lowest;
	}
	return out / left != right;
}
// 8.3p1: the declarator-id a declarator was written around, which is the
// identifier under whatever ptr-operators and suffixes stand between - so
// `int x`, `const T& x` and `T (*x)[4]` are all read for the one name.  A
// nested declarator holds it, and 8.3.5p10's unnamed place holds none.
const AstNode* written_name(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::Identifier)
		{
			return &child;
		}
		if (child.kind == AstKind::NestedDeclarator && !child.children.empty())
		{
			const AstNode* const found = written_name(*child.children[0]);
			if (found != nullptr)
			{
				return found;
			}
		}
	}
	return nullptr;
}

// 8.3.5p10: the names the definition's own declarator gave the entries of its
// parameter-declaration-clause, in the order it wrote them, with `packed`
// saying which of them 14.5.3p4 wrote `pattern...`.  A name is no part of the
// function's type, so it is read from the definition that wrote the body rather
// than from any other declaration of the function; a place the definition left
// unnamed binds nothing, and the body cannot have named it either.
void parameter_names(const AstNode& definition, std::vector<std::string>& out,
                     std::vector<char>& packed)
{
	const AstNode* declarator = nullptr;
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		if (definition.children[index]->kind == AstKind::Declarator)
		{
			declarator = definition.children[index];
			break;
		}
	}
	if (declarator == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstNode& part = *declarator->children[index];
		if (part.kind != AstKind::ParameterClause)
		{
			continue;
		}
		for (std::size_t at = 0; at < part.children.size(); ++at)
		{
			const AstNode& place = *part.children[at];
			if (place.kind != AstKind::ParameterDeclaration)
			{
				return;
			}
			std::string named;
			bool dots = false;
			for (std::size_t which = 0; which < place.children.size(); ++which)
			{
				const AstNode& held = *place.children[which];
				if (held.kind != AstKind::Declarator)
				{
					continue;
				}
				const AstNode* const id = written_name(held);
				if (id != nullptr)
				{
					named = id->text;
				}
				// 14.5.3p4: the `...` stands in the declarator, between the
				// pattern's specifiers and the name the places are given.
				dots = dots ||
					child_kind(held, AstKind::ParameterPack) != nullptr;
			}
			out.push_back(named);
			packed.push_back(dots ? 1 : 0);
		}
		return;
	}
}

}

ConstexprReading::ConstexprReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

bool ConstexprReading::is_object(const SemaConstant& value) const
{
	return value.type != kNoType &&
		analyzer_.types_.is_class(analyzer_.types_.strip_cv(value.type));
}

// 5.19p2 with 3.9.1p8: whether a subobject of this type is one the constants
// hold at all.
//
// 8.3.4p6's element and 9.2p13's member ask it alike, and so does the member a
// constructor initializes - so it is one sentence and not one per walker.  The
// kinds are the two arithmetic ones, 7.2p5's enumeration, 5.19p2's address, and
// the class or array whose own interned list holds whatever the rest of them
// are; a pointer to member is the one that is left.
bool ConstexprReading::valued_subobject(TypeId type) const
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	return analyzer_.types_.is_class(bare) ||
		analyzer_.types_.kind(bare) == TypeKind::Array ||
		analyzer_.types_.kind(bare) == TypeKind::Pointer ||
		analyzer_.arithmetic_type(bare) != kNoType;
}

bool ConstexprReading::holds_list(const SemaConstant& value) const
{
	return is_object(value) ||
		(value.type != kNoType &&
		 analyzer_.types_.kind(analyzer_.types_.strip_cv(value.type)) ==
			 TypeKind::Array);
}

// The entry a constant interns as.  3.9.1p8's two kinds of arithmetic value are
// held differently and interned the same way: an entry is a type and a number,
// and for a floating value that number is the pool index the table gives it,
// because no `unsigned long long` holds an x87 `long double`.
TypeId ConstexprReading::entry_of(const SemaConstant& value) const
{
	const TypeId arithmetic = analyzer_.arithmetic_type(value.type);
	if (arithmetic != kNoType && analyzer_.types_.is_floating(arithmetic))
	{
		return analyzer_.types_.real_type(value.type, value.real);
	}
	return analyzer_.types_.value_type(value.type, value.bits);
}

SemaConstant ConstexprReading::constant_of(TypeId entry, const TypeTable& types)
{
	SemaConstant out;
	out.type = types.target(entry);
	if (types.is_floating(out.type))
	{
		out.real = types.value_real(entry);
		return out;
	}
	out.bits = types.value_bits(entry);
	return out;
}

// 14.7.1p1 and 3.9p5: a use of the class that requires it completely defined.
//
// 8.5.1p1's aggregate, 9.2p13's members and 12.1's constructors are each a fact
// the *definition* settles, and a specialization a spelling reached while 5.4p2's
// ambiguity was being probed was declared without anyone asking for one - so a
// reading that is about to ask any of those three asks for the definition first.
// The ask is what 14.7.1p1 makes it and no more: an instantiation already made
// is one `asked_specialization` returns from at once, so a clause read against a
// class of n members costs one probe and not n.
void ConstexprReading::ask_for_definition(TypeId bare)
{
	SemaEntity* const owner = analyzer_.model_.type_owner(bare);
	if (owner != nullptr && owner->primary != nullptr && !owner->defined)
	{
		analyzer_.asked_specialization(*owner);
	}
	analyzer_.require_complete_type(bare);
}


// 5.19p3: a const object of arithmetic type initialized by a constant
// expression *is* one, which is what an array bound and 14.3.2's argument may
// be written with.  8.5p16 and 8.5.4p3 make `T k(x)` and `T k{x}` initialize
// `k` with the very expression `T k = x` does, so the question is asked of the
// one clause they wrote rather than of the list that holds it - and 14.5.3p4
// makes which clause that is a question about the run a `pattern...` entry
// stands for rather than about the syntax.  An initializer that is an ordinary
// expression leaves the object one like any other, and one that is ill formed
// is still ill formed, so only the one failure is caught.
//
// The answer is whether this reading *covered* the declaration: false where the
// fold refused for a value kind or a construct it has none of, which is no
// statement about the program and which 7.1.5p9's requirement beside it then
// asks nothing of.
// 8.3.2p1: which object a declaration of reference type bound its name to.
//
// The initializer is read as 8.5's *operand* rather than for a value, because
// there need be none: `static int n;` is an object a reference binds to and
// holds nothing.  A refusal here says only that this reading does not know
// which object it is, which leaves the binding unmade and the name answered by
// whatever 3.6.2 then does with it.
void ConstexprReading::bind_declared_reference(SemaEntity& entity,
                                               const AstNode& wrote,
                                               TypeId type,
                                               const SemaContext& ctx)
{
	try
	{
		const SemaConstant bound =
			at_reference_place(operand_constant(wrote, ctx, type), type);
		if (bound.object != 0 && static_address(bound))
		{
			entity.address = bound.object;
		}
	}
	catch (const NotConstant&)
	{
		// 5.19p2 read one name further along: a reference this build cannot say
		// which object it names is one no reading of the name may answer from,
		// and the declaration carries that by binding nothing.
	}
}

bool ConstexprReading::fold_declared_object(SemaEntity& entity,
                                            const AstNode* initializer,
                                            TypeId type, const SemaContext& ctx,
                                            bool required)
{
	const AstNode* const wrote =
		initializer == nullptr || initializer->children.empty()
			? nullptr
			: initializer->children[0];
	const TypeId bare = analyzer_.types_.strip_cv(type);
	// 3.9p10 and 7.1.5p9: an object of literal *class* type is a constant of
	// the same standing - `constexpr Lit lit(42);` gives `lit.value` a value
	// 5.19 reads - and what it comes to is the interned list of what its
	// subobjects hold, which `ConstexprReading::object_of` builds.  3.9p10
	// makes an array of literal type a literal type too, and 8.3.4p6 makes its
	// elements subobjects of it exactly as a class's members are, so an array
	// is one of these and its list is what a subscript reads.
	const bool built = analyzer_.types_.is_class(bare) ||
		analyzer_.types_.kind(bare) == TypeKind::Array;
	// 8.3.4p1: a cv-qualified array is an array of cv-qualified *elements*, so
	// the `const` 5.19p3 asks about stands on the element type and the array
	// itself carries none.
	TypeId qualified = type;
	while (analyzer_.types_.kind(analyzer_.types_.strip_cv(qualified)) ==
	       TypeKind::Array)
	{
		qualified = analyzer_.types_.target(analyzer_.types_.strip_cv(qualified));
	}
	if (analyzer_.types_.is_reference(type))
	{
		// 8.3.2p1 and 5.19p2: a declaration of reference type binds its name to
		// an object some *other* declaration owns, so what the name is worth is
		// what that object holds and never a value of the reference's own -
		// which is the binding `SemaEntity::address` already carries for a
		// reference place a call filled, written here where a declaration wrote
		// one.  5.19p2 asks only that the object be one whose lifetime does not
		// end inside the evaluation, which is what `static_address` answers.
		if (wrote != nullptr)
		{
			bind_declared_reference(entity, *wrote, type, ctx);
		}
		return true;
	}
	// 3.9.2p1 and 5.19p2: a pointer is a constant of the same standing as an
	// arithmetic value, and what it is worth is the object it designates.
	const bool addressed =
		analyzer_.types_.kind(bare) == TypeKind::Pointer;
	if ((analyzer_.types_.cv(qualified) & kCvConst) == 0 ||
	    (!built && !addressed && analyzer_.arithmetic_type(type) == kNoType))
	{
		return true;
	}
	// 8.5p6: a declaration that wrote no initializer default-initializes the
	// object, which for one of class type calls its default constructor - so
	// such a declaration is initialized like any other and what it comes to is
	// that constructor's answer.  It is asked only where 12.1p5 makes that
	// constructor a constexpr one, because default-initialization of anything
	// else performs no initialization at all and leaves the object holding
	// nothing 5.19 reads.
	const TypeId element =
		analyzer_.types_.strip_cv(analyzer_.types_.element_of(bare));
	const SemaEntity* const built_by =
		wrote != nullptr || !analyzer_.types_.is_class(element)
			? nullptr
			: analyzer_.default_constructor(element);
	if (wrote == nullptr &&
	    (built_by == nullptr || !built_by->constexpr_function))
	{
		// 8.5p6 performs no initialization here, and that is an answer about
		// the declaration rather than the edge of this reading: the object
		// holds nothing, whoever asks.
		return true;
	}
	const unsigned stood = analyzer_.stood_in_;
	try
	{
		const std::vector<SemaConstant> none;
		// 12.6p1: default-initializing an array default-initializes each of its
		// elements, which is the same constructor once per element - and one
		// answer, because the elements are all the same object.
		const SemaConstant value = wrote != nullptr
			? initialized_value(*wrote, type, ctx)
			: (analyzer_.types_.kind(bare) == TypeKind::Array
				   ? array_of(bare, none)
				   : object_of(bare, none));
		if (analyzer_.checking_ > 0 && analyzer_.stood_in_ != stood)
		{
			// 14.6p8: the initializer names something an argument list has yet
			// to settle - `static constexpr unsigned n = sizeof...(T);` where
			// the pattern stands - so the reading stood a value in its place
			// and what it arrived at is not what this declaration is worth.
			// The declaration is left with no constant and the fact that this
			// reading ran out, which is what stands a value in for the *name*
			// wherever a later member of the pattern reads it.
			entity.constant = false;
			entity.covered_constant = false;
			return false;
		}
		if (!static_address(value))
		{
			// 5.19p2: an address constant expression designates an object with
			// static storage duration, and this one designates storage the
			// evaluation itself made - 12.2p1's temporary a reference bound to,
			// or a place a folded call filled - which is gone before the program
			// reads what was written here.
			throw NotConstant(entity.name +
			                  " is initialized with the address of an object "
			                  "whose lifetime ends inside the evaluation");
		}
		entity.value = value.bits;
		entity.real = value.real;
		entity.constant = true;
	}
	catch (const NotConstant& refused)
	{
		entity.constant = false;
		if (!refused.covered)
		{
			// The reading ran out rather than answering: `constexpr int n =
			// *(values + 1);` is a program both oracles fold and this build
			// holds no address for, so 7.1.5p9 has nothing to say about it and
			// 3.6.2p2's dynamic initialization is what the declaration gets.
			// The fact stands on the declaration, because a name reaching it
			// later finds no value for the same reason.
			entity.covered_constant = false;
			return false;
		}
		if (required && ConstexprRequirement(analyzer_).demanded(type, ctx))
		{
			// 7.1.5p9: the declaration asked for a constant expression, so why
			// this one is not is the diagnostic the declaration owes - and it is
			// the fold's own sentence rather than the requirement's summary of
			// it.
			throw;
		}
	}
	return true;
}

// 8.5: what the initializer `wrote` leaves an object of type `type` holding.
//
// It is one reading and not one per place a declaration may stand: 3.3.2 makes
// `constexpr P p = {1, 2};` at namespace scope and the same declaration written
// inside a constexpr body the same initialization, so the door the *statement*
// walk comes through asks this too.  Which of 8.5's initializations it is comes
// from the type and from the shape of the initializer together - 8.5.1p2's
// clauses down the object where a braced-init-list reached one of class or array
// type, 8.5p16's direct-initialization where parentheses or one expression did,
// and 4's conversion where the object is of arithmetic type.
SemaConstant ConstexprReading::initialized_value(const AstNode& wrote,
                                                 TypeId type,
                                                 const SemaContext& ctx)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	const bool built = analyzer_.types_.is_class(bare) ||
		analyzer_.types_.kind(bare) == TypeKind::Array;
	if (analyzer_.types_.kind(bare) == TypeKind::Array &&
	    wrote.kind != AstKind::ParenInitializer)
	{
		// 8.5.1p2 and 8.5.2p1: what initializes an array is its own list of
		// clauses or the string literal 8.5.2p1 gives it the code units of, and
		// a declaration's initializer is read for both exactly as a clause
		// written for an array subobject is - so `= "x"`, `= {"x"}` and
		// `= {1, 2}` come through the one reading.
		return clause_of(wrote, type, ctx);
	}
	if (built && wrote.kind == AstKind::BracedInitList)
	{
		// 8.5.1p2: the clauses reach the object's subobjects, and a clause
		// written as a list of its own reaches one whose type says how to
		// read it - so the whole list is one reading down the object rather
		// than a row of expressions this declaration then places.
		return clause_of(wrote, type, ctx);
	}
	// The list is read only where 5.19p3 asks, so a declaration that folds
	// nothing pays nothing for the clauses it wrote.  The node an entry
	// comes to is the arena's and the region it is read in is the model's,
	// so both outlive the walk that reads them out.
	//
	// Each clause is read as 8.5's *operand* and not as a value: 4.2p1's array,
	// 4.3p1's function and 8.3.2p1's reference each fill a place from the object
	// the initializer designates, and `static char buf[3];` is an object with no
	// value at all.  Which of the two the place wanted is settled below and by
	// `convert`, so the reading is the same whichever spelling of 8.5's
	// initializers wrote it.
	std::vector<SemaConstant> operands;
	// 8.5.4p7's clause and the region it was read in, kept for the one place
	// that clause has to be judged against: the type it initializes.
	const AstNode* narrowed = nullptr;
	SemaContext narrowed_in = ctx;
	if (wrote.kind == AstKind::ParenInitializer ||
	    wrote.kind == AstKind::BracedInitList)
	{
		InitializerClauses clauses(&wrote, analyzer_, ctx);
		if (clauses.list.unsettled())
		{
			// 14.6p8: a run no argument list has settled says neither how
			// many clauses there are nor what they are worth.
			throw NotConstant("a constant expression initializes an object "
			                  "from a run an argument list has yet to settle", false);
		}
		while (!clauses.spent())
		{
			const SemaContext inner = clauses.in(ctx);
			const AstNode& clause = clauses.next();
			++clauses.at;
			narrowed = &clause;
			narrowed_in = inner;
			operands.push_back(operand_constant(clause, inner));
		}
	}
	else
	{
		operands.push_back(operand_constant(wrote, ctx));
	}
	if (!built)
	{
		if (operands.empty() && wrote.kind == AstKind::BracedInitList &&
		    !analyzer_.types_.is_reference(bare))
		{
			// 8.5.4p3: an initializer-list with no elements value-initializes
			// the object, which 8.5p7 makes the zero of a scalar type - the same
			// value `int()` comes to.  8.5.3p5 leaves a reference out: there is
			// no temporary for an empty list to make of one.
			SemaConstant zero;
			zero.type = type;
			return zero;
		}
		if (operands.size() != 1)
		{
			throw NotConstant("a constant expression initializes " +
			                  analyzer_.types_.description(type) +
			                  " from more than one value");
		}
		if (narrowed != nullptr && wrote.kind == AstKind::BracedInitList)
		{
			// 8.5.4p7: a list-initialization makes no narrowing conversion, and
			// a clause the reading has a value for is judged by that value
			// rather than by the range of the type it was written with.  It is
			// the semantic layer's own walk of the clause, asked here because a
			// call written where 5.19 reads - inside a `static_assert`, inside a
			// template-argument-list - is a list no other reading ever analyzed.
			analyzer_.require_no_narrowing(*narrowed, argument_value(operands[0]),
			                               type, narrowed_in);
		}
		SemaConstant value = analyzer_.convert(operands[0], type);
		value.type = type;
		return value;
	}
	if (operands.size() == 1 &&
	    analyzer_.types_.strip_cv(operands[0].type) == bare)
	{
		// 8.5p14: the initializer is a prvalue of the object's own type, so
		// the object *is* that value and no constructor stands between.
		return operands[0];
	}
	if (analyzer_.types_.kind(bare) == TypeKind::Array)
	{
		return array_of(bare, operands);
	}
	if (operands.size() == 1 && wrote.kind != AstKind::ParenInitializer)
	{
		// 8.5p14 and 8.5p16: `T k = x` copy-initializes, which is the one
		// reading every other place of class type filled from a single value
		// asks - so 4.10p3's base class subobject and 13.3.1.4p1's conversion
		// function reach a declaration exactly as they reach an argument.
		// Parentheses are 8.5p16's *direct*-initialization instead: the
		// clauses are 13.3.1.3's arguments over the class's own constructors,
		// which is `object_of`'s question and not this one.
		return at_class_place(operands[0], bare);
	}
	return object_of(bare, operands);
}


SemaConstant ConstexprReading::called(const AstNode& callee,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& ctx)
{
	if (callee.kind == AstKind::MemberExpression)
	{
		// 5.2.2p1: the postfix-expression is 5.2.5p1's member access, which is
		// the one shape of a call that runs a body on an object.
		return member_called(callee, arguments, ctx);
	}
	if (callee.kind != AstKind::IdExpression)
	{
		// 13.3.1.2p1 with 13.5.4p1: the parentheses were written on something
		// that is no name - a temporary a functional cast built, the value
		// another call handed back - so what the call names is an `operator()`
		// of that value's class, chosen over the object and the arguments as
		// one operand list.
		const SemaConstant target = analyzer_.evaluate(callee, ctx);
		SemaEntity* const through = called_through(target);
		if (through != nullptr)
		{
			// 5.2.2p1: what stands before the parentheses designates a
			// function, so the call is a call of *that* declaration - which is
			// the same sentence `called_name` reads of a pointer or a
			// reference a name is worth, asked here of the value an expression
			// handed back.  `forward<F>(f)(args)` over `F = int (&)()` is the
			// shape a generic call wrapper writes and it names no function at
			// all.
			return call(*through, nullptr, arguments);
		}
		std::vector<SemaConstant> operands;
		operands.push_back(target);
		operands.insert(operands.end(), arguments.begin(), arguments.end());
		SemaConstant called;
		if (operator_constant(OP_LPAREN, operands, ctx, called))
		{
			return called;
		}
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate", false);
	}
	return called_name(callee.text, &callee, arguments, ctx);
}

SemaConstant ConstexprReading::called_name(
	const std::string& name, const AstNode* callee,
	const std::vector<SemaConstant>& arguments, const SemaContext& ctx)
{
	std::vector<AnalyzedValue> written;
	argument_values(arguments, written);
	std::vector<SemaEntity*> candidates;
	std::size_t singles = 0;
	std::size_t associated = 0;
	SemaEntity* const named =
		callee_candidates(name, callee, ctx, written, candidates, singles,
		                  associated);
	if (named == nullptr)
	{
		throw NotConstant(name +
		                  " is written where a constant expression calls and "
		                  "names no function");
	}
	if (named->kind != SemaKind::Function)
	{
		const SemaConstant held = entity_constant(*named, name);
		SemaEntity* const through = called_through(held);
		if (through != nullptr)
		{
			// 5.2.2p1: the postfix-expression is of pointer to function type, so
			// the call is of the function that pointer designates - and 8.3.2p1's
			// reference to function is the same call written without the `&`.
			return call(*through, nullptr, arguments);
		}
		// 13.5.4p1: what the parentheses were written on is an object, so the
		// call is a call of a member `operator()` of its class - which is a
		// member call on the object that name is worth and no further reading
		// of its own.
		return member_call(held, "operator()", arguments, ctx);
	}
	// 9.3.1p3: a call written with no object expression is one on the object the
	// function being read stands on - `f(args)` inside a member body is
	// `(*this).f(args)`, and 10.2 finds the declaration in a base of that
	// object's class exactly as a call written with the `.` does.  So the object
	// the fold bound the enclosing call to is 13.3.1p3's implicit object
	// argument here as it is there, and 13.3.1p4 makes that place an exact match
	// for every candidate that has no object parameter - so offering it decides
	// nothing between a member and the non-members beside it.
	SemaEntity* const bound = folded_this(ctx);
	SemaConstant object;
	if (bound != nullptr)
	{
		object = loaded(static_cast<std::uint32_t>(bound->value));
	}
	const AnalyzedValue self = object_value(object);
	SemaEntity& one = selected(name, candidates, written,
	                           bound == nullptr ? nullptr : &self, singles,
	                           associated);
	if (one.object_member && bound == nullptr)
	{
		// A fold that has no object cannot make such a call at all, which is
		// 5.19's answer about a body this reading is walking without one.
		throw NotConstant(name +
		                  " is called on an object a constant expression does "
		                  "not name", false);
	}
	return call(one, one.object_member ? &object : nullptr, arguments);
}

// 13.3.3.1: what a constant a fold holds is worth to a ranking of candidates.
//
// A constant is 5.19's *value*, and 3.10p1 makes a value a prvalue however the
// expression that reached it was written: the fold holds what the operand came
// to and no object the program named.  So a candidate declared over `T&` is one
// nothing here reaches and one declared over `T const&` is reached by every
// constant of its type - which is 8.5.3p5 as it stands for any other prvalue,
// and the whole of what tells `read(T&)` from `read(T const&)` apart.  The bits
// travel with the type because 4.10p1's null pointer constant is a fact of the
// *value* and 13.3.3.2p4 ranks by it.
void ConstexprReading::argument_values(
	const std::vector<SemaConstant>& arguments,
	std::vector<AnalyzedValue>& out) const
{
	out.reserve(arguments.size());
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		out.push_back(argument_value(arguments[index]));
	}
}

AnalyzedValue ConstexprReading::argument_value(const SemaConstant& value) const
{
	AnalyzedValue out;
	if (value.braced != nullptr)
	{
		// 13.3.3.1.5p1: the argument is the braced-init-list itself, which has
		// no type - so what it reaches a place through is a list-initialization
		// sequence, ranked by how many clauses it wrote.  That is the one fact
		// 13.3 needs of it, and it is the same value the expression layer builds
		// for such an argument.
		out.braced = value.braced;
		SemaContext where;
		where.scope = value.region;
		where.dump = value.region == nullptr ? nullptr : value.region->dump;
		out.clauses = InitializerClauses(value.braced, analyzer_, where).list.size();
		out.category = ValueCategory::PRValue;
		return out;
	}
	out.type = out.spelled = value.type;
	// 3.10p1: an operand this reading has no value of is one it read for the
	// object it designates alone, and that object is an lvalue - which is what
	// leaves `address_of(T &)` a candidate for a `static int n;` where every
	// constant beside it is 5.19's value and so a prvalue.
	out.category = value.valued ? ValueCategory::PRValue : ValueCategory::LValue;
	out.constant = value.valued;
	out.value = value.bits;
	out.real = value.real;
	out.null_constant = value.valued && value.bits == 0 && !holds_list(value) &&
		analyzer_.integral_type(value.type) != kNoType;
	return out;
}

// 13.3.1p3: the implicit object argument of a call written on a constant
// object, which 9.3.1p3 holds as a pointer to it.
//
// The cv-qualification the constant's own type carries is the one a candidate's
// object parameter has to accept, which is what leaves a member function that
// is not `const` no candidate for a call on a `constexpr` object - the fact the
// arity that used to rank these could not see.
AnalyzedValue ConstexprReading::object_value(const SemaConstant& object) const
{
	AnalyzedValue out;
	out.type = out.spelled = analyzer_.types_.pointer_to(object.type);
	out.category = ValueCategory::PRValue;
	// 8.3.5p1: a ref-qualifier binds by the category the object expression had,
	// and a constant object a declaration named is an lvalue.
	out.object_category = ValueCategory::LValue;
	out.nonnull = true;
	return out;
}

// 3.4, 3.4.2 and 14.2: the declarations the name of a callee reaches, as the
// set 13.3 chooses from.
//
// This is the lookup `call_expression` writes for a call the program's own
// semantics reads, asked here of a fold - a template-id names the
// specializations 14.8.1 makes of every template of that name rather than
// anything an ordinary lookup finds, and an unqualified name also names what
// 3.4.2p2's associated namespaces declare.  What a *fold* adds to that is
// nothing at all, which is the point: a call is one construct, and a constant
// expression is not a dialect of it.
SemaEntity* ConstexprReading::callee_candidates(
	const std::string& name, const AstNode* callee, const SemaContext& ctx,
	const std::vector<AnalyzedValue>& written,
	std::vector<SemaEntity*>& candidates, std::size_t& singles,
	std::size_t& associated)
{
	SemaEntity* named = nullptr;
	associated = 0;
	if (callee != nullptr &&
	    child_kind(*callee, AstKind::CarriedExpression) != nullptr)
	{
		// 7.1.6.2p1: the nested-name-specifier begins with a decltype-specifier,
		// so the region the name is looked up in is the one that type names.
		named = analyzer_.decltype_qualified_name(*callee, ctx, LookupKind::Any,
		                                          &candidates);
	}
	else
	{
		named = analyzer_.template_specializations(name, ctx, candidates);
	}
	if (named == nullptr)
	{
		candidates.clear();
		named = analyzer_.resolve(name, ctx, LookupKind::Any, &candidates);
	}
	if (named == nullptr)
	{
		// 1.4p8: the name is one the implementation reserves for a function of
		// its own, so what the program did not declare the implementation
		// declares here - the same one declaration the expression layer's own
		// lookup makes, and 3.4.3.2p1's `::__builtin_expect` among them, because
		// a fold that made a second declaration would rank a different set.
		named = analyzer_.reserved_function(name, &candidates);
	}
	if (named != nullptr && named->kind != SemaKind::Function)
	{
		// 13.5.4p1: the name reaches an object rather than a function, which is
		// a callee only its class's `operator()` answers.
		candidates.clear();
		return named;
	}
	if (named != nullptr && candidates.empty())
	{
		candidates.push_back(named);
	}
	// 3.4.2 appends after 3.4.1's half, so where the two meet is what the set
	// holds before the search below runs - and 14.6.4.2p1 asks its question of
	// the first half alone, exactly as the expression layer's own call does.
	const std::size_t reached = candidates.size();
	if (!QualifiedName(name).qualified() &&
	    ArgumentLookup(analyzer_).allowed(named))
	{
		// 3.4.2p1: an unqualified callee also names what the types of the
		// arguments reach, and p3 leaves that search out where the ordinary
		// lookup found a member, a block-scope declaration or a non-function.
		// A name the ordinary lookup found nothing of is one this search is the
		// whole of, which is what reaches a function declared beside its own
		// argument's class and nowhere else.
		singles = ArgumentLookup(analyzer_).candidates(name, written,
		                                               candidates);
	}
	associated = candidates.size() - reached;
	if (named == nullptr)
	{
		return candidates.empty() ? nullptr : candidates[0];
	}
	return named;
}

SemaEntity& ConstexprReading::selected(
	const std::string& name, const std::vector<SemaEntity*>& candidates,
	const std::vector<AnalyzedValue>& written, const AnalyzedValue* object,
	std::size_t singles, std::size_t associated)
{
	SemaEntity* one = nullptr;
	try
	{
		one = analyzer_.select_overload(candidates, written, name, object,
		                                false, singles, nullptr, nullptr,
		                                associated);
	}
	catch (const NotConstant&)
	{
		// A refusal the fold itself made while the ranking read an argument or
		// a default-argument keeps its own answer about whose error it is.
		throw;
	}
	catch (const std::runtime_error& refused)
	{
		// 13.3 refuses the *call*, and a fold is asked speculatively - a
		// template argument, a `static_assert`, an initializer the analysis
		// will read again as a dynamic one.  So a set with nothing viable in it
		// says here what every other unreadable operand says: this expression
		// is no constant expression.
		throw NotConstant(std::string(refused.what()));
	}
	if (one == nullptr)
	{
		throw NotConstant(name +
		                  " names no one constexpr function a constant "
		                  "expression may call with these arguments");
	}
	// 3.2p2 and 14.7.1p1: choosing a declaration is naming it, and what a fold
	// then reads is the *body* - which for a specialization is a body only this
	// asks the template for, and for a member of an instantiated class one the
	// instantiation put aside until a use arrived.  So a fold names what it
	// chose exactly as the expression layer does; 7.3.3p1's using-declaration
	// is followed to the base's own declaration there and here alike.
	return analyzer_.named_function(*one);
}

// 8.5p14 and 8.5p16: a constant read where a place of class type is
// initialized from it.
//
// 13.3 has already ranked the call this place belongs to, and what it ranked
// the argument by is the conversion sequence that reaches the place - so what
// is left here is to *perform* it, which for a place of class type is the
// constructor 13.3.1.3 chose called with the one value.  8.5p14 leaves a value
// already of the place's own class as it stands: the fold holds the object, so
// there is nothing for 12.8p31's copy to elide.  A place of no class type is
// answered by `convert` where its caller reaches it, and a reference place is
// the type it binds to.
SemaConstant ConstexprReading::at_class_place(const SemaConstant& value,
                                              TypeId place)
{
	TypeId bare = analyzer_.types_.strip_cv(place);
	if (analyzer_.types_.is_reference(bare))
	{
		bare = analyzer_.types_.strip_cv(analyzer_.types_.target(bare));
	}
	if (!analyzer_.types_.is_class(bare) ||
	    analyzer_.types_.strip_cv(value.type) == bare)
	{
		return value;
	}
	SemaEntity* const base = Derivation(analyzer_).base_in(value.type, bare);
	if (base != nullptr)
	{
		// 4.10p3 with 12.8p12: a value of a class derived from the place's is
		// copied from its own base class subobject, and 12.8's copy of a base
		// is that subobject as it stands - so no constructor of the derived
		// class is between them and the answer is the entry the object's list
		// already holds.
		return base_subobject(value, *base);
	}
	if (is_object(value) &&
	    analyzer_.converting_constructor(argument_value(value), bare) == nullptr)
	{
		// 13.3.1.4p1: the candidate set for a place of class type filled from a
		// value of another class is the converting constructors of the place's
		// class *and* the conversion functions of the value's, and
		// `match_by_value` asks the second exactly where no one of the first
		// takes the value.  A fold asks the same question in the same order:
		// 5.19p3's converted constant expression is one rule, and a place of
		// class type is one such place like the arithmetic and pointer places
		// beside it.
		return converted(value, bare, false);
	}
	const std::vector<SemaConstant> one(1, value);
	return object_of(bare, one);
}

// 13.3.1.2p2: an operator all of whose operands are of built-in type is the
// built-in operator and no lookup is done for it at all, so this one test is
// what keeps every arithmetic fold paying nothing for 13.3.1.2.
bool ConstexprReading::overloadable_operand(const SemaConstant& value) const
{
	return overloadable_place(value.type);
}

bool ConstexprReading::overloadable_place(TypeId type) const
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	return analyzer_.types_.is_class(bare) ||
		analyzer_.types_.kind(bare) == TypeKind::Enum;
}

// 13.3.1.2p3 asked of one operand, and asked only whether it reaches any
// declaration at all.
//
// 5.14p1 and 5.15p1 leave the right operand of `&&` and `||` unevaluated where
// the left one decides, and that is the *built-in* operator's rule alone: 13.5p4
// makes an overloaded one an ordinary call, and a call evaluates every argument.
// Which of the two stands here is a question about declarations rather than
// about values - so it is asked before the right operand is read, over the one
// operand a fold has read by then.  13.3.1.2p2's own test cannot be the one
// asked: it is over the types of *both* operands, and reading the second is
// exactly what 5.14p1 says not to do.  Three of 13.3.1.2p3's four sources are
// reached from the first operand alone - the lookup in its own class, the
// unqualified lookup, and 3.4.2's namespaces for its type - and where they hold
// nothing the built-in operator is what is left.  The fourth, 3.4.2's namespaces
// for the *right* operand's type, is the one a fold cannot ask, and is the gap
// the plan records.
bool ConstexprReading::reaches_operator(unsigned token,
                                        const SemaConstant& operand,
                                        const SemaContext& ctx)
{
	if (ctx.scope == nullptr)
	{
		return false;
	}
	const std::vector<SemaConstant> one(1, operand);
	std::vector<AnalyzedValue> written;
	argument_values(one, written);
	std::vector<SemaEntity*> candidates;
	OperatorCall(analyzer_).candidates(token, ctx, written,
	                                   OperatorCall::member_only(token),
	                                   candidates);
	return !candidates.empty();
}

// 13.3.1.2p1: the call an operator expression stands for, over the constants
// its operands came to.
//
// 13.3.1p3 and 13.3.1.2p4 put the first operand in two places at once: it is
// the implicit object argument of every member candidate and the first written
// argument of every non-member one, and 13.3 is offered it as both.  That is
// the whole of what this door does differently from `called_name`; the set, the
// ranking, the naming of what was chosen and the reading of its body are the
// call path's own.
bool ConstexprReading::operator_constant(unsigned token,
                                         const std::vector<SemaConstant>& operands,
                                         const SemaContext& ctx,
                                         SemaConstant& out)
{
	if (operands.empty() || ctx.scope == nullptr)
	{
		return false;
	}
	bool overloadable = false;
	for (std::size_t index = 0; !overloadable && index < operands.size();
	     ++index)
	{
		overloadable = overloadable_operand(operands[index]);
	}
	if (!overloadable)
	{
		return false;
	}
	std::vector<AnalyzedValue> written;
	argument_values(operands, written);
	std::vector<SemaEntity*> candidates;
	std::size_t associated = 0;
	const std::size_t singles =
		OperatorCall(analyzer_).candidates(token, ctx, written,
		                                   OperatorCall::member_only(token),
		                                   candidates, &associated);
	if (candidates.empty())
	{
		return false;
	}
	const AnalyzedValue object = object_value(operands[0]);
	const std::vector<AnalyzedValue> rest(written.begin() + 1, written.end());
	const std::string name =
		std::string("operator") + OperatorCall::spelling(token);
	SemaEntity* chosen = nullptr;
	try
	{
		// 13.3.1.2p4: the first operand stands in two places at once, so it is
		// handed over twice - as the object argument a member candidate binds,
		// and as the first written argument a non-member one takes.
		chosen = analyzer_.select_overload(candidates, rest, name, &object,
		                                   false, singles, &written[0], nullptr,
		                                   associated);
	}
	catch (const NotConstant&)
	{
		// As above: the fold's own refusal keeps the answer it came with.
		throw;
	}
	catch (const std::runtime_error& refused)
	{
		// 13.3 refuses the operator, which for an operand of class type leaves
		// the expression no meaning at all rather than leaving a built-in one.
		throw NotConstant(std::string(refused.what()));
	}
	if (chosen == nullptr || chosen->surrogate_for != nullptr)
	{
		// 13.3.1.2p2: nothing in the set is viable, so what is left is 13.6's
		// built-in operator - which the caller reads, through 12.3.2p1's
		// conversion function where an operand of class type needs one.
		// 13.3.1.1.2p2's surrogate stands for a call through a pointer to
		// function, which 5.19p2 holds no constant of.
		return false;
	}
	if (analyzer_.better_builtin(*chosen, object, written))
	{
		// 13.6 and 13.3.3p1: a built-in operator is a candidate of the same set
		// and reads these operands better than the declaration just chosen.
		return false;
	}
	// 7.3.3p1 and 14.7.1p1: what the operator calls is the declaration the
	// class made, reached through the using-declaration that named it, and the
	// body a fold reads is one `named_function` asks the template for.
	SemaEntity& run =
		analyzer_.named_function(SemaAnalyzer::declared_member(*chosen));
	const std::vector<SemaConstant> arguments(
		operands.begin() + (run.object_member ? 1 : 0), operands.end());
	out = call(run, run.object_member ? &operands[0] : nullptr, arguments);
	return true;
}

// 12.3.2p1 with 14.3.2p5: an object of class type brought to an arithmetic type.
//
// A conversion function is a member function of no parameters whose name is the
// type it converts to, so what the fold does is call it on the object - and
// *which* one it calls is 13.3.3.1.2's user-defined conversion sequence, the
// question `conversion_match` answers for every other reader of one.  A fold
// asks it there rather than ranking a set of its own, for the same reason
// `selected` hands a call's candidates to `select_overload`: a conversion is
// one construct, and a constant expression is not a dialect of it.
SemaConstant ConstexprReading::at_arithmetic_place(const SemaConstant& value,
                                                   TypeId place,
                                                   bool contextual)
{
	if (!value.valued)
	{
		// 8.5p11 and 4.1p1: the operand designates an object this reading holds
		// no value of, and every place here asks for one.  Whether that is 5.19's
		// own answer about the program - `static int n; enum { e = n };` is ill
		// formed - or this build running out is the object's own answer, carried
		// here because the refusal is the same one name further along.
		throw NotConstant("a constant expression reads an object it holds no "
		                  "value of where a value belongs",
		                  covered_object(value.object));
	}
	// A constant of class type is the identifier of an interned list and not a
	// number of the object's own width, so reading its bits where a number was
	// asked for is reading the identifier.  5.19p3 leaves a converted constant
	// expression its user-defined conversions, which is the one reading that
	// turns such a constant into one.
	return is_object(value) ? converted(value, place, contextual) : value;
}

SemaConstant ConstexprReading::converted(const SemaConstant& value,
                                         TypeId place, bool contextual)
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		throw NotConstant("a constant expression converts an object of a type "
		                  "it does not know", false);
	}
	// 13.3.1p3 and 9.3.1p3: the object the conversion function is called on,
	// which carries the constant's own cv-qualification because that is what
	// says whether a candidate's object parameter accepts it.  8.3.5p1's
	// ref-qualifier binds by the category the object expression had, and a
	// constant a declaration named is an lvalue.
	AnalyzedValue object = argument_value(value);
	object.category = ValueCategory::LValue;
	// 13.6 and 6.4.2p2: a place that named no type asks only that the class
	// reach *a* value an arithmetic reading can take, which is the question the
	// built-in operators ask of an operand of class type - and a class that
	// reaches two of them reaches no one of them.
	const TypeId wanted = place != kNoType
		? place
		: analyzer_.builtin_conversion_type(object);
	const SemaAnalyzer::Match match = wanted == kNoType
		? SemaAnalyzer::Match()
		: analyzer_.conversion_match(object, wanted, contextual);
	if (!match.viable || match.converted == nullptr)
	{
		// 13.3.3p1: no conversion function of the class is viable for this
		// place, or two are and neither is better - which is no user-defined
		// conversion sequence at all and so no value here.
		throw NotConstant(analyzer_.types_.description(value.type) +
		                  " declares no one conversion function a constant "
		                  "expression reaches this place through");
	}
	// 3.2p2 and 8.4.3p2: choosing a declaration is naming it, which is what
	// asks an instantiation for the body the fold is about to read and what
	// refuses a deleted one.  Whether the declaration chosen is a constexpr
	// function this unit defined is `call`'s answer and no part of the choice:
	// a set ranked with the others left out would hand back the value of a
	// conversion the program does not perform.
	SemaEntity& one = analyzer_.named_function(*match.converted);
	const std::vector<SemaConstant> none;
	return call(one, &value, none);
}

SemaEntity& ConstexprReading::bind_constant(const std::string& name,
                                            const SemaConstant& value,
                                            const SemaContext& inner,
                                            bool written)
{
	SemaEntity& bound =
		analyzer_.model_.create(SemaKind::Variable, name, value.type);
	bound.constant = value.valued;
	bound.fold_local = written;
	bound.value = value.bits;
	bound.real = value.real;
	if (analyzer_.types_.is_reference(value.type))
	{
		// 8.3.2p1: a place declared of reference type names the object the
		// argument designated, so the binding carries that object rather than a
		// value - and every reading of the name goes through it.
		bound.address = static_cast<std::uint32_t>(value.bits);
	}
	bound.region = inner.scope;
	analyzer_.model_.bind(*inner.scope, name, bound);
	analyzer_.model_.declare_in(*inner.scope, bound);
	return bound;
}

// 8.3.5p10 with 14.5.3p4: the names of the places `callee`'s declarator opened,
// one per place of the function's type.
//
// The declarator wrote the *entries* of a parameter-declaration-clause, and an
// entry written `pattern... name` is not one place but as many as the run its
// packs were bound to holds.  What says how many is the type: 14.8.2 settled
// the run when it made this declaration, and the places the type has past the
// ones the other entries wrote are the run's.  8.3.5p10 names them after the
// pack - `pack_element_name`'s spellings, whose first is the pack's own name -
// which is exactly what a `name...` written in the body looks up, so the
// bindings a fold makes for them are found by 14.5.3p4's own reading.
// `runs` takes that length at the place a run begins and zero everywhere else.
void ConstexprReading::declared_places(SemaEntity& callee,
                                       std::vector<std::string>& out,
                                       std::vector<unsigned>& runs,
                                       std::string& empty) const
{
	if (callee.constexpr_body == nullptr)
	{
		return;
	}
	std::vector<std::string> entries;
	std::vector<char> packed;
	parameter_names(*callee.constexpr_body, entries, packed);
	const std::size_t implicit = callee.object_member ? 1u : 0u;
	const std::size_t held = analyzer_.types_.parameters(callee.type).size();
	const std::size_t places = held > implicit ? held - implicit : 0;
	std::size_t fixed = 0;
	for (std::size_t index = 0; index < packed.size(); ++index)
	{
		fixed += packed[index] != 0 ? 0u : 1u;
	}
	// 14.5.3p4: a pack expanded into no place at all is still declared, so the
	// run is what the type has over the entries that are one place each and
	// never a negative count.
	const std::size_t run = places > fixed ? places - fixed : 0;
	for (std::size_t index = 0; index < entries.size(); ++index)
	{
		if (packed[index] == 0)
		{
			out.push_back(entries[index]);
			runs.push_back(0);
			continue;
		}
		if (run == 0)
		{
			// 14.5.3p4: the run holds no element, so the expansion made no
			// place at all - and the pack is still declared, because
			// `sizeof...` and a `name...` written in the body are asked about
			// it and have to come to nothing rather than to nothing found.
			empty = entries[index];
		}
		for (std::size_t element = 0; element < run; ++element)
		{
			out.push_back(pack_element_name(entries[index], element));
			runs.push_back(element == 0 ? static_cast<unsigned>(run) : 0u);
		}
	}
}

// 9.2p1 and 10.2: the subobjects of the object a call was written on, bound
// under the names their declarations gave them.
//
// The class's own members are bound first and each base subobject walked after
// them, so a member of the derived class hides one of that name in a base - and
// a base subobject binds nothing itself, because 10p1 gives it no name at all.
void ConstexprReading::bind_subobjects(const SemaConstant& object,
                                       const SemaContext& inner)
{
	std::vector<Subobject> members;
	subobjects(object.type, members);
	std::vector<SemaConstant> held;
	held.reserve(members.size());
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		SemaConstant value;
		bool holds = true;
		try
		{
			value = subobject_value(object, index);
		}
		catch (const NotConstant&)
		{
			// A body that names no such member reads nothing of it, so an entry
			// the list does not hold is left to the name that asks for one
			// rather than refused here.  9.5p1 is why it is this member and not
			// every member after it: a union holds one at a time, so the one
			// whose lifetime an initialization began may stand anywhere in the
			// class's own order and the ones beside it hold nothing.
			holds = false;
		}
		held.push_back(value);
		if (!holds || members[index].base ||
		    inner.scope->names.count(members[index].entity->name) != 0)
		{
			continue;
		}
		// 5.19p2: the object the call was written on is one whose lifetime
		// began before this evaluation, so its subobjects are read here and are
		// no binding the body may write.
		SemaEntity& bound =
			bind_constant(members[index].entity->name, value, inner, false);
		if (value.object != 0)
		{
			// 9.2p1 with 3.10p2: a member named with no object expression names
			// the subobject of *that* object, so the binding designates it -
			// which is what `&value` and `return elems;` inside the body then
			// hand back.
			bound.address = value.object;
		}
	}
	for (std::size_t index = 0; index < held.size(); ++index)
	{
		if (members[index].base)
		{
			bind_subobjects(held[index], inner);
		}
	}
}

// 9.3.2p1 and 9.2p1: the binding the innermost folded call made for the object
// it was called on, or null where this reading has no such object.
//
// 8.3.5p10 puts the keyword's own name in a member function's declarator region
// too - the analysis declares `this` there as the parameter 9.3.1p3 makes it -
// so a lookup of the name alone finds that declaration wherever the fold made
// no binding of its own: inside a constructor, whose object is the one being
// built and is designated by nothing yet, and inside any member body read for a
// use rather than for a call.  What tells them apart is which object the
// binding names, because the fold writes one and the declaration names none.
SemaEntity* ConstexprReading::folded_this(const SemaContext& ctx) const
{
	SemaEntity* const bound =
		ctx.scope == nullptr
			? nullptr
			: analyzer_.model_.lookup(*ctx.scope, kFoldThis, LookupKind::Any);
	return bound != nullptr && bound->value != 0 ? bound : nullptr;
}

// 9.3.2p1: the object the innermost folded call was written on, as the prvalue
// of pointer type the keyword is.
SemaConstant ConstexprReading::this_constant(const SemaContext& ctx)
{
	SemaEntity* const bound = folded_this(ctx);
	if (bound == nullptr)
	{
		// 9.3.2p1: `this` may be written only in a non-static member function,
		// and a fold reaches one only by having been given the object - so a
		// body this reading is walking without one is 5.19's answer about the
		// program exactly as the expression layer's refusal is.
		throw NotConstant("a constant expression writes `this` outside a member "
		                  "function called on an object it holds");
	}
	return pointer_constant(static_cast<std::uint32_t>(bound->value), kNoType);
}

void ConstexprReading::bind_arguments(SemaEntity& callee,
                                      const SemaConstant* object,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& inner)
{
	if (object != nullptr)
	{
		// 9.3.1p3 and 9.2p1: what a member named with no object expression
		// denotes is the object the function was called on, so the object's
		// own subobjects are bindings of this region - one constant each,
		// under the name the member was declared with.
		bind_subobjects(*object, inner);
		if (object->object != 0)
		{
			// 9.3.2p1: `this` is a prvalue of pointer type whose value is the
			// address of that object.  The keyword is no identifier a program
			// may write, so the binding stands beside the places 8.3.5p10 named
			// and is what the fold's reading of the keyword then reaches.
			bind_constant(kFoldThis, pointer_constant(object->object, kNoType),
			              inner, false);
		}
	}
	// 8.3.5p10 and 5.2.2p4: the places the declarator wrote, each bound to what
	// the argument written for it came to after 8.5's conversion to its type.
	std::vector<std::string> places;
	std::vector<unsigned> runs;
	std::string empty_run;
	declared_places(callee, places, runs, empty_run);
	if (!empty_run.empty())
	{
		// 14.5.3p4: the pack the expansion was over is bound to a run of no
		// elements, which is a declaration and no place - the one the reading
		// of a `pattern...` in the body finds to learn it stands for nothing.
		SemaEntity& none = analyzer_.model_.create(
			SemaKind::Typedef, empty_run,
			analyzer_.types_.pack_type(std::vector<TypeId>()));
		analyzer_.model_.bind(*inner.scope, none.name, none);
	}
	// 14.5.3p4: the run one expansion of a function parameter pack was bound to
	// is counted on the first of the places it made, and every place after it
	// carries that first one back - which is what a `pattern...` written in the
	// body reads to learn how many readings it stands for.  The bindings the
	// fold makes stand in for those places, so they carry the same facts.
	SemaEntity* run_of = nullptr;
	unsigned run_left = 0;
	for (std::size_t index = 0;
	     index < arguments.size() && index < places.size(); ++index)
	{
		const std::string& name = places[index];
		if (name.empty())
		{
			// 8.3.5p10: 3.3.4 ends a declaration's parameter names at its own
			// declarator, so a place the definition left unnamed binds nothing
			// and the body cannot have named it either.
			run_left = run_left != 0 ? run_left - 1 : 0;
			continue;
		}
		// 5.19p2 with 12.2p1: a place the call filled is an object created by
		// this evaluation, which is the one standing an assignment inside the
		// body may write.
		SemaEntity& bound =
			bind_constant(name, arguments[index], inner, true);
		bound.pack_run = runs[index];
		if (bound.pack_run != 0)
		{
			run_of = &bound;
			run_left = bound.pack_run - 1;
		}
		else if (run_left != 0)
		{
			bound.pack_element_of = run_of;
			--run_left;
		}
	}
}

// 5.2.2p4 with 8.3.6p1: the list the places of `callee` are filled with.
//
// 8.3.5p10 converts each argument to the type of the place it reached before
// the body reads it, which is what makes the fold a fact of the converted list
// and not of the spellings that wrote it - and 8.3.6p1 reads a call that stops
// short of a place as if the default-argument stood where the argument is
// missing, so those values are part of that same list.  8.3.6p9 leaves such an
// expression to the region the declaration that introduced it was written in,
// which is where it is read here.
std::uint32_t ConstexprReading::passed_arguments(
	SemaEntity& callee, const SemaConstant* object,
	const std::vector<SemaConstant>& arguments,
	std::vector<SemaConstant>& passed)
{
	const std::vector<TypeId>& places = analyzer_.types_.parameters(callee.type);
	const std::size_t implicit = callee.object_member ? 1 : 0;
	const std::unordered_map<std::uint32_t,
	                         std::vector<ParameterRecord> >::const_iterator
		wrote = analyzer_.defaults_.find(
			SemaAnalyzer::wrote_defaults(callee).id);
	passed.reserve(places.size());
	std::vector<TypeId> key;
	key.reserve(places.size() + 1);
	key.push_back(object == nullptr ? kNoType : entry_of(*object));
	// 3.10p2 and 5.19p2: *which* object the call was written on, beside what
	// that object is worth.  Two objects of one class holding one value are two
	// objects, and a body that hands back the address of a member of the one it
	// was called on hands back two different addresses - so a fold of the first
	// is no answer for the second.
	key.push_back(analyzer_.types_.value_type(
		analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT),
		object == nullptr ? 0 : object->object));
	for (std::size_t at = implicit; at < places.size(); ++at)
	{
		const std::size_t index = at - implicit;
		SemaConstant given;
		if (index < arguments.size())
		{
			given = arguments[index];
		}
		else
		{
			const AstNode* const written =
				wrote == analyzer_.defaults_.end() || at >= wrote->second.size()
					? nullptr
					: wrote->second[at].initializer.written;
			SemaContext where;
			where.scope = written == nullptr
				? nullptr : wrote->second[at].initializer.scope;
			if (written == nullptr || where.scope == nullptr ||
			    written->children.empty() ||
			    written->children[0]->children.empty())
			{
				throw NotConstant(callee.name +
				                  " is called with no argument for a place its "
				                  "declaration gives no default-argument");
			}
			where.dump = where.scope->dump;
			given = operand_constant(*written->children[0]->children[0],
			                         where, places[at]);
		}
		if (given.braced != nullptr)
		{
			// 8.5.4p1 with 13.3.3.1.5: the place is known now, so the list is
			// read for it - 8.5.3p5 list-initializes a temporary of the referred
			// type where the place is a reference and the place itself
			// otherwise, which is the one reading a declaration of that type
			// initialized from the same list would get.
			SemaContext where;
			where.scope = given.region;
			where.dump = given.region == nullptr ? nullptr : given.region->dump;
			const TypeId wanted = analyzer_.types_.is_reference(places[at])
				? analyzer_.types_.target(places[at])
				: places[at];
			given = initialized_value(*given.braced, wanted, where);
			given.type = wanted;
			passed.push_back(given);
			key.push_back(entry_of(given));
			continue;
		}
		if (analyzer_.types_.is_reference(places[at]))
		{
			// 8.3.2p1 and 8.5.3: the place binds to the object the argument
			// designates rather than taking a copy of what it is worth, which is
			// what makes `&value` inside the body the address of the *argument*.
			// The binding is part of the key, so a call on one object and a call
			// on another holding the same value are two folds and not one.
			given = at_reference_place(given, places[at]);
		}
		else if (analyzer_.arithmetic_type(places[at]) != kNoType ||
		         analyzer_.types_.kind(
			         analyzer_.types_.strip_cv(places[at])) == TypeKind::Pointer)
		{
			given = analyzer_.convert(given, places[at]);
			given.type = places[at];
		}
		else
		{
			// 5.2.2p4: the argument initializes the place, so a value of
			// another type reaching one of class type is the constructor 13.3
			// chose when it ranked this call - which is what makes `C(1) + 2`
			// read `b.n` on an object and not on the integer that was written.
			given = at_class_place(given, places[at]);
		}
		passed.push_back(given);
		key.push_back(entry_of(given));
	}
	// 5.2.2p7: an argument the ellipsis matched fills no place, and what it is
	// worth is still what tells one fold of this callee from another.
	for (std::size_t index = passed.size(); index < arguments.size(); ++index)
	{
		passed.push_back(arguments[index]);
		key.push_back(entry_of(arguments[index]));
	}
	return analyzer_.types_.type_list(key);
}

// 14p1 and 14.6p8: which declarations a reading of a pattern holds no
// definition of, however the program wrote them.
//
// Two shapes reach the fold, and both are one sentence: a template declares no
// function until an argument list is given for it.  A member of the class a
// pattern declares stands in a region a head bound its own parameters in, and
// `dependent_reading` is that walk; a specialization of a function template
// named with an argument that depends on a parameter is a declaration made for
// an argument list that says nothing yet, and its own arguments are where that
// shows.  Neither is a body this unit is missing - the instantiation reads the
// same call again, and that reading is where 7.1.5p2 is answered.
bool ConstexprReading::unsettled_callee(const SemaEntity& callee) const
{
	if (callee.region != nullptr && analyzer_.dependent_reading(*callee.region))
	{
		return true;
	}
	if (callee.primary == nullptr)
	{
		return false;
	}
	if (analyzer_.checking_ > 0 && callee.primary->templated == nullptr)
	{
		// The third shape, and the same sentence a third time: a member
		// template of the class a pattern declares carries no pattern of its
		// own while that pattern is being read, so this reading has no body for
		// it however plainly the argument list is written - `enabled<int>()`
		// folded inside the class template that declares `enabled`.  The
		// instantiation records the pattern and reads the call again.
		return true;
	}
	if (callee.template_arguments == 0)
	{
		return false;
	}
	const std::vector<TypeId>& arguments =
		analyzer_.types_.type_list_at(callee.template_arguments);
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (analyzer_.types_.is_dependent(arguments[index]))
		{
			return true;
		}
	}
	return false;
}

SemaConstant ConstexprReading::call(SemaEntity& callee,
                                    const SemaConstant* object,
                                    const std::vector<SemaConstant>& arguments)
{
	if (callee.builtin != kNotBuiltin)
	{
		// 1.4p8: the callee is one of the functions the implementation provides,
		// so no definition of it is anything the program wrote and 7.1.5p2's
		// question about `constexpr` is not the one to ask.  What such a call is
		// worth is what the implementation says it is - and only the branch hint
		// says anything: it hands back its first operand, already brought to the
		// parameter's type by the same conversion an ordinary call's argument
		// takes.  Every other one of them names storage or ends the program, and
		// none of those is a value 5.19 reads.
		std::vector<SemaConstant> given;
		passed_arguments(callee, object, arguments, given);
		if (callee.builtin == kBuiltinExpect && !given.empty())
		{
			return given[0];
		}
		throw NotConstant(callee.name +
		                  " is not a function a constant expression evaluates",
		                  false);
	}
	// 7.1.5p2: `constexpr` stands on the *template's* declarator and every
	// specialization of it is a constexpr function, whatever the argument
	// list - except 14.7.3p1's, which the program wrote out as a declaration
	// of its own and which says what its own decl-specifiers say.  A
	// specialization this reading made carries no definition to have read the
	// flag off yet, so the template's is what answers for it, and an
	// instantiation writes it again where it reads the definition.
	const bool constexpr_declared = callee.constexpr_function ||
		(callee.primary != nullptr && !callee.explicit_specialization &&
		 callee.primary->constexpr_function);
	// 14.7.1p1: reading what this call comes to is a context that *requires the
	// function definition to exist*, whichever operand the naming that chose it
	// stood in - 5p8 leaves the operand unevaluated and says nothing about the
	// constant expressions written inside it, so `decltype(box<width<int>()>())`
	// and `noexcept(taking<width<int>()>())` name `width<int>` where nothing is
	// evaluated and read its value all the same.  3.2p2 is the other sentence
	// and answers about the *symbol*: the operand odr-uses nothing, so this
	// demand is the fold's alone and asks for no definition of the unit.
	// 14.6p8's dialect declares nothing and asks for nothing, and `instantiate`
	// is what says a specialization no argument list has settled has no body to
	// read yet.
	if (constexpr_declared && callee.constexpr_body == nullptr &&
	    callee.primary != nullptr && analyzer_.checking_ == 0)
	{
		analyzer_.instantiate(callee);
	}
	if (!callee.constexpr_function || callee.constexpr_body == nullptr ||
	    callee.constexpr_region == nullptr)
	{
		if (constexpr_declared && analyzer_.templating() &&
		    unsettled_callee(callee))
		{
			// 14p1 and 14.6p8: a template declares no function until it is
			// instantiated, so a reading of the *pattern* holds the definition
			// of nothing the pattern declares - `check_template_definition`
			// reads the body against types it has none of yet and records
			// none.  What a call of one comes to is therefore the arguments'
			// to say, exactly as the size of a dependent type is, and the
			// reading stands one value in its place rather than calling this
			// the program's error.  The instantiation reads the same call
			// again with the arguments bound, which is where it is answered.
			//
			// A pattern's reading is not the only place such a callee is met:
			// a template *head* read in earnest declares places of its own, and
			// a default argument written over them - `enable_if_t<ok<U>()>` -
			// names a specialization over a `U` no list has settled while the
			// dialect is a lowering and `checking_` is zero.  What the call
			// comes to there is the same arguments' to say.
			++analyzer_.stood_in_;
			SemaConstant stood;
			stood.type = analyzer_.types_.fundamental(FT_INT);
			stood.bits = 1;
			return stood;
		}
		// 7.1.5p2: a call of a function no declaration wrote `constexpr` on is
		// 5.19's own answer about the program; one of a constexpr function
		// whose definition this reading has not got is where the walk stops,
		// which 14.7.1p1's queued member definition is the standing example of.
		throw NotConstant(callee.name +
		                  " is not a constexpr function this unit has defined",
		                  constexpr_declared == false);
	}
	const TypeId result = analyzer_.types_.target(callee.type);
	std::vector<SemaConstant> passed;
	const std::uint32_t list = passed_arguments(callee, object, arguments, passed);
	const TypeId held = analyzer_.model_.folded_call(callee, list);
	if (held != kNoType)
	{
		return returned_constant(constant_of(held, analyzer_.types_));
	}
	unsigned& depth = analyzer_.model_.folding_depth();
	if (depth >= kMaxConstexprDepth)
	{
		throw NotConstant("a constant expression calls constexpr functions "
		                  "more deeply than this implementation reads", false);
	}
	const ReadingDepth folding(depth);
	// 14.6.1p1 and 3.3.3: a region of the fold's own, so the bindings one call
	// makes are not the ones another sees and the body's own names are looked
	// up over the region its declarator opened.
	SemaContext inner;
	inner.scope = &analyzer_.model_.open(ScopeKind::Block,
	                                     *callee.constexpr_region, nullptr,
	                                     callee.constexpr_region->dump);
	inner.dump = callee.constexpr_region->dump;
	bind_arguments(callee, object, passed, inner);
	const AstNode& body = *callee.constexpr_body;
	const AstNode* compound = nullptr;
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		if (body.children[index]->kind == AstKind::CompoundStatement)
		{
			compound = body.children[index];
			break;
		}
	}
	if (compound == nullptr)
	{
		throw NotConstant(callee.name +
		                  " has no function-body a constant expression reads", false);
	}
	// 6.1-6.6: the body is run rather than pattern-matched.  7.1.5p3 leaves a
	// C++11 constexpr body one `return` statement, and this milestone's own
	// boundary allows the evaluation a strict superset of that - so what says
	// the call is not a constant expression is a statement the walk cannot run
	// or a value it cannot read, not the shape of the body.
	ConstexprFrame frame;
	// 6.6.3p2: the place a `return` statement's operand fills, which is what
	// 8.5.4p1 makes a braced-init-list written there the initialization of.
	frame.returns = result;
	statement(*compound, inner, frame);
	if (!frame.returned)
	{
		throw NotConstant(callee.name +
		                  " is a constexpr function whose body reaches no "
		                  "return statement 6.6.3p2 gives the call a value by");
	}
	SemaConstant answer = frame.result;
	if (analyzer_.types_.is_reference(result))
	{
		// 8.3.2p1 and 6.6.3p2: a function whose return type is a reference hands
		// back the *object* the return statement designated and no copy of what
		// it holds - so the answer is that object, which is what makes
		// `&values[0] == values.data()` one address and not two.  The object is
		// what the fold is held under too, so a second naming of the call reads
		// the same object back out of the memo.
		answer = at_reference_place(answer, result);
	}
	else if (analyzer_.arithmetic_type(result) != kNoType ||
	         analyzer_.types_.kind(analyzer_.types_.strip_cv(result)) ==
		         TypeKind::Pointer)
	{
		// 6.6.3p2: the value the return statement's expression is converted to
		// the return type, which is what the caller reads - and 4.2p1's decay of
		// an array the body named is one of those conversions.
		answer = analyzer_.convert(answer, result);
		answer.type = result;
	}
	else
	{
		// 6.6.3p2 again, where the return type is a class: `return token();`
		// out of a function returning `property` is the copy-initialization
		// 8.5p16 makes it, so what the caller reads is an object of the
		// *declared* type and not of the one the expression was written with.
		answer = at_class_place(answer, result);
	}
	analyzer_.model_.hold_folded_call(callee, list, entry_of(answer));
	return returned_constant(answer);
}

// 5.2.9p2, 5.2.9p4 and 8.5: `value` read where the place named is of reference
// or class type, which is the reading a cast written to one asks and the
// reading an initialization of one asks alike.  A reference place binds to the
// object rather than taking a copy, so what the expression is then worth is
// that object read back - which is what carries `static_cast<X&&>(x)` on to the
// conversion function of the object `x` names.
SemaConstant ConstexprReading::at_object_place(const SemaConstant& value,
                                               TypeId place)
{
	return analyzer_.types_.is_reference(analyzer_.types_.strip_cv(place))
		? returned_constant(at_reference_place(value, place))
		: at_class_place(value, place);
}

// 8.3.2p1 and 5.2.2p10: what the caller reads of a call whose answer the memo
// holds, which for a reference return type is the object that answer designates
// - the value it holds, carrying the object it came out of, so that `&f()` and
// `f().m` each reach the storage the callee named.
SemaConstant ConstexprReading::returned_constant(const SemaConstant& answer)
{
	return analyzer_.types_.is_reference(answer.type)
		? held_at(static_cast<std::uint32_t>(answer.bits))
		: answer;
}

SemaConstant ConstexprReading::id_constant(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 7.1.6.2p1: a nested-name-specifier that begins with a decltype-specifier
	// reaches its region through the expression the parser kept beside the
	// name, which no spelling holds - so 5.19's reading asks the same question
	// of an id-expression that every other reader of one asks.  14.2 is the
	// other half of that question and `folded_name` is where both are asked:
	// `f<int>` names a specialization ordinary lookup finds no declaration of.
	SemaEntity* const named =
		child_kind(node, AstKind::CarriedExpression) == nullptr
		? analyzer_.folded_name(node.text, ctx)
		: analyzer_.decltype_qualified_name(node, ctx, LookupKind::Any);
	return entity_constant(analyzer_.require(named, node.text), node.text);
}

// 5.19p2: what a declaration the name reached is worth, which is the same
// question wherever the lookup that reached it was written - so a door that has
// already found the declaration asks this and looks the name up no second time.
SemaConstant ConstexprReading::entity_constant(SemaEntity& entity,
                                               const std::string& spelling)
{
	if (entity.object_member && analyzer_.self_ == nullptr)
	{
		// 5.1.1p13: an id-expression that denotes a non-static data member is
		// written as part of a class member access, or to form a pointer to
		// member, or inside an unevaluated operand - because the member is a
		// subobject of an object and the name alone does not say of which.
		// 5.2.5p1's access reaches this reading with the object already in hand
		// and never through here, and a name written inside a member function
		// has 9.3.1p3's implied one.  A name that arrives with neither is the
		// program's error rather than a reading that ran out, so 5.19p2's own
		// refusal - which a caller may answer with a stand-in - is not it.
		throw std::runtime_error(spelling + " names a non-static data member "
		                         "and no object it is a member of is written");
	}
	if (entity.address != 0 && analyzer_.types_.is_reference(entity.type))
	{
		// 8.3.2p1: the name of a reference names the object it was bound to, so
		// what it is worth is what that object holds - and `&` written on it is
		// that object's address and not one of the reference's own.
		return held_at(entity.address);
	}
	if (!entity.constant)
	{
		if (analyzer_.checking_ > 0 &&
		    (analyzer_.types_.is_dependent(entity.type) ||
		     !entity.covered_constant))
		{
			// 14.6p8: what a name that depends on a template parameter is
			// worth, an argument list is what says.  The reading stands one
			// value in its place, as it does for the size of a dependent type.
			// A declaration of the pattern whose own initializer this reading
			// ran out on is the same answer one name further along: its type
			// need not depend on anything - `static constexpr bool values[] =
			// { is_long<T>::value... };` is an array of `bool` however long the
			// pack is - and what it holds is still the arguments' to say.
			++analyzer_.stood_in_;
			SemaConstant stood;
			stood.type = analyzer_.types_.fundamental(FT_INT);
			stood.bits = 1;
			return stood;
		}
		if (entity.kind == SemaKind::Function)
		{
			// 4.3p1: the name of a function is one no reading takes a *value*
			// out of either, and every operand position it may stand at makes
			// it the pointer to that function before looking at it - so what
			// the name is worth is which function it is, exactly as an array's
			// name is which object it is.  14.3.2p1 is what asks: the `&` may
			// be omitted where the name refers to a function.
			return held_at(designated_entity(entity, spelling));
		}
		if ((entity.kind == SemaKind::Variable ||
		     entity.kind == SemaKind::Parameter) &&
		    analyzer_.types_.kind(analyzer_.types_.strip_cv(entity.type)) ==
		        TypeKind::Array)
		{
			// 4.2p1: a name of array type is one no reading takes a *value* out
			// of - `int numbers[4];` has none for a fold to wait on, and every
			// operand position it may stand at converts it to the address of
			// its first element before looking at it.  So what the name is
			// worth is which object it is, exactly as `static int n;` is, and
			// the conversion is each reader's: `numbers + 1`, `1 + numbers`,
			// `numbers != other`, `!numbers`, `*numbers` and `numbers[2]` are
			// one sentence asked at one door, and 8.3.2p1's binding of a
			// reference to the array reads the same object without it.
			return held_at(designated_entity(entity, spelling));
		}
		// 5.19p2 asked of a declaration whose own initializer this reading ran
		// out on is the same running out one name further along: `constexpr
		// char text[] = "ab";` holds a value 5.19 has and this build has not,
		// and `text[0]` beside it is no more the program's error than it was.
		throw NotConstant(spelling + " is not a constant expression",
		                  entity.covered_constant);
	}
	SemaConstant out;
	out.type = entity.type;
	out.bits = entity.value;
	out.real = entity.real;
	if (entity.kind == SemaKind::Variable || entity.kind == SemaKind::Parameter ||
	    entity.kind == SemaKind::Function)
	{
		// 3.10p1: the name of an object or a function is an lvalue, so what it
		// came to travels with the object it came out of - which is what 4.2p1's
		// decay, 8.3.2p1's reference binding and 5.3.1p3's `&` each ask for one
		// step further on.  The address is worked out once per declaration and
		// held on it, so a name read n times costs one interning and n reads.
		out.object = designated_entity(entity, spelling);
	}
	return out;
}

SemaConstant ConstexprReading::unary_constant(const AstNode& node,
                                              const SemaContext& ctx)
{
	if (node.token == OP_INC || node.token == OP_DEC)
	{
		// 5.3.2p1: the operand is written and the value is the object as it now
		// stands, so the operand is not read as a value first.
		return increment_constant(node, ctx, true);
	}
	if (node.token == OP_AMP)
	{
		// 5.3.1p3 with 5.19p2: `&E` is the address of the object `E` designates,
		// which is a value of its own and no reading of what that object holds -
		// so the operand is designated rather than evaluated, and `&n` over a
		// `static int n;` is a constant expression where `n` is not.  13.3.1.2p2
		// stands in front of it as it does in front of every operator, so an
		// operand of class or enumeration type is asked for its declarations of
		// `operator&` first.
		const std::uint32_t object = designated(*node.children[0], ctx);
		if (overloadable_place(address_type(object)))
		{
			const std::vector<SemaConstant> one(1, held_at(object));
			SemaConstant called;
			if (operator_constant(node.token, one, ctx, called))
			{
				return called;
			}
		}
		return pointer_constant(object, kNoType);
	}
	const SemaConstant given = analyzer_.evaluate(*node.children[0], ctx);
	{
		// 13.3.1.2p1: a unary operator on an operand of class or enumeration
		// type is a call of an operator function, which is chosen before any of
		// the readings below - 5.3.1p9's contextual `bool` among them, because
		// a class that declares `operator!` reaches this operator through that
		// declaration and not through a conversion to `bool`.
		const std::vector<SemaConstant> one(1, given);
		SemaConstant called;
		if (operator_constant(node.token, one, ctx, called))
		{
			return called;
		}
	}
	if (node.token == OP_STAR)
	{
		// 5.3.1p1: the unary `*` names the object its operand points to, whose
		// value is what the expression is worth where one is asked for.  4.2p1
		// stands in front of it, because `*numbers` is written on the array and
		// read through the pointer its first element has.
		return loaded(pointed_object(decayed_operand(given)));
	}
	if (node.token == OP_LNOT)
	{
		// 5.3.1p9: the operand is contextually converted to `bool`, which is
		// the reading `&&`, `||` and a condition ask and not a look at the
		// operand's bits - so an object of class type reaches it through the
		// one conversion 12.3.2p2 leaves `explicit` in, and a floating operand
		// is read by 4.12p1 rather than compared against zero here.
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = truth(given) ? 0 : 1;
		return out;
	}
	const SemaConstant operand = analyzer_.promote(given);
	SemaConstant out;
	out.type = operand.type;
	if (analyzer_.types_.is_floating(analyzer_.arithmetic_type(operand.type)))
	{
		// 5.3.1p7 and p8: `+` and `-` take a floating operand as it stands and
		// hand back its value and its negation.  5.3.1p10's `~` asks for an
		// integral operand; 5.3.1p9's `!` was answered above, where 4p3 reads
		// every operand of it alike.
		switch (node.token)
		{
		case OP_PLUS: out.real = operand.real; return out;
		case OP_MINUS: out.real = -operand.real; return out;
		default:
			throw NotConstant("a constant expression writes an operator that "
			                  "asks for an integral operand on a floating one");
		}
	}
	switch (node.token)
	{
	case OP_PLUS:
		out.bits = operand.bits;
		break;

	case OP_MINUS:
	{
		if (analyzer_.is_signed(out.type) && analyzer_.width_of(out.type) == 64 &&
		    operand.bits == (1ULL << 63))
		{
			throw NotConstant("a constant expression overflows");
		}
		out.bits = 0ULL - operand.bits;
		break;
	}

	case OP_COMPL:
		out.bits = ~operand.bits;
		break;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not evaluate", false);
	}
	return analyzer_.convert(out, out.type);
}

SemaConstant ConstexprReading::binary_constant(const AstNode& node,
                                               const SemaContext& ctx)
{
	// 5.14p1 and 5.15p1: the right operand of `&&` and `||` is evaluated only
	// when the left one does not decide the answer.  13.5p4 leaves that rule to
	// the *built-in* operator alone: where the expression is a call of an
	// operator function it is a call like any other, and a call evaluates both.
	// So which one stands here is asked first, of the declarations the left
	// operand reaches, and only then is the operand it leaves unread read.
	if (node.token == OP_LAND || node.token == OP_LOR)
	{
		std::vector<SemaConstant> operands;
		operands.push_back(analyzer_.evaluate(*node.children[0], ctx));
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		const bool overloaded = overloadable_operand(operands[0]);
		// 4.12p1's conversion to `bool` is the *built-in* operator's own
		// reading of the operand, so one of class or enumeration type is asked
		// for its declarations before it is asked for its truth: a class that
		// declares `operator&&` and no conversion to `bool` has no truth to
		// read at all.
		bool may_call =
			overloaded && reaches_operator(node.token, operands[0], ctx);
		// And an operand of any other type is asked for them where its truth
		// would end the reading, because that is the one place the answer
		// changes what is read - which leaves a fold that goes on to read the
		// right operand paying no lookup at all.
		if (!may_call && truth(operands[0]) == (node.token == OP_LOR))
		{
			may_call =
				!overloaded && reaches_operator(node.token, operands[0], ctx);
			if (!may_call)
			{
				out.bits = node.token == OP_LOR ? 1 : 0;
				return out;
			}
		}
		operands.push_back(analyzer_.evaluate(*node.children[1], ctx));
		SemaConstant called;
		if (operator_constant(node.token, operands, ctx, called))
		{
			return called;
		}
		// 13.3.1.2p2: the set held a declaration but 13.3 chose none of them,
		// so 13.6's built-in operator is what stands here after all - and its
		// left operand may already have decided the answer.
		if (may_call && truth(operands[0]) == (node.token == OP_LOR))
		{
			out.bits = node.token == OP_LOR ? 1 : 0;
			return out;
		}
		out.bits = truth(operands[1]) ? 1 : 0;
		return out;
	}
	// 5.18p1: the left operand of a comma is evaluated and its value discarded,
	// and the result is the right one - which is what a for-statement's head
	// writes to advance more than one object per pass.  13.5.3 gives `,` an
	// operator function too, which the same door answers.
	std::vector<SemaConstant> operands;
	operands.push_back(analyzer_.evaluate(*node.children[0], ctx));
	operands.push_back(analyzer_.evaluate(*node.children[1], ctx));
	SemaConstant called;
	if (operator_constant(node.token, operands, ctx, called))
	{
		return called;
	}
	if (node.token == OP_COMMA)
	{
		return operands[1];
	}
	return binary_value(node.token, operands[0], operands[1]);
}

// 5.6, 5.7 and 5.9 over two operands 5p10 has already brought to one floating
// type: the arithmetic is the machine's own at that width, and the operators an
// integral operand is what the clause asks for - the remainder, the shifts and
// the bitwise ones - reach no floating value at all.
SemaConstant ConstexprReading::real_value(unsigned token, long double left,
                                          long double right, TypeId type)
{
	SemaConstant out;
	switch (token)
	{
	case OP_LT: case OP_GT: case OP_LE: case OP_GE: case OP_EQ: case OP_NE:
	{
		bool answer = false;
		switch (token)
		{
		case OP_LT: answer = left < right; break;
		case OP_GT: answer = left > right; break;
		case OP_LE: answer = left <= right; break;
		case OP_GE: answer = left >= right; break;
		case OP_EQ: answer = left == right; break;
		default: answer = left != right; break;
		}
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}

	case OP_PLUS: out.real = left + right; break;
	case OP_MINUS: out.real = left - right; break;
	case OP_STAR: out.real = left * right; break;
	case OP_DIV:
		// 5.6p4: division by zero has undefined behaviour, which 5.19p2 leaves
		// outside a constant expression however the operands are spelled.
		if (right == 0)
		{
			throw NotConstant("a constant expression divides by zero");
		}
		out.real = left / right;
		break;

	default:
		throw NotConstant("a constant expression writes an operator that asks "
		                  "for integral operands on floating ones");
	}
	out.type = type;
	// 4.8p1 again: the answer is held at the width the operands were brought
	// to, which for `float` is narrower than the machine computed in.
	return analyzer_.convert(out, type);
}

SemaConstant ConstexprReading::binary_value(unsigned token,
                                            const SemaConstant& given_left,
                                            const SemaConstant& given_right)
{
	{
		// 5.7 and 5.9-5.10: an operator one of whose operands is a pointer is
		// arithmetic on an address rather than on a number, and 5p10's usual
		// arithmetic conversions reach neither operand of it - so it is asked
		// before the promotion that would read an address as one.
		SemaConstant address;
		if (address_operation(token, given_left, given_right, address))
		{
			return address;
		}
	}
	const SemaConstant left = analyzer_.promote(given_left);
	const SemaConstant right = analyzer_.promote(given_right);
	const bool comparison = token == OP_LT || token == OP_GT ||
		token == OP_LE || token == OP_GE || token == OP_EQ ||
		token == OP_NE;
	// 5.8p1: a shift takes 5p10's conversions on neither operand.  Each is
	// promoted on its own and the result has the type of the promoted left one,
	// so an unsigned count does not make the value it shifts unsigned.
	const bool shifted = token == OP_LSHIFT || token == OP_RSHIFT;
	const TypeId type =
		shifted ? left.type : analyzer_.common_type(left.type, right.type);
	if (analyzer_.types_.is_floating(analyzer_.arithmetic_type(type)))
	{
		return real_value(token, analyzer_.convert(left, type).real,
		                  analyzer_.convert(right, type).real, type);
	}
	const unsigned long long lhs = analyzer_.convert(left, type).bits;
	const unsigned long long rhs = shifted ? right.bits : analyzer_.convert(right, type).bits;
	const bool sign = analyzer_.is_signed(type);
	const long long signed_lhs = static_cast<long long>(lhs);
	const long long signed_rhs = static_cast<long long>(rhs);

	if (comparison)
	{
		bool answer = false;
		switch (token)
		{
		case OP_LT: answer = sign ? signed_lhs < signed_rhs : lhs < rhs; break;
		case OP_GT: answer = sign ? signed_lhs > signed_rhs : lhs > rhs; break;
		case OP_LE: answer = sign ? signed_lhs <= signed_rhs : lhs <= rhs; break;
		case OP_GE: answer = sign ? signed_lhs >= signed_rhs : lhs >= rhs; break;
		case OP_EQ: answer = lhs == rhs; break;
		default: answer = lhs != rhs; break;
		}
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}

	SemaConstant out;
	out.type = type;
	long long result = 0;
	bool overflowed = false;
	switch (token)
	{
	case OP_PLUS:
		overflowed = add_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs + rhs;
		break;

	case OP_MINUS:
		overflowed = subtract_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs - rhs;
		break;

	case OP_STAR:
		overflowed = multiply_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs * rhs;
		break;

	case OP_DIV:
	case OP_MOD:
		if (rhs == 0)
		{
			throw NotConstant("a constant expression divides by zero");
		}
		if (sign)
		{
			overflowed = signed_lhs == (-1 - 0x7FFFFFFFFFFFFFFFLL) && signed_rhs == -1;
			result = token == OP_DIV ? signed_lhs / signed_rhs
			                              : signed_lhs % signed_rhs;
			out.bits = static_cast<unsigned long long>(result);
			break;
		}
		out.bits = token == OP_DIV ? lhs / rhs : lhs % rhs;
		break;

	case OP_LSHIFT:
	case OP_RSHIFT:
	{
		const unsigned long long count =
			analyzer_.is_signed(right.type) && static_cast<long long>(rhs) < 0
				? static_cast<unsigned long long>(analyzer_.width_of(type))
				: rhs;
		if (count >= analyzer_.width_of(type))
		{
			throw NotConstant("a constant expression shifts by more than "
			                         "the width of its type");
		}
		if (token == OP_LSHIFT)
		{
			out.bits = lhs << count;
			result = static_cast<long long>(out.bits);
			// 5.8p2: a signed left operand shall be non-negative, and the value
			// shall be representable in the *unsigned* type of the same width -
			// so `1LL << 63` is the sign bit and not an overflow, which is what
			// the bits shifted past the width say.
			overflowed = sign &&
				(signed_lhs < 0 ||
				 (count != 0 && (lhs >> (analyzer_.width_of(type) - count)) != 0));
			break;
		}
		out.bits = sign ? static_cast<unsigned long long>(signed_lhs >> count)
		                : lhs >> count;
		break;
	}

	case OP_AMP: out.bits = lhs & rhs; break;
	case OP_BOR: out.bits = lhs | rhs; break;
	case OP_XOR: out.bits = lhs ^ rhs; break;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not evaluate", false);
	}

	// 5p4: an operation whose result its type cannot represent has undefined
	// behaviour, so it is not a constant expression.  5.8p2's shift is the one
	// operation whose signed result may hold the sign bit and still be the
	// value the clause names, so it answers for itself.
	const SemaConstant narrowed = analyzer_.convert(out, type);
	if (sign && (overflowed || (!shifted && narrowed.bits != out.bits)))
	{
		throw NotConstant("a constant expression overflows");
	}
	return narrowed;
}

// 5.2.3p1, 5.2.3p2/p3 and 5.2.2p1: the one shape the grammar hands on as a
// call, which is a cast written in functional notation, an object of literal
// class type, or a call of a function - and which of the three it is, is
// settled by the one lookup of the name before the parentheses.
// 5.2.1p1: `E1[E2]`, whose left operand is one of three things - an array a
// name designates, a pointer into one, or an object of class type whose
// `operator[]` 13.5.5p1 makes this a call of.
SemaConstant ConstexprReading::subscript_constant(const AstNode& node,
                                                  const SemaContext& ctx)
{
	// 2.14.5p8's literal is an array object no declaration named, and
	// `evaluate` hands it back as that object - so the four left operands
	// 5.2.1p1 has are one reading here and no arm of this dispatch.
	return element_at(analyzer_.evaluate(*node.children[0], ctx),
	                  analyzer_.evaluate(*node.children[1], ctx), ctx);
}

// The same operator over two operands already read, which is what the reading
// that has a *spelling* rather than a tree holds - 14.2 writes a subscript
// inside a template-argument-list, and `sema_value_expression.cpp` splits it
// back out of the words.  The three arms are 5.2.1p1's three left operands; the
// string literal is the fourth and is the one arm a spelling answers on its
// own, because 2.14.5p8's array is the literal itself and no operand at all.
SemaConstant ConstexprReading::element_at(const SemaConstant& held,
                                          const SemaConstant& index,
                                          const SemaContext& ctx)
{
	// 5.2.1p1: `E1[E2]` is `*(E1 + E2)`, and 5.7p5's addition is written either
	// way round - so which of the two operands is the array is what says which
	// is the index, and `2[a]` names the element `a[2]` does.  13.5.5p1's
	// `operator[]` is asked below in the order the program wrote, because that
	// one is a member of the left operand's class and not a commutative
	// operator at all.
	const bool reversed = !indexable(held) && indexable(index);
	const SemaConstant& array = reversed ? index : held;
	const SemaConstant& at = reversed ? held : index;
	if (holds_address(array))
	{
		// `E1[E2]` is `*(E1 + E2)`, which over a pointer operand is 5.7p5's
		// element and no reading of an array's own list.
		return subscripted(array, at);
	}
	// 13.3.1.2p1 with 13.5.5p1: a subscript of an object of class type is the
	// call of a member `operator[]` and no reading of an array at all.
	std::vector<SemaConstant> operands;
	operands.push_back(held);
	operands.push_back(index);
	SemaConstant called;
	if (operator_constant(OP_LSQUARE, operands, ctx, called))
	{
		return called;
	}
	return element_value(array, counted(at));
}

// Whether an operand of 5.2.1p1's subscript is the array rather than the index:
// a pointer into one, or an array read as the list of its own elements.
bool ConstexprReading::indexable(const SemaConstant& value) const
{
	return holds_address(value) ||
		analyzer_.types_.kind(analyzer_.types_.strip_cv(value.type)) ==
			TypeKind::Array;
}

// 5.4p4 and 5.2.9: a cast written in either notation direct-initializes an
// object of the type named, so which reading answers it is that type - and each
// of the four is the reading an initialization of such a place asks anywhere
// else.  12.3.2p2 makes this the other place a conversion function declared
// `explicit` answers, which is why the arithmetic arm asks for the conversion
// here rather than leaving it to `convert`: that one is 5.19p3's implicit
// sequence and reaches no such declaration.
SemaConstant ConstexprReading::cast_constant(const AstNode& node,
                                             const SemaContext& ctx)
{
	if (node.children.size() != 2 ||
	    node.children[0]->kind != AstKind::TypeId)
	{
		throw NotConstant("a constant expression holds a cast PA11 does not "
		                  "evaluate", false);
	}
	TypeTable& types = analyzer_.types_;
	const TypeId type = analyzer_.type_id_type(*node.children[0], ctx);
	if (types.kind(types.strip_cv(type)) == TypeKind::Pointer)
	{
		// 4.10p1 and 4.2p1: what reaches a pointer is an address or the null
		// pointer value, so the operand is read for the object it designates as
		// it is at any other place of that type.  12.3.2p2 makes this the other
		// place a conversion function declared `explicit` answers, which is why
		// the door is asked here rather than left to `convert`.
		SemaConstant out;
		if (at_pointer_place(operand_constant(*node.children[1], ctx), type, out,
		                     true))
		{
			return out;
		}
	}
	if (types.is_reference(types.strip_cv(type)) ||
	    types.is_class(types.strip_cv(type)))
	{
		// 5.2.9p2 and 5.2.9p4: a cast to a reference type binds that reference
		// to the object the operand designates, and one to a class type builds
		// an object of it.
		return at_object_place(operand_constant(*node.children[1], ctx), type);
	}
	if (types.is_void(types.strip_cv(type)))
	{
		// 5.2.9p4: any expression converts to cv `void`, and what it becomes is
		// a discarded-value expression - so the operand is evaluated, for the
		// writes 5.19 lets an evaluation make and for the refusal a subexpression
		// that is no constant expression is, and the result holds no value.  It
		// is 5.18p1's left operand and 5.16p2's arm of `void` type that reach
		// this, and neither of them reads what it comes to.
		SemaConstant discarded = operand_constant(*node.children[1], ctx);
		discarded.type = type;
		discarded.bits = 0;
		discarded.real = 0;
		discarded.object = 0;
		discarded.valued = false;
		return discarded;
	}
	if (analyzer_.arithmetic_type(type) == kNoType)
	{
		throw NotConstant("a constant expression casts to a type that is not "
		                  "arithmetic", false);
	}
	SemaConstant out = analyzer_.convert(
		at_arithmetic_place(analyzer_.evaluate(*node.children[1], ctx),
		                    analyzer_.arithmetic_type(type), true),
		type);
	out.type = type;
	return out;
}

SemaConstant ConstexprReading::call_or_cast(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 5.2.3p1: `T(x)` is the cast `(T)x` written in functional notation,
	// which the grammar hands on as a call because it cannot say whether
	// the name before the parentheses is a type.  A call of a *function*
	// is the other reading of the shape and is 5.19p2's constexpr
	// function, so only the arm whose callee names an arithmetic type
	// folds here.
	const AstNode& callee = *node.children[0];
	TypeId target = kNoType;
	if (callee.kind == AstKind::IdExpression)
	{
		target = analyzer_.keyword_type(callee.text);
	}
	if (target == kNoType && callee.kind == AstKind::IdExpression)
	{
		// 7.1.6.2p1: the simple-type-specifier may be a typedef-name or an
		// enum-name the program declared, which 3.4 answers for - and a
		// name that reaches no region at all is a call and not a cast.
		//
		// 3.3.10p2: a class or enumeration name declared in the same region as
		// a variable, a data member, a function or an enumerator of that name
		// is hidden wherever that other name is visible, so what tells 5.2.3's
		// cast from 5.2.2's call is 3.4.1's ordinary lookup - which finds the
		// function and leaves the class unreachable to everything but an
		// elaborated-type-specifier.  `LookupKind::Type` is 3.4.4p2's reading
		// and answers the class here, which is the question a *cast* is asked
		// and not the one this door has.
		const unsigned stood = analyzer_.stood_in_;
		try
		{
			SemaEntity* const named =
				analyzer_.resolve(callee.text, ctx, LookupKind::Any);
			target = named == nullptr || !names_a_type(*named) ? kNoType
			                                                   : named->type;
		}
		catch (const std::exception&)
		{
			// 14.6p8's count is of the stand-ins a *reading* made, and a name
			// this one threw out made none - the same as `probe_type_id` and
			// the pattern reading `sema_specialize.cpp` throws away.  A name
			// that is a template-id reaches an argument list on the way here,
			// so the count can move before the throw.
			analyzer_.stood_in_ = stood;
			target = kNoType;
		}
	}
	// 5.2.3p3: `T{x}` is written where `T(x)` is, and the braces put
	// 8.5.4's one initializer-clause where the operand stands.
	const AstNode* list =
		node.children.size() < 2 ? nullptr : node.children[1];
	if (list != nullptr && list->braced)
	{
		list = list->children.empty() ? nullptr : list->children[0];
	}
	if (list == nullptr)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate", false);
	}
	// 14.5.3p4: what the parentheses hold is the run a `pattern...` entry
	// stands for, which is the same reading the analyzer_.lowering of this cast is
	// given - 5.2.3's one rule has one answer here too.  It is read once
	// here, because a cast, an object of class type and 5.2.2's call all
	// take the operands the same list wrote.
	InitializerClauses written(list, analyzer_, ctx);
	if (written.list.unsettled())
	{
		// 14.6p8: a run no argument list has settled says neither how many
		// operands the cast has nor what they are worth, so the reading
		// stands a value in for it exactly as it does for `sizeof...`.
		// The stand-in is 1 rather than 0 because 8.3.4p1 gives an array
		// no bound of zero.
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = target == kNoType ? analyzer_.types_.fundamental(FT_INT) : target;
		out.bits = 1;
		return out;
	}
	if (target != kNoType && analyzer_.types_.is_dependent(target))
	{
		// 14.6p8: what an object of a type an argument list has yet to settle
		// holds is something only the instantiation knows, so the reading stands
		// a value in its place exactly as it does for the size of a dependent
		// type - and the instantiation reads the expression again with the
		// arguments bound, which is where the answer is made.
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = target;
		out.bits = 1;
		return out;
	}
	std::vector<SemaConstant> operands;
	operands.reserve(written.list.size());
	const unsigned stood = analyzer_.stood_in_;
	while (!written.spent())
	{
		const SemaContext inner = written.in(ctx);
		const AstNode& clause = written.next();
		++written.at;
		if (target == kNoType && clause.kind == AstKind::BracedInitList)
		{
			// 8.5.4p1: the operand is a braced-init-list, which is no
			// expression and has no type of its own - `f({})` is 0 at an `int`
			// place, a null pointer at a pointer one and an object at a class
			// one - so there is nothing to arrive at until 13.3 has chosen the
			// declaration whose place it fills.  The list travels as the
			// operand and 8.5.4 reads it where that place is known, which is
			// the same order 13.3.3.1.5 puts the two questions in.
			SemaConstant listed;
			listed.braced = &clause;
			listed.region = inner.scope;
			operands.push_back(listed);
			continue;
		}
		// 8.3.2p1: a place this call may reach binds to the object the argument
		// designates rather than to a value, so an argument that has no value
		// this reading holds is still one such a place accepts.
		operands.push_back(target == kNoType
			                   ? operand_constant(clause, inner)
			                   : analyzer_.evaluate(clause, inner));
	}
	if (analyzer_.stood_in_ != stood)
	{
		// 14.6p8: an operand whose own reading stood a value in is one an
		// argument list has yet to settle, and neither 13.3's choice among the
		// declarations nor the body the one chosen names is a question this
		// reading may answer over such a value - so the whole call stands in
		// too, and the instantiation reads it again with the arguments bound.
		SemaConstant out;
		out.type =
			target == kNoType ? analyzer_.types_.fundamental(FT_INT) : target;
		out.bits = 1;
		return out;
	}
	if (target != kNoType && analyzer_.types_.is_class(analyzer_.types_.strip_cv(target)))
	{
		// 5.2.3p2 and p3: an object of literal class type, which at a value
		// place is what 12.3.2p1's conversion function reads.
		return object_of(target, operands);
	}
	if (target == kNoType)
	{
		// 5.2.2p1 with 7.1.5p2: the other reading of the shape, which is a
		// call of a function - and only a constexpr function whose body
		// this unit has read is one 5.19p2 folds.
		return called(callee, operands, ctx);
	}
	if (analyzer_.arithmetic_type(target) == kNoType || operands.size() > 1)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate", false);
	}
	if (operands.empty())
	{
		// 5.2.3p2: `T()` is the value-initialization 8.5p7 writes, which
		// for an arithmetic type is the zero 8.5p6 converts to it.
		SemaConstant out;
		out.type = target;
		out.bits = 0;
		return out;
	}
	// 5.2.3p1 and 5.4p4: the functional notation direct-initializes the object
	// too, so 12.3.2p2's `explicit` conversion function answers this cast as it
	// answers the one written `(T)x`.
	SemaConstant out = analyzer_.convert(
		at_arithmetic_place(operands[0], analyzer_.arithmetic_type(target),
		                    true),
		target);
	out.type = target;
	return out;
}
