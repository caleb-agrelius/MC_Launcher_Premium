#pragma once

#include "Storage/Account.h"

#include <atomic>
#include <string>

class IHttpClient;

// Opens Microsoft sign-in in the system browser using OAuth authorization code
// flow with PKCE and a temporary localhost callback, then authenticates with
// Xbox Live and Minecraft Services. Fails unless the account owns Minecraft: Java
// Edition (store entitlements) and has a Minecraft profile.
bool SignInWithMicrosoft(IHttpClient& http,
						 const std::string& clientId,
						 Account& account,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// Gets a fresh Minecraft access token from the saved refresh token and checks
// ownership and the profile again, as sign-in does. Updates the account's tokens,
// name and UUID in place.
bool RefreshMicrosoftAccount(IHttpClient& http,
							 const std::string& clientId,
							 Account& account,
							 std::string* err = nullptr,
							 const std::atomic<bool>* cancel = nullptr);
