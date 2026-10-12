#include "UI/Pages/Discover/PackInfoTabs.h"

#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <cfloat>

using Theme::S;

bool PackInfoTabs::Draw(const Modpack& pack)
{
	const std::string modsKey = PackDetailsService::ModsKey(pack);
	const PackModList* modsForTab = m_details.Mods(modsKey);
	const std::string modsTabLabel = modsForTab && !modsForTab->mods.empty()
		? "Mods (" + std::to_string(modsForTab->mods.size()) + ")" : std::string("Mods");
	const char* tabLabels[] = { "Description", modsTabLabel.c_str() };
	const bool changed = Widgets::TabStrip("pack_tabs", tabLabels, 2, &m_tab);
	ImGui::Spacing();
	if (m_tab == 0)
		DrawDescription(pack);
	else
		DrawMods(pack);
	return changed;
}

void PackInfoTabs::DrawDescription(const Modpack& pack)
{
	const std::string* description = m_details.Description(pack.id);
	const std::string* descriptionError = m_details.DescriptionError(pack.id);
	const bool loadingDescription = m_details.DescriptionLoading(pack.id);
	if (!description && !loadingDescription && !descriptionError)
		m_details.FetchDescription(pack);
	if (loadingDescription) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading description...");
	} else if (descriptionError) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't load the description: " + *descriptionError).c_str());
		if (ImGui::Button("Retry##description"))
			m_details.FetchDescription(pack);
	} else if (description) {
		ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(description->empty() ? "This pack has no description." : description->c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
	}
}

void PackInfoTabs::DrawMods(const Modpack& pack)
{
	const Theme::Fonts& fonts = Theme::GetFonts();
	const std::string key = PackDetailsService::ModsKey(pack);
	const PackModList* list = m_details.Mods(key);
	const bool loadingMods = m_details.ModsLoading(key);
	const std::string* modsError = m_details.ModsError(key);
	// CurseForge only lists mods in the pack zip, so it waits for a click.
	const bool needsClick = pack.source == PackSource::CurseForge;
	if (pack.filesLoaded && !loadingMods && !list && !modsError && !needsClick && !pack.sourceVersionId.empty())
		m_details.FetchMods(pack);
	if (!pack.filesLoaded || loadingMods) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled(!pack.filesLoaded ? "Loading the pack's version..."
			: needsClick ? "Downloading the pack's manifest..." : "Loading mod list...");
	} else if (modsError) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't load the mod list: " + *modsError).c_str());
		if (ImGui::Button("Retry##mods"))
			m_details.FetchMods(pack);
	} else if (!list) {
		ImGui::TextDisabled("CurseForge lists a pack's mods in its archive.");
		if (ImGui::Button("Load mod list"))
			m_details.FetchMods(pack);
		ImGui::SetItemTooltip("Downloads the pack's archive to read its manifest. Installing reuses it.");
	} else {
		const PackModList& mods = *list;
		if (!mods.note.empty())
			ImGui::TextDisabled("%s", mods.note.c_str());
		if (mods.mods.empty()) {
			if (mods.note.empty())
				ImGui::TextDisabled("This version doesn't list any mods.");
			return;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##mods_filter", "Filter mods...", &m_modsFilter);
		const std::string filter = ToLower(m_modsFilter);
		int shown = 0;
		for (std::size_t i = 0; i < mods.mods.size(); ++i) {
			const PackMod& mod = mods.mods[i];
			if (!filter.empty() && !ContainsIgnoreCase(mod.name, filter) && !ContainsIgnoreCase(mod.summary, filter))
				continue;
			++shown;
			ImGui::PushID(static_cast<int>(i));
			ImGui::PushFont(fonts.bold, 0.0f);
			if (!mod.url.empty())
				ImGui::TextLinkOpenURL(mod.name.c_str(), mod.url.c_str());
			else
				ImGui::TextUnformatted(mod.name.c_str());
			ImGui::PopFont();
			if (!mod.version.empty() && mod.version != mod.name) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", mod.version.c_str());
			}
			auto tag = [&](const char* text, const ImVec4& tint) {
				ImGui::SameLine();
				Widgets::Chip(text, tint);
			};
			if (mod.clientOnly)
				tag("Client only", Theme::C().info);
			if (mod.serverOnly)
				tag("Server only", Theme::C().info);
			if (mod.optional)
				tag("Optional", Theme::C().textDim);
			if (!mod.summary.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
				ImGui::TextWrapped("%s", mod.summary.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			ImGui::PopID();
		}
		if (shown == 0)
			ImGui::TextDisabled("No mods match the filter.");
	}
}
