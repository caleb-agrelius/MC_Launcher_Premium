#pragma once

#include "Core/Runtime.h"
#include "Services/Account/AccountService.h"
#include "Services/Library/PackLibrary.h"

#include <string>

// What the player chose for a pack of their own (My Library > New pack...).
struct CustomPackSpec {
	std::string name;
	std::string minecraftVersion;
	std::string loader;        // "fabric", "quilt", "forge", "neoforge", or empty for vanilla
	std::string loaderVersion;
};

// Makes packs the player builds themselves: a whole-pack install with no files yet, to which
// mods are added from Manage > Content. UI thread only.
class CustomPackService {
public:
	CustomPackService(const Runtime& runtime, PackLibrary& library, AccountService& accounts);

	// Creates the pack and adds it to My Library. Returns its id, or empty (after telling the
	// player why) if it couldn't be created.
	std::string Create(const CustomPackSpec& spec);

private:
	Runtime m_rt;
	PackLibrary& m_library;
	AccountService& m_accounts;
};
