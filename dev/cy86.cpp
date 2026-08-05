// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cy86_codegen.h"
#include "cy86_model.h"
#include "cy86_parser.h"
#include "name_table.h"
#include "preprocessor.h"
#include "sema_token.h"
#include "source_files.h"
#include "x86_elf.h"

// cy86: the CY86 mock intermediate language translated to a Linux x86-64
// program.
//
// Phases 1 to 7 are PA1 to PA5 unchanged, and the assignment concatenates what
// every translation unit tokenizes to into one sequence before parsing it, so
// one token vector and one literal pool serve the whole command line.  What
// follows is in dev/src: the parser, the code generator, and the ELF writer.

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

// The tokens of every source file, in the order the command line gives them.
// Each file ends with `ST_EOF`, which is dropped so that the sequence reads as
// one program with one end.
void read_sources(const std::vector<std::string>& srcfiles, NameTable& names,
                  std::vector<SemaToken>& tokens, std::vector<LiteralValue>& literals)
{
	PreprocessorOptions options;
	options.author = kAuthor;
	set_build_time(options);

	SourceFileTable files;
	for (std::size_t index = 0; index < srcfiles.size(); ++index)
	{
		build_sema_tokens(files, options, srcfiles[index], names, tokens, literals);
		tokens.pop_back();
	}

	SemaToken end;
	end.type = ST_EOF;
	end.name = kNoName;
	tokens.push_back(end);
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

		std::string outfile;
		std::vector<std::string> srcfiles;
		for (std::size_t index = 0; index < args.size(); ++index)
		{
			if (args[index] == "--target" && index + 1 < args.size())
			{
				++index;
			}
			else if (args[index] == "-o" && index + 1 < args.size())
			{
				outfile = args[++index];
			}
			else
			{
				srcfiles.push_back(args[index]);
			}
		}

		if (outfile.empty())
		{
			throw std::logic_error("invalid usage");
		}

		NameTable names;
		std::vector<SemaToken> tokens;
		std::vector<LiteralValue> literals;
		read_sources(srcfiles, names, tokens, literals);

		Cy86OpcodeTable opcodes;
		Cy86Program program;
		Cy86Parser parser(tokens, literals, names, opcodes, program);
		parser.run();

		Cy86Codegen codegen(program, names);
		codegen.run();

		ElfProgram elf;
		elf.image = codegen.image();
		elf.base_address = 0x400000;
		elf.image_offset = codegen.base() - elf.base_address;
		elf.entry = codegen.base();
		write_elf_executable(outfile, elf);

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR:" << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
