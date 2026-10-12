#pragma once

#include "Network/IHttpClient.h"

// Wraps another IHttpClient and traces every request on the http channel of the debug harness
// (Util/DebugTrace.h): method, URL (secret query values hidden), the names of the extra headers,
// then the outcome, size and time. With the body channel on it also shows the start of each GET
// response. Behaves exactly like the client it wraps, and costs a channel check per call when
// tracing is off. Each call traces on the thread that makes it.
class TracingHttpClient final : public IHttpClient {
public:
	explicit TracingHttpClient(IHttpClient& inner) : m_inner(inner) {}

	bool DownloadToFile(const std::string& url,
						const std::string& outPath,
						std::function<void(double, double)> progress = nullptr,
						std::string* err = nullptr,
						const std::atomic<bool>* cancel = nullptr,
						const std::vector<std::string>& headers = {}) override;

	bool Get(const std::string& url,
			 std::string& body,
			 std::string* err = nullptr,
			 const std::atomic<bool>* cancel = nullptr,
			 const std::vector<std::string>& headers = {}) override;

	bool Post(const std::string& url,
			  const std::string& jsonBody,
			  std::string& body,
			  std::string* err = nullptr,
			  const std::atomic<bool>* cancel = nullptr,
			  const std::vector<std::string>& headers = {}) override;

	bool Request(const std::string& url,
				 const std::vector<std::string>& headers,
				 const std::string& requestBody,
				 std::string& responseBody,
				 long& status,
				 std::string* err = nullptr,
				 const std::atomic<bool>* cancel = nullptr) override;

private:
	IHttpClient& m_inner;
};
