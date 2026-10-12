#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// What the pack sources, installers and pages need from an HTTP client. HttpClient (libcurl)
// is the real one; depending on this interface lets those callers run against a fake in tests
// and keeps libcurl out of their headers.
// DownloadToFile downloads the given URL to outPath and optionally reports progress
// via a callback that receives (totalBytes, downloadedBytes). On error, returns false
// and optionally fills err with a description. If cancel is provided and becomes
// true, the transfer is aborted (the partial file is removed) and false is returned.
// Get fetches the given URL into body, with the same error and cancel behavior.
// Post sends jsonBody as application/json and reads the response into body.
// headers are extra request headers such as "x-api-key: ...".
class IHttpClient {
public:
	virtual ~IHttpClient() = default;

	virtual bool DownloadToFile(const std::string& url,
								const std::string& outPath,
								std::function<void(double, double)> progress = nullptr,
								std::string* err = nullptr,
								const std::atomic<bool>* cancel = nullptr,
								const std::vector<std::string>& headers = {}) = 0;

	virtual bool Get(const std::string& url,
					 std::string& body,
					 std::string* err = nullptr,
					 const std::atomic<bool>* cancel = nullptr,
					 const std::vector<std::string>& headers = {}) = 0;

	virtual bool Post(const std::string& url,
					  const std::string& jsonBody,
					  std::string& body,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr,
					  const std::vector<std::string>& headers = {}) = 0;

	// Sends a request and returns the response body and HTTP status even for error
	// statuses (sign-in APIs explain failures in 4xx bodies). A non-empty body is
	// sent as a POST. Returns false only if no response arrived (network error, cancel).
	virtual bool Request(const std::string& url,
						 const std::vector<std::string>& headers,
						 const std::string& requestBody,
						 std::string& responseBody,
						 long& status,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr) = 0;

	// Percent-encodes text for use in a URL query parameter.
	static std::string UrlEncode(const std::string& text);
};
