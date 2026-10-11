#pragma once

#include <atomic>
#include <string>
#include <functional>

// Minimal HTTP client wrapper using libcurl.
// DownloadToFile downloads the given URL to outPath and optionally reports progress
// via a callback that receives (totalBytes, downloadedBytes). On error, returns false
// and optionally fills err with a description. If cancel is provided and becomes
// true, the transfer is aborted (the partial file is removed) and false is returned.
class HttpClient {
public:
	HttpClient();
	~HttpClient();

	bool DownloadToFile(const std::string& url,
						const std::string& outPath,
						std::function<void(double, double)> progress = nullptr,
						std::string* err = nullptr,
						const std::atomic<bool>* cancel = nullptr);
};
