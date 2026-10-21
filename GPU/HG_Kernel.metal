/*
	HG_Kernel.metal

	Metal Shading Language entry points. The build prepends HG_PixelMath.h and
	HG_Kernels.h (cmake/embed_kernel.py); all arithmetic lives there. The
	library is compiled at device setup with fast math disabled (§12.5).
*/

#include "HG_PixelMath.h"
#include "HG_Kernels.h"

kernel void hg_down(device const float4 *src [[buffer(0)]], device float4 *dst [[buffer(1)]],
					constant HG_DownArgs &args [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
	const HG_DownArgs a = args;
	if ((int)gid.x >= a.dst.w || (int)gid.y >= a.dst.h) return;
	hg_down_body(src, dst, a, (int)gid.x, (int)gid.y);
}

kernel void hg_accum(device const float4 *d [[buffer(0)]], device const float4 *coarse [[buffer(1)]],
					 device float4 *dst [[buffer(2)]], constant HG_AccumArgs &args [[buffer(3)]],
					 uint2 gid [[thread_position_in_grid]])
{
	const HG_AccumArgs a = args;
	if ((int)gid.x >= a.dst.w || (int)gid.y >= a.dst.h) return;
	hg_accum_body(d, coarse, dst, a, (int)gid.x, (int)gid.y);
}

kernel void hg_final(device const float4 *src [[buffer(0)]], device float4 *dst [[buffer(1)]],
					 device const float4 *g0 [[buffer(2)]], device const float4 *g1 [[buffer(3)]],
					 constant HG_FinalArgs &args [[buffer(4)]], uint2 gid [[thread_position_in_grid]])
{
	const HG_FinalArgs a = args;
	if ((int)gid.x >= a.dst.w || (int)gid.y >= a.dst.h) return;
	hg_final_body(src, dst, g0, g1, a, (int)gid.x, (int)gid.y);
}
