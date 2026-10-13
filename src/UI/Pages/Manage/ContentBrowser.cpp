#include "UI/Pages/Manage/ContentBrowser.h"

#include "Network/PackSources.h"
#include "UI/Common/Background.h"
#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using Theme::S;

ContentSearch ContentBrowser::BrowseSearch(const ManagePageContext& context) const
{
	ContentSearch search;
	search.kind = ContentModel::kKinds[m_kind];
	search.query = m_query;
	search.query.erase(0, search.query.find_first_not_of(' '));
	search.query.erase(search.query.find_last_not_of(' ') + 1);
	search.source = m_source;
	search.gameVersion = context.pack->gameVersion;
	// The pack's loader: the first of its loaders ("fabric", "forge, neoforge", ...).
	const std::string loaders = ToLower(context.pack->loaders);
	search.loader = loaders.substr(0, loaders.find_first_of(", "));
	return search;
}

void ContentBrowser::Search(const ManagePageContext& context)
{
	const ContentSearch search = BrowseSearch(context);
	const std::string key = std::to_string(static_cast<int>(search.source)) + "|" + std::to_string(static_cast<int>(search.kind)) +
		"|" + search.query + "|" + search.gameVersion + "|" + search.loader;
	if (key == m_searchedFor || !context.http)
		return;
	m_searchedFor = key;
	m_searching = true;
	m_searchError.clear();
	const int generation = ++m_generation;
	IHttpClient* http = context.http;
	struct Found {
		std::vector<ContentProject> projects;
		std::string error;
		bool ok = false;
	};
	RunInBackground(context.tasks, [http, search]() {
		Found found;
		found.ok = SearchContent(*http, search, 30, found.projects, &found.error);
		return found;
	}, [this, generation](Found found) {
		if (generation != m_generation)
			return;
		m_searching = false;
		m_results = std::move(found.projects);
		m_searchError = found.ok ? std::string() : found.error;
	});
}

void ContentBrowser::LoadInstalledProjects(const ManagePageContext& context)
{
	if (!context.http)
		return;
	if (m_installedFor != context.gameDir.u8string()) { // another pack is shown now
		m_installedFor = context.gameDir.u8string();
		m_installedProjects.clear();
		m_installedLoaded.clear();
	}
	// Read once per pack and source, when that source is first shown (CurseForge hashes every file).
	if (!m_installedLoaded.insert(m_source).second)
		return;
	IHttpClient* http = context.http;
	const fs::path gameDir = context.gameDir;
	const ContentSource source = m_source;
	const std::string forDir = m_installedFor;
	RunInBackground(context.tasks, [http, gameDir, source]() {
		std::set<std::string> projects;
		for (ContentKind kind : ContentModel::kKinds)
			FindInstalledProjects(*http, gameDir / GetContentKind(kind).folder, projects, nullptr, nullptr, source);
		return projects;
	}, [this, forDir](std::set<std::string> projects) {
		if (forDir == m_installedFor)
			m_installedProjects.insert(projects.begin(), projects.end());
	});
}

void ContentBrowser::InstallProject(const ManagePageContext& context, const ContentProject& project)
{
	if (m_installing.count(project.id) || !context.http)
		return;
	m_installing.insert(project.id);
	m_installErrors.erase(project.id);
	const ContentSearch search = BrowseSearch(context);
	IHttpClient* http = context.http;
	auto recordChange = context.recordChange;
	const fs::path gameDir = context.gameDir;
	const std::string forDir = context.gameDir.u8string();
	struct Installed {
		std::vector<AddedContent> added;
		std::vector<std::string> notes;
		std::string error;
		bool ok = false;
	};
	RunInBackground(context.tasks, [http, search, gameDir, id = project.id]() {
		Installed result;
		result.ok = InstallContent(*http, search, id, gameDir, result.added, result.notes, &result.error);
		return result;
	}, [this, recordChange, forDir, project](Installed result) {
		m_installing.erase(project.id);
		if (!result.ok) {
			m_installErrors[project.id] = result.error;
			return;
		}
		for (const AddedContent& file : result.added) {
			if (recordChange)
				recordChange(ContentPath(file.kind, file.fileName), ContentChange::Added);
		}
		if (forDir != m_installedFor)
			return; // another pack is shown now
		m_installedProjects.insert(project.id);
		for (const AddedContent& file : result.added)
			m_installedProjects.insert(file.projectId); // the mods it needed, too
		const std::size_t required = result.added.empty() ? 0 : result.added.size() - 1;
		m_model.notice = result.added.empty() ? project.name + " is already in this pack."
			: "Added " + project.name + (required == 0 ? std::string(".")
				: " and " + std::to_string(required) + " mod" + (required == 1 ? "" : "s") + " it needs.");
		std::string noteText;
		for (const std::string& note : result.notes)
			noteText += (noteText.empty() ? "" : "\n") + note;
		m_model.error = noteText;
		m_model.reloadRequested = true;
	});
}

bool ContentBrowser::DrawSourceChips()
{
	std::string reason;
	const bool curseForgeAvailable = IsSourceAvailable(PackSource::CurseForge, &reason);
	ContentSource picked = m_source;
	if (!curseForgeAvailable)
		picked = ContentSource::Modrinth; // the key was removed while CurseForge was shown
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Source");
	ImGui::SameLine();
	if (Widgets::ToggleChip("Modrinth", picked == ContentSource::Modrinth, UiHelpers::SourceColor(PackSource::Modrinth)))
		picked = ContentSource::Modrinth;
	ImGui::SameLine();
	if (Widgets::ToggleChip("CurseForge", picked == ContentSource::CurseForge, UiHelpers::SourceColor(PackSource::CurseForge), curseForgeAvailable))
		picked = ContentSource::CurseForge;
	if (!curseForgeAvailable)
		ImGui::SetItemTooltip("%s", reason.c_str());
	if (picked == m_source)
		return false;
	m_source = picked;
	m_results.clear();
	m_searchError.clear();
	return true;
}

bool ContentBrowser::Draw(const ManagePageContext& context, int kind)
{
	m_kind = kind;
	DrawSourceChips();
	const ContentSearch search = BrowseSearch(context);
	const ContentKindInfo& info = GetContentKind(search.kind);
	const char* sourceName = ContentSourceName(m_source);
	const float back = Widgets::ButtonWidth("Back to installed") + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - back);
	if (ImGui::InputTextWithHint("##browse_query", (std::string("Search ") + sourceName + " for " + ToLower(info.label) + "...").c_str(), &m_query))
		m_editedAt = ImGui::GetTime();
	ImGui::SameLine();
	if (ImGui::Button("Back to installed")) {
		return false;
	}
	std::string scope = "Showing " + ToLower(info.label);
	if (!search.gameVersion.empty())
		scope += " for Minecraft " + search.gameVersion;
	if (search.kind == ContentKind::Mods && !search.loader.empty())
		scope += " on " + search.loader;
	scope += search.kind == ContentKind::Mods ? ". Required mods are added too." : ".";
	ImGui::TextDisabled("%s", scope.c_str());
	if (context.playBusy)
		ImGui::TextDisabled("Close the game to add content.");
	m_model.DrawMessages();

	// Search a moment after typing stops.
	if (ImGui::GetTime() - m_editedAt > 0.4)
		Search(context);
	LoadInstalledProjects(context);

	if (m_searching) {
		UiHelpers::BusyLabel((std::string("Searching ") + sourceName + "...").c_str());
	} else if (!m_searchError.empty()) {
		Widgets::WrappedText(Theme::C().error, (std::string("Couldn't search ") + sourceName + ": " + m_searchError).c_str());
		if (ImGui::Button("Retry"))
			m_searchedFor.clear();
		return true;
	}

	Widgets::BeginCard("browse_list", ImVec2(S(14.0f), S(10.0f)));
	const float iconSize = S(44.0f);
	int shown = 0;
	for (const ContentProject& project : m_results) {
		ImGui::PushID(project.id.c_str());
		if (shown++ > 0)
			ImGui::Separator();
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		Modpack iconPack; // for the placeholder tile's color and letter
		iconPack.id = project.id;
		iconPack.name = project.name;
		Widgets::DrawPackIcon(ImGui::GetWindowDrawList(), context.images ? context.images->Get(project.iconUrl) : nullptr,
			iconPack, rowStart, iconSize, S(8.0f));
		ImGui::SetCursorScreenPos(ImVec2(rowStart.x + iconSize + S(12.0f), rowStart.y));
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - Widgets::ButtonWidth("Installing...") - S(24.0f));
		ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
		ImGui::TextUnformatted(project.name.c_str());
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		ImGui::TextLinkOpenURL("Page", project.pageUrl.c_str());
		ImGui::TextDisabled("by %s  \xC2\xB7  %s downloads", project.author.c_str(), FormatCount(project.downloads).c_str());
		if (!project.summary.empty())
			Widgets::WrappedText(Theme::C().textDim, project.summary.c_str());
		const auto error = m_installErrors.find(project.id);
		if (error != m_installErrors.end())
			Widgets::WrappedText(Theme::C().error, error->second.c_str());
		ImGui::PopFont();
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		const float rowHeight = std::max(ImGui::GetItemRectMax().y - rowStart.y, iconSize);
		const float frameHeight = ImGui::GetFrameHeight();
		const float controlsY = rowStart.y + std::floor((rowHeight - frameHeight) * 0.5f);

		// Install, or what's happening with it.
		const bool installing = m_installing.count(project.id) > 0;
		const bool installed = m_installedProjects.count(project.id) > 0;
		const char* label = installing ? "Installing..." : installed ? "Installed" : "Install";
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - Widgets::ButtonWidth(label),
			controlsY));
		if (installed && !installing) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(Theme::C().accent, "%s", label);
		} else {
			ImGui::BeginDisabled(installing || context.playBusy);
			if (Widgets::PrimaryButton(label))
				InstallProject(context, project);
			ImGui::EndDisabled();
		}
		ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + rowHeight));
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
		ImGui::PopID();
	}
	if (m_results.empty() && !m_searching)
		ImGui::TextDisabled("Nothing on %s matches for this pack's Minecraft version%s.", sourceName,
			search.kind == ContentKind::Mods && !search.loader.empty() ? " and loader" : "");
	Widgets::EndCard();
	return true;
}
