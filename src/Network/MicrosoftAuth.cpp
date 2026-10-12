#include "MicrosoftAuth.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <thread>

using json = nlohmann::json;

static const char* kDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
static const char* kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static const char* kScope = "XboxLive.signin offline_access";

static const std::vector<std::string> kFormHeaders = { "Content-Type: application/x-www-form-urlencoded" };
static const std::vector<std::string> kJsonHeaders = { "Content-Type: application/json", "Accept: application/json" };

static std::string Form(std::initializer_list<std::pair<const char*, std::string>> fields)
{
	std::string body;
	for (const auto& [key, value] : fields) {
		if (!body.empty())
			body += '&';
		body += key;
		body += '=';
		body += HttpClient::UrlEncode(value);
	}
	return body;
}

// Sends a request and parses the JSON response. Error statuses still parse
// (their bodies describe the problem); a non-JSON body gives an error.
static bool RequestJson(HttpClient& http, const std::string& url, const std::vector<std::string>& headers,
	const std::string& body, json& out, long& status, std::string* err, const std::atomic<bool>* cancel)
{
	std::string response;
	if (!http.Request(url, headers, body, response, status, err, cancel))
		return false;
	try {
		out = response.empty() ? json::object() : json::parse(response);
	}
	catch (...) {
		if (err) *err = "Unexpected response from " + url + " (HTTP " + std::to_string(status) + ")";
		return false;
	}
	return true;
}

static std::string OAuthError(const json& j, long status)
{
	std::string text = j.value("error_description", j.value("error", std::string()));
	const std::size_t newline = text.find_first_of("\r\n");
	if (newline != std::string::npos)
		text.resize(newline); // Microsoft appends trace ids on later lines
	return text.empty() ? "HTTP " + std::to_string(status) : text;
}

// Xbox Live -> XSTS -> Minecraft login -> profile. msAccessToken is a Microsoft
// OAuth access token with the XboxLive.signin scope.
static bool LoginWithMicrosoftToken(HttpClient& http, const std::string& msAccessToken, Account& account,
	std::string* err, const std::atomic<bool>* cancel)
{
	json j;
	long status = 0;

	const json xblRequest = {
		{ "Properties", { { "AuthMethod", "RPS" }, { "SiteName", "user.auth.xboxlive.com" }, { "RpsTicket", "d=" + msAccessToken } } },
		{ "RelyingParty", "http://auth.xboxlive.com" },
		{ "TokenType", "JWT" },
	};
	if (!RequestJson(http, "https://user.auth.xboxlive.com/user/authenticate", kJsonHeaders, xblRequest.dump(), j, status, err, cancel))
		return false;
	if (status != 200 || !j.contains("Token")) {
		if (err) *err = "Xbox Live sign-in failed (HTTP " + std::to_string(status) + ")";
		return false;
	}
	const std::string xblToken = j["Token"].get<std::string>();

	const json xstsRequest = {
		{ "Properties", { { "SandboxId", "RETAIL" }, { "UserTokens", json::array({ xblToken }) } } },
		{ "RelyingParty", "rp://api.minecraftservices.com/" },
		{ "TokenType", "JWT" },
	};
	if (!RequestJson(http, "https://xsts.auth.xboxlive.com/xsts/authorize", kJsonHeaders, xstsRequest.dump(), j, status, err, cancel))
		return false;
	if (status != 200 || !j.contains("Token")) {
		const std::uint64_t xerr = j.value("XErr", 0ULL);
		if (err) {
			if (xerr == 2148916233ULL)
				*err = "This Microsoft account has no Xbox profile. Sign in once at minecraft.net to create one.";
			else if (xerr == 2148916235ULL)
				*err = "Xbox Live isn't available in this account's country.";
			else if (xerr == 2148916236ULL || xerr == 2148916237ULL)
				*err = "This account needs adult verification on xbox.com.";
			else if (xerr == 2148916238ULL)
				*err = "This is a child account. An adult must add it to a Microsoft family first.";
			else
				*err = "Xbox authorization failed (HTTP " + std::to_string(status) + ")";
		}
		return false;
	}
	const std::string xstsToken = j["Token"].get<std::string>();
	std::string userHash;
	try {
		userHash = j.at("DisplayClaims").at("xui").at(0).at("uhs").get<std::string>();
	}
	catch (...) {
		if (err) *err = "Xbox authorization response had no user hash";
		return false;
	}

	const json mcRequest = { { "identityToken", "XBL3.0 x=" + userHash + ";" + xstsToken } };
	if (!RequestJson(http, "https://api.minecraftservices.com/authentication/login_with_xbox", kJsonHeaders, mcRequest.dump(), j, status, err, cancel))
		return false;
	if (status != 200 || !j.contains("access_token")) {
		if (err) {
			*err = status == 403
				? "Minecraft services rejected this app. The Azure client ID must be approved by Mojang for the Minecraft API."
				: "Minecraft sign-in failed (HTTP " + std::to_string(status) + ")";
		}
		return false;
	}
	const std::string mcToken = j["access_token"].get<std::string>();
	const std::int64_t expiresIn = j.value("expires_in", std::int64_t{ 86400 });

	if (!RequestJson(http, "https://api.minecraftservices.com/minecraft/profile",
			{ "Authorization: Bearer " + mcToken, "Accept: application/json" }, "", j, status, err, cancel))
		return false;
	if (status == 404) {
		if (err) *err = "This Microsoft account doesn't own Minecraft: Java Edition.";
		return false;
	}
	if (status != 200 || !j.contains("id") || !j.contains("name")) {
		if (err) *err = "Couldn't read the Minecraft profile (HTTP " + std::to_string(status) + ")";
		return false;
	}

	account.type = Account::Type::Microsoft;
	account.name = j["name"].get<std::string>();
	account.uuid = j["id"].get<std::string>();
	account.accessToken = mcToken;
	account.accessExpires = static_cast<std::int64_t>(std::time(nullptr)) + expiresIn;
	account.xuid = userHash;
	return true;
}

bool StartMicrosoftDeviceCode(HttpClient& http, const std::string& clientId, DeviceCode& out,
	std::string* err, const std::atomic<bool>* cancel)
{
	json j;
	long status = 0;
	if (!RequestJson(http, kDeviceCodeUrl, kFormHeaders, Form({ { "client_id", clientId }, { "scope", kScope } }), j, status, err, cancel))
		return false;
	if (status != 200 || !j.contains("device_code")) {
		if (err) *err = "Couldn't start Microsoft sign-in: " + OAuthError(j, status);
		return false;
	}
	out.userCode = j.value("user_code", "");
	out.verificationUri = j.value("verification_uri", "https://www.microsoft.com/link");
	out.deviceCode = j.value("device_code", "");
	out.intervalSeconds = std::max(1, j.value("interval", 5));
	out.expiresInSeconds = j.value("expires_in", 900);
	return true;
}

bool FinishMicrosoftDeviceCode(HttpClient& http, const std::string& clientId, const DeviceCode& code,
	Account& out, std::string* err, const std::atomic<bool>* cancel)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code.expiresInSeconds);
	int interval = code.intervalSeconds;
	while (std::chrono::steady_clock::now() < deadline) {
		// Sleep in small steps so cancelling is quick.
		for (int i = 0; i < interval * 10; ++i) {
			if (cancel && cancel->load()) {
				if (err) *err = "Cancelled";
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		json j;
		long status = 0;
		const std::string body = Form({
			{ "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
			{ "client_id", clientId },
			{ "device_code", code.deviceCode },
		});
		if (!RequestJson(http, kTokenUrl, kFormHeaders, body, j, status, err, cancel))
			return false;
		if (status == 200 && j.contains("access_token")) {
			Account account;
			account.refreshToken = j.value("refresh_token", "");
			if (!LoginWithMicrosoftToken(http, j["access_token"].get<std::string>(), account, err, cancel))
				return false;
			out = account;
			return true;
		}
		const std::string error = j.value("error", "");
		if (error == "authorization_pending")
			continue;
		if (error == "slow_down") {
			interval += 5;
			continue;
		}
		if (err) {
			if (error == "authorization_declined")
				*err = "Sign-in was declined.";
			else if (error == "expired_token")
				*err = "The sign-in code expired. Try again.";
			else
				*err = "Microsoft sign-in failed: " + OAuthError(j, status);
		}
		return false;
	}
	if (err) *err = "The sign-in code expired. Try again.";
	return false;
}

bool RefreshMicrosoftAccount(HttpClient& http, const std::string& clientId, Account& account,
	std::string* err, const std::atomic<bool>* cancel)
{
	if (account.refreshToken.empty()) {
		if (err) *err = "No saved sign-in. Sign in again.";
		return false;
	}
	json j;
	long status = 0;
	const std::string body = Form({
		{ "grant_type", "refresh_token" },
		{ "client_id", clientId },
		{ "refresh_token", account.refreshToken },
		{ "scope", kScope },
	});
	if (!RequestJson(http, kTokenUrl, kFormHeaders, body, j, status, err, cancel))
		return false;
	if (status != 200 || !j.contains("access_token")) {
		if (err) *err = "Your Microsoft sign-in expired (" + OAuthError(j, status) + "). Sign in again.";
		return false;
	}
	Account refreshed = account;
	refreshed.refreshToken = j.value("refresh_token", account.refreshToken);
	if (!LoginWithMicrosoftToken(http, j["access_token"].get<std::string>(), refreshed, err, cancel))
		return false;
	account = refreshed;
	return true;
}
