/*
	HG_ColourCache.h

	Best-effort Luminance Space "Auto" cache (spec §3.12.1, §3.12.2).

	Only main-thread UI commands refresh it; render selectors only read. The
	cache is a process-wide immutable snapshot swapped atomically, so concurrent
	Multi-Frame Rendering threads never see a half-written value and never
	serialise on a lock. A stale snapshot is possible by design.
*/

#pragma once
#ifndef HG_COLOUR_CACHE_H
#define HG_COLOUR_CACHE_H

#include "HG_Core.h"

#include <cstdint>
#include <memory>
#include <string>

namespace hg {

enum class AutoState : int {
	Unavailable = 0,	// never resolved, or not a known primaries set
	Cached = 1,			// resolved before; may be stale now
	Verified = 2,		// resolved by the latest main-thread refresh
};

struct ColourSnapshot {
	AutoState state = AutoState::Unavailable;
	LuminanceSpace space = LuminanceSpace::Rec709;	// Rec709 is the compat fallback
	float luminance[3] = { 0.2126390f, 0.7151687f, 0.0721923f };
	std::uint64_t generation = 0;
};

// Snapshot for one render; take it once at render start.
std::shared_ptr<const ColourSnapshot> CurrentColourSnapshot();

// Main thread only. 'name' is an ICC description or OCIO colour-space name.
// Known primaries -> Verified; an empty name (nothing readable) keeps the
// previous value as Cached; unknown primaries -> Unavailable (Rec.709 fallback).
void PublishWorkingSpaceName(const std::string &name);

// Name -> primaries (ASCII, case-insensitive). False when not recognised.
bool MatchWorkingSpaceName(const std::string &name, LuminanceSpace &space);

} // namespace hg

#endif // HG_COLOUR_CACHE_H
