#pragma once

#include "UI/Navigation.h"
#include "UI/Pages/IPage.h"

class DownloadService;
class ImageCache;
struct DownloadState;

// Downloads: a card per file or whole-pack install with its progress, and an "Install files"
// button for finished downloads that were never extracted.
class DownloadsPage final : public IPage {
public:
	DownloadsPage(DownloadService& downloads, ImageCache& images, INavigator& navigator)
		: m_downloads(downloads), m_images(images), m_navigator(navigator) {}

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;

private:
	void DrawCard(const std::string& key, const DownloadState& state);

	DownloadService& m_downloads;
	ImageCache& m_images;
	INavigator& m_navigator;
};
