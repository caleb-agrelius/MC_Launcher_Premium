#pragma once

#include "Network/IHttpClient.h"

// An IHttpClient for offline mode (`--offline` or MCLP_OFFLINE=1, see docs/features/debug-harness.md):
// every call fails at once with a clear error and nothing touches the network, so debug launches
// don't hit the pack APIs or anyone else's servers. Callers see an ordinary failed request.
// MCLP_OFFLINE_DELAY_MS=n makes each refusal wait n milliseconds first (ending early if the request is
// cancelled), which keeps the app in its loading states long enough to look at them.
class OfflineHttpClient final : public IHttpClient {
public:
	OfflineHttpClient();

	static constexpr const char* kMessage = "Offline mode: network requests are disabled (started with --offline)";

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

private:
	int m_delayMs = 0;
};
