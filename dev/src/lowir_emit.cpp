#include "lowir_emit.h"

#include <ctime>
#include <stdexcept>

#include "ast_model.h"
#include "ast_parser.h"
#include "ast_tokens.h"
#include "lowir_lower.h"
#include "preprocessor.h"
#include "sema_analyzer.h"
#include "source_files.h"

// The PA15 driver mode.
//
// The earlier dump modes write one description per translation unit; a LowIR
// program is one program however many units it was translated from, so this
// builds the program across the whole command line and writes it once.

namespace
{

// `__CPPGM_AUTHOR__`, as enrolled in the course.
const char kAuthor[] = "Vishvananda Abrams";

void set_build_time(PreprocessorOptions& options)
{
	const std::time_t now = std::time(nullptr);
	const std::string stamp = std::asctime(std::localtime(&now));
	if (stamp.size() < 24)
	{
		return;
	}
	options.date = stamp.substr(4, 7) + stamp.substr(20, 4);
	options.time = stamp.substr(11, 8);
}

}

void emit_lowir(const std::string& outfile,
                const std::vector<std::string>& inputs)
{
	PreprocessorOptions options;
	options.author = kAuthor;
	set_build_time(options);

	SourceFileTable files;
	LowirProgramBuilder builder;
	for (std::size_t index = 0; index < inputs.size(); ++index)
	{
		AstTokenStream tokens;
		tokens.build(files, options, inputs[index]);
		AstArena arena;
		AstParser parser(tokens, arena);
		const AstNode* root = parser.run();
		if (root == nullptr)
		{
			throw std::runtime_error(inputs[index] + " is not a translation unit");
		}
		// 3.5: a declaration is a fact about one translation unit, so each is
		// analysed on its own; what crosses the boundary is the symbol a name
		// with external linkage stands for, which the program builder holds.
		SemaAnalyzer analyzer(SemaDialect::Lowering);
		// 16.6: a `#pragma pack` is a phase 4 directive whose one effect is on
		// 9.2p13's layout in phase 7, so what the stream recorded about where
		// it stood travels with the tree that was parsed from it.
		analyzer.set_packing(tokens.packs());
		// 2.2p1: which of the definitions this unit read are the ones its own
		// source wrote is a phase 4 fact too, and the same stream recorded it.
		analyzer.set_sources(tokens.sources());
		analyzer.run(*root);
		builder.add_unit(analyzer.resolved(), analyzer.types());
	}
	builder.finish();
	// The PA13 validator also holds a program to the runtime roles a linked
	// program has.  What this mode writes is the LowIR of the translation units
	// on the command line, which need not be a whole program, so the structural
	// rules are what apply to it.
	lowir_model::write_lowir_program_file(outfile, builder.program());
}
