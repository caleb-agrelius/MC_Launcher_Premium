#pragma once

#include "Model/Modpack.h"

#include <string>

// The dialogs pages can open. A page asks for one by name; the dialog host owns what each looks
// like and what pressing its buttons does.
class IDialogs {
public:
	virtual ~IDialogs() = default;
	// "Stop Minecraft?" for the running game under this play key.
	virtual void ConfirmStopGame(const std::string& playKey) = 0;
	// Per-pack launch settings.
	virtual void ShowLaunchSettings(const std::string& packId) = 0;
	// "Uninstall pack?" for one version of a pack, or with an empty versionKey the whole pack.
	virtual void ConfirmUninstall(const std::string& packId, const std::string& versionKey) = 0;
	// "Delete server?" for a pack's dedicated server.
	virtual void ConfirmDeleteServer(const std::string& packId) = 0;
	// "Update pack": looks up the newest version of a library pack and offers to install it.
	virtual void CheckForUpdate(const Modpack& pack) = 0;
	// My Library > New pack...
	virtual void NewPack() = 0;
};
