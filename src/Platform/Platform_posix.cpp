// Linux and other POSIX definitions of the Platform functions (see Platform.h).
// The title bar is left to the desktop, and the file picker uses whichever of zenity and
// kdialog is installed (nothing when neither is: drag and drop and Open folder still work).
#include "Platform/Platform.h"

#include "Util/PosixFd.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

extern char** environ;

namespace Platform {

namespace {

// Runs a program without a shell and without waiting for it. It keeps running on its own
// (a browser or file manager may outlive this call); a helper thread reaps it when it ends.
bool SpawnDetached(const std::vector<std::string>& args)
{
	std::vector<char*> argv;
	for (const std::string& arg : args)
		argv.push_back(const_cast<char*>(arg.c_str()));
	argv.push_back(nullptr);

	// The standard streams go to /dev/null so a chatty opener can't write over the launcher's console.
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	pid_t pid = -1;
	const int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	if (error != 0)
		return false;
	std::thread([pid]() {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
	}).detach();
	return true;
}

// Hands path to the desktop's opener (xdg-open).
bool OpenWithDesktop(const std::string& target)
{
	return SpawnDetached({ "xdg-open", target });
}

// target quoted for a POSIX shell: 'it'\''s'.
std::string ShellQuote(const std::string& text)
{
	std::string quoted = "'";
	for (char c : text) {
		if (c == '\'')
			quoted += "'\\''";
		else
			quoted += c;
	}
	return quoted + "'";
}

// Runs a shell command and collects what it prints. Returns the command's exit code, or -1.
int RunShellCapture(const std::string& command, std::string& output)
{
	output.clear();
	FILE* pipe = popen(command.c_str(), "r");
	if (!pipe)
		return -1;
	char buffer[4096];
	std::size_t count;
	while ((count = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
		output.append(buffer, count);
	const int status = pclose(pipe);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::vector<std::string> SplitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		if (end > start)
			lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

// "*.jar;*.zip" -> "*.jar *.zip"
std::string SpaceSeparatedPatterns(std::string patterns)
{
	for (char& c : patterns) {
		if (c == ';')
			c = ' ';
	}
	return patterns;
}

}

bool OpenFolder(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	return OpenWithDesktop(std::filesystem::absolute(path, ec).string());
}

bool OpenFile(const std::string& path)
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec))
		return false;
	return OpenWithDesktop(std::filesystem::absolute(path, ec).string());
}

bool OpenUrl(const std::string& url)
{
	return OpenWithDesktop(url);
}

std::vector<std::string> PickFiles(const std::string& title, const std::string& filterName, const std::string& patterns)
{
	const std::string spaced = SpaceSeparatedPatterns(patterns);
	std::string output;

	// zenity (GNOME and most desktops). Exit code 1 means the player cancelled; 127 means it isn't installed.
	const std::string zenity = "zenity --file-selection --multiple --separator='\n' --title=" + ShellQuote(title) +
		" --file-filter=" + ShellQuote(filterName + " | " + spaced) + " --file-filter='All files | *' 2>/dev/null";
	int code = RunShellCapture(zenity, output);
	if (code == 127) {
		// kdialog (KDE). The start folder is the player's home.
		const char* home = std::getenv("HOME");
		const std::string kdialog = "kdialog --title " + ShellQuote(title) + " --getopenfilename --multiple --separate-output " +
			ShellQuote(home ? home : ".") + " " + ShellQuote(spaced + "|" + filterName + "\n*|All files") + " 2>/dev/null";
		code = RunShellCapture(kdialog, output);
	}
	if (code != 0)
		return {};
	return SplitLines(output);
}

bool RandomBytes(unsigned char* out, std::size_t count)
{
	PosixFd random(::open("/dev/urandom", O_RDONLY | O_CLOEXEC));
	if (!random.Valid())
		return false;
	std::size_t done = 0;
	while (done < count) {
		const ssize_t got = ::read(random.Get(), out + done, count - done);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return false;
		done += static_cast<std::size_t>(got);
	}
	return true;
}

bool ProtectSecret(const std::string& plain, std::string& protectedText)
{
	protectedText = plain; // no per-user store here; the caller restricts the file to its owner
	return true;
}

bool UnprotectSecret(const std::string& protectedText, std::string& plain)
{
	plain = protectedText;
	return true;
}

void RestrictToOwner(const std::filesystem::path& file)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
}

struct LoopbackListener::Impl {
	PosixFd listener;
	PosixFd connection;
};

LoopbackListener::LoopbackListener() : m_impl(std::make_unique<Impl>())
{
}

LoopbackListener::~LoopbackListener() = default;

bool LoopbackListener::Start(unsigned short& port, std::string* err)
{
	m_impl->listener.Reset(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
	if (!m_impl->listener.Valid()) {
		if (err) *err = "Could not create the local OAuth callback listener.";
		return false;
	}
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (::bind(m_impl->listener.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
		::listen(m_impl->listener.Get(), 1) != 0) {
		if (err) *err = "Could not bind the local OAuth callback listener. Check whether local connections are blocked.";
		return false;
	}
	socklen_t addressLength = sizeof(address);
	if (::getsockname(m_impl->listener.Get(), reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
		if (err) *err = "Could not determine the local OAuth callback port.";
		return false;
	}
	port = ntohs(address.sin_port);
	return true;
}

LoopbackListener::Result LoopbackListener::WaitForRequest(int timeoutMs, std::string& head)
{
	head.clear();
	m_impl->connection.Reset();
	pollfd wanted{ m_impl->listener.Get(), POLLIN, 0 };
	const int ready = ::poll(&wanted, 1, timeoutMs);
	if (ready < 0)
		return errno == EINTR ? Result::Timeout : Result::Failed;
	if (ready == 0)
		return Result::Timeout;

	m_impl->connection.Reset(::accept4(m_impl->listener.Get(), nullptr, nullptr, SOCK_CLOEXEC));
	if (!m_impl->connection.Valid())
		return Result::Timeout;
	std::array<char, 16384> buffer{};
	std::size_t received = 0;
	while (received < buffer.size() - 1) {
		// A browser that connects and then says nothing must not hold sign-in up forever.
		pollfd incoming{ m_impl->connection.Get(), POLLIN, 0 };
		if (::poll(&incoming, 1, 5000) <= 0)
			break;
		const ssize_t count = ::recv(m_impl->connection.Get(), buffer.data() + received, buffer.size() - 1 - received, 0);
		if (count <= 0)
			break;
		received += static_cast<std::size_t>(count);
		if (std::string(buffer.data(), received).find("\r\n\r\n") != std::string::npos)
			break;
	}
	head.assign(buffer.data(), received);
	return Result::Request;
}

void LoopbackListener::Reply(const std::string& html)
{
	if (!m_impl->connection.Valid())
		return;
	const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
		std::to_string(html.size()) + "\r\n\r\n" + html;
	std::size_t sent = 0;
	while (sent < response.size()) {
		const ssize_t count = ::send(m_impl->connection.Get(), response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		sent += static_cast<std::size_t>(count);
	}
	m_impl->connection.Reset();
}

std::uint64_t SystemMemoryMb()
{
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long pageSize = sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || pageSize <= 0)
		return 0;
	return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) / (1024 * 1024);
}

std::string LocalNetworkAddress()
{
	// Connecting a UDP socket sends nothing, but makes the system pick the outgoing interface.
	PosixFd sock(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
	if (!sock.Valid())
		return {};
	sockaddr_in target{};
	target.sin_family = AF_INET;
	target.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
	sockaddr_in local{};
	socklen_t length = sizeof(local);
	if (::connect(sock.Get(), reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0 ||
		::getsockname(sock.Get(), reinterpret_cast<sockaddr*>(&local), &length) != 0)
		return {};
	char text[INET_ADDRSTRLEN] = {};
	if (!inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
		return {};
	return text;
}

std::vector<NetworkAddress> LocalNetworkAddresses()
{
	std::vector<NetworkAddress> out;
	ifaddrs* all = nullptr;
	if (getifaddrs(&all) != 0)
		return out;
	for (const ifaddrs* entry = all; entry; entry = entry->ifa_next) {
		if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
			continue;
		if (!(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK))
			continue;
		const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
		char text[INET_ADDRSTRLEN] = {};
		if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)))
			continue;
		// Self-assigned (169.254.x.x) addresses mean the adapter has no working network.
		if (std::string(text).rfind("169.254.", 0) == 0)
			continue;
		out.push_back({ text, entry->ifa_name ? entry->ifa_name : "" });
	}
	freeifaddrs(all);
	return out;
}

bool WaitForDisplayRefresh()
{
	return false;
}

double CompositorRefreshRate()
{
	return 0.0;
}

void UsePreciseTimer()
{
}

void AttachParentConsole()
{
}
std::filesystem::path ExecutableDir()
{
	std::error_code ec;
	const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
	return ec ? std::filesystem::path() : self.parent_path();
}

void ApplyTitleBarStyle(GLFWwindow*, const TitleBarStyle&)
{
}

}
