#pragma once

#include "UI/Pages/Servers/ServerView.h"

class ServerService;

// Docker's state, the last Docker action's result, and what's needed to use it. Shown on the
// Run and Docker tabs.
void DrawDockerStatus(const ServerTabContext& ctx, ServerService& servers);
