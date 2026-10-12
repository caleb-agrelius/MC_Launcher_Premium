#include "UI/Pages/Servers/ServerDetails.h"

#include "Services/AppPaths.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <filesystem>
#include <string>

using Theme::S;
namespace fs = std::filesystem;

void ServerDetails::DrawHeader(const ServerTabContext& ctx)
{
	const ServerState& server = ctx.server;
	UiHelpers::PackIconItem(ctx.pack, m_images, S(56.0f));
	ImGui::SameLine(0.0f, S(14.0f));
	ImGui::BeginGroup();
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, ctx.pack.name.c_str());
	if (server.running)
		ImGui::TextColored(Theme::C().accent, "Running");
	else if (server.docker && server.dockerUp)
		ImGui::TextColored(Theme::C().accent, "Running in Docker");
	else if (ctx.setup.busy)
		ImGui::TextDisabled("Setting up...");
	else if (server.ready)
		ImGui::TextDisabled("%s %s  \xC2\xB7  Minecraft %s  \xC2\xB7  stopped", server.info.loader.c_str(),
			server.info.loaderVersion.c_str(), server.info.minecraftVersion.c_str());
	else
		ImGui::TextDisabled("No server yet");
	ImGui::EndGroup();
	ImGui::Spacing();
}

void ServerDetails::DrawUnavailable(const ServerTabContext& ctx)
{
	Widgets::WrappedText(Theme::C().textDim, !ctx.inLibrary
		? "This server's pack was uninstalled, and its server files are incomplete."
		: "This pack's files are missing or incomplete, so it can't be turned into a server. "
			"Repair or reinstall it from My Library.");
	if (!ctx.hasServerDir)
		return;
	ImGui::Spacing();
	ImGui::BeginDisabled(ctx.setup.busy);
	if (ImGui::Button("Delete server..."))
		m_dialogs.ConfirmDeleteServer(ctx.packId);
	ImGui::EndDisabled();
	if (ctx.setup.busy) {
		ImGui::SameLine();
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", ctx.setup.step.c_str());
	}
	if (!ctx.setup.error.empty())
		Widgets::WrappedText(Theme::C().error, ctx.setup.error.c_str());
}

void ServerDetails::Draw(const std::string& packId, const Modpack& pack, ServerView& view, const HostNetwork& network)
{
	Widgets::BeginScrollArea("server_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	ServerState& server = m_servers.State(packId);
	const ServerSetupSnapshot setup = m_servers.Setup(packId);
	const fs::path dir = fs::path(AppPaths::kServersRoot) / packId;
	const bool inLibrary = m_library.Contains(packId);
	std::error_code ec;
	const ServerTabContext ctx{ packId, pack, server, view, setup, dir, network, m_servers.Docker(), inLibrary,
		inLibrary && m_servers.CanHost(pack), fs::is_directory(dir, ec) };

	DrawHeader(ctx);
	if (!ctx.supported && !server.ready) {
		DrawUnavailable(ctx);
	} else {
		if (!server.modsLoaded)
			m_servers.ReloadMods(packId, server);
		const std::string modsTab = "Mods (" + std::to_string(server.mods.size()) + ")";
		const char* tabs[] = { "Run", "Settings", modsTab.c_str(), server.docker ? "Docker (on)" : "Docker" };
		Widgets::TabStrip("server_tabs", tabs, 4, &view.tab);
		ImGui::Spacing();
		if (view.tab == 0) {
			m_files.Draw(ctx);
			ImGui::Spacing();
			m_run.Draw(ctx);
			ImGui::Spacing();
			m_console.Draw(ctx);
		} else if (view.tab == 1) {
			m_settings.Draw(ctx);
		} else if (view.tab == 2) {
			m_mods.Draw(ctx);
		} else {
			m_docker.Draw(ctx);
		}
	}
	Widgets::EndScrollArea();
}
