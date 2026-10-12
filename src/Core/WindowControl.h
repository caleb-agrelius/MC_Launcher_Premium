#pragma once

// What services may ask of the launcher's window. Implemented by the app's window; services
// (e.g. the Play flow's "when the game starts" setting) never touch the windowing library.
class IWindowControl {
public:
	virtual ~IWindowControl() = default;
	virtual void Minimize() = 0;
	virtual void Restore() = 0;
	virtual void RequestClose() = 0;
};

// How big the launcher's window content is drawn on this monitor, as a multiplier (1.0 = 96 dpi).
// Pages that show it (Settings) ask here instead of the windowing library.
class IDisplayMetrics {
public:
	virtual ~IDisplayMetrics() = default;
	virtual float DpiScale() const = 0;
};
