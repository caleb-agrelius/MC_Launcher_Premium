#include "UI/Pages/Manage/ScreenshotsTab.h"

#include "Platform/Platform.h"
#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "imgui_internal.h" // IsKeyPressed for keys keyboard navigation also uses

#include <algorithm>

namespace fs = std::filesystem;
using Theme::S;

void ScreenshotsTab::Reload(const ManagePageContext& context)
{
	std::error_code ec;
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
}

void ScreenshotsTab::Draw(const ManagePageContext& context, ManageFrame& frame)
{
	if (m_screenshots.empty()) {
		UiHelpers::EmptyWithRefresh("No screenshots yet. Press F2 in game to take one.", "Refresh##shots", frame.reload);
		return;
	}
	if (ImGui::Button("Open folder##shots"))
		Platform::OpenFolder((context.gameDir / "screenshots").string());
	ImGui::SameLine();
	if (ImGui::Button("Refresh##shots"))
		frame.reload = true;
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

void ScreenshotsTab::DrawViewer(const ManagePageContext& context)
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
