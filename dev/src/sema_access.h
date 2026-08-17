#pragma once

#include <string>
#include <vector>

class SemaAnalyzer;
class Scope;
struct AstNode;
struct SemaContext;
struct SemaEntity;
struct DeclSpecifiers;
class QualifiedName;

// 11: which contexts may name what a class declared, and 11.3's two ways a
// class gives that reach away.
//
// The two halves are one reader because they are one question asked from
// either end.  11.2p4 answers "may this name be written here" by walking from
// the class the name was written on down to the class that declared the member,
// asking of each base-specifier on the way what 11.2p1 asks of a member; 11.3
// records what a class granted, and every step of that walk asks the record.
// So the walk, the grant and the refusal belong together, and apart from the
// class reading that completes a class: nothing here reads syntax, and the two
// facts it stands on - a class's base-specifiers and its friendships - are
// settled before any name is looked up in it.
class Access
{
public:
	explicit Access(SemaAnalyzer& analyzer);

	// 11.2: whether a context in `from` may name `member`, and the error that
	// it may not.
	// `naming_class` is 11.2p5's class the name was written on, whose own bases
	// may grant what the declaring class does not; null where the name was not
	// written on an object.
	bool accessible(const SemaEntity& member, const Scope* from,
	                const Scope* naming_class = nullptr) const;
	void require_access(const SemaEntity& member, const Scope* from,
	                    const Scope* naming_class = nullptr);
	// 11.2 and 14.2: the access asked about a component written as a
	// template-id, which the *template* the lookup finds carries - the
	// specialization its arguments make is no declaration the program wrote an
	// access-specifier over.  A component that names no template is left to the
	// reading that follows it to refuse.
	void require_component_access(const std::string& component,
	                              const SemaContext& ctx, Scope& in);
	// 11.4p1: the additional check a protected non-static member named on an
	// object asks, which is that the object is of the class the access occurs in
	// rather than of the base that declared the member.
	void require_protected_object(const std::vector<SemaEntity*>& found,
	                              const SemaEntity& member, const Scope* from,
	                              const Scope* object_class);
	// 12.4p11: the destructor an object's lifetime ends in a call of, which is
	// named where the object is declared and has to be accessible there.
	void require_destruction_access(const SemaEntity& entity, const Scope* from);

	// 11.3p1: the innermost class around a friend declaration, which is the one
	// that grants what the declaration grants.
	SemaEntity* granting_class(const SemaContext& ctx) const;
	// 11.3p2: `friend C;` and `friend class C;` grant this class's access to the
	// class the specifiers named, which declares nothing.
	void grant_class_friendship(const SemaContext& ctx,
	                            const DeclSpecifiers& specifiers);
	// 14.5.4p1: the grant a friend declaration whose declarator-id is a
	// template-id makes, which declares nothing and names the function template
	// the region `target` already holds.
	void grant_template_friendship(const std::string& component,
	                               const SemaContext& target,
	                               SemaEntity& granting);
	// 11.3p6: a friend declaration declares its function in the innermost
	// enclosing namespace, so a declarator written after `friend` is read
	// against that region rather than against the class it stands in.  Returns
	// the class the declaration grants access to, and leaves `target` naming the
	// region the function is declared in.
	SemaEntity* friend_target(const SemaContext& ctx,
	                          const QualifiedName& spelled, SemaContext& target,
	                          SemaEntity* granting);
	// 11.3p1: whether a name read in `from` reaches what `granting` declared
	// because `granting` befriended what owns `from`.
	bool befriended(const Scope& granting, const Scope& from) const;
	// 11.2p5: whether any class between the one a name was written on and the
	// one that declared the member grants `from` the reach of its own members.
	bool befriends_between(const Scope& at, const Scope& declaring,
	                       const Scope& from) const;
	// 11.2p1: whether a base-specifier `derived` wrote with this access may be
	// reached from `from` - a member or friend of `derived` reaches any of them,
	// and a class derived from it reaches a protected one.
	bool base_accessible(const Scope& derived, unsigned char access,
	                     const Scope* from) const;

private:
	// 11.2p4's last bullet: a member the naming class did not declare is reached
	// through a base of it, and is accessible only where that base is - so the
	// one path between the two classes is asked link by link.
	bool base_path_accessible(const Scope& naming, const Scope& declaring,
	                          const Scope* from) const;
	// The one walk down to `declaring`, clearing `reaches` at the first link on
	// it that denies the access.  True where the class was reached at all.
	bool base_path(const Scope& naming, const Scope& declaring,
	               const Scope* from, bool& reaches) const;

	Access(const Access&);
	Access& operator=(const Access&);

	SemaAnalyzer& analyzer_;
};
