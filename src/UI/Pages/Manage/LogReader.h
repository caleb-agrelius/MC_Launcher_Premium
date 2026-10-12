#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Reading the game's log files for the Logs tab.
namespace LogReader {

// Reads a log file, unzipping .gz logs. Very long logs keep their last 24 MB. False, with err
// set, when the file can't be read.
bool ReadText(const std::filesystem::path& path, std::string& out, std::string* err);

// Splits text into lines, dropping carriage returns and expanding tabs (ImGui fonts don't draw them).
std::vector<std::string> SplitLines(const std::string& text);

}
