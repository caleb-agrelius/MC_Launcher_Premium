#pragma once

#include "imgui.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class HttpClient;

// A decoded image uploaded as an OpenGL texture.
struct LoadedImage {
	ImTextureID texture = ImTextureID_Invalid;
	int width = 0;
	int height = 0;
	double readyTime = 0.0; // ImGui time when it became available, for fade-ins
};

// Downloads and decodes images (PNG, JPEG, WebP, ...) by URL on a small pool of
// background threads, then uploads them to OpenGL on the UI thread.
// Images are cached in memory for the life of the app.
// All methods except the worker threads' internals must be called on the UI (GL) thread.
class ImageCache {
public:
	explicit ImageCache(HttpClient& http, int threadCount = 4);
	~ImageCache();

	ImageCache(const ImageCache&) = delete;
	ImageCache& operator=(const ImageCache&) = delete;

	// Returns the image if it is ready, else nullptr. The first call for a URL queues a
	// download; failed URLs are not retried. Empty URLs always return nullptr.
	const LoadedImage* Get(const std::string& url);

	// Uploads images decoded since the last call. Call once per frame, before drawing.
	void Update(double now);

	// Stops the worker threads and frees all textures. Call before the GL context is destroyed.
	void Shutdown();

private:
	enum class Status { Queued, Ready, Failed };
	struct Entry {
		Status status = Status::Queued;
		LoadedImage image;
	};
	struct Decoded {
		std::string url;
		int width = 0;
		int height = 0;
		std::vector<std::uint8_t> rgba; // empty on failure
	};

	void WorkerLoop();

	HttpClient& m_http;
	std::map<std::string, Entry> m_entries; // UI thread only

	std::mutex m_mutex; // guards m_queue, m_decoded
	std::condition_variable m_wake;
	std::deque<std::string> m_queue;
	std::vector<Decoded> m_decoded;
	std::atomic<bool> m_stopping{ false };
	std::vector<std::thread> m_threads;
};
