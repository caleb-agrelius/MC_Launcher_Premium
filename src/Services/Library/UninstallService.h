#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Model/Modpack.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"

#include <string>

// Removing installed packs: out of My Library right away, and their folders on a worker so big
// packs don't stall the UI.
class UninstallService {
public:
	UninstallService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads, PlayService& plays);

	// Uninstalls one installed version of a library pack (versionKey), or with an empty key
	// every version and the pack itself. Deletes the versions' install and game folders
	// (worlds included), plus downloads/<id>/ and the pack's saved downloads when the whole
	// pack goes. Its server folder, if any, is kept (see ServerService::StartDelete).
	void Uninstall(const Modpack& pack, const std::string& versionKey);

private:
	Runtime m_rt;
	PackLibrary& m_library;
	DownloadService& m_downloads;
	PlayService& m_plays;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};
