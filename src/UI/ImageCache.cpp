#include "ImageCache.h"

#include "Network/HttpClient.h"

#include <glad/glad.h>
#include <webp/decode.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <algorithm>
#include <cstring>

// Larger images (raw gallery uploads can be 4K+) are downscaled before upload.
static constexpr int kMaxTextureSide = 2048;

static bool IsWebP(const std::string& data)
{
	return data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 &&
		std::memcmp(data.data() + 8, "WEBP", 4) == 0;
}

// Decodes data to 8-bit RGBA, downscaling so neither side exceeds kMaxTextureSide.
static bool DecodeImage(const std::string& data, int& width, int& height, std::vector<std::uint8_t>& rgba)
{
	const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
	std::uint8_t* pixels = nullptr;
	bool fromWebP = false;
	if (IsWebP(data)) {
		pixels = WebPDecodeRGBA(bytes, data.size(), &width, &height);
		fromWebP = true;
	} else {
		int channels = 0;
		pixels = stbi_load_from_memory(bytes, static_cast<int>(data.size()), &width, &height, &channels, 4);
	}
	if (!pixels || width <= 0 || height <= 0)
		return false;

	const int largest = width > height ? width : height;
	if (largest > kMaxTextureSide) {
		const int newWidth = std::max(1, static_cast<int>(static_cast<long long>(width) * kMaxTextureSide / largest));
		const int newHeight = std::max(1, static_cast<int>(static_cast<long long>(height) * kMaxTextureSide / largest));
		rgba.resize(static_cast<std::size_t>(newWidth) * newHeight * 4);
		stbir_resize_uint8_srgb(pixels, width, height, 0, rgba.data(), newWidth, newHeight, 0, STBIR_RGBA);
		width = newWidth;
		height = newHeight;
	} else {
		rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
	}

	if (fromWebP)
		WebPFree(pixels);
	else
		stbi_image_free(pixels);
	return true;
}

static ImTextureID UploadTexture(const std::uint8_t* rgba, int width, int height)
{
	GLint previous = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);

	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	// Mipmaps keep icons smooth when drawn smaller than their source size.
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
	return static_cast<ImTextureID>(texture);
}

ImageCache::ImageCache(HttpClient& http, int threadCount)
	: m_http(http)
{
	for (int i = 0; i < threadCount; ++i)
		m_threads.emplace_back([this]() { WorkerLoop(); });
}

ImageCache::~ImageCache()
{
	Shutdown();
}

const LoadedImage* ImageCache::Get(const std::string& url)
{
	if (url.empty() || m_stopping.load())
		return nullptr;
	const auto it = m_entries.find(url);
	if (it != m_entries.end())
		return it->second.status == Status::Ready ? &it->second.image : nullptr;

	m_entries[url] = Entry{};
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push_back(url);
	}
	m_wake.notify_one();
	return nullptr;
}

void ImageCache::Update(double now)
{
	std::vector<Decoded> decoded;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		decoded.swap(m_decoded);
	}
	for (Decoded& item : decoded) {
		Entry& entry = m_entries[item.url];
		if (item.rgba.empty()) {
			entry.status = Status::Failed;
			continue;
		}
		entry.image.texture = UploadTexture(item.rgba.data(), item.width, item.height);
		entry.image.width = item.width;
		entry.image.height = item.height;
		entry.image.readyTime = now;
		entry.status = Status::Ready;
	}
}

void ImageCache::Shutdown()
{
	if (m_stopping.exchange(true))
		return;
	m_wake.notify_all();
	for (std::thread& thread : m_threads) {
		if (thread.joinable())
			thread.join();
	}
	m_threads.clear();

	for (auto& [url, entry] : m_entries) {
		if (entry.status == Status::Ready) {
			const GLuint texture = static_cast<GLuint>(entry.image.texture);
			glDeleteTextures(1, &texture);
		}
	}
	m_entries.clear();
}

void ImageCache::WorkerLoop()
{
	for (;;) {
		std::string url;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wake.wait(lock, [this]() { return m_stopping.load() || !m_queue.empty(); });
			if (m_stopping.load())
				return;
			url = std::move(m_queue.front());
			m_queue.pop_front();
		}

		Decoded result;
		result.url = url;
		std::string body;
		if (m_http.Get(url, body, nullptr, &m_stopping)) {
			if (!DecodeImage(body, result.width, result.height, result.rgba))
				result.rgba.clear();
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		m_decoded.push_back(std::move(result));
	}
}
