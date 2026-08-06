#include "semantics_emit.h"

#include <ostream>

#include "ast_emit.h"
#include "sema_analyzer.h"

namespace
{

// 3.5 makes a declaration a fact about one translation unit, so each is
// analysed on its own and nothing of one is reachable from another.
void write_unit_semantics(std::ostream& out, const AstNode& unit)
{
	SemaAnalyzer analyzer(SemaDialect::Semantics);
	analyzer.run(unit);
	analyzer.write(out);
}

}

void emit_semantics(const std::string& outfile,
                    const std::vector<std::string>& inputs)
{
	emit_translation_units(outfile, inputs, write_unit_semantics);
}
