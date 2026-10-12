// Writes the launcher logo as resources/logo.ico (the executable's icon) and resources/logo.png.
// Run from the repository root after changing src/UI/Logo.cpp:
//   build\Debug\logo_export.exe
#include "UI/Logo.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<std::uint8_t> EncodePng(int size)
{
	const std::vector<std::uint8_t> pixels = Logo::BlockPixels(size);
	std::vector<std::uint8_t> png;
	stbi_write_png_to_func([](void* context, void* data, int length) {
		auto* out = static_cast<std::vector<std::uint8_t>*>(context);
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		out->insert(out->end(), bytes, bytes + length);
	}, &png, size, size, 4, pixels.data(), size * 4);
	return png;
}

static void PutLe(std::vector<std::uint8_t>& out, std::uint32_t value, int bytes)
{
	for (int i = 0; i < bytes; ++i)
		out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
}

int main()
{
	// An .ico of PNG images (supported since Windows Vista).
	const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
	const int count = static_cast<int>(sizeof(sizes) / sizeof(sizes[0]));
	std::vector<std::vector<std::uint8_t>> images;
	for (int size : sizes)
		images.push_back(EncodePng(size));
	std::vector<std::uint8_t> ico;
	PutLe(ico, 0, 2);     // reserved
	PutLe(ico, 1, 2);     // type: icon
	PutLe(ico, count, 2);
	std::uint32_t offset = 6 + 16 * count;
	for (int i = 0; i < count; ++i) {
		ico.push_back(static_cast<std::uint8_t>(sizes[i] >= 256 ? 0 : sizes[i])); // 0 means 256
		ico.push_back(static_cast<std::uint8_t>(sizes[i] >= 256 ? 0 : sizes[i]));
		ico.push_back(0); // palette size
		ico.push_back(0); // reserved
		PutLe(ico, 1, 2);  // color planes
		PutLe(ico, 32, 2); // bits per pixel
		PutLe(ico, static_cast<std::uint32_t>(images[static_cast<std::size_t>(i)].size()), 4);
		PutLe(ico, offset, 4);
		offset += static_cast<std::uint32_t>(images[static_cast<std::size_t>(i)].size());
	}
	for (const auto& image : images)
		ico.insert(ico.end(), image.begin(), image.end());

	std::ofstream("resources/logo.ico", std::ios::binary).write(reinterpret_cast<const char*>(ico.data()),
		static_cast<std::streamsize>(ico.size()));
	const std::vector<std::uint8_t> png = images.back();
	std::ofstream("resources/logo.png", std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
		static_cast<std::streamsize>(png.size()));
	std::printf("Wrote resources/logo.ico (%zu bytes) and resources/logo.png\n", ico.size());
	return 0;
}
