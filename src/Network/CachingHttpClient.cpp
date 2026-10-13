#include "Network/CachingHttpClient.h"

#include "Util/DebugTrace.h"

#include <chrono>

namespace {

double SteadySeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

CachingHttpClient::CachingHttpClient(IHttpClient& inner)
	: CachingHttpClient(inner, Limits{})
{
}

CachingHttpClient::CachingHttpClient(IHttpClient& inner, Limits limits, Clock clock)
	: m_inner(inner), m_limits(limits), m_clock(clock ? std::move(clock) : Clock(SteadySeconds))
{
}

std::string CachingHttpClient::Key(const std::string& url, const std::vector<std::string>& headers)
{
	std::string key = url;
	for (const std::string& header : headers)
		key += "\n" + header; // an API key changes the answer, so it is part of the key (memory only)
	return key;
}

bool CachingHttpClient::DownloadToFile(const std::string& url, const std::string& outPath,
	std::function<void(double, double)> progress, std::string* err, const std::atomic<bool>* cancel,
	const std::vector<std::string>& headers)
{
	return m_inner.DownloadToFile(url, outPath, std::move(progress), err, cancel, headers);
}

bool CachingHttpClient::Post(const std::string& url, const std::string& jsonBody, std::string& body, std::string* err,
	const std::atomic<bool>* cancel, const std::vector<std::string>& headers)
{
	return m_inner.Post(url, jsonBody, body, err, cancel, headers);
}

bool CachingHttpClient::Request(const std::string& url, const std::vector<std::string>& headers, const std::string& requestBody,
	std::string& responseBody, long& status, std::string* err, const std::atomic<bool>* cancel)
{
	return m_inner.Request(url, headers, requestBody, responseBody, status, err, cancel);
}

bool CachingHttpClient::Get(const std::string& url, std::string& body, std::string* err, const std::atomic<bool>* cancel,
	const std::vector<std::string>& headers)
{
	const std::string key = Key(url, headers);
	std::unique_lock<std::mutex> lock(m_mutex);
	for (;;) {
		const auto cached = m_entries.find(key);
		if (cached != m_entries.end()) {
			if (m_clock() - cached->second.storedAt < m_limits.lifetimeSeconds) {
				body = cached->second.body;
				++m_stats.hits;
				if (Debug::Enabled(Debug::kHttp))
					Debug::Write(Debug::kHttp, "CACHE " + Debug::RedactUrl(url) + " (" + std::to_string(body.size()) + " bytes, no request)");
				return true;
			}
			m_totalBytes -= cached->second.body.size();
			m_entries.erase(cached);
		}
		if (m_inFlight.count(key) == 0)
			break; // nobody is fetching it: this call does
		// The same GET is already running: wait for it, then look in the cache again.
		++m_stats.shared;
		while (m_inFlight.count(key) != 0) {
			if (cancel && cancel->load()) {
				if (err)
					*err = "Cancelled";
				return false;
			}
			m_flightDone.wait_for(lock, std::chrono::milliseconds(100));
		}
	}

	m_inFlight[key] = 1;
	const unsigned generation = m_generation;
	++m_stats.requests;
	lock.unlock();
	std::string fetched;
	bool ok = false;
	try {
		ok = m_inner.Get(url, fetched, err, cancel, headers);
	} catch (...) {
		lock.lock();
		m_inFlight.erase(key);
		m_flightDone.notify_all();
		throw;
	}
	lock.lock();
	m_inFlight.erase(key);
	if (ok && generation == m_generation) // a Clear() while it was on its way means the answer may already be old
		Store(key, fetched);
	m_flightDone.notify_all();
	if (ok)
		body = std::move(fetched);
	return ok;
}

// Keeps a response, making room first. The caller holds the lock.
void CachingHttpClient::Store(const std::string& key, const std::string& body)
{
	if (body.size() > m_limits.maxEntryBytes)
		return;
	const double now = m_clock();
	const auto old = m_entries.find(key);
	if (old != m_entries.end()) {
		m_totalBytes -= old->second.body.size();
		m_entries.erase(old);
	}
	while (!m_entries.empty() && m_totalBytes + body.size() > m_limits.maxTotalBytes) {
		auto oldest = m_entries.begin();
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
			if (it->second.storedAt < oldest->second.storedAt)
				oldest = it;
		}
		m_totalBytes -= oldest->second.body.size();
		m_entries.erase(oldest);
	}
	m_entries[key] = Entry{ body, now };
	m_totalBytes += body.size();
}

void CachingHttpClient::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_entries.clear();
	m_totalBytes = 0;
	++m_generation;
	if (Debug::Enabled(Debug::kHttp))
		Debug::Write(Debug::kHttp, "CACHE cleared");
}

CachingHttpClient::Stats CachingHttpClient::GetStats() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_stats;
}

std::size_t CachingHttpClient::EntryCount() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_entries.size();
}
