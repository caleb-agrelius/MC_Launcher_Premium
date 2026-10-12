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
