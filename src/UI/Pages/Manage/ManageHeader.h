#pragma once

#include "UI/Pages/Manage/ManageTypes.h"

// The top of the Manage page: back to My Library, what the pack is, Open folder, and PLAY / Cancel /
// Stop game with the play status under it.
class ManageHeader {
public:
	ManageRequest Draw(const ManagePageContext& context);
};
