#include "semantics_emit.h"

#include <ostream>

#include "ast_emit.h"
#include "ast_model.h"
#include "sema_analyzer.h"

namespace
{

// 3.5 makes a declaration a fact about one translation unit, so each is
// analysed on its own and nothing of one is reachable from another.
void write_unit_semantics(std::ostream& out, const AstNode& unit,
                          AstArena& written)
{
	SemaAnalyzer analyzer(SemaDialect::Semantics);
	// 7.1.6.2p1: a name whose nested-name-specifier begins with a
	// decltype-specifier arrives here as one spelling, and the tree the parse
	// read for the operand is what says which region it names.
	analyzer.set_expressions(written);
	analyzer.run(unit);
	analyzer.write(out);
}

}

void emit_semantics(const std::string& outfile,
                    const std::vector<std::string>& inputs)
{
	emit_translation_units(outfile, inputs, write_unit_semantics);
}
