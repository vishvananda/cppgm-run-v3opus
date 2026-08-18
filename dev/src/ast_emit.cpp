#include "ast_emit.h"

#include <ctime>
#include <fstream>
#include <ostream>
#include <stdexcept>

#include "ast_model.h"
#include "ast_parser.h"
#include "ast_tokens.h"
#include "preprocessor.h"
#include "source_files.h"

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

void emit_translation_unit(std::ostream& out, SourceFileTable& files,
                           const PreprocessorOptions& options,
                           const std::string& path,
                           void (*write_unit)(std::ostream&, const AstNode&,
                                              AstArena&))
{
	AstTokenStream tokens;
	tokens.build(files, options, path);

	AstArena arena;
	AstParser parser(tokens, arena);
	const AstNode* root = parser.run();
	if (root == nullptr)
	{
		throw std::runtime_error(path + " is not a translation unit");
	}
	write_unit(out, *root, arena);
}

void write_unit_ast(std::ostream& out, const AstNode& unit, AstArena&)
{
	write_ast(out, unit, 0);
}

}

void emit_ast(const std::string& outfile, const std::vector<std::string>& inputs)
{
	emit_translation_units(outfile, inputs, write_unit_ast);
}

void emit_translation_units(const std::string& outfile,
                            const std::vector<std::string>& inputs,
                            void (*write_unit)(std::ostream&, const AstNode&,
                                               AstArena&))
{
	PreprocessorOptions options;
	options.author = kAuthor;
	set_build_time(options);

	std::ofstream out(outfile.c_str());
	if (!out)
	{
		throw std::runtime_error("cannot write " + outfile);
	}
	out << inputs.size() << " translation units\n";

	// The bytes of a file are a fact about the run rather than about one
	// translation unit, so the table that holds them outlives each unit.
	SourceFileTable files;
	for (std::size_t index = 0; index < inputs.size(); ++index)
	{
		out << "start translation unit " << (index + 1) << "\n";
		emit_translation_unit(out, files, options, inputs[index], write_unit);
		out << "end translation unit\n";
	}
}
