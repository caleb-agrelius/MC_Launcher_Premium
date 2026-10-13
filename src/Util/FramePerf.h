#pragma once

#include <chrono>

// Frame timing for the `perf` debug channel (docs/features/debug-harness.md): every 5 seconds it
// prints the frame rate and the average milliseconds each part of a frame took. With the channel
// off, a Scope costs one atomic load and the clock is never read. Call from the UI thread only.
namespace FramePerf {

enum Slot { kPoll, kUi, kMascot, kRender, kSwap, kSlotCount };

void Add(Slot slot, std::chrono::steady_clock::duration elapsed);

// Call once at the end of every frame; prints the summary when 5 seconds have passed.
void FrameDone();

// Adds the time between construction and destruction to a slot.
class Scope {
public:
	explicit Scope(Slot slot);
	~Scope();
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Slot m_slot;
	bool m_active;
	std::chrono::steady_clock::time_point m_start;
};

}
