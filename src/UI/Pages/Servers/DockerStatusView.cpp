#include "UI/Pages/Servers/DockerStatusView.h"

#include "Services/Servers/ServerService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

void DrawDockerStatus(const ServerTabContext& ctx, ServerService& servers)
{
	const ServerState& server = ctx.server;
	// What's blocking Docker, as one short paragraph with "Check again" below it.
	std::string blocked;
	if (ctx.docker == DockerStatus::Stopped)
		blocked = "Docker isn't running. Start Docker Desktop.";
	if (server.ready && !server.props.serverIp.empty())
		blocked += std::string(blocked.empty() ? "" : " ") + "Docker needs Host IP set to \"All network adapters\".";
	if (!blocked.empty())
		Widgets::WrappedText(Theme::C().error, blocked.c_str());
	if (ctx.docker == DockerStatus::Checking || ctx.docker == DockerStatus::Unknown) {
		ImGui::AlignTextToFramePadding(); // as tall as the "Check again" button that may replace it
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Looking for Docker...");
	} else if (ctx.docker == DockerStatus::Missing) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Docker isn't installed. You can still write the compose file.");
		ImGui::SameLine();
		ImGui::TextLinkOpenURL("Get Docker", "https://docs.docker.com/get-started/get-docker/");
	} else if (ctx.docker == DockerStatus::Stopped) {
		if (ImGui::Button("Check again"))
			servers.CheckDocker();
	}
	if (server.dockerBusy) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
	}
	if (!server.dockerMessage.empty())
		Widgets::WrappedText(server.dockerFailed ? Theme::C().error : Theme::C().textDim, server.dockerMessage.c_str());
}
