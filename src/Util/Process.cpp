#include "Process.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static std::wstring Widen(const std::string& text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}

// Quotes one argument the way the Microsoft C runtime parses command lines.
static void AppendQuoted(std::wstring& commandLine, const std::wstring& arg)
{
	if (!commandLine.empty())
		commandLine += L' ';
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		commandLine += arg;
		return;
	}
	commandLine += L'"';
	for (auto it = arg.begin();; ++it) {
		std::size_t backslashes = 0;
		while (it != arg.end() && *it == L'\\') {
			++it;
			++backslashes;
		}
		if (it == arg.end()) {
			commandLine.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"') {
			commandLine.append(backslashes * 2 + 1, L'\\');
			commandLine += L'"';
		} else {
			commandLine.append(backslashes, L'\\');
			commandLine += *it;
		}
	}
	commandLine += L'"';
}

static std::string LastErrorText(const std::string& what)
{
	return what +" (Windows error " + std::to_string(GetLastError()) + ")";
}
#endif

bool RunProcess(const std::vector<std::string>& args,
				const fs::path& workDir,
				const fs::path& logFile,
				bool wait,
				int* exitCode,
				std::string* err,
				const std::atomic<bool>* cancel,
				bool killOnCancel)
{
#ifdef _WIN32
	if (args.empty()) {
		if (err) *err = "No program to run";
		return false;
	}
	std::wstring commandLine;
	for (const std::string& arg : args)
		AppendQuoted(commandLine, Widen(arg));

	SECURITY_ATTRIBUTES inherit{ sizeof(inherit), nullptr, TRUE };
	std::error_code ec;
	fs::create_directories(logFile.parent_path(), ec);
	HANDLE log = CreateFileW(logFile.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		&inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE) {
		if (err) *err = LastErrorText("Couldn't open log file");
		return false;
	}
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = nul;
	startup.hStdOutput = log;
	startup.hStdError = log;
	PROCESS_INFORMATION process{};
	const std::wstring dir = workDir.wstring();
	const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		nullptr, dir.empty() ? nullptr : dir.c_str(), &startup, &process);
	const std::string startError = started ? std::string() : LastErrorText("Couldn't start " + args[0]);
	CloseHandle(log);
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (!started) {
		if (err) *err = startError;
		return false;
	}
	CloseHandle(process.hThread);

	if (!wait) {
		CloseHandle(process.hProcess);
		return true;
	}
	while (WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
		if (cancel && cancel->load()) {
			if (killOnCancel)
				TerminateProcess(process.hProcess, 1);
			CloseHandle(process.hProcess);
			if (err) *err = "Cancelled";
			return false;
		}
	}
	DWORD code = 0;
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hProcess);
	if (exitCode) *exitCode = static_cast<int>(code);
	return true;
#else
	(void)args; (void)workDir; (void)logFile; (void)wait; (void)exitCode; (void)cancel; (void)killOnCancel;
	if (err) *err = "Launching programs is only implemented on Windows";
	return false;
#endif
}
