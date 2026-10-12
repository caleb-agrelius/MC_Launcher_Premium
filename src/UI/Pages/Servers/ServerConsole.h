#pragma once

#include "UI/Pages/Servers/ServerView.h"

// The running server's console: its output, and a line to send commands.
class ServerConsole {
public:
	void Draw(const ServerTabContext& ctx);
};
