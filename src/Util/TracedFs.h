#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

// The file operations that change the disk, each doing what the std::filesystem function of the
// same name does and tracing it on the file channel of the debug harness (Util/DebugTrace.h).
// Use these instead of the std::filesystem calls so every change to the player's files shows in
// the trace; reads and listings stay plain std::filesystem. Each takes the error_code the
// std::filesystem version takes.
namespace TracedFs {

bool Remove(const std::filesystem::path& path, std::error_code& ec);
std::uintmax_t RemoveAll(const std::filesystem::path& path, std::error_code& ec);
void Rename(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);
bool CopyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);
bool CopyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::filesystem::copy_options options,
	std::error_code& ec);
void Copy(const std::filesystem::path& from, const std::filesystem::path& to, std::filesystem::copy_options options,
	std::error_code& ec);
bool CreateDirectories(const std::filesystem::path& path, std::error_code& ec);
// Like the std::filesystem call without an error_code: throws std::filesystem::filesystem_error.
bool CreateDirectories(const std::filesystem::path& path);
// A hard link at link to the file target.
void CreateHardLink(const std::filesystem::path& target, const std::filesystem::path& link, std::error_code& ec);

// Opens path for writing, like std::ofstream(path, mode), and notes the write. Use it for the files the
// launcher writes itself (saves, markers, config files), so they show in the trace.
std::ofstream OpenWrite(const std::filesystem::path& path, std::ios::openmode mode = std::ios::out);

// Notes that the code wrote a file itself (an ofstream, an extracted zip entry): "write path (N bytes)".
// Call it after the write; bytes of 0 are left out of the line.
void NoteWrite(const std::filesystem::path& path, std::uintmax_t bytes = 0);

}
