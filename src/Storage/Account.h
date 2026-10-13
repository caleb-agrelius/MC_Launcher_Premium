#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The player identity passed to Minecraft at launch.
struct Account {
	enum class Type { None, Microsoft };
	Type type = Type::None;
	std::string name;        // in-game username
	std::string uuid;        // 32 hex digits, no dashes
	std::string accessToken; // Minecraft access token
	std::int64_t accessExpires = 0; // unix seconds; Microsoft only
	std::string refreshToken;       // Microsoft OAuth refresh token
	std::string xuid;               // Xbox user id; Microsoft only
};

// Every saved account, and which one Play uses.
struct AccountList {
	std::vector<Account> accounts;
	std::size_t active = 0; // index into accounts; ignored when accounts is empty

	// The active account, or a Type::None account when there are none.
	Account Active() const;
	// Adds an account, or replaces the saved one for the same player (same UUID), and makes
	// it active.
	void AddOrReplace(const Account& account);
	// Updates the saved account with this UUID (e.g. after a token refresh). Does nothing
	// if it was removed meanwhile.
	void Update(const Account& account);
	// Removes the account at index; the active one moves to a neighbor.
	void Remove(std::size_t index);
};

// account.dat in the data folder. On Windows the contents are encrypted with
// DPAPI, so only the same Windows user can read the saved tokens. Files from before
// multiple accounts (a single account) still load. Offline accounts saved by older
// versions are dropped. Missing or unreadable files give an empty list.
AccountList LoadAccounts(const std::string& path);
bool SaveAccounts(const std::string& path, const AccountList& list);

// Whether this account may launch Minecraft: a Microsoft account with a Minecraft access
// token, a refresh token and a profile. It says nothing about whether the token is still
// accepted; PlayService checks that with Minecraft services before every launch.
bool CanLaunchAs(const Account& account);
