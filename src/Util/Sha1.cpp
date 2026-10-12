#include "Sha1.h"
#include "Paths.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

struct Sha1 {
	std::uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
	unsigned char block[64] = {};
	std::size_t blockLen = 0;
	std::uint64_t totalBytes = 0;

	static std::uint32_t Rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

	void ProcessBlock()
	{
		std::uint32_t w[80];
		for (int i = 0; i < 16; ++i) {
			w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
				(std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
		}
		for (int i = 16; i < 80; ++i)
			w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; ++i) {
			std::uint32_t f, k;
			if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
			const std::uint32_t temp = Rotl(a, 5) + f + e + k + w[i];
			e = d; d = c; c = Rotl(b, 30); b = a; a = temp;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}

	void Update(const unsigned char* data, std::size_t len)
	{
		totalBytes += len;
		while (len > 0) {
			const std::size_t take = std::min(len, sizeof(block) - blockLen);
			std::copy(data, data + take, block + blockLen);
			blockLen += take;
			data += take;
			len -= take;
			if (blockLen == sizeof(block)) {
				ProcessBlock();
				blockLen = 0;
			}
		}
	}

	std::string Finish()
	{
		const std::uint64_t bits = totalBytes * 8;
		const unsigned char pad = 0x80;
		Update(&pad, 1);
		const unsigned char zero = 0;
		while (blockLen != 56)
			Update(&zero, 1);
		unsigned char length[8];
		for (int i = 0; i < 8; ++i)
			length[i] = static_cast<unsigned char>(bits >> (56 - i * 8));
		Update(length, 8);

		char hex[41];
		for (int i = 0; i < 5; ++i)
			std::snprintf(hex + i * 8, 9, "%08x", h[i]);
		return std::string(hex, 40);
	}
};

} // namespace

std::string Sha1File(const std::string& path)
{
	std::ifstream in(LongPath(std::filesystem::path(path)), std::ios::binary); // deep pack files too
	if (!in.is_open())
		return {};
	Sha1 sha;
	std::vector<char> buffer(1 << 16);
	while (in) {
		in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize got = in.gcount();
		if (got > 0)
			sha.Update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<std::size_t>(got));
	}
	if (in.bad())
		return {};
	return sha.Finish();
}
