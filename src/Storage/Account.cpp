#include "Account.h"
#include "Util/TracedFs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

using json = nlohmann::json;

static const char* TypeName(Account::Type type)
{
	switch (type) {
	case Account::Type::Microsoft: return "microsoft";
	default: return "none";
	}
}

#ifdef _WIN32
static bool Protect(const std::string& plain, std::string& out, bool encrypt)
{
	DATA_BLOB in{ static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data())) };
	DATA_BLOB result{};
	const BOOL ok = encrypt
		? CryptProtectData(&in, L"MC Launcher Premium account", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
		: CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result);
	if (!ok)
		return false;
	out.assign(reinterpret_cast<const char*>(result.pbData), result.cbData);
	LocalFree(result.pbData);
	return true;
}
#endif

static Account AccountFromJson(const json& j)
{
	Account account;
	const std::string type = j.value("type", "none");
	// Anything else (such as "offline" from older versions) can't be played as, so it isn't loaded.
	if (type == "microsoft")
		account.type = Account::Type::Microsoft;
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
	return a.type == b.type && a.uuid == b.uuid;
}

Account AccountList::Active() const
{
	return accounts.empty() ? Account{} : accounts[std::min(active, accounts.size() - 1)];
}

void AccountList::AddOrReplace(const Account& account)
{
	for (std::size_t i = 0; i < accounts.size(); ++i) {
		if (SamePlayer(accounts[i], account)) {
			accounts[i] = account;
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
#ifdef _WIN32
	std::string plain;
	if (!Protect(data, plain, false))
		return list;
	data.swap(plain);
#endif
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
#ifdef _WIN32
	std::string encrypted;
	if (!Protect(data, encrypted, true))
		return false;
	data.swap(encrypted);
#endif
	auto ofs = TracedFs::OpenWrite(path, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open())
		return false;
	ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
	return ofs.good();
}

bool CanLaunchAs(const Account& account)
{
	return account.type == Account::Type::Microsoft && !account.name.empty() && !account.uuid.empty() &&
		!account.accessToken.empty() && account.accessToken != "0" && !account.refreshToken.empty();
}
