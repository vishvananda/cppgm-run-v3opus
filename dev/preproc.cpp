// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DebugPostTokenStream.h"
#include "post_token.h"
#include "posttokenizer.h"
#include "preprocessor.h"
#include "source_files.h"

// preproc: translation phases 1 to 6 and the tokenization part of phase 7, on
// a set of source files.
//
// The phases live in dev/src, so this file builds the pipeline and writes the
// outfile: phases 1 to 3 and 4 are the Preprocessor, which feeds the phase 5
// to 7 driver PA2 already uses.  Each source file is preprocessed on its own,
// sharing only the build date and time and the bytes of files already read.

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

// One source file: phases 1 to 7 driven to exhaustion, described on `out`.
void preprocess(SourceFileTable& files, const PreprocessorOptions& options,
                const std::string& path, std::ostream& out)
{
	DebugPostTokenStream stream(out);
	Preprocessor preprocessor(files, options, path);
	PostTokenizer tokenizer(preprocessor);

	PostToken token;
	while (tokenizer.next(token))
	{
		if (token.kind == PostTokenKind::Invalid)
		{
			throw SourceError(" a token of the file is not a token of C++");
		}
		stream.emit(token);
	}
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
			throw std::logic_error("invalid usage");

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
		out << "preproc " << nsrcfiles << "\n";

		// The bytes of a file are the same whichever source file asks for
		// them, so the table outlives one translation unit.  Nothing else
		// does: every semantic fact belongs to one Preprocessor.
		SourceFileTable files;
		for (std::size_t index = 0; index < nsrcfiles; ++index)
		{
			const std::string& srcfile = args[index + 2];
			out << "sof " << srcfile << "\n";
			preprocess(files, options, srcfile, out);
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR:" << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
