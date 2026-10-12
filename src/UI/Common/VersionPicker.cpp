#include "UI/Common/VersionPicker.h"

#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui.h"

namespace {

// The details after a version in a dropdown row, e.g. "Minecraft 1.20.1 - beta - 2024-05-01".
std::string VersionDetails(const PackVersionInfo& v)
{
	std::string details;
	auto add = [&](const std::string& part) {
		if (!part.empty())
			details += (details.empty() ? "" : "  \xC2\xB7  ") + part;
	};
	add(v.gameVersion.empty() ? std::string() : "Minecraft " + v.gameVersion);
	add(v.versionType == "release" ? std::string() : v.versionType);
	add(v.published);
	return details;
}

}

bool VersionPicker::Combo(const char* id, const Modpack& pack, const std::string& preview, const std::string& currentId,
	PackVersionInfo& picked, const Modpack* libraryPack)
{
	static const ImVec4 kInstalledGreen(0.30f, 0.80f, 0.42f, 1.0f);
	bool chosen = false;
	if (Widgets::BeginSelect(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
		VersionList& list = m_details.Versions(pack.id);
		if (!list.loaded && !list.loading && list.error.empty())
			m_details.ListVersions(pack);
		if (list.loading) {
			Widgets::Spinner(Theme::C().accent);
			ImGui::SameLine();
			ImGui::TextDisabled("Loading versions...");
		} else if (!list.error.empty()) {
			ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", list.error.c_str());
			if (Widgets::ListItem("Retry", false, false))
				m_details.ListVersions(pack);
		}
		for (std::size_t i = 0; i < list.versions.size(); ++i) {
			const PackVersionInfo& v = list.versions[i];
			ImGui::PushID(static_cast<int>(i));
			const bool current = !currentId.empty() && v.sourceVersionId == currentId;
			bool installed = false;
			if (libraryPack) {
				for (const InstalledVersion& have : libraryPack->installedVersions) {
					installed = installed || (have.sourceVersionId.empty() ? have.version == v.versionNumber
						: have.sourceVersionId == v.sourceVersionId);
				}
			}
			const std::string details = (i == 0 ? std::string("newest  \xC2\xB7  ") : std::string())
				+ (installed ? std::string("installed  \xC2\xB7  ") : std::string()) + VersionDetails(v);
			if (Widgets::ListItem(v.versionNumber.empty() ? v.sourceVersionId.c_str() : v.versionNumber.c_str(), current,
					true, details.c_str(), installed ? &kInstalledGreen : nullptr)) {
				picked = v;
				chosen = true;
			}
			if (current)
				ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		Widgets::EndSelect();
	}
	return chosen;
}

void VersionPicker::Apply(Modpack& target, const PackVersionInfo& v)
{
	target.gameVersion = v.gameVersion;
	ApplyPackVersion(target, v);
}

bool VersionPicker::IsInstalled(const Modpack& pack, const PackVersionInfo& v)
{
	return pack.sourceVersionId.empty() ? v.versionNumber == pack.version : v.sourceVersionId == pack.sourceVersionId;
}
