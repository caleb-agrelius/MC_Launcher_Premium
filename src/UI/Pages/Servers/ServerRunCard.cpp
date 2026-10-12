#include "UI/Pages/Servers/ServerRunCard.h"

#include "Services/Servers/ServerService.h"
#include "UI/Pages/Servers/DockerStatusView.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <string>
#include <vector>

using Theme::S;

void ServerRunCard::DrawButtons(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	if (server.docker) {
		// Containerized: the same folder and world, run by docker-minecraft-server.
		const bool dockerReady = ctx.docker == DockerStatus::Ready;
		ImGui::BeginDisabled(!dockerReady || !server.ready || !server.eula || server.dockerBusy || !server.props.serverIp.empty());
		if (Widgets::PrimaryButton("  Start in Docker  ", ImVec2(0.0f, S(44.0f))))
			m_servers.StartDockerCompose(ctx.packId, { "compose", "up", "-d" },
				"Started. Watch it with \"docker compose logs -f\" in the server folder.");
		ImGui::EndDisabled();
		if (!server.eula)
			ImGui::SetItemTooltip("Agree to the Minecraft EULA first");
		ImGui::SameLine();
		ImGui::BeginDisabled(!dockerReady || server.dockerBusy);
		if (ImGui::Button("  Stop in Docker  ", ImVec2(0.0f, S(44.0f))))
			m_servers.StartDockerCompose(ctx.packId, { "compose", "down" }, "Stopped. The world was saved.");
		ImGui::EndDisabled();
	} else if (!server.running) {
		ImGui::BeginDisabled(!server.ready || !server.eula || ctx.setup.busy);
		if (Widgets::PrimaryButton("  Start server  ", ImVec2(0.0f, S(44.0f)))) {
			if (m_servers.StartProcess(ctx.packId))
				ctx.view.ResetConsole();
		}
		ImGui::EndDisabled();
		if (!server.ready)
			ImGui::SetItemTooltip("Create the server files first");
		else if (!server.eula)
			ImGui::SetItemTooltip("Agree to the Minecraft EULA first");
	} else {
		if (ImGui::Button("  Stop server  ", ImVec2(0.0f, S(44.0f)))) {
			server.process->Send("stop");
			server.stopRequestedAt = ImGui::GetTime();
		}
		ImGui::SameLine();
		if (Widgets::DangerButton("Force stop", ImVec2(0.0f, S(44.0f))))
			server.process->Kill();
		ImGui::SetItemTooltip("Kill the server without saving");
	}
	ImGui::PopFont();
}

void ServerRunCard::DrawJoinAddresses(const ServerTabContext& ctx)
{
	const ServerState& server = ctx.server;
	ImGui::Spacing();
	// A server bound to one address only answers there, so show that one.
	const std::string& boundIp = server.props.serverIp;
	const bool loopbackOnly = IsLoopbackServerIp(boundIp);
	ImGui::TextDisabled("Join from this PC:");
	ImGui::SameLine();
	const std::string localAddress = (boundIp.empty() ? std::string("localhost") : boundIp) + ":" +
		std::to_string(server.props.port);
	ImGui::TextUnformatted(localAddress.c_str());
	const std::string networkIp = boundIp.empty() ? ctx.network.lanAddress : loopbackOnly ? std::string() : boundIp;
	if (!networkIp.empty()) {
		ImGui::TextDisabled("Friends on your network:");
		ImGui::SameLine();
		const std::string address = networkIp + ":" + std::to_string(server.props.port);
		ImGui::TextUnformatted(address.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy"))
			ImGui::SetClipboardText(address.c_str());
	}
	if (loopbackOnly)
		ImGui::TextDisabled("Only players on this PC can join. Change Host IP on the Settings tab to let friends in.");
	else
		ImGui::TextDisabled("Friends over the internet need port %d forwarded on your router to this PC.", server.props.port);
}

void ServerRunCard::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	Widgets::BeginCard("server_run");
	Widgets::SectionLabel("RUN");
	bool eula = server.eula;
	ImGui::BeginDisabled(server.eula);
	if (Widgets::Toggle("##eula", &eula) && eula)
		m_servers.AcceptEula(ctx.packId, server);
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, S(12.0f));
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("I agree to the");
	ImGui::SameLine(0.0f, S(4.0f));
	ImGui::TextLinkOpenURL("Minecraft EULA", "https://aka.ms/MinecraftEULA");
	DrawButtons(ctx);
	if (server.docker)
		DrawDockerStatus(ctx, m_servers);
	if (server.stopRequestedAt >= 0.0 && server.running) {
		ImGui::SameLine();
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Saving and stopping...");
	}
	DrawJoinAddresses(ctx);
	Widgets::EndCard();
}
