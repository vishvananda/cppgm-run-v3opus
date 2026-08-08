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
// 12.1 and 12.4: one constructor or destructor has more than one object-file
// name, because the ABI gives a complete object and a base subobject an entry
// point each.  This milestone has no virtual base, so the two are one body
// under two names, and a caller asks for whichever of them it is naming.
const unsigned kCompleteObjectAbi = 0;
const unsigned kBaseObjectAbi = 1;
// 12.4 and 5.3.5p3: the third entry point a *virtual* destructor has, which
// runs the destructor on the complete object and then gives its storage back.
// It is the entry a `delete` of a pointer to a base reaches, because only the
// object's own class knows which deallocation function 5.3.5p9 chose.
const unsigned kDeletingObjectAbi = 2;

std::string abi_symbol_of(const SemaEntity& entity, TypeTable& types,
                          unsigned variant = kCompleteObjectAbi);

// 10.3 and the ABI: the object-file names the polymorphic object model gives a
// class - its virtual function table, its type information record, and the
// string that record names the type by.  None of the three is a declaration any
// program wrote, so each is named from the type alone.
std::string abi_vtable_symbol_of(TypeId type, TypeTable& types);
std::string abi_typeinfo_symbol_of(TypeId type, TypeTable& types);
std::string abi_typeinfo_name_symbol_of(TypeId type, TypeTable& types);
// The bytes that string holds, which are the encoding of the type itself - the
// `_ZTS` name without the prefix that says what the name is for.
std::string abi_type_name_of(TypeId type, TypeTable& types);

// 14.7.1p1 and 3.2p5: whether an instantiation made this declaration - a
// specialization of a function template, or a member of a class a template-id
// named.  Such a definition is one every translation unit that needs it writes
// for itself, so no unit owns it and the object file has to say so; the walk
// is the same one the encoded name is built from, because it is the same
// question about the same regions.
bool abi_instantiated(const SemaEntity& entity, TypeTable& types);
// The same question asked of a class rather than of a declaration, which is
// what the tables and records named after a class read.
bool abi_instantiated_class(const SemaEntity& entity, TypeTable& types);

// 3.7.2p2: the object-file name of the wrapper function the ABI gives a
// variable with thread storage duration.  It is named after the variable
// rather than after a declaration of its own, because no program declares it.
std::string abi_thread_wrapper_of(const SemaEntity& entity);
