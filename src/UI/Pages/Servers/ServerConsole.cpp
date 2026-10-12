#include "UI/Pages/Servers/ServerConsole.h"

#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <string>

using Theme::S;

void ServerConsole::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	ServerView& view = ctx.view;
	if (!server.process)
		return;
	ImGui::Spacing();
	Widgets::SectionLabel("CONSOLE");
	// Copy the lines only when there are new ones.
	if (server.process->TotalLines() != view.consoleTotal)
		view.consoleLines = server.process->Lines(&view.consoleTotal);
	const std::uint64_t firstLine = view.consoleTotal - view.consoleLines.size();
	const float consoleHeight = std::max(S(260.0f), ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
	view.console.Draw("server_console", view.consoleLines, ImVec2(0.0f, consoleHeight), firstLine);
	ImGui::BeginDisabled(!server.running);
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Send").x -
		ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
	const bool entered = ImGui::InputTextWithHint("##server_command", "Type a command, e.g. op <name>, say hi, time set day",
		view.command, sizeof(view.command), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if ((ImGui::Button("Send") || entered) && view.command[0] != '\0') {
		std::string command = view.command;
		if (!command.empty() && command[0] == '/')
			command.erase(0, 1);
		server.process->Send(command);
		view.command[0] = '\0';
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::EndDisabled();
}
