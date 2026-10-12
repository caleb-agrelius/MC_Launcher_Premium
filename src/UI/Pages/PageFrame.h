#pragma once

#include <filesystem>
#include <vector>

// What the shell tells a page each frame.
struct PageFrame {
	double shownAt = 0.0; // when the page was opened (entrance animation)
	const std::vector<std::filesystem::path>* droppedPaths = nullptr; // files dropped on the window this frame
};
