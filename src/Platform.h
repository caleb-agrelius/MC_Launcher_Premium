#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Small OS helpers.
namespace Platform {

// Opens a folder in the system file browser (Explorer on Windows). Creates it first if
// it doesn't exist. Returns false if it couldn't be opened.
bool OpenFolder(const std::string& path);

// Opens a file with its default program (e.g. a .log in Notepad). Returns false if the
// file doesn't exist or couldn't be opened.
bool OpenFile(const std::string& path);

// Total physical memory in MB, or 0 if unknown.
std::uint64_t SystemMemoryMb();

// This PC's IPv4 address on the local network (e.g. "192.168.1.20"), or empty if unknown.
// Used to tell friends on the same network where to join a server.
std::string LocalNetworkAddress();

// One IPv4 address assigned to this PC, and the network adapter it belongs to.
struct NetworkAddress {
	std::string address;  // e.g. "192.168.1.20"
	std::string adapter;  // e.g. "Wi-Fi", "Tailscale"
};

// Every IPv4 address on adapters that are up, loopback excluded, in adapter order.
// Lets the player pick which address a server binds to (Servers > Host IP).
std::vector<NetworkAddress> LocalNetworkAddresses();

}
