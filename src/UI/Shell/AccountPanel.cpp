#include "UI/Shell/AccountPanel.h"

#include "Services/Account/AccountService.h"
#include "Services/Settings/SettingsStore.h"
#include "Storage/Account.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

namespace {

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
					if (Widgets::ListItem(item.name.c_str(), i == list.active, true, "Microsoft"))
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
		ImGui::TextDisabled("Microsoft account");
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(!hasClientId);
		if (ImGui::Button("Add account", ImVec2(half, 0.0f)))
			m_accounts.StartMicrosoftSignIn();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", hasClientId ? "Sign in with another Microsoft account"
			: "Needs an Azure client ID approved for Minecraft. Set it in Settings > Account.");
		ImGui::SameLine();
		if (ImGui::Button("Sign out", ImVec2(-1.0f, 0.0f)))
			m_accounts.RemoveActive();
		ImGui::SetItemTooltip("Remove %s from this launcher", account.name.c_str());
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
		ShowError(m_accounts.SignInError());
	}
}
