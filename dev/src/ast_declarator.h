#pragma once

#include <string>

#include "ast_model.h"

// The questions a declarator's own syntax answers.
//
// Each of these is a fact of the tree the parse already built, or of the
// spelling a name was written as, and none of them reads a token, a name in
// scope or any other parser state - so they hold wherever that tree is read
// and belong beside the model rather than inside the rule that made it.

// True when a declarator declares a function, which is what tells a function
// definition apart from a declaration with a braced initializer.
//
// 8.3p1 builds the type outside in, so which constructor the declarator-id
// ends up under is not the same question as which suffixes the declarator
// wrote.  A declarator hands its direct-declarator the type its ptr-operators
// made, and a direct-declarator hands its core the type its *outermost* suffix
// made - and the outermost is the last one, so the constructor the core is
// left with is the *first* suffix at its own level, or whatever the level
// around it handed down where that level wrote none.  `int (*f)(int)` is the
// pointer its own level makes of the parameter-clause above it, and
// `int (&f(int))[2]` is the function its own parameter-clause makes however
// many suffixes stand outside it.
bool declares_function(const AstNode* declarator, bool inherited = false);

// 12.1p1: a declarator whose first suffix is a parameter-clause, which is the
// one shape a constructor and a destructor are written in.
bool declares_bare_function(const AstNode* declarator);

// The declarator-id a declarator ends in, or null for an abstract one.
const AstNode* declarator_identifier(const AstNode* declarator);
bool has_declarator_identifier(const AstNode* declarator);

// 3.4.1p8: the nested-name-specifier a declarator-id was written with, up to
// and including its last `::`, or empty for an unqualified one.  It is what
// says which region the rest of the declarator and the body after it reach
// unqualified.
std::string declarator_qualifier(const AstNode* declarator);
std::string name_qualifier(const std::string& spelling);

// 14.5.5p1: whether a class-head-name is a simple-template-id, which is the one
// class-name that ends in `>`.
bool class_head_is_template_id(const std::string& name);
