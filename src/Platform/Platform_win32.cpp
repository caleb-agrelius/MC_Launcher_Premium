// Windows definitions of the Platform functions (see Platform.h).
#include "Platform/Platform.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <io.h>
#include <iostream>
#include <iterator>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <timeapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dwmapi.lib")

namespace Platform {

bool OpenFolder(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

bool OpenFile(const std::string& path)
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec))
		return false;
	const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

std::vector<std::string> PickFiles(const std::string& title, const std::string& filterName, const std::string& patterns)
{
	std::vector<std::string> picked;
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
	return picked;
}

std::uint64_t SystemMemoryMb()
{
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		return status.ullTotalPhys / (1024 * 1024);
	return 0;
}

std::string LocalNetworkAddress()
{
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
}

std::vector<NetworkAddress> LocalNetworkAddresses()
{
	std::vector<NetworkAddress> out;
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
	return out;
}

bool WaitForDisplayRefresh()
{
	return SUCCEEDED(DwmFlush());
}

double CompositorRefreshRate()
{
	DWM_TIMING_INFO info{};
	info.cbSize = sizeof(info);
	if (FAILED(DwmGetCompositionTimingInfo(nullptr, &info)) || info.rateRefresh.uiDenominator == 0)
		return 0.0;
	return static_cast<double>(info.rateRefresh.uiNumerator) / info.rateRefresh.uiDenominator;
}

void UsePreciseTimer()
{
	timeBeginPeriod(1);
}

void AttachParentConsole()
{
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		return;
	// A GUI-subsystem process can inherit console handles that look valid but don't reach the terminal,
	// so point each stream at the console unless it was redirected to a file or pipe.
	const auto bind = [](DWORD id, const char* device, const char* mode, FILE* stream) {
		const DWORD type = GetFileType(GetStdHandle(id));
		if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
			return;
		FILE* reopened = nullptr;
		if (freopen_s(&reopened, device, mode, stream) == 0)
			SetStdHandle(id, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream))));
	};
	bind(STD_OUTPUT_HANDLE, "CONOUT$", "w", stdout);
	bind(STD_ERROR_HANDLE, "CONOUT$", "w", stderr);
	bind(STD_INPUT_HANDLE, "CONIN$", "r", stdin);
	std::ios::sync_with_stdio();
}
std::filesystem::path ExecutableDir()
{
	wchar_t buffer[MAX_PATH * 4];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length > 0 && length < std::size(buffer))
		return std::filesystem::path(buffer, buffer + length).parent_path();
	return {};
}

void ApplyTitleBarStyle(GLFWwindow* window, const TitleBarStyle& style)
{
	const HWND hwnd = glfwGetWin32Window(window);
	const BOOL dark = style.dark ? TRUE : FALSE;
	DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
	const COLORREF caption = RGB(style.caption.r, style.caption.g, style.caption.b);
	const COLORREF text = RGB(style.text.r, style.text.g, style.text.b);
	DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
	DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}

}
