#include "Services/Library/CustomPackService.h"

#include "Game/PackCreator.h"
#include "Services/AppPaths.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

CustomPackService::CustomPackService(const Runtime& runtime, PackLibrary& library, AccountService& accounts)
	: m_rt(runtime), m_library(library), m_accounts(accounts)
{
}

std::string CustomPackService::Create(const CustomPackSpec& spec)
{
	// Ids already used by the library or by folders left on disk.
	std::vector<std::string> taken;
	for (const auto& [id, existing] : m_library.Packs())
		taken.push_back(id);
	std::error_code listError;
	for (const char* root : { AppPaths::kInstallsRoot, AppPaths::kInstancesRoot, AppPaths::kServersRoot }) {
		for (const auto& entry : fs::directory_iterator(root, listError)) {
			const std::string name = entry.path().filename().u8string();
			taken.push_back(name.substr(0, name.find('@')));
		}
	}
	Modpack pack;
	pack.id = CustomPackId(spec.name, taken);
	pack.name = spec.name;
	pack.version = "1.0";
	pack.source = PackSource::Custom;
	pack.gameVersion = spec.minecraftVersion;
	pack.loaders = spec.loader;
	pack.author = m_accounts.Active().name;
	pack.description = "Your own pack for Minecraft " + spec.minecraftVersion +
		(spec.loader.empty() ? std::string() : " with " + spec.loader + " " + spec.loaderVersion) + ".";
	pack.filesLoaded = true;
	const InstalledVersion version = PlanInstalledVersion(m_library.Packs(), pack, true);
	std::string error;
	if (!WriteCustomPack(fs::u8path(version.installDir), pack.version, spec.minecraftVersion, spec.loader,
			spec.loaderVersion, &error)) {
		m_rt.notify.Notify(NoticeKind::Error, "Couldn't create the pack", error);
		return {};
	}
	AddInstalledVersion(m_library.Packs(), pack, version);
	m_library.Save();
	m_rt.log << "Created pack " << pack.id << " (Minecraft " << spec.minecraftVersion << ", " <<
		(spec.loader.empty() ? std::string("vanilla") : spec.loader + " " + spec.loaderVersion) << ")\n";
	m_rt.notify.Notify(NoticeKind::Success, "Pack created", "Add mods from Modrinth or your own files.");
	return pack.id;
}
