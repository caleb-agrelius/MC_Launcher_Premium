#pragma once

#include "Core/Runtime.h"
#include "Model/Modpack.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"

#include <string>

// The player's edits to an installed pack (added, removed or switched mods and packs). The
// edits belong to one installed version and are saved as they happen. UI thread only.
class PackContentService {
public:
	PackContentService(const Runtime& runtime, PackLibrary& library, PlayService& plays);

	// The version the player edits: the pack's active one, else its first. Null if none installed.
	InstalledVersion* EditingVersion(Modpack& pack);

	// Notes that path was changed in a version of a pack, and saves the library.
	void RecordChange(const std::string& packId, const std::string& versionKey, const std::string& path, ContentChange change);

	// Puts the pack's active version back to the pack's own files. Files the player removed come
	// back from the pack's sources (a repair). False, after telling the player why, when the
	// changes couldn't be undone.
	bool UndoChanges(Modpack& pack);

private:
	Runtime m_rt;
	PackLibrary& m_library;
	PlayService& m_plays;
};
