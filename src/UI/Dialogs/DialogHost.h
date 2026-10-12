#pragma once

#include "UI/Dialogs/DeleteServerDialog.h"
#include "UI/Dialogs/Dialogs.h"
#include "UI/Dialogs/LaunchSettingsDialog.h"
#include "UI/Dialogs/NewPackDialogHost.h"
#include "UI/Dialogs/OptionalModsDialog.h"
#include "UI/Dialogs/StopGameDialog.h"
#include "UI/Dialogs/UninstallDialog.h"
#include "UI/Dialogs/UpdateDialog.h"

// Opens and draws the app's modal dialogs. Pages ask for one through IDialogs; the shell calls
// Draw() once a frame, after the page.
class DialogHost final : public IDialogs {
public:
	DialogHost(StopGameDialog& stop, LaunchSettingsDialog& launch, NewPackDialogHost& newPack, OptionalModsDialog& optional,
		DeleteServerDialog& deleteServer, UninstallDialog& uninstall, UpdateDialog& update)
		: m_stop(stop), m_launch(launch), m_newPack(newPack), m_optional(optional), m_deleteServer(deleteServer),
		  m_uninstall(uninstall), m_update(update) {}

	void ConfirmStopGame(const std::string& playKey) override { m_stop.Open(playKey); }
	void ShowLaunchSettings(const std::string& packId) override { m_launch.Open(packId); }
	void ConfirmUninstall(const std::string& packId, const std::string& versionKey) override { m_uninstall.Open(packId, versionKey); }
	void ConfirmDeleteServer(const std::string& packId) override { m_deleteServer.Open(packId); }
	void CheckForUpdate(const Modpack& pack) override { m_update.Open(pack); }
	void NewPack() override { m_newPack.Open(); }

	void Draw();

private:
	StopGameDialog& m_stop;
	LaunchSettingsDialog& m_launch;
	NewPackDialogHost& m_newPack;
	OptionalModsDialog& m_optional;
	DeleteServerDialog& m_deleteServer;
	UninstallDialog& m_uninstall;
	UpdateDialog& m_update;
};
