#include "UI/Pages/Manage/LogReader.h"

#include "Util/Text.h"

#include <miniz/miniz.h>

#include <cstdint>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxLogBytes = 24ull * 1024 * 1024; // longer logs show their end

}

namespace LogReader {

bool ReadText(const fs::path& path, std::string& out, std::string* err)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		if (err) *err = "Couldn't open " + path.filename().u8string();
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::uint64_t size = static_cast<std::uint64_t>(in.tellg());
	const bool gzip = ToLower(path.extension().u8string()) == ".gz";
	const std::uint64_t start = !gzip && size > kMaxLogBytes ? size - kMaxLogBytes : 0;
	in.seekg(static_cast<std::streamoff>(start));
	std::string data(std::istreambuf_iterator<char>(in), {});
	if (!gzip) {
		out = std::move(data);
		return true;
	}
	// gzip: a 10-byte header, optional fields, then raw deflate data and an 8-byte trailer.
	if (data.size() < 18 || static_cast<unsigned char>(data[0]) != 0x1F || static_cast<unsigned char>(data[1]) != 0x8B) {
		if (err) *err = "Not a gzip file";
		return false;
	}
	const unsigned char flags = static_cast<unsigned char>(data[3]);
	std::size_t pos = 10;
	if (flags & 4)
		pos += 2 + (static_cast<unsigned char>(data[pos]) | (static_cast<unsigned char>(data[pos + 1]) << 8));
	if (flags & 8)
		pos = data.find('\0', pos) + 1;
	if (flags & 16)
		pos = data.find('\0', pos) + 1;
	if (flags & 2)
		pos += 2;
	if (pos == 0 || pos >= data.size() - 8) {
		if (err) *err = "Damaged gzip file";
		return false;
	}
	std::size_t length = 0;
	void* inflated = tinfl_decompress_mem_to_heap(data.data() + pos, data.size() - 8 - pos, &length, 0);
	if (!inflated) {
		if (err) *err = "Couldn't unpack " + path.filename().u8string();
		return false;
	}
	out.assign(static_cast<const char*>(inflated), length);
	mz_free(inflated);
	if (out.size() > kMaxLogBytes)
		out.erase(0, out.size() - kMaxLogBytes);
	return true;
}

std::vector<std::string> SplitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		// Tabs (stack traces) don't render in ImGui fonts.
		for (std::size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', tab))
			line.replace(tab, 1, "    ");
		lines.push_back(std::move(line));
		start = end + 1;
	}
	return lines;
}

}
