#pragma once

#include "Network/IHttpClient.h"

#include <atomic>
#include <string>
#include <functional>
#include <vector>

// Minimal HTTP client wrapper using libcurl. Every request sends kUserAgent
// (Modrinth asks API clients to identify themselves).
// DownloadToFile downloads the given URL to outPath and optionally reports progress
// via a callback that receives (totalBytes, downloadedBytes). On error, returns false
// and optionally fills err with a description. If cancel is provided and becomes
// true, the transfer is aborted (the partial file is removed) and false is returned.
// Get fetches the given URL into body, with the same error and cancel behavior.
// Post sends jsonBody as application/json and reads the response into body.
// headers are extra request headers such as "x-api-key: ...".
// Each thread reuses one curl handle, so repeated requests to a host keep the connection open.
// Every thread that made requests, other than the one destroying HttpClient, must exit first.
class HttpClient final : public IHttpClient {
public:
	static constexpr const char* kUserAgent = "MC_Launcher_Premium/0.1";

	HttpClient();
	~HttpClient();

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

	// Sends a request and returns the response body and HTTP status even for error
	// statuses (sign-in APIs explain failures in 4xx bodies). A non-empty body is
	// sent as a POST. Returns false only if no response arrived (network error, cancel).
	bool Request(const std::string& url,
				 const std::vector<std::string>& headers,
				 const std::string& requestBody,
				 std::string& responseBody,
				 long& status,
				 std::string* err = nullptr,
				 const std::atomic<bool>* cancel = nullptr) override;

	// Frees the calling thread's curl handle now instead of at thread exit. Worker threads
	// call this before reporting they're done, so joining them never waits on curl cleanup.
	static void ReleaseThreadHandle();

};
