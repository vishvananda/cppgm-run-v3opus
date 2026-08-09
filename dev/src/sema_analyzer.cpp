#include "sema_analyzer.h"

#include <ostream>
#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

namespace
{

std::string decimal(unsigned long long value, bool negative)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	if (digits.empty())
	{
		digits = "0";
	}
	return negative ? "-" + digits : digits;
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

// 9p1: which class-key a class-specifier or elaborated-type-specifier wrote.
ClassTag tag_of(const AstNode& node)
{
	const AstNode* key = child_of(node, AstKind::ClassKey);
	if (key == nullptr)
	{
		return ClassTag::Struct;
	}
	if (key->token == KW_CLASS)
	{
		return ClassTag::Class;
	}
	return key->token == KW_UNION ? ClassTag::Union : ClassTag::Struct;
}

const char* tag_text(ClassTag tag)
{
	switch (tag)
	{
	case ClassTag::Class: return "class ";
	case ClassTag::Union: return "union ";
	default: return "struct ";
	}
}

// 7.3.1.1p1: the namespace-definition PA10 wrote no name for, which it spells
// with the placeholder the dump uses.
bool is_unnamed_namespace(const AstNode& node)
{
	return node.text == "<unnamed>";
}

}

SemaAnalyzer::SemaAnalyzer(SemaDialect dialect)
	: reading_(nullptr)
	, dialect_(dialect)
	, packs_(nullptr)
	, sources_(nullptr)
	, anonymous_enums_(0)
	, local_types_(0)
	, instantiating_class_(0)
	, template_pattern_(nullptr)
	, template_pattern_dump_(nullptr)
	, instantiating_(nullptr)
	, checking_(0)
	, self_(nullptr)
	, naming_(nullptr)
	, breakable_(0)
	, continuable_(0)
	, switches_(0)
	, live_destructions_(0)
	, returns_(kNoType)
	, standard_only_(false)
	, direct_initialized_(kNoType)
	, c_linkage_(false)
{}

PendingDefinition::PendingDefinition()
	: function(nullptr)
	, self(nullptr)
	, body(nullptr)
	, scope(nullptr)
	, initializers(nullptr)
	, members(nullptr)
	, instantiation(false)
{}

AnalyzedValue::AnalyzedValue()
	: type(kNoType)
	, spelled(kNoType)
	, category(ValueCategory::PRValue)
	, node(nullptr)
	, functions(nullptr)
	, addressed(nullptr)
	, name(nullptr)
	, what(nullptr)
	, null_constant(false)
	, nonnull(false)
	, constant(false)
	, value(0)
	, entity(nullptr)
	, op(0)
	, operands(kNoType)
	// 13.3.1p4: an object argument no member call built names an object, and
	// every object a name reaches is an lvalue.
	, object_category(ValueCategory::LValue)
	, through_using(false)
	, braced(nullptr)
	, listed_class(kNoType)
{}

OverloadMatch::OverloadMatch()
	: viable(false)
	, rank(0)
	, to_bool(false)
	, reference(false)
	, binds_rvalue_ref(false)
	, binds_lvalue(false)
	, qualified(kNoType)
	, materialized(kNoType)
	, to_base(nullptr)
	, converting(nullptr)
	, converted(nullptr)
	, second_rank(0)
	, list_class(kNoType)
{}

void SemaAnalyzer::write(std::ostream& out) const
{
	if (semantics())
	{
		write_nodes(out, model_.unit(), 0);
		return;
	}
	write_dump(out, model_.root(), 0);
}

std::string SemaAnalyzer::dump_name(const Scope& scope,
                                    const std::string& name) const
{
	// 3.4.3.1 and 3.4.3.2: a declaration of a namespace or of a class is named
	// from outside it by the regions it is written in, which is what the prefix
	// of the region holds.  A block has no such name, so a declaration of one
	// is spelled as it was written.
	return scope.kind == ScopeKind::Namespace || scope.kind == ScopeKind::Class
		? scope.prefix + name
		: name;
}

// 3.5p4 and 7.3.1.1p1: the same name with the one region the dump leaves out
// written in - the unnamed namespace, which belongs to this translation unit
// and which the object file has to spell so that another unit's is a different
// entity.  Where no unnamed namespace stands around the region, the two names
// are the same string and only one of them is kept.
std::string SemaAnalyzer::abi_name(const Scope& scope,
                                   const std::string& name) const
{
	if (scope.abi_prefix.empty())
	{
		return dump_name(scope, name);
	}
	return scope.kind == ScopeKind::Namespace || scope.kind == ScopeKind::Class
		? scope.abi_prefix + name
		: name;
}

void SemaAnalyzer::run(const AstNode& unit)
{
	Context ctx;
	ctx.scope = &model_.global();
	ctx.dump = model_.global().dump;
	ctx.node = &model_.unit();
	if (semantics())
	{
		// 18.2p9: `std::nullptr_t` is the type of `nullptr`.  The course
		// declares it in the global namespace, and PA12 overloads on it, so
		// the name is bound before the unit is read.  It writes no line: the
		// dump describes what the unit declares.
		SemaEntity& entity = model_.create(SemaKind::Typedef, "nullptr_t",
		                                   types_.fundamental(FT_NULLPTR_T));
		model_.bind(*ctx.scope, entity.name, entity);
		// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation
		// functions are declared in the global namespace of every translation
		// unit whether or not the unit wrote them, so they are bound before it
		// is read.  A program that writes one of the four writes another
		// declaration of *this* function, which is what lets 17.6.4.6's
		// replacement be a definition of it rather than a second function of
		// the same name.  Nothing is emitted for a declaration no use reaches.
		declare_allocation_functions(*ctx.scope);
		// 3.4.1p8 and 9.3p2: a member defined outside its class settles facts a
		// body written before it already asks about, and the syntax of the
		// whole unit is in hand here.
		collect_unit_definitions(unit);
	}
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index], ctx);
	}
	// 3.6.3p1: the objects with static storage duration this unit constructed
	// are destroyed when the program ends, in the reverse order of their
	// construction.  Asking for the destructors here is what makes their
	// definitions part of the run of pending ones written below.
	for (std::size_t index = static_lifetimes_.size(); index-- > 0;)
	{
		destructor_action(*static_lifetimes_[index], *ctx.node, Placement::Named);
	}
	// 3.7.2p2: an object with thread storage duration is destroyed when its own
	// thread ends, which is a point the program hands to the runtime where the
	// object is initialized.  The action stands under the declaration, after
	// everything else that declaration wrote, and in declaration order: each
	// thread ends its own objects in the reverse of the order it began them,
	// which is what the runtime it was handed them in that order does.
	for (std::size_t index = 0; index < thread_lifetimes_.size(); ++index)
	{
		destructor_action(*thread_lifetimes_[index].entity,
		                  *thread_lifetimes_[index].line, Placement::Named);
	}
	write_pending_definitions();
	// 12.6.2p6: every delegation this unit wrote is settled now, so the chain
	// each one heads is walked here rather than at each definition, where a
	// constructor further along may not have been read yet.
	check_delegation_cycles();
}

bool SemaAnalyzer::accepts_arity(const SemaEntity& function,
                                 std::size_t given) const
{
	const std::size_t declared = types_.parameters(function.type).size();
	if (given >= declared)
	{
		return true;
	}
	// 8.3.6p4 and 7.3.3p1: a default-argument belongs to the declaration that
	// wrote it, which for a member a using-declaration brought into a class is
	// the base's - and a later declaration of the base's function may add one,
	// so the question is asked of that declaration rather than of a copy made
	// where the using-declaration stood.
	const std::unordered_map<std::uint32_t, std::vector<ParameterRecord> >::const_iterator
		found = defaults_.find(wrote_defaults(function).id);
	if (found == defaults_.end())
	{
		return false;
	}
	for (std::size_t index = given; index < declared; ++index)
	{
		if (index >= found->second.size() ||
		    found->second[index].initializer.written == nullptr)
		{
			return false;
		}
	}
	return true;
}

namespace
{

// 8.3.5p10 and 14.7.1p1: which of the two spellings the record holds is this
// function's name for the place.  A declaration the program wrote takes the
// first name any declaration of the function gave; a specialization is a
// declaration nothing wrote, so what spells its places is the template's own
// first declaration and no later one.
const std::string& spelled_for(const SemaEntity& function,
                               const ParameterRecord& record)
{
	return function.primary != nullptr ? record.pattern_name : record.name;
}

}

void SemaAnalyzer::record_declared_parameters(
	const SemaEntity& function, std::vector<Parameter>& declared,
	Scope* region)
{
	// 9.3.1p3: a member function's declarator does not write the object
	// parameter, so the parameters it did write begin after it.  What the
	// declarations of the function said about each parameter is held at the
	// place the function type gives that parameter, which is what every arity
	// question asks about.
	// 14.7.1p1: a specialization is a declaration nothing wrote, so the
	// declarations that said anything about its parameters are the template's -
	// which is the entity `wrote_defaults` already answers with, and the one
	// every reader of this record asks.  Reading the body of a specialization
	// re-reads the pattern's own declarator, so a name the template's *other*
	// declaration wrote is only there.
	const std::uint32_t held_by = wrote_defaults(function).id;
	const std::size_t total = types_.parameters(function.type).size();
	const std::size_t implicit =
		total > declared.size() ? total - declared.size() : 0;
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		// 8.3.5p10: a parameter's name is no part of the function's type, so no
		// two declarations of one function need agree about it and one that
		// wrote none still declares the parameter.  The name the object file
		// writes is therefore the function's rather than the declaration's, and
		// the first one any declaration gave is it.  It is only the object file
		// that asks, so the earlier dialects, whose dumps describe declarations
		// one at a time, are left writing what each of them wrote.
		const bool names = lowering() && !declared[index].name.empty();
		const bool takes = lowering() && declared[index].name.empty();
		if (declared[index].initializer == nullptr && !names && !takes)
		{
			continue;
		}
		const std::size_t at = index + implicit;
		std::vector<ParameterRecord>& held = defaults_[held_by];
		held.resize(at + 1 > held.size() ? at + 1 : held.size());
		if (names && held[at].name.empty())
		{
			held[at].name = declared[index].name;
			// The definitions read above left the object for this place
			// unnamed, because nothing had named it yet.  This declaration is
			// the first namer, so it is this place's spelling in the object
			// file however far below those definitions it stands.  It binds
			// nothing there: 3.3.4 ends a declaration's parameter names at its
			// own declarator, so a body already read never named this one.
			for (std::size_t which = 0; which < held[at].objects.size(); ++which)
			{
				held[at].objects[which]->name = declared[index].name;
			}
			held[at].objects.clear();
		}
		else if (takes)
		{
			// The object this declarator makes for the place is spelled with
			// the function's name for it, which no clause of this one wrote.
			declared[index].object_name = spelled_for(function, held[at]);
		}
		if (lowering() && function.primary == nullptr &&
		    !held[at].pattern_frozen)
		{
			// 14.7.1p1: a specialization is a declaration nothing wrote, so
			// what spells its places is the template's own declaration - the
			// first one, which is where the template and the places it gives a
			// name to begin.  A later declaration redeclares the template and
			// says nothing about a specialization; a definition still spells
			// the places its own declarator wrote, because the body read for a
			// specialization is that declarator's.
			held[at].pattern_name = held[at].name;
			held[at].pattern_frozen = true;
		}
		if (declared[index].initializer == nullptr ||
		    held[at].initializer.written != nullptr)
		{
			// 8.3.6p4: a parameter's default-argument belongs to the
			// declaration that first gave it, which a later one does not move.
			continue;
		}
		held[at].initializer.written = declared[index].initializer;
		held[at].initializer.scope = region;
	}
}

// 8.3.5p10: the names the object file spells `function`'s places with, given to
// a list of parameters taken from one of its declarations.
//
// A definition the standard rather than the program writes reads those objects
// throughout and has no declarator of its own to spell them with, so it is
// handed the list some declaration wrote - and what that one declaration left
// unnamed is still named, by whichever declaration of the function named it.
// Only the spelling travels, exactly as it does for a declarator: 3.3.4 ends a
// declaration's parameter names at its own declarator, so nothing is bound.
void SemaAnalyzer::name_recorded_parameters(const SemaEntity& function,
                                            std::vector<Parameter>& taken,
                                            std::size_t implicit) const
{
	const std::unordered_map<std::uint32_t, std::vector<ParameterRecord> >::const_iterator
		found = defaults_.find(wrote_defaults(function).id);
	if (found == defaults_.end())
	{
		return;
	}
	for (std::size_t index = 0; index < taken.size(); ++index)
	{
		const std::size_t at = index + implicit;
		if (!taken[index].name.empty() || !taken[index].object_name.empty() ||
		    at >= found->second.size())
		{
			continue;
		}
		taken[index].object_name = spelled_for(function, found->second[at]);
	}
}

void SemaAnalyzer::write_default_argument(const SemaEntity& function,
                                          std::size_t index, DumpNode& parent)
{
	// 7.3.3p1: the default-argument stands on the declaration the base wrote,
	// which is the one this call runs.
	const std::unordered_map<std::uint32_t, std::vector<ParameterRecord> >::const_iterator
		found = defaults_.find(wrote_defaults(function).id);
	if (found == defaults_.end() || index >= found->second.size() ||
	    found->second[index].initializer.written == nullptr)
	{
		throw std::runtime_error("a call omits an argument the declaration "
		                         "gives no default for");
	}
	const AstNode& written = *found->second[index].initializer.written;
	if (written.children.empty() || written.children[0]->children.empty())
	{
		throw std::runtime_error("a default-argument is written with no value");
	}
	// 8.3.6p9: the default-argument is looked up and read in the region the
	// declaration that introduced it was written in, not the one the call is.
	// 14.7.1p1: for a specialization that region is the template's, where every
	// parameter still stands for itself - so what the expression is read
	// against is a region of its own binding each of them to the argument this
	// specialization was made from, exactly as its body is read.
	Context where;
	where.scope = found->second[index].initializer.scope;
	if (function.primary != nullptr &&
	    function.primary->templated != nullptr &&
	    where.scope != nullptr &&
	    where.scope->kind == ScopeKind::TemplateParameters)
	{
		where.scope = &open_template_bindings(
			*function.primary->templated,
			types_.type_list_at(function.template_arguments));
	}
	where.dump = where.scope->dump;
	where.node = &parent;
	// 8.3.6p1: the call is made as if the default-argument stood where the
	// argument is missing, so a temporary it makes is storage that argument
	// asked for, named like every other argument's.
	initialize(*written.children[0]->children[0],
	           types_.parameters(function.type)[index], where, parent, false,
	           Requested::Argument);
}

// 12.4 and the ABI: which of the destructor's two entry points a use of it
// names - the base-object one for a base class subobject, and the
// complete-object one for every other object - and, for an implicitly declared
// one, the definition 12.4p6 says odr-using it asks for.  Every end of a
// lifetime asks the same two things, whether a statement writes the call or
// 15.2p2 leaves it to the cleanup around a partly built object, so both are
// settled here rather than beside each of the places that end one.
void SemaAnalyzer::note_destruction_entry(SemaEntity& destructor, bool base)
{
	// 14.7.1p1 and 12.4p11: ending the lifetime of an object is a use of its
	// class's destructor, so a specialization's is asked for here.
	require_definition(destructor);
	if (base)
	{
		destructor.base_object_entry = true;
	}
	else
	{
		destructor.complete_object_entry = true;
	}
	if (destructor.defined || !destructor.defaulted)
	{
		return;
	}
	destructor.defined = true;
	Pending pending;
	pending.function = &destructor;
	pending.self =
		&model_.create(SemaKind::Parameter, "this", this_type(destructor));
	pending.members = destructor.region;
	pending_.push_back(pending);
}

// 15.2p2: the destructor an exception out of a later step of the constructor
// would run on the subobject a step has just built.  Which steps have one after
// them is a question about the whole list, so the steps are collected in order
// and the caller asks it once the list is complete.  An array is as many steps
// as it has elements, and each element but the last is left standing by the one
// after it, so an array of more than one element stands for two.
void SemaAnalyzer::record_unwind_subobject(TypeId type)
{
	SemaEntity* const ends = class_destructor(element_of(type));
	if (ends == nullptr || ends->trivial || ends->deleted)
	{
		return;
	}
	const TypeId bare = types_.strip_cv(type);
	unwind_subobjects_.push_back(ends);
	if (types_.kind(bare) == TypeKind::Array &&
	    types_.object_size(bare) >
	        types_.object_size(types_.strip_cv(element_of(bare))))
	{
		unwind_subobjects_.push_back(ends);
	}
}

void SemaAnalyzer::write_pending_definitions()
{
	// A body read here may itself default-initialize an object, and so ask for
	// a definition that is not in the list yet.  Walking by index is what lets
	// the list grow while it is being written, and each definition is added
	// once, so the walk ends.
	for (std::size_t index = 0; index < pending_.size(); ++index)
	{
		write_definition(pending_[index]);
	}
}

void SemaAnalyzer::write_definition(Pending& pending)
{
	SemaEntity& function = *pending.function;
	if (pending.instantiation)
	{
		write_instantiation(pending);
		return;
	}
	DumpNode& line = open_fact(model_.unit(), "function-definition " +
	                           function.dump_name + " " +
	                           function_description(function.type,
	                                                function.object_member),
	                           FactKind::FunctionDefinition);
	line.fact.entity = &function;
	line.fact.type = function.type;
	if (pending.self != nullptr)
	{
		DumpNode& self = open_fact(line, "parameter " + pending.self->name + " " +
		                           types_.description(pending.self->type),
		                           FactKind::Parameter);
		self.fact.entity = pending.self;
		self.fact.type = pending.self->type;
	}
	Context inner;
	// 12.1p5: a definition no declaration wrote has no region of its own that
	// its names are read in, and the class it belongs to is what its member
	// initializations are read against.
	inner.scope = pending.scope != nullptr ? pending.scope : pending.members;
	if (inner.scope != nullptr)
	{
		inner.dump = inner.scope->dump;
		inner.node = &model_.unit();
	}
	if (pending.scope != nullptr)
	{
		const std::size_t implicit = pending.self != nullptr ? 1 : 0;
		// 8.3.5p10: the list is the one declaration's that wrote it, and the
		// name a place is spelled with is the function's - so it is settled
		// here, where the objects are made, because a declaration naming a
		// place may stand anywhere in the unit and this is written after the
		// whole of it has been read.  12.9p8's inherited constructor takes the
		// base's places, so the base's declarations are the ones that named
		// them.
		name_recorded_parameters(function, pending.parameters, implicit);
		if (function.inherited != nullptr)
		{
			name_recorded_parameters(*function.inherited, pending.parameters,
			                         implicit);
		}
		declare_parameters(pending.parameters, function.type, inner, &line,
		                   implicit);
	}
	// 9.2p2: the body is read where the class is complete, which is here, so
	// what the walk of the class left behind is put back for it.  The frames a
	// break or a continue leaves are depths into `lifetimes_`, so they belong to
	// the body that opened them and not to this one.
	const FunctionReading reading(*this, pending.self,
	                              types_.target(function.type));
	// 5.2.2p4: a definition read here owes the end of a parameter of class type
	// exactly as one read where it was written does - a constructor, a member
	// function defined in its class body and a member the standard defined are
	// all handed an object the caller built - so the lines just written are read
	// for those parameters here too.
	open_parameter_lifetimes(line);
	if (function.special == kConstructorFunction && pending.members != nullptr)
	{
		// 12.6.2p10: the members are initialized before the body runs.
		std::vector<SemaEntity*> enclosing_subobjects;
		enclosing_subobjects.swap(unwind_subobjects_);
		write_member_initializations(pending, line, inner);
		for (std::size_t index = 0; index + 1 < unwind_subobjects_.size();
		     ++index)
		{
			// 15.2p2: a step with a step after it leaves a built subobject
			// behind wherever that later one throws, so its destructor is
			// odr-used here.  The last step leaves nothing behind: the body
			// after it is not a region the references write a handler around.
			note_destruction_entry(*unwind_subobjects_[index], false);
		}
		unwind_subobjects_.swap(enclosing_subobjects);
	}
	else if (function.transfer != kNotTransfer && function.defaulted &&
	         pending.members != nullptr)
	{
		// 12.8p28: an assignment operator the standard defines assigns each
		// subobject of the object it is called on from the corresponding
		// subobject of the one it is passed, before its body - which is empty -
		// runs.
		write_transfer_steps(pending, line, inner);
	}
	if (pending.body != nullptr)
	{
		semantic_statement(*pending.body->children.back(), inner, line);
	}
	else
	{
		// 12.1p5 and 12.4p3: a definition no declaration wrote has a body that
		// does nothing beyond what the standard already said it does.
		open_fact(line, "compound-statement", FactKind::Compound);
	}
	if (function.transfer != kNotTransfer && function.defaulted &&
	    function.special != kConstructorFunction && pending.members != nullptr)
	{
		// 12.8p28: the assignment hands back the object it wrote into, which is
		// the lvalue `*this` denotes.
		DumpNode& returned = open_fact(line, "return-statement", FactKind::Return);
		Value self = this_value(returned);
		DumpNode& held = model_.wrap_node(*self.node, std::string());
		held.text = spell("unary-expression", ValueCategory::LValue,
		                  types_.target(self.type), "*");
		set_fact(held, FactKind::Unary, types_.target(self.type),
		         ValueCategory::LValue);
		held.fact.op = OP_STAR;
	}
	if (function.special == kDestructorFunction && pending.members != nullptr)
	{
		// 12.4p8: after the body, the members are destroyed.
		write_member_destructions(*pending.members, line);
	}
	// 6.6.3p2 and 3.8p1: control reaching the end of the definition leaves the
	// function as a return does, and what the parameters the boundary handed it
	// owe is owed after everything else the definition writes.
	end_parameter_lifetimes(line);
}

void SemaAnalyzer::declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;

	switch (node.kind)
	{
	case AstKind::NamespaceDefinition:
		namespace_definition(node, ctx);
		return;

	case AstKind::NamespaceAliasDefinition:
		namespace_alias(node, ctx);
		return;

	case AstKind::UsingDirective:
		using_directive(node, ctx);
		return;

	case AstKind::UsingDeclaration:
		using_declaration(node, ctx);
		return;

	case AstKind::AliasDeclaration:
		alias_declaration(node, ctx);
		return;

	case AstKind::StaticAssertDeclaration:
		static_assert_declaration(node, ctx);
		return;

	case AstKind::TemplateDeclaration:
		template_declaration(node, ctx);
		return;

	case AstKind::ExplicitInstantiationDeclaration:
	case AstKind::ExplicitInstantiationDefinition:
		explicit_instantiation(node, ctx);
		return;

	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
		// A class-specifier that is a whole declaration wrote no
		// decl-specifier-seq, so 9.5p3's `static` is not among what it says.
		inject_anonymous_members(
			&class_declaration(node, ctx, span,
			                   node.kind == AstKind::ClassSpecifier,
			                   std::string()),
			ctx, span, false);
		return;

	case AstKind::EnumSpecifier:
		enum_declaration(node, ctx, false, std::string());
		return;

	case AstKind::SimpleDeclaration:
		simple_declaration(node, ctx);
		return;

	case AstKind::FunctionDefinition:
		function_definition(node, ctx);
		return;

	case AstKind::SpecialMemberDefinition:
	case AstKind::SpecialMemberDeclaration:
		// 9.3p2: a constructor or a destructor defined outside its class, whose
		// declarator-id names the class it belongs to.  A definition written in
		// a class body is read where that body is, so one reaching here is
		// written outside every class.  8.4.2p2's `= default` is a definition
		// like any other: what differs is that the standard writes the body.
		if (semantics() || checking_ > 0)
		{
			// 14.6p8: a reading of a template's own definition reads this one
			// too, because what it says about the declaration the class made
			// is settled where the definition stands.
			special_member_definition(node, ctx);
		}
		return;

	case AstKind::LinkageSpecification:
	{
		// 7.5p4: linkage specifications nest, and the innermost one a
		// declaration is written in is the linkage it has.
		const bool enclosing = c_linkage_;
		c_linkage_ = node.text == "C";
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			declaration(*node.children[index], ctx);
		}
		c_linkage_ = enclosing;
		return;
	}

	default:
		// An access-specifier, an empty declaration and the member forms PA11
		// gives no meaning to declare nothing.
		return;
	}
}

void SemaAnalyzer::namespace_definition(const AstNode& node, const Context& ctx)
{
	// 7.3.1p2: a namespace-definition of a name already declared here as a
	// namespace extends it rather than declaring a second one.
	SemaEntity* entity = model_.find(*ctx.scope, node.text, LookupKind::Space);
	if (entity == nullptr)
	{
		if (model_.find(*ctx.scope, node.text, LookupKind::Any) != nullptr)
		{
			throw std::runtime_error("a namespace is declared with the name of "
			                         "another declaration");
		}
		entity = &model_.create(SemaKind::Namespace, node.text, kNoType);
		DumpScope& dump =
			model_.open_dump(*ctx.dump, "scope namespace " + node.text);
		entity->scope =
			&model_.open(ScopeKind::Namespace, *ctx.scope, entity, &dump);
		// 7.3.1.1p1: an unnamed namespace has no name to write before its
		// members, so they are spelled by the namespace around it.
		const bool unnamed = is_unnamed_namespace(node);
		entity->scope->prefix = unnamed
			? ctx.scope->prefix
			: ctx.scope->prefix + node.text + "::";
		// 3.5p4 and the ABI: what the dump leaves out the object file cannot,
		// because two units each writing an unnamed namespace declare two
		// entities and the names have to differ.  The ABI's name for the region
		// is `_GLOBAL__N_1`, and every region inside one carries it on.
		entity->scope->unnamed_region =
			unnamed || ctx.scope->unnamed_region;
		if (entity->scope->unnamed_region)
		{
			const std::string& outer = ctx.scope->abi_prefix.empty()
				? ctx.scope->prefix
				: ctx.scope->abi_prefix;
			entity->scope->abi_prefix =
				outer + (unnamed ? "_GLOBAL__N_1" : node.text) + "::";
		}
		model_.bind(*ctx.scope, node.text, *entity);
		model_.declare_in(*ctx.scope, *entity);
		// 7.3.1p8 and 7.3.1.1p1: an inline or unnamed member's declarations
		// are also declarations of the namespace around it.
		if (has_child(node, AstKind::Inline) || is_unnamed_namespace(node))
		{
			model_.nominate(*ctx.scope, *entity->scope);
		}
	}
	else if (entity->kind != SemaKind::Namespace)
	{
		throw std::runtime_error("a namespace-definition names a namespace alias");
	}

	Context inner;
	inner.scope = entity->scope;
	inner.dump = entity->scope->dump;
	// The dump writes one node per namespace-definition, so a namespace opened
	// twice is two nodes over one region.
	inner.node = semantics()
		? &open_fact(*ctx.node, "namespace-definition " + node.text,
		             FactKind::Namespace)
		: ctx.node;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		declaration(*node.children[index], inner);
	}
}

void SemaAnalyzer::namespace_alias(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	SemaEntity& space =
		require(resolve(target->text, ctx, LookupKind::Space), target->text);
	SemaEntity& entity = model_.create(SemaKind::NamespaceAlias, node.text, kNoType);
	entity.scope = model_.region_of(space);
	require_no_template_parameter(node.text, *ctx.scope);
	model_.bind(*ctx.scope, node.text, entity);
}

void SemaAnalyzer::using_directive(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	SemaEntity& space =
		require(resolve(target->text, ctx, LookupKind::Space), target->text);
	// 7.3.4p2: the directive is recorded where the program wrote it, because
	// that is what the rule turns on at both ends - the names it nominates can
	// be used *in the scope the directive appears in*, and they appear at the
	// level of the nearest enclosing namespace holding both it and the
	// namespace it named.  A lookup asks the first question of the region chain
	// it stands in and the second of that chain's levels, which is one reading
	// in `SemaModel::lookup`.  Recording it at the level instead answers the
	// second and loses the first: a directive written in one function's block
	// would then reach every lookup in the namespace around it, so
	// `int g() { using namespace R; } int h() { return q; }` would find `R::q`
	// in `h`, and two functions each nominating a different namespace that
	// declares one name would make every use of that name ambiguous.
	model_.nominate(*ctx.scope, *model_.region_of(space));
}

void SemaAnalyzer::using_declaration(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	// 7.3.3p1 and 14.6p2: `using typename X::y` says the name is a type, which
	// is what a nested-name-specifier that depends on a template parameter
	// needs said - and the name the declaration targets is the one after it.
	const std::string spelling =
		target->text.compare(0, 9, "typename ") == 0 ? target->text.substr(9)
		                                             : target->text;
	const QualifiedName written(spelling);
	if (written.names_a_template_id())
	{
		// 7.3.3p5: a using-declaration shall not name a template-id.
		throw std::runtime_error("a using-declaration names a template-id");
	}
	SemaEntity& entity =
		require(resolve(spelling, ctx, LookupKind::Any), spelling);
	const std::string name = written.last();
	if (ctx.scope->kind == ScopeKind::Class && ctx.scope->owner != nullptr)
	{
		// 7.3.3p1 read in a class: what the using-declaration makes is a
		// declaration of this class, not a second name for the base's.  The
		// access 11p1 gave it and the hiding 7.3.3p14 asks about are facts
		// about this class's declaration, and the base's is what a use of it
		// reaches.
		if (entity.kind == SemaKind::Class && written.qualified() &&
		    entity.scope == resolve_prefix(written, ctx))
		{
			// 12.9p1: the unqualified-id names the class the
			// nested-name-specifier named, so what it names is that class's
			// constructors rather than a member of it.
			if (ctx.scope->owner->base != &entity)
			{
				throw std::runtime_error(
					"a using-declaration names the constructors of a class "
					"that is not a direct base of the one it is written in");
			}
			// 12.9p1: which of the base's constructors are inherited is a
			// question about the complete class - it leaves out the ones this
			// class declares itself, and a member declaration standing after
			// this one declares them just as one standing before it does.  So
			// the class records that it asked, and 9.2p2's completion settles
			// it.
			ctx.scope->inheriting_constructors = true;
			return;
		}
		declare_using_members(entity, *ctx.scope, name);
		write_entity_line(*ctx.dump, entity);
		return;
	}
	// 14.6.1p6 is not asked here.  A using-declaration declares no name of its
	// own - 7.3.3p1 makes the declarations it names members of this region, and
	// the name they are found by is the one the region it came from gave them -
	// so `using N::T` inside a template whose head declared `T` redeclares
	// nothing, which is what both oracles say of it.  A class-scope
	// using-declaration is asked, because 7.3.3p1 there makes a declaration of
	// this class that `declare_using_member` creates.
	model_.bind(*ctx.scope, name, entity);
	model_.declare_in(*ctx.scope, entity);
	write_entity_line(*ctx.dump, entity);
}

std::uint32_t SemaAnalyzer::member_signature(const SemaEntity& function)
{
	return member_signature(function.type, function.object_member);
}

// 8.3.5p1, 8.3.5p7 and 9.3.1p3: how a function's type is spelled in the output.
//
// The two qualifiers a declarator writes after its parameter-clause are part of
// the function type it wrote, which is what a typedef, a pointer to function and
// the function type a pointer to member points to each go on holding and what
// their descriptions spell.  Where 9.3.1p3 has already made the object the first
// parameter, the cv-qualifier-seq is spelled as that parameter's own - and the
// ref-qualifier, which the type goes on carrying because 13.1 tells `f() &` from
// `f() &&` by it, is not spelled a second time beside it.
TypeId SemaAnalyzer::function_description_type(TypeId type, bool object_member)
{
	return object_member
		? types_.ref_qualified_function(type, RefQualifier::None)
		: type;
}

std::string SemaAnalyzer::function_description(TypeId type, bool object_member)
{
	return types_.description(function_description_type(type, object_member));
}

std::uint32_t SemaAnalyzer::member_signature(TypeId type, bool object_member)
{
	const std::vector<TypeId>& written = types_.parameters(type);
	std::vector<TypeId> list;
	list.reserve(written.size());
	// 9.3.1p3: the object parameter is one this milestone put in the type and
	// no declarator wrote, so it is no part of 8.3.5p4's parameter-type-list -
	// which is why a static member function and a non-static one declare the
	// same list when their declarators wrote the same parameters.  What the
	// object parameter says about the declaration itself is 8.3.5p7's
	// cv-qualifier-seq, and a static member function wrote none, so that stands
	// beside the list as its own step rather than in place of a parameter.
	for (std::size_t index = object_member ? 1u : 0u; index < written.size();
	     ++index)
	{
		list.push_back(written[index]);
	}
	// Where 9.3.1p3's lowering has not run - PA11 describes what the declarator
	// wrote and adds no object parameter - the cv-qualifier-seq is still on the
	// function type itself, so the one step reads it from wherever it stands.
	TypeId object = types_.qualified(
		types_.fundamental(FT_VOID),
		object_member ? types_.object_cv(types_.target(written[0]))
		              : types_.cv(type));
	// 8.3.5p1 and 13.1: a ref-qualifier is as much a part of what tells two
	// declarations of one member apart as the cv-qualifier-seq beside it, so it
	// stands in the same step: `f() &` and `f() &&` are two declarations of one
	// name, and a definition written outside the class matches the declaration
	// whose qualifiers it repeats rather than colliding with it.
	const RefQualifier ref = types_.function_ref_qualifier(type);
	if (ref != RefQualifier::None)
	{
		object = types_.reference_to(object, ref == RefQualifier::RValue);
	}
	list.push_back(object);
	return (types_.type_list(list) << 1) | (types_.variadic(type) ? 1u : 0u);
}

void SemaAnalyzer::hide_using_members(Scope& where)
{
	for (std::size_t index = 0; index < where.using_names.size(); ++index)
	{
		const std::string& name = where.using_names[index];
		SemaEntity* const head = model_.find(where, name, LookupKind::Any);
		if (head == nullptr || head->kind != SemaKind::Function)
		{
			continue;
		}
		// 7.3.3p14: what this class declared itself hides what the base
		// declared with the same name and parameter-type-list rather than
		// conflicting with it, whichever of the two the class body wrote
		// first.  The declarations of the name are what the source wrote, so
		// one pass says which signatures the class has of its own and one more
		// takes the brought-in declarations of those signatures off the chain.
		std::unordered_set<std::uint32_t> declared;
		bool brought_in = false;
		for (SemaEntity* at = head; at != nullptr; at = at->next)
		{
			if (at->shadowed != nullptr)
			{
				brought_in = true;
			}
			else
			{
				declared.insert(member_signature(*at));
			}
		}
		if (!brought_in || declared.empty())
		{
			continue;
		}
		SemaEntity* kept = nullptr;
		SemaEntity* tail = nullptr;
		for (SemaEntity* at = head; at != nullptr;)
		{
			SemaEntity* const next = at->next;
			at->next = nullptr;
			if (at->shadowed == nullptr ||
			    declared.count(member_signature(*at)) == 0)
			{
				if (kept == nullptr)
				{
					kept = at;
				}
				else
				{
					tail->next = at;
				}
				tail = at;
			}
			at = next;
		}
		kept->tail = tail;
		if (kept == head)
		{
			continue;
		}
		// What the name was bound to was one of the declarations hidden, so
		// the name now binds what is left - and 13.1's index of the chain is
		// keyed by the declaration the name binds, so what stays is keyed
		// again under it.  A declaration a using-declaration brought in is one
		// 7.3.3p14 hides rather than one 13.1 redeclares, so it stays out of
		// that index exactly as it was when the using-declaration made it.
		model_.bind(where, name, *kept);
		for (SemaEntity* at = kept; at != nullptr; at = at->next)
		{
			if (at->shadowed == nullptr)
			{
				model_.hold_overload(
					*kept, declaration_signature(where, at->type,
					                             at->object_member), *at);
			}
		}
	}
}

void SemaAnalyzer::declare_using_members(SemaEntity& named, Scope& where,
                                         const std::string& name)
{
	// 7.3.3p14: a member function this class declared itself hides the one the
	// base declared with the same name and parameter list rather than
	// conflicting with it, so what is brought in is what the class does not
	// already declare.  The declarations of the name in each region are what
	// the source wrote, so this costs one pass over them and no search.
	std::unordered_set<std::uint32_t> declared;
	for (SemaEntity* at = model_.find(where, name, LookupKind::Any);
	     at != nullptr && at->kind == SemaKind::Function; at = at->next)
	{
		declared.insert(member_signature(*at));
	}
	for (SemaEntity* at = &named; at != nullptr; at = at->next)
	{
		if (at->kind != SemaKind::Function)
		{
			// A name bound to anything else is bound to one declaration, so
			// there is no chain to walk and nothing of the class's own that it
			// could be hiding.
			declare_using_member(*at, where, name);
			return;
		}
		if (declared.insert(member_signature(*at)).second)
		{
			declare_using_member(*at, where, name);
		}
	}
}

SemaEntity& SemaAnalyzer::declare_using_member(SemaEntity& target, Scope& where,
                                               const std::string& name)
{
	require_no_template_parameter(name, where);
	SemaEntity& shadow = model_.create(target.kind, name, target.type);
	const std::uint32_t id = shadow.id;
	// The declaration says the same thing about the entity the base declared as
	// the base's own does - it is the same member of the same class, laid out
	// where the base laid it out - so it is copied whole, and what is a fact
	// about this declaration alone is written over it.
	shadow = target;
	shadow.id = id;
	shadow.name = name;
	shadow.next = nullptr;
	shadow.tail = &shadow;
	shadow.region = nullptr;
	shadow.access = kPublicAccess;
	shadow.shadowed =
		target.shadowed != nullptr ? target.shadowed : &target;
	name_in_region(shadow, where, name);
	if (shadow.kind == SemaKind::Function && shadow.object_member &&
	    where.owner != nullptr)
	{
		// 13.3.3.1p4: a non-conversion function a using-declaration brought into
		// a derived class is a member of that class where the type of the
		// implicit object parameter is concerned, which is what ranks it against
		// the class's own declarations on an object of the class.  9.3.1p3 put
		// that parameter in the type, so this is where the rule is written; what
		// the call passes is still the base subobject, because the function it
		// runs is the base's.
		const std::vector<TypeId>& written = types_.parameters(target.type);
		std::vector<TypeId> parameters;
		parameters.push_back(types_.pointer_to(types_.qualified(
			where.owner->type, types_.object_cv(types_.target(written[0])))));
		for (std::size_t index = 1; index < written.size(); ++index)
		{
			parameters.push_back(written[index]);
		}
		// 8.3.5p1: only the implicit object parameter is the derived class's;
		// the ref-qualifier written after the parameter-clause is part of the
		// function type the base declared, so it travels with the rest of it -
		// which is what 13.3.1p4 reads to say the brought-in declaration is
		// viable, and what 7.3.3p14 reads to say the derived class's own
		// declaration of the same spelling hides it.
		shadow.type = types_.ref_qualified_function(
			types_.function_of(types_.target(target.type), parameters,
			                   types_.variadic(target.type)),
			types_.function_ref_qualifier(target.type));
	}
	SemaEntity* const head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind == SemaKind::Function &&
	    shadow.kind == SemaKind::Function)
	{
		// 13.1: the declarations of one name in one region are one chain, and
		// 13.3 ranks what a using-declaration brought in beside the class's own.
		head->tail->next = &shadow;
		head->tail = &shadow;
	}
	else
	{
		model_.bind(where, name, shadow);
	}
	if (shadow.kind == SemaKind::Function)
	{
		// 7.3.3p14's hiding is asked of this name where the class is complete,
		// and this is what says the class has one to ask about.  A class writes
		// the using-declarations it writes, so holding them is what the source
		// wrote and no search.
		bool held = false;
		for (std::size_t index = 0;
		     !held && index < where.using_names.size(); ++index)
		{
			held = where.using_names[index] == name;
		}
		if (!held)
		{
			where.using_names.push_back(name);
		}
	}
	model_.declare_in(where, shadow);
	return shadow;
}

// 14.6.1p6: a template-parameter shall not be redeclared within its scope,
// which is the declaration its head parameterises and every region nested in
// that one.
//
// The question is a fact of the regions a declaration stands in, so it is asked
// where the name is bound rather than at each syntax that can bind one: every
// declaration that binds a name reaches it - a typedef and an alias-declaration
// through `declare_type_alias`, a class and an enumeration through
// `declare_type_name`, and an object, a parameter, a function, an enumerator, a
// class-scope using-declaration and a namespace-alias where each of those
// binds.  The template's own declared name is bound before its head is read, so
// `record_template` asks that one of the head itself.  What an
// instantiation binds in a region of the same kind is the argument
// and not the parameter - a typedef-name of the type an argument named - so a
// body read for a specialization is not asked this about names its own template
// head never declared.
void SemaAnalyzer::require_no_template_parameter(const std::string& name,
                                                 const Scope& where)
{
	for (Scope* at = where.kind == ScopeKind::TemplateParameters
	         ? const_cast<Scope*>(&where)
	         : where.template_head;
	     at != nullptr; at = at->template_head)
	{
		const SemaEntity* const parameter =
			model_.find(*at, name, LookupKind::Any);
		if (parameter != nullptr && parameter->kind == SemaKind::TemplateType)
		{
			throw std::runtime_error(name + " redeclares a template parameter "
			                         "within the scope of the template head "
			                         "that declared it");
		}
	}
}

// 7.1.3p3 and 9.2p1: the typedef-name a `typedef` or an alias-declaration
// declares.
//
// 7.1.3p3 lets a typedef-name be declared again in a region that already
// declares it, for the same type - which is what a header included twice
// writes - and 9.2p1 does not: a class shall not declare a member twice, and
// the second declaration is one whether or not it names the same type.
// 7.1.3p6's redefinition of a class-name is the one the class does allow, and
// it is a class-name and not a typedef-name that stands there.
SemaEntity& SemaAnalyzer::declare_type_alias(const std::string& name,
                                             TypeId aliased, Scope& where)
{
	require_no_template_parameter(name, where);
	if (where.kind == ScopeKind::Class)
	{
		const SemaEntity* const declared =
			model_.find(where, name, LookupKind::Any);
		if (declared != nullptr && declared->kind == SemaKind::Typedef)
		{
			throw std::runtime_error(name + " is declared twice as a member "
			                         "type of one class");
		}
	}
	SemaEntity& entity = model_.create(SemaKind::Typedef, name, aliased);
	model_.bind(where, name, entity);
	model_.declare_in(where, entity);
	return entity;
}

// 3.3.10p2, 9.2p1 and 14.6.1p6: what binding a class-name or an enum-name in a
// region owes, which is the other half of the question `declare_type_alias`
// asks.
//
// 7.1.3p3's leniency is a typedef-name's alone: it lets a region declare the
// *same* typedef-name again, and 7.1.3p6 lets a class-name be redefined as a
// typedef-name for the type it already names.  Neither runs the other way.  A
// class-name and an enum-name are what 3.3.10p2's tag binding holds and an
// elaborated-type-specifier reaches, and a typedef-name is not one - so
// declaring either where the region already declares a typedef-name of that
// spelling declares one name as two different kinds of type.  9.2p1 is a
// class's own reason to refuse it and 3.3p4 the reason every other region has.
void SemaAnalyzer::declare_type_name(const std::string& name, Scope& where)
{
	require_no_template_parameter(name, where);
	const SemaEntity* const declared =
		model_.find(where, name, LookupKind::Any);
	if (declared != nullptr && declared->kind == SemaKind::Typedef)
	{
		throw std::runtime_error(name + " is declared as a class or an "
		                         "enumeration where the region it stands in "
		                         "already declares a typedef-name of that "
		                         "spelling");
	}
}

void SemaAnalyzer::alias_declaration(const AstNode& node, const Context& ctx)
{
	const AstNode* type = child_of(node, AstKind::TypeId);
	const TypeId aliased = type_id_type(*type, ctx);
	declare_type_alias(node.text, aliased, *ctx.scope);
	if (semantics())
	{
		// 9.2p1: an alias a class declares is a member of it, and a member
		// declaration writes no line of its own.
		if (ctx.node != nullptr)
		{
			open_fact(*ctx.node, "type-alias " + node.text + " " +
			          types_.description(aliased), FactKind::TypeAlias);
		}
		return;
	}
	write_line(*ctx.dump, "type-alias", node.text, aliased);
}

void SemaAnalyzer::static_assert_declaration(const AstNode& node,
                                             const Context& ctx)
{
	const Constant value = evaluate(*node.children[0], ctx);
	if (value.bits == 0)
	{
		throw std::runtime_error("a static_assert condition is false");
	}
}

void SemaAnalyzer::template_declaration(const AstNode& node, const Context& ctx)
{
	// 14p1: a template declares nothing until it is instantiated, so where the
	// milestone instantiates one the pattern is recorded rather than read.
	// PA11 and PA12 describe what the declaration *says*, which is the walk
	// below, and neither instantiates anything.
	if (lowering() && record_template(node, ctx))
	{
		return;
	}
	// 14.1p1 and 3.3.2p4: the template parameters are declared in a region of
	// their own that encloses the declaration they parameterise.
	DumpScope& dump = model_.open_dump(*ctx.dump, "scope template-parameters");
	Context inner;
	inner.scope =
		&model_.open(ScopeKind::TemplateParameters, *ctx.scope, nullptr, &dump);
	inner.dump = &dump;
	inner.node = ctx.node;
	// 14.1p1: the declaration this head parameterises is read here, so a
	// declarator-id that names a region of its own still has these parameters
	// standing over it.  A reading nested inside that declaration opens a
	// context of its own and inherits nothing of this.
	inner.template_head = inner.scope;

	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::TemplateParameterClause)
		{
			const AstNode* list =
				child_of(child, AstKind::TemplateParameterList);
			for (std::size_t at = 0; list != nullptr && at < list->children.size();
			     ++at)
			{
				template_parameter(*list->children[at], inner);
			}
			continue;
		}
		// 14p1: the declaration is a pattern, and 14.7.1p1's instantiation of
		// it is a second reading of the same syntax - so the walk that reads it
		// for its declaration records where the syntax stands, and every
		// declaration it makes takes that record.
		const AstNode* const enclosing = template_pattern_;
		DumpScope* const enclosing_dump = template_pattern_dump_;
		template_pattern_ = lowering() ? &child : nullptr;
		template_pattern_dump_ = ctx.dump;
		declaration(child, inner);
		template_pattern_ = enclosing;
		template_pattern_dump_ = enclosing_dump;
	}
}

void SemaAnalyzer::template_parameter(const AstNode& node, const Context& ctx)
{
	if (node.kind != AstKind::TypeParameter)
	{
		// 14.1p2: a non-type parameter binds a value, which PA11 does not model.
		return;
	}
	const AstNode* id = child_of(node, AstKind::Identifier);
	if (id == nullptr)
	{
		return;
	}
	// 14.1p2: a parameter declared with `template` names a template rather
	// than a type; the parameters of its own clause belong to it alone.
	const bool is_template = has_child(node, AstKind::TemplateTemplateParameter);
	const TypeId type = types_.template_parameter_type(model_.type_entity_id(),
	                                                   is_template, id->text);
	// 14.1p2 and the ABI's `<template-param>`: a specialization's own name is
	// encoded from the template's signature, where the parameter stands for
	// itself and is written by its place rather than by its spelling.
	types_.set_template_index(
		type, static_cast<unsigned>(ctx.scope->declarations.size()));
	SemaEntity& entity = model_.create(SemaKind::TemplateType, id->text, type);
	model_.bind(*ctx.scope, id->text, entity);
	model_.declare_in(*ctx.scope, entity);
	// 14.1p9: the argument this place takes where the use wrote none and no
	// deduction reached it.  It is the parameter's own fact - 14.1p2 lets each
	// declaration of one template spell its places as it likes - so it is kept
	// beside the declaration and read where a deduction finds the place empty.
	const AstNode* const written =
		child_of(node, AstKind::DefaultTemplateArgument);
	if (written != nullptr)
	{
		const AstNode* const carried = child_of(*written, AstKind::TypeId);
		if (carried != nullptr)
		{
			parameter_defaults_.insert(std::make_pair(entity.id, carried));
		}
	}
	write_line(*ctx.dump, "type", id->text, type);
}

SemaEntity* SemaAnalyzer::redeclared(const Context& ctx, const std::string& name,
                                     SemaKind kind)
{
	SemaEntity* found = model_.find(*ctx.scope, name,
	                                kind == SemaKind::Class || kind == SemaKind::Enum
	                                    ? LookupKind::Type
	                                    : LookupKind::Any);
	if (found == nullptr || found->kind != kind)
	{
		return nullptr;
	}
	return found;
}

// 9.1p2 and 9.2p2: the declaration a class-head names, which is the one an
// earlier declaration of the same name in the same region already made and
// otherwise one this class-head makes.
//
// 3.5p4 and 9.8p1 are read here too, because both are facts of where the
// declaration stands rather than of what its body holds: the regions the object
// file writes around the name, and the function whose body wrote it.
SemaEntity* SemaAnalyzer::class_head_entity(const Context& ctx, ClassTag tag,
                                            const QualifiedName& spelled,
                                            const std::string& written,
                                            bool define, Scope* declaring)
{
	const std::string name = spelled.last();
	SemaEntity* entity = nullptr;
	if (spelled.qualified())
	{
		// 9.1p2 and 3.4.3p3: a class-head-name with a nested-name-specifier
		// defines the class that region already declared, wherever the
		// definition is written, rather than declaring a second class of that
		// name where it stands.
		entity = &require(model_.lookup_in(*declaring, name, LookupKind::Type),
		                  written);
		if (entity->kind != SemaKind::Class)
		{
			throw std::runtime_error("a class definition names " + written +
			                         ", which is not a class");
		}
	}
	else if (!name.empty())
	{
		entity = redeclared(ctx, name, SemaKind::Class);
	}
	if (entity != nullptr)
	{
		// 9p3: two declarations of one class agree exactly when neither or
		// both wrote `union`.
		const bool was_union = types_.class_tag(entity->type) == ClassTag::Union;
		if (was_union != (tag == ClassTag::Union))
		{
			throw std::runtime_error("a class is redeclared with a class-key "
			                         "that does not agree with its definition");
		}
		if (define && entity->defined)
		{
			throw std::runtime_error("a class is defined twice");
		}
		return entity;
	}
	const std::uint32_t id = model_.type_entity_id();
	// The dump spells a class by the named namespaces around it, which is
	// a fact about the declaration rather than about the use, so the type
	// carries it.
	const std::string qualified = dump_name(*ctx.scope, name);
	// 3.5p4: the object file names it by the regions the ABI writes, which
	// is the same string but for 7.3.1.1p1's unnamed namespace - so the
	// type carries both, and every name encoded from a use of it reads the
	// second.
	const TypeId type = types_.class_type(
		id, tag, semantics() ? qualified : name, abi_name(*ctx.scope, name));
	entity = &model_.create(SemaKind::Class, name, type);
	own_type(type, *entity);
	if (!name.empty())
	{
		declare_type_name(name, *ctx.scope);
		model_.bind(*ctx.scope, name, *entity);
		model_.declare_in(*ctx.scope, *entity);
	}
	else
	{
		// 9.8p1 and the ABI's `<unnamed-type-name>`: a class the function's
		// body left unnamed is bound in no region, so `declare_in` never sees
		// it - and the spelling this unit lends it below is one it counted for
		// itself, which another unit gives to a class of its own.  What the
		// object file names it by is the function and its place among the types
		// that function left unnamed.
		model_.settle_unnamed_local_name(*ctx.scope, *entity);
	}
	// 9.8p1: the declaration is what says the class is local to a function, and
	// the type is what a use of it - as a parameter, as the type an object-file
	// name for its table is written from - reads afterwards, so the two carry
	// the same answer.
	types_.set_local_name(type, entity->local_function,
	                      entity->local_occurrence, entity->local_unnamed);
	return entity;
}

SemaEntity& SemaAnalyzer::class_declaration(const AstNode& node,
                                            const Context& ctx, const Span& span,
                                            bool define,
                                            const std::string& named_by,
                                            SemaEntity* as,
                                            const std::string* spelled_as)
{
	const ClassTag tag = tag_of(node);
	// 7.1.3p2: a class its specifiers left unnamed is named by the first
	// declarator of the declaration it belongs to, before its body is read, so
	// every line the body writes spells it the way the program will.  A class
	// defined in a function is named by the convention instead: 3.5p8 gives a
	// local class no linkage, so no other translation unit can name it and the
	// name a declarator would lend it says nothing about it.
	// 9.4.2p1 and 3.4.1p8: a class-head-name with a nested-name-specifier
	// defines a member of the region that name reaches, and its body is read
	// there - so what encloses the class is that region and not the one the
	// definition happens to be written in.  A name the body writes then reaches
	// what the enclosing class declares, 9p2's injected-class-name included.
	Context outer = ctx;
	if (as == nullptr && QualifiedName(node.text).qualified())
	{
		outer.scope = resolve_prefix(QualifiedName(node.text), ctx);
		outer.dump = outer.scope->dump;
	}
	const bool local = outer.scope->kind == ScopeKind::Block ||
		outer.scope->kind == ScopeKind::Function;
	const std::string pattern_name =
		node.text.empty() ? (local ? std::string() : named_by) : node.text;
	// 14.7.1p1: a specialization is spelled by the template-id that named it,
	// so its lines and its members are written under that name; the class-head
	// still says which name its body binds to the injected class-name.
	const std::string written =
		spelled_as != nullptr ? *spelled_as : pattern_name;
	const QualifiedName spelled(written);
	const std::string name = QualifiedName(pattern_name).last();

	SemaEntity* const entity =
		as != nullptr ? as
		              : class_head_entity(ctx, tag, spelled, written, define,
		                                  outer.scope);

	if (!name.empty())
	{
		// The line is spelled as this declaration spells it, class-key and
		// nested-name-specifier included.
		ctx.dump->lines.push_back("type " + written + " " + tag_text(tag) + written);
	}
	if (!define)
	{
		return *entity;
	}

	// 9.5p2 and the shared convention: an unnamed class no declarator names is
	// named after the terminals its declaration was written from, and one a
	// declarator in a function names is numbered among the classes the
	// translation unit defines in a function.
	std::string header = written;
	if (name.empty())
	{
		header = named_by.empty()
			? std::string("__anonymous_") +
				(tag == ClassTag::Union ? "union" : "class") + "_type__" +
				decimal(span.begin, false) + "_" + decimal(span.end, false)
			: "__local_type" + decimal(++local_types_, false);
		types_.rename(entity->type, header, abi_name(*outer.scope, header));
	}
	DumpScope& dump = model_.open_dump(*outer.dump, "scope class " + header);
	Scope& scope = model_.open(ScopeKind::Class, *outer.scope, entity, &dump);
	entity->scope = &scope;
	entity->defined = true;
	// 9.1p2: a member is named through its class, so the dump spells a member
	// declaration with the class before it, and the class with the named
	// namespaces around it, which is what its type already carries.
	scope.prefix = types_.user_name(entity->type) + "::";
	// 3.5p4: the object file names a member by its class and the class by the
	// regions around it, one of which - 7.3.1.1p1's unnamed namespace - the
	// dump leaves out.  The type is what carries that second spelling, so a
	// member is named from the class rather than from the region the
	// definition of the class happens to stand in.
	if (scope.unnamed_region)
	{
		scope.abi_prefix = types_.user_qualified_name(entity->type) + "::";
	}

	Context inner;
	inner.scope = &scope;
	inner.dump = &dump;
	// 9.2p2: the members of a class are declarations of it rather than of the
	// region it is written in, and the PA12 output describes what a function
	// body means, so a member declaration writes no line of its own.
	inner.node = nullptr;
	// 9p2: the class's own name is declared in the class, so a member may name
	// the class it belongs to and a qualified name may reach it through a class
	// derived from it.
	if (!name.empty())
	{
		model_.bind(scope, name, *entity);
	}
	// 10p1: the base-clause is read before the members, because from here on
	// the class holds what its base declares - a type the base named, a member
	// a member declaration uses - and the members are read against that.
	const AstNode* const bases = child_of(node, AstKind::BaseClause);
	if (bases != nullptr)
	{
		read_base_clause(*bases, *entity, scope, ctx, header);
	}
	// 11p2: what a member with no access-specifier before it is declared under,
	// which the class-key decides and each access-specifier changes from there.
	unsigned char access =
		tag == ClassTag::Class ? kPrivateAccess : kPublicAccess;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& member = *node.children[index];
		if (member.kind == AstKind::ClassKey)
		{
			continue;
		}
		if (member.kind == AstKind::AlignmentSpecifier)
		{
			// 7.6.2p1: the alignment-specifiers of a class-head are read where
			// the class is, and 9.2p2 applies the strictest of them when the
			// class is laid out.
			continue;
		}
		if (member.kind == AstKind::AccessSpecifier)
		{
			// 11p1: the specifier holds until the next one or the end of the
			// class, so it is the state the member declarations are read in.
			access = member.token == KW_PRIVATE
				? kPrivateAccess
				: (member.token == KW_PROTECTED ? kProtectedAccess
				                                : kPublicAccess);
			continue;
		}
		if (member.kind == AstKind::BaseClause)
		{
			// Read above, before the members, because they are read against
			// what it added to the class.
			continue;
		}
		if (semantics() && !lowering() && checking_ == 0 &&
		    member.kind == AstKind::BitFieldDeclaration)
		{
			// 9.6p1: a bit-field is a member whose width its declaration writes,
			// which the layout and every use of it read.  PA12 has no rule for
			// either, so the member would be missing from the class the output
			// describes.
			throw std::runtime_error(header + " declares a bit-field, which PA12 "
			                         "does not describe");
		}
		// 11p1: the access a declaration was written under is a fact about the
		// declaration, so it is written onto whatever this member declared.
		// One declaration declares few names, and each is reached once.
		const std::size_t before = scope.declarations.size();
		if ((lowering() || checking_ > 0) &&
		    member.kind == AstKind::BitFieldDeclaration)
		{
			// 9.6p1: the width is part of what the declaration declares, so the
			// declarators are read against it rather than through the ordinary
			// path, which would leave the member an object with an address.
			bit_field_declaration(member, inner);
		}
		else if ((semantics() || checking_ > 0) &&
		         (member.kind == AstKind::SpecialMemberDeclaration ||
		          member.kind == AstKind::SpecialMemberDefinition))
		{
			// 12.1 and 12.4: a constructor or a destructor is a member whose
			// declaration writes no decl-specifier-seq and whose name is the
			// class's own, so it is read here rather than through the ordinary
			// declaration path, which would look that name up as a type.
			special_member(member, inner);
		}
		else
		{
			declaration(member, inner);
		}
		for (std::size_t at = before; at < scope.declarations.size(); ++at)
		{
			scope.declarations[at]->access = access;
		}
	}
	// 9.2p2: the class is complete here, so 7.3.3p14 can be asked of what it
	// declares rather than of what the body had written so far.
	hide_using_members(scope);
	// 10.3p1: whether the object holds a vpointer is a layout question, so it
	// is asked before 9.2p13 places anything.  What the declarations already
	// say is enough for it; which function each slot holds is settled once the
	// class has the members no declaration wrote.
	note_polymorphism(*entity, scope);
	lay_out_class(*entity, scope, tag == ClassTag::Union,
	              requested_alignment(node, inner), packing_of(node));
	// 8.5.1p1: a class with a base class or a virtual function is not an
	// aggregate, so a braced-init-list initializing an object of it chooses a
	// constructor.
	entity->aggregate = entity->base == nullptr && !entity->polymorphic &&
		aggregate_class(scope);
	if (semantics())
	{
		declare_special_members(*entity, scope);
	}
	return *entity;
}

// The typed facts of a node the analysis builds rather than reads: a
// constructor call has no expression in the source to be spelled from, and the
// lowering reads facts and never text.
void SemaAnalyzer::set_fact(DumpNode& node, FactKind kind, TypeId type,
                            ValueCategory category)
{
	node.fact.kind = kind;
	node.fact.type = type;
	node.fact.spelled = type;
	node.fact.category = category;
}

// 7.1.3p2: the declarator-id of the first declarator of a declaration, which
// is the name an unnamed class or enumeration in its specifiers takes.
std::string SemaAnalyzer::name_from_declarators(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& init = *node.children[index];
		if (init.children.empty())
		{
			continue;
		}
		const AstNode* id = declarator_id(*init.children[0]);
		if (id != nullptr)
		{
			return id->text;
		}
	}
	return std::string();
}

DumpNode& SemaAnalyzer::open_fact(DumpNode& parent, const std::string& text,
                                  FactKind kind)
{
	DumpNode& node = model_.open_node(parent, text);
	node.fact.kind = kind;
	return node;
}

void SemaAnalyzer::write_line(DumpScope& dump, const char* what,
                              const std::string& name, TypeId type)
{
	dump.lines.push_back(std::string(what) + " " + name + " " +
	                     types_.description(type));
}

void SemaAnalyzer::write_entity_line(DumpScope& dump, const SemaEntity& entity)
{
	switch (entity.kind)
	{
	case SemaKind::Class:
	case SemaKind::Enum:
	case SemaKind::TemplateType:
		write_line(dump, "type", entity.name, entity.type);
		return;

	case SemaKind::Typedef:
		write_line(dump, "type-alias", entity.name, entity.type);
		return;

	case SemaKind::Function:
		write_line(dump, "function", entity.name, entity.type);
		return;

	case SemaKind::Parameter:
		write_line(dump, "parameter", entity.name, entity.type);
		return;

	case SemaKind::Enumerator:
		dump.lines.push_back("enumerator " + entity.name + " " +
		                     types_.description(entity.type) + " " +
		                     spell_value(entity.type, entity.value));
		return;

	case SemaKind::Variable:
		write_line(dump, "variable", entity.name, entity.type);
		return;

	default:
		// A namespace and an alias of one have no line of their own.
		return;
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
		? "__anonymous_enum" + decimal(++anonymous_enums_, false)
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
			const Constant written = evaluate(*child.children[0], inner);
			value = written.bits;
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

// 16.6: the packing alignment the definition `node` is laid out under.
//
// The directive is a fact of a position in the source and the layout is a fact
// of a class, so the one thing that joins them is a position the class has.
// That position is the `}` the definition ends at, because 9.2p2 completes the
// class there and the layout is settled once, from every member at once - which
// is also what a directive written between two members means for the class it
// is written in.  It is the class-specifier's own `completed` and not the end
// of its span, because the span of a class-specifier that is a whole
// declaration reaches past the `;`, and a directive written between the `}` and
// that `;` is one the class was already complete before.  A class defined with
// no such directive anywhere in the unit asks an empty table, which answers
// without a search.
unsigned long long SemaAnalyzer::packing_of(const AstNode& node) const
{
	return packs_ == nullptr || packs_->empty()
		? 0
		: packs_->at(node.completed);
}

// 2.2p1: whether the reading was in this unit's own source where `node` begins.
// A unit that includes nothing has an empty table and answers without a search,
// and so does every mode that hands the analysis no table at all.
bool SemaAnalyzer::own_source(const AstNode& node) const
{
	return sources_ == nullptr || sources_->empty() ||
		sources_->own_at(node.begin);
}

void SemaAnalyzer::simple_declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const AstNode* list = child_of(node, AstKind::InitDeclaratorList);
	const std::string declared =
		list == nullptr ? std::string() : name_from_declarators(*list);
	// 11p6: the access every name here is checked with is the one the entity
	// being declared has, which for a static data member defined outside its
	// class reaches what the class declared private.
	const Naming naming(*this, naming_context(declared, ctx));
	Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, declared);
	if (list == nullptr)
	{
		if (specifiers.is_friend)
		{
			// 11.3p2: a friend declaration with no declarator names a class,
			// and what it does is grant rather than declare.
			grant_class_friendship(ctx, specifiers);
			return;
		}
		inject_anonymous_members(specifiers.introduced, ctx, span,
			                         specifiers.is_static);
		return;
	}
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& init = *list->children[index];
		init_declarator(*init.children[0], child_of(init, AstKind::Initializer),
		                specifiers, ctx);
	}
}

// 6.4p3: a condition declares its name in a region that encloses the statement's
// substatements, which is the region the statement itself is written in.
void SemaAnalyzer::condition_declaration(const AstNode& node, const Context& ctx)
{
	Span span;
	span.begin = node.begin;
	span.end = node.end;
	const Specifiers specifiers =
		read_specifiers(*node.children[0], ctx, span, true, std::string());
	const AstNode* declarator = child_of(node, AstKind::Declarator);
	if (declarator != nullptr)
	{
		init_declarator(*declarator, child_of(node, AstKind::Initializer),
		                specifiers, ctx);
	}
}

// 8.3.5 and 13.1: one declarator that declares a function, from the point its
// type is known.  A friend declaration reaches here too: 11.3p6 already put
// `target` on the region around the class, and `granting` is the class whose
// access the declaration carries.
void SemaAnalyzer::declare_function_declarator(
	const AstNode& node, const std::string& name, TypeId type,
	const QualifiedName& spelled, const Specifiers& specifiers,
	const Context& target, SemaEntity* granting,
	std::vector<Parameter>& spelled_parameters, const AstNode* initializer)
{
	// 9.3.1p3: a member function is called on an object, which is a
	// parameter of it that the declarator does not write.
	const TypeId written_type = type;
	type = with_object_parameter(type, node, target, specifiers.is_static, name,
	                             spelled.qualified());
	SemaEntity& function =
		declare_function(name, type, target, false,
		                 granting != nullptr && !spelled.qualified(),
		                 type != written_type);
	// 15.4p1: one declaration written with a non-throwing
	// exception-specification is what says the function throws nothing,
	// however the others were written.
	function.nonthrowing = function.nonthrowing || declarator_nonthrowing(node);
	function.wrote_exception_specification =
		function.wrote_exception_specification ||
		declarator_writes_exception_specification(node);
	function.object_member = type != written_type;
	// 10.3p1 and 7.1.2p1: `virtual` is a fact of the function rather than of one
	// declaration of it - a definition written outside the class repeats
	// neither the keyword nor the virt-specifiers - so it accumulates over the
	// declarations, exactly as 7.1.2p2's `inline` beside it does, and the one
	// declaration that may write it is the one the class body makes.  9.4p1's
	// static member is the class's own to refuse, where the class is complete.
	require_virtual_placement(specifiers.is_virtual, &node, *target.scope,
	                          spelled.qualified(), name);
	function.virtual_function =
		function.virtual_function || specifiers.is_virtual;
	read_virt_specifiers(function, node, initializer);
	// 10.4p3: an abstract class shall not be used as a parameter type or as a
	// function return type, which is asked of the type the declarator built and
	// therefore of every declaration alike.
	require_no_abstract_boundary(written_type, name);
	if (!function.object_member)
	{
		require_operator_operand(name, type,
		                         target.scope->kind == ScopeKind::Class);
	}
	if (granting != nullptr)
	{
		// 11.3p1 and 3.4.2p2: the class grants this declaration its access,
		// and holds it as one of the declarations a lookup that reaches the
		// class finds even where no lookup written in a region does.
		model_.befriend(*granting, function);
		if (!spelled.qualified() && granting->scope != nullptr)
		{
			granting->scope->friend_functions.push_back(&function);
		}
	}
	// 3.5p4: and so does every name 7.3.1.1p1's unnamed namespace declares,
	// whether the declaration wrote `static` or not.
	function.internal_linkage = function.internal_linkage ||
		target.scope->unnamed_region ||
		(specifiers.is_static && target.scope->kind == ScopeKind::Namespace);
	// 7.1.2p2: one declaration of a function with `inline` makes it inline,
	// so the fact accumulates over the declarations of one entity.
	function.inline_function =
		function.inline_function || specifiers.is_inline;
	if (initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::SpecialInitializer)
	{
		// 8.4.2 and 8.4.3: `= default` asks for the definition 12.8 would have
		// given this special member, and `= delete` for a declaration every use
		// of is ill formed.  8.4.2p1 makes the first of them implicitly inline,
		// so the definition it stands for belongs to every translation unit
		// that needs one rather than to the one that wrote the class.
		//
		// 8.4.2p2 and 3.4.3p3: written outside the class, on a declarator-id
		// that declares nothing new, it is a definition of the declaration the
		// class already made - and 7.1.2p2 leaves that definition non-inline,
		// so this unit is the one that holds it.  A copy or move assignment
		// operator reaches 8.4.2p2 here and a constructor or a destructor
		// reaches it in `special_member_definition`; the two say the same
		// thing, because whether the definition is this unit's is a fact of
		// where it was written and not of which special member it defines.
		const bool outside_the_class = spelled.qualified();
		if (outside_the_class && (function.defaulted || function.deleted ||
		                          function.defined))
		{
			// 3.2p1: a definition written outside the class defines the
			// declaration that class already made, and no function has two
			// definitions - which `special_member_definition` says of a
			// constructor and a destructor in the same words.
			throw std::runtime_error(name + " is defined twice");
		}
		function.deleted = initializer->children[0]->text == "delete";
		function.defaulted = !function.deleted;
		function.defined = false;
		function.inline_function =
			function.inline_function || !outside_the_class;
		// 12.8p28: the definition the standard gives this declaration names the
		// object it reads from throughout, and the declarator is what said what
		// that name is - so the parameters it wrote travel to the definition,
		// exactly as a constructor's do.  A declaration that named none leaves
		// the definition to give it a name of its own.
		std::vector<Parameter> named = spelled_parameters;
		constructor_parameters_[function.id].swap(named);
		if (outside_the_class && !function.deleted && semantics())
		{
			// 3.2p4: the definition this unit was told to write is written
			// whether or not anything here names the function, because another
			// unit's use of it is what a non-inline definition is for.
			demand_transfer_definition(function);
		}
	}
	record_declared_parameters(function, spelled_parameters, target.scope);
	if (function.template_parameters != nullptr)
	{
		templates_[function.id].swap(spelled_parameters);
	}
	if (semantics())
	{
		// 14p1: a template is not a function; the unit has the ones its
		// instantiations declare, and the output describes those.
		if (target.node != nullptr &&
		    target.scope->kind != ScopeKind::TemplateParameters)
		{
			DumpNode& declared =
				open_fact(*target.node, "function-declaration " +
				          function.dump_name + " " +
				          types_.description(type),
				          FactKind::FunctionDeclaration);
			declared.fact.entity = &function;
			declared.fact.type = type;
		}
		return;
	}
	write_line(*target.dump, "function", name, type);
	return;
}

// 3.5, 3.7.1 and 3.7.2: where the object a declaration declares lives - which
// linkage its name has and which region ends its lifetime.  Both are facts of
// the variable rather than of the declaration that happened to spell them, so
// they are settled against the declaration of the same variable this region
// already holds as well as against this one's own specifiers.
void SemaAnalyzer::record_storage(SemaEntity& entity, const SemaEntity* prior,
                                  const Specifiers& specifiers,
                                  const Context& target, TypeId type)
{
	// 3.5p3: at namespace scope a name declared `static` has internal linkage,
	// and so does a `const` object no declaration wrote `extern`.
	// 3.5p4: a name an unnamed namespace declares has internal linkage however
	// the declaration was written.
	entity.internal_linkage = entity.internal_linkage ||
		target.scope->unnamed_region ||
		(target.scope->kind == ScopeKind::Namespace &&
		 (specifiers.is_static ||
		  (!specifiers.is_extern &&
		   (types_.object_cv(type) & kCvConst) != 0)));
	// 7.1.1p1: `thread_local` is a fact of the variable rather than of one
	// declaration of it, so every declaration of one variable writes it or none
	// does.  A declaration that disagrees with the one this region already
	// holds names a storage duration the variable does not have, whether it is
	// an `extern` declaration and the definition that follows it or a static
	// data member declared in its class and defined outside it.
	if (prior != nullptr && prior->thread_storage != specifiers.is_thread_local)
	{
		throw std::runtime_error(
			specifiers.is_thread_local
				? "a declaration of " + entity.name +
				      " writes thread_local where an earlier declaration of it "
				      "does not"
				: "a declaration of " + entity.name +
				      " leaves out the thread_local an earlier declaration of "
				      "it wrote");
	}
	// 3.7.2p1: `thread_local` on a declaration of a variable gives that
	// variable thread storage duration, so the fact belongs to the variable and
	// not to the declaration that happened to write the keyword - a static data
	// member is declared in its class and defined outside it, and 7.1.1p1 above
	// makes either spelling of it say the same thing.
	entity.thread_storage = entity.thread_storage || specifiers.is_thread_local;
	if (!semantics())
	{
		return;
	}
	// 3.7.1p3 and 3.7.2p1: a block-scope object declared `static` is one object
	// of the program and one declared `thread_local` is one per thread; neither
	// is an object of the block that declares it.  6.7p4 initializes it the
	// first time control passes through the declaration, and 3.6.3p1 or the end
	// of its thread destroys it.  Writing it as the automatic object of its
	// block would describe a different program, and the guard 6.7p4 asks for is
	// not part of this milestone - so it is refused wherever it is declared and
	// whatever its type, rather than only where a class of its own ends the
	// lifetime.
	if (entity.object_definition &&
	    (specifiers.is_static || entity.thread_storage) &&
	    target.scope->kind != ScopeKind::Namespace &&
	    target.scope->kind != ScopeKind::Class)
	{
		throw std::runtime_error(
			std::string("a block-scope ") +
			(entity.thread_storage ? "thread_local" : "static") + " object of " +
			types_.description(type) +
			" is declared, whose one initialization and its destruction this "
			"milestone does not write");
	}
}

void SemaAnalyzer::init_declarator(const AstNode& node,
                                   const AstNode* initializer,
                                   const Specifiers& specifiers,
                                   const Context& ctx)
{
	// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
	// looked up in the region that name reaches, so the region is settled from
	// the declarator-id before the declarator around it is read - which is what
	// lets `auto A::f() -> nested` and `void A::g(nested)` name a type the
	// class declares.
	const AstNode* const id = declarator_id(node);
	const std::string written_id = id == nullptr ? std::string() : id->text;
	const QualifiedName spelled(written_id);
	// 3.4.3p3: a declarator-id with a nested-name-specifier declares into the
	// region that names, wherever the declaration is written.
	Context target = ctx;
	// 14.1p1: and the parameters the head standing over *this* declaration
	// declared stand inside that region while the declarator is read, so
	// `template<class T> int n::f(T);` declares the template `n` declares and
	// reads `T` where it was written.  This is the declaration form of the same
	// rule `function_definition` reads a body under.
	Scope* const head =
		spelled.qualified() && ctx.template_head == ctx.scope ? ctx.scope
		                                                      : nullptr;
	Context looked_up = ctx;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
		target.template_head = head;
		looked_up.scope = head != nullptr ? head : target.scope;
		looked_up.dump = target.dump;
	}
	const StandingIn stood(head, *target.scope);
	std::string written;
	// 14.1: a template's declarator is the pattern its instantiations write
	// their own parameters from, and 8.3.6p4 makes a function declaration's
	// default-arguments the function's from that declaration on, whether or not
	// it is the one with the body.  Both read the parameter clause the
	// declarator already spelled, so it is captured here rather than read again.
	std::vector<Parameter> spelled_parameters;
	TypeId type = declarator_type(node, specifier_type(specifiers), looked_up,
	                              &written, &spelled_parameters,
	                              declares_object_member(specifiers));
	// 8.3.4p3: an array declared with no bound and initialized from a braced
	// list has as many elements as the list has clauses.
	if (types_.kind(type) == TypeKind::Array && !types_.bounded(type) &&
	    initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::BracedInitList)
	{
		type = types_.array_of(types_.target(type), true,
		                       initializer->children[0]->children.size());
	}
	const std::string name = spelled.last();
	if (name.empty())
	{
		return;
	}
	require_mutable_data_member(specifiers, target, name, type);
	// 7.1.2p1: `virtual` says how a call of a *function* is dispatched, so a
	// declarator that declares anything else may not carry it.
	if (specifiers.is_virtual && types_.kind(type) != TypeKind::Function)
	{
		throw std::runtime_error(name + " is declared `virtual` and is not a "
		                         "function");
	}
	// 11.3p6: what a friend declaration declares belongs to the region around
	// the class, so the declarator is read against that region and the class
	// gets the grant.
	SemaEntity* const granting =
		specifiers.is_friend ? friend_target(ctx, spelled, target) : nullptr;

	if (specifiers.is_typedef)
	{
		declare_type_alias(name, type, *target.scope);
		if (semantics())
		{
			if (target.node != nullptr)
			{
				open_fact(*target.node, "type-alias " + name + " " +
				          types_.description(type), FactKind::TypeAlias);
			}
			return;
		}
		write_line(*target.dump, "type-alias", name, type);
		return;
	}
	if (types_.kind(type) == TypeKind::Function)
	{
		declare_function_declarator(node, name, type, spelled, specifiers,
		                            target, granting, spelled_parameters,
		                            initializer);
		return;
	}
	if (initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::SpecialInitializer)
	{
		throw std::runtime_error(name + " is not a function and is declared "
		                                "with `= default` or `= delete`");
	}

	// 14p1: a template-declaration declares no object.  A declarator that is
	// not a function names storage an argument list is what makes, so the
	// declaration is a pattern here exactly as a class template's is, and the
	// specialization a use names is what a later milestone gives storage to.
	// What says the head is this declaration's own is the region it stands in:
	// a name a function template's body declares belongs to a region of that
	// body.  And what tells the pattern from 14.5.1.3p1's static data member is
	// the region the declaration *belongs* to rather than the spelling that
	// reached it: 9.4.2p2's member is defined into the class its qualified
	// declarator-id names - which `record_template` already took - while a
	// qualified name reaching a namespace declares the same pattern an
	// unqualified one there does, and laying out storage for it would name an
	// object of a type an argument list has yet to say.
	if (ctx.template_head == ctx.scope && target.scope->kind != ScopeKind::Class)
	{
		return;
	}

	declare_object_declarator(initializer, specifiers, ctx, target, spelled,
	                          written, type);
	if (checking_ > 0 && initializer != nullptr)
	{
		// 14.6p8 and 3.4p1: an initializer is an expression of this definition
		// exactly as the operand of a `return` is, so the names it writes are
		// looked up where the definition stands too.  3.3.2p1 puts its point
		// after the declarator, which is why it is read once the declarator
		// this one belongs to has declared its name.
		check_expression_names(*initializer, ctx);
	}
}

// 3.1p2 and 8.5: the object a declarator that is not a function declares, from
// the point its type and the region it belongs to are known.  The declaration
// is what says whether it is also a definition, what its lifetime is, and which
// of 8.5's initializations builds it.
void SemaAnalyzer::declare_object_declarator(const AstNode* initializer,
                                             const Specifiers& specifiers,
                                             const Context& ctx,
                                             const Context& target,
                                             const QualifiedName& spelled,
                                             const std::string& written,
                                             TypeId type)
{
	const std::string name = spelled.last();
	// 7.1.5p9: a constexpr object is a const object.
	if (specifiers.is_constexpr)
	{
		type = types_.qualified(type, kCvConst);
	}
	// 3.3.2 and 9.4.2p2: a declarator-id with a nested-name-specifier defines
	// the object that region already declares - a static data member is
	// declared in its class and defined outside it - rather than declaring a
	// second one there, so what its first declaration said about it stands.
	// The declaration that region already has is also what 7.1.1p1's
	// `thread_local` has to agree with, wherever this one is written, so it is
	// found before this declaration is bound over it.
	SemaEntity* const prior = redeclared(target, name, SemaKind::Variable);
	SemaEntity* const declared = spelled.qualified() ? prior : nullptr;
	SemaEntity& entity = declared != nullptr
		? *declared
		: model_.create(SemaKind::Variable, name, type);
	if (initializer != nullptr && !initializer->children.empty() &&
	    (types_.cv(type) & kCvConst) != 0 && arithmetic_type(type) != kNoType)
	{
		// 5.19p3: a const object of integral type initialized by a constant
		// expression is one, and is what an array bound may be written with.
		// An initializer that is an ordinary expression leaves it an object
		// like any other; an initializer that is ill formed is still ill
		// formed, so only the one failure is caught.
		try
		{
			entity.value = convert(evaluate(*initializer->children[0], ctx),
			                       type).bits;
			entity.constant = true;
		}
		catch (const NotConstant&)
		{
			entity.constant = false;
		}
	}
	if (declared == nullptr)
	{
		require_no_template_parameter(name, *target.scope);
		entity.c_linkage = c_linkage_;
		model_.bind(*target.scope, name, entity);
		model_.declare_in(*target.scope, entity);
		// The qualified spelling a use of the name writes, built here as it is
		// for a function, because that is where the regions around it are known.
		name_in_region(entity, *target.scope, name);
		// 9.2p1: a data member is part of an object of its class and is reached
		// through one, which 9.4p2 makes untrue of a member declared `static`:
		// that one is a variable the class names.
		entity.object_member =
			target.scope->kind == ScopeKind::Class && !specifiers.is_static;
		// 7.1.1p10: the const of the object holding this member stops at it,
		// which the declaration is where 5.2.5p4 reads.
		entity.mutable_member = specifiers.is_mutable;
		// 7.6.2p1: what an alignment-specifier on the declaration asked for is
		// a fact about what it declares, which 9.2p13's layout reads.
		entity.requested_align = specifiers.alignment;
		// 12.6.2p8: a brace-or-equal-initializer on a non-static data member is
		// what initializes it wherever a constructor does not say otherwise, and
		// 8.5.1p1 makes a class that has one no aggregate.
		entity.default_initializer = entity.object_member &&
			initializer != nullptr && !initializer->children.empty();
		if (entity.default_initializer)
		{
			// 12.6.2p8 and 9.2p2: the initializer is read by every constructor
			// that does not name the member, in the complete-class context the
			// member was declared in rather than where the constructor is.
			HeldInitializer& held = member_initializers_[entity.id];
			held.written = initializer->children[0];
			held.scope = target.scope;
		}
	}
	// 3.1p2: an `extern` declaration with no initializer declares the object
	// and does not define it; every other declaration of one at namespace scope
	// does, and a later definition of the same object says so once.  9.4.2p2
	// makes the declaration a static data member's class writes no definition
	// of it however it was written, so the one that defines it is the one
	// written outside the class, which is the one whose declarator-id carries
	// the nested-name-specifier that named the class.
	const bool defines_object =
		(target.scope->kind != ScopeKind::Class || spelled.qualified()) &&
		(!specifiers.is_extern ||
		 (initializer != nullptr && !initializer->children.empty()));
	entity.object_definition = entity.object_definition || defines_object;
	// 10.4p2: a class with a pure final overrider has no objects, so the
	// declarations that lay one out are refused.  9.4.2p2's declaration of a
	// static data member lays none out - the definition written outside the
	// class does, and that one is a definition like any other - while 9.2p1's
	// non-static data member is an object of every object of its class.
	if (defines_object ||
	    (target.scope->kind == ScopeKind::Class && !specifiers.is_static))
	{
		require_creatable_object(type, name);
	}
	record_storage(entity, prior, specifiers, target, type);
	// 9.4.2p2: a definition written with a nested-name-specifier declares
	// nothing where it names, so the line it writes is not one of that region's:
	// it stands where the definition is written, spelled the way it wrote it.
	// The region it names keeps the one line its own declaration wrote.
	const bool defines_elsewhere = declared != nullptr;
	const Context& where = defines_elsewhere ? ctx : target;
	const std::string& spelling = defines_elsewhere ? written : name;
	if (!semantics())
	{
		write_line(*where.dump, "variable", spelling, type);
		return;
	}
	if (where.node == nullptr)
	{
		// 9.2p1: a data member declares no object of its own; the object it is
		// part of is what a declaration of the class type declares.
		return;
	}
	DumpNode& line = open_fact(*where.node, "variable " + spelling + " " +
	                           types_.description(type), FactKind::Variable);
	line.fact.entity = &entity;
	line.fact.type = type;
	line.fact.object_definition = line.fact.object_definition || defines_object;
	const AstNode* const clause =
		initializer == nullptr || initializer->children.empty()
			? nullptr
			: initializer->children[0];
	// 12.8p31 and 5.2.3p3: `T x = T{...}` creates `x` itself, so the braced
	// list is what initializes it and the initialization is the one the same
	// list written on the declarator would be - which for an aggregate is
	// 8.5.1's and not a constructor's.  The prvalue was direct-list-initialized
	// where it stands, so 8.5.4p3 asks nothing of the list here either.
	const AstNode* const elided = clause == nullptr
		? nullptr
		: braced_prvalue_of(*clause, type, ctx);
	const AstNode* const value = elided != nullptr ? elided : clause;
	const bool copied =
		elided == nullptr && initializer != nullptr && initializer->copied;
	if (types_.is_class(element_of(types_.strip_cv(type))) &&
	    entity.object_definition)
	{
		// 3.8p1: the end of the lifetime of an object of class type is an
		// action of the region that declared it, whatever form its initializer
		// took.  An aggregate initialized from a braced-init-list is written
		// below rather than by a constructor, and its lifetime still ends.
		record_lifetime(entity, target, line);
	}
	if (types_.is_class(types_.strip_cv(type)) && entity.object_definition &&
	    !(value != nullptr && value->kind == AstKind::BracedInitList &&
	      aggregate_type(type)))
	{
		// 8.5 and 12.1: an object of class type is initialized by one of the
		// constructors of its class, whatever form the initializer took, unless
		// 8.5.1 makes the class an aggregate initialized from the clauses of a
		// braced-init-list.
		construct_object(entity, line, value, ctx, Placement::Named, copied);
		return;
	}
	if (entity.object_definition && element_constructed(type, value))
	{
		// 12.6p1: an array of class type is initialized by constructing each of
		// its elements, and where no clause named one the constructor every
		// element is given is the same one.  The action names the array, so
		// there is one of it however many elements there are.
		construct_object(entity, line, value, ctx, Placement::Named, copied);
		return;
	}
	// 5.19p3 and 7.1.5p9: a constexpr object is initialized by a constant
	// expression, and the dump writes the value it stands for rather than the
	// expression that computed it.
	if (value == nullptr)
	{
		// 9.4.2p3: the class declared the member with a brace-or-equal-initializer
		// and 9.4.2p2's definition at namespace scope shall write none of its own,
		// so the object that definition gives the member holds the value the class
		// wrote - which is a fact of the member and not of either declaration's
		// syntax, and is what makes every unit that defines it define one object.
		if (defines_object && declared != nullptr && entity.constant &&
		    entity.region != nullptr &&
		    entity.region->kind == ScopeKind::Class)
		{
			DumpNode& held = model_.open_node(
				line, spell("literal", ValueCategory::PRValue, type,
				            spell_value(type, entity.value)));
			set_fact(held, FactKind::Literal, type, ValueCategory::PRValue);
			held.fact.constant = true;
			held.fact.value = entity.value;
			return;
		}
		// 8.5p6: an object of any other type with no initializer holds no value
		// the program may read, and there is nothing to describe.
		return;
	}
	if (entity.constant && specifiers.is_constexpr)
	{
		model_.open_node(line, spell("literal", ValueCategory::PRValue, type,
		                             spell_value(type, entity.value)));
		return;
	}
	// 3.6.2p2 and 3.7.1p1: an object at namespace scope, and the static data
	// member 9.4.2p2 defines there, is given its value by the program image
	// rather than built where its declaration stands.
	write_initializer(*value, type, ctx, line,
	                  entity.object_definition &&
	                  (target.scope->kind == ScopeKind::Namespace ||
	                   target.scope->kind == ScopeKind::Class));
	// 12.2p5: where that initializer bound this reference to a temporary, the
	// temporary's lifetime is the reference's from here on.
	extend_bound_temporary(type, ctx, line);
}

void SemaAnalyzer::write_initializer(const AstNode& initializer, TypeId type,
                                     const Context& ctx, DumpNode& line,
                                     bool image)
{
	if (initializer.kind == AstKind::ParenInitializer)
	{
		// 8.5p16: direct-initialization from one expression, which differs from
		// copy-initialization in one thing only - 12.3.2p2 lets it choose a
		// conversion function declared `explicit`.
		if (!initializer.children.empty())
		{
			initialize(*initializer.children[0], type, ctx, line, false,
			           Requested::Written, true);
		}
		return;
	}
	if (initializer.kind == AstKind::BracedInitList)
	{
		// 8.5.1: an aggregate is initialized from the clauses of its list, each
		// initializing one subobject.  3.6.2p2 makes that initialization the
		// value of the object's own storage where the object has static storage
		// duration, so no element of it is an object a function builds.
		list_initialize(initializer, type, ctx, line, image);
		return;
	}
	// The same reading a list standing where an expression initializes an
	// object gets.
	initialize(initializer, type, ctx, line);
}

void SemaAnalyzer::statement(const AstNode& node, const Context& ctx)
{
	switch (node.kind)
	{
	case AstKind::CompoundStatement:
	{
		DumpScope& dump = model_.open_dump(*ctx.dump, "scope block");
		Context inner;
		inner.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, &dump);
		inner.dump = &dump;
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			statement(*node.children[index], inner);
		}
		return;
	}

	case AstKind::SimpleDeclaration:
	case AstKind::AliasDeclaration:
	case AstKind::UsingDeclaration:
	case AstKind::UsingDirective:
	case AstKind::NamespaceAliasDefinition:
	case AstKind::StaticAssertDeclaration:
	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
	case AstKind::EnumSpecifier:
		declaration(node, ctx);
		return;

	case AstKind::ConditionDeclaration:
		condition_declaration(node, ctx);
		return;

	case AstKind::IfStatement:
	case AstKind::SwitchStatement:
	case AstKind::WhileStatement:
	case AstKind::DoStatement:
	case AstKind::ForStatement:
	case AstKind::TryBlock:
	case AstKind::Handler:
	case AstKind::LabeledStatement:
	case AstKind::CaseStatement:
	case AstKind::DefaultStatement:
	case AstKind::Then:
	case AstKind::Else:
	case AstKind::Iteration:
	case AstKind::ForInitStatement:
	case AstKind::Condition:
		// 3.3.3: a statement with a substatement encloses it, and PA11 models
		// of a statement only the regions it opens.
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			statement(*node.children[index], ctx);
		}
		return;

	default:
		// An expression declares nothing PA11 describes.
		if (checking_ > 0)
		{
			// 14.6p8: a template definition is read where it stands, and what
			// the expressions of its body say about names is part of what it
			// says.
			check_expression_names(node, ctx);
		}
		return;
	}
}
