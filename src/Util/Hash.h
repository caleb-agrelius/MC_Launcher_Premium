#pragma once

#include <string>

// SHA-1 and MD5 digests of a file as lowercase hex, used to verify downloaded pack files.
// They return an empty string if the file can't be read.
std::string Sha1HexOfFile(const std::string& path);
std::string Md5HexOfFile(const std::string& path);
