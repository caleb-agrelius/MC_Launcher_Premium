#include "UI/Pages/Settings/MinecraftTab.h"

#include "Storage/Settings.h"
#include "UI/Theme.h"

using Theme::S;

bool MinecraftTab::Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui)
{
	ui.Notice(settings.packLaunch.empty() ? "These apply to every pack the next time you press Play."
		: "These apply to every pack the next time you press Play, except packs with their own launch settings "
		  "(My Library > Manage > Launch settings).", Theme::C().info);
	return m_cards.Draw(settings.launch, context, ui, true);
}
