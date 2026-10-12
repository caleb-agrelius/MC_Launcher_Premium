#include "UI/Pages/Servers/ServerSettingsTab.h"

#include "Platform/Platform.h"
#include "Services/Servers/ServerService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

using Theme::S;

bool ServerSettingsTab::DrawHostIp(const ServerTabContext& ctx, float labelWidth)
{
	ServerProperties& props = ctx.server.props;
	ServerView& view = ctx.view;
	bool changed = false;
	auto adapterOf = [&](const std::string& address) -> const Platform::NetworkAddress* {
		for (const Platform::NetworkAddress& item : ctx.network.adapters) {
			if (item.address == address)
				return &item;
		}
		return nullptr;
	};
	const Platform::NetworkAddress* current = adapterOf(props.serverIp);
	const bool custom = view.hostIpCustom || (!props.serverIp.empty() && !current);
	std::string preview = custom ? "Custom address" : props.serverIp.empty() ? "All network adapters"
		: props.serverIp + "  (" + current->adapter + ")";
	if (Widgets::BeginSelect("##host_ip", preview.c_str())) {
		if (Widgets::ListItem("All network adapters", !custom && props.serverIp.empty())) {
			view.hostIpCustom = false;
			props.serverIp.clear();
			changed = true;
		}
		for (const Platform::NetworkAddress& item : ctx.network.adapters) {
			const std::string text = item.address + "  (" + item.adapter + ")";
			if (Widgets::ListItem(text.c_str(), !custom && props.serverIp == item.address)) {
				view.hostIpCustom = false;
				props.serverIp = item.address;
				changed = true;
			}
		}
		if (Widgets::ListItem("Custom address...", custom)) {
			view.hostIpCustom = true;
			view.hostIpEdit = props.serverIp;
		}
		Widgets::EndSelect();
	}
	ImGui::SetItemTooltip("The address the server listens on. Pick one adapter, such as a VPN like "
		"Tailscale or ZeroTier, to accept players only through it.");
	if (custom) {
		if (!view.hostIpCustom) {
			view.hostIpCustom = true;
			view.hostIpEdit = props.serverIp;
		}
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
		ImGui::SameLine(labelWidth);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##host_ip_custom", "e.g. 192.168.1.20 or localhost", &view.hostIpEdit) &&
			IsValidServerIp(view.hostIpEdit) && view.hostIpEdit != props.serverIp) {
			props.serverIp = view.hostIpEdit;
			changed = true;
		}
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
		ImGui::SameLine(labelWidth);
		if (!IsValidServerIp(view.hostIpEdit))
			ImGui::TextColored(Theme::C().error, "Enter an IPv4 address like 192.168.1.20 or localhost, or leave it empty.");
		else if (IsLoopbackServerIp(props.serverIp))
			ImGui::TextDisabled("Only players on this PC can join.");
		else if (!props.serverIp.empty() && !adapterOf(props.serverIp) && !IsLoopbackServerIp(props.serverIp))
			ImGui::TextColored(Theme::C().error, "No adapter on this PC has this address, so the server won't start.");
		else
			ImGui::TextDisabled("Empty listens on every adapter.");
	}
	return changed;
}

void ServerSettingsTab::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	Widgets::BeginCard("server_settings");
	Widgets::SectionLabel("SETTINGS");
	ServerProperties& props = server.props;
	bool propsChanged = false;
	const float labelWidth = S(170.0f);
	auto label = [&](const char* text) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(text);
		ImGui::SameLine(labelWidth);
		ImGui::SetNextItemWidth(-FLT_MIN);
	};
	label("Message of the day");
	propsChanged |= ImGui::InputText("##motd", &props.motd);
	label("Port");
	if (ImGui::InputInt("##port", &props.port, 0)) {
		props.port = std::clamp(props.port, 1024, 65535);
		propsChanged = true;
	}
	label("Host IP");
	propsChanged |= DrawHostIp(ctx, labelWidth);
	label("Max players");
	propsChanged |= ImGui::SliderInt("##players", &props.maxPlayers, 1, 100);
	label("Game mode");
	const char* modes[] = { "survival", "creative", "adventure", "spectator" };
	int mode = 0;
	for (int i = 0; i < 4; ++i) if (props.gamemode == modes[i]) mode = i;
	const char* modeLabels[] = { "Survival", "Creative", "Adventure", "Spectator" };
	if (Widgets::Select("##gamemode", &mode, modeLabels, 4)) {
		props.gamemode = modes[mode];
		propsChanged = true;
	}
	label("Difficulty");
	const char* difficulties[] = { "peaceful", "easy", "normal", "hard" };
	int difficulty = 2;
	for (int i = 0; i < 4; ++i) if (props.difficulty == difficulties[i]) difficulty = i;
	const char* difficultyLabels[] = { "Peaceful", "Easy", "Normal", "Hard" };
	if (Widgets::Select("##difficulty", &difficulty, difficultyLabels, 4)) {
		props.difficulty = difficulties[difficulty];
		propsChanged = true;
	}
	label("View distance");
	propsChanged |= ImGui::SliderInt("##view", &props.viewDistance, 3, 32, "%d chunks");
	label("PvP");
	propsChanged |= Widgets::Toggle("##pvp", &props.pvp);
	label("Whitelist");
	propsChanged |= Widgets::Toggle("##whitelist", &props.whitelist);
	ImGui::SameLine(0.0f, S(12.0f));
	ImGui::TextDisabled("Only players you add with \"whitelist add <name>\" can join");
	label("Online mode");
	propsChanged |= Widgets::Toggle("##online", &props.onlineMode);
	ImGui::SameLine(0.0f, S(12.0f));
	if (props.onlineMode)
		ImGui::TextDisabled("Players need a Microsoft account that owns Minecraft");
	else
		ImGui::TextColored(Theme::C().error, "Anyone can join with any name. Use a whitelist.");
	label("Memory");
	float memoryGb = server.memoryMb / 1024.0f;
	if (ImGui::SliderFloat("##server_memory", &memoryGb, 1.0f,
			std::max(2.0f, Platform::SystemMemoryMb() / 1024.0f), "%.1f GB")) {
		server.memoryMb = std::max(1024, static_cast<int>(std::lround(memoryGb * 2.0f)) * 512);
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
		m_servers.SaveMemory(ctx.packId, server);
	if (propsChanged)
		m_servers.SaveProperties(ctx.packId, server);
	if (server.running)
		ImGui::TextDisabled("Changes apply the next time the server starts.");
	Widgets::EndCard();
	ImGui::Spacing();
}
