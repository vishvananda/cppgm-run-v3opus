// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "decl_parser.h"
#include "entity_model.h"
#include "name_table.h"
#include "preprocessor.h"
#include "sema_token.h"
#include "source_files.h"
#include "type_model.h"

// nsdecl: what does each source file declare at namespace scope?
//
// Phases 1 to 7 are PA1 to PA5 and the terminals are PA6's, so this file builds
// the pipeline, hands each translation unit to the semantic parser and writes
// the object model it produced.  A translation unit is analysed on its own:
// linkage is a later assignment, so nothing is shared between two of them
// except the tables that hold facts about the run rather than about a unit.

namespace
{

// `__CPPGM_AUTHOR__`, as enrolled in the course.
const char kAuthor[] = "Vishvananda Abrams";

// The build date and time, taken from `std::asctime` once for the whole run.
// Its format is "Www Mmm dd hh:mm:ss yyyy\n", from which `__DATE__` is the
// month, day and year and `__TIME__` is the time of day.
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

void describe_file(std::ostream& out, SourceFileTable& files,
                   const PreprocessorOptions& options, const std::string& path,
                   NameTable& names, TypeTable& types)
{
	std::vector<SemaToken> tokens;
	std::vector<LiteralValue> literals;
	build_sema_tokens(files, options, path, names, tokens, literals);

	TranslationUnitModel model(types);
	DeclParser parser(tokens, literals, types, model);
	parser.run();

	write_namespace(out, model.global(), types, names);
}

}

int main(int argc, char** argv)
{
	try
	{
		std::vector<std::string> args;
		for (int index = 1; index < argc; ++index)
		{
			args.emplace_back(argv[index]);
		}

		if (args.size() < 3 || args[0] != "-o")
		{
			throw std::logic_error("invalid usage");
		}

		const std::string outfile = args[1];
		const std::size_t nsrcfiles = args.size() - 2;

		PreprocessorOptions options;
		options.author = kAuthor;
		set_build_time(options);

		std::ofstream out(outfile);
		if (!out)
		{
			throw std::runtime_error("cannot write " + outfile);
		}
		out << nsrcfiles << " translation units\n";

		// The bytes of a file, the spelling of a name and the identity of a
		// type are facts about the run rather than about one translation unit,
		// so their tables outlive each unit's model.
		SourceFileTable files;
		NameTable names;
		TypeTable types;
		for (std::size_t index = 0; index < nsrcfiles; ++index)
		{
			const std::string& srcfile = args[index + 2];
			out << "start translation unit " << srcfile << "\n";
			describe_file(out, files, options, srcfile, names, types);
			out << "end translation unit\n";
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
