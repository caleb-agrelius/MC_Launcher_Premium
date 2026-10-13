#pragma once

#include <string>

// Short notifications in the bottom-right corner, e.g. "Download complete".
// UI thread only; background threads should post through UiQueue.
namespace Toasts {

enum class Kind { Info, Success, Error };

void Push(Kind kind, std::string title, std::string body = {});

// True while any toast is showing (they slide and fade, so the frame rate must stay up).
bool Visible();

// Draws the visible toasts and drops expired ones. Call once per frame, after the main window.
void Draw(bool reduceMotion);

}
