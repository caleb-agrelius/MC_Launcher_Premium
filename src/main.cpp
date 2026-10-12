#include "imgui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Model/Modpack.h"
#include "Network/HttpClient.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"
#include "Storage/InstallPlan.h"
#include "Storage/Installer.h"
#include "Storage/Library.h"
#include "Storage/Settings.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace fs = std::filesystem;
using Theme::S;

static const fs::path kIndexRelativePath = fs::path("resources") / "index.json";
static const char* kSettingsFile = "settings.json";
static const char* kLibraryFile = "library.json";
static const char* kDownloadsFile = "downloads.json";
static constexpr float kUiScaleStep = 0.1f;
static constexpr double kSearchDelaySeconds = 0.4; // pause after typing before searching
static constexpr int kSearchLimit = 30;            // results per search

static fs::path GetExecutableDir(const char* argv0)
{
	std::error_code ec;
#ifdef _WIN32
	wchar_t buffer[MAX_PATH * 4];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length > 0 && length < std::size(buffer))
		return fs::path(buffer, buffer + length).parent_path();
#else
	const fs::path self = fs::read_symlink("/proc/self/exe", ec);
	if (!ec)
		return self.parent_path();
#endif
	if (argv0 != nullptr)
		return fs::absolute(argv0, ec).parent_path();
	return {};
}

// Finds the folder holding resources/index.json: the working directory first,
// then the executable's folder and each of its parents (so launching
// build/Debug/MC_Launcher_Premium.exe directly still finds the repo root).
// Returns an empty path if none is found.
static fs::path FindDataRoot(const char* argv0)
{
	std::error_code ec;
	const fs::path cwd = fs::current_path(ec);
	if (!ec && fs::exists(cwd / kIndexRelativePath, ec))
		return cwd;

	for (fs::path dir = GetExecutableDir(argv0); !dir.empty(); dir = dir.parent_path()) {
		if (fs::exists(dir / kIndexRelativePath, ec))
			return dir;
		if (dir == dir.parent_path())
			break;
	}
	return {};
}

struct DownloadState {
	std::string fileName;
	Modpack pack; // pack the file belongs to, for the Downloads page (name, icon)
	double total = 0.0;
	double now = 0.0;
	bool active = false;
	std::string error;
	std::string outPath;
	bool installed = false;
	bool installing = false;
	double installProgress = 0.0;
	std::string installError;

	// Whole-pack installs (sources that install from a plan, e.g. CurseForge). These are
	// keyed "pack:<pack id>"; active/error/installed above describe the whole install.
	bool planInstall = false;
	std::string stage;            // "Resolving files", "Downloading files", ...
	std::uint64_t stageDone = 0;
	std::uint64_t stageTotal = 0;
	std::vector<PlannedFile> blocked; // files the user has to download by hand
	std::vector<std::string> notes;
};

enum class Page {
	Discover,
	Library,
	Downloads
};

// One Discover sub-page: a source's search box, filters, results and selection.
// Each source keeps its own state, so switching sub-pages doesn't lose a search.
struct SourceView {
	PackSource source = PackSource::Modrinth;
	SourceCapabilities caps;
	char search[128] = "";
	char gameVersion[32] = "";
	int loader = 0; // 0 = any, else caps.loaders[loader - 1]
	int sort = 0;   // index into caps.sorts
	std::vector<Modpack> packs;
	int selected = -1;
	// Typing schedules a search; it runs once the input has been unchanged for
	// kSearchDelaySeconds. The first visit runs the default (empty) search.
	bool searchPending = true;
	double lastSearchEdit = -1.0;
	bool searching = false;
	int searchGeneration = 0; // results from older searches are ignored
	std::string searchStatus;
	bool searchFailed = false;
};

static const char* LoaderLabel(const char* loader)
{
	const std::string key = loader;
	if (key == "forge") return "Forge";
	if (key == "neoforge") return "NeoForge";
	if (key == "fabric") return "Fabric";
	if (key == "quilt") return "Quilt";
	return loader;
}

static std::string FormatFileSize(std::uint64_t bytes)
{
	const char* units[] = { "B", "KB", "MB", "GB" };
	double size = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (size >= 1024.0 && unit < 3) {
		size /= 1024.0;
		++unit;
	}
	char formatted[48];
	std::snprintf(formatted, sizeof(formatted), unit == 0 ? "%.0f %s" : "%.1f %s", size, units[unit]);
	return formatted;
}

// Formats large counts compactly: 950, 12.3K, 18.4M.
static std::string FormatCount(std::uint64_t count)
{
	char formatted[32];
	if (count >= 1000000)
		std::snprintf(formatted, sizeof(formatted), "%.1fM", count / 1000000.0);
	else if (count >= 1000)
		std::snprintf(formatted, sizeof(formatted), "%.1fK", count / 1000.0);
	else
		std::snprintf(formatted, sizeof(formatted), "%llu", static_cast<unsigned long long>(count));
	return formatted;
}

static std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

static bool ContainsIgnoreCase(const std::string& haystack, const std::string& lowerNeedle)
{
	return ToLower(haystack).find(lowerNeedle) != std::string::npos;
}

// A background task plus a flag it sets when finished, so the UI loop can join
// and drop completed threads instead of keeping them until exit.
struct Worker {
	std::thread thread;
	std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
};

static void StartWorker(std::vector<Worker>& workers, std::function<void()> task)
{
	Worker worker;
	worker.thread = std::thread([task = std::move(task), done = worker.done]() {
		task();
		done->store(true);
	});
	workers.push_back(std::move(worker));
}

static void ReapFinishedWorkers(std::vector<Worker>& workers)
{
	for (auto it = workers.begin(); it != workers.end();) {
		if (it->done->load()) {
			it->thread.join();
			it = workers.erase(it);
		} else {
			++it;
		}
	}
}

// Work handed from background threads to the UI thread. Drain() runs it at the
// start of a frame, so UI state (pack list, selection) is only touched on the UI thread.
struct UiQueue {
	std::mutex mutex;
	std::vector<std::function<void()>> tasks;

	void Post(std::function<void()> task)
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(std::move(task));
	}

	void Drain()
	{
		std::vector<std::function<void()>> pending;
		{
			std::lock_guard<std::mutex> lock(mutex);
			pending.swap(tasks);
		}
		for (auto& task : pending)
			task();
	}
};

// Asks Windows 10/11 to draw the window's title bar in dark mode to match the app.
static void UseDarkTitleBar(GLFWwindow* window)
{
#ifdef _WIN32
	const BOOL dark = TRUE;
	DwmSetWindowAttribute(glfwGetWin32Window(window), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
#else
	(void)window;
#endif
}

// Rounds to the nearest 5% and keeps the scale within the supported range.
static float NormalizeUiScale(float scale)
{
	return std::clamp(std::round(scale * 20.0f) / 20.0f, kMinUiScale, kMaxUiScale);
}

// Moves the cursor so a button with this label ends at the right edge of the content region.
static void AlignRightForButton(const char* label)
{
	const float width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), x));
}

// Starts a card: a bordered child with the card background color that grows to fit
// its contents. Pair with EndCard().
static void BeginCard(const char* id)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kCardBg);
	ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
}

static void EndCard()
{
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

// Draws a pack icon at the cursor and leaves the cursor beside it, ready for SameLine text.
static void PackIconItem(const Modpack& pack, ImageCache& images, float size)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	Widgets::DrawPackIcon(ImGui::GetWindowDrawList(), images.Get(pack.iconUrl), pack, pos, size, S(8.0f));
}

int main(int argc, char** argv)
{
	(void)argc;
	// Run from the folder that holds resources/, so the index, app.log,
	// downloads/ and installs/ all resolve the same way however the app is launched.
	const fs::path dataRoot = FindDataRoot(argc > 0 ? argv[0] : nullptr);
	std::error_code chdirError;
	if (!dataRoot.empty())
		fs::current_path(dataRoot, chdirError);
	std::ofstream log("app.log", std::ios::app);
	try {
		log << "Starting app\n";
		if (dataRoot.empty())
			log << "Could not find " << kIndexRelativePath.string() << " near the working directory or executable\n";
		else
			log << "Data folder: " << dataRoot.string() << "\n";
		if (!glfwInit()) {
			log << "glfwInit failed\n";
			return -1;
		}

		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		// Size the window in logical pixels, so it isn't tiny on high-DPI (e.g. 4K at 150%) monitors.
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		GLFWwindow* window = glfwCreateWindow(1360, 840, "MC Launcher Premium", nullptr, nullptr);
		if (window == nullptr) {
			log << "glfwCreateWindow failed\n";
			glfwTerminate();
			return -1;
		}
		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);
		UseDarkTitleBar(window);

		if (!gladLoadGL()) {
			fprintf(stderr, "Failed to initialize GL loader\n");
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		// UI scale = monitor DPI scale (from Windows display settings) x the user's UI scale setting.
		Settings settings = LoadSettings(kSettingsFile);
		float requestedUiScale = settings.uiScale;
		float dpiScale = 1.0f;
		glfwGetWindowContentScale(window, &dpiScale, nullptr);
		Theme::LoadFonts();
		Theme::Apply(dpiScale, settings.uiScale);
		glfwSetWindowSizeLimits(window, static_cast<int>(S(900.0f)), static_cast<int>(S(600.0f)), GLFW_DONT_CARE, GLFW_DONT_CARE);
		log << "DPI scale " << dpiScale << ", UI scale " << settings.uiScale << "\n";

		if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
			log << "ImGui GLFW backend initialization failed\n";
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}
		if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
			log << "ImGui OpenGL backend initialization failed\n";
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}

		// The local index is the offline fallback; Discover normally shows Modrinth results.
		const std::vector<Modpack> localPacks = LoadIndex(kIndexRelativePath.string());
		log << "Loaded " << localPacks.size() << " local modpack(s)\n";
		log.flush();
		// One Discover sub-page per online source. std::map keeps references stable,
		// so background tasks can hold a SourceView& safely.
		std::map<PackSource, SourceView> views;
		for (PackSource source : OnlineSources()) {
			SourceView& view = views[source];
			view.source = source;
			view.caps = GetSourceCapabilities(source);
		}
		PackSource discoverSource = PackSourceFromKey(settings.discoverSource);
		if (views.count(discoverSource) == 0)
			discoverSource = OnlineSources().front();
		Page page = Page::Discover;
		HttpClient http;
		ImageCache images(http);
		std::map<std::string, DownloadState> downloads;
		std::mutex downloadsMutex;
		std::vector<Worker> workers;
		std::atomic<bool> shuttingDown{ false };
		UiQueue uiQueue;
		fs::create_directories("downloads");
		fs::create_directories("installs");

		// Per-pack version lookups for online results, keyed by pack id (unique across sources).
		std::set<std::string> filesLoading;
		std::map<std::string, std::string> filesErrors;
		// Full-size banner lookups, keyed by pack id.
		std::set<std::string> bannersLoading;
		// Packs whose files were extracted (UI thread only). Saved to library.json so
		// My Library survives restarts; packs whose installs/ folder is gone are dropped.
		Library libraryPacks = LoadPackLibrary(kLibraryFile);
		for (auto it = libraryPacks.begin(); it != libraryPacks.end();) {
			std::error_code existsError;
			if (fs::is_directory(fs::path("installs") / it->first, existsError))
				++it;
			else
				it = libraryPacks.erase(it);
		}
		log << "Loaded " << libraryPacks.size() << " library pack(s)\n";

		// Finished downloads from earlier sessions, so Discover offers "Install files" again.
		// Entries whose file was deleted are skipped. Workers aren't running yet, so no lock.
		for (SavedDownload& saved : LoadSavedDownloads(kDownloadsFile)) {
			std::error_code existsError;
			if (!fs::is_regular_file(saved.outPath, existsError))
				continue;
			DownloadState state;
			state.fileName = std::move(saved.fileName);
			state.outPath = std::move(saved.outPath);
			state.installed = libraryPacks.count(saved.pack.id) > 0;
			state.pack = std::move(saved.pack);
			downloads[saved.url] = std::move(state);
		}
		log << "Loaded " << downloads.size() << " saved download(s)\n";

		// Writes every finished download to downloads.json. UI thread only.
		auto saveDownloads = [&]() {
			std::vector<SavedDownload> saved;
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				for (const auto& [url, state] : downloads) {
					if (!state.outPath.empty())
						saved.push_back({ url, state.fileName, state.outPath, state.pack });
				}
			}
			if (!SaveDownloads(kDownloadsFile, saved))
				log << "Could not save " << kDownloadsFile << "\n";
		};

		// Extracts a downloaded file into installs/<pack id>/ and, on success, adds
		// the pack to My Library. Used by Discover and the Downloads page.
		auto startInstall = [&](const std::string& url, const Modpack& pack, const std::string& zipPath) {
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				DownloadState& current = downloads[url];
				current.installing = true;
				current.installProgress = 0.0;
				current.installError.clear();
			}
			const std::string targetDir = (fs::path("installs") / pack.id).string();
			StartWorker(workers, [zipPath, targetDir, &downloads, &downloadsMutex, &shuttingDown, &uiQueue,
				&libraryPacks, &log, url, pack]() {
				std::string error;
				auto progress = [&](int total, int now) {
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					if (total > 0)
						current.installProgress = static_cast<double>(now) / static_cast<double>(total);
				};
				const bool ok = InstallZip(zipPath, targetDir, progress, &error, &shuttingDown);
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					current.installing = false;
					if (ok)
						current.installed = true;
					else
						current.installError = error;
				}
				if (ok) {
					uiQueue.Post([&libraryPacks, &log, pack]() {
						libraryPacks[pack.id] = pack;
						if (!SavePackLibrary(kLibraryFile, libraryPacks))
							log << "Could not save " << kLibraryFile << "\n";
					});
				}
			});
		};

		auto startSearch = [&](SourceView& view) {
			const int generation = ++view.searchGeneration;
			const std::string query = view.search;
			SearchFilters filters;
			filters.gameVersion = view.gameVersion;
			if (view.loader > 0 && view.loader <= static_cast<int>(view.caps.loaders.size()))
				filters.loader = view.caps.loaders[static_cast<std::size_t>(view.loader - 1)];
			filters.sort = view.sort;
			view.searching = true;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &localPacks, &log, query, filters, generation]() {
				SourceSearchResult result;
				std::string error;
				const bool ok = SearchPacks(http, view.source, query, filters, kSearchLimit, result, &error, &shuttingDown);
				uiQueue.Post([&view, &localPacks, &log, query, generation, ok, result, error]() {
					if (generation != view.searchGeneration)
						return;
					const char* sourceName = PackSourceName(view.source);
					view.searching = false;
					view.searchFailed = !ok;
					if (ok) {
						view.packs = result.packs;
						view.searchStatus = FormatCount(result.totalHits) + " modpacks on " + sourceName;
						log << sourceName << " search \"" << query << "\": " << result.packs.size() << " of "
							<< result.totalHits << " result(s)\n";
					} else {
						log << sourceName << " search \"" << query << "\" failed: " << error << "\n";
						view.packs.clear();
						if (view.source == PackSource::Modrinth) {
							// Offline or API error: fall back to the local index.
							const std::string lowerQuery = ToLower(query);
							for (const Modpack& pack : localPacks) {
								if (lowerQuery.empty() || ContainsIgnoreCase(pack.name, lowerQuery) ||
									ContainsIgnoreCase(pack.description, lowerQuery))
									view.packs.push_back(pack);
							}
							view.searchStatus = "Modrinth unavailable (" + error + ") - showing local packs";
						} else {
							view.searchStatus = std::string(sourceName) + " unavailable (" + error + ")";
						}
					}
					view.selected = view.packs.empty() ? -1 : 0;
				});
			});
		};

		auto startFetchFiles = [&](SourceView& view, const Modpack& pack) {
			const std::string packId = pack.id;
			filesLoading.insert(packId);
			filesErrors.erase(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &filesLoading, &filesErrors, &log, pack, packId]() {
				PackVersionInfo version;
				std::string error;
				const bool ok = FetchPackVersion(http, pack, version, &error, &shuttingDown);
				uiQueue.Post([&view, &filesLoading, &filesErrors, &log, packId, ok, version, error]() {
					filesLoading.erase(packId);
					if (!ok) {
						log << "Version lookup for " << packId << " failed: " << error << "\n";
						filesErrors[packId] = error;
						return;
					}
					log << "Version lookup for " << packId << ": " << version.versionNumber << " ("
						<< (version.files.empty() ? "id " + version.sourceVersionId : version.files.front().name) << ")\n";
					for (Modpack& item : view.packs) {
						if (item.id == packId)
							ApplyPackVersion(item, version);
					}
				});
			});
		};

		auto startFetchBanner = [&](SourceView& view, const std::string& packId) {
			bannersLoading.insert(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &bannersLoading, &log, packId]() {
				std::string url;
				std::string error;
				const bool ok = FetchModrinthBanner(http, packId, url, &error, &shuttingDown);
				uiQueue.Post([&view, &bannersLoading, &log, packId, ok, url, error]() {
					bannersLoading.erase(packId);
					if (!ok)
						log << "Banner lookup for " << packId << " failed: " << error << "\n";
					for (Modpack& pack : view.packs) {
						if (pack.id != packId)
							continue;
						pack.bannerHiResUrl = url;
						pack.bannerLoaded = true; // also on failure: keep the thumbnail, don't retry
					}
				});
			});
		};

		// Installs a whole pack (sources whose capabilities say installsPacks): builds the
		// source's install plan, runs it into installs/<pack id>/, then adds the pack to My Library.
		auto startPlanInstall = [&](const Modpack& pack) {
			const std::string key = "pack:" + pack.id;
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				DownloadState started;
				started.fileName = pack.version.empty() ? pack.name : pack.name + " " + pack.version;
				started.pack = pack;
				started.active = true;
				started.planInstall = true;
				started.stage = "Preparing";
				downloads[key] = std::move(started);
			}
			log << "Installing " << pack.id << " " << pack.version << " from " << PackSourceName(pack.source) << "\n";
			StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, &libraryPacks, &log, key, pack]() {
				auto setStage = [&](const std::string& stage, std::uint64_t done, std::uint64_t total) {
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[key];
					current.stage = stage;
					current.stageDone = done;
					current.stageTotal = total;
				};
				std::string error;
				InstallPlan plan;
				std::vector<PlannedFile> blocked;
				const std::string workDir = (fs::path("downloads") / pack.id).string();
				std::error_code dirError;
				fs::create_directories(workDir, dirError);
				bool ok = BuildInstallPlan(http, pack, workDir, plan,
					[&](const std::string& status) { setStage(status, 0, 0); }, &error, &shuttingDown);
				if (ok) {
					ok = RunInstallPlan(http, plan, (fs::path("installs") / pack.id).string(),
						[&](const InstallProgress& progress) { setStage(progress.stage, progress.done, progress.total); },
						blocked, &error, &shuttingDown);
				}
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[key];
					current.active = false;
					current.installed = ok;
					current.error = ok ? std::string() : error;
					current.blocked = blocked;
					current.notes = plan.notes;
				}
				uiQueue.Post([&libraryPacks, &log, pack, ok, error, blockedCount = blocked.size(), fileCount = plan.files.size()]() {
					if (!ok) {
						log << "Install of " << pack.id << " failed: " << error << "\n";
						return;
					}
					log << "Installed " << pack.id << ": " << fileCount << " file(s), " << blockedCount << " blocked\n";
					libraryPacks[pack.id] = pack;
					if (!SavePackLibrary(kLibraryFile, libraryPacks))
						log << "Could not save " << kLibraryFile << "\n";
				});
			});
		};

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			ReapFinishedWorkers(workers);
			uiQueue.Drain();

			// Rebuild the style when the window moves to a monitor with a different DPI
			// or the user changes the UI scale.
			float contentScale = dpiScale;
			glfwGetWindowContentScale(window, &contentScale, nullptr);
			if (contentScale > 0.0f && (contentScale != dpiScale || requestedUiScale != settings.uiScale)) {
				dpiScale = contentScale;
				if (requestedUiScale != settings.uiScale) {
					settings.uiScale = requestedUiScale;
					if (!SaveSettings(kSettingsFile, settings))
						log << "Could not save " << kSettingsFile << "\n";
				}
				Theme::Apply(dpiScale, settings.uiScale);
				glfwSetWindowSizeLimits(window, static_cast<int>(S(900.0f)), static_cast<int>(S(600.0f)), GLFW_DONT_CARE, GLFW_DONT_CARE);
			}
			images.Update(ImGui::GetTime());

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// Ctrl + / Ctrl - / Ctrl 0 change the UI scale (applied next frame).
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd))
				requestedUiScale = NormalizeUiScale(settings.uiScale + kUiScaleStep);
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract))
				requestedUiScale = NormalizeUiScale(settings.uiScale - kUiScaleStep);
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Keypad0))
				requestedUiScale = 1.0f;

			const Theme::Fonts& fonts = Theme::GetFonts();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("MC_Launcher_Premium", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

			// ---- Sidebar ----
			ImGui::BeginChild("sidebar", ImVec2(S(230.0f), 0.0f), ImGuiChildFlags_Borders);
			{
				// Logo: a little grass block, then the name.
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				const ImVec2 logo = ImGui::GetCursorScreenPos();
				const float block = S(34.0f);
				const float px = block / 8.0f;
				for (int y = 0; y < 8; ++y) {
					for (int x = 0; x < 8; ++x) {
						const bool grass = y < 3 || (y == 3 && (x * 7 + 3) % 3 == 0);
						const float shade = 0.85f + 0.15f * static_cast<float>((x * 5 + y * 3) % 4) / 3.0f;
						const ImVec4 color = grass ? ImVec4(0.30f * shade, 0.68f * shade, 0.36f * shade, 1.0f)
							: ImVec4(0.47f * shade, 0.33f * shade, 0.22f * shade, 1.0f);
						drawList->AddRectFilled(ImVec2(logo.x + x * px, logo.y + y * px),
							ImVec2(logo.x + (x + 1) * px, logo.y + (y + 1) * px), ImGui::GetColorU32(color));
					}
				}
				ImGui::Dummy(ImVec2(block, block));
				ImGui::SameLine(0.0f, S(12.0f));
				ImGui::BeginGroup();
				Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::kText, "MC Launcher");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6.0f));
				Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::kAccent, "PREMIUM  \xC2\xB7  JAVA");
				ImGui::EndGroup();
			}
			ImGui::Dummy(ImVec2(0.0f, S(14.0f)));
			Widgets::SectionLabel("BROWSE");
			if (Widgets::NavItem("Discover", page == Page::Discover))
				page = Page::Discover;
			// One sub-page per source. The choice is remembered in settings.json.
			for (PackSource source : OnlineSources()) {
				if (Widgets::SubNavItem(PackSourceName(source), page == Page::Discover && discoverSource == source)) {
					page = Page::Discover;
					if (discoverSource != source) {
						discoverSource = source;
						settings.discoverSource = PackSourceKey(source);
						if (!SaveSettings(kSettingsFile, settings))
							log << "Could not save " << kSettingsFile << "\n";
					}
				}
			}
			if (Widgets::NavItem("My Library", page == Page::Library))
				page = Page::Library;
			if (Widgets::NavItem("Downloads", page == Page::Downloads))
				page = Page::Downloads;

			// Footer: UI scale control.
			const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() * 2.0f +
				ImGui::GetStyle().ItemSpacing.y * 3.0f;
			ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - footerHeight - ImGui::GetStyle().WindowPadding.y));
			ImGui::Separator();
			Widgets::SectionLabel("UI SCALE");
			if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
				requestedUiScale = NormalizeUiScale(settings.uiScale - kUiScaleStep);
			ImGui::SetItemTooltip("Smaller (Ctrl -)");
			ImGui::SameLine();
			char scaleLabel[32];
			std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f%%##scale", settings.uiScale * 100.0f);
			const float plusWidth = ImGui::GetFrameHeight();
			if (ImGui::Button(scaleLabel, ImVec2(ImGui::GetContentRegionAvail().x - plusWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
				requestedUiScale = 1.0f;
			ImGui::SetItemTooltip("Reset to 100%% (Ctrl 0)");
			ImGui::SameLine();
			if (ImGui::Button("+", ImVec2(plusWidth, 0.0f)))
				requestedUiScale = NormalizeUiScale(settings.uiScale + kUiScaleStep);
			ImGui::SetItemTooltip("Larger (Ctrl +)");
			ImGui::TextDisabled("Launching: coming soon");
			ImGui::EndChild();

			// ---- Main content ----
			ImGui::SameLine();
			ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
			Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::kAccent, "MINECRAFT  /  MODPACKS");
			if (page == Page::Discover) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::kText,
					(std::string("Discover on ") + PackSourceName(discoverSource)).c_str());
				ImGui::TextDisabled("Find a pack, download its files, and prepare it for Minecraft.");
			} else if (page == Page::Library) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::kText, "My library");
				ImGui::TextDisabled("Modpack files you have extracted are listed here.");
			} else {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::kText, "Downloads");
				ImGui::TextDisabled("Track downloads and extraction progress.");
			}
			ImGui::Spacing();

			if (page == Page::Discover) {
				SourceView& view = views[discoverSource];
				const SourceCapabilities& caps = view.caps;
				const char* sourceName = PackSourceName(view.source);
				std::string unavailableReason;
				const bool sourceAvailable = IsSourceAvailable(view.source, &unavailableReason);

				// Search box, then this source's filters on one row.
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(11.0f)));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
				ImGui::BeginDisabled(!sourceAvailable);
				if (ImGui::InputTextWithHint("##search", caps.searchHint, view.search, sizeof(view.search))) {
					view.searchPending = true;
					view.lastSearchEdit = ImGui::GetTime();
				}
				ImGui::EndDisabled();
				ImGui::PopStyleVar(2);

				bool filtersChanged = false; // combos search right away; text waits for typing to pause
				ImGui::BeginDisabled(!sourceAvailable);
				if (caps.gameVersionFilter) {
					ImGui::SetNextItemWidth(S(170.0f));
					if (ImGui::InputTextWithHint("##mcversion", "Minecraft version", view.gameVersion, sizeof(view.gameVersion))) {
						view.searchPending = true;
						view.lastSearchEdit = ImGui::GetTime();
					}
					ImGui::SetItemTooltip("Only packs for this Minecraft version, e.g. 1.20.1");
					ImGui::SameLine();
				}
				if (caps.loaderFilter) {
					ImGui::SetNextItemWidth(S(150.0f));
					const char* preview = view.loader == 0 ? "Any loader" : LoaderLabel(caps.loaders[static_cast<std::size_t>(view.loader - 1)]);
					if (ImGui::BeginCombo("##loader", preview)) {
						if (Widgets::ListItem("Any loader", view.loader == 0)) {
							view.loader = 0;
							filtersChanged = true;
						}
						for (std::size_t i = 0; i < caps.loaders.size(); ++i) {
							if (Widgets::ListItem(LoaderLabel(caps.loaders[i]), view.loader == static_cast<int>(i + 1))) {
								view.loader = static_cast<int>(i + 1);
								filtersChanged = true;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
				}
				if (caps.sorts.size() > 1) {
					ImGui::SetNextItemWidth(S(200.0f));
					const std::string sortPreview = std::string("Sort: ") + caps.sorts[static_cast<std::size_t>(view.sort)];
					if (ImGui::BeginCombo("##sort", sortPreview.c_str())) {
						for (std::size_t i = 0; i < caps.sorts.size(); ++i) {
							if (Widgets::ListItem(caps.sorts[i], view.sort == static_cast<int>(i))) {
								view.sort = static_cast<int>(i);
								filtersChanged = true;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
				}
				ImGui::EndDisabled();
				if (filtersChanged) {
					view.searchPending = true;
					view.lastSearchEdit = -1.0; // no typing delay
				}
				// The status goes after the filters, or on its own line when it doesn't fit.
				const std::string& statusText = !sourceAvailable ? unavailableReason : view.searchStatus;
				if (caps.gameVersionFilter || caps.loaderFilter || caps.sorts.size() > 1) {
					if (ImGui::CalcTextSize(statusText.c_str()).x + S(30.0f) > ImGui::GetContentRegionAvail().x)
						ImGui::NewLine();
				}
				ImGui::AlignTextToFramePadding();
				if (!sourceAvailable) {
					Widgets::WrappedText(Theme::kError, unavailableReason.c_str());
				} else if (view.searching) {
					Widgets::Spinner(Theme::kAccent);
					ImGui::SameLine();
					ImGui::TextDisabled("Searching %s...", sourceName);
				} else if (view.searchFailed) {
					Widgets::WrappedText(Theme::kError, view.searchStatus.c_str());
				} else {
					ImGui::TextDisabled("%s", view.searchStatus.c_str());
				}
				if (sourceAvailable && view.searchPending && ImGui::GetTime() - view.lastSearchEdit >= kSearchDelaySeconds) {
					view.searchPending = false;
					startSearch(view);
				}

				// Pack list
				std::vector<Modpack>& packs = view.packs;
				int& selected = view.selected;
				const bool browsing = view.search[0] == '\0' && view.gameVersion[0] == '\0' && view.loader == 0 && view.sort == 0;
				ImGui::BeginChild("pack_list", ImVec2(S(340.0f), 0.0f), ImGuiChildFlags_Borders);
				Widgets::SectionLabel(browsing ? caps.browseLabel : "RESULTS");
				for (std::size_t i = 0; i < packs.size(); ++i) {
					const Modpack& item = packs[i];
					std::string meta;
					if (item.source == PackSource::LocalIndex) {
						meta = item.version + "  \xC2\xB7  " + std::to_string(item.files.size()) + " files";
					} else {
						meta = item.gameVersion;
						if (item.downloads > 0)
							meta += (meta.empty() ? "" : "  \xC2\xB7  ") + FormatCount(item.downloads) + " downloads";
						if (meta.empty())
							meta = item.author;
						if (meta.empty())
							meta = PackSourceName(item.source); // e.g. Technic search results, until selected
					}
					ImGui::PushID(static_cast<int>(i));
					if (Widgets::PackRow(item, images, selected == static_cast<int>(i), meta))
						selected = static_cast<int>(i);
					ImGui::PopID();
				}
				if (packs.empty() && !view.searching && sourceAvailable)
					ImGui::TextDisabled("No modpacks found.");
				ImGui::EndChild();
				ImGui::SameLine();

				// Pack details
				ImGui::BeginChild("pack_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
				if (selected >= 0 && selected < static_cast<int>(packs.size())) {
					const Modpack& pack = packs[static_cast<std::size_t>(selected)];
					const bool online = pack.source != PackSource::LocalIndex;
					const bool loadingFiles = filesLoading.count(pack.id) > 0;
					const auto filesError = filesErrors.find(pack.id);
					if (online && !pack.filesLoaded && !loadingFiles && filesError == filesErrors.end())
						startFetchFiles(view, pack);
					if (pack.source == PackSource::Modrinth && !pack.bannerLoaded && bannersLoading.count(pack.id) == 0)
						startFetchBanner(view, pack.id);

					Widgets::HeroBanner(pack, images, std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, S(200.0f), S(340.0f)));
					ImGui::Spacing();

					if (online) {
						if (pack.downloads > 0) {
							Widgets::Chip((FormatCount(pack.downloads) + " downloads").c_str(), Theme::kAccent);
							ImGui::SameLine();
						}
						if (!pack.websiteUrl.empty()) {
							std::string shown = pack.websiteUrl;
							for (const char* scheme : { "https://", "http://" }) {
								if (shown.rfind(scheme, 0) == 0)
									shown.erase(0, std::strlen(scheme));
							}
							ImGui::AlignTextToFramePadding();
							ImGui::TextLinkOpenURL(shown.c_str(), pack.websiteUrl.c_str());
						}
					}
					ImGui::Spacing();
					ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
					ImGui::TextWrapped("%s", pack.description.c_str());
					ImGui::PopFont();
					ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

					if (caps.installsPacks && online) {
						// Whole-pack install: one card for the pack's latest version.
						Widgets::SectionLabel("INSTALL");
						const std::string key = "pack:" + pack.id;
						DownloadState state;
						bool hasState = false;
						{
							std::lock_guard<std::mutex> lock(downloadsMutex);
							const auto it = downloads.find(key);
							if (it != downloads.end()) {
								state = it->second;
								hasState = true;
							}
						}
						const auto libraryEntry = libraryPacks.find(pack.id);
						const bool inLibrary = libraryEntry != libraryPacks.end() &&
							libraryEntry->second.sourceVersionId == pack.sourceVersionId;

						BeginCard("install_card");
						ImGui::AlignTextToFramePadding();
						if (loadingFiles) {
							Widgets::Spinner(Theme::kAccent);
							ImGui::SameLine();
							ImGui::TextDisabled("Loading latest version...");
						} else if (filesError != filesErrors.end()) {
							ImGui::TextColored(Theme::kError, "Couldn't load versions: %s", filesError->second.c_str());
							AlignRightForButton("Retry");
							if (ImGui::Button("Retry"))
								startFetchFiles(view, pack);
						} else if (pack.filesLoaded) {
							ImGui::PushFont(fonts.bold, 0.0f);
							ImGui::TextUnformatted(pack.version.empty() ? "Latest version" : pack.version.c_str());
							ImGui::PopFont();
							ImGui::SameLine();
							std::string details;
							if (!pack.gameVersion.empty())
								details = "Minecraft " + pack.gameVersion;
							if (!pack.loaders.empty())
								details += (details.empty() ? "" : "  \xC2\xB7  ") + pack.loaders;
							ImGui::TextDisabled("%s", details.c_str());

							if (hasState && state.active) {
								std::string progressLabel = state.stage;
								if (state.stageTotal > 0)
									progressLabel += "  " + std::to_string(state.stageDone) + " / " + std::to_string(state.stageTotal);
								AlignRightForButton(progressLabel.c_str());
								ImGui::TextDisabled("%s", progressLabel.c_str());
							} else {
								const bool installed = (hasState && state.installed) || inLibrary;
								const char* label = installed ? "Reinstall" : (hasState && !state.error.empty() ? "Retry" : "Install pack");
								AlignRightForButton(label);
								if (ImGui::Button(label))
									startPlanInstall(pack);
							}
						}
						if (hasState && state.active) {
							if (state.stageTotal > 0)
								Widgets::ThinProgressBar(static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)));
							else
								Widgets::ThinProgressBar(0.0f);
						}
						if (hasState && !state.error.empty())
							ImGui::TextColored(Theme::kError, "Install failed: %s", state.error.c_str());
						if (!(hasState && state.active) && ((hasState && state.installed) || inLibrary)) {
							ImGui::TextColored(Theme::kAccent, "Installed");
							ImGui::SameLine();
							ImGui::TextDisabled("to installs/%s/minecraft", pack.id.c_str());
						}
						if (hasState && !state.blocked.empty()) {
							ImGui::Spacing();
							ImGui::TextColored(Theme::kError, "%llu file(s) must be downloaded by hand into installs/%s/minecraft:",
								static_cast<unsigned long long>(state.blocked.size()), pack.id.c_str());
							for (const PlannedFile& file : state.blocked) {
								ImGui::PushID(file.relPath.c_str());
								ImGui::BulletText("%s  ->  %s", file.name.c_str(), file.relPath.c_str());
								if (!file.manualPageUrl.empty()) {
									ImGui::SameLine();
									ImGui::TextLinkOpenURL("download page", file.manualPageUrl.c_str());
								}
								if (!file.blockedReason.empty())
									ImGui::TextDisabled("    %s", file.blockedReason.c_str());
								ImGui::PopID();
							}
						}
						for (const std::string& note : state.notes)
							ImGui::TextDisabled("Note: %s", note.c_str());
						EndCard();
					} else {
						char filesLabel[48];
						std::snprintf(filesLabel, sizeof(filesLabel), "PACK FILES  (%llu)", static_cast<unsigned long long>(pack.files.size()));
						Widgets::SectionLabel(filesLabel);
						if (loadingFiles) {
							Widgets::Spinner(Theme::kAccent);
							ImGui::SameLine();
							ImGui::TextDisabled("Loading latest version...");
						} else if (filesError != filesErrors.end()) {
							ImGui::TextColored(Theme::kError, "Couldn't load versions: %s", filesError->second.c_str());
							if (ImGui::Button("Retry"))
								startFetchFiles(view, pack);
						}

						for (const PackFile& file : pack.files) {
							DownloadState state;
							bool hasState = false;
							{
								std::lock_guard<std::mutex> lock(downloadsMutex);
								const auto it = downloads.find(file.url);
								if (it != downloads.end()) {
									state = it->second;
									hasState = true;
								}
							}

							ImGui::PushID(file.url.c_str());
							BeginCard("file_card");
							ImGui::AlignTextToFramePadding();
							ImGui::PushFont(fonts.bold, 0.0f);
							ImGui::TextUnformatted(file.name.c_str());
							ImGui::PopFont();
							ImGui::SameLine();
							ImGui::TextDisabled("%s", FormatFileSize(file.size).c_str());

							if (!hasState || (!state.active && state.outPath.empty())) {
								const char* label = hasState ? "Retry" : "Download";
								AlignRightForButton(label);
								if (ImGui::Button(label)) {
									DownloadState started;
									started.fileName = file.name;
									started.pack = pack;
									started.active = true;
									{
										std::lock_guard<std::mutex> lock(downloadsMutex);
										downloads[file.url] = started;
									}
									const std::string url = file.url;
									// Per-pack folder so same-named files from different packs don't collide.
									const fs::path packDownloadDir = fs::path("downloads") / pack.id;
									const std::string outputPath = (packDownloadDir / file.name).string();
									StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, &saveDownloads,
										url, packDownloadDir, outputPath]() {
										std::string error;
										std::error_code dirError;
										fs::create_directories(packDownloadDir, dirError);
										auto progress = [&](double total, double now) {
											std::lock_guard<std::mutex> lock(downloadsMutex);
											DownloadState& current = downloads[url];
											current.total = total;
											current.now = now;
										};
										const bool ok = http.DownloadToFile(url, outputPath, progress, &error, &shuttingDown);
										{
											std::lock_guard<std::mutex> lock(downloadsMutex);
											DownloadState& current = downloads[url];
											current.active = false;
											if (ok)
												current.outPath = outputPath;
											else
												current.error = error;
										}
										if (ok)
											uiQueue.Post([&saveDownloads]() { saveDownloads(); });
									});
								}
								if (hasState && !state.error.empty())
									ImGui::TextColored(Theme::kError, "Download failed: %s", state.error.c_str());
							} else if (state.active) {
								char progressLabel[64];
								if (state.total > 0.0)
									std::snprintf(progressLabel, sizeof(progressLabel), "Downloading  %.0f%%", 100.0 * state.now / state.total);
								else
									std::snprintf(progressLabel, sizeof(progressLabel), "Downloading");
								AlignRightForButton(progressLabel);
								ImGui::AlignTextToFramePadding();
								ImGui::TextDisabled("%s", progressLabel);
							} else if (state.installing) {
								AlignRightForButton("Extracting");
								ImGui::AlignTextToFramePadding();
								ImGui::TextDisabled("Extracting");
							} else if (state.installed) {
								AlignRightForButton("Extracted");
								ImGui::AlignTextToFramePadding();
								ImGui::TextColored(Theme::kAccent, "Extracted");
							} else if (!state.outPath.empty()) {
								AlignRightForButton("Install files");
								if (ImGui::Button("Install files"))
									startInstall(file.url, pack, state.outPath);
							}

							if (state.active && state.total > 0.0)
								Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
							else if (state.installing)
								Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
							if (!state.installError.empty())
								ImGui::TextColored(Theme::kError, "Extraction failed: %s", state.installError.c_str());
							EndCard();
							ImGui::PopID();
						}

					}

					ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
					ImGui::PushFont(fonts.bold, Theme::kFontHeading);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
					ImGui::BeginDisabled(true);
					ImGui::Button("PLAY  \xE2\x80\x94  COMING SOON", ImVec2(-1.0f, S(52.0f)));
					ImGui::EndDisabled();
					ImGui::PopStyleVar();
					ImGui::PopFont();
					ImGui::TextDisabled("Minecraft installation, account sign-in, and launching are not wired up yet.");
				} else if (sourceAvailable && !view.searching) {
					ImGui::TextDisabled("No modpacks match your search.");
				}
				ImGui::EndChild();
			} else if (page == Page::Library) {
				const float iconSize = S(64.0f);
				for (const auto& [packId, pack] : libraryPacks) {
					BeginCard(pack.id.c_str());
					PackIconItem(pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::kText, pack.name.c_str());
					if (GetSourceCapabilities(pack.source).installsPacks) {
						std::string details = std::string(PackSourceName(pack.source)) + "  \xC2\xB7  " + pack.version;
						if (!pack.gameVersion.empty())
							details += "  \xC2\xB7  Minecraft " + pack.gameVersion;
						if (!pack.loaders.empty())
							details += "  \xC2\xB7  " + pack.loaders;
						ImGui::TextDisabled("%s", details.c_str());
						ImGui::TextDisabled("Game folder: installs/%s/minecraft", pack.id.c_str());
					} else {
						ImGui::TextDisabled("%s  \xC2\xB7  %llu extracted file(s)",
							pack.version.c_str(), static_cast<unsigned long long>(pack.files.size()));
					}
					ImGui::TextDisabled("Files are prepared; this prototype does not launch Minecraft yet.");
					ImGui::EndGroup();
					EndCard();
				}
				if (libraryPacks.empty())
					ImGui::TextDisabled("No extracted modpacks yet. Browse packs and install their files to see them here.");
			} else {
				std::vector<std::pair<std::string, DownloadState>> snapshot;
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					snapshot.assign(downloads.begin(), downloads.end());
				}
				if (snapshot.empty()) {
					ImGui::TextDisabled("Nothing downloaded yet. Choose a pack and start a download.");
				}
				const float iconSize = S(56.0f);
				for (const auto& item : snapshot) {
					const DownloadState& state = item.second;
					BeginCard(item.first.c_str());
					PackIconItem(state.pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					Widgets::Text(fonts.bold, Theme::kFontBody, Theme::kText,
						state.pack.name.empty() ? state.fileName.c_str() : state.pack.name.c_str());
					if (!state.pack.name.empty())
						ImGui::TextDisabled("%s", state.fileName.c_str());
					if (state.planInstall) {
						if (state.active) {
							if (state.stageTotal > 0)
								ImGui::TextDisabled("%s...  %llu of %llu", state.stage.c_str(),
									static_cast<unsigned long long>(state.stageDone), static_cast<unsigned long long>(state.stageTotal));
							else
								ImGui::TextDisabled("%s...", state.stage.c_str());
						} else if (!state.error.empty()) {
							ImGui::TextColored(Theme::kError, "Install failed: %s", state.error.c_str());
						} else if (!state.blocked.empty()) {
							ImGui::TextColored(Theme::kError, "Installed - %llu file(s) need a manual download (see Discover)",
								static_cast<unsigned long long>(state.blocked.size()));
						} else {
							ImGui::TextColored(Theme::kAccent, "Installed");
						}
					} else if (state.active) {
						if (state.total > 0.0)
							ImGui::TextDisabled("Downloading...  %s of %s", FormatFileSize(static_cast<std::uint64_t>(state.now)).c_str(),
								FormatFileSize(static_cast<std::uint64_t>(state.total)).c_str());
						else
							ImGui::TextDisabled("Downloading...");
					} else if (!state.error.empty()) {
						ImGui::TextColored(Theme::kError, "Download failed: %s", state.error.c_str());
					} else if (state.installing) {
						ImGui::TextDisabled("Extracting files...");
					} else if (!state.installError.empty()) {
						ImGui::TextColored(Theme::kError, "Extraction failed: %s", state.installError.c_str());
					} else if (state.installed) {
						ImGui::TextColored(Theme::kAccent, "Files extracted");
					} else {
						ImGui::TextDisabled("Downloaded - ready to extract.");
					}
					ImGui::EndGroup();
					if (!state.active && !state.outPath.empty() && !state.installing && !state.installed) {
						AlignRightForButton("Install files");
						if (ImGui::Button("Install files"))
							startInstall(item.first, state.pack, state.outPath);
					}
					if (state.planInstall && state.active)
						Widgets::ThinProgressBar(state.stageTotal > 0
							? static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)) : 0.0f);
					else if (state.active && state.total > 0.0)
						Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
					else if (state.installing)
						Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
					EndCard();
				}
			}

			ImGui::EndChild();
			ImGui::End();

			ImGui::Render();
			int display_w = 0;
			int display_h = 0;
			glfwGetFramebufferSize(window, &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			glClearColor(0.055f, 0.070f, 0.085f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
		}

		// Ask in-flight downloads/extractions to stop, then wait for them.
		shuttingDown.store(true);
		for (Worker& worker : workers) {
			if (worker.thread.joinable())
				worker.thread.join();
		}
		uiQueue.Drain(); // saves downloads/extractions that finished during shutdown

		images.Shutdown(); // frees textures while the GL context still exists
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		glfwDestroyWindow(window);
		glfwTerminate();
		log << "Exiting normally\n";
		return 0;
	}
	catch (const std::exception& ex) {
		log << "Unhandled exception: " << ex.what() << "\n";
		log.close();
		throw;
	}
	catch (...) {
		log << "Unhandled unknown exception\n";
		log.close();
		throw;
	}
}
