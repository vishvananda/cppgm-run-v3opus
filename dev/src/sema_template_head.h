#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sema_declaration.h"
#include "sema_scope.h"
#include "sema_template.h"
#include "sema_value.h"
#include "type_model.h"

struct AstNode;
class Scope;
class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;
struct DumpScope;

// 14.1p2's template-parameter-clause and 14.3p1's template-argument-list, as
// one reader.
//
// A template head declares *places* rather than declarations: what each place
// is - a type, a value of a written type, or a template - is settled once, in
// 14.6.1p1's own region, and every argument list read afterwards substitutes
// its own bindings into what was settled there rather than reading the head's
// syntax again.  This reader owns both halves of that: the head, and the
// reading of one written argument list against it.
//
// 14.2 writes an argument list inside a name, so an argument arrives as text.
// A type argument is turned back into what was written by `sema_type_id.cpp`
// and a value argument by `sema_value_expression.cpp`; what belongs here is
// which of the three a place asked for, and what the answer is bound as.
//
// The lookups, the type table and the expression layer are the analyzer's, so
// the reader borrows it rather than holding state of its own.
class TemplateHead
{
public:
	explicit TemplateHead(SemaAnalyzer& analyzer)
		: analyzer_(analyzer)
	{}

	// 14.1p2: the parameters `clause` declared, written onto `info`.
	//
	// 14.1p11 leaves a pack the last place a *primary* template's head
	// declares, because the arguments a list writes past the places before it
	// are all the pack's.  14.5.5p1's head declares places nothing writes an
	// argument for - every one of them is deduced from the pattern - so a pack
	// may stand anywhere in it, and `primary` is which of the two this is.
	void read(const AstNode& clause, TemplateInfo& info, bool primary = true);
	// 14.1p2: the head a template-template place wrote, read once per clause.
	TemplateInfo& parameter_head(const AstNode* clause);
	// 14.1p2: that head settled and recorded against the type `place` stands
	// for, which is what 14.3.3p1 is asked of wherever an argument arrives at
	// the place - however the head that declared it was read.  `enclosing` is
	// the region the place itself was declared in, which the head's own places
	// are settled inside.
	void record_place(TypeId place, TemplateInfo& head, Scope& enclosing);
	// 14.1p9 at a template place: the default argument names a template, which
	// 8.1p1's reading of a type-id has nothing to make of.
	TypeId place_default(const AstNode& written, const TemplateInfo* head,
	                     const SemaContext& ctx);
	// 14.3.3p1: whether a template whose head is `argument` is at least as
	// specialized as the place `place` declared, which is what says a written
	// template-name may stand at that place.
	bool argument_matches(const TemplateInfo& place,
	                      const TemplateInfo& argument);
	// 14.5.6.1p5 with 14.1p2: whether two heads declare equivalent places, which
	// is what every declaration of one template has to write.  It is 14.3.3p1's
	// question with its direction taken away, and it opens 14.6.1p1's region of
	// either head that declares a value place, because what such a place names
	// a value of is settled nowhere else.
	bool heads_equivalent(TemplateInfo& left, TemplateInfo& right);
	// Whether any place of `head`, or of a head one of its template places
	// wrote, names a value.
	static bool has_value_place(const TemplateInfo& head);
	// 14.3.3p1: the entry standing for the template `named`, which is what a
	// template-template place binds, and the template such an entry names.
	TypeId name_argument(SemaEntity& named);
	SemaEntity* named_template(TypeId argument) const;

	// 14.1p4: the name a non-type parameter's declarator gave it, and the type
	// it names a value of - read in a region that already binds the places
	// before it, because `template<class T, T v>` names one.
	static std::string non_type_name(const AstNode& parameter);
	TypeId non_type_type(const AstNode& parameter, const SemaContext& ctx);

	// 14.1p4 with 14.1p8: the type such a place declares once the adjustment
	// is made - an array of T and a function returning T are each written as
	// the pointer 8.3.5p5 would have made of a parameter of that type, because
	// the argument at either is an address and never a copy.  `kNoType` where
	// the type is one 14.1p4 does not list, or one this milestone leaves out.
	TypeId non_type_place(TypeId written) const;
	// 14.1p4's second and third bullets: whether a place of this type takes
	// 5.19p2's address constant rather than a number.  The two are read apart
	// everywhere the argument is - bound, spelled, mangled, and read back where
	// the name stands - because what such an argument holds is *which object*.
	bool address_place(TypeId place) const;
	// 14.3.2p5 at one of those places: the argument entry, whose bits are the
	// identifier `AddressTable` interned the object under.  4.2p1's decay,
	// 4.3p1's function-to-pointer conversion and 8.3.2p1's binding of the
	// reference are each the one the place asks for.
	TypeId address_argument(const SemaConstant& given, TypeId place);
	// 14.3.2p1 read back where the name of such a place stands: the expression
	// that names the object the argument designates, which is that object's own
	// name for a reference place and 5.3.1p3's `&` on it for a pointer one.  A
	// place binds no storage of its own, so this is what a use of it lowers
	// through - `*P = true` writes the object the argument named.
	AnalyzedValue address_value(SemaEntity& bound, DumpNode& parent);

	// 14.6.1p1: the region binding each place of `info` to something standing
	// for itself, opened once by the first reading that needs it.
	void open_region(TemplateInfo& info);
	// 14.1p2: the definition's own names for the places an earlier declaration
	// already named, taken after a region has been opened over them.
	void rename_parameters(TemplateInfo& info,
	                       const std::vector<TemplateInfo::Parameter>& head);

	// 14.3p1: one place of a template bound to the argument its list gave it -
	// a typedef-name of a type, a declaration carrying a constant, or a second
	// name for the template the argument named.
	SemaEntity& bind(Scope& region, const std::string& name, TypeId argument,
	                 SemaKind kind);
	// 14.3p1: a region binding each of `info`'s parameters to the argument
	// beside it, which is what a pattern is read against.
	Scope& open_bindings(const TemplateInfo& info,
	                     const std::vector<TypeId>& arguments);
	// 14.5.1.3p1 and 14.1p2: a region of one out-of-class member definition's
	// own, binding the names *its* head wrote to the arguments their places
	// took, opened inside `enclosing` - the region the class was completed
	// against.  Null where the head declares a different number of places than
	// the specialization takes arguments.  `carried` is the region the heads
	// standing outside this one bound, whose names stand here beside this
	// head's own: a member of a member template writes one head per class it is
	// nested in, and the region this one is opened inside is the *inner*
	// class's, which binds none of what the heads above wrote.
	Scope* open_member_parameters(Scope& enclosing, const AstNode& clause,
	                              const std::vector<TypeId>& arguments,
	                              SemaKind kind, DumpScope* dump,
	                              Scope* carried = nullptr);

	// 14.1p4: the type the place at `index` declares, with the arguments the
	// places before it took substituted into it.  The second overload is the
	// same question at the function tier, where each place is a declaration of
	// its own rather than an entry of a `TemplateInfo`.
	TypeId place_type(const TemplateInfo& info, std::size_t index,
	                  const std::vector<TypeId>& before);
	TypeId place_type(const std::vector<SemaEntity*>& places, std::size_t index,
	                  const std::vector<TypeId>& before);
	// 14.3p1: the argument the list wrote at `index`, read as the place says.
	TypeId bound_argument(const TemplateInfo& info, std::size_t index,
	                      const std::string& written,
	                      const std::vector<TypeId>& before,
	                      const SemaContext& ctx);
	TypeId explicit_argument(const std::vector<SemaEntity*>& places,
	                         std::size_t index,
	                         const std::vector<TypeId>& before,
	                         const std::string& written, const SemaContext& ctx);
	// 14.3p1: the argument list `written` gives `primary`, with 14.1p9's
	// defaults filled in for the places the list stopped short of.
	void bind_arguments(SemaEntity& primary,
	                    const std::vector<std::string>& written,
	                    const SemaContext& ctx, std::vector<TypeId>& out);

private:
	// 14.3.3p1: whether one place of `place` accepts one place of `argument` -
	// the kind, a value place's own signature, and a template place's head one
	// level down.  A pack asks it of each place the other head has left.
	bool places_match(const TemplateInfo& place, std::size_t at,
	                  const TemplateInfo& argument, std::size_t index);
	// 14.3.3p1 with 14.5.6.1p5: the type the value place at `index` names a
	// value of, with each place of `head` standing for its own position.
	TypeId place_signature(const TemplateInfo& head, std::size_t index);
	// 14.3.3p1: the template a written template-template argument names, and
	// the entry standing for it.  A place forwarded as an argument of another
	// head stands for itself until an argument list settles it.
	TypeId template_argument(const std::string& written,
	                         const TemplateInfo* place, const SemaContext& ctx);

	TemplateHead(const TemplateHead&);
	TemplateHead& operator=(const TemplateHead&);

	SemaAnalyzer& analyzer_;
};
