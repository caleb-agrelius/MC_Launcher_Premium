#pragma once

#include <cstdint>
#include <string>

// SHA-1 and MD5 digests of a file as lowercase hex, used to verify downloaded pack files.
// They return an empty string if the file can't be read.
std::string Sha1HexOfFile(const std::string& path);
std::string Md5HexOfFile(const std::string& path);

// CurseForge's fingerprint of a file: MurmurHash2 (seed 1) of its bytes with tabs, line feeds,
// carriage returns and spaces left out. CurseForge identifies an uploaded file by it, so it
// finds which project an installed mod belongs to.
std::uint32_t CurseForgeFingerprint(const std::string& bytes);
// The same for a file on disk, read in two passes. False if the file can't be read.
bool CurseForgeFingerprintOfFile(const std::string& path, std::uint32_t& fingerprint);
