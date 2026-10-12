#pragma once

#include <string>

class PackLibrary;
class ServerService;

// "Delete server?": confirms before deleting a pack's server folder, world included.
class DeleteServerDialog {
public:
	DeleteServerDialog(ServerService& servers, PackLibrary& library) : m_servers(servers), m_library(library) {}

	void Open(const std::string& packId) { m_packId = packId; }
	void Draw();

private:
	ServerService& m_servers;
	PackLibrary& m_library;
	std::string m_packId; // empty = closed
};
