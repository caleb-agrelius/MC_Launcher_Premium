#pragma once

#include "Network/IHttpClient.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>

// What a refresh button needs from the response cache: throw everything away.
class ICacheControl {
public:
	virtual ~ICacheControl() = default;
	virtual void Clear() = 0;
};

// Wraps another IHttpClient and remembers successful GET responses, so browsing the same data again
// (leaving a page and coming back, toggling a source, reopening a pack) doesn't call the pack APIs again.
//  - An entry is keyed by URL and extra headers and is served until it is older than the lifetime.
//  - Identical GETs running at the same moment share one request; the others wait for it.
//  - Failures are never remembered, so a retry always goes out.
//  - Responses over maxEntryBytes aren't kept, and the oldest entries go when maxTotalBytes is passed.
//  - Post, Request and DownloadToFile pass straight through.
// Safe to call from several worker threads. Clear() (the Refresh button) also discards responses that
// are still on their way, so they can't put old data back.
class CachingHttpClient final : public IHttpClient, public ICacheControl {
public:
	struct Limits {
		double lifetimeSeconds = 15.0 * 60.0;
		std::size_t maxEntryBytes = 8u * 1024u * 1024u;
		std::size_t maxTotalBytes = 64u * 1024u * 1024u;
	};
	// Seconds on any steady clock; a test passes its own to move time.
	using Clock = std::function<double()>;

	struct Stats {
		std::size_t hits = 0;     // answered from memory
		std::size_t shared = 0;   // waited for an identical request already running
		std::size_t requests = 0; // sent to the wrapped client
	};

	explicit CachingHttpClient(IHttpClient& inner, Limits limits = {}, Clock clock = nullptr);

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

	void Clear() override;

	Stats GetStats() const;
	std::size_t EntryCount() const;

private:
	struct Entry {
		std::string body;
		double storedAt = 0.0;
	};

	static std::string Key(const std::string& url, const std::vector<std::string>& headers);
	void Store(const std::string& key, const std::string& body);

	IHttpClient& m_inner;
	Limits m_limits;
	Clock m_clock;

	mutable std::mutex m_mutex;
	std::condition_variable m_flightDone;
	std::map<std::string, Entry> m_entries;
	std::map<std::string, int> m_inFlight; // keys being fetched now
	std::size_t m_totalBytes = 0;
	unsigned m_generation = 0; // bumped by Clear()
	Stats m_stats;
};
