/*
	HG_Parallel.h

	Row-band parallelism inside one CPU render. Every band writes a disjoint
	set of rows, so the result is bit-identical to a serial sweep and the render
	stays safe under After Effects Multi-Frame Rendering (no shared state).
*/

#pragma once
#ifndef HG_PARALLEL_H
#define HG_PARALLEL_H

#include <algorithm>
#include <thread>
#include <vector>

namespace hg {

template <typename Fn>
void ParallelRows(int y0, int y1, const Fn &fn)
{
	const int rows = y1 - y0;
	if (rows <= 0) return;
	const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
	const int bands = static_cast<int>(std::min<unsigned>(hw, static_cast<unsigned>((rows + 15) / 16)));
	if (bands <= 1) {
		for (int y = y0; y < y1; ++y) fn(y);
		return;
	}
	std::vector<std::thread> workers;
	workers.reserve(bands - 1);
	const int step = (rows + bands - 1) / bands;
	for (int b = 1; b < bands; ++b) {
		const int lo = y0 + b * step;
		const int hi = std::min(y1, lo + step);
		if (lo >= hi) break;
		workers.emplace_back([lo, hi, &fn]() {
			for (int y = lo; y < hi; ++y) fn(y);
		});
	}
	for (int y = y0; y < std::min(y1, y0 + step); ++y) fn(y);
	for (std::thread &t : workers) t.join();
}

} // namespace hg

#endif // HG_PARALLEL_H
