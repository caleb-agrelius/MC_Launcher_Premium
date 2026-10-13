#include "Account.h"
#include "Platform/Platform.h"
#include "Util/TracedFs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

using json = nlohmann::json;

static const char* TypeName(Account::Type type)
{
	switch (type) {
	case Account::Type::Microsoft: return "microsoft";
	case Account::Type::Offline: return "offline";
	default: return "none";
	}
}

static Account AccountFromJson(const json& j)
{
	Account account;
	const std::string type = j.value("type", "none");
	if (type == "microsoft")
		account.type = Account::Type::Microsoft;
	else if (type == "offline")
		account.type = Account::Type::Offline;
	account.name = j.value("name", "");
	account.uuid = j.value("uuid", "");
	account.accessToken = j.value("accessToken", "");
	account.accessExpires = j.value("accessExpires", std::int64_t{ 0 });
	account.refreshToken = j.value("refreshToken", "");
	account.xuid = j.value("xuid", "");
	if (account.type == Account::Type::None || account.name.empty() || account.uuid.empty())
		return Account{};
	return account;
}

static json AccountToJson(const Account& account)
{
	return {
		{ "type", TypeName(account.type) },
		{ "name", account.name },
		{ "uuid", account.uuid },
		{ "accessToken", account.accessToken },
		{ "accessExpires", account.accessExpires },
		{ "refreshToken", account.refreshToken },
		{ "xuid", account.xuid },
	};
}

static bool SamePlayer(const Account& a, const Account& b)
{
	if (a.type != b.type)
		return false;
	// Offline accounts get a new random UUID each time, so the name identifies them.
	return a.type == Account::Type::Offline ? a.name == b.name : a.uuid == b.uuid;
}

Account AccountList::Active() const
{
	return accounts.empty() ? Account{} : accounts[std::min(active, accounts.size() - 1)];
}

void AccountList::AddOrReplace(const Account& account)
{
	for (std::size_t i = 0; i < accounts.size(); ++i) {
		if (SamePlayer(accounts[i], account)) {
			// Keep an offline player's UUID, so their worlds still know them.
			const std::string uuid = accounts[i].uuid;
			accounts[i] = account;
			if (account.type == Account::Type::Offline)
				accounts[i].uuid = uuid;
			active = i;
			return;
		}
	}
	accounts.push_back(account);
	active = accounts.size() - 1;
}

void AccountList::Update(const Account& account)
{
	for (Account& saved : accounts) {
		if (saved.type == account.type && saved.uuid == account.uuid)
			saved = account;
	}
}

void AccountList::Remove(std::size_t index)
{
	if (index >= accounts.size())
		return;
	accounts.erase(accounts.begin() + static_cast<std::ptrdiff_t>(index));
	if (active > index || active >= accounts.size())
		active = active > 0 ? active - 1 : 0;
}

AccountList LoadAccounts(const std::string& path)
{
	AccountList list;
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs.is_open())
		return list;
	std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	std::string plain;
	if (!Platform::UnprotectSecret(data, plain))
		return list;
	data.swap(plain);
	try {
		const json j = json::parse(data);
		if (j.contains("accounts")) {
			for (const auto& item : j["accounts"]) {
				const Account account = AccountFromJson(item);
				if (account.type != Account::Type::None)
					list.accounts.push_back(account);
			}
			list.active = std::min<std::size_t>(j.value("active", std::size_t{ 0 }),
				list.accounts.empty() ? 0 : list.accounts.size() - 1);
		} else {
			// A single account, written before multiple accounts existed.
			const Account account = AccountFromJson(j);
			if (account.type != Account::Type::None)
				list.accounts.push_back(account);
		}
	}
	catch (...) {
		list = AccountList{};
	}
	return list;
}

bool SaveAccounts(const std::string& path, const AccountList& list)
{
	json accounts = json::array();
	for (const Account& account : list.accounts)
		accounts.push_back(AccountToJson(account));
	std::string data = json{ { "active", list.active }, { "accounts", accounts } }.dump();
	std::string encrypted;
	if (!Platform::ProtectSecret(data, encrypted))
		return false;
	data.swap(encrypted);
	auto ofs = TracedFs::OpenWrite(path, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open())
		return false;
	// Created with the user's umask; where there is no per-user encryption, only the owner may read it.
	Platform::RestrictToOwner(path);
	ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
	return ofs.good();
}

Account MakeOfflineAccount(const std::string& name)
{
	std::random_device rd;
	std::uniform_int_distribution<int> nibble(0, 15);
	char uuid[33];
	for (int i = 0; i < 32; ++i)
		std::snprintf(uuid + i, 2, "%x", nibble(rd));
	uuid[12] = '4'; // version 4 (random)
	uuid[16] = "89ab"[nibble(rd) & 3]; // RFC 4122 variant

	Account account;
	account.type = Account::Type::Offline;
	account.name = name;
	account.uuid = uuid;
	account.accessToken = "0";
	return account;
}

bool IsValidPlayerName(const std::string& name)
{
	if (name.size() < 3 || name.size() > 16)
		return false;
	for (unsigned char c : name) {
		if (!std::isalnum(c) && c != '_')
			return false;
	}
	return true;
}
