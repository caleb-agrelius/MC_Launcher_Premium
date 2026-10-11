#include "imgui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Model/Modpack.h"
#include "Network/HttpClient.h"
#include "Network/Modrinth.h"
#include "Storage/Installer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdint>
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
#endif

namespace fs = std::filesystem;

static const fs::path kIndexRelativePath = fs::path("resources") / "index.json";
static constexpr double kSearchDelaySeconds = 0.4; // pause after typing before searching
static constexpr int kSearchLimit = 30;            // results per Modrinth search

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
	double total = 0.0;
	double now = 0.0;
	bool active = false;
	std::string error;
	std::string outPath;
	bool installed = false;
	bool installing = false;
	double installProgress = 0.0;
	std::string installError;
};

enum class Page {
	Discover,
	Library,
	Downloads
};

static void ApplyLauncherStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 8.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 5.0f;
	style.PopupRounding = 5.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 5.0f;
	style.WindowPadding = ImVec2(18.0f, 16.0f);
	style.FramePadding = ImVec2(12.0f, 9.0f);
	style.ItemSpacing = ImVec2(10.0f, 10.0f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.070f, 0.085f, 1.0f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.090f, 0.105f, 1.0f);
	colors[ImGuiCol_Border] = ImVec4(0.15f, 0.19f, 0.21f, 1.0f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.13f, 0.14f, 1.0f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.20f, 0.17f, 1.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.25f, 0.19f, 1.0f);
	colors[ImGuiCol_Button] = ImVec4(0.16f, 0.34f, 0.22f, 1.0f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.43f, 0.28f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.13f, 0.29f, 0.19f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.13f, 0.25f, 0.18f, 1.0f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.33f, 0.22f, 1.0f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.38f, 0.25f, 1.0f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.38f, 0.80f, 0.49f, 1.0f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.68f, 0.40f, 1.0f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.82f, 0.50f, 1.0f);
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

static bool IsPackInstalled(const Modpack& pack, const std::map<std::string, DownloadState>& downloads)
{
	for (const PackFile& file : pack.files) {
		const auto state = downloads.find(file.url);
		if (state != downloads.end() && state->second.installed)
			return true;
	}
	return false;
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

		GLFWwindow* window = glfwCreateWindow(1280, 760, "MC_Launcher_Premium", nullptr, nullptr);
		if (window == nullptr) {
			log << "glfwCreateWindow failed\n";
			glfwTerminate();
			return -1;
		}
		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);

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
		ApplyLauncherStyle();

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
		std::vector<Modpack> packs;
		int selected = -1;
		Page page = Page::Discover;
		char search[128] = "";
		HttpClient http;
		std::map<std::string, DownloadState> downloads;
		std::mutex downloadsMutex;
		std::vector<Worker> workers;
		std::atomic<bool> shuttingDown{ false };
		UiQueue uiQueue;
		fs::create_directories("downloads");
		fs::create_directories("installs");

		// Search state (UI thread only). Typing schedules a search; it runs once
		// the query has been unchanged for kSearchDelaySeconds.
		bool searchPending = true; // run the initial "most downloaded" search on the first frame
		double lastSearchEdit = -1.0;
		bool searching = false;
		int searchGeneration = 0; // results from older searches are ignored
		std::string searchStatus;
		bool searchFailed = false;
		// Per-pack version lookups for Modrinth results, keyed by pack id.
		std::set<std::string> filesLoading;
		std::map<std::string, std::string> filesErrors;
		// Packs the user has started installing, so My Library survives new searches.
		std::map<std::string, Modpack> libraryPacks;

		auto startSearch = [&]() {
			const int generation = ++searchGeneration;
			const std::string query = search;
			searching = true;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packs, &selected, &searching,
				&searchStatus, &searchFailed, &searchGeneration, &localPacks, &log, query, generation]() {
				ModrinthSearchResult result;
				std::string error;
				const bool ok = SearchModrinthModpacks(http, query, kSearchLimit, result, &error, &shuttingDown);
				uiQueue.Post([&packs, &selected, &searching, &searchStatus, &searchFailed, &searchGeneration,
					&localPacks, &log, query, generation, ok, result, error]() {
					if (generation != searchGeneration)
						return;
					searching = false;
					searchFailed = !ok;
					if (ok) {
						packs = result.packs;
						searchStatus = FormatCount(result.totalHits) + " modpacks on Modrinth";
						log << "Search \"" << query << "\": " << result.packs.size() << " of "
							<< result.totalHits << " result(s)\n";
					} else {
						log << "Search \"" << query << "\" failed: " << error << "\n";
						// Offline or API error: fall back to the local index.
						const std::string lowerQuery = ToLower(query);
						packs.clear();
						for (const Modpack& pack : localPacks) {
							if (lowerQuery.empty() || ContainsIgnoreCase(pack.name, lowerQuery) ||
								ContainsIgnoreCase(pack.description, lowerQuery))
								packs.push_back(pack);
						}
						searchStatus = "Modrinth unavailable (" + error + ") - showing local packs";
					}
					selected = packs.empty() ? -1 : 0;
				});
			});
		};

		auto startFetchFiles = [&](const std::string& packId) {
			filesLoading.insert(packId);
			filesErrors.erase(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packs, &filesLoading, &filesErrors, &log, packId]() {
				ModrinthVersion version;
				std::string error;
				const bool ok = FetchLatestModrinthVersion(http, packId, version, &error, &shuttingDown);
				uiQueue.Post([&packs, &filesLoading, &filesErrors, &log, packId, ok, version, error]() {
					filesLoading.erase(packId);
					if (!ok) {
						log << "Version lookup for " << packId << " failed: " << error << "\n";
						filesErrors[packId] = error;
						return;
					}
					log << "Version lookup for " << packId << ": " << version.versionNumber << " ("
						<< (version.files.empty() ? std::string("no file") : version.files.front().name) << ")\n";
					for (Modpack& pack : packs) {
						if (pack.id != packId)
							continue;
						pack.version = version.versionNumber;
						if (!version.gameVersion.empty())
							pack.gameVersion = version.gameVersion;
						pack.loaders = version.loaders;
						pack.files = version.files;
						pack.filesLoaded = true;
					}
				});
			});
		};

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			ReapFinishedWorkers(workers);
			uiQueue.Drain();
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("MC_Launcher_Premium", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

			ImGui::BeginChild("sidebar", ImVec2(205.0f, 0.0f), true);
			ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "MC_Launcher_Premium");
			ImGui::TextDisabled("MINECRAFT JAVA");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Selectable("Discover", page == Page::Discover, 0, ImVec2(0.0f, 42.0f)))
				page = Page::Discover;
			if (ImGui::Selectable("My Library", page == Page::Library, 0, ImVec2(0.0f, 42.0f)))
				page = Page::Library;
			if (ImGui::Selectable("Downloads", page == Page::Downloads, 0, ImVec2(0.0f, 42.0f)))
				page = Page::Downloads;

			ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 92.0f);
			ImGui::Separator();
			ImGui::TextDisabled("Minecraft Java Edition");
			ImGui::TextDisabled("Launcher integration");
			ImGui::TextDisabled("coming soon");
			ImGui::EndChild();

			ImGui::SameLine();
			ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), false);
			ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "MINECRAFT  /  MODPACKS");
			if (page == Page::Discover) {
				ImGui::Text("Discover modpacks");
				ImGui::TextDisabled("Find a pack, download its files, and prepare it for Minecraft.");
			} else if (page == Page::Library) {
				ImGui::Text("My library");
				ImGui::TextDisabled("Modpack files you have extracted are listed here.");
			} else {
				ImGui::Text("Downloads");
				ImGui::TextDisabled("Track downloads and extraction progress.");
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (page == Page::Discover) {
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputTextWithHint("##search", "Search Modrinth modpacks...", search, sizeof(search))) {
					searchPending = true;
					lastSearchEdit = ImGui::GetTime();
				}
				if (searchPending && ImGui::GetTime() - lastSearchEdit >= kSearchDelaySeconds) {
					searchPending = false;
					startSearch();
				}
				if (searching)
					ImGui::TextDisabled("Searching Modrinth...");
				else if (searchFailed)
					ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "%s", searchStatus.c_str());
				else
					ImGui::TextDisabled("%s", searchStatus.c_str());
				ImGui::Spacing();

				ImGui::BeginChild("pack_list", ImVec2(285.0f, 0.0f), true);
				ImGui::TextDisabled(search[0] == '\0' ? "MOST DOWNLOADED" : "RESULTS");
				ImGui::Spacing();
				for (std::size_t i = 0; i < packs.size(); ++i) {
					const Modpack& item = packs[i];
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(item.name.c_str(), selected == static_cast<int>(i),
						ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 54.0f)))
						selected = static_cast<int>(i);
					if (item.fromModrinth)
						ImGui::TextDisabled("  %s  |  %s downloads", item.gameVersion.c_str(),
							FormatCount(item.downloads).c_str());
					else
						ImGui::TextDisabled("  %s  |  %llu files", item.version.c_str(),
							static_cast<unsigned long long>(item.files.size()));
					ImGui::Spacing();
					ImGui::PopID();
				}
				if (packs.empty() && !searching)
					ImGui::TextDisabled("No modpacks found.");
				ImGui::EndChild();
				ImGui::SameLine();

				ImGui::BeginChild("pack_details", ImVec2(0.0f, 0.0f), true);
				if (selected >= 0 && selected < static_cast<int>(packs.size())) {
					const Modpack& pack = packs[static_cast<std::size_t>(selected)];
					const bool loadingFiles = filesLoading.count(pack.id) > 0;
					const auto filesError = filesErrors.find(pack.id);
					if (pack.fromModrinth && !pack.filesLoaded && !loadingFiles && filesError == filesErrors.end())
						startFetchFiles(pack.id);

					ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "MODPACK");
					ImGui::SameLine();
					ImGui::TextDisabled("  %s", pack.version.c_str());
					ImGui::Spacing();
					ImGui::Text("%s", pack.name.c_str());
					if (pack.fromModrinth) {
						std::string meta = "by " + pack.author + "  |  " + FormatCount(pack.downloads) + " downloads";
						if (!pack.gameVersion.empty())
							meta += "  |  Minecraft " + pack.gameVersion;
						if (!pack.loaders.empty())
							meta += "  |  " + pack.loaders;
						ImGui::TextDisabled("%s", meta.c_str());
						ImGui::TextDisabled("modrinth.com/modpack/%s", pack.id.c_str());
					}
					ImGui::Separator();
					ImGui::TextWrapped("%s", pack.description.c_str());
					ImGui::Spacing();

					ImGui::BeginChild("play_banner", ImVec2(0.0f, 80.0f), true);
					ImGui::Text("Ready your next adventure");
					ImGui::TextDisabled("Download and extract a pack to prepare its files.");
					ImGui::EndChild();
					ImGui::Spacing();
					if (loadingFiles) {
						ImGui::TextDisabled("Loading latest version...");
					} else if (filesError != filesErrors.end()) {
						ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Couldn't load versions: %s",
							filesError->second.c_str());
						if (ImGui::Button("Retry"))
							startFetchFiles(pack.id);
					}
					ImGui::Text("PACK FILES  (%llu)", static_cast<unsigned long long>(pack.files.size()));
					ImGui::Separator();

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
						ImGui::Text("%s", file.name.c_str());
						ImGui::SameLine();
						ImGui::TextDisabled("%s", FormatFileSize(file.size).c_str());
						ImGui::SameLine();

						if (!hasState || (!state.active && state.outPath.empty())) {
							if (ImGui::Button(hasState ? "Retry" : "Download")) {
								DownloadState started;
								started.fileName = file.name;
								started.active = true;
								{
									std::lock_guard<std::mutex> lock(downloadsMutex);
									downloads[file.url] = started;
								}
								const std::string url = file.url;
								// Per-pack folder so same-named files from different packs don't collide.
								const fs::path packDownloadDir = fs::path("downloads") / pack.id;
								const std::string outputPath = (packDownloadDir / file.name).string();
								StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, url, packDownloadDir, outputPath]() {
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
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[url];
									current.active = false;
									if (ok)
										current.outPath = outputPath;
									else
										current.error = error;
								});
							}
							if (hasState && !state.error.empty()) {
								ImGui::SameLine();
								ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Download failed: %s", state.error.c_str());
							}
						} else if (state.active) {
							ImGui::TextDisabled("Downloading");
						} else if (state.installing) {
							ImGui::TextDisabled("Extracting");
						} else if (state.installed) {
							ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "Extracted");
						} else if (!state.outPath.empty()) {
							if (ImGui::Button("Install files")) {
								libraryPacks[pack.id] = pack;
								{
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[file.url];
									current.installing = true;
									current.installProgress = 0.0;
									current.installError.clear();
								}
								const std::string zipPath = state.outPath;
								const std::string targetDir = (fs::path("installs") / pack.id).string();
								const std::string url = file.url;
								StartWorker(workers, [zipPath, targetDir, &downloads, &downloadsMutex, &shuttingDown, url]() {
									std::string error;
									auto progress = [&](int total, int now) {
										std::lock_guard<std::mutex> lock(downloadsMutex);
										DownloadState& current = downloads[url];
										if (total > 0)
											current.installProgress = static_cast<double>(now) / static_cast<double>(total);
									};
									const bool ok = InstallZip(zipPath, targetDir, progress, &error, &shuttingDown);
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[url];
									current.installing = false;
									if (ok)
										current.installed = true;
									else
										current.installError = error;
								});
							}
						}

						if (state.active && state.total > 0.0) {
							const float fraction = static_cast<float>(state.now / state.total);
							ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
						} else if (state.installing) {
							ImGui::ProgressBar(static_cast<float>(state.installProgress), ImVec2(-1.0f, 0.0f));
						}
						if (!state.installError.empty())
							ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Extraction failed: %s", state.installError.c_str());
						ImGui::Separator();
						ImGui::PopID();
					}

					ImGui::Spacing();
					ImGui::BeginDisabled(true);
					ImGui::Button("PLAY  -  COMING SOON", ImVec2(-1.0f, 42.0f));
					ImGui::EndDisabled();
					ImGui::TextDisabled("Minecraft installation, account sign-in, and launching are not wired up yet.");
				} else {
					ImGui::Text("No modpacks match your search.");
				}
				ImGui::EndChild();
			} else if (page == Page::Library) {
				std::lock_guard<std::mutex> lock(downloadsMutex);
				bool anyInstalled = false;
				for (const auto& [packId, pack] : libraryPacks) {
					if (!IsPackInstalled(pack, downloads))
						continue;
					anyInstalled = true;
					ImGui::BeginChild(pack.id.c_str(), ImVec2(0.0f, 100.0f), true);
					ImGui::Text("%s", pack.name.c_str());
					ImGui::TextDisabled("%s  |  %llu extracted file(s)",
						pack.version.c_str(), static_cast<unsigned long long>(pack.files.size()));
					ImGui::TextDisabled("Files are prepared; this prototype does not launch Minecraft yet.");
					ImGui::EndChild();
					ImGui::Spacing();
				}
				if (!anyInstalled)
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
				for (const auto& item : snapshot) {
					ImGui::BeginChild(item.first.c_str(), ImVec2(0.0f, 105.0f), true);
					ImGui::Text("%s", item.second.fileName.c_str());
					if (item.second.active) {
						ImGui::TextDisabled("Downloading...");
						if (item.second.total > 0.0)
							ImGui::ProgressBar(static_cast<float>(item.second.now / item.second.total), ImVec2(-1.0f, 0.0f));
					} else if (!item.second.error.empty()) {
						ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Download failed: %s", item.second.error.c_str());
					} else if (item.second.installing) {
						ImGui::TextDisabled("Extracting files...");
						ImGui::ProgressBar(static_cast<float>(item.second.installProgress), ImVec2(-1.0f, 0.0f));
					} else if (!item.second.installError.empty()) {
						ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Extraction failed: %s", item.second.installError.c_str());
					} else if (item.second.installed) {
						ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "Files extracted");
					} else {
						ImGui::TextDisabled("Downloaded - open Discover to extract this pack.");
					}
					ImGui::EndChild();
					ImGui::Spacing();
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
