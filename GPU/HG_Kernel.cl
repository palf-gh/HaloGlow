/*
	HG_Kernel.cl

	OpenCL C 1.2 entry points. The build prepends HG_PixelMath.h and
	HG_Kernels.h (cmake/embed_kernel.py); all arithmetic lives there. The
	program is compiled without -cl-fast-relaxed-math or -cl-mad-enable (§12.5).
*/

#include "HG_PixelMath.h"
#include "HG_Kernels.h"

__kernel void hg_down(__global const float4 *src, __global float4 *dst, HG_DownArgs a)
{
	const int x = (int)get_global_id(0);
	const int y = (int)get_global_id(1);
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_down_body(src, dst, a, x, y);
}

__kernel void hg_accum(__global const float4 *d, __global const float4 *coarse, __global float4 *dst,
					   HG_AccumArgs a)
{
	const int x = (int)get_global_id(0);
	const int y = (int)get_global_id(1);
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_accum_body(d, coarse, dst, a, x, y);
}

__kernel void hg_final(__global const float4 *src, __global float4 *dst, __global const float4 *g0,
					   __global const float4 *g1, HG_FinalArgs a)
{
	const int x = (int)get_global_id(0);
	const int y = (int)get_global_id(1);
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_final_body(src, dst, g0, g1, a, x, y);
}
