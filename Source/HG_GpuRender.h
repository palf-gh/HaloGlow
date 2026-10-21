/*
	HG_GpuRender.h

	Backend-independent GPU driver. It walks the same hg::Plan as the CPU
	reference and issues one kernel per pyramid stage; CUDA, OpenCL and Metal
	only implement the Backend interface (allocation and three launches). No
	After Effects headers, so the CUDA parity test can drive it directly.
*/

#pragma once
#ifndef HG_GPU_RENDER_H
#define HG_GPU_RENDER_H

#include "HG_Core.h"
#include "../GPU/HG_Kernels.h"

#include <cstddef>

namespace hg {
namespace gpu {

class Backend {
public:
	virtual ~Backend() = default;
	// Device buffer of at least 'bytes' (> 0). Null on failure.
	virtual void *Allocate(std::size_t bytes) = 0;
	virtual void Free(void *mem) = 0;
	// Launches over args.dst (w x h threads). False on a launch error.
	virtual bool Down(const void *src, void *dst, const HG_DownArgs &args) = 0;
	virtual bool Accum(const void *d, const void *coarse, void *dst, const HG_AccumArgs &args) = 0;
	virtual bool Final(const void *src, void *dst, const void *g0, const void *g1, const HG_FinalArgs &args) = 0;
	// Blocks until every launch issued so far has completed. Render calls it
	// before releasing any buffer, so no allocation is freed while in use.
	virtual bool Finish() = 0;
};

// A host GPU world: BGRA128 straight alpha, origin in layer coordinates.
struct World {
	void *mem = nullptr;
	int x0 = 0;
	int y0 = 0;
	int width = 0;
	int height = 0;
	int pitch = 0;		// pixels per row
};

enum class Status { Ok, OutOfMemory, LaunchFailed };

// Renders plan.outputRect into dst. 'src' may have no memory (empty input).
Status Render(const Plan &plan, Backend &backend, const World &src, const World &dst);

} // namespace gpu
} // namespace hg

#endif // HG_GPU_RENDER_H
