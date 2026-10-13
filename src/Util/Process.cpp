#include "Process.h"

#include "Util/DebugTrace.h"
#include "Util/TracedFs.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Util/PosixFd.h"

extern char** environ;
#endif

#include <chrono>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// Traces a child process being started, or failing to.
static void TraceStart(const std::vector<std::string>& args, const fs::path& workDir, const std::string& startError)
{
	if (!Debug::Enabled(Debug::kProcess))
		return;
	Debug::Write(Debug::kProcess, (startError.empty() ? "start  " : "FAILED to start: " + startError + "  ") +
		Debug::DescribeCommand(args) + "  (in " + Debug::PathText(workDir.u8string()) + ")");
}

#ifdef _WIN32

struct ServerProcess::Native {
	HANDLE process = nullptr;
	HANDLE input = nullptr;   // write end of the program's stdin
	HANDLE output = nullptr;  // read end of the program's stdout/stderr
};

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

// Starts a hidden child with the given standard handles. Only those handles are inherited:
// without the list, a child gets every inheritable handle the launcher has open at that
// moment, such as another child's pipe or log, which keeps that pipe open after its own
// process exits and stalls whoever reads it.
static bool StartChild(std::wstring& commandLine, const std::wstring& dir, HANDLE input, HANDLE output,
	PROCESS_INFORMATION& process)
{
	HANDLE handles[2];
	DWORD handleCount = 0;
	if (input && input != INVALID_HANDLE_VALUE)
		handles[handleCount++] = input;
	handles[handleCount++] = output;

	SIZE_T attributeSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
	std::vector<char> attributeBuffer(attributeSize);
	auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
	if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize))
		return false;
	const bool listed = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
		handleCount * sizeof(HANDLE), nullptr, nullptr) != FALSE;

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = (input && input != INVALID_HANDLE_VALUE) ? input : nullptr;
	startup.StartupInfo.hStdOutput = output;
	startup.StartupInfo.hStdError = output;
	startup.lpAttributeList = listed ? attributes : nullptr;
	const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW | (listed ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, dir.empty() ? nullptr : dir.c_str(),
		&startup.StartupInfo, &process);
	const DWORD error = GetLastError();
	DeleteProcThreadAttributeList(attributes);
	SetLastError(error);
	return started != FALSE;
}

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
	if (args.empty()) {
		if (err) *err = "No program to run";
		return false;
	}
	std::wstring commandLine;
	for (const std::string& arg : args)
		AppendQuoted(commandLine, Widen(arg));

	SECURITY_ATTRIBUTES inherit{ sizeof(inherit), nullptr, TRUE };
	std::error_code ec;
	TracedFs::CreateDirectories(logFile.parent_path(), ec);
	HANDLE log = CreateFileW(logFile.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		&inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE) {
		if (err) *err = LastErrorText("Couldn't open log file");
		return false;
	}
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

	PROCESS_INFORMATION process{};
	const bool started = StartChild(commandLine, workDir.wstring(), nul, log, process);
	const std::string startError = started ? std::string() : LastErrorText("Couldn't start " + args[0]);
	TraceStart(args, workDir, startError);
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
			Debug::Write(Debug::kProcess, "killed  " + Debug::DescribeCommand({ args[0] }) + " (stopped)");
			if (err) *err = "Stopped";
			return false;
		}
		if (cancel && cancel->load()) {
			if (killOnCancel)
				TerminateProcess(process.hProcess, 1);
			CloseHandle(process.hProcess);
			Debug::Write(Debug::kProcess, std::string(killOnCancel ? "killed  " : "left running  ") + Debug::DescribeCommand({ args[0] }) + " (cancelled)");
			if (err) *err = "Cancelled";
			return false;
		}
	}
	DWORD code = 0;
	GetExitCodeProcess(process.hProcess, &code);
	Debug::Write(Debug::kProcess, "exit " + std::to_string(static_cast<int>(code)) + "  " + Debug::DescribeCommand({ args[0] }));
	CloseHandle(process.hProcess);
	if (exitCode) *exitCode = static_cast<int>(code);
	return true;
}

ServerProcess::ServerProcess() = default;

ServerProcess::~ServerProcess()
{
	if (Running())
		Kill();
	if (m_native && m_native->input)
		CloseHandle(m_native->input);
	if (m_reader.joinable()) {
		// The reader ends when every copy of the pipe's write end is closed. A process the
		// server started itself can still hold one after the server is gone, so stop waiting.
		const HANDLE reader = static_cast<HANDLE>(m_reader.native_handle());
		while (!m_readerDone.load()) {
			CancelSynchronousIo(reader);
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		m_reader.join();
	}
	if (m_native) {
		if (m_native->output)
			CloseHandle(m_native->output);
		if (m_native->process)
			CloseHandle(m_native->process);
	}
}

bool ServerProcess::Start(const std::vector<std::string>& args, const fs::path& workDir, const fs::path& logFile,
	std::string* err)
{
	if (m_native) {
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

	PROCESS_INFORMATION process{};
	const bool started = StartChild(commandLine, workDir.wstring(), inputRead, outputWrite, process);
	const std::string startError = started ? std::string() : LastErrorText("Couldn't start " + args[0]);
	TraceStart(args, workDir, startError);
	CloseHandle(inputRead);
	CloseHandle(outputWrite);
	if (!started) {
		CloseHandle(inputWrite);
		CloseHandle(outputRead);
		if (err) *err = startError;
		return false;
	}
	CloseHandle(process.hThread);
	m_native = std::make_unique<Native>();
	m_native->process = process.hProcess;
	m_native->input = inputWrite;
	m_native->output = outputRead;
	m_logFile = logFile;
	std::error_code ec;
	TracedFs::CreateDirectories(logFile.parent_path(), ec);
	m_reader = std::thread([this]() { ReadOutput(); });
	return true;
}

// Reads what the program has written. Returns the number of bytes read, or 0 once its pipe has closed.
static std::size_t ReadPipe(HANDLE pipe, char* buffer, std::size_t size)
{
	DWORD read = 0;
	if (!ReadFile(pipe, buffer, static_cast<DWORD>(size), &read, nullptr))
		return 0;
	return read;
}

bool ServerProcess::Running() const
{
	return m_native && m_native->process && WaitForSingleObject(m_native->process, 0) == WAIT_TIMEOUT;
}

int ServerProcess::ExitCode() const
{
	DWORD code = 0;
	if (m_native && m_native->process && GetExitCodeProcess(m_native->process, &code))
		return static_cast<int>(code);
	return -1;
}

void ServerProcess::Send(const std::string& line)
{
	if (!m_native || !m_native->input || !Running())
		return;
	const std::string text = line + "\n";
	DWORD written = 0;
	WriteFile(m_native->input, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
	AddLine("> " + line);
}

void ServerProcess::Kill()
{
	if (m_native && Running()) {
		TerminateProcess(m_native->process, 1);
		WaitForSingleObject(m_native->process, 5000);
	}
}

bool ServerProcess::WaitForExit(int timeoutMs)
{
	if (!m_native || !m_native->process)
		return true;
	return WaitForSingleObject(m_native->process, static_cast<DWORD>(timeoutMs)) != WAIT_TIMEOUT;
}

#else // POSIX

namespace {

// How long a program gets to shut down after SIGTERM before it is killed outright.
constexpr int kTerminateGraceMs = 3000;
constexpr auto kPollInterval = std::chrono::milliseconds(20);
// The status Reap reports for a program that is no longer our child (already reaped elsewhere).
constexpr int kNotOurs = -1;

std::string ErrorText(const std::string& what, int error)
{
	return what + " (" + std::strerror(error) + ")";
}

// Moves a descriptor above 2, so it can't be confused with a standard stream if those were closed.
PosixFd AboveStandardStreams(PosixFd fd)
{
	if (!fd.Valid() || fd.Get() > 2)
		return fd;
	return PosixFd(fcntl(fd.Get(), F_DUPFD_CLOEXEC, 3));
}

// Every descriptor the launcher opens is close-on-exec, so a child inherits only the
// standard streams it is given below. Without that it would also get another child's pipe
// or log, which keeps that pipe open after its own process exits and stalls whoever reads it.
PosixFd OpenDescriptor(const fs::path& path, int flags)
{
	return AboveStandardStreams(PosixFd(::open(path.c_str(), flags | O_CLOEXEC, 0644)));
}

bool MakePipe(PosixFd& readEnd, PosixFd& writeEnd)
{
	int fds[2];
#ifdef __linux__
	if (::pipe2(fds, O_CLOEXEC) != 0)
		return false;
#else
	if (::pipe(fds) != 0)
		return false;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
	readEnd = AboveStandardStreams(PosixFd(fds[0]));
	writeEnd = AboveStandardStreams(PosixFd(fds[1]));
	return readEnd.Valid() && writeEnd.Valid();
}

// Starts the program with the given standard streams. Returns 0, or the error number.
int StartChild(const std::vector<std::string>& args, const fs::path& workDir, int input, int output, pid_t& pid)
{
	// A relative path to the program is relative to the launcher, not to the folder it runs in.
	fs::path program(args[0]);
	if (program.has_parent_path() && program.is_relative()) {
		std::error_code ec;
		program = fs::absolute(program, ec);
	}

	posix_spawn_file_actions_t actions;
	if (const int error = posix_spawn_file_actions_init(&actions); error != 0)
		return error;
	posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
	posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
	if (!workDir.empty())
		posix_spawn_file_actions_addchdir_np(&actions, workDir.c_str());

	std::vector<char*> argv;
	argv.reserve(args.size() + 1);
	for (const std::string& arg : args)
		argv.push_back(const_cast<char*>(arg.c_str()));
	argv.push_back(nullptr);

	const int error = posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	return error;
}

// The shell's convention: the exit code, or 128 plus the signal that ended the program.
int ExitCodeOf(int status)
{
	if (status == kNotOurs)
		return -1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return -1;
}

// Reaps the program if it has ended. Returns true when it has (status is then set).
bool Reap(pid_t pid, int& status)
{
	for (;;) {
		const pid_t result = ::waitpid(pid, &status, WNOHANG);
		if (result == pid)
			return true;
		if (result == 0)
			return false;
		if (errno != EINTR) {
			status = kNotOurs;
			return true;
		}
	}
}

// Waits up to timeoutMs for the program to end. Returns true when it has.
bool WaitFor(pid_t pid, int timeoutMs, int& status)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (!Reap(pid, status)) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(kPollInterval);
	}
	return true;
}

// Asks the program to end (SIGTERM, so a Java server can still save), then kills it if it
// hasn't gone within the grace period. Always reaps it.
void Terminate(pid_t pid, int graceMs)
{
	int status = 0;
	if (Reap(pid, status))
		return;
	::kill(pid, SIGTERM);
	if (WaitFor(pid, graceMs, status))
		return;
	::kill(pid, SIGKILL);
	while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
}

// Reaps a program we leave running, once it ends, so it doesn't stay a zombie.
void ReapInBackground(pid_t pid)
{
	std::thread([pid]() {
		int status = 0;
		while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
	}).detach();
}

}

struct ServerProcess::Native {
	pid_t pid = -1;
	PosixFd input;   // write end of the program's stdin
	PosixFd output;  // read end of the program's stdout/stderr
	std::atomic<bool> stopReading{ false };
	mutable std::mutex mutex; // guards exited and exitCode
	mutable bool exited = false;
	mutable int exitCode = -1;
};

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
	if (args.empty()) {
		if (err) *err = "No program to run";
		return false;
	}
	std::error_code ec;
	TracedFs::CreateDirectories(logFile.parent_path(), ec);
	PosixFd log = OpenDescriptor(logFile, O_WRONLY | O_APPEND | O_CREAT);
	if (!log.Valid()) {
		if (err) *err = ErrorText("Couldn't open log file", errno);
		return false;
	}
	PosixFd nul = OpenDescriptor("/dev/null", O_RDONLY);
	if (!nul.Valid()) {
		if (err) *err = ErrorText("Couldn't open /dev/null", errno);
		return false;
	}

	pid_t pid = -1;
	const int startErrno = StartChild(args, workDir, nul.Get(), log.Get(), pid);
	const std::string startError = startErrno == 0 ? std::string() : ErrorText("Couldn't start " + args[0], startErrno);
	TraceStart(args, workDir, startError);
	if (startErrno != 0) {
		if (err) *err = startError;
		return false;
	}
	log.Reset();
	nul.Reset();

	if (!wait) {
		ReapInBackground(pid);
		return true;
	}
	for (;;) {
		int status = 0;
		if (Reap(pid, status)) {
			const int code = ExitCodeOf(status);
			Debug::Write(Debug::kProcess, "exit " + std::to_string(code) + "  " + Debug::DescribeCommand({ args[0] }));
			if (exitCode) *exitCode = code;
			return true;
		}
		if (kill && kill->load()) {
			Terminate(pid, kTerminateGraceMs);
			Debug::Write(Debug::kProcess, "killed  " + Debug::DescribeCommand({ args[0] }) + " (stopped)");
			if (err) *err = "Stopped";
			return false;
		}
		if (cancel && cancel->load()) {
			if (killOnCancel)
				Terminate(pid, kTerminateGraceMs);
			else
				ReapInBackground(pid);
			Debug::Write(Debug::kProcess, std::string(killOnCancel ? "killed  " : "left running  ") + Debug::DescribeCommand({ args[0] }) + " (cancelled)");
			if (err) *err = "Cancelled";
			return false;
		}
		std::this_thread::sleep_for(kPollInterval);
	}
}

ServerProcess::ServerProcess() = default;

ServerProcess::~ServerProcess()
{
	if (Running())
		Kill();
	if (m_native)
		m_native->input.Reset();
	if (m_reader.joinable()) {
		// The reader ends when every copy of the pipe's write end is closed. A program the
		// server started itself can still hold one after the server is gone, so stop waiting.
		for (int attempt = 0; attempt < 10 && !m_readerDone.load(); ++attempt)
			std::this_thread::sleep_for(kPollInterval);
		m_native->stopReading.store(true);
		m_reader.join();
	}
}

bool ServerProcess::Start(const std::vector<std::string>& args, const fs::path& workDir, const fs::path& logFile,
	std::string* err)
{
	if (m_native) {
		if (err) *err = "This server process has already been started";
		return false;
	}
	if (args.empty()) {
		if (err) *err = "No program to run";
		return false;
	}
	// Writing to a program that has already gone must fail with an error, not end the launcher.
	static const bool sigpipeIgnored = (std::signal(SIGPIPE, SIG_IGN), true);
	(void)sigpipeIgnored;

	PosixFd inputRead, inputWrite, outputRead, outputWrite;
	if (!MakePipe(inputRead, inputWrite) || !MakePipe(outputRead, outputWrite)) {
		if (err) *err = ErrorText("Couldn't create pipes", errno);
		return false;
	}
	pid_t pid = -1;
	const int startErrno = StartChild(args, workDir, inputRead.Get(), outputWrite.Get(), pid);
	const std::string startError = startErrno == 0 ? std::string() : ErrorText("Couldn't start " + args[0], startErrno);
	TraceStart(args, workDir, startError);
	if (startErrno != 0) {
		if (err) *err = startError;
		return false;
	}
	// The child has its own copies; ours must go so the output pipe closes when it exits.
	inputRead.Reset();
	outputWrite.Reset();

	m_native = std::make_unique<Native>();
	m_native->pid = pid;
	m_native->input = std::move(inputWrite);
	m_native->output = std::move(outputRead);
	m_logFile = logFile;
	std::error_code ec;
	TracedFs::CreateDirectories(logFile.parent_path(), ec);
	m_reader = std::thread([this]() { ReadOutput(); });
	return true;
}

// Reads what the program has written. Returns the number of bytes read, or 0 once its pipe has
// closed or reading is stopped.
static std::size_t ReadPipe(const std::atomic<bool>& stop, int pipe, char* buffer, std::size_t size)
{
	while (!stop.load()) {
		pollfd wanted{ pipe, POLLIN, 0 };
		const int ready = ::poll(&wanted, 1, 100);
		if (ready < 0 && errno != EINTR)
			return 0;
		if (ready <= 0)
			continue;
		const ssize_t count = ::read(pipe, buffer, size);
		if (count >= 0)
			return static_cast<std::size_t>(count);
		if (errno != EINTR && errno != EAGAIN)
			return 0;
	}
	return 0;
}

bool ServerProcess::Running() const
{
	if (!m_native || m_native->pid <= 0)
		return false;
	std::lock_guard<std::mutex> lock(m_native->mutex);
	if (!m_native->exited) {
		int status = 0;
		if (!Reap(m_native->pid, status))
			return true;
		m_native->exited = true;
		m_native->exitCode = ExitCodeOf(status);
	}
	return false;
}

int ServerProcess::ExitCode() const
{
	if (!m_native || Running())
		return -1;
	std::lock_guard<std::mutex> lock(m_native->mutex);
	return m_native->exitCode;
}

void ServerProcess::Send(const std::string& line)
{
	if (!m_native || !m_native->input.Valid() || !Running())
		return;
	const std::string text = line + "\n";
	std::size_t sent = 0;
	while (sent < text.size()) {
		const ssize_t count = ::write(m_native->input.Get(), text.data() + sent, text.size() - sent);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		sent += static_cast<std::size_t>(count);
	}
	AddLine("> " + line);
}

void ServerProcess::Kill()
{
	if (m_native && Running()) {
		::kill(m_native->pid, SIGTERM);
		if (!WaitForExit(2000))
			::kill(m_native->pid, SIGKILL);
		WaitForExit(5000);
	}
}

bool ServerProcess::WaitForExit(int timeoutMs)
{
	if (!m_native || m_native->pid <= 0)
		return true;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (Running()) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(kPollInterval);
	}
	return true;
}

#endif

void ServerProcess::AddLine(std::string line)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lines.push_back(std::move(line));
	++m_totalLines;
	if (m_lines.size() > kMaxLines)
		m_lines.pop_front();
}

void ServerProcess::ReadOutput()
{
	std::ofstream log(m_logFile, std::ios::app | std::ios::binary);
	std::string pending;
	char buffer[4096];
	for (;;) {
#ifdef _WIN32
		const std::size_t read = ReadPipe(m_native->output, buffer, sizeof(buffer));
#else
		const std::size_t read = ReadPipe(m_native->stopReading, m_native->output.Get(), buffer, sizeof(buffer));
#endif
		if (read == 0)
			break;
		log.write(buffer, static_cast<std::streamsize>(read));
		log.flush();
		pending.append(buffer, read);
		std::size_t newline;
		while ((newline = pending.find('\n')) != std::string::npos) {
			std::string line = pending.substr(0, newline);
			pending.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			AddLine(std::move(line));
		}
	}
	if (!pending.empty())
		AddLine(pending);
	m_readerDone.store(true);
}

std::uint64_t ServerProcess::TotalLines() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_totalLines;
}

std::vector<std::string> ServerProcess::Lines(std::uint64_t* total) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (total)
		*total = m_totalLines;
	return std::vector<std::string>(m_lines.begin(), m_lines.end());
}
