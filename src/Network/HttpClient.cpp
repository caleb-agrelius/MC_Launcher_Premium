#include "HttpClient.h"
#include <curl/curl.h>
#include <cstdio>
#include <memory>
#include <utility>

struct ProgressData {
	std::function<void(double,double)> callback;
	const std::atomic<bool>* cancel = nullptr;
};

static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream)
{
	FILE* f = static_cast<FILE*>(stream);
	return fwrite(ptr, size, nmemb, f);
}

static size_t write_string(void* ptr, size_t size, size_t nmemb, void* userdata)
{
	std::string* body = static_cast<std::string*>(userdata);
	body->append(static_cast<const char*>(ptr), size * nmemb);
	return size * nmemb;
}

static int xferinfo(void* p,
					curl_off_t dltotal,
					curl_off_t dlnow,
					curl_off_t /*ultotal*/,
					curl_off_t /*ulnow*/)
{
	ProgressData* pd = static_cast<ProgressData*>(p);
	if (pd && pd->cancel && pd->cancel->load())
		return 1; // abort the transfer
	if (pd && pd->callback) {
		double total = static_cast<double>(dltotal);
		double now = static_cast<double>(dlnow);
		pd->callback(total, now);
	}
	return 0; // return non-zero to abort
}

// Options shared by every request: redirects, HTTP errors as failures,
// user agent, and the progress/cancel callback.
static void ApplyCommonOptions(CURL* curl, const std::string& url, ProgressData* pd)
{
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, HttpClient::kUserAgent);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);

	// progress
#if LIBCURL_VERSION_NUM >= 0x072000
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, pd);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#else
	// older libcurl may not have xferinfo; ignore progress
	(void)pd;
#endif
}

HttpClient::HttpClient()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient()
{
	curl_global_cleanup();
}

bool HttpClient::DownloadToFile(const std::string& url,
								const std::string& outPath,
								std::function<void(double, double)> progress,
								std::string* err,
								const std::atomic<bool>* cancel)
{
	CURL* curl = curl_easy_init();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	FILE* fp = nullptr;
#ifdef _WIN32
	if (fopen_s(&fp, outPath.c_str(), "wb") != 0) {
		fp = nullptr;
	}
#else
	fp = fopen(outPath.c_str(), "wb");
#endif
	if (!fp) {
		if (err) *err = "Failed to open output file";
		curl_easy_cleanup(curl);
		return false;
	}

	ProgressData pd{progress, cancel};
	ApplyCommonOptions(curl, url, &pd);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (err) *err = (cancel && cancel->load()) ? "Cancelled" : curl_easy_strerror(res);
		fclose(fp);
		curl_easy_cleanup(curl);
		// remove partial file
		remove(outPath.c_str());
		return false;
	}

	fclose(fp);
	curl_easy_cleanup(curl);
	return true;
}

bool HttpClient::Get(const std::string& url,
					 std::string& body,
					 std::string* err,
					 const std::atomic<bool>* cancel)
{
	body.clear();
	CURL* curl = curl_easy_init();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	ProgressData pd{nullptr, cancel};
	ApplyCommonOptions(curl, url, &pd);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		if (err) *err = (cancel && cancel->load()) ? "Cancelled" : curl_easy_strerror(res);
		body.clear();
		return false;
	}
	return true;
}

std::string HttpClient::UrlEncode(const std::string& text)
{
	char* escaped = curl_easy_escape(nullptr, text.c_str(), static_cast<int>(text.size()));
	if (!escaped)
		return {};
	std::string result(escaped);
	curl_free(escaped);
	return result;
}
