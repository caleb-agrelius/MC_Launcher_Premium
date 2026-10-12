#include "Services/Discover/OptionalModsService.h"

#include "Storage/Library.h"

#include <algorithm>

OptionalModsService::OptionalModsService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads)
	: m_rt(runtime), m_library(library), m_downloads(downloads), m_scope(runtime.tasks)
{
}

void OptionalModsService::SetOptional(std::size_t index, bool on)
{
	if (index >= m_picker.mods.size() || (m_picker.chosen[index] != 0) == on)
		return;
	m_picker.chosen[index] = on ? 1 : 0;
	const OptionalMod& mod = m_picker.mods[index];
	for (std::size_t i = 0; i < m_picker.mods.size(); ++i) {
		const OptionalMod& other = m_picker.mods[i];
		if (on && i != index && !mod.group.empty() && other.group == mod.group)
			SetOptional(i, false);
		if (on && std::find(mod.depends.begin(), mod.depends.end(), other.name) != mod.depends.end())
			SetOptional(i, true);
		if (!on && std::find(other.depends.begin(), other.depends.end(), mod.name) != other.depends.end())
			SetOptional(i, false);
	}
}

void OptionalModsService::Confirm()
{
	Modpack pack = m_picker.pack;
	pack.optionalMods.clear();
	for (std::size_t i = 0; i < m_picker.mods.size(); ++i) {
		if (m_picker.chosen[i])
			pack.optionalMods.push_back(m_picker.mods[i].name);
	}
	m_downloads.StartPlanInstall(pack);
	m_picker.pack = Modpack{};
}

void OptionalModsService::Cancel()
{
	m_picker.pack = Modpack{};
}

void OptionalModsService::RequestInstall(const Modpack& pack)
{
	if (pack.source != PackSource::ATLauncher) {
		m_downloads.StartPlanInstall(pack);
		return;
	}
	m_picker = OptionalPicker{ pack, true, {}, {}, {}, m_picker.generation + 1 };
	// A reinstall of this version starts from the choices made for it last time.
	const Modpack* saved = m_library.Find(pack.id);
	const InstalledVersion* savedVersion = saved ? FindInstalledVersion(*saved, VersionKey(pack)) : nullptr;
	const std::vector<std::string> previous = savedVersion ? savedVersion->optionalMods : std::vector<std::string>();
	const bool hadChoice = savedVersion != nullptr;
	m_scope.Run([this, pack, previous, hadChoice, generation = m_picker.generation]() {
		std::vector<OptionalMod> mods;
		std::string error;
		const bool ok = FetchOptionalMods(m_rt.http, pack, mods, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, pack, previous, hadChoice, generation, ok, mods, error]() {
			if (m_picker.generation != generation || m_picker.pack.id.empty())
				return; // cancelled or replaced
			m_picker.loading = false;
			if (!ok) {
				m_picker.error = error;
				return;
			}
			if (mods.empty()) {
				m_picker.pack = Modpack{};
				m_downloads.StartPlanInstall(pack);
				return;
			}
			m_picker.mods = mods;
			m_picker.chosen.assign(mods.size(), 0);
			for (std::size_t i = 0; i < mods.size(); ++i) {
				const bool wanted = hadChoice
					? std::find(previous.begin(), previous.end(), mods[i].name) != previous.end()
					: mods[i].selected;
				if (wanted)
					SetOptional(i, true);
			}
		});
	});
}
