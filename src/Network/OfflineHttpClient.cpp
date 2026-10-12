#include "Network/OfflineHttpClient.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <thread>

namespace {

// Waits delayMs (in short steps, so a cancel ends it) then refuses.
bool Refuse(std::string* err, int delayMs, const std::atomic<bool>* cancel)
{
	for (int waited = 0; waited < delayMs && !(cancel && cancel->load()); waited += 50)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	if (err)
		*err = OfflineHttpClient::kMessage;
	return false;
}

}

OfflineHttpClient::OfflineHttpClient()
{
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	if (const char* delay = std::getenv("MCLP_OFFLINE_DELAY_MS"))
		m_delayMs = std::max(0, std::atoi(delay));
}

bool OfflineHttpClient::DownloadToFile(const std::string&, const std::string&, std::function<void(double, double)>,
	std::string* err, const std::atomic<bool>* cancel, const std::vector<std::string>&)
{
	return Refuse(err, m_delayMs, cancel);
}

bool OfflineHttpClient::Get(const std::string&, std::string&, std::string* err, const std::atomic<bool>* cancel,
	const std::vector<std::string>&)
{
	return Refuse(err, m_delayMs, cancel);
}

bool OfflineHttpClient::Post(const std::string&, const std::string&, std::string&, std::string* err,
	const std::atomic<bool>* cancel, const std::vector<std::string>&)
{
	return Refuse(err, m_delayMs, cancel);
}

bool OfflineHttpClient::Request(const std::string&, const std::vector<std::string>&, const std::string&, std::string&,
	long& status, std::string* err, const std::atomic<bool>* cancel)
{
	status = 0;
	return Refuse(err, m_delayMs, cancel);
}
