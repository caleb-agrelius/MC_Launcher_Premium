#include "Process.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <fstream>

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
				bool killOnCancel,
				const std::atomic<bool>* kill)
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
		if (kill && kill->load()) {
			TerminateProcess(process.hProcess, 1);
			WaitForSingleObject(process.hProcess, 5000);
			CloseHandle(process.hProcess);
			if (err) *err = "Stopped";
			return false;
		}
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
	(void)args; (void)workDir; (void)logFile; (void)wait; (void)exitCode; (void)cancel; (void)killOnCancel; (void)kill;
	if (err) *err = "Launching programs is only implemented on Windows";
	return false;
#endif
}

ServerProcess::~ServerProcess()
{
	if (Running())
		Kill();
#ifdef _WIN32
	if (m_input)
		CloseHandle(static_cast<HANDLE>(m_input));
	m_input = nullptr;
#endif
	if (m_reader.joinable())
		m_reader.join();
#ifdef _WIN32
	if (m_output)
		CloseHandle(static_cast<HANDLE>(m_output));
	if (m_process)
		CloseHandle(static_cast<HANDLE>(m_process));
#endif
}

bool ServerProcess::Start(const std::vector<std::string>& args, const fs::path& workDir, const fs::path& logFile,
	std::string* err)
{
#ifdef _WIN32
	if (m_process) {
		if (err) *err = "This server process has already been started";
		return false;
	}
	if (args.empty()) {
		if (err) *err = "No program to run";
		return false;
	}
	std::wstring commandLine;
	for (const std::string& arg : args)
		AppendQuoted(commandLine, Widen(arg));

	SECURITY_ATTRIBUTES inherit{ sizeof(inherit), nullptr, TRUE };
	HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
	if (!CreatePipe(&inputRead, &inputWrite, &inherit, 0) || !CreatePipe(&outputRead, &outputWrite, &inherit, 0)) {
		if (err) *err = LastErrorText("Couldn't create pipes");
		if (inputRead) CloseHandle(inputRead);
		if (inputWrite) CloseHandle(inputWrite);
		return false;
	}
	// Our ends must not be inherited by the child.
	SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = inputRead;
	startup.hStdOutput = outputWrite;
	startup.hStdError = outputWrite;
	PROCESS_INFORMATION process{};
	const std::wstring dir = workDir.wstring();
	const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		nullptr, dir.empty() ? nullptr : dir.c_str(), &startup, &process);
	const std::string startError = started ? std::string() : LastErrorText("Couldn't start " + args[0]);
	CloseHandle(inputRead);
	CloseHandle(outputWrite);
	if (!started) {
		CloseHandle(inputWrite);
		CloseHandle(outputRead);
		if (err) *err = startError;
		return false;
	}
	CloseHandle(process.hThread);
	m_process = process.hProcess;
	m_input = inputWrite;
	m_output = outputRead;
	m_logFile = logFile;
	std::error_code ec;
	fs::create_directories(logFile.parent_path(), ec);
	m_reader = std::thread([this]() { ReadOutput(); });
	return true;
#else
	(void)args; (void)workDir; (void)logFile;
	if (err) *err = "Running servers is only implemented on Windows";
	return false;
#endif
}

void ServerProcess::ReadOutput()
{
#ifdef _WIN32
	std::ofstream log(m_logFile, std::ios::app | std::ios::binary);
	std::string pending;
	char buffer[4096];
	DWORD read = 0;
	while (ReadFile(static_cast<HANDLE>(m_output), buffer, sizeof(buffer), &read, nullptr) && read > 0) {
		log.write(buffer, read);
		log.flush();
		pending.append(buffer, read);
		std::size_t newline;
		while ((newline = pending.find('\n')) != std::string::npos) {
			std::string line = pending.substr(0, newline);
			pending.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.push_back(std::move(line));
			++m_totalLines;
			if (m_lines.size() > kMaxLines)
				m_lines.pop_front();
		}
	}
	if (!pending.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lines.push_back(pending);
		++m_totalLines;
		if (m_lines.size() > kMaxLines)
			m_lines.pop_front();
	}
#endif
}

bool ServerProcess::Running() const
{
#ifdef _WIN32
	return m_process && WaitForSingleObject(static_cast<HANDLE>(m_process), 0) == WAIT_TIMEOUT;
#else
	return false;
#endif
}

int ServerProcess::ExitCode() const
{
#ifdef _WIN32
	DWORD code = 0;
	if (m_process && GetExitCodeProcess(static_cast<HANDLE>(m_process), &code))
		return static_cast<int>(code);
#endif
	return -1;
}

void ServerProcess::Send(const std::string& line)
{
#ifdef _WIN32
	if (!m_input || !Running())
		return;
	const std::string text = line + "\n";
	DWORD written = 0;
	WriteFile(static_cast<HANDLE>(m_input), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lines.push_back("> " + line);
	++m_totalLines;
	if (m_lines.size() > kMaxLines)
		m_lines.pop_front();
#else
	(void)line;
#endif
}

void ServerProcess::Kill()
{
#ifdef _WIN32
	if (m_process && Running()) {
		TerminateProcess(static_cast<HANDLE>(m_process), 1);
		WaitForSingleObject(static_cast<HANDLE>(m_process), 5000);
	}
#endif
}

bool ServerProcess::WaitForExit(int timeoutMs)
{
#ifdef _WIN32
	if (!m_process)
		return true;
	return WaitForSingleObject(static_cast<HANDLE>(m_process), static_cast<DWORD>(timeoutMs)) != WAIT_TIMEOUT;
#else
	(void)timeoutMs;
	return true;
#endif
}

std::vector<std::string> ServerProcess::Lines(std::uint64_t* total) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (total)
		*total = m_totalLines;
	return std::vector<std::string>(m_lines.begin(), m_lines.end());
}
