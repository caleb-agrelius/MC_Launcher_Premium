#include "UI/Pages/Settings/MinecraftTab.h"

#include "Storage/Settings.h"
#include "UI/Theme.h"

using Theme::S;

bool MinecraftTab::Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui)
{
	bool changed = false;
	if (!Settings::NoticeDismissed(settings, "minecraft_tip") &&
		ui.Notice("minecraft_tip", settings.packLaunch.empty() ? "These apply to every pack the next time you press Play."
			: "These apply to every pack the next time you press Play, except packs with their own launch settings "
			  "(My Library > Manage > Launch settings).", Theme::C().info)) {
		settings.dismissedNotices.push_back("minecraft_tip");
		changed = true;
	}
	return m_cards.Draw(settings.launch, context, ui, true) || changed;
}
