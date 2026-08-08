#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "ast_model.h"

// 5.3.4 and 5.3.5: the storage an object with dynamic storage duration stands
// in, and the lifetime that begins and ends in it.
//
// Nothing here is a second object model.  A new-expression is one call of an
// allocation function and then the initialization 8.5 would give an object of
// the type declared with the same initializer; a delete-expression is 12.4p3's
// end of that object's lifetime and then one call of a deallocation function.
// What is peculiar to them is only where the storage came from and who gives it
// back - which is why the analysis writes both as actions over storage the
// expression already names, and why the array form's count is a value the
// expression carries rather than a fact of any type.

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

const AstNode* arguments_of(const AstNode& node)
{
	const AstNode* list = child_kind(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_kind(node, AstKind::ParenArgumentList);
}

}

// 3.7.4.1p2 and 5.3.4p9: which declarations of `operator new` a new-expression
// chooses among.  13.5p1 leaves the allocation functions out of the operators a
// program may give a meaning to, so the name is one an ordinary qualified
// lookup finds and nothing about it is special but where the lookup starts: the
// global namespace for a new-expression written with `::` and for one whose
// allocated type is no class, and the class itself otherwise, which 10.2 also
// searches the bases of.  A class that declares none leaves the global
// namespace's declarations to answer.
SemaEntity* SemaAnalyzer::allocation_function(bool global, TypeId created,
                                              bool array,
                                              std::vector<SemaEntity*>& found)
{
	static const std::string kName("operatornew");
	static const std::string kArrayName("operatornew[]");
	const std::string& name = array ? kArrayName : kName;
	if (!global)
	{
		SemaEntity* const owner = model_.type_owner(types_.strip_cv(created));
		if (owner != nullptr && owner->scope != nullptr)
		{
			SemaEntity* const member =
				model_.lookup_in(*owner->scope, name, LookupKind::Any, &found);
			if (member != nullptr)
			{
				return member;
			}
		}
	}
	found.clear();
	return model_.lookup_in(model_.global(), name, LookupKind::Any, &found);
}

// 3.7.4.2p2 and 12.5p4: the deallocation function a delete-expression calls, or
// the one 5.3.4p18 pairs with a new-expression's allocation.
//
// 5.3.5p9 says where to look: the class of the object being destroyed for a
// delete-expression not written with `::`, and the global namespace otherwise
// or where that class declares none.  12.5p4 then says which of what the lookup
// found is the *usual* one - the one taking the storage alone, and failing that
// the one taking the storage and its size.  Every other declaration is a
// placement deallocation function, which only 5.3.4p18's cleanup ever calls and
// which a delete-expression never chooses.
SemaEntity* SemaAnalyzer::deallocation_function(bool global, TypeId destroyed,
                                                bool array,
                                                std::vector<SemaEntity*>& found)
{
	static const std::string kName("operatordelete");
	static const std::string kArrayName("operatordelete[]");
	const std::string& name = array ? kArrayName : kName;
	SemaEntity* reached = nullptr;
	if (!global)
	{
		SemaEntity* const owner = model_.type_owner(types_.strip_cv(destroyed));
		if (owner != nullptr && owner->scope != nullptr)
		{
			reached = model_.lookup_in(*owner->scope, name, LookupKind::Any,
			                           &found);
		}
	}
	if (reached == nullptr)
	{
		found.clear();
		reached = model_.lookup_in(model_.global(), name, LookupKind::Any,
		                           &found);
	}
	if (reached == nullptr)
	{
		return nullptr;
	}
	const TypeId size = types_.fundamental(FT_UNSIGNED_LONG_INT);
	SemaEntity* sized = nullptr;
	for (SemaEntity* at = reached; at != nullptr; at = at->next)
	{
		const std::vector<TypeId>& parameters = types_.parameters(at->type);
		// 9.3.1p3: 12.5p1 makes a member deallocation function static however
		// it was declared, so the storage is its first parameter either way.
		if (parameters.size() == 1)
		{
			return at;
		}
		if (parameters.size() == 2 && types_.strip_cv(parameters[1]) == size)
		{
			sized = at;
		}
	}
	return sized;
}

// 5.3.5p2: an operand of class type is contextually implicitly converted to a
// pointer, which its class has to have exactly one conversion function to.  It
// is 6.4.2p2's question over a different set of destination types, so it is
// answered the same way: one walk of the conversions the class holds, and a
// class that reaches two pointer types reaches no one of them.
void SemaAnalyzer::contextual_pointer(Value& value, const Context& ctx)
{
	if (value.node == nullptr || !types_.is_class(types_.strip_cv(value.type)))
	{
		return;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		return;
	}
	std::vector<SemaEntity*> candidates;
	gather_conversions(*owner, candidates);
	SemaEntity* found = nullptr;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity* const at = candidates[index];
		TypeId result = types_.target(at->type);
		if (types_.is_reference(result))
		{
			result = types_.target(result);
		}
		if (at->explicit_function || at->deleted ||
		    !types_.is_object_pointer(types_.strip_cv(result)))
		{
			continue;
		}
		if (found != nullptr)
		{
			return;
		}
		found = at;
	}
	if (found != nullptr)
	{
		value = call_conversion(value, *found, ctx);
	}
}

// 5.3.5: a delete-expression ends the lifetime of the object its operand points
// to and gives the storage that object stood in back to the deallocation
// function 5.3.5p9 chose.  Both act on storage the operand already names, so
// the tree holds them as such: the destruction is 12.4p3's own action and the
// deallocation is the one function that call names, with the pointer under this
// node as its argument.  The array form is the same two things once per
// element, over the count 5.3.4p1 wrote in front of them.
SemaAnalyzer::Value SemaAnalyzer::delete_expression(const AstNode& node,
                                                    const Context& ctx,
                                                    DumpNode& parent)
{
	bool global = false;
	bool array = false;
	const AstNode* written = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::GlobalScope)
		{
			global = true;
		}
		else if (child.kind == AstKind::ArrayDelete)
		{
			array = true;
		}
		else
		{
			written = &child;
		}
	}
	if (written == nullptr)
	{
		throw std::runtime_error("a delete-expression names no operand");
	}
	DumpNode& line = model_.open_node(parent, std::string());
	Value operand = expression(*written, ctx, line);
	contextual_pointer(operand, ctx);
	if (!types_.is_object_pointer(types_.strip_cv(operand.type)))
	{
		// 5.3.5p2: the operand shall be of pointer to object type, or of a
		// class type with one conversion function to one.
		throw std::runtime_error("a delete-expression is written over an "
		                         "operand of " +
		                         types_.description(operand.type) +
		                         ", which is not a pointer to an object");
	}
	// 5.3.5p2: the object destroyed is the one the pointer points to, and the
	// cv-qualification the pointer carried says nothing about what destroying
	// it comes to.
	const TypeId destroyed = types_.strip_cv(types_.target(operand.type));
	if (types_.is_incomplete(destroyed))
	{
		throw std::runtime_error("a delete-expression destroys an object of the "
		                         "incomplete type " +
		                         types_.description(destroyed));
	}
	// 5.3.5p1: the expression itself is worth nothing, so the type it has says
	// nothing about what it does - and the type of the object it destroys says
	// all of it.  `spelled` is that type, which is what every reader of this
	// node asks for.
	Value value;
	value.type = types_.fundamental(FT_VOID);
	value.spelled = destroyed;
	value.category = ValueCategory::PRValue;
	value.what = "delete-expression";
	value.node = &line;
	respell(value);
	line.fact.array_form = array;
	// 3.7.4.2p2: the storage is given back by the function `line`'s own child
	// names, which is what says whether the size of the object goes with it.
	std::vector<SemaEntity*>& reached = model_.open_overloads();
	SemaEntity* const given =
		deallocation_function(global, destroyed, array, reached);
	if (given == nullptr)
	{
		throw std::runtime_error("a delete-expression is written where no "
		                         "deallocation function is declared");
	}
	Value target;
	target.node = &model_.open_node(line, std::string());
	name_function(target, *given, "callee");
	// 12.4p3: the lifetime of the object ends in a call of the destructor of
	// its class, which 12.4p5 leaves nothing to run where the program wrote no
	// destructor below it.  What the object stands in is the storage the
	// operand points to, so the action names no object of its own.
	SemaEntity* const destructor = class_destructor(destroyed);
	if (destructor == nullptr || destructor->trivial)
	{
		return value;
	}
	if (destructor->deleted)
	{
		throw std::runtime_error("a delete-expression ends the lifetime of an "
		                         "object whose destructor is deleted");
	}
	note_destruction_entry(*destructor, false);
	DumpNode& action = model_.open_node(
		line, "destructor-action " + destructor->dump_name);
	action.fact.kind = FactKind::DestructorAction;
	action.fact.entity = destructor;
	action.fact.type = destroyed;
	return value;
}

// 8.5p6 and 12.1p5: whether default-initializing one object of `element` comes
// to nothing.  It is 12.4p8's question the other way round, and it is asked in
// one place only: the array form of a new-expression writes one loop for
// however many elements there are, and a loop whose body is a call that does
// nothing is written as nothing at all.
//
// A constructor 12.1p5 made trivial does nothing by 12.1p6.  One the program
// wrote does nothing where its body writes no statement and its class holds no
// subobject for a mem-initializer to name - which is what makes "holds nothing"
// the whole of the test rather than a walk of what a definition initializes.
bool SemaAnalyzer::vacuous_construction(TypeId element)
{
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(element));
	if (owner == nullptr)
	{
		return true;
	}
	SemaEntity* const head = class_constructors(types_.strip_cv(element));
	SemaEntity* chosen = nullptr;
	for (SemaEntity* at = head; at != nullptr; at = at->next)
	{
		if (!accepts_arity(*at, 1))
		{
			// 9.3.1p3 put the object among the parameters, so a default
			// constructor is one every parameter after it has a default
			// argument for.
			continue;
		}
		if (chosen != nullptr)
		{
			// 13.3 has two candidates to choose between, and which of them
			// runs is not a question this one asks.
			return false;
		}
		chosen = at;
	}
	if (chosen == nullptr || chosen->deleted)
	{
		return false;
	}
	return chosen->trivial ||
		(chosen->empty_body && owner->empty_class && owner->base == nullptr);
}

// 5.3.4p15: what the elements of an array a new-expression creates come to.
// Every element is initialized the same way, so the tree holds one description
// of one element and 5.3.4p1's count says how many of them there are: for a
// class that is 12.1's constructor call, and for any other type `()` is 8.5p7's
// zero of the storage and no new-initializer at all is 8.5p6's no
// initialization.  The deallocation function 5.3.4p18 pairs with the allocation
// is written beside the call, because 15.2p2's cleanup around a partly built
// array is what gives the storage back.
void SemaAnalyzer::array_new_initialization(TypeId created,
                                            const AstNode* written,
                                            bool global, const Context& ctx,
                                            DumpNode& line)
{
	const TypeId element = element_of(created);
	const bool value_initialized =
		written != nullptr && written->children.empty() &&
		(written->kind == AstKind::ParenInitializer ||
		 written->kind == AstKind::BracedInitList);
	if (written != nullptr && !value_initialized)
	{
		throw std::runtime_error("a new-expression creates an array from an "
		                         "initializer that names a value per element, "
		                         "which 8.5.4 gives its elements one at a time");
	}
	if (!types_.is_class(element))
	{
		// 8.5p7: value-initializing an object of no class type is the zero of
		// its storage, which over the whole array is the zero of every byte.
		line.fact.zero_initialized = value_initialized;
		return;
	}
	if (!value_initialized && vacuous_construction(element))
	{
		return;
	}
	SemaEntity& object =
		model_.create(SemaKind::Variable, std::string(), element);
	object.object_member = false;
	construct_object(object, line, nullptr, ctx, Placement::Named, false,
	                 nullptr, value_initialized);
	std::vector<SemaEntity*>& reached = model_.open_overloads();
	SemaEntity* const given =
		deallocation_function(global, element, true, reached);
	if (given == nullptr)
	{
		throw std::runtime_error("a new-expression creates an array where no "
		                         "deallocation function 5.3.4p18 pairs with "
		                         "its allocation function is declared");
	}
	Value target;
	target.node = &model_.open_node(line, std::string());
	name_function(target, *given, "callee");
}

// 5.3.4p6: the type a new-expression creates, and the expression that says how
// many objects of it the array form asks for.  A new-declarator's first
// array-suffix is the one dimension the standard lets be written with an
// expression that is not a constant, and every dimension after it is part of
// the type one element has - so the first is handed back as the expression it
// is and the rest are read the way any declarator's suffixes are.  A
// new-expression that writes no array-suffix at all leaves `bound` null and the
// type is what the type-id says.
TypeId SemaAnalyzer::new_type(const AstNode& type_id, const Context& ctx,
                              const AstNode*& bound)
{
	bound = nullptr;
	const AstNode* const seq = child_kind(type_id, AstKind::TypeSpecifierSeq);
	if (seq == nullptr)
	{
		throw std::runtime_error("a new-expression names no type");
	}
	Span span;
	span.begin = type_id.begin;
	span.end = type_id.end;
	Specifiers specifiers = read_specifiers(*seq, ctx, span, false, std::string());
	TypeId type = specifier_type(specifiers);
	const AstNode* declarator = child_kind(type_id, AstKind::Declarator);
	if (declarator == nullptr)
	{
		declarator = child_kind(type_id, AstKind::AbstractDeclarator);
	}
	if (declarator == nullptr)
	{
		return type;
	}
	// 8.3: the pointer operators written before the suffixes apply to what the
	// specifiers named, exactly as they do in any declarator.
	std::size_t index = 0;
	while (index < declarator->children.size() &&
	       (declarator->children[index]->kind == AstKind::PtrOperator ||
	        declarator->children[index]->kind == AstKind::CvQualifier))
	{
		const AstNode& part = *declarator->children[index];
		type = part.kind == AstKind::CvQualifier
			? types_.qualified(type, part.token == KW_CONST ? kCvConst
			                                                : kCvVolatile)
			: apply_pointer(part, type, ctx);
		++index;
	}
	if (index < declarator->children.size() &&
	    declarator->children[index]->kind == AstKind::ArraySuffix &&
	    !declarator->children[index]->children.empty())
	{
		bound = declarator->children[index]->children[0];
		++index;
	}
	for (std::size_t suffix = declarator->children.size(); suffix-- > index;)
	{
		type = apply_suffix(*declarator->children[suffix], type, ctx, nullptr);
	}
	return type;
}

// 5.3.4p8 for one object: the bytes it occupies, written as the integer literal
// 2.14.2p2 gives a value of that size.
SemaAnalyzer::Value SemaAnalyzer::object_size_value(unsigned long long bytes,
                                                    DumpNode& parent)
{
	Value size;
	size.type = size.spelled =
		types_.fundamental(bytes <= 0x7FFFFFFFull ? FT_INT : FT_LONG_INT);
	size.category = ValueCategory::PRValue;
	size.constant = true;
	size.value = bytes;
	size.what = "literal";
	size.payload = decimal(bytes);
	size.node = &model_.open_node(
		parent, spell(size.what, size.category, size.type, size.payload));
	record(size);
	return size;
}

// 5.3.4p8 for the array form: the bytes `bound` elements of `element` occupy,
// with `cookie` more in front of them where the count is kept.
//
// 5.3.4p6 converts the count to a `std::size_t` where it is *passed*, so the
// scaling is done in the type the count expression has and reaches the
// parameter through the one conversion 5.2.2p4 gives any argument - which is
// what makes `n * sizeof(T)` for an `unsigned` count the unsigned arithmetic a
// program writing it would get.  A count the translation knows is one number,
// and `elements` hands back how many objects that is so the initialization and
// the count written in front of them need no second reading of the operand.
SemaAnalyzer::Value SemaAnalyzer::array_new_size(const AstNode& bound,
                                                 TypeId element,
                                                 unsigned long long cookie,
                                                 const Context& ctx,
                                                 DumpNode& parent,
                                                 unsigned long long& elements)
{
	const unsigned long long stride = size_of(element);
	bool known = false;
	unsigned long long count = 0;
	try
	{
		// 5.19: a count the translation knows makes the bytes one number too,
		// and the whole product is the one literal it comes to.  It is asked
		// for before anything is written, because a constant leaves no
		// arithmetic in the tree at all.
		count = evaluate(bound, ctx).bits;
		known = true;
	}
	catch (const std::runtime_error&)
	{
		// 5.3.4p6: the count of the array form is the one array bound the
		// standard does not require to be a constant expression, so an
		// expression the constant layer cannot fold is read as a value here.
	}
	if (known)
	{
		elements = count * array_element_count(element);
		return object_size_value(count * stride + cookie, parent);
	}
	elements = 0;
	// The scaling stands above the count, so its nodes are opened before the
	// operand is read into the innermost of them.
	DumpNode* under = &parent;
	DumpNode* const sum =
		cookie == 0 ? nullptr : &model_.open_node(*under, std::string());
	under = sum != nullptr ? sum : under;
	DumpNode* const product =
		stride == 1 ? nullptr : &model_.open_node(*under, std::string());
	under = product != nullptr ? product : under;
	Value value = expression(bound, ctx, *under);
	const TypeId counted = types_.strip_cv(value.type);
	if (!types_.is_integral(counted) && types_.kind(counted) != TypeKind::Enum)
	{
		throw std::runtime_error("the number of elements a new-expression "
		                         "creates is not of integral type");
	}
	if (product == nullptr && sum == nullptr)
	{
		// 5.3.4p8: each element occupies one byte and no count is written in
		// front of them, so the bytes the call asks for are the count itself.
		return value;
	}
	value.type = value.spelled = promoted(value.type);
	value.category = ValueCategory::PRValue;
	value.constant = false;
	value.value = 0;
	value.entity = nullptr;
	value.functions = nullptr;
	value.name = nullptr;
	value.addressed = nullptr;
	value.payload.clear();
	value.what = "binary-expression";
	if (product != nullptr)
	{
		const Value factor = object_size_value(stride, *product);
		value.type = value.spelled = value.operands =
			arithmetic_result(value.type, factor.type);
		value.op = OP_STAR;
		value.node = product;
		respell(value);
	}
	if (sum != nullptr)
	{
		const Value extra = object_size_value(cookie, *sum);
		value.type = value.spelled = value.operands =
			arithmetic_result(value.type, extra.type);
		value.op = OP_PLUS;
		value.node = sum;
		respell(value);
	}
	return value;
}

// 8.3.4p1: how many objects of the type one element of an array of `element` is
// there are in one element of it - one for a type that is no array, and the
// product of the bounds otherwise.
unsigned long long SemaAnalyzer::array_element_count(TypeId element)
{
	unsigned long long total = 1;
	TypeId at = types_.strip_cv(element);
	while (types_.kind(at) == TypeKind::Array)
	{
		total *= types_.bound(at);
		at = types_.strip_cv(types_.target(at));
	}
	return total;
}

// 5.3.4: a new-expression creates one object with dynamic storage duration, and
// the resolved tree holds the two things that are: 5.3.4p8's call of an
// allocation function for the bytes the object occupies, and 5.3.4p12's
// initialization of an object of the type at the address that call returned.
// What the expression is worth is that address, so the object is built where
// the allocation put it and the function it was written in gives it no storage
// of its own - which is the one way this initialization differs from the one a
// declaration of the same object would carry.
SemaAnalyzer::Value SemaAnalyzer::new_expression(const AstNode& node,
                                                 const Context& ctx,
                                                 DumpNode& parent)
{
	bool global = false;
	const AstNode* placement = nullptr;
	const AstNode* written_type = nullptr;
	const AstNode* written_init = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::GlobalScope)
		{
			global = true;
		}
		else if (child.kind == AstKind::Placement)
		{
			placement = &child;
		}
		else if (child.kind == AstKind::TypeId)
		{
			written_type = &child;
		}
		else if (child.kind == AstKind::Initializer && !child.children.empty())
		{
			written_init = child.children[0];
		}
	}
	if (written_type == nullptr)
	{
		throw std::runtime_error("a new-expression names no type");
	}
	const AstNode* bound = nullptr;
	const TypeId created = new_type(*written_type, ctx, bound);
	const bool array = bound != nullptr;
	if (types_.is_incomplete(created))
	{
		// 5.3.4p1: the type shall be complete, which is what has the size
		// 5.3.4p8 passes.
		throw std::runtime_error("a new-expression creates an object of the "
		                         "incomplete type " +
		                         types_.description(created));
	}
	DumpNode& line = model_.open_node(parent, std::string());

	// 5.3.4p8: the allocation function is called with the number of bytes the
	// objects it creates occupy, and with the new-placement's own arguments
	// after it.  For one object that is one number the translation knows, so it
	// is written with the type 2.14.2p2 gives an integer literal of that value
	// and reaches the parameter through the conversion 5.2.2p4 gives any
	// argument - which is what leaves `std::size_t` a type this unit needs no
	// name for.
	DumpNode& call = model_.open_node(line, std::string());
	DumpNode& callee = model_.open_node(call, std::string());
	// 5.3.4p1 and the ABI: an array of class type is handed back with the
	// number of its elements written in front of them, because 5.3.5p2's array
	// delete has to run one destructor per element and the pointer alone does
	// not say how many those are.
	const unsigned long long cookie =
		array && types_.is_class(element_of(created)) ? kArrayCookieBytes : 0;
	std::vector<Value> arguments;
	unsigned long long elements = 0;
	const Value size = array
		? array_new_size(*bound, created, cookie, ctx, call, elements)
		: object_size_value(size_of(created), call);
	arguments.push_back(size);
	const AstNode* const written_placement =
		placement == nullptr ? nullptr : arguments_of(*placement);
	for (std::size_t index = 0;
	     written_placement != nullptr &&
	     index < written_placement->children.size(); ++index)
	{
		arguments.push_back(
			expression(*written_placement->children[index], ctx, call));
	}
	std::vector<SemaEntity*>& reached = model_.open_overloads();
	SemaEntity* const declared =
		allocation_function(global, element_of(created), array, reached);
	if (declared == nullptr)
	{
		throw std::runtime_error("a new-expression is written where no "
		                         "allocation function is declared");
	}
	// 13.3p1: the candidate set is what that lookup reached, and the arguments
	// choose one of them exactly as they would for a call the program wrote.
	std::vector<SemaEntity*> candidates;
	if (reached.empty())
	{
		candidates.push_back(declared);
	}
	else
	{
		candidates = reached;
	}
	Value target;
	target.node = &callee;
	name_function(target, *select_overload(candidates, arguments, "operator new"),
	              "callee");
	const Value allocated = finish_call(call, target.type, arguments,
	                                    target.entity, ctx);
	if (types_.kind(types_.strip_cv(allocated.type)) != TypeKind::Pointer)
	{
		// 3.7.4.1p2: an allocation function returns the address of the storage
		// it obtained, which is what the object is then created at.
		throw std::runtime_error("the allocation function a new-expression "
		                         "calls does not return a pointer");
	}

	// 5.3.4p1: the value is a pointer to the object created, which is the
	// address the allocation function returned read at the type of that
	// object.  For the array form 5.3.4p5 makes it a pointer to the first
	// element, and the elements after it are the same objects one at a time.
	Value value;
	value.type = value.spelled = types_.pointer_to(created);
	value.category = ValueCategory::PRValue;
	value.what = "new-expression";
	value.node = &line;
	respell(value);
	line.fact.array_form = array;
	line.fact.elements = elements;

	if (array)
	{
		// 5.3.4p15: the elements are default-initialized where no initializer
		// was written and value-initialized where `()` was, and every one of
		// them is initialized the same way - so the tree holds one description
		// of what one element comes to and the count says how many.
		array_new_initialization(created, written_init, global, ctx, line);
		return value;
	}

	// 5.3.4p15 and 8.5p16: the object at that address is initialized by the
	// new-initializer, which is the same initialization a declaration of an
	// object of the type with the same initializer written on it would be.
	if (types_.is_class(types_.strip_cv(created)))
	{
		// 8.5p7: `{}` value-initializes the object rather than naming a clause
		// for any member, which is the zero of its storage and the default
		// constructor its class was given, so it is not the list 8.5.1 reads.
		const bool braced = written_init != nullptr &&
			written_init->kind == AstKind::BracedInitList &&
			!written_init->children.empty();
		SemaEntity* const from_members = braced && aggregate_type(created)
			? member_constructor(created)
			: nullptr;
		if (from_members != nullptr)
		{
			// 8.5.4p3 and 13.3.1.7: the class is an aggregate, so its clauses
			// initialize its members - and what they initialize is an object of
			// its own rather than a subobject an enclosing list reaches into,
			// so it is built by the constructor 8.5.1 gives the class from its
			// non-static data members.
			construct_from_members(*from_members, *written_init, ctx, line);
		}
		else
		{
			// 12.1 and 13.3.1.3: any other class object is built by one of the
			// constructors its class declared, chosen from what the
			// new-initializer wrote.  The object is one no name reaches, which
			// is what leaves the address the allocation returned as the only
			// thing that says where it stands.
			SemaEntity& object =
				model_.create(SemaKind::Variable, std::string(), created);
			object.object_member = false;
			construct_object(object, line, written_init, ctx, Placement::Named);
		}
		if (line.children.size() != 2 ||
		    line.children[1]->fact.kind != FactKind::ConstructorAction)
		{
			// 12.8p31's elision reaches an object the place asking already owns
			// storage for, and here the storage is one the allocation function
			// chose, so a value of the class's own type has nowhere to be
			// elided into.
			throw std::runtime_error("a new-expression initializes an object of "
			                         "class type from a value of that class, "
			                         "which 12.8p1 makes a call of the copy "
			                         "constructor its program wrote");
		}
	}
	else if (written_init != nullptr)
	{
		// 8.5p16 over a scalar: the object holds what the one clause of the
		// initializer says, and 8.5p7's `()` and `{}` hold the zero of the
		// type.
		const bool braced = written_init->kind == AstKind::BracedInitList;
		if ((!braced && written_init->kind != AstKind::ParenInitializer) ||
		    written_init->children.size() > 1)
		{
			throw std::runtime_error("a new-expression initializes an object of "
			                         "non-class type with more than one value");
		}
		if (written_init->children.empty())
		{
			// 8.5p7: `()` and `{}` value-initialize the object, which for a
			// scalar is the zero of its type.  It is the zero of an object
			// rather than a literal the program wrote, which is what says a
			// pointer takes 4.10p1's null pointer value and not the `0` a null
			// pointer constant is spelled with.
			DumpNode& zero = model_.open_node(
				line, spell("literal", ValueCategory::PRValue, created, "0"));
			set_fact(zero, FactKind::Literal, created, ValueCategory::PRValue);
			zero.fact.constant = true;
			zero.fact.zero_initialized = true;
		}
		else
		{
			initialize(*written_init->children[0], created, ctx, line, braced);
		}
	}
	return value;
}
