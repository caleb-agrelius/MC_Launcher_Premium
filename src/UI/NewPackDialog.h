#pragma once

#include "UI/BackgroundTasks.h"

#include <functional>
#include <string>
#include <vector>

class HttpClient;

// What the dialog needs from the app. Rebuilt every frame.
struct NewPackDialogContext {
	HttpClient* http = nullptr;
	BackgroundTasks tasks;
};

// The pack to create.
struct NewPackRequest {
	std::string name;
	std::string minecraftVersion;
	std::string loader;        // "fabric", "quilt", "forge", "neoforge", or empty for vanilla
	std::string loaderVersion;
};

// My Library > New pack...: a name, a Minecraft release and a mod loader with its version,
// both lists read from the internet (see Game/PackCreator.h). Keep one for the app's
// lifetime; background work posts back to it.
class NewPackDialog {
public:
	void Open();
	// Draws the dialog while it's open. True, with request filled in, when Create was pressed.
	bool Draw(const NewPackDialogContext& context, NewPackRequest& request);

private:
	void LoadReleases(const NewPackDialogContext& context);
	void LoadLoaderVersions(const NewPackDialogContext& context);

	bool m_open = false;
	std::string m_name;
	// Minecraft releases, newest first.
	std::vector<std::string> m_releases;
	bool m_releasesLoading = false;
	std::string m_releasesError;
	int m_release = 0;
	int m_loader = 1; // index into the loader list; Fabric
	// Versions of the chosen loader for the chosen release.
	std::vector<std::string> m_loaderVersions;
	std::string m_loaderVersionsFor; // "<loader>@<minecraft>" they belong to
	bool m_loaderLoading = false;
	std::string m_loaderError;
	int m_loaderVersion = 0;
};
