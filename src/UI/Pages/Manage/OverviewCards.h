#pragma once

#include "UI/Pages/Manage/ManageTypes.h"

// The Overview tab's cards. Each sets request when a button in it is pressed.

// Every installed version, with Make active and Uninstall.
class InstalledVersionsCard {
public:
	void Draw(const ManagePageContext& context, ManageRequest& request);
};

// What the player changed in the active version, and Undo my changes.
class PlayerChangesCard {
public:
	void Draw(const ManagePageContext& context, ManageRequest& request);

private:
	bool m_confirmUndo = false; // the "Undo my changes" confirmation is open
};

// Folders, launch settings, update, repair, host a server, uninstall.
class PackToolsCard {
public:
	void Draw(const ManagePageContext& context, ManageRequest& request);
};
