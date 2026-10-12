#include "Logo.h"

#include <algorithm>
#include <cmath>

namespace Logo {

namespace {

struct Rgb {
	int r, g, b;
};

// Deterministic per-texel noise, so the block looks the same every time.
unsigned Hash(int x, int y, int salt)
{
	unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u +
		static_cast<unsigned>(salt) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

constexpr Rgb kGrass[] = { { 89, 166, 54 }, { 106, 184, 64 }, { 76, 145, 46 }, { 120, 196, 74 } };
constexpr Rgb kDirt[] = { { 134, 96, 67 }, { 121, 85, 58 }, { 150, 108, 74 }, { 107, 75, 51 } };

// A 16x16 face texture: the grass top, or a dirt side with grass hanging over its top edge.
Rgb Texel(bool top, int u, int v, int salt)
{
	const unsigned noise = Hash(u, v, salt);
	if (top)
		return kGrass[noise % 4];
	// Grass overhang: 3 rows, plus a ragged fringe that reaches 1-2 rows further down.
	const int fringe = 3 + static_cast<int>(Hash(u, 0, salt + 7) % 3);
	if (v < fringe)
		return kGrass[noise % 4];
	return kDirt[noise % 4];
}

Rgb Shade(Rgb c, float f)
{
	return { static_cast<int>(c.r * f), static_cast<int>(c.g * f), static_cast<int>(c.b * f) };
}

// The block at kBaseSize, with a dark outline around its silhouette.
std::vector<std::uint8_t> RenderBase()
{
	const int n = kBaseSize;
	// One pixel of room on every side for the outline.
	const float half = (n - 2) * 0.5f;     // the top face's center x
	const float quarter = (n - 2) * 0.25f; // the top face's half height
	std::vector<std::uint8_t> px(static_cast<std::size_t>(n * n * 4), 0);
	auto put = [&](int x, int y, Rgb c) {
		std::uint8_t* p = &px[static_cast<std::size_t>((y * n + x) * 4)];
		p[0] = static_cast<std::uint8_t>(std::clamp(c.r, 0, 255));
		p[1] = static_cast<std::uint8_t>(std::clamp(c.g, 0, 255));
		p[2] = static_cast<std::uint8_t>(std::clamp(c.b, 0, 255));
		p[3] = 255;
	};
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < n; ++x) {
			const float fx = x - 0.5f;
			const float fy = y - 0.5f;
			// Top face: (half, 0) + u * (half, quarter) + v * (-half, quarter).
			const float a = fy / quarter;
			const float b = (fx - half) / half;
			const float tu = (a + b) * 0.5f;
			const float tv = (a - b) * 0.5f;
			if (tu >= 0.0f && tu < 1.0f && tv >= 0.0f && tv < 1.0f) {
				const Rgb c = Texel(true, static_cast<int>(tu * 16.0f), static_cast<int>(tv * 16.0f), 1);
				put(x, y, c);
				continue;
			}
			if (fx < half) {
				// Left face: (0, quarter) + u * (half, quarter) + v * (0, half).
				const float u = fx / half;
				const float v = (fy - quarter - quarter * u) / half;
				if (v >= 0.0f && v < 1.0f)
					put(x, y, Shade(Texel(false, static_cast<int>(u * 16.0f), static_cast<int>(v * 16.0f), 2), 0.82f));
			} else {
				// Right face: (half, half) + u * (half, -quarter) + v * (0, half).
				const float u = (fx - half) / half;
				const float v = (fy - half + quarter * u) / half;
				if (v >= 0.0f && v < 1.0f)
					put(x, y, Shade(Texel(false, static_cast<int>(u * 16.0f), static_cast<int>(v * 16.0f), 3), 0.62f));
			}
		}
	}
	// Outline: every transparent pixel touching the block becomes a dark edge, so the
	// block stays readable on any taskbar or title bar color.
	std::vector<std::uint8_t> out = px;
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < n; ++x) {
			if (px[static_cast<std::size_t>((y * n + x) * 4 + 3)] != 0)
				continue;
			bool edge = false;
			for (const auto [dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
				const int nx = x + dx;
				const int ny = y + dy;
				if (nx >= 0 && ny >= 0 && nx < n && ny < n && px[static_cast<std::size_t>((ny * n + nx) * 4 + 3)] != 0)
					edge = true;
			}
			if (edge) {
				std::uint8_t* p = &out[static_cast<std::size_t>((y * n + x) * 4)];
				p[0] = 28;
				p[1] = 36;
				p[2] = 22;
				p[3] = 255;
			}
		}
	}
	return out;
}

}

std::vector<std::uint8_t> BlockPixels(int size)
{
	size = std::max(1, size);
	const std::vector<std::uint8_t> base = RenderBase();
	std::vector<std::uint8_t> out(static_cast<std::size_t>(size * size * 4), 0);
	if (size < kBaseSize) {
		// Box filter, weighting colors by alpha so the outline doesn't bleed into transparency.
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				const int x0 = x * kBaseSize / size, x1 = std::max(x0 + 1, (x + 1) * kBaseSize / size);
				const int y0 = y * kBaseSize / size, y1 = std::max(y0 + 1, (y + 1) * kBaseSize / size);
				double sum[4] = {};
				int count = 0;
				for (int sy = y0; sy < y1; ++sy) {
					for (int sx = x0; sx < x1; ++sx) {
						const std::uint8_t* p = &base[static_cast<std::size_t>((sy * kBaseSize + sx) * 4)];
						const double alpha = p[3] / 255.0;
						for (int c = 0; c < 3; ++c)
							sum[c] += p[c] * alpha;
						sum[3] += alpha;
						++count;
					}
				}
				std::uint8_t* o = &out[static_cast<std::size_t>((y * size + x) * 4)];
				if (sum[3] > 0.0) {
					for (int c = 0; c < 3; ++c)
						o[c] = static_cast<std::uint8_t>(std::lround(sum[c] / sum[3]));
				}
				o[3] = static_cast<std::uint8_t>(std::lround(255.0 * sum[3] / count));
			}
		}
		return out;
	}
	const int scale = size / kBaseSize;
	const int offset = (size - scale * kBaseSize) / 2;
	for (int y = 0; y < kBaseSize * scale; ++y) {
		for (int x = 0; x < kBaseSize * scale; ++x) {
			const std::uint8_t* p = &base[static_cast<std::size_t>(((y / scale) * kBaseSize + x / scale) * 4)];
			std::copy(p, p + 4, &out[static_cast<std::size_t>(((y + offset) * size + x + offset) * 4)]);
		}
	}
	return out;
}

std::array<std::uint8_t, 7> Glyph(char c)
{
	switch (c) {
	case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
	case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
	case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
	case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
	case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
	case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
	case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
	case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
	case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
	case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
	case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
	default: return {};
	}
}

}
