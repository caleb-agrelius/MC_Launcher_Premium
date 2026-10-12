#include "UI/Pages/Manage/ContentTab.h"

#include "Platform/Platform.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using Theme::S;

void ContentTab::Reload(const ManagePageContext&)
{
	m_removeName.clear();
}

bool ContentTab::AddFiles(const ManagePageContext& context, const std::vector<fs::path>& paths)
{
	const int shown = m_model.AddFiles(context, paths, m_kind);
	if (shown < 0)
		return false;
	m_kind = shown;
	m_filter.clear();
	m_removeName.clear();
	return true;
}

void ContentTab::Draw(const ManagePageContext& context, ManageFrame& frame)
{
	const std::string labels[3] = {
		"Mods (" + std::to_string(m_model.lists[0].size()) + ")",
		"Resource packs (" + std::to_string(m_model.lists[1].size()) + ")",
		"Shader packs (" + std::to_string(m_model.lists[2].size()) + ")",
	};
	const char* kindLabels[3] = { labels[0].c_str(), labels[1].c_str(), labels[2].c_str() };
	if (Widgets::TabStrip("content_kinds", kindLabels, 3, &m_kind))
		m_removeName.clear();
	const ContentKind kind = ContentModel::kKinds[m_kind];
	const ContentKindInfo& info = GetContentKind(kind);
	const fs::path folder = context.gameDir / info.folder;
	std::vector<ContentFile>& list = m_model.lists[m_kind];

	ImGui::Spacing();
	if (m_browsing) {
		m_browsing = m_browser.Draw(context, m_kind);
		return;
	}
	const float buttons = Widgets::ButtonWidth("Browse Modrinth") + Widgets::ButtonWidth("Add files...") +
		Widgets::ButtonWidth("Open folder") + Widgets::ButtonWidth("Refresh") + ImGui::GetStyle().ItemSpacing.x * 4.0f;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
	ImGui::InputTextWithHint("##content_filter", ("Filter " + ToLower(info.label) + "...").c_str(), &m_filter);
	ImGui::SameLine();
	if (Widgets::PrimaryButton("Browse Modrinth")) {
		m_browsing = true;
		m_model.notice.clear();
		m_model.error.clear();
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
		frame.reload = true;
	ImGui::SetItemTooltip("Read the folders again, e.g. after adding files by hand");
	if (context.playBusy)
		ImGui::TextDisabled("Close the game to turn content on or off.");
	m_model.DrawMessages();
	if (kind == ContentKind::Mods && m_model.scanning)
		UiHelpers::BusyLabel("Reading mod names...");

	const std::string filter = ToLower(m_filter);
	int shown = 0;
	Widgets::BeginCard("content_list", ImVec2(S(14.0f), S(10.0f)));
	for (std::size_t i = 0; i < list.size(); ++i) {
		ContentFile& file = list[i];
		const ModMetadata* meta = nullptr;
		if (kind == ContentKind::Mods && m_model.metadata) {
			const auto found = m_model.metadata->find(file.fileName);
			if (found != m_model.metadata->end())
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
				m_model.error.clear();
				if (context.recordChange)
					context.recordChange(contentPath, on ? ContentChange::Enabled : ContentChange::Disabled);
			} else {
				m_model.error = error;
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
					list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
					m_model.error.clear();
				} else {
					m_model.error = error;
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
	if (list.empty())
		ImGui::TextDisabled("No %s in %s yet. Add files..., or drop them on this window.", ToLower(info.label).c_str(), info.folder);
	else if (shown == 0)
		ImGui::TextDisabled("Nothing matches the filter.");
	Widgets::EndCard();
}
