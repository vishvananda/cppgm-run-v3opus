// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "parse_token.h"
#include "preprocessor.h"
#include "recognizer.h"
#include "source_files.h"

// recog: is each source file a `translation-unit` of the PA6 grammar?
//
// The phases up to and including tokenization are PA1 to PA5, so this file
// builds the pipeline, hands the token sequence to the Recognizer and writes
// the verdict.  A file that cannot be read, that is not a valid sequence of
// C++ tokens, or whose tokens do not match the grammar is `BAD`; only a usage
// or output error is a failure of the tool itself.

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

bool recognize_file(SourceFileTable& files, const PreprocessorOptions& options,
                    const std::string& path)
{
	std::vector<ParseToken> tokens;
	try
	{
		build_parse_tokens(files, options, path, tokens);
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << path << ":" << error.what() << std::endl;
		return false;
	}

	Recognizer recognizer(tokens);
	return recognizer.recognize();
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
		out << "recog " << nsrcfiles << "\n";

		// The bytes of a file are the same whichever source file asks for
		// them, so the table outlives one translation unit.
		SourceFileTable files;
		for (std::size_t index = 0; index < nsrcfiles; ++index)
		{
			const std::string& srcfile = args[index + 2];
			const bool matched = recognize_file(files, options, srcfile);
			out << srcfile << (matched ? " OK" : " BAD") << "\n";
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
