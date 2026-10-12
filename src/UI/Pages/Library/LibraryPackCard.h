#pragma once

#include "Model/Modpack.h"
#include "UI/Dialogs/Dialogs.h"
#include "UI/Navigation.h"

class ImageCache;
class PackLibrary;
class PlayService;
class ServerService;
class UpdateService;

// One installed pack in My Library: its icon, version picker, play status and the
// PLAY / Manage / Open folder / Server buttons.
class LibraryPackCard {
public:
	LibraryPackCard(PackLibrary& library, PlayService& plays, UpdateService& updates, ServerService& servers,
		ImageCache& images, INavigator& navigator, IDialogs& dialogs)
		: m_library(library), m_plays(plays), m_updates(updates), m_servers(servers), m_images(images),
		  m_navigator(navigator), m_dialogs(dialogs) {}

	void Draw(Modpack& pack);

private:
	PackLibrary& m_library;
	PlayService& m_plays;
	UpdateService& m_updates;
	ServerService& m_servers;
	ImageCache& m_images;
	INavigator& m_navigator;
	IDialogs& m_dialogs;
};
