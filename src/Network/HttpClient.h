#pragma once

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
class HttpClient {
public:
	static constexpr const char* kUserAgent = "MC_Launcher_Premium/0.1";

	HttpClient();
	~HttpClient();

	bool DownloadToFile(const std::string& url,
						const std::string& outPath,
						std::function<void(double, double)> progress = nullptr,
						std::string* err = nullptr,
						const std::atomic<bool>* cancel = nullptr);

	bool Get(const std::string& url,
			 std::string& body,
			 std::string* err = nullptr,
			 const std::atomic<bool>* cancel = nullptr);

	// Sends a request and returns the response body and HTTP status even for error
	// statuses (sign-in APIs explain failures in 4xx bodies). A non-empty body is
	// sent as a POST. Returns false only if no response arrived (network error, cancel).
	bool Request(const std::string& url,
				 const std::vector<std::string>& headers,
				 const std::string& requestBody,
				 std::string& responseBody,
				 long& status,
				 std::string* err = nullptr,
				 const std::atomic<bool>* cancel = nullptr);

	// Percent-encodes text for use in a URL query parameter.
	static std::string UrlEncode(const std::string& text);
};
