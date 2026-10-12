#include "Hash.h"
#include "Paths.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::uint32_t RotateLeft(std::uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

std::string ToHex(const std::uint8_t* bytes, std::size_t count)
{
	static const char* digits = "0123456789abcdef";
	std::string hex;
	hex.reserve(count * 2);
	for (std::size_t i = 0; i < count; ++i) {
		hex.push_back(digits[bytes[i] >> 4]);
		hex.push_back(digits[bytes[i] & 0x0F]);
	}
	return hex;
}

// Both algorithms process 64-byte blocks and pad the same way; only the length's
// byte order differs (big-endian for SHA-1, little-endian for MD5).
template <typename Algorithm>
class BlockHasher {
public:
	void Update(const std::uint8_t* data, std::size_t size)
	{
		m_length += size;
		while (size > 0) {
			const std::size_t take = std::min(size, sizeof(m_buffer) - m_used);
			std::memcpy(m_buffer + m_used, data, take);
			m_used += take;
			data += take;
			size -= take;
			if (m_used == sizeof(m_buffer)) {
				m_algorithm.Block(m_buffer);
				m_used = 0;
			}
		}
	}

	std::string Finish()
	{
		const std::uint64_t bits = m_length * 8;
		const std::uint8_t pad = 0x80;
		Update(&pad, 1);
		const std::uint8_t zero = 0;
		while (m_used != 56)
			Update(&zero, 1);
		std::uint8_t lengthBytes[8];
		for (int i = 0; i < 8; ++i)
			lengthBytes[i] = Algorithm::kBigEndian ? static_cast<std::uint8_t>(bits >> (56 - 8 * i))
				: static_cast<std::uint8_t>(bits >> (8 * i));
		Update(lengthBytes, 8);
		return m_algorithm.Digest();
	}

private:
	Algorithm m_algorithm;
	std::uint8_t m_buffer[64] = {};
	std::size_t m_used = 0;
	std::uint64_t m_length = 0;
};

struct Sha1 {
	static constexpr bool kBigEndian = true;
	std::uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

	void Block(const std::uint8_t* block)
	{
		std::uint32_t w[80];
		for (int i = 0; i < 16; ++i)
			w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
				(std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
		for (int i = 16; i < 80; ++i)
			w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; ++i) {
			std::uint32_t f, k;
			if (i < 20) {
				f = (b & c) | (~b & d);
				k = 0x5A827999u;
			} else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1u;
			} else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDCu;
			} else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6u;
			}
			const std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = RotateLeft(b, 30);
			b = a;
			a = temp;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}

	std::string Digest() const
	{
		std::uint8_t out[20];
		for (int i = 0; i < 5; ++i)
			for (int j = 0; j < 4; ++j)
				out[i * 4 + j] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
		return ToHex(out, sizeof(out));
	}
};

struct Md5 {
	static constexpr bool kBigEndian = false;
	std::uint32_t h[4] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u };

	void Block(const std::uint8_t* block)
	{
		static const std::uint32_t k[64] = {
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
		};
		static const int shifts[64] = {
			7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
			5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
			4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
			6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
		};
		std::uint32_t m[16];
		for (int i = 0; i < 16; ++i)
			m[i] = std::uint32_t(block[i * 4]) | (std::uint32_t(block[i * 4 + 1]) << 8) |
				(std::uint32_t(block[i * 4 + 2]) << 16) | (std::uint32_t(block[i * 4 + 3]) << 24);

		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
		for (int i = 0; i < 64; ++i) {
			std::uint32_t f;
			int g;
			if (i < 16) {
				f = (b & c) | (~b & d);
				g = i;
			} else if (i < 32) {
				f = (d & b) | (~d & c);
				g = (5 * i + 1) % 16;
			} else if (i < 48) {
				f = b ^ c ^ d;
				g = (3 * i + 5) % 16;
			} else {
				f = c ^ (b | ~d);
				g = (7 * i) % 16;
			}
			const std::uint32_t temp = d;
			d = c;
			c = b;
			b = b + RotateLeft(a + f + k[i] + m[g], shifts[i]);
			a = temp;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
	}

	std::string Digest() const
	{
		std::uint8_t out[16];
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				out[i * 4 + j] = static_cast<std::uint8_t>(h[i] >> (8 * j));
		return ToHex(out, sizeof(out));
	}
};

template <typename Algorithm>
std::string HashFile(const std::string& path)
{
	std::ifstream file(LongPath(std::filesystem::path(path)), std::ios::binary);
	if (!file.is_open())
		return {};
	BlockHasher<Algorithm> hasher;
	std::vector<char> buffer(1 << 16);
	while (file) {
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize got = file.gcount();
		if (got > 0)
			hasher.Update(reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(got));
	}
	if (file.bad())
		return {};
	return hasher.Finish();
}

}

std::string Sha1HexOfFile(const std::string& path) { return HashFile<Sha1>(path); }
std::string Md5HexOfFile(const std::string& path) { return HashFile<Md5>(path); }
