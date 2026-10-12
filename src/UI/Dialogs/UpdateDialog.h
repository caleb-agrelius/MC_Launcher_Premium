#pragma once

#include "Model/Modpack.h"

class DownloadService;
class PackLibrary;
class PlayService;
class UpdateService;

// "Update pack": says whether a newer version exists and, if so, offers to install it next to
// the current one.
class UpdateDialog {
public:
	UpdateDialog(PackLibrary& library, UpdateService& updates, PlayService& plays, DownloadService& downloads)
		: m_library(library), m_updates(updates), m_plays(plays), m_downloads(downloads) {}

	// Looks up the newest version and opens the dialog.
	void Open(const Modpack& pack);
	void Draw();

private:
	PackLibrary& m_library;
	UpdateService& m_updates;
	PlayService& m_plays;
	DownloadService& m_downloads;
};
