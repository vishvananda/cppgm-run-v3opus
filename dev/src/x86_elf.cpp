#include "x86_elf.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

#include <sys/stat.h>

namespace
{

const std::size_t kHeaderSize = 64;
const std::size_t kProgramHeaderSize = 56;

// One little-endian field of `size` bytes, appended to `out`.  The headers are
// built as bytes rather than as structures so that the file layout is what
// this file says it is whatever the host packs a structure like.
void field(std::string& out, unsigned long long value, std::size_t size)
{
	for (std::size_t index = 0; index < size; ++index)
	{
		out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
	}
}

void elf_header(std::string& out, const ElfProgram& program)
{
	const char magic[] = { 0x7F, 'E', 'L', 'F', 2, 1, 1, 0 };
	out.append(magic, sizeof(magic));
	out.append(8, '\0');          // ABI version and padding
	field(out, 2, 2);             // ET_EXEC
	field(out, 0x3E, 2);          // EM_X86_64
	field(out, 1, 4);             // EV_CURRENT
	field(out, program.entry, 8);
	field(out, kHeaderSize, 8);   // the program header table follows
	field(out, 0, 8);             // no section header table
	field(out, 0, 4);             // no processor specific flags
	field(out, kHeaderSize, 2);
	field(out, kProgramHeaderSize, 2);
	field(out, 1, 2);             // one program header
	field(out, 0, 2);             // no section header table entries
	field(out, 0, 2);
	field(out, 0, 2);
}

void program_header(std::string& out, const ElfProgram& program)
{
	const unsigned long long size = program.image_offset + program.image.size();
	field(out, 1, 4);             // PT_LOAD
	field(out, 7, 4);             // readable, writable and executable
	field(out, 0, 8);             // from the start of the file
	field(out, program.base_address, 8);
	field(out, program.base_address, 8);
	field(out, size, 8);
	field(out, size, 8);
	field(out, 0x1000, 8);
}

}

void write_elf_executable(const std::string& path, const ElfProgram& program)
{
	std::string file;
	file.reserve(program.image_offset + program.image.size());
	elf_header(file, program);
	program_header(file, program);
	if (file.size() > program.image_offset)
	{
		throw std::runtime_error("the program image overlaps its headers");
	}
	file.resize(program.image_offset, '\0');
	file += program.image;

	std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
	out.write(file.data(), static_cast<std::streamsize>(file.size()));
	out.close();
	if (!out)
	{
		throw std::runtime_error("cannot write " + path);
	}

	if (chmod(path.c_str(), 0755) != 0)
	{
		throw std::runtime_error("cannot make " + path + " executable");
	}
}
