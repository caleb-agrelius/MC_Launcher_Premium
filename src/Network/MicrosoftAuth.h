#pragma once

#include "Storage/Account.h"

#include <atomic>
#include <string>

class HttpClient;

// Microsoft account sign-in for Minecraft: Microsoft OAuth (device code flow)
// -> Xbox Live -> XSTS -> Minecraft services. Needs an Azure app client ID that
// Mojang has approved for the Minecraft API (set "microsoftClientId" in settings.json).

struct DeviceCode {
	std::string userCode;        // short code the player types in the browser
	std::string verificationUri; // usually https://www.microsoft.com/link
	std::string deviceCode;      // secret used to poll for the result
	int intervalSeconds = 5;
	int expiresInSeconds = 900;
};

bool StartMicrosoftDeviceCode(HttpClient& http,
							  const std::string& clientId,
							  DeviceCode& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);

// Polls until the player finishes signing in, the code expires, or cancel is set,
// then completes the Xbox/Minecraft steps. Fills a Microsoft account.
bool FinishMicrosoftDeviceCode(HttpClient& http,
							   const std::string& clientId,
							   const DeviceCode& code,
							   Account& out,
							   std::string* err = nullptr,
							   const std::atomic<bool>* cancel = nullptr);

// Gets a fresh Minecraft access token from the saved refresh token. Updates the
// account's tokens, name and UUID in place.
bool RefreshMicrosoftAccount(HttpClient& http,
							 const std::string& clientId,
							 Account& account,
							 std::string* err = nullptr,
							 const std::atomic<bool>* cancel = nullptr);
