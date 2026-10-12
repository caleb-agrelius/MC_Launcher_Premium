#include "UI/Shell/AccountPanel.h"

#include "Services/Account/AccountService.h"
#include "Services/Settings/SettingsStore.h"
#include "Storage/Account.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

namespace {

const char* AccountKind(const Account& account)
{
	return account.type == Account::Type::Microsoft ? "Microsoft account" : "Offline (singleplayer)";
}

void ShowError(const std::string& error)
{
	if (error.empty())
		return;
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
	ImGui::TextWrapped("%s", error.c_str());
	ImGui::PopStyleColor();
}

}

void AccountPanel::Draw()
{
	ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	Widgets::SectionLabel("ACCOUNT");
	const Account& account = m_accounts.Active();
	const AccountList& list = m_accounts.List();
	const bool hasClientId = !m_settings.Get().microsoftClientId.empty();
	const bool signingIn = m_accounts.SignIn() == AccountService::SignInState::Starting;
	if (account.type != Account::Type::None && !signingIn) {
		if (list.accounts.size() > 1) {
			// Switch which saved account Play uses.
			ImGui::SetNextItemWidth(-1.0f);
			if (Widgets::BeginSelect("##account", account.name.c_str())) {
				for (std::size_t i = 0; i < list.accounts.size(); ++i) {
					const Account& item = list.accounts[i];
					ImGui::PushID(static_cast<int>(i));
					if (Widgets::ListItem(item.name.c_str(), i == list.active, true,
							item.type == Account::Type::Microsoft ? "Microsoft" : "Offline"))
						m_accounts.Select(i);
					ImGui::PopID();
				}
				Widgets::EndSelect();
			}
		} else {
			ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
			ImGui::TextUnformatted(account.name.c_str());
			ImGui::PopFont();
		}
		ImGui::TextDisabled("%s", AccountKind(account));
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if (ImGui::Button("Add account", ImVec2(half, 0.0f)))
			ImGui::OpenPopup("add_account");
		ImGui::SameLine();
		if (ImGui::Button("Sign out", ImVec2(-1.0f, 0.0f)))
			m_accounts.RemoveActive();
		ImGui::SetItemTooltip("Remove %s from this launcher", account.name.c_str());
		if (ImGui::BeginPopup("add_account")) {
			if (Widgets::MenuItem("Sign in with Microsoft", nullptr, hasClientId))
				m_accounts.StartMicrosoftSignIn();
			if (Widgets::MenuItem("Play offline..."))
				m_openOfflinePopup = true;
			ImGui::EndPopup();
		}
		ShowError(m_accounts.SignInError());
	} else if (signingIn) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled(m_accounts.SignInCancelling()
			? "Cancelling sign-in..." : "Waiting for sign-in in your browser...");
		ImGui::BeginDisabled(m_accounts.SignInCancelling());
		if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f)))
			m_accounts.CancelSignIn();
		ImGui::EndDisabled();
	} else {
		ImGui::BeginDisabled(!hasClientId);
		if (ImGui::Button("Sign in with Microsoft", ImVec2(-1.0f, 0.0f)))
			m_accounts.StartMicrosoftSignIn();
		ImGui::EndDisabled();
		if (!hasClientId)
			ImGui::SetItemTooltip("Needs an Azure client ID approved for Minecraft. Set it in Settings > Account.");
		if (ImGui::Button("Play offline...", ImVec2(-1.0f, 0.0f)))
			m_openOfflinePopup = true;
		ImGui::SetItemTooltip("Singleplayer and offline-mode servers only. For players who own Minecraft.");
		ShowError(m_accounts.SignInError());
	}
}

void AccountPanel::DrawOfflinePopup()
{
	if (m_openOfflinePopup) {
		ImGui::OpenPopup("Play offline");
		m_openOfflinePopup = false;
	}
	ImGui::SetNextWindowSize(ImVec2(S(420.0f), 0.0f));
	if (!ImGui::BeginPopupModal("Play offline", nullptr, ImGuiWindowFlags_NoResize))
		return;
	ImGui::TextWrapped("Offline mode skips Microsoft sign-in. You can play singleplayer and join "
		"servers that don't check accounts. Use it only if you own Minecraft: Java Edition.");
	ImGui::Spacing();
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	const bool entered = ImGui::InputTextWithHint("##offline_name", "Username", m_offlineName, sizeof(m_offlineName),
		ImGuiInputTextFlags_EnterReturnsTrue);
	const bool valid = IsValidPlayerName(m_offlineName);
	if (!valid && m_offlineName[0] != '\0')
		ImGui::TextColored(Theme::C().error, "3-16 letters, numbers or underscores.");
	ImGui::BeginDisabled(!valid);
	if (ImGui::Button("Use this name") || (entered && valid)) {
		m_accounts.Add(MakeOfflineAccount(m_offlineName));
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel##offline"))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
