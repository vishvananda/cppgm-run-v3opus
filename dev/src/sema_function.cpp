#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "token_model.h"

// 8.4 and 13.1: what a function declaration declares, and what its definition
// reads.
//
// A declarator that made a function type is where four of the standard's
// questions meet: 13.1p2 asks whether the region already declares this
// function, 11.3p6 lets a friend declaration declare one into a region no
// lookup written there reaches, 8.3.5p4's parameter-type-list is what the two
// are compared by, and 8.4.1p1's body is read against the parameters that list
// declared.  So one owner answers all four, and the walk that reads a
// declaration hands it a declarator and takes back a declaration.
//
// 14.6p8 puts a fifth beside them: a definition written under a
// template-parameter-clause declares no function at all, and is still read for
// what it says about the names it writes.

// 6.6.1, 6.6.3 and 12.2p3: the reading of one function body, which puts aside
// what the reading around it knew and gives it back.
//
// Every one of these is a fact about the function being read and about nothing
// else: what `this` denotes, what a return converts to, whether a break or a
// continue has a statement to leave, which lifetimes are open, and which labels
// the body wrote for its gotos to name.  Holding them here is what lets a
// specialization named in the middle of one body be read as a body of its own.
SemaAnalyzer::FunctionReading::FunctionReading(SemaAnalyzer& analyzer,
                                               SemaEntity* self, TypeId returns)
	: analyzer_(analyzer)
	, self_(analyzer.self_)
	, returns_(analyzer.returns_)
	, breakable_(analyzer.breakable_)
	, continuable_(analyzer.continuable_)
	, switches_(analyzer.switches_)
	, live_destructions_(analyzer.live_destructions_)
{
	lifetimes_.swap(analyzer_.lifetimes_);
	breakable_frames_.swap(analyzer_.breakable_frames_);
	continuable_frames_.swap(analyzer_.continuable_frames_);
	parameter_objects_.swap(analyzer_.parameter_objects_);
	labels_.swap(analyzer_.labels_);
	gotos_.swap(analyzer_.gotos_);
	analyzer_.self_ = self;
	analyzer_.returns_ = returns;
	analyzer_.breakable_ = 0;
	analyzer_.continuable_ = 0;
	analyzer_.switches_ = 0;
	analyzer_.live_destructions_ = 0;
}

SemaAnalyzer::FunctionReading::~FunctionReading()
{
	analyzer_.self_ = self_;
	analyzer_.returns_ = returns_;
	analyzer_.breakable_ = breakable_;
	analyzer_.continuable_ = continuable_;
	analyzer_.switches_ = switches_;
	analyzer_.live_destructions_ = live_destructions_;
	analyzer_.lifetimes_.swap(lifetimes_);
	analyzer_.breakable_frames_.swap(breakable_frames_);
	analyzer_.continuable_frames_.swap(continuable_frames_);
	analyzer_.parameter_objects_.swap(parameter_objects_);
	analyzer_.labels_.swap(labels_);
	analyzer_.gotos_.swap(gotos_);
}

// 15.4p1: two declarations of one function shall agree about what it throws.
//
// A declaration that wrote an exception-specification and one that wrote none
// are two different declarations of one function, which 15.4p1 refuses however
// they are ordered - so the question is asked wherever a declaration redeclares
// one the region already made, and what a declaration says is a fact of that
// declaration rather than of the function they both declare.
void SemaAnalyzer::require_matching_exception_specification(
	const SemaEntity& declared, bool wrote, bool nothrowing,
	const std::string& name)
{
	if (wrote != declared.wrote_exception_specification ||
	    (wrote && nothrowing != declared.nonthrowing))
	{
		throw std::runtime_error("two declarations of " + name + " write "
		                         "exception-specifications 15.4p1 does not "
		                         "make the same");
	}
}

// 6.6.4p1: every label a goto of this function names is one the function
// writes.  The two lists are the body's own, so the question is asked where the
// body ends and nowhere else.
void SemaAnalyzer::require_labelled_gotos()
{
	for (std::size_t index = 0; index < gotos_.size(); ++index)
	{
		if (labels_.count(gotos_[index]) == 0)
		{
			throw std::runtime_error("a goto statement names " + gotos_[index] +
			                         ", which labels no statement of the "
			                         "function");
		}
	}
}

void SemaAnalyzer::declare_parameters(const std::vector<Parameter>& parameters,
                                      TypeId type, const Context& inner,
                                      DumpNode* node, std::size_t implicit)
{
	// 8.4.1p1: the parameters the declarator's own parameter-clause declared,
	// which the type it built already read.  9.3.1p3 put the implicit object
	// parameter before them, so the two lists start apart.
	const std::vector<TypeId>& adjusted = types_.parameters(type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		const TypeId written = index + implicit < adjusted.size()
			? adjusted[index + implicit]
			: parameters[index].type;
		// 8.3.5p5: a parameter written as an array or as a function is a pointer,
		// so the object the body names is one - which is what says a use of it
		// loads the pointer the caller passed rather than 4.2's or 4.3's view of
		// an object standing where the parameter does.  The cv-qualifiers that
		// clause drops are dropped from the *function type* and not from the
		// object: a by-value parameter written `const` is a const object, and
		// which of 13.3's overloads a call on it reaches is that object's
		// question.  PA11 describes the declarator as it was written.
		const TypeId held = semantics()
			? types_.parameter_object(parameters[index].type)
			: parameters[index].type;
		// 8.3.5p10: the object is spelled with the function's name for the
		// place, which is this clause's own where it wrote one and otherwise
		// the first name any declaration of the function gave.  Only the
		// spelling travels: what a declarator did not write is bound nowhere
		// and 14.6.1p6 is asked about nothing, because 3.3.4 ends a
		// declaration's parameter names at its own declarator.
		SemaEntity& parameter = model_.create(
			SemaKind::Parameter,
			parameters[index].name.empty() ? parameters[index].object_name
			                               : parameters[index].name,
			held);
		if (!parameters[index].name.empty())
		{
			require_no_template_parameter(parameters[index].name, *inner.scope);
			model_.bind(*inner.scope, parameters[index].name, parameter);
		}
		model_.declare_in(*inner.scope, parameter);
		if (lowering() && parameter.name.empty() &&
		    inner.scope->kind == ScopeKind::Function &&
		    inner.scope->owner != nullptr &&
		    inner.scope->owner->primary == nullptr)
		{
			// No declaration read so far named this place, and one written
			// below this definition may still be the first to - so the object
			// waits on the function's own record for that name.
			//
			// 14.7.1p1: a specialization waits for nothing.  Its object is
			// made from the pattern whose definition was read, so the names
			// it can be spelled with are the ones the template's declarations
			// had already given - a declaration of the *template* written
			// below that definition declares the template, and renames
			// nothing the specialization has already made.
			std::vector<ParameterRecord>& record =
				defaults_[wrote_defaults(*inner.scope->owner).id];
			const std::size_t at = index + implicit;
			record.resize(at + 1 > record.size() ? at + 1 : record.size());
			record[at].objects.push_back(&parameter);
		}
		if (node != nullptr)
		{
			DumpNode& line = open_fact(*node, "parameter " + parameter.name + " " +
			                           types_.description(written),
			                           FactKind::Parameter);
			line.fact.entity = &parameter;
			line.fact.type = written;
			continue;
		}
		write_line(*inner.dump, "parameter", parameter.name, parameter.type);
	}
}

void SemaAnalyzer::function_definition(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const AstNode& declarator = *node.children[1];
	const AstNode* id = declarator_id(declarator);
	const std::string written = id == nullptr ? std::string() : id->text;
	// 14.7.1p1: the specialization this reading is of, taken before anything
	// else is read.  Reading a specifier or a declarator can name another
	// specialization - a dependent qualified name in the return type
	// instantiates the class it is a member of, and every member function that
	// class defines in its body is read there and then - so the fact belongs to
	// this reading rather than to the analyzer for as long as it lasts, and a
	// definition read in the middle of this one is the ordinary one it is.
	SemaEntity* const specializing = instantiating_;
	instantiating_ = nullptr;
	// 11p6: a member function defined outside its class names, in its leading
	// return type as much as in its body, what that class gave itself.
	const Naming naming(*this, naming_context(written, ctx));
	Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
	const QualifiedName spelled(written);
	const std::string name = spelled.last();

	// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
	// looked up in the region that name reaches.
	Context target = ctx;
	// 14.1p1 and 3.4.1p8: a template-declaration whose declarator-id is
	// qualified declares into the region that name reaches, and the parameters
	// its own head declared stand over that region for as long as the
	// declarator and the body are read - which is the same place 14.5.1.3p1's
	// out-of-class member definition puts its head's names.  The head is this
	// declaration's own: a definition read inside this one opens a context of
	// its own and stands over nothing.
	Scope* const head =
		spelled.qualified() && ctx.template_head == ctx.scope ? ctx.scope
		                                                      : nullptr;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
		target.template_head = head;
	}
	const EnclosedBy enclosed(*target.scope, head);
	// 11.3p6: a friend function defined in a class body is a member of the
	// region around the class.  3.4.1p9 still reads the names in its body as a
	// member function's are read, so the region its parameters and body are
	// written in is enclosed by the class while the declaration is not.
	Scope* const lexical = ctx.scope;
	SemaEntity* const granting =
		specifiers.is_friend ? friend_target(ctx, spelled, target) : nullptr;

	std::string ignored;
	std::vector<Parameter> parameters;
	// 3.4.1p8 and 3.4.1p9: the rest of a declarator is read where the
	// declarator-id names it - in the region a qualified one reaches, and
	// otherwise where the declaration stands, which for a friend declaration is
	// the class it is written in and not the namespace it declares into.
	TypeId type = declarator_type(declarator, specifier_type(specifiers),
	                              spelled.qualified() ? target : ctx, &ignored,
	                              &parameters,
	                              declares_object_member(specifiers));
	if (types_.kind(type) != TypeKind::Function)
	{
		throw std::runtime_error("a function definition declares " + name +
		                         ", which is not a function");
	}
	require_mutable_data_member(specifiers, target, name, type);
	// 9.3.1p3: a member function is called on an object its declarator does not
	// write, whether it is defined in its class or after it.
	const TypeId written_type = type;
	type = with_object_parameter(type, declarator, target, specifiers.is_static,
	                             name, spelled.qualified());

	bool redeclares = false;
	SemaEntity& entity =
		declare_function(name, type, target, true,
		                 granting != nullptr && !spelled.qualified(),
		                 type != written_type,
		                 spelled.qualified() && granting == nullptr,
		                 specializing, &redeclares);
	const bool wrote_specification =
		declarator_writes_exception_specification(declarator);
	const bool nothrowing = declarator_nonthrowing(declarator);
	if (redeclares && specializing == nullptr)
	{
		require_matching_exception_specification(entity, wrote_specification,
		                                         nothrowing, name);
	}
	entity.nonthrowing = entity.nonthrowing || nothrowing;
	entity.wrote_exception_specification =
		entity.wrote_exception_specification || wrote_specification;
	entity.object_member = type != written_type;
	// 10.3p1 and 10.3p4/p5: the definition is a declaration like any other, so
	// what it wrote about dispatch stands for the function whether or not
	// another declaration of it wrote the same thing - and 7.1.2p1 lets the one
	// written in the class body write `virtual` and the one written outside it
	// not.
	require_virtual_placement(specifiers.is_virtual, &declarator, *target.scope,
	                          spelled.qualified(), name);
	entity.virtual_function =
		entity.virtual_function || specifiers.is_virtual;
	read_virt_specifiers(entity, declarator, nullptr);
	require_no_abstract_boundary(written_type, name);
	if (!entity.object_member)
	{
		require_operator_operand(name, type,
		                         target.scope->kind == ScopeKind::Class);
	}
	if (granting != nullptr)
	{
		model_.befriend(*granting, entity);
		if (!spelled.qualified() && granting->scope != nullptr)
		{
			granting->scope->friend_functions.push_back(&entity);
			// 11.3p5: the definition declares a member of the enclosing
			// namespace and is written where no ordinary lookup finds its
			// name, so the class that wrote it is where this unit reads it.
			entity.friend_definition =
				entity.friend_definition || ctx.scope->kind == ScopeKind::Class;
		}
	}
	// 3.5p3: one declaration written `static` gives the name internal linkage,
	// however the others were written.
	// 3.5p4: so does 7.3.1.1p1's unnamed namespace, which the definition may
	// stand in without any declaration of it writing a specifier.
	entity.internal_linkage = entity.internal_linkage ||
		target.scope->unnamed_region ||
		(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
	// 7.1.2p2 and 9.3p2: `inline` says so, and so does defining a member
	// function inside the class definition - which is where the definition is
	// written, not the region it declares into.  A member defined outside its
	// class declares into that class and is a definition this unit owns like
	// any other: it binds strongly and is emitted whether or not this unit uses
	// it.
	entity.inline_function = entity.inline_function || specifiers.is_inline ||
		ctx.scope->kind == ScopeKind::Class;
	record_declared_parameters(entity, parameters, target.scope);

	DumpScope& dump = model_.open_dump(*target.dump, "scope function " + name);
	Context inner;
	inner.scope = &model_.open(ScopeKind::Function,
	                           granting != nullptr ? *lexical : *target.scope,
	                           &entity, &dump);
	inner.dump = &dump;
	inner.node = ctx.node;

	if (!semantics())
	{
		write_line(*target.dump, "function", name, type);
		if (checking_ > 0 && ctx.scope->kind == ScopeKind::Class)
		{
			// 9.2p2: a member function body written inside the class body is a
			// complete-class context, so it is read where the class is
			// complete rather than where it stands - which is what lets it
			// name a member the class declares below it.
			hold_pattern_body(node, inner, parameters, type);
			return;
		}
		declare_parameters(parameters, type, inner, nullptr);
		for (std::size_t index = 2; index < node.children.size(); ++index)
		{
			statement(*node.children[index], inner);
		}
		return;
	}

	// 9.3.1p3 and 9.2p2: a member function is called on an object, which is
	// declared in the region its body reads names in, and its body is read
	// where the class is complete rather than where it is written.
	SemaEntity* self = nullptr;
	if (entity.object_member)
	{
		self = &model_.create(SemaKind::Parameter, "this",
		                      types_.parameters(type)[0]);
		model_.bind(*inner.scope, self->name, *self);
		model_.declare_in(*inner.scope, *self);
	}
	if ((target.scope->kind == ScopeKind::TemplateParameters || head != nullptr) &&
	    specializing == nullptr)
	{
		// 14p1 and 14.6: a template declares no function until it is
		// instantiated, so the output has no definition to write and the body
		// is not read against the types it has none of yet.  A reading of the
		// pattern *for* an instantiation stands in the same kind of region -
		// the one that binds its arguments - and is the declaration.
		//
		// 14.6p8 still reads it: what the body says about names that depend on
		// no template parameter is settled here, where it stands.
		check_template_definition(node, inner, parameters, type);
		return;
	}
	if (target.node == nullptr)
	{
		// 9.2p2: a member function defined in its class is read where the class
		// is complete, which is the end of the translation unit, and the output
		// writes it there.
		Pending pending;
		pending.function = &entity;
		pending.self = self;
		pending.body = &node;
		pending.scope = inner.scope;
		pending.parameters = parameters;
		queue_definition(pending);
		return;
	}

	DumpNode& line = open_fact(*target.node, "function-definition " +
	                           entity.dump_name + " " +
	                           function_description(type, entity.object_member),
	                           FactKind::FunctionDefinition);
	line.fact.entity = &entity;
	line.fact.type = type;
	if (self != nullptr)
	{
		// A member function defined after its class is written where it is
		// written, and the object it is called on is still its first parameter.
		DumpNode& object = open_fact(line, "parameter " + self->name + " " +
		                             types_.description(self->type),
		                             FactKind::Parameter);
		object.fact.entity = self;
		object.fact.type = self->type;
	}
	declare_parameters(parameters, type, inner, &line, self != nullptr ? 1 : 0);

	// 6.6.3, 6.6.1 and 6.6.2 are facts about the function being read, so the
	// walk of one body neither sees nor leaves behind what encloses it.
	const FunctionReading reading(*this, self, types_.target(type));
	// 5.2.2p4: the parameters of class type this definition has to end are read
	// once, off the lines just written, before the body that may return.
	open_parameter_lifetimes(line);
	for (std::size_t index = 2; index < node.children.size(); ++index)
	{
		semantic_statement(*node.children[index], inner, line);
	}
	// 6.6.3p2 and 3.8p1: control reaching the end of the body leaves the
	// function as a return does, so what a return would end is ended there too.
	end_parameter_lifetimes(line);
	require_labelled_gotos();
}

// 7.1.1p10: `mutable` may be written only on a non-static data member whose
// type is neither const-qualified nor a reference.  What it says is a fact about
// what the declaration declares, so every declaration is asked and not only the
// one that goes on to declare an object: a member function - declared or
// defined - a typedef, a static data member and a declaration of no class at
// all each declare something the specifier says nothing about.
void SemaAnalyzer::require_mutable_data_member(const Specifiers& specifiers,
                                               const Context& target,
                                               const std::string& name,
                                               TypeId type)
{
	if (!specifiers.is_mutable)
	{
		return;
	}
	if (target.scope->kind != ScopeKind::Class || specifiers.is_static ||
	    specifiers.is_typedef || specifiers.is_friend ||
	    types_.kind(type) == TypeKind::Function ||
	    (types_.cv(type) & kCvConst) != 0 || types_.is_reference(type))
	{
		throw std::runtime_error(
			name + " is declared `mutable`, which 7.1.1p10 allows only for a "
			"non-static data member of neither const-qualified nor reference "
			"type");
	}
}

// 9.3.1p3 put the object parameter of a non-static member function in its type,
// and 8.3.5p4's parameter-type-list is what a declarator wrote - so the two
// declarations `void unlink();` and `static void unlink(block*);` of one class
// have one function type and are two functions.  13.1's index is keyed by the
// list the declarator wrote wherever a class is what declares the name, with
// 8.3.5p7's cv-qualifier-seq beside it, which is the same key 7.3.3p14's hiding
// already asks with.  A namespace declares no function with an object
// parameter, so there the type's own list is the list and costs no rebuild.
std::uint32_t SemaAnalyzer::declaration_signature(const Scope& where,
                                                  TypeId type,
                                                  bool object_member)
{
	return where.kind == ScopeKind::Class
		? member_signature(type, object_member)
		: types_.signature(type);
}

// 13.1p2: a class shall not declare a member function with a ref-qualifier and
// one without where the two have the same name and the same parameter-type-list,
// because 13.3.1p5's rule that an unqualified member binds an rvalue too would
// leave a call on an rvalue with no way to choose between them.
//
// 8.3.5p4's parameter-type-list is the types the declarator wrote, which
// 8.3.5p7's cv-qualifier-seq is no part of - so `f() const` and `f() &&` are a
// pair this refuses just as `f()` and `f() &&` are, and the declaration asked
// about may have written any of the four qualifications.  The chain the name
// heads is indexed by both qualifiers along with the rest of the signature, so
// each is one further probe of that index rather than a walk of the
// declarations already made.
void SemaAnalyzer::require_uniform_ref_qualifiers(const SemaEntity& head,
                                                  const std::string& name,
                                                  TypeId type)
{
	static const unsigned kQualifications[] = {
		kCvNone, kCvConst, kCvVolatile, kCvConst | kCvVolatile
	};
	static const RefQualifier kSpellings[] = {
		RefQualifier::LValue, RefQualifier::RValue
	};
	// Two ref-qualified declarations are two functions 13.3.1p4 tells apart by
	// the category the object argument has, so what a declaration that wrote one
	// asks about is the unqualified spelling alone, and what a declaration that
	// wrote none asks about is either of the two.
	const bool qualified =
		types_.function_ref_qualifier(type) != RefQualifier::None;
	const std::vector<TypeId>& written = types_.parameters(type);
	std::vector<TypeId> parameters(written);
	const TypeId object = types_.strip_cv(types_.target(written[0]));
	for (std::size_t index = 0; index < 4; ++index)
	{
		parameters[0] =
			types_.pointer_to(types_.qualified(object, kQualifications[index]));
		const TypeId probe = types_.function_of(types_.target(type), parameters,
		                                        types_.variadic(type));
		for (std::size_t spelling = 0; spelling < (qualified ? 1u : 2u);
		     ++spelling)
		{
			const TypeId other = types_.ref_qualified_function(
				probe, qualified ? RefQualifier::None : kSpellings[spelling]);
			if (model_.overload_of(head, member_signature(other, true)) !=
			    nullptr)
			{
				throw std::runtime_error(
					"a class declares " + name +
					" both with and without a ref-qualifier, which 13.1p2 does "
					"not allow");
			}
		}
	}
}

SemaEntity& SemaAnalyzer::declare_function(const std::string& name, TypeId type,
                                           const Context& target, bool define,
                                           bool hidden, bool object_member,
                                           bool redeclaration, SemaEntity* as,
                                           bool* redeclares)
{
	if (as != nullptr)
	{
		// 14.7.1p1: the declaration this reading is of was made where the
		// template-id or the call named it, and reading the pattern again for
		// its definition declares nothing further.  The type the pattern makes
		// against the bound arguments is the type that declaration has.
		as->type = type;
		as->defined = define;
		return *as;
	}
	// 14.1p1: the region a template's parameters are declared in encloses only
	// the declaration they parameterise, so the function that declaration
	// declares is declared in the region around it, which is where a call of it
	// looks and where its other declarations are.
	Scope& where = declaring_region(*target.scope);
	// 14.1p1 and 3.4.1p8: which head this declaration is written under, which
	// is the region it is read in for every declarator-id but a qualified one -
	// and for that one the head standing over the region the name reaches.
	Scope* const head_region =
		target.scope->kind == ScopeKind::TemplateParameters ? target.scope
		                                                    : target.template_head;
	// 14.6.1p6: the name is written inside the template-declaration the head
	// parameterises, however far out the region that declares it stands - so
	// the question is asked of where the declaration was *written* and not of
	// the region `declaring_region` steps out to.
	require_no_template_parameter(name, *target.scope);
	SemaEntity* head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind != SemaKind::Function)
	{
		head = nullptr;
	}
	const std::uint32_t signature = declaration_signature(where, type,
	                                                      object_member);
	// 1.3.11 and 13.1: two declarations declare the same function exactly when
	// their parameter type lists agree, which 8.3.5p5 has already normalised.
	// The chain the name heads is indexed by that list, so the question is a
	// probe rather than a walk of the declarations already made.
	SemaEntity* prior =
		head == nullptr ? nullptr : model_.overload_of(*head, signature);
	// 11.3p6: a friend declaration declared this function into this region
	// without binding its name, so the chain the name heads is not the only
	// place a declaration of it can be.
	const std::unordered_map<std::string, SemaEntity*>::iterator concealed =
		where.hidden.empty() ? where.hidden.end() : where.hidden.find(name);
	if (prior == nullptr && concealed != where.hidden.end())
	{
		prior = model_.overload_of(*concealed->second, signature);
		if (prior != nullptr && !hidden)
		{
			// 7.3.1.2p3: a matching declaration at namespace scope is what
			// makes the friend's name visible, and the two declare one
			// function.
			reveal_friend(where, name, *prior, signature);
		}
	}
	if (prior == nullptr && head != nullptr && head_region != nullptr)
	{
		// 14.5.6.1p5: two function templates declare the same template when
		// their heads declare the same parameters and their types agree once
		// each head's parameters stand for the other's.  The types themselves
		// differ, because each head declared parameters of its own, so the
		// chain's index of parameter type lists cannot answer this and the
		// declarations of the name are asked one at a time.
		prior = equivalent_template(*head, *head_region, type);
	}
	if (prior != nullptr)
	{
		if (redeclares != nullptr)
		{
			*redeclares = true;
		}
		if (prior->type != type && prior->template_parameters == nullptr)
		{
			throw std::runtime_error("two declarations of " + name +
			                         " differ only in their return type");
		}
		if (define && prior->defined)
		{
			throw std::runtime_error(name + " is defined twice");
		}
		prior->defined = prior->defined || define;
		if (define && prior->template_parameters != nullptr &&
		    prior->templated == nullptr)
		{
			// 14.5.6.1p5 and 14p1: the definition is of the template an earlier
			// declaration made, so what an instantiation reads is this
			// definition's syntax and the parameter names *it* wrote - against
			// the parameters the declaration's own type is written over, which
			// is what a deduction binds.
			record_function_template(*prior, *head_region, where);
		}
		return *prior;
	}
	if (redeclaration)
	{
		// 9.3p2 and 3.4.3.2p1: a definition written with a qualified
		// declarator-id defines the declaration that region already made, so a
		// declarator that matches none of them names a member the region does
		// not have however nearly it spells one - which is what tells `int
		// X::f() &&` from the `int X::f() &` the class declared, and equally
		// what tells a mistyped parameter list or cv-qualifier-seq from the one
		// the class wrote.
		throw std::runtime_error("a definition of " + name +
		                         " matches no declaration of it");
	}
	if (object_member && where.kind == ScopeKind::Class && head != nullptr)
	{
		require_uniform_ref_qualifiers(*head, name, type);
	}

	SemaEntity& entity = model_.create(SemaKind::Function, name, type);
	name_in_region(entity, where, name);
	entity.defined = define;
	entity.c_linkage = c_linkage_;
	entity.tail = &entity;
	if (head_region != nullptr)
	{
		// 14p1: this declares a template rather than a function, and the
		// parameters it is written over are what an instantiation of it
		// substitutes arguments for.
		entity.template_parameters = head_region;
		record_function_template(entity, *head_region, where);
	}
	if (hidden)
	{
		// 11.3p6: the declaration is a member of this region whose name no
		// lookup written in it finds, so it joins the region's hidden chain and
		// binds nothing.  3.4.2p2 reaches it through the class that wrote it.
		SemaEntity*& concealed_head = where.hidden[name];
		if (concealed_head == nullptr)
		{
			concealed_head = &entity;
		}
		else
		{
			concealed_head->tail->next = &entity;
			concealed_head->tail = &entity;
		}
		model_.hold_overload(*concealed_head, signature, entity);
		model_.declare_in(where, entity);
		return entity;
	}
	if (head != nullptr)
	{
		head->tail->next = &entity;
		head->tail = &entity;
	}
	else
	{
		head = &entity;
		model_.bind(where, name, entity);
	}
	model_.hold_overload(*head, signature, entity);
	model_.declare_in(where, entity);
	return entity;
}

// 7.3.1.2p3: a namespace-scope declaration that matches a friend declaration
// declares the same function, and is what first makes its name visible.  The
// declaration leaves the hidden chain for the one the name binds; the other
// friend declarations of that name stay where they are, because each is made
// visible by a declaration of its own.
void SemaAnalyzer::reveal_friend(Scope& where, const std::string& name,
                                 SemaEntity& entity, std::uint32_t signature)
{
	const std::unordered_map<std::string, SemaEntity*>::iterator held =
		where.hidden.find(name);
	SemaEntity* concealed = held->second;
	// The chain is indexed by the declaration the name would be bound to, and
	// that is the declaration that may be leaving, so the whole index of this
	// chain is dropped and rebuilt.  A chain holds the friend declarations of
	// one name in one namespace, which is what the source wrote.
	for (SemaEntity* at = concealed; at != nullptr; at = at->next)
	{
		model_.drop_overload(
			*concealed,
			declaration_signature(where, at->type, at->object_member));
	}
	SemaEntity* before = nullptr;
	for (SemaEntity* at = concealed; at != &entity; at = at->next)
	{
		before = at;
	}
	if (before == nullptr)
	{
		concealed = entity.next;
	}
	else
	{
		before->next = entity.next;
	}
	entity.next = nullptr;
	entity.tail = &entity;
	if (concealed == nullptr)
	{
		where.hidden.erase(held);
	}
	else
	{
		SemaEntity* last = concealed;
		while (last->next != nullptr)
		{
			last = last->next;
		}
		concealed->tail = last;
		held->second = concealed;
		for (SemaEntity* at = concealed; at != nullptr; at = at->next)
		{
			model_.hold_overload(
				*concealed,
				declaration_signature(where, at->type, at->object_member), *at);
		}
	}
	SemaEntity* head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind == SemaKind::Function)
	{
		head->tail->next = &entity;
		head->tail = &entity;
	}
	else
	{
		head = &entity;
		model_.bind(where, name, entity);
	}
	model_.hold_overload(*head, signature, entity);
}
