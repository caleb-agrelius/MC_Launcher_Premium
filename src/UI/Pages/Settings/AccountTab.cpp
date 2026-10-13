#include "UI/Pages/Settings/AccountTab.h"

#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using Theme::S;

bool AccountTab::Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui)
{
	bool changed = false;
	ui.BeginCard("account_card");
	ui.CardTitle("Current account");
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float avatar = S(56.0f);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	// Steve-style head made of blocks.
	const float px = avatar / 8.0f;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			ImVec4 color = y < 2 ? ImVec4(0.27f, 0.18f, 0.11f, 1.0f) : ImVec4(0.75f, 0.55f, 0.42f, 1.0f);
			if (y == 4 && (x == 1 || x == 2 || x == 5 || x == 6))
				color = x == 1 || x == 6 ? ImVec4(1, 1, 1, 1) : ImVec4(0.30f, 0.25f, 0.60f, 1.0f);
			if (y == 6 && x >= 3 && x <= 4)
				color = ImVec4(0.45f, 0.28f, 0.22f, 1.0f);
			drawList->AddRectFilled(ImVec2(pos.x + x * px, pos.y + y * px), ImVec2(pos.x + (x + 1) * px, pos.y + (y + 1) * px),
				ImGui::GetColorU32(color));
		}
	}
	ImGui::Dummy(ImVec2(avatar, avatar));
	ImGui::SameLine(0.0f, S(16.0f));
	ImGui::BeginGroup();
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	ImGui::TextUnformatted(context.accountName.empty() ? "Not signed in" : context.accountName.c_str());
	ImGui::PopFont();
	ImGui::TextDisabled("%s", context.accountKind.c_str());
	if (context.accountCount > 1)
		ImGui::TextDisabled("%d accounts saved. Switch, add or sign out from the ACCOUNT section of the sidebar.",
			context.accountCount);
	else
		ImGui::TextDisabled("Sign in, add another account, or sign out from the ACCOUNT section of the sidebar.");
	ImGui::EndGroup();
	ui.EndCard();

	ui.RowsCard("microsoft_card", "Microsoft sign-in", "Signing in with Microsoft needs an Azure app (client) ID that Mojang has approved for the "
		"Minecraft API. Without one you can't sign in, and Minecraft can't be played.", [&] {
		ui.Row("Azure client ID", "Takes effect the next time you sign in.");
		changed |= ImGui::InputTextWithHint("##client_id", "00000000-0000-0000-0000-000000000000", &settings.microsoftClientId);
	});
	return changed;
}
