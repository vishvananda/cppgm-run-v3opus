#pragma once

// Structural well-formedness rules for a parsed LowIR program.
//
// The checks here are the ones `pa13/lowir.md` calls out as required rejections:
// symbol/alias/TLS-wrapper uniqueness, per-function name uniqueness, block and
// terminator structure, operand definedness, parameter/call-boundary legality,
// and conversion width direction. They run on the typed program, never on text.

#include "lowir_model.h"

namespace lowir_model {

// Throws `ParseError` describing the first structural violation found.
void validate_lowir_program(const Program & program);

// Names of the program-wide runtime roles, resolved from `role=` metadata with
// the legacy `@main` / `@__cppgm_init` / `@__cppgm_fini` spellings as fallback.
struct RuntimeRoles
{
  const Function * entry = 0;
  const Function * init = 0;
  const Function * fini = 0;
};

RuntimeRoles resolve_runtime_roles(const Program & program);

}  // namespace lowir_model
