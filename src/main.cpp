#include "imgui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Model/Modpack.h"
#include "Network/HttpClient.h"
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

		std::vector<Modpack> packs = LoadIndex(kIndexRelativePath.string());
		log << "Loaded " << packs.size() << " modpack(s)\n";
		log.flush();
		int selected = packs.empty() ? -1 : 0;
		Page page = Page::Discover;
		char search[128] = "";
		HttpClient http;
		std::map<std::string, DownloadState> downloads;
		std::mutex downloadsMutex;
		std::vector<Worker> workers;
		std::atomic<bool> shuttingDown{ false };
		fs::create_directories("downloads");
		fs::create_directories("installs");

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			ReapFinishedWorkers(workers);
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
				ImGui::InputTextWithHint("##search", "Search modpacks...", search, sizeof(search));
				ImGui::Spacing();

				ImGui::BeginChild("pack_list", ImVec2(285.0f, 0.0f), true);
				ImGui::TextDisabled("AVAILABLE PACKS");
				ImGui::Spacing();
				const std::string query = ToLower(search);
				for (std::size_t i = 0; i < packs.size(); ++i) {
					if (!query.empty() && !ContainsIgnoreCase(packs[i].name, query) &&
						!ContainsIgnoreCase(packs[i].description, query))
						continue;

					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(packs[i].name.c_str(), selected == static_cast<int>(i),
						ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 54.0f)))
						selected = static_cast<int>(i);
					ImGui::TextDisabled("  %s  |  %llu files", packs[i].version.c_str(),
						static_cast<unsigned long long>(packs[i].files.size()));
					ImGui::Spacing();
					ImGui::PopID();
				}
				ImGui::EndChild();
				ImGui::SameLine();

				ImGui::BeginChild("pack_details", ImVec2(0.0f, 0.0f), true);
				if (selected >= 0 && selected < static_cast<int>(packs.size())) {
					const Modpack& pack = packs[static_cast<std::size_t>(selected)];
					ImGui::TextColored(ImVec4(0.40f, 0.83f, 0.50f, 1.0f), "MODPACK");
					ImGui::SameLine();
					ImGui::TextDisabled("  %s", pack.version.c_str());
					ImGui::Spacing();
					ImGui::Text("%s", pack.name.c_str());
					ImGui::Separator();
					ImGui::TextWrapped("%s", pack.description.c_str());
					ImGui::Spacing();

					ImGui::BeginChild("play_banner", ImVec2(0.0f, 80.0f), true);
					ImGui::Text("Ready your next adventure");
					ImGui::TextDisabled("Download and extract a pack to prepare its files.");
					ImGui::EndChild();
					ImGui::Spacing();
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
				for (const Modpack& pack : packs) {
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
