#include "MicrosoftAuth.h"

#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
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

#ifndef MCLP_MICROSOFT_CLIENT_ID
#define MCLP_MICROSOFT_CLIENT_ID ""
#endif

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
	SocketHandle(SocketHandle&& other) noexcept : m_socket(other.m_socket)
	{
		other.m_socket = INVALID_SOCKET;
	}

	SocketHandle& operator=(SocketHandle&& other) noexcept
	{
		if (this != &other) {
			Close();
			m_socket = other.m_socket;
			other.m_socket = INVALID_SOCKET;
		}
		return *this;
	}

	SOCKET Get() const { return m_socket; }
	bool Valid() const { return m_socket != INVALID_SOCKET; }

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

static std::string Base64Url(const unsigned char* bytes, std::size_t length)
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

static bool SecureRandom(std::vector<unsigned char>& bytes, std::string* err)
{
	if (bytes.empty())
		return true;
	const NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (status < 0) {
		if (err) *err = "Windows could not generate secure random data for OAuth.";
		return false;
	}
	return true;
}

static bool CreateCodeChallenge(const std::string& verifier, std::string& challenge, std::string* err)
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	const NTSTATUS opened = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
	if (opened < 0) {
		if (err) *err = "Could not initialize SHA-256 for OAuth PKCE.";
		return false;
	}

	std::array<unsigned char, 32> digest{};
	const NTSTATUS hashed = BCryptHash(algorithm, nullptr, 0,
		reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),
		static_cast<ULONG>(verifier.size()), digest.data(), static_cast<ULONG>(digest.size()));
	BCryptCloseAlgorithmProvider(algorithm, 0);
	if (hashed < 0) {
		if (err) *err = "Could not calculate the OAuth PKCE challenge.";
		return false;
	}

	challenge = Base64Url(digest.data(), digest.size());
	return true;
}

static bool CreateRandomToken(std::size_t byteCount, std::string& token, std::string* err)
{
	std::vector<unsigned char> bytes(byteCount);
	if (!SecureRandom(bytes, err))
		return false;
	token = Base64Url(bytes.data(), bytes.size());
	return true;
}

static bool DecodeQueryComponent(const std::string& input, std::string& output)
{
	output.clear();
	for (std::size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '+') {
			output.push_back(' ');
		} else if (input[i] == '%') {
			if (i + 2 >= input.size())
				return false;
			const auto hexValue = [](char c) -> int {
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

static bool ParseQuery(const std::string& query, std::map<std::string, std::string>& values)
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
				!DecodeQueryComponent(equals == std::string::npos ? "" : pair.substr(equals + 1), value))
				return false;
			if (!values.emplace(std::move(key), std::move(value)).second)
				return false;
		}
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return true;
}

static void SendBrowserResponse(SOCKET socket, bool success)
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
	~LoopbackListener()
	{
		m_listener4.Close();
		m_listener6.Close();
	}

	bool Start(unsigned short& port, std::string* err)
	{
		m_listener4 = SocketHandle(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
		if (!m_listener4.Valid()) {
			if (err) *err = "Could not create the local OAuth callback listener.";
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (bind(m_listener4.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
			listen(m_listener4.Get(), 1) == SOCKET_ERROR) {
			if (err) *err = "Could not bind the local OAuth callback listener. Check whether local connections are blocked.";
			return false;
		}

		int addressLength = sizeof(address);
		if (getsockname(m_listener4.Get(), reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR) {
			if (err) *err = "Could not determine the local OAuth callback port.";
			return false;
		}
		port = ntohs(address.sin_port);

		m_listener6 = SocketHandle(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
		if (m_listener6.Valid()) {
			DWORD ipv6Only = 1;
			sockaddr_in6 address6{};
			address6.sin6_family = AF_INET6;
			address6.sin6_addr = in6addr_loopback;
			address6.sin6_port = htons(port);
			if (setsockopt(m_listener6.Get(), IPPROTO_IPV6, IPV6_V6ONLY,
					reinterpret_cast<const char*>(&ipv6Only), sizeof(ipv6Only)) == SOCKET_ERROR ||
				bind(m_listener6.Get(), reinterpret_cast<sockaddr*>(&address6), sizeof(address6)) == SOCKET_ERROR ||
				listen(m_listener6.Get(), 1) == SOCKET_ERROR)
				m_listener6.Close();
		}
		return true;
	}

	bool WaitForAuthorizationCode(const std::string& expectedState,
								  const std::atomic<bool>& cancel,
								  std::string& code,
								  std::string* err)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
		while (!cancel.load() && std::chrono::steady_clock::now() < deadline) {
			fd_set readSet;
			FD_ZERO(&readSet);
			if (m_listener4.Valid())
				FD_SET(m_listener4.Get(), &readSet);
			if (m_listener6.Valid())
				FD_SET(m_listener6.Get(), &readSet);
			timeval timeout{};
			timeout.tv_usec = 250000;
			const int selected = select(0, &readSet, nullptr, nullptr, &timeout);
			if (selected == SOCKET_ERROR) {
				if (err) *err = "The local OAuth callback listener failed.";
				return false;
			}
			if (selected == 0)
				continue;

			const SOCKET readyListener = m_listener4.Valid() && FD_ISSET(m_listener4.Get(), &readSet)
				? m_listener4.Get() : m_listener6.Get();
			sockaddr_storage peer{};
			int peerLength = sizeof(peer);
			SocketHandle connection(accept(readyListener, reinterpret_cast<sockaddr*>(&peer), &peerLength));
			if (!connection.Valid()) {
				if (err) *err = "Could not accept the local OAuth callback.";
				return false;
			}
			const bool isLoopback = peer.ss_family == AF_INET
				? reinterpret_cast<const sockaddr_in*>(&peer)->sin_addr.s_addr == htonl(INADDR_LOOPBACK)
				: peer.ss_family == AF_INET6 &&
					IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_addr);
			if (!isLoopback)
				continue;

			DWORD receiveTimeoutMs = 2000;
			setsockopt(connection.Get(), SOL_SOCKET, SO_RCVTIMEO,
				reinterpret_cast<const char*>(&receiveTimeoutMs), sizeof(receiveTimeoutMs));
			std::string request;
			char buffer[1024];
			while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
				const int received = recv(connection.Get(), buffer, sizeof(buffer), 0);
				if (received <= 0)
					break;
				request.append(buffer, static_cast<std::size_t>(received));
			}

			const std::size_t lineEnd = request.find("\r\n");
			if (lineEnd == std::string::npos || request.compare(0, 5, "GET /") != 0) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned an invalid local OAuth callback.";
				return false;
			}
			const std::size_t pathEnd = request.find(' ', 5);
			if (pathEnd == std::string::npos || pathEnd > lineEnd) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned an invalid local OAuth callback.";
				return false;
			}
			const std::string requestTarget = request.substr(5, pathEnd - 5);
			const std::size_t queryStart = requestTarget.find('?');
			std::map<std::string, std::string> parameters;
			if (requestTarget.empty() || requestTarget[0] != '?' || queryStart != 0 ||
				!ParseQuery(requestTarget.substr(queryStart + 1), parameters)) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "Microsoft returned an invalid local OAuth callback.";
				return false;
			}

			const auto state = parameters.find("state");
			if (state == parameters.end() || state->second != expectedState) {
				SendBrowserResponse(connection.Get(), false);
				if (err) *err = "The OAuth state did not match. Sign-in was rejected.";
				return false;
			}
			const auto oauthError = parameters.find("error");
			if (oauthError != parameters.end()) {
				SendBrowserResponse(connection.Get(), false);
				const auto description = parameters.find("error_description");
				if (err) {
					*err = "Microsoft sign-in failed: " + oauthError->second;
					if (description != parameters.end() && !description->second.empty())
						*err += " (" + description->second + ")";
				}
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

		if (err)
			*err = cancel.load() ? "Sign-in was cancelled." : "Timed out waiting for Microsoft sign-in.";
		return false;
	}

private:
	SocketHandle m_listener4;
	SocketHandle m_listener6;
};

static std::string JsonString(const json& value, const char* key)
{
	const auto it = value.find(key);
	return it != value.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static json ParseResponse(const std::string& response)
{
	return json::parse(response, nullptr, false);
}

static std::string ApiError(const char* service, long statusCode, const std::string& response)
{
	std::string message = std::string(service) + " rejected the sign-in request (HTTP " +
		std::to_string(statusCode) + ").";
	const json body = ParseResponse(response);
	if (!body.is_discarded()) {
		std::string detail = JsonString(body, "error_description");
		if (detail.empty())
			detail = JsonString(body, "errorMessage");
		if (detail.empty())
			detail = JsonString(body, "Message");
		if (detail.empty())
			detail = JsonString(body, "error");
		if (!detail.empty())
			message += " " + detail;
		const auto xboxError = body.find("XErr");
		if (xboxError != body.end() && (xboxError->is_number() || xboxError->is_string()))
			message += " Xbox error code: " + xboxError->dump() + ".";
	}
	return message;
}

static bool PostJson(HttpClient& http,
					 const std::string& url,
					 const json& request,
					 const char* service,
					 json& response,
					 const std::atomic<bool>& cancel,
					 std::string* err)
{
	std::string body;
	long statusCode = 0;
	std::string transportError;
	if (!http.Post(url, request.dump(), "application/json", body, statusCode, &transportError, &cancel)) {
		if (err) *err = std::string(service) + " request failed: " + transportError;
		return false;
	}
	if (statusCode < 200 || statusCode >= 300) {
		if (err) *err = ApiError(service, statusCode, body);
		return false;
	}

	response = ParseResponse(body);
	if (response.is_discarded() || !response.is_object()) {
		if (err) *err = std::string(service) + " returned an invalid response.";
		return false;
	}
	return true;
}

static bool ExchangeAuthorizationCode(HttpClient& http,
									  const std::string& redirectUri,
									  const std::string& code,
									  const std::string& verifier,
									  json& response,
									  const std::atomic<bool>& cancel,
									  std::string* err)
{
	const std::string body =
		"client_id=" + HttpClient::UrlEncode(MCLP_MICROSOFT_CLIENT_ID) +
		"&grant_type=authorization_code&code=" + HttpClient::UrlEncode(code) +
		"&redirect_uri=" + HttpClient::UrlEncode(redirectUri) +
		"&code_verifier=" + HttpClient::UrlEncode(verifier) +
		"&scope=" + HttpClient::UrlEncode("XboxLive.signin");
	std::string responseBody;
	long statusCode = 0;
	std::string transportError;
	if (!http.Post("https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
		body, "application/x-www-form-urlencoded", responseBody, statusCode, &transportError, &cancel)) {
		if (err) *err = "Microsoft token exchange failed: " + transportError;
		return false;
	}
	if (statusCode < 200 || statusCode >= 300) {
		if (err) *err = ApiError("Microsoft token service", statusCode, responseBody);
		return false;
	}

	response = ParseResponse(responseBody);
	if (response.is_discarded() || !response.is_object() ||
		JsonString(response, "access_token").empty()) {
		if (err) *err = "Microsoft returned a token response without an access token.";
		return false;
	}
	return true;
}

static bool SignIn(HttpClient& http,
				   MinecraftAccount& account,
				   const std::atomic<bool>& cancel,
				   std::string* err)
{
	if (MCLP_MICROSOFT_CLIENT_ID[0] == '\0') {
		if (err) *err = "Microsoft sign-in is not configured. Set MCLP_MICROSOFT_CLIENT_ID when configuring CMake.";
		return false;
	}
	if (cancel.load()) {
		if (err) *err = "Sign-in was cancelled.";
		return false;
	}

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
	const std::string authorizationUrl =
		"https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"
		"?client_id=" + HttpClient::UrlEncode(MCLP_MICROSOFT_CLIENT_ID) +
		"&response_type=code&response_mode=query" +
		"&redirect_uri=" + HttpClient::UrlEncode(redirectUri) +
		"&scope=" + HttpClient::UrlEncode("XboxLive.signin") +
		"&state=" + HttpClient::UrlEncode(state) +
		"&code_challenge=" + HttpClient::UrlEncode(challenge) +
		"&code_challenge_method=S256&prompt=select_account";
	if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", authorizationUrl.c_str(),
		nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
		if (err) *err = "Could not open the browser for Microsoft sign-in.";
		return false;
	}

	std::string code;
	if (!listener.WaitForAuthorizationCode(state, cancel, code, err))
		return false;
	if (cancel.load()) {
		if (err) *err = "Sign-in was cancelled.";
		return false;
	}

	json microsoftToken;
	if (!ExchangeAuthorizationCode(http, redirectUri, code, verifier, microsoftToken, cancel, err))
		return false;
	const std::string microsoftAccessToken = JsonString(microsoftToken, "access_token");

	json xboxRequest = {
		{"Properties", {
			{"AuthMethod", "RPS"},
			{"SiteName", "user.auth.xboxlive.com"},
			{"RpsTicket", "d=" + microsoftAccessToken}
		}},
		{"RelyingParty", "http://auth.xboxlive.com"},
		{"TokenType", "JWT"}
	};
	json xboxResponse;
	if (!PostJson(http, "https://user.auth.xboxlive.com/user/authenticate", xboxRequest,
		"Xbox Live", xboxResponse, cancel, err))
		return false;
	const std::string xboxToken = JsonString(xboxResponse, "Token");
	const json* xboxClaims = xboxResponse.contains("DisplayClaims") ? &xboxResponse["DisplayClaims"] : nullptr;
	const json* xboxUsers = xboxClaims && xboxClaims->is_object() && xboxClaims->contains("xui")
		? &(*xboxClaims)["xui"] : nullptr;
	if (xboxToken.empty() || !xboxUsers || !xboxUsers->is_array() || xboxUsers->empty() ||
		!(*xboxUsers)[0].is_object()) {
		if (err) *err = "Xbox Live returned an incomplete authentication response.";
		return false;
	}
	const std::string userHash = JsonString((*xboxUsers)[0], "uhs");
	if (userHash.empty()) {
		if (err) *err = "Xbox Live did not return the account identifier required by Minecraft.";
		return false;
	}

	json xstsRequest = {
		{"Properties", {
			{"SandboxId", "RETAIL"},
			{"UserTokens", json::array({xboxToken})}
		}},
		{"RelyingParty", "rp://api.minecraftservices.com/"},
		{"TokenType", "JWT"}
	};
	json xstsResponse;
	if (!PostJson(http, "https://xsts.auth.xboxlive.com/xsts/authorize", xstsRequest,
		"Xbox security service", xstsResponse, cancel, err))
		return false;
	const std::string xstsToken = JsonString(xstsResponse, "Token");
	const json* xstsClaims = xstsResponse.contains("DisplayClaims") ? &xstsResponse["DisplayClaims"] : nullptr;
	const json* xstsUsers = xstsClaims && xstsClaims->is_object() && xstsClaims->contains("xui")
		? &(*xstsClaims)["xui"] : nullptr;
	if (xstsToken.empty() || !xstsUsers || !xstsUsers->is_array() || xstsUsers->empty() ||
		!(*xstsUsers)[0].is_object()) {
		if (err) *err = "Xbox security service returned an incomplete authentication response.";
		return false;
	}
	const std::string xstsUserHash = JsonString((*xstsUsers)[0], "uhs");
	if (xstsUserHash.empty()) {
		if (err) *err = "Xbox security service did not return the account identifier required by Minecraft.";
		return false;
	}

	json minecraftRequest = {
		{"identityToken", "XBL3.0 x=" + xstsUserHash + ";" + xstsToken}
	};
	json minecraftResponse;
	if (!PostJson(http, "https://api.minecraftservices.com/authentication/login_with_xbox",
		minecraftRequest, "Minecraft Services", minecraftResponse, cancel, err))
		return false;
	const std::string minecraftAccessToken = JsonString(minecraftResponse, "access_token");
	if (minecraftAccessToken.empty()) {
		if (err) *err = "Minecraft Services returned a response without an access token.";
		return false;
	}

	std::string profileBody;
	long profileStatus = 0;
	std::string transportError;
	if (!http.GetWithBearer("https://api.minecraftservices.com/minecraft/profile",
		minecraftAccessToken, profileBody, profileStatus, &transportError, &cancel)) {
		if (err) *err = "Minecraft profile lookup failed: " + transportError;
		return false;
	}
	if (profileStatus == 404) {
		if (err) *err = "No Minecraft Java profile was found for this account. It may not own Minecraft: Java Edition or may not have a profile set up.";
		return false;
	}
	if (profileStatus < 200 || profileStatus >= 300) {
		if (err) *err = ApiError("Minecraft profile service", profileStatus, profileBody);
		return false;
	}

	const json profile = ParseResponse(profileBody);
	const std::string username = profile.is_object() ? JsonString(profile, "name") : std::string();
	const std::string uuid = profile.is_object() ? JsonString(profile, "id") : std::string();
	if (username.empty() || uuid.empty()) {
		if (err) *err = "Minecraft returned an incomplete Java profile.";
		return false;
	}

	account.username = username;
	account.uuid = uuid;
	return true;
}

} // namespace
#endif

bool SignInWithMicrosoft(HttpClient& http,
						 MinecraftAccount& account,
						 const std::atomic<bool>& cancel,
						 std::string* err)
{
	account = {};
	if (err)
		err->clear();
#ifdef _WIN32
	return SignIn(http, account, cancel, err);
#else
	(void)http;
	(void)cancel;
	if (err) *err = "Microsoft sign-in is currently supported only on Windows.";
	return false;
#endif
}
