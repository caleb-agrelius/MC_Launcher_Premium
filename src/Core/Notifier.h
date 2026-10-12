#pragma once

#include <string>

// Short messages for the player ("Server deleted", "Download failed"). Services report through
// this; the app decides how they appear (toasts today).
enum class NoticeKind { Info, Success, Error };

class INotifier {
public:
	virtual ~INotifier() = default;
	virtual void Notify(NoticeKind kind, std::string title, std::string body = {}) = 0;
};
