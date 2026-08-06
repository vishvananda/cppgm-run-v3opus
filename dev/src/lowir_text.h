#pragma once

// LowIR text boundary: lexing, parsing and structural validation of the
// serialized LowIR format described by `pa13/lowir.md` and `pa13/pa13.gram`.
//
// This module owns every fact that is recoverable from LowIR source text and
// hands the result to backends as a typed `lowir_model::Program`. It never
// produces target text of its own.

#include <string>
#include <vector>

#include "lowir_model.h"

namespace lowir_model {

// Scalar/pointer/object properties of a LowIR type, recovered once from its
// spelling so backends do not re-parse type text per instruction.
struct TypeFacts
{
  enum Category
  {
    TC_VOID,
    TC_INTEGER,
    TC_FLOAT,
    TC_POINTER,
    TC_OBJECT
  };

  Category category = TC_VOID;
  bool is_signed = false;
  std::size_t size = 0;       // storage size in bytes
  std::size_t alignment = 1;  // required alignment in bytes
  int width_bits = 0;         // register width for scalar operations
};

// Returns false when `type` is not a legal PA13 LowIR type spelling.
bool describe_low_type(const LowType & type, TypeFacts & facts);

// True for types whose values live in addressable storage rather than a
// general-purpose register (`f80` and `obj<...>`).
bool is_memory_class_type(const LowType & type);

}  // namespace lowir_model
