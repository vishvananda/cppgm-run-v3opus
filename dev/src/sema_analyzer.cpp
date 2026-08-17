#include "sema_analyzer.h"

#include <ostream>
#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_constexpr.h"
#include "sema_derivation.h"
#include "sema_operator.h"
#include "sema_string_init.h"
#include "sema_template_head.h"

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
	, unit_dialect_(dialect)
	, packs_(nullptr)
	, sources_(nullptr)
	, written_(nullptr)
	, anonymous_enums_(0)
	, local_types_(0)
	, resettle_classes_(false)
	, instantiating_class_(0)
	, instantiated_body_(0)
	, template_pattern_(nullptr)
	, template_pattern_dump_(nullptr)
	, instantiating_(nullptr)
	, checking_(0)
	, stood_in_(0)
	, unevaluated_(0)
	, declared_only_(0)
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
	, stands_in(nullptr)
	, head(nullptr)
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
	, real(0)
	, entity(nullptr)
	, op(0)
	, operands(kNoType)
	// 13.3.1p4: an object argument no member call built names an object, and
	// every object a name reaches is an lvalue.
	, object_category(ValueCategory::LValue)
	, through_using(false)
	, braced(nullptr)
	, clauses(0)
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
	// 8.4.2p2: every definition the program wrote outside a class has been read
	// here, so the classes this unit completed before one of them arrived are
	// asked their answers again before anything that reads one is written.
	resettle_completed_classes();
	// 3.6.3p1: the objects with static storage duration this unit constructed
	// are destroyed when the program ends, in the reverse order of their
	// construction.  Asking for the destructors here is what makes their
	// definitions part of the run of pending ones written below.
	for (std::size_t index = static_lifetimes_.size(); index-- > 0;)
	{
		destructor_action(*static_lifetimes_[index], *ctx.node, Placement::Named);
	}
	// 3.7.2p2 and 3.6.3p1: an object with thread storage duration is destroyed
	// when its own thread ends, and a block-scope `static` when the program
	// does; both are points the program hands to the runtime where the object
	// is initialized.  The action stands under the declaration, after
	// everything else that declaration wrote, and in declaration order: the
	// runtime ends the objects in the reverse of the order it was handed them,
	// which is the order they were begun in.
	for (std::size_t index = 0; index < declared_lifetimes_.size(); ++index)
	{
		destructor_action(*declared_lifetimes_[index].entity,
		                  *declared_lifetimes_[index].line, Placement::Named);
	}
	write_pending_definitions();
	// 12.6.2p6: every delegation this unit wrote is settled now, so the chain
	// each one heads is walked here rather than at each definition, where a
	// constructor further along may not have been read yet.
	check_delegation_cycles();
}

SemaEntity& SemaAnalyzer::declared_member(SemaEntity& entity)
{
	return entity.shadowed != nullptr ? *entity.shadowed : entity;
}

const SemaEntity& SemaAnalyzer::declared_member(const SemaEntity& entity)
{
	return entity.shadowed != nullptr ? *entity.shadowed : entity;
}

const SemaEntity& SemaAnalyzer::wrote_defaults(const SemaEntity& entity)
{
	return entity.primary != nullptr ? *entity.primary
	                                 : declared_member(entity);
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
	// 14.5.3p4: a pack expanded into no place at all is one entry of this list
	// and no place of the function, so what the two lists are counted apart by
	// is the places rather than the entries.
	std::size_t written = 0;
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		written += types_.is_settled_run(declared[index].type) ? 0u : 1u;
	}
	const std::size_t implicit = total > written ? total - written : 0;
	std::size_t place = 0;
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		if (types_.is_settled_run(declared[index].type))
		{
			continue;
		}
		// 8.3.5p10: a parameter's name is no part of the function's type, so no
		// two declarations of one function need agree about it and one that
		// wrote none still declares the parameter.  The name the object file
		// writes is therefore the function's rather than the declaration's, and
		// the first one any declaration gave is it.  It is only the object file
		// that asks, so the earlier dialects, whose dumps describe declarations
		// one at a time, are left writing what each of them wrote.
		const std::size_t at = place + implicit;
		++place;
		const bool names = lowering() && !declared[index].name.empty();
		const bool takes = lowering() && declared[index].name.empty();
		if (declared[index].initializer == nullptr && !names && !takes)
		{
			continue;
		}
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
		where.scope = &TemplateHead(*this).open_bindings(
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
// 12.1 and the ABI: what an initialization creates is a complete object - one a
// declaration named, or the member subobject that is one of its own - so it
// runs the complete-object entry of the constructor.  A base class subobject is
// the one case that runs the base-object entry instead.
void SemaAnalyzer::note_construction_entry(SemaEntity& constructor, bool base)
{
	if (!base)
	{
		constructor.complete_object_entry = true;
		return;
	}
	const Scope* const region = constructor.region;
	if ((constructor.transfer == kCopyConstructorTransfer ||
	     constructor.transfer == kMoveConstructorTransfer) &&
	    constructor.trivial && region != nullptr && region->owner != nullptr &&
	    types_.has_vacuous_destruction(region->owner->type))
	{
		// 12.8p12 and 12.4p8: what carries this base subobject is the bytes of
		// the one it reads from, so no entry point of the member runs and the
		// object file owes neither of them for it.  The definition it has is
		// still the program's where the program wrote one, which is 9.3p2's
		// question and not this one.
		return;
	}
	constructor.base_object_entry = true;
	// 14.7.1p1: a base subobject the program itself wrote the construction of
	// is what asks this unit for the whole of an instantiated definition, entry
	// points and all - one written inside another instantiation asks for the
	// entry it names, because that instantiation owes the rest.
	constructor.source_base_entry =
		constructor.source_base_entry || instantiated_body_ == 0;
}

void SemaAnalyzer::note_destruction_entry(SemaEntity& destructor, bool base)
{
	// 14.7.1p1 and 12.4p11: ending the lifetime of an object is a use of its
	// class's destructor, so a specialization's is asked for here.
	require_definition(destructor);
	if (base)
	{
		destructor.base_object_entry = true;
		destructor.source_base_entry =
			destructor.source_base_entry || instantiated_body_ == 0;
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
	SemaEntity* const ends = class_destructor(types_.element_of(type));
	if (ends == nullptr || ends->trivial || ends->deleted)
	{
		return;
	}
	const TypeId bare = types_.strip_cv(type);
	unwind_subobjects_.push_back(ends);
	if (types_.kind(bare) == TypeKind::Array &&
	    types_.object_size(bare) >
	        types_.object_size(types_.strip_cv(types_.element_of(bare))))
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
		// 14.7.1p1: a body an instantiation made is read here as one the
		// program wrote is read where it stands, and the difference between
		// them is what 3.2p3 asks of the uses written inside.  A body reached
		// from this walk never stands inside another - what it names joins the
		// list rather than being read where it is named - so the mark is set
		// per entry rather than saved and restored around a nest.
		instantiated_body_ =
			instantiated_declaration(*pending_[index].function, types_) ? 1 : 0;
		write_definition(pending_[index]);
		instantiated_body_ = 0;
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
	// 14.5.1.3p1 and 14.1p2: a definition written outside its class is read
	// with its own head standing between that class and the region around it,
	// and 14.7.1p1 leaves the body until long after the reading that made the
	// declaration - so the link that reading held is put back here.
	const EnclosedBy stands_in(pending.stands_in, pending.head);
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
	const unsigned stood = stood_in_;
	Constant value;
	try
	{
		value = evaluate(*node.children[0], ctx);
	}
	catch (const NotConstant&)
	{
		// 14.6p8: the reading ran out on the stand-in rather than on what the
		// program wrote - `make().v` for a `make` the pattern declares - so the
		// condition is the arguments' to settle like any other.
		if (checking_ == 0 || stood_in_ == stood)
		{
			throw;
		}
		return;
	}
	if (stood_in_ != stood)
	{
		// 14.6p8 and 7p4: the condition names something an argument list has
		// yet to settle, so the reading stood a value in its place and what it
		// came out as says nothing.  The instantiation evaluates it again with
		// the arguments, which is where the assertion is made.
		return;
	}
	// 7p4 with 4p3: the condition is contextually converted to `bool`, which
	// for an object of class type is 12.3.2p1's conversion function.
	if (!ConstexprReading(*this).truth(value))
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
	if (node.kind == AstKind::NonTypeTemplateParameter)
	{
		// 14.1p4: a place that binds a value, whose type its own
		// decl-specifier-seq and declarator write in this region.  It is
		// declared for the same reason a type place is - the body looks its
		// name up here - and no line is written for it, because the dump of
		// this region names the types it declares.
		non_type_template_parameter(node, ctx);
		return;
	}
	if (node.kind != AstKind::TypeParameter)
	{
		// 14.1p1's template parameter and 14.5.3's pack belong to a later
		// milestone.
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
	// 14.5.3p1: a place declared with `...` stands for a run of arguments, and
	// every name written for it shall be expanded - which is a fact of the type
	// the place declared, so a reading of an expansion finds it without the head.
	types_.set_template_pack(type, has_child(node, AstKind::ParameterPack));
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

// 14.1p4: a non-type template parameter, declared into the region its head
// opened.
//
// It stands for its place exactly as a type parameter does - the ABI writes a
// `<template-param>` for either - and what tells the two apart is the type of
// the value it names, which is a fact of the place and is what a region binding
// an argument to it reads.  14.6.2p2 leaves the value itself unknown while the
// pattern is being read, so the declaration carries no constant here: an
// argument list is what gives it one.
void SemaAnalyzer::non_type_template_parameter(const AstNode& node,
                                               const Context& ctx)
{
	const std::string name = TemplateHead::non_type_name(node);
	const TypeId type = types_.template_parameter_type(
		model_.type_entity_id(), false,
		name.empty()
			? "#" + std::to_string(ctx.scope->declarations.size())
			: name);
	types_.set_template_index(
		type, static_cast<unsigned>(ctx.scope->declarations.size()));
	types_.set_template_pack(type, has_child(node, AstKind::ParameterPack));
	types_.set_parameter_value_type(type, TemplateHead(*this).non_type_type(node, ctx));
	// 14.1p3: a place its head left unnamed still takes an argument, and a
	// function template's places are counted from the region - so it is
	// declared under a name nothing writes, and 14.1p9's default is its own
	// however unreachable the name is.
	SemaEntity& entity = model_.create(SemaKind::TemplateValue, name, type);
	if (!name.empty())
	{
		model_.bind(*ctx.scope, name, entity);
	}
	model_.declare_in(*ctx.scope, entity);
	const AstNode* const written =
		child_of(node, AstKind::DefaultTemplateArgument);
	if (written != nullptr && !written->children.empty())
	{
		// 14.1p9: the value this place takes where the use wrote none, which is
		// an expression rather than a type-id and is read where the deduction
		// finds the place empty.
		parameter_defaults_.insert(
			std::make_pair(entity.id, written->children[0]));
	}
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

// 14.6.2.1p9: a class or an enumeration declared in the current instantiation
// is itself a dependent type, because what its members come to and what an
// object of it holds is what the enclosing argument list settles - so a
// template-id written over it names no class yet, and a base-specifier that
// writes one is 14.6.2p3's dependent base.  The question is asked of the region
// the declaration belongs to and answered once, where the type is made: a class
// nested two deep is reached through a level that was asked the same question.
void SemaAnalyzer::note_nested_in_dependent(TypeId type, const Scope& where)
{
	if (where.kind == ScopeKind::Class && where.owner != nullptr &&
	    types_.is_dependent(where.owner->type))
	{
		types_.set_nested_in_dependent(type);
	}
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
	note_nested_in_dependent(type, *ctx.scope);
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
		// 3.4.1p8: a name written in the base-clause of a class defined outside
		// the class it is a member of is looked up in the region its
		// class-head-name reached, and not in the one the definition stands in
		// - so a member type of the enclosing class is found there, and 3.3.2p5
		// puts this class's own name in that region too, because a class first
		// declared by a class-head is declared immediately after its
		// class-head-name.  The two contexts are the same wherever the
		// class-head-name wrote no nested-name-specifier.
		Derivation(*this).read_base_clause(*bases, *entity, scope, outer,
		                                   header);
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
	entity->aggregate = entity->bases.empty() && !entity->polymorphic &&
		aggregate_class(scope);
	if (semantics())
	{
		declare_special_members(*entity, scope);
	}
	// 9.2p2 and 15.4p1: an exception-specification is one of the contexts a
	// class is regarded as complete in, so a condition a member's declarator
	// wrote and the reading there could not answer is folded here, where the
	// class-specifier closes and every member it declares can be named.  It
	// stands outside the reading above because 15.4p1's answer is asked of a
	// declaration in every dialect - 15.4p3's two declarations of one function
	// are compared wherever a definition is read - and not only where a class
	// is given the members no declaration wrote.
	ConstexprReading(*this).settle_specifications(scope, inner);
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
	const Specifiers specifiers =
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
		                specifiers, ctx, &init);
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
	function.nonthrowing =
		function.nonthrowing || declarator_nonthrowing(node, target);
	// 9.2p2: an exception-specification is a complete-class context, so a
	// condition the fold above could not answer is kept for the closing brace.
	ConstexprReading(*this).defer_specification(function, node, target);
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
		OperatorCall(*this).require_operand(name, type,
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
	// 7.1.5p1 and p2: `constexpr` is a fact of the function, and it makes the
	// function implicitly inline - so a declaration that wrote it says both,
	// wherever among the declarations of the function it stands.
	function.constexpr_function =
		function.constexpr_function || specifiers.is_constexpr;
	function.inline_function =
		function.inline_function || specifiers.is_constexpr;
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
		if (outside_the_class)
		{
			function.own_source_definition = own_source(node);
			function.out_of_class_definition =
				holds_written_definitions(*target.scope);
			// 8.4.2p2 and 12.8p12: the class was complete before this
			// definition was read, so what the standard's definition of the
			// member comes to is settled again against it.
			resettle_defaulted_member(function);
		}
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
                                  const Context& target, TypeId type,
                                  const AstNode* whole)
{
	// 3.5p3: at namespace scope a name declared `static` has internal linkage,
	// and so does a `const` object that is neither explicitly declared `extern`
	// nor *previously declared to have external linkage* - so `extern const int
	// k;` written above the definition is what makes the definition's own name
	// one another unit may name, and the linkage is a fact of the variable and
	// not of the declaration that happened to spell the `const`.
	// 3.5p4: a name an unnamed namespace declares has internal linkage however
	// the declaration was written.
	const bool declared_external = prior != nullptr && !prior->internal_linkage;
	entity.internal_linkage = entity.internal_linkage ||
		target.scope->unnamed_region ||
		(target.scope->kind == ScopeKind::Namespace &&
		 (specifiers.is_static ||
		  (!specifiers.is_extern && !declared_external &&
		   (types_.object_cv(type) & kCvConst) != 0 &&
		   (types_.object_cv(type) & kCvVolatile) == 0)));
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
	// is an object of the block that declares it.  Writing it as the automatic
	// object of its block would describe a different program, so the storage
	// duration is recorded on the variable here - 6.7p4's initialization the
	// first time control reaches the declaration and 3.6.3p1's destruction at
	// the end of the program are what the lowering then writes for it.
	//
	// 3.7.2p2's one object per thread is left refused: the storage the ABI
	// gives it is reached through a wrapper of its own, which is a second
	// question from the one 6.7p4 asks and which nothing in a block has yet
	// needed.
	if (!entity.object_definition ||
	    (!specifiers.is_static && !entity.thread_storage) ||
	    target.scope->kind == ScopeKind::Namespace ||
	    target.scope->kind == ScopeKind::Class)
	{
		return;
	}
	if (entity.thread_storage)
	{
		throw std::runtime_error(
			"a block-scope thread_local object of " + types_.description(type) +
			" is declared, whose one initialization and its destruction this "
			"milestone does not write");
	}
	entity.local_static = true;
	if (whole != nullptr && entity.declared_end == 0)
	{
		// 3.7.1p3: the storage is the program's and the name is one block's, so
		// what the image calls it is where it was written.  The first
		// declaration of the object is what settles that, exactly as the first
		// region it is recorded in settles its name.
		entity.declared_begin = whole->begin;
		entity.declared_end = whole->end;
	}
}

void SemaAnalyzer::init_declarator(const AstNode& node,
                                   const AstNode* initializer,
                                   const Specifiers& specifiers,
                                   const Context& ctx,
                                   const AstNode* whole)
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
	// list has as many elements as the list has clauses - which 14.5.3p4 makes
	// a question about the runs its clauses stand for and not about the syntax.
	if (types_.kind(type) == TypeKind::Array && !types_.bounded(type) &&
	    initializer != nullptr && !initializer->children.empty())
	{
		// 8.5.2p1: an array of unknown bound initialized by a string literal
		// has as many elements as the literal has code units, the terminating
		// one among them - whether or not the program wrote the braces 8.5.1
		// would otherwise count the clauses of.
		const AstNode& first = *initializer->children[0];
		const AstNode* const spelled =
			first.kind == AstKind::BracedInitList && first.children.size() == 1
				? first.children[0]
				: &first;
		type = StringInitialization(*this).deduced_bound(type, *spelled,
		                                                 looked_up);
	}
	if (types_.kind(type) == TypeKind::Array && !types_.bounded(type) &&
	    initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::BracedInitList)
	{
		// 8.3.4p3 first: a bound an earlier declaration of the same object
		// wrote is the one this definition has, and the list only says how
		// many of its elements the program wrote out.
		SemaEntity* const before =
			redeclared(target, spelled.last(), SemaKind::Variable);
		if (before != nullptr && types_.kind(before->type) == TypeKind::Array &&
		    types_.bounded(before->type) &&
		    types_.target(before->type) == types_.target(type))
		{
			type = before->type;
		}
		else
		{
			const WrittenList clauses(initializer->children[0], *this,
			                          looked_up);
			if (!clauses.unsettled())
			{
				// 14.6p8: a clause standing for a run no argument list has
				// settled says nothing about how many elements there are, so
				// the bound is what the reading made from an argument list
				// settles.
				//
				// 8.5.1p11: how many of those clauses one element takes is its
				// own walk's answer wherever an element can take more than
				// one, so the bound is that walk's count rather than the
				// length of the list.
				const TypeId element = types_.target(type);
				const unsigned long long count =
					clause_capacity(element) > 1
						? deduced_array_bound(type,
						                      *initializer->children[0],
						                      looked_up)
						: clauses.size();
				type = types_.array_of(element, true, count);
			}
		}
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

	// 3.4.1p8: an initializer stands *after* the declarator-id, so a definition
	// written outside the region its declarator-id names reads its names there
	// as the rest of the declarator already does - which is what lets
	// `const long D::block_size = sizeof(value_type);` name what `D` declares.
	// The line the definition writes still stands where it was written, and
	// `inside` differs from `ctx` in nothing else.
	Context inside = ctx;
	inside.scope = looked_up.scope;
	declare_object_declarator(initializer, specifiers, inside, target, spelled,
	                          written, type, whole);
	if (checking_ > 0 && initializer != nullptr)
	{
		// 14.6p8 and 3.4p1: an initializer is an expression of this definition
		// exactly as the operand of a `return` is, so the names it writes are
		// looked up where the definition stands too.  3.3.2p1 puts its point
		// after the declarator, which is why it is read once the declarator
		// this one belongs to has declared its name.
		check_expression_names(*initializer, inside);
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
                                             TypeId type, const AstNode* whole)
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
	if (declared != nullptr && types_.kind(declared->type) == TypeKind::Array &&
	    !types_.bounded(declared->type) &&
	    types_.kind(type) == TypeKind::Array && types_.bounded(type) &&
	    types_.target(declared->type) == types_.target(type))
	{
		// 3.9p7 and 8.3.4p3: an array of unknown bound is an incomplete type
		// the definition of the object completes, so the bound that definition
		// deduced from its list is the *declaration's* from there on - which is
		// what a `sizeof` written over the name the class declared reads.
		declared->type = type;
	}
	else if (declared != nullptr && types_.kind(type) == TypeKind::Array &&
	         !types_.bounded(type) &&
	         types_.kind(declared->type) == TypeKind::Array &&
	         types_.bounded(declared->type) &&
	         types_.target(type) == types_.target(declared->type))
	{
		// The same rule read the other way round.  9.4.2p3 leaves 9.4.2p2's
		// definition of a static data member no initializer of its own, so the
		// bound is one only the brace-or-equal-initializer the *class* wrote
		// deduced - and the object that definition lays out is the array that
		// bound describes rather than one of unknown bound.  3.9p7 makes the
		// declared type complete here for the same reason the line above makes
		// it complete there: the two declarations declare one object.
		type = declared->type;
	}
	SemaEntity& entity = declared != nullptr
		? *declared
		: model_.create(SemaKind::Variable, name, type);
	// 7.1.5p9: what the fold comes to is also a requirement on the declaration
	// that wrote `constexpr`, asked here because this is where that declaration
	// is - the fold says why an initializer is no constant expression, and the
	// reading beside it says what else 7.1.5 asks of the declaration.
	const bool covered = ConstexprReading(*this).fold_declared_object(
		entity, initializer, type, ctx, specifiers.is_constexpr);
	if (specifiers.is_constexpr)
	{
		ConstexprRequirement(*this).require_object(entity, type, ctx,
		                                           initializer, covered);
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
		// 9.4.2p3: the same holding, for the other member a class may write an
		// initializer for.  9.4.2p2's definition of a static data member stands
		// outside the class and writes no initializer of its own, so the one the
		// class wrote is what initializes the object that definition lays out -
		// and it is read there, in the class, exactly as 12.6.2p8's is.
		if (entity.default_initializer ||
		    (target.scope->kind == ScopeKind::Class && specifiers.is_static &&
		     initializer != nullptr && !initializer->children.empty()))
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
	// 14.7.1p1: a definition an instantiation read is one no unit wrote for
	// these arguments, so the storage it lays out belongs to the program where
	// the program reaches it.  14.7.3p1's `template<>` is written out and is
	// this unit's own, which is what tells the two apart here.
	entity.instantiated_definition = entity.instantiated_definition ||
		(defines_object && ctx.instantiated_member);
	// 10.4p2: a class with a pure final overrider has no objects, so the
	// declarations that lay one out are refused.  9.4.2p2's declaration of a
	// static data member lays none out - the definition written outside the
	// class does, and that one is a definition like any other - while 9.2p1's
	// non-static data member is an object of every object of its class.
	if (defines_object ||
	    (target.scope->kind == ScopeKind::Class && !specifiers.is_static))
	{
		// 3.9p5 and 14.7.1p1: the declared type of an object shall be complete
		// where the object is *defined*, which is the same declarator 10.4p2 is
		// asked about - so a specialization a name left declared is asked for
		// its definition here, and 3.1p2's `extern` declaration and 9.4.2p2's
		// declaration a class writes of its static data member ask for nothing.
		require_complete_type(type);
		require_creatable_object(type, name);
		if (defines_object)
		{
			// 3.2p3 with 14.7.1p1: laying out an object is what reaches every
			// static data member the classes in it declare, whether or not a
			// name here writes one - so this is where a specialization is asked
			// for the storage its own definition would lay out.  9.2p1's
			// non-static data member lays out no object of its own: it is one
			// subobject of every object of its class, which is where the walk
			// reaches it - and asking here would both lay out storage for a
			// class no object of which is ever declared and settle the answer
			// before the definition that would fill it has been read.
			demand_object_storage(type, types_, model_);
		}
	}
	record_storage(entity, prior, specifiers, target, type, whole);
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
	describe_object_initialization(entity, line, type, initializer, specifiers,
	                               ctx, target, declared, defines_object);
}

// 8.5: what the dump says one declared object's initialization is - which of
// 8.5's initializations was written, the constructor it names, the value it
// came to, or the image it leaves the storage holding.  It is a reading of its
// own because it is where the *initializer* is answered, and the declaration
// above it is where the object is: 9.4.2p3 is what makes the two different
// declarations, and everything else here reads one.
//
// `declared` is 9.4.2p2's earlier declaration of the same object where this one
// defines it outside the region that declared it, and null otherwise.
void SemaAnalyzer::describe_object_initialization(
	SemaEntity& entity, DumpNode& line, TypeId type,
	const AstNode* initializer, const Specifiers& specifiers,
	const Context& ctx, const Context& target, const SemaEntity* declared,
	bool defines_object)
{
	const AstNode* written_clause =
		initializer == nullptr || initializer->children.empty()
			? nullptr
			: initializer->children[0];
	// 9.4.2p3: 9.4.2p2's definition of a static data member writes no
	// initializer, because its class already wrote one - so what initializes the
	// object this definition lays out is that one, read in the class it was
	// written in exactly as 12.6.2p8's brace-or-equal-initializer is.  The
	// initialization is then this definition's own like any other: it names the
	// constructors it chooses, gives the storage its image, and is not a
	// default-initialization the definition's silence asked for.
	//
	// It is asked of every declared type and not only of the ones an object is
	// *built* in.  A member of arithmetic type would reach its image anyway,
	// because 5.19p3 folded it to one value and `SemaEntity::value` carries that
	// value on to 3.6.2p2 - but a scalar whose initializer is an *address* has no
	// such value: `static constexpr const char* text = "ab";` comes to 4.10p1's
	// pointer to the literal's own object, which no `bits` of a constant holds.
	// A definition that read the silence as 8.5p6's default-initialization laid
	// out zero for it and wrote no initialization at all, which is a null pointer
	// where the program wrote the address of a string.
	Context initializing = ctx;
	if (written_clause == nullptr && declared != nullptr)
	{
		const std::unordered_map<std::uint32_t, HeldInitializer>::const_iterator
			held = member_initializers_.find(entity.id);
		if (held != member_initializers_.end())
		{
			written_clause = held->second.written;
			initializing.scope = held->second.scope;
		}
	}
	const AstNode* const clause = written_clause;
	// 12.8p31 and 5.2.3p3: `T x = T{...}` creates `x` itself, so the braced
	// list is what initializes it and the initialization is the one the same
	// list written on the declarator would be - which for an aggregate is
	// 8.5.1's and not a constructor's.  The prvalue was direct-list-initialized
	// where it stands, so 8.5.4p3 asks nothing of the list here either.
	const AstNode* const elided = clause == nullptr
		? nullptr
		: braced_prvalue_of(*clause, type, initializing);
	const AstNode* const value = elided != nullptr ? elided : clause;
	const bool copied =
		elided == nullptr && initializer != nullptr && initializer->copied;
	if (types_.is_class(types_.element_of(types_.strip_cv(type))) &&
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
		construct_object(entity, line, value, initializing, Placement::Named,
		                 copied);
		return;
	}
	if (entity.object_definition && element_constructed(type, value))
	{
		// 12.6p1: an array of class type is initialized by constructing each of
		// its elements, and where no clause named one the constructor every
		// element is given is the same one.  The action names the array, so
		// there is one of it however many elements there are.
		construct_object(entity, line, value, initializing, Placement::Named,
		                 copied);
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
		    arithmetic_type(type) != kNoType &&
		    entity.region != nullptr &&
		    entity.region->kind == ScopeKind::Class)
		{
			DumpNode& held = model_.open_node(
				line, spell("literal", ValueCategory::PRValue, type,
				            spell_value(type, entity.value, entity.real)));
			set_fact(held, FactKind::Literal, type, ValueCategory::PRValue);
			held.fact.constant = true;
			held.fact.value = entity.value;
			held.fact.real = entity.real;
			return;
		}
		// 8.5p6: an object of any other type with no initializer holds no value
		// the program may read, and there is nothing to describe.
		return;
	}
	// A constant of class type is one 5.19 reads a member out of and no line of
	// this dump spells: its bits are the identifier of the list its subobjects
	// hold, so the object it stands for is described by the initialization the
	// declaration wrote exactly as any other object of that class is.
	if (entity.constant && specifiers.is_constexpr && !lowering() &&
	    arithmetic_type(type) != kNoType)
	{
		// The dump writes what the object is worth and stops there.  A lowering
		// still has to give the storage that value: 7.1.5p9 makes a constexpr
		// object a const object and nothing more, so 3.6.2p2's initialization
		// of it is the one every other const object at this scope gets.
		model_.open_node(line, spell("literal", ValueCategory::PRValue, type,
		                             spell_value(type, entity.value,
		                                         entity.real)));
		return;
	}
	// 3.6.2p2 and 3.7.1p1: an object at namespace scope, and the static data
	// member 9.4.2p2 defines there, is given its value by the program image
	// rather than built where its declaration stands.
	// 3.7.1p3 makes an object a block declares `static` one of those too: its
	// storage is the program's, so 3.6.2p1's constant initialization of it is
	// the image and not something the block builds.
	write_initializer(*value, type, initializing, line,
	                  entity.object_definition &&
	                  (target.scope->kind == ScopeKind::Namespace ||
	                   target.scope->kind == ScopeKind::Class ||
	                   entity.local_static));
	// 12.2p5: where that initializer bound this reference to a temporary, the
	// temporary's lifetime is the reference's from here on.
	extend_bound_temporary(type, initializing, line);
}


void SemaAnalyzer::write_initializer(const AstNode& initializer, TypeId type,
                                     const Context& ctx, DumpNode& line,
                                     bool image)
{
	if (initializer.kind == AstKind::ParenInitializer)
	{
		// 8.5p16: direct-initialization from one expression, which differs from
		// copy-initialization in one thing only - 12.3.2p2 lets it choose a
		// conversion function declared `explicit`.  Which expression that is is
		// 14.5.3p4's question: a clause written `pattern...` is one expression
		// per element of the run its packs are bound to.
		Clauses written(&initializer, *this, ctx);
		if (written.list.size() > 1)
		{
			throw std::runtime_error("an object of non-class type is "
			                         "initialized from parentheses holding "
			                         "more than one expression");
		}
		if (!written.spent())
		{
			initialize(written.next(), type, written.in(ctx), line, false,
			           Requested::Written, true);
		}
		return;
	}
	if (initializer.kind == AstKind::BracedInitList &&
	    initializer.children.size() == 1 &&
	    StringInitialization(*this).as_object(type, *initializer.children[0],
	                                          ctx, line))
	{
		// 8.5.2p1: the braces hold a string literal and the object is an array
		// of character type, so the elements are the literal's own code units -
		// which is the same initialization the array gets where the braces were
		// left out, written under the same line.
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
	if (StringInitialization(*this).as_object(type, initializer, ctx, line))
	{
		// 8.5p14 and 8.5.2p1: an array of character type written with a string
		// literal takes the literal's code units as its elements, which is no
		// conversion of the literal to the array's type and so is asked before
		// the initialization every other expression gets.
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
