#pragma once

#include <iosfwd>
#include <streambuf>
#include <string>
#include <vector>

// The debug harness: timestamped trace lines for API requests, file operations, child
// processes and app.log, written to the console. Everything is off unless switched on with
// `--debug[=channels]` or the MCLP_DEBUG environment variable (see docs/features/debug-harness.md),
// and then a trace call costs one atomic load per channel check. Safe to call from any thread.
namespace Debug {

enum Channel : unsigned {
	kHttp = 1,     // each API request and download: URL, status, size, time
	kHttpBody = 2, // the start of each GET response body (not part of "all": bodies can be large)
	kFile = 4,     // files and folders created, copied, renamed, removed or written
	kProcess = 8,  // child processes: command line (tokens hidden) and exit code
	kLog = 16,     // everything the launcher writes to app.log
	kAll = kHttp | kFile | kProcess | kLog,
};

// Reads a comma-separated channel list: "http,file", "all", or "all,body". Names are
// http, body, file, process, log and all. Returns false and names the unknown entries in
// unknown (when given); the known ones are still added to mask.
bool ParseChannels(const std::string& text, unsigned& mask, std::string* unknown = nullptr);

// Reads `--debug` / `--debug=list` from the arguments and MCLP_DEBUG from the environment
// ("1" or empty means all), applies the result with Configure and returns the mask.
// Problems with the list are described in warnings.
unsigned ConfigureFromEnvironment(int argc, char** argv, std::vector<std::string>& warnings);

// Turns the channels in mask on and sends the trace to out (the console, std::cerr, if null).
// Call before starting threads.
void Configure(unsigned mask, std::ostream* out = nullptr);

bool Enabled(Channel channel);
// "http,file,..." for the channels in mask.
std::string ChannelNames(unsigned mask);

// Writes one line: time, channel, thread and message. Does nothing if the channel is off.
void Write(Channel channel, const std::string& message);

// A URL with the values of secret-looking query parameters (key, token, secret, code,
// password) replaced by ***.
std::string RedactUrl(const std::string& url);

// A command line for display, with the value after any --accessToken or similar flag hidden.
std::string DescribeCommand(const std::vector<std::string>& args);

// A path for display, without the Windows long-path prefix.
std::string PathText(const std::string& path);

// A std::streambuf that passes everything to another one and also traces each finished line
// on the kLog channel. Wraps the app.log stream so the log shows in the console too.
class LogTee final : public std::streambuf {
public:
	explicit LogTee(std::streambuf* target) : m_target(target) {}

protected:
	int_type overflow(int_type ch) override;
	std::streamsize xsputn(const char* text, std::streamsize count) override;
	int sync() override;

private:
	void Feed(char ch);

	std::streambuf* m_target;
	std::string m_line;
};

}
