#include "types_emit.h"

#include <ostream>

#include "ast_emit.h"
#include "sema_analyzer.h"

namespace
{

// Each translation unit is analysed on its own: 3.5 makes a declaration a fact
// about one unit, so the scopes, entities and types of one are never reachable
// from another.
void write_unit_types(std::ostream& out, const AstNode& unit)
{
	SemaAnalyzer analyzer;
	analyzer.run(unit);
	analyzer.write(out);
}

}

void emit_types(const std::string& outfile, const std::vector<std::string>& inputs)
{
	emit_translation_units(outfile, inputs, write_unit_types);
}
