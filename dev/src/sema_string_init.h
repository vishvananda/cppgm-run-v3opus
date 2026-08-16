#pragma once

#include <vector>

#include "type_model.h"

class SemaAnalyzer;
struct AstNode;
struct DumpNode;
struct InitializerClauses;
struct SemaContext;

// 8.5.2: the array of character type a string literal initializes.
//
// It is an initialization of its own rather than a conversion: no expression of
// array type converts to another array type, so the literal never reaches
// 8.5p16's reading at all, and what the elements hold is the code units the
// literal was made of rather than anything a clause said.  Every form the
// program can write it in - the braces 8.5.1 would otherwise count the clauses
// of, written or left out; the array declared with no bound, whose length
// 8.3.4p3 takes from the literal; the array standing as a subobject of an
// enclosing aggregate - is that one reading, so they are kept together and each
// caller asks it once.
//
// The code units are read out of the literal phase 6 already built, so an array
// of n elements is one scan of n units and nothing is re-lexed.
class StringInitialization
{
public:
	explicit StringInitialization(SemaAnalyzer& analyzer);

	// 8.3.4p3 and 8.5.2p1: the bound an array of unknown bound takes from the
	// literal, which is the code units it holds - the terminating one among
	// them.  The array is returned as it stands where the initializer is no
	// string literal of this array's own width.
	TypeId deduced_bound(TypeId array, const AstNode& written,
	                     const SemaContext& ctx);

	// 8.5.2p1 where the array is an object of its own: the elements stand under
	// the one list every other array initialization writes, which is what the
	// lowering reads the storage of a declared array out of.  False where the
	// initializer is no string literal for this array, which is read as the
	// expression it is.
	bool as_object(TypeId array, const AstNode& written, const SemaContext& ctx,
	               DumpNode& line);

	// 8.5.2p1 where the array is a subobject of an enclosing one: each element
	// is a subobject of its own, and the ones past the literal hold what any
	// other unreached element holds.  The clause is taken where it answers.
	bool as_subobject(TypeId array, InitializerClauses& clauses,
	                  const SemaContext& ctx, DumpNode& parent);

private:
	StringInitialization(const StringInitialization&);
	StringInitialization& operator=(const StringInitialization&);

	// 8.5.2p1: the code units the literal initializes the elements with, and
	// the one element each of them is written as.
	bool units_of(TypeId array, const AstNode& written, const SemaContext& ctx,
	              std::vector<unsigned long long>& units);
	void write_unit(TypeId element, unsigned long long bits, DumpNode& parent);

	SemaAnalyzer& analyzer_;
};
