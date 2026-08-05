#include "source_files.h"

#include <algorithm>
#include <fstream>
#include <utility>

// The bootstrap system call interface the assignment provides.  `stat` is the
// one fact about a file that is not in its bytes, and it is what `#pragma
// once` is defined in terms of.
extern "C" long int syscall(long int n, ...) throw ();

namespace
{

// x86_64 `stat`, of which only the leading device and inode are read.
struct RawFileStatus
{
	unsigned long int dev;
	unsigned long int ino;
	long int rest[16];
};

const long int kStatSystemCall = 4;

}

SourceFile::SourceFile(std::string path, std::string bytes)
	: path_(std::move(path))
	, text_(std::make_shared<const std::string>(std::move(bytes)))
{
	const std::string& body = *text_;
	// One pass, once per distinct path: every later question about a line is
	// a binary search rather than a scan.
	line_starts_.push_back(0);
	for (std::size_t at = 0; at < body.size(); ++at)
	{
		if (body[at] == '\n')
		{
			line_starts_.push_back(static_cast<std::uint32_t>(at + 1));
		}
	}
}

unsigned SourceFile::line_of(std::size_t offset) const
{
	const std::vector<std::uint32_t>::const_iterator found =
		std::upper_bound(line_starts_.begin(), line_starts_.end(),
			static_cast<std::uint32_t>(offset));
	return static_cast<unsigned>(found - line_starts_.begin());
}

const SourceFile* SourceFileTable::open(const std::string& path)
{
	const std::unordered_map<std::string, std::unique_ptr<SourceFile> >::iterator
		found = files_.find(path);
	if (found != files_.end())
	{
		return found->second.get();
	}

	std::unique_ptr<SourceFile> file;
	std::ifstream in(path.c_str(), std::ios::binary);
	if (in)
	{
		std::string bytes;
		char chunk[1 << 16];
		while (in.read(chunk, sizeof chunk) || in.gcount() > 0)
		{
			bytes.append(chunk, static_cast<std::size_t>(in.gcount()));
		}
		// A path that opens but cannot be read - a directory is the one that
		// happens - is not a source file, and is not an empty one either:
		// reading it fails badly rather than at end of file.
		if (!in.bad())
		{
			file.reset(new SourceFile(path, std::move(bytes)));
		}
	}

	const SourceFile* result = file.get();
	files_.insert(std::make_pair(path, std::move(file)));
	return result;
}

bool SourceFileTable::stat_identity(const std::string& path, SourceFileId& out)
{
	RawFileStatus status;
	if (syscall(kStatSystemCall, path.c_str(), &status) != 0)
	{
		return false;
	}
	out = std::make_pair(status.dev, status.ino);
	return true;
}

bool SourceFileTable::identify(const std::string& path, SourceFileId& out)
{
	const std::unordered_map<std::string, Identity>::iterator found =
		identities_.find(path);
	if (found != identities_.end())
	{
		out = found->second.id;
		return found->second.found;
	}

	Identity identity;
	identity.id = SourceFileId();
	identity.found = stat_identity(path, identity.id);
	identities_.insert(std::make_pair(path, identity));
	out = identity.id;
	return identity.found;
}
