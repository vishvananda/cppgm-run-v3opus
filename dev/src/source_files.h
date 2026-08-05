#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "source_reader.h"

// The file system as the compiler sees it: the bytes of a source file, where
// its lines begin, and the identity the operating system gives it.
//
// A header is included as many times as a translation unit asks for it, and
// each inclusion has to be preprocessed again because the macros in force
// differ.  Reading and indexing it again does not, so a distinct path is read
// once and every inclusion shares the result.

// The identity `stat` gives a file: device and inode, which is what makes two
// paths that reach the same file compare equal.  `#pragma once` is defined in
// terms of this rather than of the path, so `a/../b.h` and `b.h` are one file.
typedef std::pair<unsigned long, unsigned long> SourceFileId;

struct SourceFileIdHash
{
	std::size_t operator()(const SourceFileId& id) const
	{
		return static_cast<std::size_t>(id.first * 1000003ULL + id.second);
	}
};

// One source file, read once.
//
// `line_starts_` indexes the *physical* lines of the file as it was written,
// which is what `__LINE__` counts: phase 2 splices lines away, but a spliced
// line still occupies the physical lines it was written on.
class SourceFile
{
public:
	SourceFile(std::string path, std::string bytes);

	const std::string& path() const { return path_; }
	// The bytes, shared rather than copied: a reader over an inclusion of this
	// file borrows them for as long as the inclusion lasts.
	const SourceText& text() const { return text_; }

	// The one-based physical line containing byte `offset`.
	unsigned line_of(std::size_t offset) const;

private:
	std::string path_;
	SourceText text_;
	std::vector<std::uint32_t> line_starts_;
};

// The files read so far, by the path they were named with.  This is the one
// place the operating system is asked about a path, so a path costs one
// attempt for the whole run whatever asks about it and however often.
class SourceFileTable
{
public:
	// The file at `path`, read if this is the first time it is asked for.
	// Null when it cannot be read.  A path that could not be read is
	// remembered as such, so a failed search costs one attempt for the run.
	const SourceFile* open(const std::string& path);

	// The identity of the file at `path`; false when there is no such file.
	bool identify(const std::string& path, SourceFileId& out);

private:
	// What `stat` says about a path, remembered including that it said the
	// path is not there: an inclusion searches two paths and a repeated
	// inclusion searches the same two again.
	struct Identity
	{
		bool found;
		SourceFileId id;
	};

	static bool stat_identity(const std::string& path, SourceFileId& out);

	std::unordered_map<std::string, std::unique_ptr<SourceFile> > files_;
	std::unordered_map<std::string, Identity> identities_;
};
