#include "Text.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <regex>
#include <sstream>
#include <vector>

static std::string Lowercase(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

static void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
	for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
		text.replace(at, from.size(), to);
}

static std::string Utf8(unsigned long code)
{
	std::string out;
	if (code < 0x80) {
		out += static_cast<char>(code);
	} else if (code < 0x800) {
		out += static_cast<char>(0xC0 | (code >> 6));
		out += static_cast<char>(0x80 | (code & 0x3F));
	} else if (code < 0x10000) {
		out += static_cast<char>(0xE0 | (code >> 12));
		out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (code & 0x3F));
	} else if (code < 0x110000) {
		out += static_cast<char>(0xF0 | (code >> 18));
		out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (code & 0x3F));
	}
	return out;
}

// HTML tags become line breaks or nothing; entities are decoded.
static std::string StripHtml(const std::string& text)
{
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size();) {
		if (text[i] == '<') {
			const std::size_t end = text.find('>', i);
			if (end == std::string::npos) {
				out += text.substr(i);
				break;
			}
			std::string tag = Lowercase(text.substr(i + 1, end - i - 1));
			const bool closing = !tag.empty() && tag[0] == '/';
			std::string name;
			for (std::size_t c = closing ? 1 : 0; c < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[c])) || tag[c] == '!'); ++c)
				name += tag[c];
			// Skip the contents of script and style elements.
			if (!closing && (name == "script" || name == "style")) {
				const std::size_t close = Lowercase(text).find("</" + name, end);
				i = close == std::string::npos ? text.size() : text.find('>', close) + 1;
				continue;
			}
			if (name == "br")
				out += "\n";
			else if (name == "li" && !closing)
				out += "\n- ";
			else if (name == "p" || name == "div" || name == "hr" || name == "ul" || name == "ol" || name == "tr" ||
				(name.size() == 2 && name[0] == 'h' && std::isdigit(static_cast<unsigned char>(name[1]))))
				out += "\n\n";
			else if (name == "td" || name == "th")
				out += "  ";
			i = end + 1;
			continue;
		}
		if (text[i] == '&') {
			const std::size_t end = text.find(';', i);
			if (end != std::string::npos && end - i <= 10) {
				const std::string entity = text.substr(i + 1, end - i - 1);
				std::string decoded;
				if (entity == "amp") decoded = "&";
				else if (entity == "lt") decoded = "<";
				else if (entity == "gt") decoded = ">";
				else if (entity == "quot") decoded = "\"";
				else if (entity == "apos") decoded = "'";
				else if (entity == "nbsp") decoded = " ";
				else if (entity == "rsquo" || entity == "lsquo") decoded = "'";
				else if (entity == "rdquo" || entity == "ldquo") decoded = "\"";
				else if (entity == "ndash" || entity == "mdash") decoded = "-";
				else if (entity == "hellip") decoded = "...";
				else if (entity.size() > 1 && entity[0] == '#') {
					const bool hex = entity[1] == 'x' || entity[1] == 'X';
					char* parsedEnd = nullptr;
					const unsigned long code = std::strtoul(entity.c_str() + (hex ? 2 : 1), &parsedEnd, hex ? 16 : 10);
					if (parsedEnd && *parsedEnd == '\0' && code > 0)
						decoded = Utf8(code);
				}
				if (!decoded.empty()) {
					out += decoded;
					i = end + 1;
					continue;
				}
			}
		}
		out += text[i++];
	}
	return out;
}

std::string MarkupToPlainText(const std::string& input)
{
	std::string text = input;
	ReplaceAll(text, "\r\n", "\n");
	text = StripHtml(text);

	// Inline Markdown. Images go first, so a linked image ([![alt](img)](url)) leaves an empty link.
	static const std::regex image(R"(!\[[^\]]*\]\([^)]*\))");
	static const std::regex link(R"(\[([^\]]*)\]\([^)]*\))");
	static const std::regex referenceLink(R"(\[([^\]]+)\]\[[^\]]*\])");
	static const std::regex bold(R"((\*\*|__)(.+?)\1)");
	static const std::regex italic(R"(\*([^*\n]+)\*)");
	static const std::regex strike(R"(~~(.+?)~~)");
	static const std::regex code("`+([^`]*)`+");
	text = std::regex_replace(text, image, "");
	text = std::regex_replace(text, link, "$1");
	text = std::regex_replace(text, referenceLink, "$1");
	text = std::regex_replace(text, bold, "$2");
	text = std::regex_replace(text, strike, "$1");
	text = std::regex_replace(text, code, "$1");

	// Line-level Markdown, then collapse blank lines.
	static const std::regex heading(R"(^\s{0,3}#{1,6}\s*)");
	static const std::regex bullet(R"(^(\s*)[-*+]\s+)");
	static const std::regex quote(R"(^\s*>\s?)");
	static const std::regex rule(R"(^\s*([-*_]\s*){3,}$)");
	static const std::regex tableRule(R"(^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$)");
	static const std::regex referenceTarget(R"(^\s{0,3}\[[^\]]+\]:\s*\S+.*$)");
	std::istringstream lines(text);
	std::string line, out;
	int blank = 0;
	while (std::getline(lines, line)) {
		// Table header separators and link definitions vanish; rules become paragraph breaks.
		if ((line.find('|') != std::string::npos && std::regex_match(line, tableRule)) || std::regex_match(line, referenceTarget))
			continue;
		if (std::regex_match(line, rule))
			line.clear();
		line = std::regex_replace(line, heading, "");
		line = std::regex_replace(line, quote, "");
		line = std::regex_replace(line, bullet, "$1\xC2\xB7 ");
		line = std::regex_replace(line, italic, "$1");
		if (line.find('|') != std::string::npos && line.find_first_not_of(" |") != std::string::npos) {
			// A table row: its cells, trimmed, separated by three spaces.
			std::string row, cell;
			std::istringstream cells(line);
			while (std::getline(cells, cell, '|')) {
				const std::size_t first = cell.find_first_not_of(" \t");
				if (first == std::string::npos)
					continue;
				cell = cell.substr(first, cell.find_last_not_of(" \t") - first + 1);
				row += (row.empty() ? "" : "   ") + cell;
			}
			line = row;
		}
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
			line.pop_back();
		const bool isBlank = line.find_first_not_of(" \t") == std::string::npos;
		if (isBlank) {
			++blank;
			continue;
		}
		if (!out.empty())
			out += blank > 0 ? "\n\n" : "\n";
		blank = 0;
		out += line;
	}

	// Typography the UI font doesn't have.
	ReplaceAll(out, "\xE2\x80\x98", "'");
	ReplaceAll(out, "\xE2\x80\x99", "'");
	ReplaceAll(out, "\xE2\x80\x9C", "\"");
	ReplaceAll(out, "\xE2\x80\x9D", "\"");
	ReplaceAll(out, "\xE2\x80\x93", "-");
	ReplaceAll(out, "\xE2\x80\x94", "-");
	ReplaceAll(out, "\xE2\x80\xA6", "...");
	ReplaceAll(out, "\xE2\x80\xA2", "\xC2\xB7");
	return out;
}

std::string ShortenText(const std::string& text, std::size_t maxLength)
{
	std::string flat = text;
	for (char& c : flat) {
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
	}
	if (flat.size() <= maxLength)
		return flat;
	std::size_t cut = flat.rfind(' ', maxLength);
	if (cut == std::string::npos || cut < maxLength / 2)
		cut = maxLength;
	// Don't split a UTF-8 sequence.
	while (cut > 0 && (static_cast<unsigned char>(flat[cut]) & 0xC0) == 0x80)
		--cut;
	flat.resize(cut);
	while (!flat.empty() && (flat.back() == ' ' || flat.back() == ',' || flat.back() == '.'))
		flat.pop_back();
	return flat + "...";
}

std::string FormatCount(std::uint64_t count)
{
	char formatted[32];
	if (count >= 1000000)
		std::snprintf(formatted, sizeof(formatted), "%.1fM", count / 1000000.0);
	else if (count >= 1000)
		std::snprintf(formatted, sizeof(formatted), "%.1fK", count / 1000.0);
	else
		std::snprintf(formatted, sizeof(formatted), "%llu", static_cast<unsigned long long>(count));
	return formatted;
}

std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

bool ContainsIgnoreCase(const std::string& haystack, const std::string& lowerNeedle)
{
	return ToLower(haystack).find(lowerNeedle) != std::string::npos;
}

std::string FormatFileSize(std::uint64_t bytes)
{
	const char* units[] = { "B", "KB", "MB", "GB" };
	double size = static_cast<double>(bytes);
	int unit = 0;
	while (size >= 1024.0 && unit < 3) {
		size /= 1024.0;
		++unit;
	}
	char formatted[32];
	std::snprintf(formatted, sizeof(formatted), unit == 0 ? "%.0f %s" : "%.1f %s", size, units[unit]);
	return formatted;
}

std::string FormatPlaytime(std::int64_t seconds)
{
	if (seconds <= 0)
		return "Not played yet";
	const std::int64_t minutes = seconds / 60;
	if (minutes < 1)
		return "Under a minute";
	if (minutes < 60)
		return std::to_string(minutes) + " min";
	const std::int64_t hours = minutes / 60;
	if (hours >= 100)
		return std::to_string(hours) + " h";
	char text[32];
	std::snprintf(text, sizeof(text), "%lld h %02lld min", static_cast<long long>(hours), static_cast<long long>(minutes % 60));
	return text;
}

std::string FormatFileTime(std::filesystem::file_time_type time)
{
	namespace fs = std::filesystem;
	const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
	const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &seconds);
#else
	localtime_r(&seconds, &local);
#endif
	char text[32];
	std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
	return text;
}
