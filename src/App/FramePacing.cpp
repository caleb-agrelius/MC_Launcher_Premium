#include "App/FramePacing.h"

#include <algorithm>

namespace FramePacing {

Pace Choose(const Input& input)
{
	const double requested = input.maxFps > 0 ? 1.0 / input.maxFps : 0.0;
	// A minimized window has nothing to show, and its swap returns at once, so an unpaced loop
	// would run flat out.
	if (input.minimized)
		return { std::max(requested, 1.0 / kBackgroundFps), true };
	if (!input.lowerWhenIdle)
		return { requested, false };
	if (!input.focused)
		return { std::max(requested, 1.0 / kBackgroundFps), true };
	if (input.idleSeconds >= kActiveSeconds)
		return { std::max(requested, 1.0 / kIdleFps), true };
	return { requested, false };
}

}
