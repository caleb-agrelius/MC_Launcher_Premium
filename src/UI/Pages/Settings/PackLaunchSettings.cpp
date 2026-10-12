#include "UI/Pages/Settings/PackLaunchSettings.h"

#include "Storage/Settings.h"
#include "UI/Widgets.h"

bool PackLaunchSettings::Draw(Settings& settings, const std::string& packId, const SettingsPageContext& context, SettingsLayout& ui)
{
	bool changed = false;
	bool custom = settings.packLaunch.count(packId) > 0;
	ui.BeginCard("pack_launch_card");
	if (ui.BeginRows("pack_launch_rows")) {
		ui.Row("Use custom launch settings", "Off uses Settings > Minecraft, like every other pack.");
		if (Widgets::Toggle("##pack_custom", &custom)) {
			if (custom)
				settings.packLaunch[packId] = settings.launch; // start from the global options
			else
				settings.packLaunch.erase(packId);
			changed = true;
		}
		ui.EndRows();
	}
	ui.EndCard();
	const auto it = settings.packLaunch.find(packId);
	if (it != settings.packLaunch.end())
		changed |= m_cards.Draw(it->second, context, ui, false);
	return changed;
}
