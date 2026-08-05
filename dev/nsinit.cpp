// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "decl_parser.h"
#include "entity_model.h"
#include "init_semantics.h"
#include "name_table.h"
#include "preprocessor.h"
#include "program_model.h"
#include "sema_token.h"
#include "source_files.h"
#include "type_model.h"

// nsinit: what does the program these source files make up contain?
//
// Phases 1 to 7 are PA1 to PA5 and the declaration parser is PA7's, extended
// with the productions and the semantics `pa8.gram` adds.  What is new is that
// the translation units are one program: each is analysed against its own
// object model, because a namespace is a declarative region of one unit, but
// every namespace scope declaration reaches one `ProgramImage`, which is what
// 3.5 links, 3.6.2 initializes and the assignment lays out.

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

void analyse_file(SourceFileTable& files, const PreprocessorOptions& options,
                  const std::string& path, NameTable& names, TypeTable& types,
                  ProgramImage& image, InitSemantics& init)
{
	std::vector<SemaToken> tokens;
	std::vector<LiteralValue> literals;
	build_sema_tokens(files, options, path, names, tokens, &literals);

	ImageContext context;
	context.literals = &literals;
	context.image = &image;
	context.init = &init;

	TranslationUnitModel model(types);
	DeclParser parser(tokens, types, model, &context);
	image.begin_unit();
	parser.run();
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

		// The bytes of a file, the spelling of a name, the identity of a type
		// and the objects of the program are facts about the run rather than
		// about one translation unit, so their tables outlive each unit's
		// model.
		SourceFileTable files;
		NameTable names;
		TypeTable types;
		ProgramImage image(types);
		InitSemantics init(types, image);
		for (std::size_t index = 0; index < nsrcfiles; ++index)
		{
			analyse_file(files, options, args[index + 2], names, types, image, init);
		}

		std::ofstream out(outfile, std::ios::binary);
		if (!out)
		{
			throw std::runtime_error("cannot write " + outfile);
		}
		image.write(out);
		out.flush();
		if (!out)
		{
			throw std::runtime_error("cannot write " + outfile);
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
