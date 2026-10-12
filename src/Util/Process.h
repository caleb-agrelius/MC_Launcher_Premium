#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Starts a program without a console window. args[0] is the executable.
// stdout and stderr are appended to logFile. If wait is true, blocks until the
// program exits and fills exitCode. When cancel becomes true while waiting, the
// program is killed if killOnCancel is set; otherwise it keeps running and
// RunProcess returns false with err = "Cancelled". When kill becomes true while
// waiting, the program is always killed and RunProcess returns false with
// err = "Stopped".
// Windows only; elsewhere it fails with an error.
bool RunProcess(const std::vector<std::string>& args,
				const std::filesystem::path& workDir,
				const std::filesystem::path& logFile,
				bool wait,
				int* exitCode = nullptr,
				std::string* err = nullptr,
				const std::atomic<bool>* cancel = nullptr,
				bool killOnCancel = true,
				const std::atomic<bool>* kill = nullptr);

// A long-running program with a console we drive ourselves, used for Minecraft servers:
// its output is collected line by line (and appended to a log file), and lines can be
// written to its input. Windows only; Start fails elsewhere.
class ServerProcess {
public:
	ServerProcess() = default;
	~ServerProcess(); // kills the program if it's still running

	ServerProcess(const ServerProcess&) = delete;
	ServerProcess& operator=(const ServerProcess&) = delete;

	bool Start(const std::vector<std::string>& args,
			   const std::filesystem::path& workDir,
			   const std::filesystem::path& logFile,
			   std::string* err = nullptr);
	bool Running() const;
	int ExitCode() const; // valid once Running() is false
	// Writes a line (a server command such as "stop" or "op Steve") to the program's input.
	void Send(const std::string& line);
	void Kill();
	// Waits up to timeoutMs for the program to exit. Returns true if it has.
	bool WaitForExit(int timeoutMs);
	// The most recent output lines (up to kMaxLines) and a counter that grows with every
	// new line, so the UI can tell when to scroll.
	std::vector<std::string> Lines(std::uint64_t* total = nullptr) const;
	// Lines received so far, including dropped ones; cheap, for noticing new output.
	std::uint64_t TotalLines() const;

	static constexpr std::size_t kMaxLines = 2000;

private:
	void ReadOutput();

	void* m_process = nullptr;   // HANDLE
	void* m_input = nullptr;     // HANDLE, write end of the program's stdin
	void* m_output = nullptr;    // HANDLE, read end of the program's stdout/stderr
	std::filesystem::path m_logFile;
	std::thread m_reader;
	std::atomic<bool> m_readerDone{ false };
	mutable std::mutex m_mutex;
	std::deque<std::string> m_lines;
	std::uint64_t m_totalLines = 0;
};
