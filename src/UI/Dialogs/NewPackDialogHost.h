#pragma once

#include "Core/Runtime.h"
#include "UI/Navigation.h"
#include "UI/NewPackDialog.h"

class CustomPackService;

// My Library > New pack...: shows the dialog and, when the player presses Create, makes the
// pack and opens its Manage page on Content > Browse Modrinth.
class NewPackDialogHost {
public:
	NewPackDialogHost(const Runtime& runtime, CustomPackService& packs, INavigator& navigator)
		: m_rt(runtime), m_packs(packs), m_navigator(navigator) {}

	void Open() { m_dialog.Open(); }
	void Draw();

private:
	Runtime m_rt;
	CustomPackService& m_packs;
	INavigator& m_navigator;
	NewPackDialog m_dialog;
};
