#include "MicrosoftAuth.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#endif

using json = nlohmann::json;

static const char* kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static const char* kAuthorizeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
static const char* kScope = "XboxLive.signin offline_access";

static const std::vector<std::string> kFormHeaders = { "Content-Type: application/x-www-form-urlencoded" };
static const std::vector<std::string> kJsonHeaders = { "Content-Type: application/json", "Accept: application/json" };

#ifdef _WIN32
namespace {

class WinsockSession {
public:
	WinsockSession()
	{
		WSADATA data{};
		m_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}

	~WinsockSession()
	{
		if (m_started)
			WSACleanup();
	}

	bool Started() const { return m_started; }

private:
	bool m_started = false;
};

class SocketHandle {
public:
	explicit SocketHandle(SOCKET socket = INVALID_SOCKET) : m_socket(socket) {}
	~SocketHandle() { Close(); }

	SocketHandle(const SocketHandle&) = delete;
	SocketHandle& operator=(const SocketHandle&) = delete;

	SOCKET Get() const { return m_socket; }
	bool Valid() const { return m_socket != INVALID_SOCKET; }
	void Set(SOCKET socket)
	{
		Close();
		m_socket = socket;
	}

	void Close()
	{
		if (Valid()) {
			closesocket(m_socket);
			m_socket = INVALID_SOCKET;
		}
	}

private:
	SOCKET m_socket;
};

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
	if (!bytes.empty() && BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
		if (err) *err = "Windows could not generate secure random data for OAuth.";
		return false;
	}
	token = Base64Url(bytes.data(), bytes.size());
	return true;
}

bool CreateCodeChallenge(const std::string& verifier, std::string& challenge, std::string* err)
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
		if (err) *err = "Could not initialize SHA-256 for OAuth PKCE.";
		return false;
	}

	std::array<unsigned char, 32> digest{};
	const NTSTATUS result = BCryptHash(algorithm, nullptr, 0,
		reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),
		static_cast<ULONG>(verifier.size()), digest.data(), static_cast<ULONG>(digest.size()));
	BCryptCloseAlgorithmProvider(algorithm, 0);
	if (result < 0) {
		if (err) *err = "Could not calculate the OAuth PKCE challenge.";
		return false;
	}
	challenge = Base64Url(digest.data(), digest.size());
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

void SendBrowserResponse(SOCKET socket, bool success)
{
	const char* body = success
		? "<!doctype html><html><body><h2>Sign-in complete</h2><p>You can return to the Minecraft launcher.</p></body></html>"
		: "<!doctype html><html><body><h2>Sign-in could not be completed</h2><p>Return to the Minecraft launcher for details.</p></body></html>";
	const std::string response = std::string("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ") +
		std::to_string(std::char_traits<char>::length(body)) + "\r\n\r\n" + body;
	std::size_t sent = 0;
	while (sent < response.size()) {
		const int count = send(socket, response.data() + sent,
			static_cast<int>(response.size() - sent), 0);
		if (count <= 0)
			break;
		sent += static_cast<std::size_t>(count);
	}
}

class LoopbackListener {
public:
	bool Start(unsigned short& port, std::string* err)
	{
		m_listener.Set(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
		if (!m_listener.Valid()) {
			if (err) *err = "Could not create the local OAuth callback listener.";
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (bind(m_listener.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
			listen(m_listener.Get(), 1) == SOCKET_ERROR) {
			if (err) *err = "Could not bind the local OAuth callback listener. Check whether local connections are blocked.";
			return false;
		}
		int addressLength = sizeof(address);
		if (getsockname(m_listener.Get(), reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR) {
			if (err) *err = "Could not determine the local OAuth callback port.";
			return false;
		}
		port = ntohs(address.sin_port);
		return true;
	}

	bool WaitForAuthorizationCode(const std::string& expectedState, const std::atomic<bool>* cancel,
		std::string& code, std::string* err)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (cancel && cancel->load()) {
				if (err) *err = "Sign-in was cancelled.";
				return false;
			}
			fd_set readers;
			FD_ZERO(&readers);
			FD_SET(m_listener.Get(), &readers);
			timeval timeout{ 0, 250000 };
			const int ready = select(0, &readers, nullptr, nullptr, &timeout);
			if (ready == SOCKET_ERROR) {
				if (err) *err = "The local OAuth callback listener failed.";
				return false;
			}
			if (ready == 0)
				continue;

			SocketHandle connection(accept(m_listener.Get(), nullptr, nullptr));
			if (!connection.Valid())
				continue;
			std::array<char, 16384> buffer{};
			std::size_t received = 0;
			while (received < buffer.size() - 1) {
				const int count = recv(connection.Get(), buffer.data() + received,
					static_cast<int>(buffer.size() - 1 - received), 0);
				if (count <= 0)
					break;
				received += static_cast<std::size_t>(count);
				if (std::string(buffer.data(), received).find("\r\n\r\n") != std::string::npos)
					break;
			}
			if (received == 0) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Could not read Microsoft's sign-in response.";
				return false;
			}
			const std::string request(buffer.data(), received);
			const std::size_t lineEnd = request.find("\r\n");
			if (lineEnd == std::string::npos || request.compare(0, 5, "GET /") != 0) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned an invalid sign-in response.";
				return false;
			}
			const std::size_t targetStart = 5;
			const std::size_t targetEnd = request.find(' ', targetStart);
			if (targetEnd == std::string::npos || targetEnd > lineEnd) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned an invalid sign-in response.";
				return false;
			}
			const std::string target = request.substr(targetStart, targetEnd - targetStart);
			if (target.empty() || target[0] != '?') {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft did not return OAuth sign-in parameters.";
				return false;
			}

			std::map<std::string, std::string> parameters;
			if (!ParseQuery(target.substr(1), parameters)) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned malformed sign-in parameters.";
				return false;
			}
			const auto state = parameters.find("state");
			if (state == parameters.end() || state->second != expectedState) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft sign-in state did not match. Please try again.";
				return false;
			}
			const auto oauthError = parameters.find("error");
			if (oauthError != parameters.end()) {
				const auto description = parameters.find("error_description");
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = description != parameters.end()
					? "Microsoft sign-in failed: " + description->second
					: "Microsoft sign-in failed: " + oauthError->second;
				return false;
			}
			const auto authorizationCode = parameters.find("code");
			if (authorizationCode == parameters.end() || authorizationCode->second.empty()) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft did not return an authorization code.";
				return false;
			}
			SendBrowserResponse(connection.Get(), true);
			code = authorizationCode->second;
			return true;
		}
		if (err) *err = "Timed out waiting for Microsoft sign-in.";
		return false;
	}

private:
	SocketHandle m_listener;
};

} // namespace
#endif

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

bool SignInWithMicrosoft(HttpClient& http, const std::string& clientId, Account& account,
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
#ifdef _WIN32
	WinsockSession winsock;
	if (!winsock.Started()) {
		if (err) *err = "Could not initialize Windows networking for Microsoft sign-in.";
		return false;
	}

	std::string state;
	std::string verifier;
	if (!CreateRandomToken(32, state, err) || !CreateRandomToken(32, verifier, err))
		return false;
	std::string challenge;
	if (!CreateCodeChallenge(verifier, challenge, err))
		return false;

	LoopbackListener listener;
	unsigned short port = 0;
	if (!listener.Start(port, err))
		return false;
	const std::string redirectUri = "http://localhost:" + std::to_string(port);
	const std::string authorizationUrl = std::string(kAuthorizeUrl) +
		"?client_id=" + HttpClient::UrlEncode(clientId) +
		"&response_type=code&response_mode=query" +
		"&redirect_uri=" + HttpClient::UrlEncode(redirectUri) +
		"&scope=" + HttpClient::UrlEncode(kScope) +
		"&state=" + HttpClient::UrlEncode(state) +
		"&code_challenge=" + HttpClient::UrlEncode(challenge) +
		"&code_challenge_method=S256&prompt=select_account";
	if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", authorizationUrl.c_str(),
			nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
		if (err) *err = "Could not open the browser for Microsoft sign-in.";
		return false;
	}

	std::string authorizationCode;
	if (!listener.WaitForAuthorizationCode(state, cancel, authorizationCode, err))
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
#else
	(void)http;
	(void)cancel;
	if (err) *err = "Microsoft sign-in is currently supported only on Windows.";
	return false;
#endif
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
