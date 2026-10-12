#include "Platform.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
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

bool OpenFile(const std::string& path)
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec))
		return false;
	const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
#ifdef _WIN32
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
#else
	const std::string command = "xdg-open \"" + absolute.string() + "\" &";
	return std::system(command.c_str()) == 0;
#endif
}

std::vector<std::string> PickFiles(const std::string& title, const std::string& filterName, const std::string& patterns)
{
	std::vector<std::string> picked;
#ifdef _WIN32
	const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileOpenDialog* dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
		const std::wstring wideTitle = std::filesystem::u8path(title).wstring();
		const std::wstring wideName = std::filesystem::u8path(filterName).wstring();
		const std::wstring widePatterns = std::filesystem::u8path(patterns).wstring();
		const COMDLG_FILTERSPEC filters[] = { { wideName.c_str(), widePatterns.c_str() }, { L"All files", L"*.*" } };
		dialog->SetFileTypes(2, filters);
		dialog->SetTitle(wideTitle.c_str());
		FILEOPENDIALOGOPTIONS options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
		IShellItemArray* items = nullptr;
		if (SUCCEEDED(dialog->Show(GetActiveWindow())) && SUCCEEDED(dialog->GetResults(&items))) {
			DWORD count = 0;
			items->GetCount(&count);
			for (DWORD i = 0; i < count; ++i) {
				IShellItem* item = nullptr;
				PWSTR path = nullptr;
				if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					picked.push_back(std::filesystem::path(path).u8string());
					CoTaskMemFree(path);
				}
				if (item)
					item->Release();
			}
			items->Release();
		}
		dialog->Release();
	}
	if (SUCCEEDED(init))
		CoUninitialize();
#else
	(void)title;
	(void)filterName;
	(void)patterns;
#endif
	return picked;
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

std::vector<NetworkAddress> LocalNetworkAddresses()
{
	std::vector<NetworkAddress> out;
#ifdef _WIN32
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buffer;
	ULONG result = ERROR_BUFFER_OVERFLOW;
	for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
		buffer.resize(size);
		result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
			nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
	}
	if (result != NO_ERROR)
		return out;
	for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		std::string name;
		if (adapter->FriendlyName) {
			const int length = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
			if (length > 1) {
				name.resize(static_cast<std::size_t>(length - 1));
				WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, name.data(), length, nullptr, nullptr);
			}
		}
		for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
			const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
			if (!ipv4 || ipv4->sin_family != AF_INET)
				continue;
			char text[INET_ADDRSTRLEN] = {};
			if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)))
				continue;
			// Self-assigned (169.254.x.x) addresses mean the adapter has no working network.
			if (std::string(text).rfind("169.254.", 0) == 0)
				continue;
			out.push_back({ text, name });
		}
	}
#endif
	return out;
}

}
