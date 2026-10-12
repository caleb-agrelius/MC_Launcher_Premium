#include "App/GlfwWindow.h"

#include "Platform/Platform.h"
#include "UI/Logo.h"
#include "imgui.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

// True when a saved window's title bar would be on a connected monitor, so a window
// saved on a monitor that's been unplugged doesn't open off screen.
bool FitsOnAMonitor(const WindowPlacement& placement)
{
	int count = 0;
	GLFWmonitor** monitors = glfwGetMonitors(&count);
	for (int i = 0; i < count; ++i) {
		int x = 0, y = 0, width = 0, height = 0;
		glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
		const int titleX = placement.x + std::min(placement.width / 2, 100);
		const int titleY = placement.y + 10;
		if (titleX >= x && titleX < x + width && titleY >= y && titleY < y + height &&
			placement.width <= width + 40 && placement.height <= height + 40)
			return true;
	}
	return false;
}

// The logo at the sizes the title bar and taskbar ask for.
void SetIcon(GLFWwindow* window)
{
	const int sizes[] = { 16, 32, 48, 64 };
	std::vector<std::vector<unsigned char>> pixels;
	std::vector<GLFWimage> icons;
	for (int size : sizes)
		pixels.push_back(Logo::BlockPixels(size));
	for (std::size_t i = 0; i < pixels.size(); ++i)
		icons.push_back({ sizes[i], sizes[i], pixels[i].data() });
	glfwSetWindowIcon(window, static_cast<int>(icons.size()), icons.data());
}

}

void OnDrop(GLFWwindow* window, int count, const char** paths)
{
	auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
	if (!self)
		return;
	for (int i = 0; i < count; ++i)
		self->m_dropped.push_back(fs::u8path(paths[i]));
}

std::unique_ptr<GlfwWindow> GlfwWindow::Create(const WindowPlacement& saved, std::ostream& log)
{
	if (!glfwInit()) {
		log << "glfwInit failed\n";
		return nullptr;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	// The last window size is restored as-is (it's in real pixels). The first time, the
	// default size is in logical pixels, so it isn't tiny on high-DPI (e.g. 4K at 150%) monitors.
	const bool restore = saved.saved && FitsOnAMonitor(saved);
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, restore ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // shown once it's in place

	std::unique_ptr<GlfwWindow> self(new GlfwWindow());
	self->m_window = restore ? glfwCreateWindow(saved.width, saved.height, "MC Launcher Premium", nullptr, nullptr)
		: glfwCreateWindow(1360, 840, "MC Launcher Premium", nullptr, nullptr);
	if (!self->m_window) {
		log << "glfwCreateWindow failed\n";
		glfwTerminate();
		return nullptr;
	}
	GLFWwindow* window = self->m_window;
	SetIcon(window);
	if (restore)
		glfwSetWindowPos(window, saved.x, saved.y);
	glfwGetWindowPos(window, &self->m_placement.x, &self->m_placement.y);
	glfwGetWindowSize(window, &self->m_placement.width, &self->m_placement.height);
	if (restore && saved.maximized)
		glfwMaximizeWindow(window);
	glfwShowWindow(window);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	if (!gladLoadGL()) {
		std::fprintf(stderr, "Failed to initialize GL loader\n");
		log << "gladLoadGL failed\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		self->m_window = nullptr;
		return nullptr;
	}
	glfwSetWindowUserPointer(window, self.get());
	return self;
}

GlfwWindow::~GlfwWindow()
{
	if (!m_window)
		return;
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

bool GlfwWindow::InitImGuiBackends(std::ostream& log)
{
	if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true)) {
		log << "ImGui GLFW backend initialization failed\n";
		return false;
	}
	glfwSetDropCallback(m_window, OnDrop);
	if (!ImGui_ImplOpenGL3_Init("#version 130")) {
		log << "ImGui OpenGL backend initialization failed\n";
		ImGui_ImplGlfw_Shutdown();
		return false;
	}
	m_backendsStarted = true;
	return true;
}

void GlfwWindow::ShutdownImGuiBackends()
{
	if (!m_backendsStarted)
		return;
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	m_backendsStarted = false;
}

bool GlfwWindow::ShouldClose() const
{
	return glfwWindowShouldClose(m_window) != 0;
}

void GlfwWindow::PollEvents()
{
	glfwPollEvents();
}

std::vector<fs::path> GlfwWindow::TakeDroppedPaths()
{
	std::vector<fs::path> taken;
	taken.swap(m_dropped);
	return taken;
}

float GlfwWindow::ContentScale() const
{
	float scale = 1.0f;
	glfwGetWindowContentScale(m_window, &scale, nullptr);
	return scale;
}

void GlfwWindow::SetMinimumSize(int width, int height)
{
	glfwSetWindowSizeLimits(m_window, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

void GlfwWindow::ApplyTitleBar(bool dark, const ImVec4& background, const ImVec4& text)
{
	auto toRgb = [](const ImVec4& color) {
		return Platform::Rgb{ static_cast<unsigned char>(color.x * 255.0f + 0.5f), static_cast<unsigned char>(color.y * 255.0f + 0.5f),
			static_cast<unsigned char>(color.z * 255.0f + 0.5f) };
	};
	Platform::TitleBarStyle style;
	style.dark = dark;
	style.caption = toRgb(background);
	style.text = toRgb(text);
	Platform::ApplyTitleBarStyle(m_window, style);
}

void GlfwWindow::TrackPlacement()
{
	const bool iconified = glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0;
	const bool maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
	if (!iconified)
		m_placement.maximized = maximized;
	if (!maximized && !iconified) {
		glfwGetWindowPos(m_window, &m_placement.x, &m_placement.y);
		glfwGetWindowSize(m_window, &m_placement.width, &m_placement.height);
	}
	m_placement.saved = m_placement.width >= 200 && m_placement.height >= 150;
}

void GlfwWindow::BeginFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void GlfwWindow::EndFrame(const ImVec4& clearColor)
{
	ImGui::Render();
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(m_window, &width, &height);
	glViewport(0, 0, width, height);
	glClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(m_window);
}

void GlfwWindow::Minimize()
{
	glfwIconifyWindow(m_window);
}

void GlfwWindow::Restore()
{
	glfwRestoreWindow(m_window);
}

void GlfwWindow::RequestClose()
{
	glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}
