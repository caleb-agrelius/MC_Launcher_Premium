#pragma once

#include <atomic>
#include <string>

class HttpClient;

struct MinecraftAccount {
	std::string username;
	std::string uuid;
};

// Runs Microsoft OAuth with PKCE, then exchanges the token through Xbox Live,
// XSTS, and Minecraft Services to verify a Java Edition profile.
bool SignInWithMicrosoft(HttpClient& http,
						 MinecraftAccount& account,
						 const std::atomic<bool>& cancel,
						 std::string* err = nullptr);
