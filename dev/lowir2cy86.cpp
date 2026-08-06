// PA13 `lowir2cy86`: read LowIR source text and write equivalent PA9 CY86 text.

#include "lowir_cy86.h"
#include "lowir_model.h"
#include "tool_help_text.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

vector<string> collect_args(int argc, char ** argv)
{
  vector<string> args;
  for(int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

bool has_help_arg(const vector<string> & args)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "--help" || args[i] == "-h") {
      return true;
    }
  }
  return false;
}

void parse_output_invocation(const vector<string> & args,
                             string & outfile,
                             vector<string> & srcfiles)
{
  if(args.size() < 3 || args[0] != "-o") {
    throw logic_error("invalid usage");
  }

  outfile = args[1];
  srcfiles.assign(args.begin() + 2, args.end());
}

int run_lowir2cy86_mode(const vector<string> & args)
{
  if(has_help_arg(args)) {
    cout << lowir2cy86_help_text();
    return EXIT_SUCCESS;
  }

  string outfile;
  vector<string> srcfiles;
  parse_output_invocation(args, outfile, srcfiles);

  const lowir_model::Program program = lowir_model::parse_lowir_program_files(srcfiles);
  const string cy86 = lowir_cy86::emit_cy86_program(program);

  ofstream out(outfile.c_str(), ios::binary | ios::trunc);
  if(!out) {
    throw runtime_error("cannot open output file '" + outfile + "'");
  }
  out.write(cy86.data(), static_cast<streamsize>(cy86.size()));
  out.flush();
  if(!out) {
    throw runtime_error("cannot write output file '" + outfile + "'");
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_lowir2cy86_mode(collect_args(argc, argv));
  }
  catch(const exception & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
}
