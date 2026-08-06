#pragma once

// LowIR -> CY86 backend adapter.
//
// Translates a validated `lowir_model::Program` into PA9 CY86 source text using
// a memory-resident value model: every parameter, stack slot and temporary owns
// a frame cell, and each LowIR instruction becomes a short fixed CY86 sequence.

#include <string>

#include "lowir_model.h"

namespace lowir_cy86 {

std::string emit_cy86_program(const lowir_model::Program & program);

}  // namespace lowir_cy86
