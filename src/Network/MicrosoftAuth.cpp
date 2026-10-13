#include "MicrosoftAuth.h"
#include "HttpClient.h"
#include "Platform/Platform.h"
#include "Util/Sha256.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

static const char* kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static const char* kAuthorizeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
static const char* kScope = "XboxLive.signin offline_access";

static const std::vector<std::string> kFormHeaders = { "Content-Type: application/x-www-form-urlencoded" };
static const std::vector<std::string> kJsonHeaders = { "Content-Type: application/json", "Accept: application/json" };

namespace {

std::string Base64Url(const unsigned char* bytes, std::size_t length)
{
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string result;
	result.reserve((length * 4 + 2) / 3);
	for (std::size_t i = 0; i < length; i += 3) {
		const std::uint32_t a = bytes[i];
		const std::uint32_t b = i + 1 < length ? bytes[i + 1] : 0;
		const std::uint32_t c = i + 2 < length ? bytes[i + 2] : 0;
		const std::uint32_t block = (a << 16) | (b << 8) | c;
		result.push_back(alphabet[(block >> 18) & 63]);
		result.push_back(alphabet[(block >> 12) & 63]);
		if (i + 1 < length)
			result.push_back(alphabet[(block >> 6) & 63]);
		if (i + 2 < length)
			result.push_back(alphabet[block & 63]);
	}
	return result;
}

bool CreateRandomToken(std::size_t byteCount, std::string& token, std::string* err)
{
	std::vector<unsigned char> bytes(byteCount);
	if (!Platform::RandomBytes(bytes.data(), bytes.size())) {
		if (err) *err = "The system could not generate secure random data for OAuth.";
		return false;
	}
	token = Base64Url(bytes.data(), bytes.size());
	return true;
}

bool DecodeQueryComponent(const std::string& input, std::string& output)
{
	output.clear();
	for (std::size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '+') {
			output.push_back(' ');
		} else if (input[i] == '%') {
			if (i + 2 >= input.size())
				return false;
			auto hexValue = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			const int high = hexValue(input[i + 1]);
			const int low = hexValue(input[i + 2]);
			if (high < 0 || low < 0)
				return false;
			output.push_back(static_cast<char>((high << 4) | low));
			i += 2;
		} else {
			output.push_back(input[i]);
		}
	}
	return true;
}

bool ParseQuery(const std::string& query, std::map<std::string, std::string>& values)
{
	std::size_t start = 0;
	while (start <= query.size()) {
		const std::size_t end = query.find('&', start);
		const std::string pair = query.substr(start, end == std::string::npos ? end : end - start);
		if (!pair.empty()) {
			const std::size_t equals = pair.find('=');
			std::string key;
			std::string value;
			if (!DecodeQueryComponent(pair.substr(0, equals), key) ||
				!DecodeQueryComponent(equals == std::string::npos ? "" : pair.substr(equals + 1), value) ||
				!values.emplace(std::move(key), std::move(value)).second)
				return false;
		}
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return true;
}

const char* kSuccessPage =
	"<!doctype html><html><body><h2>Sign-in complete</h2><p>You can return to the Minecraft launcher.</p></body></html>";
const char* kFailurePage =
	"<!doctype html><html><body><h2>Sign-in could not be completed</h2><p>Return to the Minecraft launcher for details.</p></body></html>";

// Waits for the browser to come back to the local address, up to five minutes, and reads the
// authorization code from that request.
bool WaitForAuthorizationCode(Platform::LoopbackListener& listener, const std::string& expectedState,
	const std::atomic<bool>* cancel, std::string& code, std::string* err)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
	while (std::chrono::steady_clock::now() < deadline) {
		if (cancel && cancel->load()) {
			if (err) *err = "Sign-in was cancelled.";
			return false;
		}
		std::string head;
		const Platform::LoopbackListener::Result result = listener.WaitForRequest(250, head);
		if (result == Platform::LoopbackListener::Result::Timeout)
			continue;
		if (result == Platform::LoopbackListener::Result::Failed) {
			if (err) *err = "The local OAuth callback listener failed.";
			return false;
		}
		if (head.empty()) {
			listener.Reply(kFailurePage);
			if (err) *err = "Could not read Microsoft's sign-in response.";
			return false;
		}
		const bool ok = ParseAuthorizationRedirect(head, expectedState, code, err);
		listener.Reply(ok ? kSuccessPage : kFailurePage);
		return ok;
	}
	if (err) *err = "Timed out waiting for Microsoft sign-in.";
	return false;
}

} // namespace

std::string PkceChallenge(const std::string& verifier)
{
	const std::array<unsigned char, 32> digest = Sha256(verifier);
	return Base64Url(digest.data(), digest.size());
}

bool ParseAuthorizationRedirect(const std::string& requestHead, const std::string& expectedState,
	std::string& code, std::string* err)
{
	auto fail = [err](const char* message) {
		if (err) *err = message;
		return false;
	};
	const std::size_t lineEnd = requestHead.find("\r\n");
	if (lineEnd == std::string::npos || requestHead.compare(0, 5, "GET /") != 0)
		return fail("Microsoft returned an invalid sign-in response.");
	const std::size_t targetStart = 5;
	const std::size_t targetEnd = requestHead.find(' ', targetStart);
	if (targetEnd == std::string::npos || targetEnd > lineEnd)
		return fail("Microsoft returned an invalid sign-in response.");
	const std::string target = requestHead.substr(targetStart, targetEnd - targetStart);
	if (target.empty() || target[0] != '?')
		return fail("Microsoft did not return OAuth sign-in parameters.");

	std::map<std::string, std::string> parameters;
	if (!ParseQuery(target.substr(1), parameters))
		return fail("Microsoft returned malformed sign-in parameters.");
	const auto state = parameters.find("state");
	if (state == parameters.end() || state->second != expectedState)
		return fail("Microsoft sign-in state did not match. Please try again.");
	const auto oauthError = parameters.find("error");
	if (oauthError != parameters.end()) {
		const auto description = parameters.find("error_description");
		if (err) *err = description != parameters.end()
			? "Microsoft sign-in failed: " + description->second
			: "Microsoft sign-in failed: " + oauthError->second;
		return false;
	}
	const auto authorizationCode = parameters.find("code");
	if (authorizationCode == parameters.end() || authorizationCode->second.empty())
		return fail("Microsoft did not return an authorization code.");
	code = authorizationCode->second;
	return true;
}

static std::string Form(std::initializer_list<std::pair<const char*, std::string>> fields)
{
	std::string body;
	for (const auto& [key, value] : fields) {
		if (!body.empty())
			body += '&';
		body += key;
		body += '=';
		body += IHttpClient::UrlEncode(value);
	}
	return body;
}

// Sends a request and parses the JSON response. Error statuses still parse
// (their bodies describe the problem); a non-JSON body gives an error.
static bool RequestJson(IHttpClient& http, const std::string& url, const std::vector<std::string>& headers,
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
static bool LoginWithMicrosoftToken(IHttpClient& http, const std::string& msAccessToken, Account& account,
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

bool SignInWithMicrosoft(IHttpClient& http, const std::string& clientId, Account& account,
	std::string* err, const std::atomic<bool>* cancel)
{
	account = {};
	if (err)
		err->clear();
	if (clientId.empty()) {
		if (err) *err = "Microsoft sign-in needs an Azure application client ID. Set it in Settings > Account.";
		return false;
	}
	if (cancel && cancel->load()) {
		if (err) *err = "Sign-in was cancelled.";
		return false;
	}

	std::string state;
	std::string verifier;
	if (!CreateRandomToken(32, state, err) || !CreateRandomToken(32, verifier, err))
		return false;
	const std::string challenge = PkceChallenge(verifier);

	Platform::LoopbackListener listener;
	unsigned short port = 0;
	if (!listener.Start(port, err))
		return false;
	const std::string redirectUri = "http://localhost:" + std::to_string(port);
	const std::string authorizationUrl = std::string(kAuthorizeUrl) +
		"?client_id=" + IHttpClient::UrlEncode(clientId) +
		"&response_type=code&response_mode=query" +
		"&redirect_uri=" + IHttpClient::UrlEncode(redirectUri) +
		"&scope=" + IHttpClient::UrlEncode(kScope) +
		"&state=" + IHttpClient::UrlEncode(state) +
		"&code_challenge=" + IHttpClient::UrlEncode(challenge) +
		"&code_challenge_method=S256&prompt=select_account";
	if (!Platform::OpenUrl(authorizationUrl)) {
		if (err) *err = "Could not open the browser for Microsoft sign-in.";
		return false;
	}

	std::string authorizationCode;
	if (!WaitForAuthorizationCode(listener, state, cancel, authorizationCode, err))
		return false;
	if (cancel && cancel->load()) {
		if (err) *err = "Sign-in was cancelled.";
		return false;
	}

	json tokenResponse;
	long status = 0;
	const std::string tokenBody = Form({
		{ "client_id", clientId },
		{ "grant_type", "authorization_code" },
		{ "code", authorizationCode },
		{ "redirect_uri", redirectUri },
		{ "code_verifier", verifier },
		{ "scope", kScope },
	});
	if (!RequestJson(http, kTokenUrl, kFormHeaders, tokenBody, tokenResponse, status, err, cancel))
		return false;
	if (status != 200 || !tokenResponse.contains("access_token")) {
		if (err) *err = "Microsoft token exchange failed: " + OAuthError(tokenResponse, status);
		return false;
	}

	Account signedIn;
	signedIn.refreshToken = tokenResponse.value("refresh_token", "");
	if (!LoginWithMicrosoftToken(http, tokenResponse["access_token"].get<std::string>(),
			signedIn, err, cancel))
		return false;
	account = std::move(signedIn);
	return true;
}

bool RefreshMicrosoftAccount(IHttpClient& http, const std::string& clientId, Account& account,
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
