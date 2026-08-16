#include "sema_constexpr.h"

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_scope.h"

// 6.1-6.6 inside a constant evaluation: the statements of a constexpr
// function's body, run.
//
// 7.1.5p3 leaves a C++11 constexpr body one `return` statement, and reading
// that one expression is what `sema_constexpr.cpp` did.  PA21's own boundary
// says the evaluation may run a strict superset of it - local objects,
// assignment and loops - and every later milestone that folds a call written
// against real library code needs that superset, so the body is *executed*
// here rather than pattern-matched.  The two readings of 5.19 share it: a fold
// asked from a declaration and a fold asked from a template-argument-list both
// arrive at `ConstexprReading::call`, which is the only caller of this walk.
//
// The walk is one pass over the statements, with three facts on the frame that
// keep it from growing with the work:
//
//   - a block's region is opened once per fold and reused on every pass, so a
//     loop of n iterations costs the regions the body has rather than n of
//     them;
//   - the object a declaration declares is created once and written again, so
//     the same loop costs one entity per declaration rather than n;
//   - what a block declared is unset when the block is re-entered, so 3.3.3p2's
//     fresh object is still what a name written before its declaration reaches.
//
// The one thing an evaluation may write is such a binding: 5.19p2 leaves it no
// object of the program to change, so `assigned` refuses every name whose
// declaration this fold did not make.

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

// 5.17p1: the operator a compound assignment writes back, which 5.17p7 makes
// the whole of what tells one from another.  `OP_ASS` itself writes no operator
// and is not asked here.
unsigned compound_operator(unsigned token)
{
	switch (token)
	{
	case OP_PLUSASS: return OP_PLUS;
	case OP_MINUSASS: return OP_MINUS;
	case OP_STARASS: return OP_STAR;
	case OP_DIVASS: return OP_DIV;
	case OP_MODASS: return OP_MOD;
	case OP_LSHIFTASS: return OP_LSHIFT;
	case OP_RSHIFTASS: return OP_RSHIFT;
	case OP_BANDASS: return OP_AMP;
	case OP_BORASS: return OP_BOR;
	case OP_XORASS: return OP_XOR;
	default: break;
	}
	throw NotConstant("a constant expression writes an assignment it does not "
	                  "evaluate");
}

// 7.1.3p1: whether the declarators of this decl-specifier-seq declare type
// names rather than objects, which is the whole of what tells a
// declaration-statement the fold *runs* from one it only reads for the names it
// introduces.  It is asked of the syntax because reading the sequence declares
// whatever class it writes, and a declaration is read once.
bool declares_type_names(const AstNode& specifiers)
{
	for (std::size_t index = 0; index < specifiers.children.size(); ++index)
	{
		const AstNode& one = *specifiers.children[index];
		if (one.kind == AstKind::DeclSpecifier && one.token == KW_TYPEDEF)
		{
			return true;
		}
	}
	return false;
}

// 6.5p1: the substatement of an iteration statement, which is the one child
// that is neither its condition nor the parts of a for-statement's head.
const AstNode* substatement(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::Condition &&
		    child.kind != AstKind::ForInitStatement &&
		    child.kind != AstKind::Iteration)
		{
			return &child;
		}
	}
	return nullptr;
}

}

void ConstexprReading::step(ConstexprFrame& frame)
{
	if (++frame.steps > kMaxConstexprSteps)
	{
		throw NotConstant("a constant expression runs more statements than "
		                  "this implementation evaluates");
	}
}

bool ConstexprReading::truth(const SemaConstant& value)
{
	// 4p3: a condition is contextually converted to `bool`, which for an object
	// of class type is 12.3.2p1's conversion function - the same reading every
	// other arithmetic place asks, with `bool` as the place.  4.12p1 makes the
	// answer `bool`, so the conversion is what reads a floating value and this
	// looks at the bits it left.
	const TypeId to_bool = analyzer_.types_.fundamental(FT_BOOL);
	return analyzer_.convert(at_arithmetic_place(value, to_bool), to_bool)
		       .bits != 0;
}

unsigned long long ConstexprReading::counted(const SemaConstant& given)
{
	const SemaConstant value = at_arithmetic_place(given, kNoType);
	if (analyzer_.integral_type(value.type) == kNoType)
	{
		throw NotConstant("a constant expression counts objects with a value "
		                  "that is not integral");
	}
	return value.bits;
}

SemaContext ConstexprReading::block_region(const AstNode& node,
                                           const SemaContext& ctx,
                                           ConstexprFrame& frame)
{
	SemaContext inner = ctx;
	Scope*& held = frame.regions[&node];
	if (held == nullptr)
	{
		held = &analyzer_.model_.open(ScopeKind::Block, *ctx.scope, nullptr,
		                              ctx.dump);
		inner.scope = held;
		return inner;
	}
	// 3.3.3p2: the objects of a block belong to the pass through it, so what
	// the last one wrote is no value the next one may read.
	const std::vector<SemaEntity*>& made = frame.declared[held];
	for (std::size_t index = 0; index < made.size(); ++index)
	{
		made[index]->constant = false;
	}
	inner.scope = held;
	return inner;
}

SemaEntity* ConstexprReading::declared_object(const AstNode& key,
                                              ConstexprFrame& frame) const
{
	const std::unordered_map<const AstNode*, SemaEntity*>::const_iterator held =
		frame.objects.find(&key);
	return held == frame.objects.end() ? nullptr : held->second;
}

void ConstexprReading::introduce(const AstNode& node, const SemaContext& ctx,
                                 ConstexprFrame& frame)
{
	// 3.3.1p1: the region such a declaration introduces its name into is the
	// block's, which this fold opened once - so a later pass through the block
	// reaches the name already standing there.  Reading the declaration again
	// would introduce a second one, which for a class-specifier is 3.2p1's
	// class defined twice and for the rest is a declaration per pass.
	if (!frame.introduced.insert(&node).second)
	{
		return;
	}
	analyzer_.declaration(node, ctx);
}

SemaEntity& ConstexprReading::local(const AstNode& key, const std::string& name,
                                    TypeId type, const SemaContext& ctx,
                                    ConstexprFrame& frame)
{
	SemaEntity& made = analyzer_.model_.create(SemaKind::Variable, name, type);
	made.fold_local = true;
	made.region = ctx.scope;
	if (!name.empty())
	{
		analyzer_.model_.bind(*ctx.scope, name, made);
		analyzer_.model_.declare_in(*ctx.scope, made);
	}
	frame.declared[ctx.scope].push_back(&made);
	frame.objects[&key] = &made;
	return made;
}

TypeId ConstexprReading::declared_type(const AstNode& specifiers,
                                       const AstNode& declarator,
                                       const SemaContext& ctx,
                                       std::string& name)
{
	SemaSpan span;
	span.begin = specifiers.begin;
	span.end = declarator.end;
	const SemaAnalyzer::Specifiers read =
		analyzer_.read_specifiers(specifiers, ctx, span, true, std::string());
	return analyzer_.declarator_type(declarator,
	                                 analyzer_.specifier_type(read), ctx, &name);
}

void ConstexprReading::declared(const AstNode& node, const SemaContext& ctx,
                                ConstexprFrame& frame)
{
	const AstNode* const list = child_kind(node, AstKind::InitDeclaratorList);
	if (list == nullptr || node.children.empty() ||
	    declares_type_names(*node.children[0]))
	{
		// 7p3: a declaration that declares no object - a typedef, an
		// elaborated-type-specifier, a class - introduces the names the rest of
		// the body reads and runs nothing, so it is declared and not executed.
		introduce(node, ctx, frame);
		return;
	}
	const AstNode& specifiers = *node.children[0];
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& init = *list->children[index];
		if (init.children.empty())
		{
			continue;
		}
		// The declarator says what the object is on the pass that creates it
		// and says the same thing on every later one, so it is read once: a
		// decl-specifier-seq is what may hold a class-specifier, and reading
		// one twice is 3.2p1's class defined twice.
		SemaEntity* held = declared_object(init, frame);
		if (held == nullptr)
		{
			std::string name;
			const TypeId declared =
				declared_type(specifiers, *init.children[0], ctx, name);
			held = &local(init, name, declared, ctx, frame);
		}
		SemaEntity& object = *held;
		const TypeId type = object.type;
		const AstNode* const written = child_kind(init, AstKind::Initializer);
		if (written == nullptr || written->children.empty())
		{
			// 8.5p11: an object of scalar type with no initializer holds no
			// value, so it is bound and left unset, and reading it before an
			// assignment writes it is not a constant expression.
			object.constant = false;
			continue;
		}
		SemaConstant value = analyzer_.evaluate(*written->children[0], ctx);
		if (analyzer_.arithmetic_type(type) != kNoType)
		{
			// 8.5p16 with 4: the value is converted to the declared type, which
			// is what every later read of the object sees.
			value = analyzer_.convert(value, type);
		}
		object.value = value.bits;
		object.real = value.real;
		object.constant = true;
	}
}

bool ConstexprReading::condition_value(const AstNode& node,
                                       const SemaContext& ctx,
                                       ConstexprFrame& frame)
{
	if (node.children.empty())
	{
		throw NotConstant("a constant expression runs a statement whose "
		                  "condition it cannot read");
	}
	const AstNode& written = *node.children[0];
	if (written.kind != AstKind::ConditionDeclaration)
	{
		return truth(analyzer_.evaluate(written, ctx));
	}
	// 6.4p3 and 6.4p4: the declaration declares its name in the region the
	// statement opened, and the condition is the value of that object.
	const AstNode* const declarator =
		child_kind(written, AstKind::Declarator);
	const AstNode* const initializer =
		child_kind(written, AstKind::Initializer);
	if (declarator == nullptr || initializer == nullptr ||
	    initializer->children.empty() || written.children.empty())
	{
		throw NotConstant("a constant expression runs a condition whose "
		                  "declaration it cannot read");
	}
	SemaEntity* held = declared_object(written, frame);
	if (held == nullptr)
	{
		std::string name;
		const TypeId declared =
			declared_type(*written.children[0], *declarator, ctx, name);
		held = &local(written, name, declared, ctx, frame);
	}
	SemaEntity& object = *held;
	const TypeId type = object.type;
	SemaConstant value = analyzer_.evaluate(*initializer->children[0], ctx);
	if (analyzer_.arithmetic_type(type) != kNoType)
	{
		value = analyzer_.convert(value, type);
	}
	object.value = value.bits;
	object.real = value.real;
	object.constant = true;
	value.type = type;
	return truth(value);
}

ConstexprFlow ConstexprReading::selection(const AstNode& node,
                                          const SemaContext& ctx,
                                          ConstexprFrame& frame)
{
	// 6.4p3: the object a condition declares belongs to a region that encloses
	// both substatements, which is the one the statement itself opens.
	const SemaContext inner = block_region(node, ctx, frame);
	const AstNode* const written = child_kind(node, AstKind::Condition);
	if (written == nullptr)
	{
		throw NotConstant("a constant expression runs an if statement with no "
		                  "condition");
	}
	const AstNode* const arm =
		child_kind(node, condition_value(*written, inner, frame)
		                     ? AstKind::Then : AstKind::Else);
	if (arm == nullptr || arm->children.empty())
	{
		return ConstexprFlow::Normal;
	}
	return statement(*arm->children[0], inner, frame);
}

ConstexprFlow ConstexprReading::iteration(const AstNode& node,
                                          const SemaContext& ctx,
                                          ConstexprFrame& frame)
{
	const SemaContext inner = block_region(node, ctx, frame);
	const AstNode* const written = child_kind(node, AstKind::Condition);
	const AstNode* const body = substatement(node);
	if (written == nullptr)
	{
		throw NotConstant("a constant expression runs a loop with no "
		                  "condition");
	}
	// 6.5.1p1 tests the condition before each pass and 6.5.2p1 after each one,
	// which is the whole of what tells the two statements apart.
	const bool tested_first = node.kind == AstKind::WhileStatement;
	for (;;)
	{
		step(frame);
		if (tested_first && !condition_value(*written, inner, frame))
		{
			return ConstexprFlow::Normal;
		}
		if (body != nullptr)
		{
			const ConstexprFlow left = statement(*body, inner, frame);
			if (left == ConstexprFlow::Returned)
			{
				return left;
			}
			if (left == ConstexprFlow::Break)
			{
				return ConstexprFlow::Normal;
			}
		}
		if (!tested_first && !condition_value(*written, inner, frame))
		{
			return ConstexprFlow::Normal;
		}
	}
}

ConstexprFlow ConstexprReading::counted_loop(const AstNode& node,
                                             const SemaContext& ctx,
                                             ConstexprFrame& frame)
{
	// 6.5.3p1: the for-init-statement, the condition and the objects either
	// declares stand in a region of the statement's own, which the
	// loop-continuation expression and the substatement are read in too.
	const SemaContext inner = block_region(node, ctx, frame);
	const AstNode* const init = child_kind(node, AstKind::ForInitStatement);
	const AstNode* const written = child_kind(node, AstKind::Condition);
	const AstNode* const iteration = child_kind(node, AstKind::Iteration);
	const AstNode* const body = substatement(node);
	if (init != nullptr)
	{
		for (std::size_t index = 0; index < init->children.size(); ++index)
		{
			statement(*init->children[index], inner, frame);
		}
	}
	for (;;)
	{
		step(frame);
		// 6.5.3p1: a for-statement with no condition loops until something
		// inside it leaves.
		if (written != nullptr && !condition_value(*written, inner, frame))
		{
			return ConstexprFlow::Normal;
		}
		if (body != nullptr)
		{
			const ConstexprFlow left = statement(*body, inner, frame);
			if (left == ConstexprFlow::Returned)
			{
				return left;
			}
			if (left == ConstexprFlow::Break)
			{
				return ConstexprFlow::Normal;
			}
		}
		if (iteration != nullptr && !iteration->children.empty())
		{
			analyzer_.evaluate(*iteration->children[0], inner);
		}
	}
}

ConstexprFlow ConstexprReading::statement(const AstNode& node,
                                          const SemaContext& ctx,
                                          ConstexprFrame& frame)
{
	step(frame);
	switch (node.kind)
	{
	case AstKind::CompoundStatement:
	{
		const SemaContext inner = block_region(node, ctx, frame);
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			const ConstexprFlow left =
				statement(*node.children[index], inner, frame);
			if (left != ConstexprFlow::Normal)
			{
				return left;
			}
		}
		return ConstexprFlow::Normal;
	}

	case AstKind::ExpressionStatement:
		// 6.2p1: the expression is evaluated and its value discarded, which
		// leaves whatever it wrote behind.
		if (!node.children.empty())
		{
			analyzer_.evaluate(*node.children[0], ctx);
		}
		return ConstexprFlow::Normal;

	case AstKind::ReturnStatement:
		if (!node.children.empty())
		{
			frame.result = analyzer_.evaluate(*node.children[0], ctx);
		}
		frame.returned = true;
		return ConstexprFlow::Returned;

	case AstKind::SimpleDeclaration:
		declared(node, ctx, frame);
		return ConstexprFlow::Normal;

	case AstKind::AliasDeclaration:
	case AstKind::UsingDeclaration:
	case AstKind::UsingDirective:
	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
	case AstKind::EnumSpecifier:
		// 7.1.5p3's own list, and the type definition the parser leaves as the
		// specifier itself where a declaration wrote no declarator: these
		// introduce a name into the block's region and declare no object, so
		// the pass that first reaches one reads it and the rest walk past.
		introduce(node, ctx, frame);
		return ConstexprFlow::Normal;

	case AstKind::StaticAssertDeclaration:
		// 7p4: the condition is checked where the declaration stands, which
		// for one inside a loop is every pass through it.
		analyzer_.declaration(node, ctx);
		return ConstexprFlow::Normal;

	case AstKind::EmptyDeclaration:
		return ConstexprFlow::Normal;

	case AstKind::BreakStatement:
		return ConstexprFlow::Break;

	case AstKind::ContinueStatement:
		return ConstexprFlow::Continue;

	case AstKind::IfStatement:
		return selection(node, ctx, frame);

	case AstKind::WhileStatement:
	case AstKind::DoStatement:
		return iteration(node, ctx, frame);

	case AstKind::ForStatement:
		return counted_loop(node, ctx, frame);

	default:
		break;
	}
	throw NotConstant("a constant expression runs a statement this "
	                  "implementation does not evaluate");
}

SemaEntity& ConstexprReading::assigned(const AstNode& node,
                                       const SemaContext& ctx)
{
	const AstNode* target = &node;
	while (target->kind == AstKind::ParenthesizedExpression &&
	       !target->children.empty())
	{
		target = target->children[0];
	}
	if (target->kind != AstKind::IdExpression)
	{
		throw NotConstant("a constant expression writes to something that is "
		                  "not a name the evaluation declared");
	}
	SemaEntity& entity = analyzer_.require(
		analyzer_.resolve(target->text, ctx, LookupKind::Any), target->text);
	if (!entity.fold_local)
	{
		// 5.19p2: an evaluation may write no object whose lifetime began
		// outside it, which here is every declaration of the program.
		throw NotConstant(target->text + " is written by a constant expression "
		                  "that did not declare it");
	}
	return entity;
}

SemaConstant ConstexprReading::assignment_constant(const AstNode& node,
                                                   const SemaContext& ctx)
{
	if (node.children.size() < 2)
	{
		throw NotConstant("a constant expression holds an assignment with no "
		                  "operands");
	}
	SemaEntity& target = assigned(*node.children[0], ctx);
	const SemaConstant written = analyzer_.evaluate(*node.children[1], ctx);
	SemaConstant value = written;
	if (node.token != OP_ASS)
	{
		if (!target.constant)
		{
			throw NotConstant(target.name + " is read by a compound assignment "
			                  "before anything wrote it");
		}
		SemaConstant held;
		held.type = target.type;
		held.bits = target.value;
		held.real = target.real;
		value = binary_value(compound_operator(node.token), held, written);
	}
	if (analyzer_.arithmetic_type(target.type) != kNoType)
	{
		// 5.17p7: the result of the operation is converted back to the type of
		// the left operand, which is also 5.17p3 for the plain assignment.
		value = analyzer_.convert(value, target.type);
	}
	value.type = target.type;
	target.value = value.bits;
	target.real = value.real;
	target.constant = true;
	return value;
}

SemaConstant ConstexprReading::increment_constant(const AstNode& node,
                                                  const SemaContext& ctx,
                                                  bool prefix)
{
	if (node.children.empty())
	{
		throw NotConstant("a constant expression holds an increment with no "
		                  "operand");
	}
	SemaEntity& target = assigned(*node.children[0], ctx);
	if (!target.constant)
	{
		throw NotConstant(target.name + " is incremented before anything "
		                  "wrote it");
	}
	SemaConstant held;
	held.type = target.type;
	held.bits = target.value;
	held.real = target.real;
	SemaConstant one;
	one.type = analyzer_.types_.fundamental(FT_INT);
	one.bits = 1;
	SemaConstant next =
		binary_value(node.token == OP_INC ? OP_PLUS : OP_MINUS, held, one);
	if (analyzer_.arithmetic_type(target.type) != kNoType)
	{
		next = analyzer_.convert(next, target.type);
	}
	next.type = target.type;
	target.value = next.bits;
	target.real = next.real;
	// 5.2.6p1: the postfix forms hand back the value the object held, and
	// 5.3.2p1's prefix forms hand back the object as it now stands.
	return prefix ? next : held;
}
