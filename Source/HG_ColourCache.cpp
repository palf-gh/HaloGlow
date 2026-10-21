/*
	HG_ColourCache.cpp

	See HG_ColourCache.h. std::atomic_load / atomic_store on shared_ptr keep
	every published snapshot alive for as long as a render holds it.
*/

#include "HG_ColourCache.h"

#include <algorithm>
#include <atomic>
#include <cctype>

namespace hg {
namespace {

std::shared_ptr<const ColourSnapshot> &Slot()
{
	static std::shared_ptr<const ColourSnapshot> slot = std::make_shared<const ColourSnapshot>();
	return slot;
}

std::string Lower(const std::string &s)
{
	std::string o(s);
	std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return o;
}

bool Contains(const std::string &hay, const char *needle)
{
	return hay.find(needle) != std::string::npos;
}

} // namespace

std::shared_ptr<const ColourSnapshot> CurrentColourSnapshot()
{
	return std::atomic_load(&Slot());
}

bool MatchWorkingSpaceName(const std::string &name, LuminanceSpace &space)
{
	const std::string n = Lower(name);
	if (n.empty()) return false;
	// Primaries that are not one of the three supported sets stay unknown;
	// guessing would apply the wrong luminance weights silently.
	if (Contains(n, "2020") || Contains(n, "2100") || Contains(n, "aces2065") || Contains(n, "ap0") ||
		Contains(n, "prophoto") || Contains(n, "adobe rgb") || Contains(n, "dci")) {
		return false;
	}
	if (Contains(n, "acescg") || Contains(n, "aces cg") || Contains(n, "acescct") ||
		Contains(n, "acescc") || Contains(n, "ap1")) {
		space = LuminanceSpace::ACEScg;
		return true;
	}
	if (Contains(n, "p3")) {
		space = LuminanceSpace::DisplayP3;
		return true;
	}
	if (Contains(n, "srgb") || Contains(n, "709") || Contains(n, "hdtv")) {
		space = LuminanceSpace::Rec709;
		return true;
	}
	return false;
}

void PublishWorkingSpaceName(const std::string &name)
{
	const std::shared_ptr<const ColourSnapshot> prev = CurrentColourSnapshot();
	auto next = std::make_shared<ColourSnapshot>(*prev);
	next->generation = prev->generation + 1;
	LuminanceSpace space;
	if (MatchWorkingSpaceName(name, space)) {
		next->state = AutoState::Verified;
		next->space = space;
		LuminanceCoefficients(space, next->luminance);
	} else if (name.empty() && prev->state != AutoState::Unavailable) {
		// Nothing could be read this time: keep the last value, flagged stale.
		next->state = AutoState::Cached;
	} else {
		// Unknown primaries (or never resolved): explicit Rec.709 fallback.
		next->state = AutoState::Unavailable;
		next->space = LuminanceSpace::Rec709;
		LuminanceCoefficients(LuminanceSpace::Rec709, next->luminance);
	}
	std::atomic_store(&Slot(), std::shared_ptr<const ColourSnapshot>(std::move(next)));
}

} // namespace hg
