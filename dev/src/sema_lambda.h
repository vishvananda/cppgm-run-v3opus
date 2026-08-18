#pragma once

#include "ast_model.h"
#include "type_model.h"

class SemaAnalyzer;
struct AnalyzedValue;
struct DumpNode;
struct SemaContext;
struct SemaEntity;

// 5.1.2: the class a lambda-expression declares, and the object it stands for.
//
// p3 says the expression's type is "a unique, unnamed non-union class type"
// declared in the smallest block, class or namespace scope around it, with a
// public inline function call operator whose parameters, cv-qualification and
// exception-specification the lambda-declarator wrote and whose body is the
// compound-statement.  Every one of those is a thing the class reading already
// builds out of a class-specifier: the local class, its ABI name, its layout,
// the member function's own declaration, the reading of that member's body at
// the closing brace, and the demand-driven definition a use of it writes.  So
// the closure type is *made* as a class-specifier - the syntax the analysis
// builds rather than reads - and handed to that reading, which leaves 5.1.2
// with the two facts no tree the parse built can say: which class one
// lambda-expression makes, and what the expression is worth.
//
// p4's return type is the one part with no syntax to borrow: where the
// declarator wrote no trailing-return-type the type is the returned
// expression's after 4.1, 4.2 and 4.3, which is a question only the expression
// layer answers.  `AstKind::DeducedReturnType` is the type-specifier that asks
// it, and the declarator reading is where it comes back.
class LambdaReading
{
public:
	explicit LambdaReading(SemaAnalyzer& analyzer);

	// 5.1.2p2: the prvalue a lambda-expression is - a temporary object of the
	// closure class p3 declares, which p14 initializes from the captures and a
	// captureless lambda therefore initializes from nothing at all.
	AnalyzedValue expression(const AstNode& node, const SemaContext& ctx,
	                         DumpNode& parent);
	// 5.1.2p4: the type a `return` statement's expression gives the call
	// operator, read where 8.3.5p2's trailing-return-type is read - after the
	// parameters, whose names it may write.
	TypeId deduced_return_type(const AstNode& node, const SemaContext& ctx);

private:
	SemaEntity& closure_class(const AstNode& node, const SemaContext& ctx);
	AstNode& call_operator(const AstNode& node);
	void write_return_type(AstNode& definition, AstNode& declarator,
	                       const AstNode& body, const AstNode* written);
	AstNode& make(AstKind kind);

	SemaAnalyzer& analyzer_;
};
