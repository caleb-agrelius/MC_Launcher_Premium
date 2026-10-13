#pragma once

#include <array>
#include <string>

// SHA-256 digest of a byte string (the PKCE code challenge in Microsoft sign-in).
std::array<unsigned char, 32> Sha256(const std::string& data);
