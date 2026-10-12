#pragma once

#include "Core/Notifier.h"
#include "Core/TaskRunner.h"
#include "Network/IHttpClient.h"

#include <atomic>
#include <ostream>

// The infrastructure every service is built on, handed in once by whoever creates the services
// (the application, or a test with fakes). Each member is an abstraction, so a service never
// names the thread pool, the UI queue, libcurl or the toast widget.
//   tasks          where slow work runs (off the UI thread)
//   ui             where its results are delivered (on the UI thread)
//   http           the network
//   notify         messages for the player
//   log            app.log; write to it from the UI thread only
//   shuttingDown   set when the app is closing: long tasks should stop and not report
struct Runtime {
	ITaskRunner& tasks;
	IUiPoster& ui;
	IHttpClient& http;
	INotifier& notify;
	std::ostream& log;
	const std::atomic<bool>& shuttingDown;
};
