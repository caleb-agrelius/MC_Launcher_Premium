#include "Util/TracedFs.h"

#include "Util/DebugTrace.h"

namespace fs = std::filesystem;

namespace {

std::string Text(const fs::path& path)
{
	return Debug::PathText(path.u8string());
}

// "path" or "path (error text)" for the end of a trace line.
std::string Outcome(const std::error_code& ec)
{
	return ec ? "  FAILED: " + ec.message() : std::string();
}

void Trace(const std::string& line)
{
	Debug::Write(Debug::kFile, line);
}

}

namespace TracedFs {

bool Remove(const fs::path& path, std::error_code& ec)
{
	const bool removed = fs::remove(path, ec);
	if (removed || ec)
		Trace("remove     " + Text(path) + Outcome(ec));
	return removed;
}

std::uintmax_t RemoveAll(const fs::path& path, std::error_code& ec)
{
	const std::uintmax_t count = fs::remove_all(path, ec);
	if (count > 0 || ec)
		Trace("remove-all " + Text(path) + " (" + std::to_string(count) + " item(s))" + Outcome(ec));
	return count;
}

void Rename(const fs::path& from, const fs::path& to, std::error_code& ec)
{
	fs::rename(from, to, ec);
	Trace("rename     " + Text(from) + " -> " + Text(to) + Outcome(ec));
}

bool CopyFile(const fs::path& from, const fs::path& to, std::error_code& ec)
{
	return CopyFile(from, to, fs::copy_options::none, ec);
}

bool CopyFile(const fs::path& from, const fs::path& to, fs::copy_options options, std::error_code& ec)
{
	const bool copied = fs::copy_file(from, to, options, ec);
	if (copied || ec)
		Trace("copy       " + Text(from) + " -> " + Text(to) + Outcome(ec));
	return copied;
}

void Copy(const fs::path& from, const fs::path& to, fs::copy_options options, std::error_code& ec)
{
	fs::copy(from, to, options, ec);
	Trace("copy-tree  " + Text(from) + " -> " + Text(to) + Outcome(ec));
}

bool CreateDirectories(const fs::path& path, std::error_code& ec)
{
	const bool created = fs::create_directories(path, ec);
	if (created || ec)
		Trace("mkdir      " + Text(path) + Outcome(ec));
	return created;
}

bool CreateDirectories(const fs::path& path)
{
	std::error_code ec;
	const bool created = CreateDirectories(path, ec);
	if (ec)
		throw fs::filesystem_error("create_directories", path, ec);
	return created;
}

void CreateHardLink(const fs::path& target, const fs::path& link, std::error_code& ec)
{
	fs::create_hard_link(target, link, ec);
	Trace("hard-link  " + Text(link) + " -> " + Text(target) + Outcome(ec));
}

std::ofstream OpenWrite(const fs::path& path, std::ios::openmode mode)
{
	std::ofstream out(path, mode);
	NoteWrite(path);
	return out;
}

void NoteWrite(const fs::path& path, std::uintmax_t bytes)
{
	if (!Debug::Enabled(Debug::kFile))
		return;
	Trace("write      " + Text(path) + (bytes ? " (" + std::to_string(bytes) + " bytes)" : std::string()));
}

}
