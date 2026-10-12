#include "ManagePage.h"

#include "Network/PackSources.h"
#include "Platform.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_internal.h" // IsKeyPressed for keys keyboard navigation also uses
#include "imgui_stdlib.h"

#include <miniz/miniz.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>

namespace fs = std::filesystem;
using Theme::S;

namespace {

enum Tab { kOverview, kContent, kChangelog, kLogs, kScreenshots, kTabCount };
constexpr ContentKind kKinds[] = { ContentKind::Mods, ContentKind::ResourcePacks, ContentKind::ShaderPacks };
constexpr std::uint64_t kMaxLogBytes = 24ull * 1024 * 1024; // longer logs show their end

// A file time as local "YYYY-MM-DD HH:MM".
std::string FormatFileTime(fs::file_time_type time)
{
	const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
	const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &seconds);
#else
	localtime_r(&seconds, &local);
#endif
	char text[32];
	std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
	return text;
}

// Reads a log file, unzipping .gz logs. Very long logs keep their last kMaxLogBytes.
bool ReadLogText(const fs::path& path, std::string& out, std::string* err)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		if (err) *err = "Couldn't open " + path.filename().u8string();
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::uint64_t size = static_cast<std::uint64_t>(in.tellg());
	const bool gzip = ToLower(path.extension().u8string()) == ".gz";
	const std::uint64_t start = !gzip && size > kMaxLogBytes ? size - kMaxLogBytes : 0;
	in.seekg(static_cast<std::streamoff>(start));
	std::string data(std::istreambuf_iterator<char>(in), {});
	if (!gzip) {
		out = std::move(data);
		return true;
	}
	// gzip: a 10-byte header, optional fields, then raw deflate data and an 8-byte trailer.
	if (data.size() < 18 || static_cast<unsigned char>(data[0]) != 0x1F || static_cast<unsigned char>(data[1]) != 0x8B) {
		if (err) *err = "Not a gzip file";
		return false;
	}
	const unsigned char flags = static_cast<unsigned char>(data[3]);
	std::size_t pos = 10;
	if (flags & 4)
		pos += 2 + (static_cast<unsigned char>(data[pos]) | (static_cast<unsigned char>(data[pos + 1]) << 8));
	if (flags & 8)
		pos = data.find('\0', pos) + 1;
	if (flags & 16)
		pos = data.find('\0', pos) + 1;
	if (flags & 2)
		pos += 2;
	if (pos == 0 || pos >= data.size() - 8) {
		if (err) *err = "Damaged gzip file";
		return false;
	}
	std::size_t length = 0;
	void* inflated = tinfl_decompress_mem_to_heap(data.data() + pos, data.size() - 8 - pos, &length, 0);
	if (!inflated) {
		if (err) *err = "Couldn't unpack " + path.filename().u8string();
		return false;
	}
	out.assign(static_cast<const char*>(inflated), length);
	mz_free(inflated);
	if (out.size() > kMaxLogBytes)
		out.erase(0, out.size() - kMaxLogBytes);
	return true;
}

std::vector<std::string> SplitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		// Tabs (stack traces) don't render in ImGui fonts.
		for (std::size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', tab))
			line.replace(tab, 1, "    ");
		lines.push_back(std::move(line));
		start = end + 1;
	}
	return lines;
}

}

void ManagePage::Reload(const ManagePageContext& context)
{
	++m_generation;
	m_loadedFor = context.pack->id + "|" + context.gameDir.u8string();
	for (int i = 0; i < 3; ++i)
		m_content[i].files = ListContent(context.gameDir, kKinds[i]);
	m_contentError.clear();
	m_contentNotice.clear();
	m_removeName.clear();
	m_confirmUndo = false;
	++m_scanGeneration;
	if (m_installedFor != context.gameDir.u8string()) {
		m_installedProjects.clear();
		m_installedFor.clear();
		m_installErrors.clear();
		m_searchedFor.clear(); // the pack's Minecraft version or loader may differ
	}

	std::error_code ec;
	m_worlds = 0;
	for (const auto& entry : fs::directory_iterator(context.gameDir / "saves", ec)) {
		if (entry.is_directory(ec))
			++m_worlds;
	}
	const fs::path latest = context.gameDir / "logs" / "latest.log";
	m_lastPlayed = fs::is_regular_file(latest, ec) ? FormatFileTime(fs::last_write_time(latest, ec)) : std::string();

	// Logs: latest first, then the launcher's own output, older logs and crash reports (newest first).
	m_logs.clear();
	auto addLogs = [&](const fs::path& folder, const std::string& prefix, std::initializer_list<const char*> extensions) {
		std::vector<std::pair<fs::file_time_type, LogFile>> found;
		for (const auto& entry : fs::directory_iterator(folder, ec)) {
			if (!entry.is_regular_file(ec))
				continue;
			const std::string name = entry.path().filename().u8string();
			const std::string lower = ToLower(name);
			bool match = false;
			for (const char* extension : extensions)
				match = match || (lower.size() > std::strlen(extension) && lower.compare(lower.size() - std::strlen(extension), std::string::npos, extension) == 0);
			if (!match)
				continue;
			found.push_back({ entry.last_write_time(ec), LogFile{ prefix + name, entry.path(), entry.file_size(ec) } });
		}
		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
			if ((a.second.label.find("latest.log") != std::string::npos) != (b.second.label.find("latest.log") != std::string::npos))
				return a.second.label.find("latest.log") != std::string::npos;
			return a.first > b.first;
		});
		for (auto& [time, log] : found)
			m_logs.push_back(std::move(log));
	};
	addLogs(context.gameDir / "logs", "", { ".log", ".log.gz" });
	addLogs(context.gameDir / "crash-reports", "crash-reports/", { ".txt" });
	m_logIndex = 0;
	m_logLines.clear();
	m_shownLines.clear();
	m_shownFilter.clear();
	m_logError.clear();
	m_logLoading = false;
	m_logView.ClearSelection();

	m_screenshots.clear();
	std::vector<std::pair<fs::file_time_type, Screenshot>> shots;
	for (const auto& entry : fs::directory_iterator(context.gameDir / "screenshots", ec)) {
		const std::string lower = ToLower(entry.path().extension().u8string());
		if (entry.is_regular_file(ec) && (lower == ".png" || lower == ".jpg" || lower == ".jpeg"))
			shots.push_back({ entry.last_write_time(ec), Screenshot{ entry.path(), entry.path().filename().u8string() } });
	}
	std::sort(shots.begin(), shots.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	for (auto& [time, shot] : shots)
		m_screenshots.push_back(std::move(shot));
	m_viewing = -1;

	m_metadata.reset();
	m_scanning = false;
	ScanMetadata(context);
	if (m_tab == kLogs && !m_logs.empty())
		LoadLog(context, 0);
}

void ManagePage::ShowBrowse()
{
	m_tab = kContent;
	m_contentKind = 0;
	m_browsing = true;
	m_tabShownAt = ImGui::GetTime();
}

ContentSearch ManagePage::BrowseSearch(const ManagePageContext& context) const
{
	ContentSearch search;
	search.kind = kKinds[m_contentKind];
	search.query = m_browseQuery;
	search.query.erase(0, search.query.find_first_not_of(' '));
	search.query.erase(search.query.find_last_not_of(' ') + 1);
	search.gameVersion = context.pack->gameVersion;
	// The pack's loader: the first of its loaders ("fabric", "forge, neoforge", ...).
	const std::string loaders = ToLower(context.pack->loaders);
	search.loader = loaders.substr(0, loaders.find_first_of(", "));
	return search;
}

void ManagePage::SearchModrinth(const ManagePageContext& context)
{
	const ContentSearch search = BrowseSearch(context);
	const std::string key = std::to_string(static_cast<int>(search.kind)) + "|" + search.query + "|" + search.gameVersion + "|" + search.loader;
	if (key == m_searchedFor || !context.tasks.run || !context.http)
		return;
	m_searchedFor = key;
	m_searching = true;
	m_searchError.clear();
	const int generation = ++m_browseGeneration;
	HttpClient* http = context.http;
	auto post = context.tasks.post;
	context.tasks.run([this, http, post, search, generation]() {
		auto results = std::make_shared<std::vector<ContentProject>>();
		auto error = std::make_shared<std::string>();
		const bool ok = SearchContent(*http, search, 30, *results, error.get());
		post([this, results, error, ok, generation]() {
			if (generation != m_browseGeneration)
				return;
			m_searching = false;
			m_results = *results;
			m_searchError = ok ? std::string() : *error;
		});
	});
}

void ManagePage::LoadInstalledProjects(const ManagePageContext& context)
{
	if (m_installedFor == context.gameDir.u8string() || !context.tasks.run || !context.http)
		return;
	m_installedFor = context.gameDir.u8string();
	HttpClient* http = context.http;
	auto post = context.tasks.post;
	const fs::path gameDir = context.gameDir;
	const std::string forDir = m_installedFor;
	context.tasks.run([this, http, post, gameDir, forDir]() {
		auto projects = std::make_shared<std::set<std::string>>();
		for (ContentKind kind : kKinds)
			FindInstalledProjects(*http, gameDir / GetContentKind(kind).folder, *projects);
		post([this, projects, forDir]() {
			if (forDir == m_installedFor)
				m_installedProjects.insert(projects->begin(), projects->end());
		});
	});
}

void ManagePage::InstallProject(const ManagePageContext& context, const ContentProject& project)
{
	if (m_installing.count(project.id) || !context.tasks.run || !context.http)
		return;
	m_installing.insert(project.id);
	m_installErrors.erase(project.id);
	const ContentSearch search = BrowseSearch(context);
	HttpClient* http = context.http;
	auto post = context.tasks.post;
	auto recordChange = context.recordChange;
	const fs::path gameDir = context.gameDir;
	const std::string forDir = context.gameDir.u8string();
	context.tasks.run([this, http, post, recordChange, search, gameDir, forDir, project]() {
		auto added = std::make_shared<std::vector<AddedContent>>();
		auto notes = std::make_shared<std::vector<std::string>>();
		auto error = std::make_shared<std::string>();
		const bool ok = InstallContent(*http, search, project.id, gameDir, *added, *notes, error.get());
		post([this, recordChange, added, notes, error, ok, forDir, project]() {
			m_installing.erase(project.id);
			if (!ok) {
				m_installErrors[project.id] = *error;
				return;
			}
			for (const AddedContent& file : *added) {
				if (recordChange)
					recordChange(ContentPath(file.kind, file.fileName), ContentChange::Added);
			}
			if (forDir != m_installedFor)
				return; // another pack is shown now
			m_installedProjects.insert(project.id);
			for (const AddedContent& file : *added)
				m_installedProjects.insert(file.projectId); // the mods it needed, too
			const std::size_t required = added->empty() ? 0 : added->size() - 1;
			m_contentNotice = added->empty() ? project.name + " is already in this pack."
				: "Added " + project.name + (required == 0 ? std::string(".")
					: " and " + std::to_string(required) + " mod" + (required == 1 ? "" : "s") + " it needs.");
			std::string noteText;
			for (const std::string& note : *notes)
				noteText += (noteText.empty() ? "" : "\n") + note;
			m_contentError = noteText;
			m_reloadContent = true;
		});
	});
}

void ManagePage::ReloadContent(const ManagePageContext& context)
{
	for (int i = 0; i < 3; ++i)
		m_content[i].files = ListContent(context.gameDir, kKinds[i]);
	m_removeName.clear();
	// Read the new mods' names; the old names stay shown until then.
	++m_scanGeneration;
	m_scanning = false;
	ScanMetadata(context);
}

void ManagePage::AddFiles(const ManagePageContext& context, const std::vector<fs::path>& paths)
{
	m_contentNotice.clear();
	if (context.playBusy) {
		m_contentError = "Close the game before adding files.";
		return;
	}
	std::vector<std::string> added;
	std::string errors;
	int shownKind = -1;
	for (const fs::path& path : paths) {
		ContentKind kind;
		std::string name, error;
		if (!ContentKindForFile(path, kKinds[m_contentKind], &kind)) {
			error = path.filename().u8string() + " isn't a mod (.jar), resource pack or shader pack (.zip or folder).";
		} else if (AddContent(context.gameDir, kind, path, &name, &error)) {
			added.push_back(name);
			if (context.recordChange)
				context.recordChange(ContentPath(kind, name), ContentChange::Added);
			if (shownKind < 0)
				shownKind = static_cast<int>(std::find(std::begin(kKinds), std::end(kKinds), kind) - std::begin(kKinds));
			continue;
		}
		errors += (errors.empty() ? "" : "\n") + error;
	}
	m_contentError = errors;
	if (!added.empty()) {
		m_contentNotice = added.size() == 1 ? "Added " + added.front() + "." : "Added " + std::to_string(added.size()) + " files.";
		// Show where they went.
		m_tab = kContent;
		m_contentKind = shownKind;
		m_contentFilter.clear();
		ReloadContent(context);
	}
}

void ManagePage::ScanMetadata(const ManagePageContext& context)
{
	if (m_scanning || !context.tasks.run)
		return;
	m_scanning = true;
	const fs::path folder = context.gameDir / GetContentKind(ContentKind::Mods).folder;
	std::vector<ContentFile> mods = m_content[0].files;
	const int generation = m_scanGeneration;
	auto post = context.tasks.post;
	context.tasks.run([this, folder, mods, generation, post]() {
		auto found = std::make_shared<std::map<std::string, ModMetadata>>();
		for (const ContentFile& mod : mods) {
			fs::path path = folder / fs::u8path(mod.fileName);
			if (!mod.enabled)
				path += ".disabled";
			ModMetadata metadata;
			if (ReadModMetadata(path, metadata))
				(*found)[mod.fileName] = std::move(metadata);
		}
		post([this, found, generation]() {
			if (generation != m_scanGeneration)
				return;
			m_metadata = found;
			m_scanning = false;
		});
	});
}

void ManagePage::LoadLog(const ManagePageContext& context, std::size_t index)
{
	if (index >= m_logs.size() || !context.tasks.run)
		return;
	m_logIndex = index;
	m_logLoading = true;
	m_logError.clear();
	m_logLines.clear();
	m_shownLines.clear();
	m_refilter = true;
	m_logView.ClearSelection();
	m_logView.ScrollToBottom();
	const fs::path path = m_logs[index].path;
	const int generation = m_generation;
	auto post = context.tasks.post;
	context.tasks.run([this, path, index, generation, post]() {
		std::string text, error;
		const bool ok = ReadLogText(path, text, &error);
		auto lines = std::make_shared<std::vector<std::string>>(SplitLines(text));
		post([this, ok, error, lines, index, generation]() {
			if (generation != m_generation || index != m_logIndex)
				return;
			m_logLoading = false;
			m_logError = ok ? std::string() : error;
			m_logLines = std::move(*lines);
			m_refilter = true; // filter the new lines on the next frame
		});
	});
}

void ManagePage::LoadChangelog(const ManagePageContext& context)
{
	const Modpack& pack = *context.pack;
	const std::string key = pack.id + "@" + pack.sourceVersionId + "@" + pack.version;
	if (m_changelogsLoading.count(key) || !context.tasks.run)
		return;
	m_changelogsLoading.insert(key);
	m_changelogErrors.erase(key);
	HttpClient* http = context.http;
	auto post = context.tasks.post;
	context.tasks.run([this, http, pack, key, post]() {
		std::string text, error;
		const bool ok = FetchPackChangelog(*http, pack, text, &error);
		post([this, key, ok, text, error]() {
			m_changelogsLoading.erase(key);
			if (ok)
				m_changelogs[key] = text;
			else
				m_changelogErrors[key] = error;
		});
	});
}

ManageRequest ManagePage::Draw(const ManagePageContext& context)
{
	ManageRequest request;
	if (!context.pack)
		return request;
	if (m_loadedFor != context.pack->id + "|" + context.gameDir.u8string())
		Reload(context);
	if (!context.droppedPaths.empty())
		AddFiles(context, context.droppedPaths);
	if (m_reloadContent) {
		m_reloadContent = false;
		ReloadContent(context);
	}

	request = DrawHeader(context);
	ImGui::Spacing();

	const std::string modsLabel = "Content (" + std::to_string(m_content[0].files.size()) + ")";
	const std::string shotsLabel = "Screenshots (" + std::to_string(m_screenshots.size()) + ")";
	const std::string logsLabel = "Logs (" + std::to_string(m_logs.size()) + ")";
	const char* tabs[kTabCount] = { "Overview", modsLabel.c_str(), "Changelog", logsLabel.c_str(), shotsLabel.c_str() };
	if (Widgets::TabStrip("manage_tabs", tabs, kTabCount, &m_tab)) {
		m_tabShownAt = ImGui::GetTime();
		if (m_tab == kLogs && m_logLines.empty() && !m_logLoading && !m_logs.empty())
			LoadLog(context, m_logIndex);
	}
	ImGui::Spacing();

	Widgets::BeginEntrance(std::max(context.shownAt, m_tabShownAt));
	switch (m_tab) {
	case kOverview: {
		const ManageRequest overview = DrawOverview(context);
		if (overview.action != ManageAction::None)
			request = overview;
		break;
	}
	case kContent: DrawContent(context); break;
	case kChangelog: DrawChangelog(context); break;
	case kLogs: DrawLogs(context); break;
	case kScreenshots: DrawScreenshots(context); break;
	default: break;
	}
	Widgets::EndEntrance();
	DrawScreenshotViewer(context);
	return request;
}

ManageRequest ManagePage::DrawHeader(const ManagePageContext& context)
{
	ManageRequest request;
	const Modpack& pack = *context.pack;
	if (ImGui::Button("<  My Library"))
		request.action = ManageAction::Back;
	ImGui::SameLine(0.0f, S(16.0f));
	ImGui::AlignTextToFramePadding();
	std::string details = std::string(PackSourceName(pack.source)) + "  \xC2\xB7  " + pack.version;
	if (!pack.gameVersion.empty())
		details += "  \xC2\xB7  Minecraft " + pack.gameVersion;
	if (!pack.loaders.empty())
		details += "  \xC2\xB7  " + pack.loaders;
	ImGui::TextDisabled("%s", details.c_str());
	if (context.changes && !context.changes->empty()) {
		ImGui::SameLine();
		Widgets::ModifiedChip(DescribeContentChanges(*context.changes));
	}

	// Main actions, right-aligned.
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	const char* label = context.playing ? "Stop game"
		: context.playCancelling ? "Cancelling..." : context.playBusy ? "Cancel" : "  PLAY  ";
	const float width = Widgets::ButtonWidth("Open folder") + Widgets::ButtonWidth(label) + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
	if (ImGui::Button("Open folder"))
		Platform::OpenFolder(context.gameDir.string());
	ImGui::SetItemTooltip("Open the game folder (mods, config, saves, logs)");
	ImGui::SameLine();
	if (context.playing) {
		if (Widgets::DangerButton(label))
			request.action = ManageAction::StopGame;
	} else if (context.playBusy) {
		ImGui::BeginDisabled(context.playCancelling);
		if (ImGui::Button(label))
			request.action = ManageAction::CancelPlay;
		ImGui::EndDisabled();
	} else {
		ImGui::BeginDisabled(context.updating);
		if (Widgets::PrimaryButton(label))
			request.action = ManageAction::Play;
		ImGui::EndDisabled();
	}
	ImGui::PopFont();

	if (context.playing) {
		ImGui::TextColored(Theme::C().accent, "Minecraft is running");
	} else if (context.playBusy) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", context.playStep.c_str());
		if (context.playProgress >= 0.0)
			Widgets::ThinProgressBar(static_cast<float>(context.playProgress));
	} else if (context.updating) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", context.updateStatus.c_str());
	} else if (!context.playError.empty()) {
		Widgets::WrappedText(Theme::C().error, context.playError.c_str());
	}
	return request;
}

ManageRequest ManagePage::DrawOverview(const ManagePageContext& context)
{
	ManageRequest request;
	const Modpack& pack = *context.pack;
	Widgets::HeroBanner(pack, *context.images, std::clamp(ImGui::GetContentRegionAvail().x * 0.3f, S(180.0f), S(280.0f)),
		context.showGalleryBanners);
	ImGui::Spacing();

	// At a glance.
	auto stat = [](const std::string& text, const ImVec4& tint) {
		Widgets::Chip(text.c_str(), tint);
		ImGui::SameLine();
	};
	auto count = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
	stat(count(m_content[0].files.size(), "mod", "mods"), Theme::C().accent);
	stat(count(m_content[1].files.size(), "resource pack", "resource packs"), Theme::C().info);
	stat(count(m_content[2].files.size(), "shader pack", "shader packs"), Theme::C().info);
	stat(count(static_cast<std::size_t>(m_worlds), "world", "worlds"), Theme::C().textDim);
	stat(count(m_screenshots.size(), "screenshot", "screenshots"), Theme::C().textDim);
	ImGui::NewLine();
	ImGui::TextDisabled(m_lastPlayed.empty() ? "Not played yet" : "Last played %s", m_lastPlayed.c_str());
	if (!pack.description.empty()) {
		ImGui::Spacing();
		Widgets::WrappedText(Theme::C().text, pack.description.c_str());
	}
	ImGui::Spacing();

	// Installed versions.
	Widgets::BeginCard("manage_versions");
	Widgets::SectionLabel("INSTALLED VERSIONS");
	for (const InstalledVersion& v : pack.installedVersions) {
		ImGui::PushID(v.key.c_str());
		const bool active = v.key == pack.activeVersion;
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
		ImGui::TextUnformatted(v.version.empty() ? v.key.c_str() : v.version.c_str());
		ImGui::PopFont();
		std::string about = v.gameVersion.empty() ? std::string() : "Minecraft " + v.gameVersion;
		if (!v.loaders.empty())
			about += (about.empty() ? "" : "  \xC2\xB7  ") + v.loaders;
		ImGui::SameLine();
		ImGui::TextDisabled("%s", about.c_str());
		if (active) {
			ImGui::SameLine();
			Widgets::Chip("Active", Theme::C().accent);
		}
		if (!v.contentChanges.empty()) {
			ImGui::SameLine();
			Widgets::ModifiedChip(DescribeContentChanges(v.contentChanges));
		}
		const char* uninstall = "Uninstall...";
		const float buttons = Widgets::ButtonWidth(uninstall) + (active ? 0.0f : Widgets::ButtonWidth("Make active") + ImGui::GetStyle().ItemSpacing.x);
		ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
		ImGui::BeginDisabled(context.playBusy || context.updating);
		if (!active) {
			if (ImGui::Button("Make active"))
				request = { ManageAction::SelectVersion, v.key };
			ImGui::SetItemTooltip("PLAY, Open folder and this page use the active version");
			ImGui::SameLine();
		}
		if (ImGui::Button(uninstall))
			request = { ManageAction::Uninstall, pack.installedVersions.size() > 1 ? v.key : std::string() };
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	Widgets::EndCard();
	ImGui::Spacing();

	// The player's edits to the active version.
	if (context.changes && !context.changes->empty()) {
		Widgets::BeginCard("manage_changes");
		Widgets::SectionLabel("YOUR CHANGES");
		const std::string summary = "You changed this version: " + DescribeContentChanges(*context.changes) +
			". Your changes are kept when you play or repair it. Updating installs a new version without them.";
		Widgets::WrappedText(Theme::C().text, summary.c_str());
		ImGui::Spacing();
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		for (const auto& [path, change] : *context.changes) {
			const char* what = change == "added" ? "Added" : change == "removed" ? "Removed"
				: change == "disabled" ? "Turned off" : "Turned on";
			ImGui::TextColored(Widgets::ModifiedColor(), "%s", what);
			ImGui::SameLine(S(90.0f));
			ImGui::TextDisabled("%s", path.c_str());
		}
		ImGui::PopFont();
		ImGui::Spacing();
		ImGui::BeginDisabled(context.playBusy || context.updating);
		if (m_confirmUndo) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Delete the files you added and restore the pack's own?");
			ImGui::SameLine();
			if (Widgets::DangerButton("Undo changes")) {
				request.action = ManageAction::UndoChanges;
				m_confirmUndo = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep"))
				m_confirmUndo = false;
		} else if (ImGui::Button("Undo my changes...")) {
			m_confirmUndo = true;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Deletes added files, switches files back, and downloads removed ones again (Repair files)");
		Widgets::EndCard();
		ImGui::Spacing();
	}

	// Folders and maintenance.
	Widgets::BeginCard("manage_tools");
	Widgets::SectionLabel("MANAGE");
	// A read-only path (selectable, so it can be copied) with an Open button.
	auto folderRow = [](const char* label, const fs::path& path) {
		ImGui::PushID(label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine(S(130.0f));
		std::string text = path.u8string();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Widgets::ButtonWidth("Open") - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##path", &text, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		if (ImGui::Button("Open"))
			Platform::OpenFolder(path.string());
		ImGui::PopID();
	};
	folderRow("Game folder", context.gameDir);
	folderRow("Pack files", context.installDir);
	ImGui::Spacing();
	if (ImGui::Button(context.hasLaunchSettings ? "Launch settings (custom)..." : "Launch settings..."))
		request.action = ManageAction::LaunchSettings;
	ImGui::SetItemTooltip("Memory, Java and window options for this pack only");
	if (context.canUpdate) {
		ImGui::SameLine();
		ImGui::BeginDisabled(context.updating);
		if (ImGui::Button("Update..."))
			request.action = ManageAction::Update;
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Install the newest version from %s and bring your worlds and settings along",
			PackSourceName(pack.source));
	}
	if (context.canRepair) {
		ImGui::SameLine();
		ImGui::BeginDisabled(context.playBusy || context.updating);
		if (ImGui::Button("Repair files"))
			request.action = ManageAction::Repair;
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Re-check every pack file and re-download damaged or missing ones");
	}
	if (context.canHostServer) {
		ImGui::SameLine();
		if (ImGui::Button("Host a server"))
			request.action = ManageAction::Server;
		ImGui::SetItemTooltip("Set up and run this pack as a server on the Servers page");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(context.playBusy || context.updating);
	if (Widgets::DangerButton(pack.installedVersions.size() > 1 ? "Uninstall all versions..." : "Uninstall..."))
		request = { ManageAction::Uninstall, std::string() };
	ImGui::EndDisabled();
	Widgets::EndCard();
	return request;
}

void ManagePage::DrawContent(const ManagePageContext& context)
{
	const std::string labels[3] = {
		"Mods (" + std::to_string(m_content[0].files.size()) + ")",
		"Resource packs (" + std::to_string(m_content[1].files.size()) + ")",
		"Shader packs (" + std::to_string(m_content[2].files.size()) + ")",
	};
	const char* kindLabels[3] = { labels[0].c_str(), labels[1].c_str(), labels[2].c_str() };
	if (Widgets::TabStrip("content_kinds", kindLabels, 3, &m_contentKind))
		m_removeName.clear();
	const ContentKind kind = kKinds[m_contentKind];
	const ContentKindInfo& info = GetContentKind(kind);
	const fs::path folder = context.gameDir / info.folder;
	ContentList& list = m_content[m_contentKind];

	ImGui::Spacing();
	if (m_browsing) {
		DrawBrowse(context);
		return;
	}
	const float buttons = Widgets::ButtonWidth("Browse Modrinth") + Widgets::ButtonWidth("Add files...") +
		Widgets::ButtonWidth("Open folder") + Widgets::ButtonWidth("Refresh") + ImGui::GetStyle().ItemSpacing.x * 4.0f;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
	ImGui::InputTextWithHint("##content_filter", ("Filter " + ToLower(info.label) + "...").c_str(), &m_contentFilter);
	ImGui::SameLine();
	if (Widgets::PrimaryButton("Browse Modrinth")) {
		m_browsing = true;
		m_contentNotice.clear();
		m_contentError.clear();
	}
	ImGui::SetItemTooltip("Find %s on Modrinth for Minecraft %s and add them with what they need", ToLower(info.label).c_str(),
		context.pack->gameVersion.c_str());
	ImGui::SameLine();
	ImGui::BeginDisabled(context.playBusy);
	if (ImGui::Button("Add files...")) {
		const std::string pattern = "*" + info.extensions.front();
		std::vector<fs::path> picked;
		for (const std::string& path : Platform::PickFiles(std::string("Add ") + ToLower(info.label), info.label, pattern))
			picked.push_back(fs::u8path(path));
		if (!picked.empty())
			AddFiles(context, picked);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Copy %s files into this pack. You can also drop files on the window.", info.extensions.front().c_str());
	ImGui::SameLine();
	if (ImGui::Button("Open folder##content"))
		Platform::OpenFolder(folder.string());
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		m_loadedFor.clear(); // reloads next frame
	ImGui::SetItemTooltip("Read the folders again, e.g. after adding files by hand");
	if (context.playBusy)
		ImGui::TextDisabled("Close the game to turn content on or off.");
	if (!m_contentError.empty())
		Widgets::WrappedText(Theme::C().error, m_contentError.c_str());
	if (!m_contentNotice.empty())
		Widgets::WrappedText(Theme::C().accent, m_contentNotice.c_str());
	if (kind == ContentKind::Mods && m_scanning) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Reading mod names...");
	}

	const std::string filter = ToLower(m_contentFilter);
	int shown = 0;
	Widgets::BeginCard("content_list", ImVec2(S(14.0f), S(10.0f)));
	for (std::size_t i = 0; i < list.files.size(); ++i) {
		ContentFile& file = list.files[i];
		const ModMetadata* meta = nullptr;
		if (kind == ContentKind::Mods && m_metadata) {
			const auto found = m_metadata->find(file.fileName);
			if (found != m_metadata->end())
				meta = &found->second;
		}
		const std::string name = meta && !meta->name.empty() ? meta->name : file.fileName;
		if (!filter.empty() && ToLower(name).find(filter) == std::string::npos && ToLower(file.fileName).find(filter) == std::string::npos)
			continue;
		++shown;
		ImGui::PushID(file.fileName.c_str());
		if (shown > 1)
			ImGui::Separator();
		// The text block goes first, then the switch and Remove are centered beside it.
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float frameHeight = ImGui::GetFrameHeight();
		const float textX = rowStart.x + frameHeight * 0.8f * 1.9f + S(12.0f); // after the switch (see Widgets::Toggle)
		ImGui::SetCursorScreenPos(ImVec2(textX, rowStart.y));
		ImGui::BeginGroup();
		// Descriptions wrap before the Remove (or Delete file / Keep) buttons.
		ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - Widgets::ButtonWidth("Delete file") -
			Widgets::ButtonWidth("Keep") - ImGui::GetStyle().ItemSpacing.x - S(16.0f));
		ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, file.enabled ? Theme::C().text : Theme::C().textDim);
		ImGui::TextUnformatted(name.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();
		if (meta && !meta->version.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", meta->version.c_str());
		}
		if (meta && !meta->loader.empty()) {
			ImGui::SameLine();
			Widgets::Chip(meta->loader.c_str(), Theme::C().info);
		}
		const std::string contentPath = ContentPath(kind, file.fileName);
		if (context.changes) {
			const auto change = context.changes->find(contentPath);
			if (change != context.changes->end()) {
				ImGui::SameLine();
				const bool added = change->second == "added";
				Widgets::Chip(added ? "Added" : "Changed", Widgets::ModifiedColor());
				ImGui::SetItemTooltip(added ? "You added this; it isn't part of the pack"
					: change->second == "disabled" ? "You turned this off" : "You turned this on; the pack ships it off");
			}
		}
		std::string sub = file.fileName;
		if (!file.folder)
			sub += "  \xC2\xB7  " + FormatFileSize(file.size);
		else
			sub += "  \xC2\xB7  folder";
		if (meta && !meta->authors.empty()) {
			sub += "  \xC2\xB7  by " + meta->authors.front();
			if (meta->authors.size() > 1)
				sub += " and others";
		}
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		ImGui::TextDisabled("%s", sub.c_str());
		if (meta && !meta->description.empty())
			Widgets::WrappedText(Theme::C().textDim, ShortenText(meta->description, 220).c_str());
		ImGui::PopFont();
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		const float rowHeight = std::max(ImGui::GetItemRectMax().y - rowStart.y, frameHeight);
		const float controlsY = rowStart.y + std::floor((rowHeight - frameHeight) * 0.5f);
		const float rowBottom = rowStart.y + rowHeight;

		ImGui::SetCursorScreenPos(ImVec2(rowStart.x, controlsY));
		ImGui::BeginDisabled(context.playBusy);
		bool on = file.enabled;
		if (Widgets::Toggle("##on", &on)) {
			std::string error;
			if (SetContentEnabled(folder, file.fileName, on, &error)) {
				file.enabled = on;
				m_contentError.clear();
				if (context.recordChange)
					context.recordChange(contentPath, on ? ContentChange::Enabled : ContentChange::Disabled);
			} else {
				m_contentError = error;
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(file.enabled ? "On: Minecraft loads it" : "Off: renamed to .disabled, so Minecraft skips it");

		// Remove, with an inline confirmation.
		const bool confirming = m_removeName == file.fileName;
		const float removeWidth = confirming ? Widgets::ButtonWidth("Delete file") + Widgets::ButtonWidth("Keep") + ImGui::GetStyle().ItemSpacing.x
			: Widgets::ButtonWidth("Remove");
		ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetItemRectMax().x + S(12.0f),
			ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - removeWidth), controlsY));
		ImGui::BeginDisabled(context.playBusy);
		if (confirming) {
			if (Widgets::DangerButton("Delete file")) {
				std::string error;
				if (DeleteContent(folder, file.fileName, &error)) {
					if (context.recordChange)
						context.recordChange(contentPath, ContentChange::Removed);
					list.files.erase(list.files.begin() + static_cast<std::ptrdiff_t>(i));
					m_contentError.clear();
				} else {
					m_contentError = error;
				}
				m_removeName.clear();
				ImGui::EndDisabled();
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowBottom));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep"))
				m_removeName.clear();
		} else if (ImGui::Button("Remove")) {
			m_removeName = file.fileName;
		}
		ImGui::EndDisabled();
		ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowBottom));
		ImGui::Dummy(ImVec2(0.0f, 0.0f)); // the row's height, for the separator and next row
		ImGui::PopID();
	}
	if (list.files.empty())
		ImGui::TextDisabled("No %s in %s yet. Add files..., or drop them on this window.", ToLower(info.label).c_str(), info.folder);
	else if (shown == 0)
		ImGui::TextDisabled("Nothing matches the filter.");
	Widgets::EndCard();
}

void ManagePage::DrawBrowse(const ManagePageContext& context)
{
	const ContentSearch search = BrowseSearch(context);
	const ContentKindInfo& info = GetContentKind(search.kind);
	const float back = Widgets::ButtonWidth("Back to installed") + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - back);
	if (ImGui::InputTextWithHint("##browse_query", ("Search Modrinth for " + ToLower(info.label) + "...").c_str(), &m_browseQuery))
		m_browseEditedAt = ImGui::GetTime();
	ImGui::SameLine();
	if (ImGui::Button("Back to installed")) {
		m_browsing = false;
		return;
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
	if (!m_contentError.empty())
		Widgets::WrappedText(Theme::C().error, m_contentError.c_str());
	if (!m_contentNotice.empty())
		Widgets::WrappedText(Theme::C().accent, m_contentNotice.c_str());

	// Search a moment after typing stops.
	if (ImGui::GetTime() - m_browseEditedAt > 0.4)
		SearchModrinth(context);
	LoadInstalledProjects(context);

	if (m_searching) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Searching Modrinth...");
	} else if (!m_searchError.empty()) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't search Modrinth: " + m_searchError).c_str());
		if (ImGui::Button("Retry"))
			m_searchedFor.clear();
		return;
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
		ImGui::TextDisabled("Nothing on Modrinth matches for this pack's Minecraft version%s.",
			search.kind == ContentKind::Mods && !search.loader.empty() ? " and loader" : "");
	Widgets::EndCard();
}

void ManagePage::DrawChangelog(const ManagePageContext& context)
{
	const Modpack& pack = *context.pack;
	if (pack.source == PackSource::Custom) {
		ImGui::TextDisabled("You made this pack, so it has no changelog.");
		return;
	}
	if (!GetSourceCapabilities(pack.source).changelogs) {
		Widgets::WrappedText(Theme::C().textDim, (std::string(PackSourceName(pack.source)) +
			" doesn't publish changelogs. The pack's page may list what changed.").c_str());
		if (!pack.websiteUrl.empty())
			ImGui::TextLinkOpenURL("Open the pack's page", pack.websiteUrl.c_str());
		return;
	}
	const std::string key = pack.id + "@" + pack.sourceVersionId + "@" + pack.version;
	const auto text = m_changelogs.find(key);
	const auto error = m_changelogErrors.find(key);
	if (text == m_changelogs.end() && error == m_changelogErrors.end())
		LoadChangelog(context);
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, ("What's new in " + pack.version).c_str());
	ImGui::Spacing();
	if (m_changelogsLoading.count(key)) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading the changelog...");
	} else if (error != m_changelogErrors.end()) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't load the changelog: " + error->second).c_str());
		if (ImGui::Button("Retry"))
			LoadChangelog(context);
	} else if (text != m_changelogs.end()) {
		Widgets::BeginCard("changelog_card");
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(text->second.empty() ? "This version has no changelog." : text->second.c_str());
		ImGui::PopTextWrapPos();
		Widgets::EndCard();
	}
	if (!pack.websiteUrl.empty()) {
		ImGui::Spacing();
		ImGui::TextLinkOpenURL("Older versions on the pack's page", pack.websiteUrl.c_str());
	}
}

void ManagePage::DrawLogs(const ManagePageContext& context)
{
	if (m_logs.empty()) {
		ImGui::TextDisabled("No logs yet. They appear here once the pack has been played.");
		if (ImGui::Button("Refresh##logs"))
			m_loadedFor.clear();
		return;
	}
	const float height = std::max(S(320.0f), ImGui::GetContentRegionAvail().y);
	// The log files.
	const float maxListWidth = ImGui::GetContentRegionAvail().x / S(1.0f) - 360.0f;
	m_logListWidth = std::clamp(m_logListWidth, 160.0f, std::max(160.0f, maxListWidth));
	Widgets::BeginScrollArea("log_files", ImVec2(S(m_logListWidth), height), ImGuiChildFlags_Borders);
	for (std::size_t i = 0; i < m_logs.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (Widgets::ListItem(m_logs[i].label.c_str(), i == m_logIndex) && i != m_logIndex)
			LoadLog(context, i);
		ImGui::SetItemTooltip("%s  \xC2\xB7  %s", m_logs[i].label.c_str(), FormatFileSize(m_logs[i].size).c_str());
		ImGui::PopID();
	}
	Widgets::EndScrollArea();
	ImGui::SameLine(0.0f, 0.0f);
	Widgets::PaneSplitter("##logs_split", &m_logListWidth, 160.0f, maxListWidth);
	ImGui::SameLine(0.0f, 0.0f);

	// The selected log.
	ImGui::BeginGroup();
	const LogFile& log = m_logs[std::min(m_logIndex, m_logs.size() - 1)];
	const float buttons = Widgets::ButtonWidth("Open file") + Widgets::ButtonWidth("Open folder") + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
	ImGui::InputTextWithHint("##log_filter", "Show only lines containing...", &m_logFilter);
	ImGui::SameLine();
	if (ImGui::Button("Open file"))
		Platform::OpenFile(log.path.string());
	ImGui::SameLine();
	if (ImGui::Button("Open folder##logs"))
		Platform::OpenFolder(log.path.parent_path().string());
	if (m_refilter || m_logFilter != m_shownFilter) {
		m_refilter = false;
		m_shownFilter = m_logFilter;
		m_shownLines.clear();
		const std::string filter = ToLower(m_logFilter);
		for (const std::string& line : m_logLines) {
			if (filter.empty() || ToLower(line).find(filter) != std::string::npos)
				m_shownLines.push_back(line);
		}
		m_logView.ClearSelection();
	}
	const float viewHeight = height - ImGui::GetFrameHeightWithSpacing();
	if (m_logLoading) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Reading %s...", log.label.c_str());
	} else if (!m_logError.empty()) {
		Widgets::WrappedText(Theme::C().error, m_logError.c_str());
	} else {
		ImGui::PushFont(nullptr, Theme::kFontSmall + 0.5f);
		m_logView.Draw("log_text", m_shownLines, ImVec2(0.0f, viewHeight));
		ImGui::PopFont();
	}
	ImGui::EndGroup();
}

void ManagePage::DrawScreenshots(const ManagePageContext& context)
{
	if (m_screenshots.empty()) {
		ImGui::TextDisabled("No screenshots yet. Press F2 in game to take one.");
		if (ImGui::Button("Refresh##shots"))
			m_loadedFor.clear();
		return;
	}
	if (ImGui::Button("Open folder##shots"))
		Platform::OpenFolder((context.gameDir / "screenshots").string());
	ImGui::SameLine();
	if (ImGui::Button("Refresh##shots"))
		m_loadedFor.clear();
	ImGui::Spacing();

	const float spacing = ImGui::GetStyle().ItemSpacing.x * 1.5f;
	const float available = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, static_cast<int>((available + spacing) / (S(240.0f) + spacing)));
	const float width = (available - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
	const float height = width * 9.0f / 16.0f;
	for (std::size_t i = 0; i < m_screenshots.size(); ++i) {
		const Screenshot& shot = m_screenshots[i];
		if (i % static_cast<std::size_t>(columns) != 0)
			ImGui::SameLine(0.0f, spacing);
		ImGui::PushID(static_cast<int>(i));
		ImGui::BeginGroup();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton("##shot", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 max(min.x + width, min.y + height);
		const float rounding = S(10.0f);
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::C().cardBg), rounding);
		if (ImGui::IsItemVisible()) {
			// Thumbnails are loaded small; the viewer loads the full image.
			if (const LoadedImage* image = context.images->Get(ImageCache::FileKey(shot.path, 480))) {
				const float aspect = static_cast<float>(image->width) / std::max(1, image->height);
				ImVec2 uv0(0, 0), uv1(1, 1);
				if (aspect > 16.0f / 9.0f) {
					const float visible = (16.0f / 9.0f) / aspect;
					uv0.x = (1.0f - visible) * 0.5f;
					uv1.x = 1.0f - uv0.x;
				} else {
					const float visible = aspect / (16.0f / 9.0f);
					uv0.y = (1.0f - visible) * 0.5f;
					uv1.y = 1.0f - uv0.y;
				}
				drawList->AddImageRounded(image->texture, min, max, uv0, uv1,
					IM_COL32(255, 255, 255, static_cast<int>(255 * Widgets::FadeAlpha(*image))), rounding);
			}
		}
		drawList->AddRect(min, max, ImGui::GetColorU32(hovered ? Theme::C().accent : Theme::C().border), rounding, 0,
			hovered ? S(2.0f) : S(1.0f));
		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		ImGui::TextDisabled("%s", shot.label.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		ImGui::EndGroup();
		if (clicked)
			m_viewing = static_cast<int>(i);
		ImGui::PopID();
	}
}

void ManagePage::DrawScreenshotViewer(const ManagePageContext& context)
{
	if (m_viewing < 0 || m_viewing >= static_cast<int>(m_screenshots.size())) {
		m_viewing = -1;
		return;
	}
	if (!ImGui::IsPopupOpen("Screenshot"))
		ImGui::OpenPopup("Screenshot");
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSize(ImVec2(display.x * 0.86f, display.y * 0.86f));
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	bool open = true;
	if (!ImGui::BeginPopupModal("Screenshot", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav)) {
		m_viewing = -1;
		return;
	}
	const Screenshot& shot = m_screenshots[static_cast<std::size_t>(m_viewing)];
	const int count = static_cast<int>(m_screenshots.size());
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(shot.label.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%d of %d", m_viewing + 1, count);
	const float buttons = Widgets::ButtonWidth("<") + Widgets::ButtonWidth(">") + Widgets::ButtonWidth("Open") + Widgets::ButtonWidth("Show in folder") +
		ImGui::GetStyle().ItemSpacing.x * 3.0f;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
	if (ImGui::Button("<") || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, ImGuiKeyOwner_Any))
		m_viewing = (m_viewing + count - 1) % count;
	ImGui::SameLine();
	if (ImGui::Button(">") || ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, ImGuiKeyOwner_Any))
		m_viewing = (m_viewing + 1) % count;
	ImGui::SameLine();
	if (ImGui::Button("Open"))
		Platform::OpenFile(shot.path.string());
	ImGui::SameLine();
	if (ImGui::Button("Show in folder"))
		Platform::OpenFolder(shot.path.parent_path().string());

	// The image, fit to the space left.
	const ImVec2 area = ImGui::GetContentRegionAvail();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const LoadedImage* image = context.images->Get(ImageCache::FileKey(shot.path));
	if (!image)
		image = context.images->Get(ImageCache::FileKey(shot.path, 480)); // the thumbnail until it loads
	if (image) {
		const float scale = std::min(area.x / std::max(1, image->width), area.y / std::max(1, image->height));
		const ImVec2 size(image->width * scale, image->height * scale);
		const ImVec2 min(origin.x + (area.x - size.x) * 0.5f, origin.y + (area.y - size.y) * 0.5f);
		ImGui::GetWindowDrawList()->AddImageRounded(image->texture, min, ImVec2(min.x + size.x, min.y + size.y),
			ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(8.0f));
	} else {
		Widgets::Spinner(Theme::C().accent);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		open = false;
	if (!open) {
		m_viewing = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
