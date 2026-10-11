#pragma once

#include <string>
#include <functional>

// Minimal HTTP client wrapper using libcurl.
// DownloadToFile downloads the given URL to outPath and optionally reports progress
// via a callback that receives (totalBytes, downloadedBytes). On error, returns false
// and optionally fills err with a description.
class HttpClient {
public:
	HttpClient();
	~HttpClient();

	bool DownloadToFile(const std::string& url,
						const std::string& outPath,
						std::function<void(double, double)> progress = nullptr,
						std::string* err = nullptr);
};
