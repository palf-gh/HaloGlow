/*
	HG_Kernels.h

	Kernel bodies shared by CUDA (HG_Kernel.cu), OpenCL (HG_Kernel.cl) and Metal
	(HG_Kernel.metal). Each language file only adds a thin entry point that maps
	its thread index to (x, y) and calls the body here, so the three backends
	run the same arithmetic in the same order as the CPU reference (HG_Core.cpp).

	Buffers are float4 arrays. Internal pyramid images store premultiplied RGBA
	in x/y/z/w. After Effects GPU worlds are BGRA128, premultiplied like the
	core, so loads and stores only swizzle and sanitise (spec §6.1).
	Requires HG_PixelMath.h first. The argument structs are also visible to
	host C++ (HG_GpuRender.cpp); the bodies compile only in GPU languages.
*/

#ifndef HG_KERNELS_H
#define HG_KERNELS_H

#define HG_LOCAL_SIZE 13
#define HG_LOCAL_RADIUS 6

// Rectangle of a buffer on its level lattice plus its row pitch in pixels.
typedef struct HG_Buf {
	int x0;
	int y0;
	int w;
	int h;
	int pitch;
	int pad0;
	int pad1;
	int pad2;
} HG_Buf;

typedef struct HG_DownArgs {
	HG_Buf src;
	HG_Buf dst;
	int halveX;
	int halveY;
	int srcIsWorld;		// 1: src is the AE input world (extract on load)
	int pad0;
	HG_PixelParams pixel;
} HG_DownArgs;

typedef struct HG_AccumArgs {
	HG_Buf d;			// D_j (may be empty)
	HG_Buf coarse;		// G_{j+1} (unused when hasCoarse == 0)
	HG_Buf dst;			// G_j
	float a;
	float c;
	int blurX;
	int blurY;
	int hasCoarse;
	int upX;
	int upY;
	int pad0;
} HG_AccumArgs;

typedef struct HG_FinalArgs {
	HG_Buf src;			// AE input world
	HG_Buf dst;			// AE output world
	HG_Buf g0;			// G_1 of pyramid 0
	HG_Buf g1;			// G_1 of pyramid 1
	int glowActive;
	int pyramidCount;
	int up0X;
	int up0Y;
	int up1X;
	int up1Y;
	int hasLocal;
	int pad0;
	float weight0;
	float weight1;
	float a0;
	float pad1;
	HG_PixelParams pixel;
	float localWeights[HG_LOCAL_SIZE * HG_LOCAL_SIZE];	// "local" is an OpenCL keyword
} HG_FinalArgs;

#if defined(__CUDACC__) || defined(__OPENCL_VERSION__) || defined(__METAL_VERSION__)

#if defined(__CUDACC__)
	#define HG_GLOBAL
	#define HG_PRIVATE
	#define HG_F4 float4
	#define HG_MAKE_F4(a, b, c, d) make_float4((a), (b), (c), (d))
#elif defined(__OPENCL_VERSION__)
	#define HG_GLOBAL __global
	#define HG_PRIVATE
	#define HG_F4 float4
	#define HG_MAKE_F4(a, b, c, d) ((float4)((a), (b), (c), (d)))
#elif defined(__METAL_VERSION__)
	#define HG_GLOBAL device
	#define HG_PRIVATE thread
	#define HG_F4 float4
	#define HG_MAKE_F4(a, b, c, d) float4((a), (b), (c), (d))
#endif

HG_FN HG_RGBA hg_rgba(float r, float g, float b, float a)
{
	HG_RGBA o;
	o.r = r;
	o.g = g;
	o.b = b;
	o.a = a;
	return o;
}

HG_FN void hg_mad(HG_PRIVATE HG_RGBA *acc, float w, HG_RGBA v)
{
	acc->r += w * v.r;
	acc->g += w * v.g;
	acc->b += w * v.b;
	acc->a += w * v.a;
}

HG_FN int hg_floor_div2(int v)
{
	return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

// Internal premultiplied RGBA buffer, zero outside its rectangle.
HG_FN HG_RGBA hg_load(HG_GLOBAL const HG_F4 *p, HG_Buf b, int x, int y)
{
	const int lx = x - b.x0;
	const int ly = y - b.y0;
	if (lx < 0 || ly < 0 || lx >= b.w || ly >= b.h) {
		return hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	const HG_F4 v = p[ly * b.pitch + lx];
	return hg_rgba(v.x, v.y, v.z, v.w);
}

// AE world pixel: premultiplied BGRA -> premultiplied RGBA (sanitised).
HG_FN HG_RGBA hg_load_world(HG_GLOBAL const HG_F4 *p, HG_Buf b, int x, int y)
{
	const int lx = x - b.x0;
	const int ly = y - b.y0;
	if (lx < 0 || ly < 0 || lx >= b.w || ly >= b.h) {
		return hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	const HG_F4 v = p[ly * b.pitch + lx];
	return hg_sanitise(hg_rgba(v.z, v.y, v.x, v.w));
}

HG_FN float hg_finite_clamp(float x)
{
	const float big = 3.402823466e38f;
	if (!(x == x)) return 0.0f;
	return x > big ? big : (x < -big ? -big : x);
}

HG_FN void hg_store(HG_GLOBAL HG_F4 *p, HG_Buf b, int x, int y, HG_RGBA v)
{
	p[(y - b.y0) * b.pitch + (x - b.x0)] = HG_MAKE_F4(v.r, v.g, v.b, v.a);
}

// Premultiplied RGBA -> AE premultiplied BGRA, finite by construction.
HG_FN void hg_store_world(HG_GLOBAL HG_F4 *p, HG_Buf b, int x, int y, HG_RGBA v)
{
	p[(y - b.y0) * b.pitch + (x - b.x0)] =
		HG_MAKE_F4(hg_finite_clamp(v.b), hg_finite_clamp(v.g), hg_finite_clamp(v.r), v.a);
}

HG_FN HG_RGBA hg_down_tap(HG_GLOBAL const HG_F4 *src, HG_DownArgs a, int x, int y)
{
	if (a.srcIsWorld) {
		return hg_extract(a.pixel, hg_load_world(src, a.src, x, y));
	}
	return hg_load(src, a.src, x, y);
}

// 13-tap downsample as the 6x6 texel table of HG_Core.cpp (weights /512),
// or its single-axis projection [1,3,4,4,3,1]/16.
HG_FN void hg_down_body(HG_GLOBAL const HG_F4 *src, HG_GLOBAL HG_F4 *dst, HG_DownArgs a, int lx, int ly)
{
	const int u = a.dst.x0 + lx;
	const int v = a.dst.y0 + ly;
	const float w1[6] = { 1.0f / 16.0f, 3.0f / 16.0f, 4.0f / 16.0f, 4.0f / 16.0f, 3.0f / 16.0f, 1.0f / 16.0f };
	const float w2[36] = {
		4.0f, 4.0f, 8.0f, 8.0f, 4.0f, 4.0f,
		4.0f, 20.0f, 24.0f, 24.0f, 20.0f, 4.0f,
		8.0f, 24.0f, 32.0f, 32.0f, 24.0f, 8.0f,
		8.0f, 24.0f, 32.0f, 32.0f, 24.0f, 8.0f,
		4.0f, 20.0f, 24.0f, 24.0f, 20.0f, 4.0f,
		4.0f, 4.0f, 8.0f, 8.0f, 4.0f, 4.0f };
	HG_RGBA acc = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (a.halveX && a.halveY) {
		for (int j = 0; j < 6; ++j) {
			for (int i = 0; i < 6; ++i) {
				hg_mad(&acc, w2[j * 6 + i] * (1.0f / 512.0f), hg_down_tap(src, a, 2 * u - 2 + i, 2 * v - 2 + j));
			}
		}
	} else if (a.halveX) {
		for (int i = 0; i < 6; ++i) {
			hg_mad(&acc, w1[i], hg_down_tap(src, a, 2 * u - 2 + i, v));
		}
	} else if (a.halveY) {
		for (int j = 0; j < 6; ++j) {
			hg_mad(&acc, w1[j], hg_down_tap(src, a, u, 2 * v - 2 + j));
		}
	} else {
		acc = hg_down_tap(src, a, u, v);
	}
	hg_store(dst, a.dst, u, v, acc);
}

// Bilinear tent upsample (§7.3): even fine x reads m-2..m+1 with
// [1,5,7,3]/16, odd reads m-1..m+2 with [3,7,5,1]/16; identity if not halved.
HG_FN HG_RGBA hg_upsample(HG_GLOBAL const HG_F4 *g, HG_Buf b, int upX, int upY, int x, int y)
{
	const float even[4] = { 1.0f / 16.0f, 5.0f / 16.0f, 7.0f / 16.0f, 3.0f / 16.0f };
	const float odd[4] = { 3.0f / 16.0f, 7.0f / 16.0f, 5.0f / 16.0f, 1.0f / 16.0f };
	float wx[4];
	float wy[4];
	int fx = x;
	int fy = y;
	int cx = 1;
	int cy = 1;
	wx[0] = 1.0f;
	wy[0] = 1.0f;
	if (upX) {
		const int m = hg_floor_div2(x);
		const int isOdd = x - 2 * m;
		fx = isOdd ? m - 1 : m - 2;
		for (int i = 0; i < 4; ++i) wx[i] = isOdd ? odd[i] : even[i];
		cx = 4;
	}
	if (upY) {
		const int m = hg_floor_div2(y);
		const int isOdd = y - 2 * m;
		fy = isOdd ? m - 1 : m - 2;
		for (int i = 0; i < 4; ++i) wy[i] = isOdd ? odd[i] : even[i];
		cy = 4;
	}
	HG_RGBA acc = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	for (int j = 0; j < cy; ++j) {
		HG_RGBA row = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		for (int i = 0; i < cx; ++i) {
			hg_mad(&row, wx[i], hg_load(g, b, fx + i, fy + j));
		}
		hg_mad(&acc, wy[j], row);
	}
	return acc;
}

HG_FN void hg_accum_body(HG_GLOBAL const HG_F4 *d, HG_GLOBAL const HG_F4 *coarse, HG_GLOBAL HG_F4 *dst,
						 HG_AccumArgs a, int lx, int ly)
{
	const int x = a.dst.x0 + lx;
	const int y = a.dst.y0 + ly;
	const float bin5[5] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };
	HG_RGBA acc = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (a.a > 0.0f) {
		hg_mad(&acc, a.a, hg_load(d, a.d, x, y));
	}
	if (a.c > 0.0f) {
		const int rx = a.blurX ? 2 : 0;
		const int ry = a.blurY ? 2 : 0;
		HG_RGBA blur = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		for (int j = -ry; j <= ry; ++j) {
			HG_RGBA row = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
			for (int i = -rx; i <= rx; ++i) {
				hg_mad(&row, a.blurX ? bin5[i + 2] : 1.0f, hg_load(d, a.d, x + i, y + j));
			}
			hg_mad(&blur, a.blurY ? bin5[j + 2] : 1.0f, row);
		}
		hg_mad(&acc, a.c, blur);
	}
	if (a.hasCoarse) {
		hg_mad(&acc, 1.0f, hg_upsample(coarse, a.coarse, a.upX, a.upY, x, y));
	}
	hg_store(dst, a.dst, x, y, acc);
}

HG_FN void hg_final_body(HG_GLOBAL const HG_F4 *src, HG_GLOBAL HG_F4 *dst, HG_GLOBAL const HG_F4 *g0,
						 HG_GLOBAL const HG_F4 *g1, HG_FinalArgs a, int lx, int ly)
{
	const int x = a.dst.x0 + lx;
	const int y = a.dst.y0 + ly;

	if (!a.glowActive) {
		// Bypass (§3.2): the input bit-exact at Source Opacity 100, otherwise
		// premultiplied RGB and alpha faded together; Glow Only is transparent.
		HG_F4 o = HG_MAKE_F4(0.0f, 0.0f, 0.0f, 0.0f);
		const int sx = x - a.src.x0;
		const int sy = y - a.src.y0;
		if (!a.pixel.glowOnly && sx >= 0 && sy >= 0 && sx < a.src.w && sy < a.src.h) {
			o = src[sy * a.src.pitch + sx];
			if (a.pixel.sourceOpacity != 1.0f) {
				const float q = a.pixel.sourceOpacity;
				o = HG_MAKE_F4(o.x * q, o.y * q, o.z * q, o.w * q);
			}
		}
		dst[ly * a.dst.pitch + lx] = o;
		return;
	}

	HG_RGBA b = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (a.hasLocal) {
		for (int j = 0; j < HG_LOCAL_SIZE; ++j) {
			HG_RGBA row = hg_rgba(0.0f, 0.0f, 0.0f, 0.0f);
			for (int i = 0; i < HG_LOCAL_SIZE; ++i) {
				const float w = a.localWeights[j * HG_LOCAL_SIZE + i];
				if (w != 0.0f) {
					hg_mad(&row, w, hg_extract(a.pixel, hg_load_world(src, a.src, x + i - HG_LOCAL_RADIUS,
																	   y + j - HG_LOCAL_RADIUS)));
				}
			}
			hg_mad(&b, 1.0f, row);
		}
	} else if (a.a0 > 0.0f) {
		hg_mad(&b, a.a0, hg_extract(a.pixel, hg_load_world(src, a.src, x, y)));
	}
	if (a.pyramidCount > 0) {
		hg_mad(&b, a.weight0, hg_upsample(g0, a.g0, a.up0X, a.up0Y, x, y));
	}
	if (a.pyramidCount > 1) {
		hg_mad(&b, a.weight1, hg_upsample(g1, a.g1, a.up1X, a.up1Y, x, y));
	}
	const HG_RGBA glow = hg_glow_from_filtered(a.pixel, b);
	const HG_RGBA out = hg_composite(a.pixel, hg_load_world(src, a.src, x, y), glow);
	hg_store_world(dst, a.dst, x, y, out);
}

#endif // GPU languages

#endif // HG_KERNELS_H
