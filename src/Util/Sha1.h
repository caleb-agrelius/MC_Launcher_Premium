#pragma once

#include <string>

// Lowercase hex SHA-1 of a file's contents, or an empty string if it can't be read.
// Mojang and Modrinth publish SHA-1 hashes for every file the launcher downloads.
std::string Sha1File(const std::string& path);
