#include "Util/DebugTrace.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>

namespace Debug {

namespace {

std::atomic<unsigned> g_mask{ 0 };
std::mutex g_writeMutex;
std::ostream* g_out = nullptr; // guarded by g_writeMutex; null means std::cerr

struct ChannelName {
	const char* name;
	unsigned bits;
	const char* tag; // fixed width, for the trace columns
};
constexpr ChannelName kNames[] = {
	{ "http", kHttp, "HTTP " },
	{ "body", kHttpBody, "BODY " },
	{ "file", kFile, "FILE " },
	{ "process", kProcess, "PROC " },
	{ "log", kLog, "LOG  " },
	{ "perf", kPerf, "PERF " },
};

std::string Lowercase(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string Trim(const std::string& text)
{
	const auto first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// Small per-thread numbers (t0 is whichever thread traces first, normally the UI thread)
// read better than 5-digit thread ids.
int ThreadNumber()
{
	static std::atomic<int> next{ 0 };
	thread_local const int number = next++;
	return number;
}

std::string Clock()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const std::time_t seconds = system_clock::to_time_t(now);
	const auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &seconds);
#else
	localtime_r(&seconds, &local);
#endif
	char text[24];
	std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(millis));
	return text;
}

bool SecretName(const std::string& name)
{
	const std::string lower = Lowercase(name);
	for (const char* word : { "key", "token", "secret", "code", "password", "passwd" }) {
		if (lower.find(word) != std::string::npos)
			return true;
	}
	return false;
}

}

bool ParseChannels(const std::string& text, unsigned& mask, std::string* unknown)
{
	bool ok = true;
	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t end = text.find(',', start);
		if (end == std::string::npos)
			end = text.size();
		const std::string name = Lowercase(Trim(text.substr(start, end - start)));
		start = end + 1;
		if (name.empty())
			continue;
		if (name == "all" || name == "1" || name == "true" || name == "on") {
			mask |= kAll;
			continue;
		}
		const auto found = std::find_if(std::begin(kNames), std::end(kNames), [&](const ChannelName& entry) { return name == entry.name; });
		if (found != std::end(kNames)) {
			mask |= found->bits;
			continue;
		}
		ok = false;
		if (unknown) {
			if (!unknown->empty())
				*unknown += ", ";
			*unknown += name;
		}
	}
	return ok;
}

unsigned ConfigureFromEnvironment(int argc, char** argv, std::vector<std::string>& warnings)
{
	unsigned mask = 0;
	std::string unknown;
	auto apply = [&](const std::string& list, const char* source) {
		unknown.clear();
		if (list.empty())
			mask |= kAll;
		else if (!ParseChannels(list, mask, &unknown))
			warnings.push_back(std::string("Unknown debug channel(s) in ") + source + ": " + unknown +
				" (use http, body, file, process, log or all)");
	};
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	if (const char* env = std::getenv("MCLP_DEBUG"))
		apply(env, "MCLP_DEBUG");
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--debug")
			mask |= kAll;
		else if (arg.rfind("--debug=", 0) == 0)
			apply(arg.substr(8), "--debug");
	}
	Configure(mask);
	return mask;
}

bool OfflineRequested(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == "--offline")
			return true;
	}
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	const char* env = std::getenv("MCLP_OFFLINE");
	if (!env)
		return false;
	const std::string value = env;
	return !(value.empty() || value == "0" || value == "false");
}

void Configure(unsigned mask, std::ostream* out)
{
	{
		std::lock_guard<std::mutex> lock(g_writeMutex);
		g_out = out;
	}
	g_mask.store(mask);
}

bool Enabled(Channel channel)
{
	return (g_mask.load(std::memory_order_relaxed) & channel) != 0;
}

std::string ChannelNames(unsigned mask)
{
	std::string names;
	for (const ChannelName& entry : kNames) {
		if (!(mask & entry.bits))
			continue;
		if (!names.empty())
			names += ",";
		names += entry.name;
	}
	return names;
}

void Write(Channel channel, const std::string& message)
{
	if (!Enabled(channel))
		return;
	const char* tag = "?    ";
	for (const ChannelName& entry : kNames) {
		if (entry.bits == channel)
			tag = entry.tag;
	}
	const std::string line = Clock() + " [" + tag + "] t" + std::to_string(ThreadNumber()) + "  " + message + "\n";
	std::lock_guard<std::mutex> lock(g_writeMutex);
	std::ostream& out = g_out ? *g_out : std::cerr;
	out << line;
	out.flush();
}

std::string RedactUrl(const std::string& url)
{
	const auto query = url.find('?');
	if (query == std::string::npos)
		return url;
	std::string result = url.substr(0, query + 1);
	std::size_t start = query + 1;
	while (start <= url.size()) {
		std::size_t end = url.find('&', start);
		if (end == std::string::npos)
			end = url.size();
		const std::string pair = url.substr(start, end - start);
		const auto equals = pair.find('=');
		if (equals != std::string::npos && SecretName(pair.substr(0, equals)))
			result += pair.substr(0, equals + 1) + "***";
		else
			result += pair;
		if (end < url.size())
			result += '&';
		start = end + 1;
	}
	return result;
}

std::string DescribeCommand(const std::vector<std::string>& args)
{
	std::string text;
	bool hideNext = false;
	for (const std::string& arg : args) {
		std::string shown = arg;
		if (hideNext) {
			shown = "***";
			hideNext = false;
		} else if (arg.rfind("--", 0) == 0 && SecretName(arg)) {
			hideNext = true;
		}
		if (!text.empty())
			text += ' ';
		text += shown.find(' ') == std::string::npos ? shown : "\"" + shown + "\"";
	}
	return text;
}

std::string PathText(const std::string& path)
{
	for (const char* prefix : { "\\\\?\\UNC\\", "\\\\?\\" }) {
		const std::string text = prefix;
		if (path.rfind(text, 0) == 0)
			return (text == "\\\\?\\UNC\\" ? "\\\\" : "") + path.substr(text.size());
	}
	return path;
}

LogTee::int_type LogTee::overflow(int_type ch)
{
	if (traits_type::eq_int_type(ch, traits_type::eof()))
		return traits_type::not_eof(ch);
	Feed(traits_type::to_char_type(ch));
	return m_target->sputc(traits_type::to_char_type(ch));
}

std::streamsize LogTee::xsputn(const char* text, std::streamsize count)
{
	for (std::streamsize i = 0; i < count; ++i)
		Feed(text[i]);
	return m_target->sputn(text, count);
}

int LogTee::sync()
{
	return m_target->pubsync();
}

void LogTee::Feed(char ch)
{
	if (ch != '\n') {
		if (ch != '\r')
			m_line += ch;
		return;
	}
	Write(kLog, m_line);
	m_line.clear();
}

}
