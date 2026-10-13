#include "Util/FramePerf.h"

#include "Util/DebugTrace.h"

#include <array>
#include <cstdio>
#include <string>

namespace FramePerf {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds kReportEvery{ 5 };

std::array<Clock::duration, kSlotCount> g_totals{};
Clock::time_point g_windowStart{};
long g_frames = 0;

double Milliseconds(Clock::duration value)
{
	return std::chrono::duration<double, std::milli>(value).count();
}

}

void Add(Slot slot, Clock::duration elapsed)
{
	g_totals[slot] += elapsed;
}

void FrameDone()
{
	if (!Debug::Enabled(Debug::kPerf))
		return;
	const Clock::time_point now = Clock::now();
	if (g_frames == 0 && g_windowStart == Clock::time_point{}) {
		g_windowStart = now;
		return;
	}
	++g_frames;
	const Clock::duration window = now - g_windowStart;
	if (window < kReportEvery)
		return;
	const double seconds = std::chrono::duration<double>(window).count();
	const double frames = static_cast<double>(g_frames);
	char line[192];
	std::snprintf(line, sizeof line,
		"%.1f fps | ms per frame: poll %.3f, ui %.3f (mascot %.3f), render %.3f, swap %.3f, total %.3f",
		frames / seconds, Milliseconds(g_totals[kPoll]) / frames, Milliseconds(g_totals[kUi]) / frames,
		Milliseconds(g_totals[kMascot]) / frames, Milliseconds(g_totals[kRender]) / frames,
		Milliseconds(g_totals[kSwap]) / frames, Milliseconds(window) / frames);
	Debug::Write(Debug::kPerf, line);
	g_totals.fill(Clock::duration::zero());
	g_frames = 0;
	g_windowStart = now;
}

Scope::Scope(Slot slot)
	: m_slot(slot), m_active(Debug::Enabled(Debug::kPerf))
{
	if (m_active)
		m_start = Clock::now();
}

Scope::~Scope()
{
	if (m_active)
		Add(m_slot, Clock::now() - m_start);
}

}
