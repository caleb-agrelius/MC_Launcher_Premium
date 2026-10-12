#pragma once

#include "Core/Notifier.h"
#include "UI/Toasts.h"

// Shows the services' notices as toasts.
class ToastNotifier final : public INotifier {
public:
	void Notify(NoticeKind kind, std::string title, std::string body = {}) override
	{
		const Toasts::Kind toastKind = kind == NoticeKind::Success ? Toasts::Kind::Success
			: kind == NoticeKind::Error ? Toasts::Kind::Error : Toasts::Kind::Info;
		Toasts::Push(toastKind, std::move(title), std::move(body));
	}
};
