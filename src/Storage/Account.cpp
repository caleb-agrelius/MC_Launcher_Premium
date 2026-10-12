#include "Account.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
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
	case Account::Type::Offline: return "offline";
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

Account LoadAccount(const std::string& path)
{
	Account account;
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs.is_open())
		return account;
	std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
#ifdef _WIN32
	std::string plain;
	if (!Protect(data, plain, false))
		return account;
	data.swap(plain);
#endif
	try {
		const json j = json::parse(data);
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
		if (account.name.empty() || account.uuid.empty())
			account = Account{};
	}
	catch (...) {
		account = Account{};
	}
	return account;
}

bool SaveAccount(const std::string& path, const Account& account)
{
	const json j = {
		{ "type", TypeName(account.type) },
		{ "name", account.name },
		{ "uuid", account.uuid },
		{ "accessToken", account.accessToken },
		{ "accessExpires", account.accessExpires },
		{ "refreshToken", account.refreshToken },
		{ "xuid", account.xuid },
	};
	std::string data = j.dump();
#ifdef _WIN32
	std::string encrypted;
	if (!Protect(data, encrypted, true))
		return false;
	data.swap(encrypted);
#endif
	std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open())
		return false;
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
