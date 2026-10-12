#pragma once

#include "UI/Pages/Settings/SettingsContext.h"
#include "UI/Pages/Settings/SettingsLayout.h"

struct LaunchSettings;

// The Memory, Java and Game window cards, for Settings > Minecraft and for one pack's own
// launch settings. global adds the launcher-wide "When the game starts" choice; per-pack
// settings leave it out.
class LaunchSettingsCards {
public:
	bool Draw(LaunchSettings& launch, const SettingsPageContext& context, SettingsLayout& ui, bool global);

private:
	// The Java choice: Automatic, one of the Javas found on this PC, or a custom path.
	static bool DrawJavaChoice(LaunchSettings& launch, const SettingsPageContext& context);
};
