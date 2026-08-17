#pragma once

#include <cstdint>
#include <string>

#include "type_model.h"

class Scope;
class SemaAnalyzer;
struct SemaEntity;

// 14.5.6.1p5: when two template-declarations declare the same template.
//
// The clause is one question asked at four tiers - a redeclaration of a
// function template, a definition written out for one, 7.3.3p14's hiding of a
// base's member template, and 13.1's own index of the declarations a region
// holds of one name - and each of them has two heads whose places are
// declarations of their own.  So the answer cannot be read off the types:
// `const K&` written under one head and `const K&` written under another are
// two types, and a place *neither* declarator mentioned stands in no type at
// all.  What settles all four is one canonical form per declaration, which is
// the whole of this reader.
class TemplateSignature
{
public:
	explicit TemplateSignature(SemaAnalyzer& analyzer);

	// `type` with each parameter `parameters` declared standing for the place
	// it was declared in rather than for the declaration that made it.  Two
	// declarations declare the same template exactly when this is the same
	// type, so it is a fact of one declaration and not of a pair - which is
	// what keeps declaring the nth overload of a template name from reading the
	// n - 1 before it.  `kNoType` where a head declares a parameter that binds
	// a template, which no substitution here stands for.
	TypeId of(const Scope& parameters, TypeId type);

	// The type 13.1's index keys a declaration written under `head` by, which
	// is the form above wherever there is one and the written type otherwise.
	TypeId indexed(const Scope* head, TypeId type);

	// The declaration in the chain `head` that declares the same function
	// template as a head declaring `parameters` and a declarator that made
	// `type`, or null where none does.
	SemaEntity* equivalent(SemaEntity& head, Scope& parameters, TypeId type);

	// 7.3.3p14's key over a member function, read over the form above wherever
	// the declaration was written under a head - so a base's member template
	// and this class's own are compared by the list they wrote rather than by
	// the places each head declared it in.
	std::uint32_t hiding_key(const SemaEntity& function);

private:
	TemplateSignature(const TemplateSignature&);
	TemplateSignature& operator=(const TemplateSignature&);

	SemaAnalyzer& analyzer_;
};
