#pragma once

#include <string>

// A Linux x86-64 program, as the ELF specification describes an executable
// file.
//
// A CY86 program has one segment: the assignment gives the language no way to
// say that part of an image is read only or that part of it is not in the
// file, so one loadable, readable, writable and executable segment holds the
// whole image and there are no sections to describe it with.
//
// `image_offset` is where the image sits inside that segment.  It is a whole
// page, so the headers share the segment's first page with nothing else and
// the image starts on the widest alignment a literal can ask for.
struct ElfProgram
{
	std::string image;
	unsigned long long base_address;   // where the segment loads
	unsigned long long image_offset;   // where the image starts inside it
	unsigned long long entry;
};

// Writes `program` to `path` and makes it executable.  Throws when the file
// cannot be written.
void write_elf_executable(const std::string& path, const ElfProgram& program);
