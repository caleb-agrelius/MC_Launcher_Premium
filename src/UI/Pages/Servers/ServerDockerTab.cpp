#include "UI/Pages/Servers/ServerDockerTab.h"

#include "Services/Servers/ServerService.h"
#include "UI/Pages/Servers/DockerStatusView.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <string>

void ServerDockerTab::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	Widgets::BeginCard("server_docker");
	Widgets::SectionLabel("DOCKER");
	bool useDocker = server.docker;
	ImGui::BeginDisabled(server.running || server.dockerBusy);
	if (Widgets::ToggleRow("Enable Containerized Server (docker-minecraft-server)", &useDocker)) {
		std::string error;
		if (m_servers.SetUsesDocker(ctx.packId, server, useDocker, &error)) {
			server.dockerMessage.clear();
		} else {
			server.dockerFailed = true;
			server.dockerMessage = error;
		}
	}
	ImGui::EndDisabled();
	if (server.running)
		ImGui::SetItemTooltip("Stop the server first");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
	ImGui::TextWrapped(server.docker
		? "On: Start and Stop on the Run tab use \"docker compose\" with itzg/minecraft-server. The container uses this "
		  "same folder, world and settings; follow its output with \"docker compose logs -f\" in the server folder."
		: "Off: Start on the Run tab runs the server directly on this PC with Java, with its console in the launcher. "
		  "Turn this on to run the same folder and world in a container instead.");
	ImGui::PopStyleColor();
	ImGui::Spacing();
	ImGui::BeginDisabled(!server.ready || server.dockerBusy || !server.props.serverIp.empty());
	if (ImGui::Button("Write compose file"))
		m_servers.WriteComposeFile(ctx.packId, server);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("docker-compose.yml in the server folder, for running it yourself or on another machine");
	if (!server.ready)
		ImGui::TextDisabled("Create the server files first.");
	DrawDockerStatus(ctx, m_servers);
	Widgets::EndCard();
}
