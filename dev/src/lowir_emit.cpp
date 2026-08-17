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
		// 7.1.6.2p1 and 14.2: a decltype-specifier written inside a
		// template-argument-list reaches this analysis as text, and the tree
		// the parse read for its operand is the one thing that text cannot
		// hold - so the arena that owns the nodes travels with the tree.
		analyzer.set_expressions(arena);
		analyzer.run(*root);
		// 3.7.1p3: a block-scope `static` of a definition every unit may hold
		// is one object of the program, and no program spells a name that
		// reaches it - so what two units naming it agree on is where in the
		// source it was written, which is the record the stream kept.
		// 5.19p2: an initializer of pointer type came to *which object* it
		// designates, and no spelling below the declaration holds that - so the
		// pool those identifiers index into travels with the tree too.
		builder.add_unit(analyzer.resolved(), analyzer.types(),
		                 &tokens.positions(), &analyzer.addresses());
	}
	builder.finish();
	// The PA13 validator also holds a program to the runtime roles a linked
	// program has.  What this mode writes is the LowIR of the translation units
	// on the command line, which need not be a whole program, so the structural
	// rules are what apply to it.
	lowir_model::write_lowir_program_file(outfile, builder.program());
}
