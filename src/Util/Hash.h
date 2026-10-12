#pragma once

#include <string>

// SHA-1 and MD5 digests as lowercase hex, used to verify downloaded pack files.
// The *OfFile versions return an empty string if the file can't be read.
std::string Sha1Hex(const std::string& data);
std::string Md5Hex(const std::string& data);
std::string Sha1HexOfFile(const std::string& path);
std::string Md5HexOfFile(const std::string& path);
