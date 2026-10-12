#include "UI/Pages/Manage/ManageHeader.h"

#include "Network/PackSources.h"
#include "Platform/Platform.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

using Theme::S;

ManageRequest ManageHeader::Draw(const ManagePageContext& context)
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
		UiHelpers::BusyLabel(context.playStep.c_str());
		if (context.playProgress >= 0.0)
			Widgets::ThinProgressBar(static_cast<float>(context.playProgress));
	} else if (context.updating) {
		UiHelpers::BusyLabel(context.updateStatus.c_str());
	} else if (!context.playError.empty()) {
		Widgets::WrappedText(Theme::C().error, context.playError.c_str());
	}
	return request;
}
