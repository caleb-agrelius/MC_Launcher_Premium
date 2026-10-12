#include "Network/TracingHttpClient.h"

#include "Util/DebugTrace.h"

#include <chrono>
#include <filesystem>

namespace {

using Clock = std::chrono::steady_clock;

long long MillisSince(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void Trace(const std::string& line)
{
	Debug::Write(Debug::kHttp, line);
}

// The names of the extra headers, e.g. " [x-api-key]"; the values can be secrets, so they never show.
std::string HeaderNames(const std::vector<std::string>& headers)
{
	std::string names;
	for (const std::string& header : headers) {
		if (!names.empty())
			names += ", ";
		names += header.substr(0, header.find(':'));
	}
	return names.empty() ? std::string() : " [" + names + "]";
}

std::string Outcome(bool ok, const std::string* err)
{
	if (ok)
		return "ok";
	return "FAILED" + (err && !err->empty() ? ": " + *err : std::string());
}

// The first part of a response on one line, for the body channel.
std::string Preview(const std::string& body)
{
	constexpr std::size_t kMax = 400;
	std::string text;
	for (std::size_t i = 0; i < body.size() && text.size() < kMax; ++i) {
		const char c = body[i];
		if (c == '\n' || c == '\r' || c == '\t')
			text += ' ';
		else
			text += c;
	}
	if (body.size() > kMax)
		text += "... (" + std::to_string(body.size()) + " bytes)";
	return text;
}

}

bool TracingHttpClient::DownloadToFile(const std::string& url, const std::string& outPath,
	std::function<void(double, double)> progress, std::string* err, const std::atomic<bool>* cancel,
	const std::vector<std::string>& headers)
{
	if (!Debug::Enabled(Debug::kHttp))
		return m_inner.DownloadToFile(url, outPath, std::move(progress), err, cancel, headers);
	Trace("DOWNLOAD " + Debug::RedactUrl(url) + HeaderNames(headers) + " -> " + Debug::PathText(outPath));
	const auto start = Clock::now();
	std::string localError;
	const bool ok = m_inner.DownloadToFile(url, outPath, std::move(progress), err ? err : &localError, cancel, headers);
	std::string size;
	if (ok) {
		std::error_code ec;
		const auto bytes = std::filesystem::file_size(std::filesystem::u8path(outPath), ec);
		if (!ec)
			size = ", " + std::to_string(bytes) + " bytes";
	}
	Trace("  <- DOWNLOAD " + Outcome(ok, err ? err : &localError) + size + ", " + std::to_string(MillisSince(start)) + " ms  " +
		Debug::RedactUrl(url));
	return ok;
}

bool TracingHttpClient::Get(const std::string& url, std::string& body, std::string* err, const std::atomic<bool>* cancel,
	const std::vector<std::string>& headers)
{
	if (!Debug::Enabled(Debug::kHttp))
		return m_inner.Get(url, body, err, cancel, headers);
	Trace("GET  " + Debug::RedactUrl(url) + HeaderNames(headers));
	const auto start = Clock::now();
	std::string localError;
	const bool ok = m_inner.Get(url, body, err ? err : &localError, cancel, headers);
	Trace("  <- GET " + Outcome(ok, err ? err : &localError) + ", " + std::to_string(body.size()) + " bytes, " +
		std::to_string(MillisSince(start)) + " ms  " + Debug::RedactUrl(url));
	if (ok)
		Debug::Write(Debug::kHttpBody, Preview(body));
	return ok;
}

bool TracingHttpClient::Post(const std::string& url, const std::string& jsonBody, std::string& body, std::string* err,
	const std::atomic<bool>* cancel, const std::vector<std::string>& headers)
{
	if (!Debug::Enabled(Debug::kHttp))
		return m_inner.Post(url, jsonBody, body, err, cancel, headers);
	Trace("POST " + Debug::RedactUrl(url) + HeaderNames(headers) + " (" + std::to_string(jsonBody.size()) + " bytes sent)");
	const auto start = Clock::now();
	std::string localError;
	const bool ok = m_inner.Post(url, jsonBody, body, err ? err : &localError, cancel, headers);
	Trace("  <- POST " + Outcome(ok, err ? err : &localError) + ", " + std::to_string(body.size()) + " bytes, " +
		std::to_string(MillisSince(start)) + " ms  " + Debug::RedactUrl(url));
	return ok;
}

bool TracingHttpClient::Request(const std::string& url, const std::vector<std::string>& headers, const std::string& requestBody,
	std::string& responseBody, long& status, std::string* err, const std::atomic<bool>* cancel)
{
	if (!Debug::Enabled(Debug::kHttp))
		return m_inner.Request(url, headers, requestBody, responseBody, status, err, cancel);
	Trace(std::string(requestBody.empty() ? "GET  " : "POST ") + Debug::RedactUrl(url) + HeaderNames(headers) +
		(requestBody.empty() ? std::string() : " (" + std::to_string(requestBody.size()) + " bytes sent)"));
	const auto start = Clock::now();
	std::string localError;
	const bool ok = m_inner.Request(url, headers, requestBody, responseBody, status, err ? err : &localError, cancel);
	Trace("  <- " + (ok ? "HTTP " + std::to_string(status) : Outcome(false, err ? err : &localError)) + ", " +
		std::to_string(responseBody.size()) + " bytes, " + std::to_string(MillisSince(start)) + " ms  " + Debug::RedactUrl(url));
	return ok;
}
