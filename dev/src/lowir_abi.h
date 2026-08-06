#pragma once

#include <string>

#include "type_model.h"

struct SemaEntity;

// The object-file name of a resolved declaration.
//
// 3.5p9 makes a name with external linkage one entity across a program, and the
// object file names it with a spelling the language does not write: the Itanium
// encoding of the declaration and its type.  PA14 owns that encoding, and this
// is the one place that builds its typed target from what the analysis resolved
// - the declaration, the regions around it, its language linkage, and its type
// - so the compiler and the standalone `abimangle` tool spell one name once.
//
// Nothing here reads syntax or text: a `SemaEntity` and a `TypeId` say
// everything the encoder is given.
std::string abi_symbol_of(const SemaEntity& entity, TypeTable& types);
