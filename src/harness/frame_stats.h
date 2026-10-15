#ifndef HARNESS_FRAME_STATS_H
#define HARNESS_FRAME_STATS_H

#include "include/harness/hooks.h"

// Wraps the platform swap function and logs frame timing statistics every `pInterval_seconds`.
// Measures on the CPU side: "frame" is the time between two swaps, "work" the part of it spent
// outside of the swap call (game logic and issuing draw calls, without vsync or FPS limiter waits).
void FrameStats_Install(tHarness_platform* pPlatform, int pInterval_seconds);

#endif
