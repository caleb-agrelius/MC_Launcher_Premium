#pragma once

#include <cstdint>
#include <string>

// Small OS helpers.
namespace Platform {

// Opens a folder in the system file browser (Explorer on Windows). Creates it first if
// it doesn't exist. Returns false if it couldn't be opened.
bool OpenFolder(const std::string& path);

// Total physical memory in MB, or 0 if unknown.
std::uint64_t SystemMemoryMb();

// This PC's IPv4 address on the local network (e.g. "192.168.1.20"), or empty if unknown.
// Used to tell friends on the same network where to join a server.
std::string LocalNetworkAddress();

}
