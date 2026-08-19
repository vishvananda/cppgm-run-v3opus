#pragma once

#include <string>
#include <vector>

#include "sema_declaration.h"
#include "sema_value.h"

class SemaAnalyzer;
class Scope;
struct AstNode;
struct DumpNode;
struct SemaEntity;

// 1.4p8, 3.7.4 and 20.8.2: the functions the *implementation* provides, which
// no translation unit declares and every one may name.
//
// They come in three kinds, and the three are one subject because each answers
// the same question - what a name the program never declared denotes - at a
// different tier.  3.7.4's allocation functions are bound before the unit is
// read, so a definition the program writes redeclares one of them.  1.4p8's
// reserved functions are declared by the first use that names one, each with an
// ordinary function type 13.3 and the lowering read like any other.  And the
// third kind has no type at all: `__builtin_constant_p` answers a question
// about its operand rather than converting it, and 20.8.2p1's `INVOKE` is
// whichever *call* its first operand and the rest make - so what such a name
// denotes is settled only once the arguments standing under it are known.
//
// Keeping the three here is what makes them one reading rather than three
// branches spread across the expression layer, exactly as `sema_operator.h`
// keeps 13.3.1.2p3's candidate set and `sema_argument_lookup.h` keeps 3.4.2's
// walk.
class BuiltinReading
{
public:
	explicit BuiltinReading(SemaAnalyzer& analyzer);

	// 1.4p8 and 17.6.4.3.2p1: the declaration of a reserved function the
	// implementation provides, made where a call names one no declaration of
	// the program reached.  It is declared in the global namespace, so the
	// first call to name it declares it and every later one finds it by
	// ordinary lookup; a name the implementation reserves nothing for leaves it
	// null.  A name whose meaning is no function type at all - `call` below -
	// is not one of these and is left null here too.
	SemaEntity* reserved(const std::string& written,
	                     std::vector<SemaEntity*>* found);

	// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation functions
	// every translation unit declares in the global namespace whether or not it
	// wrote them, bound there before the unit is read so a definition the
	// program writes is a definition of one of them.
	void declare_allocation_functions(Scope& where);

	// Whether a call of this name is one the translation answers itself, which
	// is what a reading looking a callee up *where the template stands* has to
	// ask beside `reserved`: no region declares either kind, so an unqualified
	// callee naming one is not a name 14.6p9 leaves unfound.
	static bool answers(const std::string& written);

	// The calls the translation answers itself, `list` being the argument-list
	// the call wrote.  False where the name is no such call, which leaves the
	// ordinary lookups and `reserved` to say what it denotes.
	bool call(const std::string& written, const AstNode* list,
	          const SemaContext& ctx, DumpNode& parent, AnalyzedValue& out);

private:
	// 20.8.2p1 [func.require]: `INVOKE(f, t1, ..., tN)`, which is the call
	// `f(t1, ..., tN)` for every `f` that is not a pointer to member.  The
	// operands are the arguments the call wrote *expanded* - 14.5.3p4's run is
	// what says which of them is `f` - and what the reading then makes of them
	// is an ordinary call of a value, so a callable of class type reaches
	// 13.5.4p1's `operator()` and a pointer to function reaches 5.2.2p1.
	AnalyzedValue invoke(const AstNode* list, const SemaContext& ctx,
	                     DumpNode& parent);

	SemaAnalyzer& analyzer_;
};
