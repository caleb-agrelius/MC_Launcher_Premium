#include "Services/Library/PackContentService.h"

#include "Game/InstanceContent.h"

#include <filesystem>

namespace fs = std::filesystem;

PackContentService::PackContentService(const Runtime& runtime, PackLibrary& library, PlayService& plays)
	: m_rt(runtime), m_library(library), m_plays(plays)
{
}

InstalledVersion* PackContentService::EditingVersion(Modpack& pack)
{
	InstalledVersion* editing = nullptr;
	for (InstalledVersion& v : pack.installedVersions) {
		if (v.key == pack.activeVersion || !editing)
			editing = &v;
	}
	return editing;
}

void PackContentService::RecordChange(const std::string& packId, const std::string& versionKey, const std::string& path,
	ContentChange change)
{
	Modpack* pack = m_library.Find(packId);
	if (!pack)
		return;
	for (InstalledVersion& v : pack->installedVersions) {
		if (v.key == versionKey)
			RecordContentChange(v.contentChanges, path, change);
	}
	m_rt.log << "Pack " << packId << "@" << versionKey << " changed: " << path << "\n";
	m_library.Save();
}

bool PackContentService::UndoChanges(Modpack& pack)
{
	InstalledVersion* editing = EditingVersion(pack);
	if (!editing)
		return false;
	std::string error;
	if (!UndoContentChanges(fs::absolute(m_library.GameDir(pack)), editing->contentChanges, &error)) {
		m_rt.notify.Notify(NoticeKind::Error, "Couldn't undo your changes", error);
		return false;
	}
	// Files the player removed come back from the pack's sources.
	const bool restore = !RemovedContent(editing->contentChanges).empty();
	editing->contentChanges.clear();
	m_rt.log << "Undid changes to " << pack.id << "@" << editing->key << "\n";
	m_library.Save();
	if (restore)
		m_plays.Repair(pack);
	else
		m_rt.notify.Notify(NoticeKind::Success, "Changes undone", pack.name + " is back to the pack's own files.");
	return true;
}
