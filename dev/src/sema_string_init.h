#pragma once

#include <string>
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

	// The same reading where the list the elements stand under has already been
	// written - 8.5.4p1's braces around the literal, whose own node is that
	// list.  A place that opens the list itself asks this one, so an array
	// written `{"ab"}` stands under exactly the node one written `{'a','b'}`
	// does and no reader has two shapes to know.
	bool units_into(TypeId array, const AstNode& written,
	                const SemaContext& ctx, DumpNode& list);

	// 8.5.2p1 where the array is a subobject of an enclosing one: each element
	// is a subobject of its own, and the ones past the literal hold what any
	// other unreached element holds.  The clause is taken where it answers.
	bool as_subobject(TypeId array, InitializerClauses& clauses,
	                  const SemaContext& ctx, DumpNode& parent);

	// 8.5.2p1: the code units the literal initializes the elements with, which
	// is the whole of the reading where nothing is being written out - a
	// constant expression holds the elements as values and lays no line down,
	// so it asks this one question and interns the list itself.
	// `literal`, where it is given, takes the code units of the literal itself
	// - 2.14.5p8's object, which exists whether or not the copy 8.5.2p1 makes
	// of it leaves anything naming it.
	bool units_of(TypeId array, const AstNode& written, const SemaContext& ctx,
	              std::vector<unsigned long long>& units,
	              std::string* object = nullptr);

private:
	StringInitialization(const StringInitialization&);
	StringInitialization& operator=(const StringInitialization&);

	// 8.5.2p1: the one element each code unit is written as, and the run of
	// them one list holds.
	void write_unit(TypeId element, unsigned long long bits, DumpNode& parent);
	void write_units(TypeId array, const std::vector<unsigned long long>& units,
	                 DumpNode& list);

	SemaAnalyzer& analyzer_;
};
