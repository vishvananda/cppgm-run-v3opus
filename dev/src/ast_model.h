#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <vector>

// The syntax tree PA10 builds and later frontend assignments read.
//
// One node kind per construct the dump names, so a later pass asks the tree
// what a construct is rather than re-reading source text.  A node carries at
// most two payloads: the terminal it was built from, when the dump names one,
// and a spelled-out name, for the constructs whose identity is a name that the
// grammar leaves unresolved until a semantic assignment looks it up.
#define CPPGM_AST_KINDS(X) \
	X(TranslationUnit, "translation-unit") \
	X(EmptyDeclaration, "empty-declaration") \
	X(NamespaceDefinition, "namespace-definition") \
	X(NamespaceAliasDefinition, "namespace-alias-definition") \
	X(UsingDirective, "using-directive") \
	X(UsingDeclaration, "using-declaration") \
	X(AliasDeclaration, "alias-declaration") \
	X(Target, "target") \
	X(Inline, "inline") \
	X(LinkageSpecification, "linkage-specification") \
	X(ExplicitInstantiationDeclaration, "explicit-instantiation-declaration") \
	X(TemplateDeclaration, "template-declaration") \
	X(TemplateParameterClause, "template-parameter-clause") \
	X(TemplateParameterList, "template-parameter-list") \
	X(TypeParameter, "type-parameter") \
	X(TemplateTemplateParameter, "template-template-parameter") \
	X(ParameterKey, "parameter-key") \
	X(NonTypeTemplateParameter, "non-type-template-parameter") \
	X(DefaultTemplateArgument, "default-template-argument") \
	X(ParameterPack, "parameter-pack") \
	X(ClassSpecifier, "class-specifier") \
	X(ClassForwardDeclaration, "class-forward-declaration") \
	X(ClassKey, "class-key") \
	X(BaseClause, "base-clause") \
	X(BaseSpecifier, "base-specifier") \
	X(BaseName, "base-name") \
	X(Virtual, "virtual") \
	X(AccessSpecifier, "access-specifier") \
	X(MemberSpecifiers, "member-specifiers") \
	X(Specifier, "specifier") \
	X(BitFieldDeclaration, "bit-field-declaration") \
	X(BitFieldDeclarator, "bit-field-declarator") \
	X(SpecialMemberDeclaration, "special-member-declaration") \
	X(SpecialMemberDefinition, "special-member-definition") \
	X(CtorInitializer, "ctor-initializer") \
	X(MemInitializer, "mem-initializer") \
	X(MemInitializerId, "mem-initializer-id") \
	X(ParenArgumentList, "paren-argument-list") \
	X(EnumSpecifier, "enum-specifier") \
	X(EnumKey, "enum-key") \
	X(Enumerator, "enumerator") \
	X(StaticAssertDeclaration, "static-assert-declaration") \
	X(Message, "message") \
	X(FunctionDefinition, "function-definition") \
	X(SimpleDeclaration, "simple-declaration") \
	X(DeclSpecifierSeq, "decl-specifier-seq") \
	X(DeclSpecifier, "decl-specifier") \
	X(TypeSpecifierSeq, "type-specifier-seq") \
	X(TypeSpecifier, "type-specifier") \
	X(TypeName, "type-name") \
	X(CvQualifier, "cv-qualifier") \
	X(DecltypeSpecifier, "decltype-specifier") \
	X(TypeId, "type-id") \
	X(InitDeclaratorList, "init-declarator-list") \
	X(InitDeclarator, "init-declarator") \
	X(Initializer, "initializer") \
	X(ParenInitializer, "paren-initializer") \
	X(SpecialInitializer, "special-initializer") \
	X(BracedInitList, "braced-init-list") \
	X(Declarator, "declarator") \
	X(AbstractDeclarator, "abstract-declarator") \
	X(NestedDeclarator, "nested-declarator") \
	X(Identifier, "identifier") \
	X(PtrOperator, "ptr-operator") \
	X(ArraySuffix, "array-suffix") \
	X(ParameterClause, "parameter-clause") \
	X(ParameterDeclaration, "parameter-declaration") \
	X(DefaultArgument, "default-argument") \
	X(FunctionQualifier, "function-qualifier") \
	X(NoexceptSpecification, "noexcept-specification") \
	X(VirtSpecifier, "virt-specifier") \
	X(TrailingReturnType, "trailing-return-type") \
	X(CompoundStatement, "compound-statement") \
	X(ExpressionStatement, "expression-statement") \
	X(ReturnStatement, "return-statement") \
	X(IfStatement, "if-statement") \
	X(Then, "then") \
	X(Else, "else") \
	X(SwitchStatement, "switch-statement") \
	X(WhileStatement, "while-statement") \
	X(DoStatement, "do-statement") \
	X(ForStatement, "for-statement") \
	X(ForInitStatement, "for-init-statement") \
	X(Iteration, "iteration") \
	X(Condition, "condition") \
	X(ConditionDeclaration, "condition-declaration") \
	X(CaseStatement, "case-statement") \
	X(DefaultStatement, "default-statement") \
	X(LabeledStatement, "labeled-statement") \
	X(GotoStatement, "goto-statement") \
	X(BreakStatement, "break-statement") \
	X(ContinueStatement, "continue-statement") \
	X(ThrowStatement, "throw-statement") \
	X(TryBlock, "try-block") \
	X(Handler, "handler") \
	X(ExceptionDeclaration, "exception-declaration") \
	X(Ellipsis, "ellipsis") \
	X(IdExpression, "id-expression") \
	X(Literal, "literal") \
	X(KeywordLiteral, "keyword-literal") \
	X(ParenthesizedExpression, "parenthesized-expression") \
	X(CallExpression, "call-expression") \
	X(ArgumentList, "argument-list") \
	X(SubscriptExpression, "subscript-expression") \
	X(MemberExpression, "member-expression") \
	X(PostfixExpression, "postfix-expression") \
	X(UnaryExpression, "unary-expression") \
	X(BinaryExpression, "binary-expression") \
	X(ConditionalExpression, "conditional-expression") \
	X(AssignmentExpression, "assignment-expression") \
	X(CastExpression, "cast-expression") \
	X(SizeofExpression, "sizeof-expression") \
	X(TypeTraitExpression, "type-trait-expression") \
	X(NewExpression, "new-expression") \
	X(GlobalScope, "global-scope") \
	X(Placement, "placement") \
	X(DeleteExpression, "delete-expression") \
	X(ArrayDelete, "array-delete") \
	X(LambdaExpression, "lambda-expression") \
	X(LambdaIntroducer, "lambda-introducer") \
	X(LambdaDeclarator, "lambda-declarator") \
	X(LambdaSpecifier, "lambda-specifier") \
	X(PackExpansionExpression, "pack-expansion-expression")

enum class AstKind : std::uint8_t
{
#define CPPGM_AST_KIND_ENUMERATOR(name, text) name,
	CPPGM_AST_KINDS(CPPGM_AST_KIND_ENUMERATOR)
#undef CPPGM_AST_KIND_ENUMERATOR
	kAstKindCount
};

const char* ast_kind_name(AstKind kind);

// The node payload for a construct the dump does not spell a terminal for.
const std::uint16_t kNoAstToken = 0xFFFFu;

// One syntax node.
//
// Children are pointers into the arena that built them rather than values, so
// a rule that fails after building part of a tree drops its work by returning
// and a rule that succeeds hands its nodes to its caller without copying them.
struct AstNode
{
	AstKind kind;
	std::uint16_t token;
	std::string text;
	std::vector<AstNode*> children;

	explicit AstNode(AstKind node_kind)
		: kind(node_kind)
		, token(kNoAstToken)
	{}

	// Appends `child`, ignoring a null one so that an optional part of a rule
	// can be appended without asking whether it was there.
	void add(AstNode* child)
	{
		if (child != nullptr)
		{
			children.push_back(child);
		}
	}
};

// The nodes of one translation unit.
//
// A backtracking parser builds nodes it later abandons, so nodes are owned by
// the parse rather than by the tree: the arena outlives every node in it and
// frees them all at once, and an abandoned subtree costs nothing to drop.
class AstArena
{
public:
	AstNode* make(AstKind kind)
	{
		nodes_.emplace_back(kind);
		return &nodes_.back();
	}

private:
	std::deque<AstNode> nodes_;
};

// Writes the tree rooted at `root` as the assignment's line oriented dump,
// indenting two spaces per level starting at `depth`.
void write_ast(std::ostream& out, const AstNode& root, unsigned depth);
