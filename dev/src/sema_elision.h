#pragma once

#include <vector>

#include "sema_declaration.h"
#include "sema_value.h"
#include "type_model.h"

class SemaAnalyzer;
struct AstNode;
struct DumpNode;
struct SemaContext;
struct SemaEntity;

// 12.8p31 and 8.5p14: the copy an initialization of an object of class type
// from a prvalue of that class does not make, and what the initializer was
// written as.
//
// The two halves are one reader because the elision is decided from the form
// the program wrote.  8.5p15/p16 says which of 8.5's forms an initializer is -
// a list whose clauses are the constructor's arguments, one expression a
// converting constructor answers, or the `T(a, b)` and `T{...}` whose arguments
// are the object's own; 12.8p31 then asks of that form whether the prvalue's
// object and the destination are one, and 12.8p32 asks for the constructor the
// copy would have called all the same.  So the reading, the elision and the
// constructor it still names belong together, and apart from the lifetime
// layer that writes the actions: nothing here writes a line, and what it leaves
// on the node it elided is what the lowering reads to tell 5.2.2p1's call from
// 13.3.1.2p2's rewrite of an operator into one.
class Elision
{
public:
	explicit Elision(SemaAnalyzer& analyzer);

	// 8.5p15/p16 and 5.2.3p1: which of 8.5's forms the initializer an object of
	// class type was written with is.
	WrittenInitializer read_initializer(const AstNode* written,
	                                    TypeId object_type,
	                                    const SemaContext& ctx,
	                                    bool value_init);
	// 12.8p32: the constructor a copy 12.8p31 elided would have called, asked
	// for where the program wrote the initialization it elided from.
	void require_transfer(TypeId type, const SemaContext& ctx);
	// 12.8p31 and 12.2p3: the prvalue's object and the object being initialized
	// are one, so the full-expression that was holding the end of the first
	// holds it no longer.  Reached through 5.2.9p4's cast, because the object
	// stands under it and not on it.
	void release_created_object(DumpNode& node);
	// 5.2.9p4: the node the object a prvalue creates stands on, which is the
	// one under whatever cast to the same class was written over it.
	DumpNode& created_object_node(DumpNode& node);
	// 12.8p31: whether the transfer 13.3 chose for this initialization is one
	// whose argument creates the very object being initialized, which makes the
	// two objects one and leaves the transfer unwritten.  The initializer the
	// line then holds is the one that creates it.  `into_temporary` is what the
	// elision asks about the *destination*: a temporary the analysis made for a
	// prvalue is storage the enclosing initialization has not settled yet, so
	// the transfer into one is a call the program can watch run.
	// `written_call` is 5.2.2p1 asked of the initializer this elision reached:
	// what the elided node carries away is the form the program wrote it in.
	bool into_destination(const SemaEntity& constructor,
	                      std::vector<AnalyzedValue>& arguments,
	                      TypeId object_type, DumpNode& line, DumpNode& action,
	                      bool into_temporary, bool written_call);
	// 8.5p14 and 12.8p31: the whole of the copy-initialization of an object of
	// class type from one written expression, where that expression is worth a
	// prvalue of the object's own class and no constructor stands between them.
	// The initializer is read into `line`, and the answer says whether that
	// reading *is* the initialization - so the caller writes no action - or
	// whether the line has been given back and 13.3 still has to choose.
	// `written_call` is 5.2.2p1 asked of the same initializer.
	bool of_written_prvalue(TypeId object_type, const AstNode& written,
	                        const SemaContext& ctx, DumpNode& line,
	                        bool member, bool written_call,
	                        AnalyzedValue& source);

private:
	SemaAnalyzer& analyzer_;
};
