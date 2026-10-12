#include "UI/Pages/Manage/ContentModel.h"

#include "UI/Common/Background.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <algorithm>
#include <iterator>

namespace fs = std::filesystem;

void ContentModel::Reload(const ManagePageContext& context)
{
	for (int i = 0; i < kKindCount; ++i)
		lists[i] = ListContent(context.gameDir, kKinds[i]);
	error.clear();
	notice.clear();
	++m_scanGeneration;
	metadata.reset();
	scanning = false;
	ScanMetadata(context);
}

void ContentModel::ReloadFolders(const ManagePageContext& context)
{
	for (int i = 0; i < kKindCount; ++i)
		lists[i] = ListContent(context.gameDir, kKinds[i]);
	// Read the new mods' names; the old names stay shown until then.
	++m_scanGeneration;
	scanning = false;
	ScanMetadata(context);
}

int ContentModel::AddFiles(const ManagePageContext& context, const std::vector<fs::path>& paths, int preferredKind)
{
	notice.clear();
	if (context.playBusy) {
		error = "Close the game before adding files.";
		return -1;
	}
	std::vector<std::string> added;
	std::string errors;
	int shownKind = -1;
	for (const fs::path& path : paths) {
		ContentKind kind;
		std::string name, problem;
		if (!ContentKindForFile(path, kKinds[preferredKind], &kind)) {
			problem = path.filename().u8string() + " isn't a mod (.jar), resource pack or shader pack (.zip or folder).";
		} else if (AddContent(context.gameDir, kind, path, &name, &problem)) {
			added.push_back(name);
			if (context.recordChange)
				context.recordChange(ContentPath(kind, name), ContentChange::Added);
			if (shownKind < 0)
				shownKind = static_cast<int>(std::find(std::begin(kKinds), std::end(kKinds), kind) - std::begin(kKinds));
			continue;
		}
		errors += (errors.empty() ? "" : "\n") + problem;
	}
	error = errors;
	if (added.empty())
		return -1;
	notice = added.size() == 1 ? "Added " + added.front() + "." : "Added " + std::to_string(added.size()) + " files.";
	ReloadFolders(context);
	return shownKind;
}

void ContentModel::ScanMetadata(const ManagePageContext& context)
{
	if (scanning)
		return;
	scanning = true;
	const fs::path folder = context.gameDir / GetContentKind(ContentKind::Mods).folder;
	const std::vector<ContentFile> mods = lists[0];
	const int generation = m_scanGeneration;
	RunInBackground(context.tasks, [folder, mods]() {
		std::map<std::string, ModMetadata> found;
		for (const ContentFile& mod : mods) {
			fs::path path = folder / fs::u8path(mod.fileName);
			if (!mod.enabled)
				path += ".disabled";
			ModMetadata data;
			if (ReadModMetadata(path, data))
				found[mod.fileName] = std::move(data);
		}
		return found;
	}, [this, generation](std::map<std::string, ModMetadata> found) {
		if (generation != m_scanGeneration)
			return;
		metadata = std::make_shared<std::map<std::string, ModMetadata>>(std::move(found));
		scanning = false;
	});
}

void ContentModel::DrawMessages() const
{
	if (!error.empty())
		Widgets::WrappedText(Theme::C().error, error.c_str());
	if (!notice.empty())
		Widgets::WrappedText(Theme::C().accent, notice.c_str());
}
