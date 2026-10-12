#include "HttpClient.h"
#include "Util/Paths.h"
#include <curl/curl.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

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

// One curl handle per thread, reset before each request. Reusing the handle keeps
// connections alive, which matters when a pack install fetches thousands of files.
// A thread's handle is freed when the thread exits; the thread that destroys
// HttpClient frees its own first, since that happens before thread exit.
struct ThreadHandle {
	CURL* curl = nullptr;
	~ThreadHandle() { Release(); }
	void Release()
	{
		if (curl)
			curl_easy_cleanup(curl);
		curl = nullptr;
	}
};

static ThreadHandle& CurrentThreadHandle()
{
	thread_local ThreadHandle handle;
	return handle;
}

static CURL* AcquireHandle()
{
	ThreadHandle& handle = CurrentThreadHandle();
	if (!handle.curl)
		handle.curl = curl_easy_init();
	else
		curl_easy_reset(handle.curl);
	return handle.curl;
}

// Frees a header list when the request finishes.
struct HeaderList {
	curl_slist* list = nullptr;
	~HeaderList()
	{
		if (list)
			curl_slist_free_all(list);
	}
	void Add(const std::string& header) { list = curl_slist_append(list, header.c_str()); }
};

// Percent-encodes characters that aren't legal in a URL (spaces, quotes, braces, non-ASCII
// bytes...). Some pack manifests list file URLs with raw spaces, which curl rejects.
// Existing %XX escapes and reserved characters (/?#&=:@[] etc.) are left alone.
static std::string EscapeUnsafeUrlChars(const std::string& url)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(url.size());
	for (const char ch : url) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c <= 0x20 || c >= 0x7F || ch == '"' || ch == '<' || ch == '>' || ch == '\\' || ch == '^' ||
			ch == '`' || ch == '{' || ch == '|' || ch == '}') {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		} else {
			out += ch;
		}
	}
	return out;
}

// Options shared by every request: redirects, HTTP errors as failures,
// user agent, extra headers, and the progress/cancel callback. curl copies the URL string.
static void ApplyCommonOptions(CURL* curl, const std::string& url, ProgressData* pd, HeaderList& headers)
{
	curl_easy_setopt(curl, CURLOPT_URL, EscapeUnsafeUrlChars(url).c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, HttpClient::kUserAgent);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	// Give up on a transfer that stalls (under 1 byte/s for 60 s) instead of waiting forever.
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	if (headers.list)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.list);

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

// Describes a failed request: "Cancelled", or curl's message plus the HTTP status if there was one.
static std::string DescribeError(CURL* curl, CURLcode res, const std::atomic<bool>* cancel)
{
	if (cancel && cancel->load())
		return "Cancelled";
	std::string message = curl_easy_strerror(res);
	long status = 0;
	if (res == CURLE_HTTP_RETURNED_ERROR && curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status > 0)
		message = "HTTP " + std::to_string(status);
	return message;
}

HttpClient::HttpClient()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient()
{
	// Other threads that made requests must have exited by now (see the header).
	CurrentThreadHandle().Release();
	curl_global_cleanup();
}

bool HttpClient::DownloadToFile(const std::string& url,
								const std::string& outPath,
								std::function<void(double, double)> progress,
								std::string* err,
								const std::atomic<bool>* cancel,
								const std::vector<std::string>& headers)
{
	CURL* curl = AcquireHandle();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	FILE* fp = OpenFile(std::filesystem::path(outPath), "wb"); // long paths are fine
	if (!fp) {
		if (err) *err = "Failed to open output file";
		return false;
	}

	HeaderList headerList;
	for (const std::string& header : headers)
		headerList.Add(header);
	ProgressData pd{progress, cancel};
	ApplyCommonOptions(curl, url, &pd, headerList);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (err) *err = DescribeError(curl, res, cancel);
		fclose(fp);
		// remove partial file
		std::error_code removeError;
		std::filesystem::remove(LongPath(std::filesystem::path(outPath)), removeError);
		return false;
	}

	fclose(fp);
	return true;
}

bool HttpClient::Get(const std::string& url,
					 std::string& body,
					 std::string* err,
					 const std::atomic<bool>* cancel,
					 const std::vector<std::string>& headers)
{
	body.clear();
	CURL* curl = AcquireHandle();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	HeaderList headerList;
	for (const std::string& header : headers)
		headerList.Add(header);
	ProgressData pd{nullptr, cancel};
	ApplyCommonOptions(curl, url, &pd, headerList);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (err) *err = DescribeError(curl, res, cancel);
		body.clear();
		return false;
	}
	return true;
}

bool HttpClient::Post(const std::string& url,
					  const std::string& jsonBody,
					  std::string& body,
					  std::string* err,
					  const std::atomic<bool>* cancel,
					  const std::vector<std::string>& headers)
{
	body.clear();
	CURL* curl = AcquireHandle();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	HeaderList headerList;
	headerList.Add("Content-Type: application/json");
	headerList.Add("Accept: application/json");
	for (const std::string& header : headers)
		headerList.Add(header);
	ProgressData pd{nullptr, cancel};
	ApplyCommonOptions(curl, url, &pd, headerList);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(jsonBody.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (err) *err = DescribeError(curl, res, cancel);
		body.clear();
		return false;
	}
	return true;
}

bool HttpClient::Request(const std::string& url,
						 const std::vector<std::string>& headers,
						 const std::string& requestBody,
						 std::string& responseBody,
						 long& status,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	responseBody.clear();
	status = 0;
	CURL* curl = AcquireHandle();
	if (!curl) {
		if (err) *err = "Failed to initialize curl";
		return false;
	}

	HeaderList headerList;
	for (const std::string& header : headers)
		headerList.Add(header);
	ProgressData pd{nullptr, cancel};
	ApplyCommonOptions(curl, url, &pd, headerList);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	if (!requestBody.empty()) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(requestBody.size()));
	}

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (err) *err = DescribeError(curl, res, cancel);
		responseBody.clear();
		return false;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	return true;
}

void HttpClient::ReleaseThreadHandle()
{
	CurrentThreadHandle().Release();
}

std::string IHttpClient::UrlEncode(const std::string& text)
{
	char* escaped = curl_easy_escape(nullptr, text.c_str(), static_cast<int>(text.size()));
	if (!escaped)
		return {};
	std::string result(escaped);
	curl_free(escaped);
	return result;
}
