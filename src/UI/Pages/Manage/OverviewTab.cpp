#include "UI/Pages/Manage/OverviewTab.h"

#include "UI/ImageCache.h"
#include "UI/Pages/Manage/ScreenshotsTab.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using Theme::S;

void OverviewTab::Reload(const ManagePageContext& context)
{
	std::error_code ec;
	m_worlds = 0;
	for (const auto& entry : fs::directory_iterator(context.gameDir / "saves", ec)) {
		if (entry.is_directory(ec))
			++m_worlds;
	}
	const fs::path latest = context.gameDir / "logs" / "latest.log";
	m_lastPlayed = fs::is_regular_file(latest, ec) ? FormatFileTime(fs::last_write_time(latest, ec)) : std::string();
}

void OverviewTab::Draw(const ManagePageContext& context, ManageFrame& frame)
{
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
	stat(count(m_content.Count(0), "mod", "mods"), Theme::C().accent);
	stat(count(m_content.Count(1), "resource pack", "resource packs"), Theme::C().info);
	stat(count(m_content.Count(2), "shader pack", "shader packs"), Theme::C().info);
	stat(count(static_cast<std::size_t>(m_worlds), "world", "worlds"), Theme::C().textDim);
	stat(count(m_screenshots.Count(), "screenshot", "screenshots"), Theme::C().textDim);
	ImGui::NewLine();
	ImGui::TextDisabled(m_lastPlayed.empty() ? "Not played yet" : "Last played %s", m_lastPlayed.c_str());
	if (pack.playSeconds > 0) {
		ImGui::SameLine();
		ImGui::TextDisabled("  ·  Total playtime %s", FormatPlaytime(pack.playSeconds).c_str());
	}
	if (!pack.description.empty()) {
		ImGui::Spacing();
		Widgets::WrappedText(Theme::C().text, pack.description.c_str());
	}
	ImGui::Spacing();

	m_versions.Draw(context, frame.request);
	m_changes.Draw(context, frame.request);
	m_tools.Draw(context, frame.request);
}
