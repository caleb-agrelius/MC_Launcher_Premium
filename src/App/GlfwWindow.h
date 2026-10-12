#pragma once

#include "Core/WindowControl.h"
#include "Storage/Settings.h"

#include <filesystem>
#include <memory>
#include <ostream>
#include <vector>

struct GLFWwindow;
struct ImVec4;

// The launcher's window: GLFW, its OpenGL context and the Dear ImGui backends. Everything the
// rest of the app needs from it is here (frames, dropped files, size, placement); nothing else
// includes the GLFW or OpenGL headers except the OS-specific Platform code.
class GlfwWindow final : public IWindowControl {
public:
	// Creates the window hidden, places it where saved says (if that spot is still on a monitor),
	// then shows it with a GL context current. Null, after logging why, if anything fails.
	static std::unique_ptr<GlfwWindow> Create(const WindowPlacement& saved, std::ostream& log);
	~GlfwWindow();
	GlfwWindow(const GlfwWindow&) = delete;
	GlfwWindow& operator=(const GlfwWindow&) = delete;

	// Starts the Dear ImGui GLFW and OpenGL backends. Call after the ImGui context and fonts exist.
	bool InitImGuiBackends(std::ostream& log);
	// Frees ImGui's GL resources, so call it while the context is still alive.
	void ShutdownImGuiBackends();

	bool ShouldClose() const;
	void PollEvents();
	// Files dropped on the window since the last call.
	std::vector<std::filesystem::path> TakeDroppedPaths();

	// The monitor's content scale (1.0 = 96 dpi).
	float ContentScale() const;
	void SetMinimumSize(int width, int height);
	// Colors the native title bar to match the theme.
	void ApplyTitleBar(bool dark, const ImVec4& background, const ImVec4& text);

	// Remembers where the window is, unless it is maximized or minimized.
	void TrackPlacement();
	WindowPlacement Placement() const { return m_placement; }

	void BeginFrame();
	// Renders ImGui's draw data over clearColor and shows it.
	void EndFrame(const ImVec4& clearColor);

	void Minimize() override;
	void Restore() override;
	void RequestClose() override;

private:
	GlfwWindow() = default;

	GLFWwindow* m_window = nullptr;
	WindowPlacement m_placement;
	bool m_backendsStarted = false;
	std::vector<std::filesystem::path> m_dropped;

	friend void OnDrop(GLFWwindow*, int, const char**);
};
