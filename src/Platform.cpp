#include "Platform.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace Platform {

bool OpenFolder(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
#ifdef _WIN32
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
#else
	const std::string command = "xdg-open \"" + absolute.string() + "\" &";
	return std::system(command.c_str()) == 0;
#endif
}

std::uint64_t SystemMemoryMb()
{
#ifdef _WIN32
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		return status.ullTotalPhys / (1024 * 1024);
	return 0;
#else
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long pageSize = sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || pageSize <= 0)
		return 0;
	return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) / (1024 * 1024);
#endif
}

std::string LocalNetworkAddress()
{
#ifdef _WIN32
	// libcurl has already started Winsock; starting it again is harmless.
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return {};
	// Connecting a UDP socket sends nothing, but makes Windows pick the outgoing interface.
	std::string address;
	const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock != INVALID_SOCKET) {
		sockaddr_in target{};
		target.sin_family = AF_INET;
		target.sin_port = htons(53);
		inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
		sockaddr_in local{};
		int length = sizeof(local);
		if (connect(sock, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0 &&
			getsockname(sock, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
			char text[INET_ADDRSTRLEN] = {};
			if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
				address = text;
		}
		closesocket(sock);
	}
	WSACleanup();
	return address;
#else
	return {};
#endif
}

}
