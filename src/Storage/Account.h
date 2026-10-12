#pragma once

#include <cstdint>
#include <string>

// The player identity passed to Minecraft at launch.
struct Account {
	enum class Type { None, Microsoft, Offline };
	Type type = Type::None;
	std::string name;        // in-game username
	std::string uuid;        // 32 hex digits, no dashes
	std::string accessToken; // Minecraft access token ("0" for offline)
	std::int64_t accessExpires = 0; // unix seconds; Microsoft only
	std::string refreshToken;       // Microsoft OAuth refresh token
	std::string xuid;               // Xbox user id; Microsoft only
};

// account.dat in the data folder. On Windows the contents are encrypted with
// DPAPI, so only the same Windows user can read the saved tokens.
// Missing or unreadable files give Type::None.
Account LoadAccount(const std::string& path);
bool SaveAccount(const std::string& path, const Account& account);

// An offline account for singleplayer and offline-mode servers. The UUID is
// random and should be kept so worlds keep the player's inventory.
Account MakeOfflineAccount(const std::string& name);

// 3-16 characters of letters, digits and underscores, like Minecraft usernames.
bool IsValidPlayerName(const std::string& name);
