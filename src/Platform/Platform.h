#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

// Everything the launcher needs from the operating system that isn't portable C++.
// The declarations are shared; each OS has its own definitions (Platform_win32.cpp,
// Platform_posix.cpp) and CMake builds the one that matches. Code outside this folder
// calls these instead of using OS headers or #ifdef, so porting means adding a file here.
namespace Platform {

// Opens a folder in the system file browser (Explorer on Windows). Creates it first if
// it doesn't exist. Returns false if it couldn't be opened.
bool OpenFolder(const std::string& path);

// Opens a file with its default program (e.g. a .log in Notepad). Returns false if the
// file doesn't exist or couldn't be opened.
bool OpenFile(const std::string& path);

// Opens a web address in the default browser. Returns false if it couldn't be opened.
bool OpenUrl(const std::string& url);

// Shows the system's Open dialog for choosing one or more files, e.g. title "Add mods",
// filterName "Mods", patterns "*.jar" (several separated by ';'). Returns UTF-8 paths, or
// nothing when cancelled or unsupported. Blocks until the dialog closes.
std::vector<std::string> PickFiles(const std::string& title, const std::string& filterName, const std::string& patterns);

// Fills a buffer with cryptographically secure random bytes (OAuth state and PKCE verifiers).
// Returns false if the system couldn't provide them.
bool RandomBytes(unsigned char* out, std::size_t count);

// Encrypts text so only the signed-in OS user can read it back, for the saved sign-in tokens
// (DPAPI on Windows). Where the OS has no per-user store, the text comes back unchanged and
// the file is protected with RestrictToOwner instead.
bool ProtectSecret(const std::string& plain, std::string& protectedText);
bool UnprotectSecret(const std::string& protectedText, std::string& plain);

// Limits a file to its owner (mode 0600). Does nothing on Windows, where DPAPI protects the
// contents and the user's profile folder limits who can open the file.
void RestrictToOwner(const std::filesystem::path& file);

// A TCP listener on 127.0.0.1 and a free port, for the redirect at the end of a browser
// sign-in: the browser is sent back to http://localhost:<port>/?code=... and the launcher
// reads that one request.
class LoopbackListener {
public:
	LoopbackListener();
	~LoopbackListener();
	LoopbackListener(const LoopbackListener&) = delete;
	LoopbackListener& operator=(const LoopbackListener&) = delete;

	// Starts listening and reports the port. Returns false with err set if it couldn't.
	bool Start(unsigned short& port, std::string* err);

	enum class Result { Request, Timeout, Failed };
	// Waits up to timeoutMs for a browser to connect and send its request. On Request, head is
	// everything it sent up to the blank line after the headers (empty if it sent nothing), and
	// Reply must follow. Timeout means nothing connected yet; Failed means the listener broke.
	Result WaitForRequest(int timeoutMs, std::string& head);

	// Answers the request WaitForRequest returned with an HTML page, then closes the connection.
	void Reply(const std::string& html);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

// Total physical memory in MB, or 0 if unknown.
std::uint64_t SystemMemoryMb();

// This PC's IPv4 address on the local network (e.g. "192.168.1.20"), or empty if unknown.
// Used to tell friends on the same network where to join a server.
std::string LocalNetworkAddress();

// One IPv4 address assigned to this PC, and the network adapter it belongs to.
struct NetworkAddress {
	std::string address;  // e.g. "192.168.1.20"
	std::string adapter;  // e.g. "Wi-Fi", "Tailscale"
};

// Every IPv4 address on adapters that are up, loopback excluded, in adapter order.
// Lets the player pick which address a server binds to (Servers > Host IP).
std::vector<NetworkAddress> LocalNetworkAddresses();

// Asks the OS for 1 ms timer resolution so that sleeping between frames (a few ms at a time)
// is accurate; Windows otherwise rounds a sleep up to its 15.6 ms tick. Call once at startup.
void UsePreciseTimer();

// Blocks until the desktop compositor shows its next frame, sleeping in the kernel. With the graphics
// driver's own vsync wait, a thread spins a CPU core for the whole wait (a full core at 240 fps on one
// test PC); this wait cost about a tenth of that. Returns false if it can't wait on this system.
bool WaitForDisplayRefresh();

// How fast the desktop compositor refreshes, in Hz, or 0 if unknown. The compositor can run at a
// different rate from the monitor a window is on (mixed-refresh multi-monitor setups).
double CompositorRefreshRate();

// The app has no console of its own on Windows. When it was started from a terminal (mcdebug debug, a shell),
// joins that terminal so the debug harness can print there and points stdout/stderr at it unless they were
// already redirected. Does nothing when started from Explorer or on other systems. Call first in main().
void AttachParentConsole();

// The folder holding the running executable, or empty if the OS can't say.
std::filesystem::path ExecutableDir();

// The per-user folder for the launcher's data, used when the folder holding resources/ can't be written
// to (an AppImage, a system-wide install): $XDG_DATA_HOME/mc-launcher-premium, else
// ~/.local/share/mc-launcher-premium. Empty where the launcher keeps its data beside the executable
// (Windows) or the OS has no such folder. The folder is not created.
std::filesystem::path UserDataDir();

// RGB, 0-255.
struct Rgb {
	unsigned char r = 0;
	unsigned char g = 0;
	unsigned char b = 0;
};

// How the native title bar should look so it matches the launcher's theme.
struct TitleBarStyle {
	bool dark = true;  // dark or light window chrome
	Rgb caption;       // title bar background
	Rgb text;          // title text
};

// Colors the window's native title bar. Platforms or versions that can't (older Windows
// ignores the colors, other systems the whole call) leave it as it is.
void ApplyTitleBarStyle(GLFWwindow* window, const TitleBarStyle& style);

}
