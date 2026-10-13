#pragma once

#include "Storage/Account.h"

#include <atomic>
#include <string>

class IHttpClient;

// Opens Microsoft sign-in in the system browser using OAuth authorization code
// flow with PKCE and a temporary localhost callback, then authenticates with
// Xbox Live and Minecraft Services.
bool SignInWithMicrosoft(IHttpClient& http,
						 const std::string& clientId,
						 Account& account,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// Gets a fresh Minecraft access token from the saved refresh token. Updates the
// account's tokens, name and UUID in place.
bool RefreshMicrosoftAccount(IHttpClient& http,
							 const std::string& clientId,
							 Account& account,
							 std::string* err = nullptr,
							 const std::atomic<bool>* cancel = nullptr);

// The PKCE code challenge for a code verifier: its SHA-256, base64url-encoded without padding
// (RFC 7636, S256).
std::string PkceChallenge(const std::string& verifier);

// Reads the authorization code out of what the browser sent to the local redirect address:
// the request headers, starting with a line like "GET /?code=...&state=... HTTP/1.1". Returns
// false, with err set, when the request isn't Microsoft's answer to this sign-in (wrong state,
// an OAuth error, no code).
bool ParseAuthorizationRedirect(const std::string& requestHead,
								const std::string& expectedState,
								std::string& code,
								std::string* err = nullptr);
