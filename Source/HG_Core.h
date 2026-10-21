/*
	HG_Core.h

	Host-independent HaloGlow core: user settings, the render plan (level
	schedule, per-level domains, coefficients, local kernel) and the FP32 CPU
	reference pyramid. No After Effects headers are included here, so the core
	builds into the test and calibration executables unchanged.

	Coordinates. Every image lives on the layer's integer pixel lattice at
	processing resolution. Level j of a pyramid has been halved n_x(j) times in x
	and n_y(j) times in y; its pixel u has its centre at layer index
	(u + 0.5) * 2^n - 0.5 (spec §7.2). Rectangles are half-open [x0,x1)x[y0,y1)
	in the level's own index space. Integer division always floors, so negative
	origins keep the same phase as positive ones.
*/

#pragma once
#ifndef HG_CORE_H
#define HG_CORE_H

#include "../GPU/HG_PixelMath.h"

#include <cstdint>
#include <vector>

namespace hg {

constexpr int kMaxLevels = 16;		// pyramid levels 1..kMaxLevels (level 0 = E)
constexpr int kLocalRadius = 6;		// local FIR is (2r+1)^2 = 13x13 (§7.4)
constexpr int kLocalSize = 2 * kLocalRadius + 1;
constexpr int kLocalBasisCount = 4;	// sigma 0.5 / 0.75 / 1.0 / 1.5 px
constexpr int kAlgorithmVersion = 1;	// provisional calibration, spec §17

//-----------------------------------------------------------------------------
// Geometry helpers
//-----------------------------------------------------------------------------
struct Rect {
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;

	bool Empty() const { return x1 <= x0 || y1 <= y0; }
	int Width() const { return x1 > x0 ? x1 - x0 : 0; }
	int Height() const { return y1 > y0 ? y1 - y0 : 0; }
	bool operator==(const Rect &o) const
	{
		return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1;
	}
};

Rect Union(const Rect &a, const Rect &b);
Rect Intersect(const Rect &a, const Rect &b);
Rect Inflate(const Rect &r, int dx, int dy);

inline int FloorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }
inline int CeilDiv2(int v) { return -FloorDiv2(-v); }

//-----------------------------------------------------------------------------
// User settings after validation (§3). Units are the UI units.
//-----------------------------------------------------------------------------
enum class LuminanceSpace : int {
	Auto = 1,
	Rec709 = 2,
	DisplayP3 = 3,
	ACEScg = 4,
};

struct Settings {
	float intensity = 1.0f;			// 0..8
	float size = 64.0f;				// 0..512 full-resolution square px
	float threshold = 25.0f;		// 0..1600 %
	float softness = 50.0f;			// 0..100 %
	float width = 100.0f;			// 0..400 %
	float height = 100.0f;			// 0..400 %
	float falloff = 50.0f;			// 0..100
	float gamma = 1.0f;				// 0.1..3
	float saturation = 100.0f;		// 0..200 %
	float colourStrength = 0.0f;	// 0..100 %
	float colour1[3] = { 1.0f, 0.894117647f, 0.768627451f };
	float colour2[3] = { 1.0f, 1.0f, 1.0f };
	float glowOpacity = 100.0f;		// 0..100 %
	float sourceOpacity = 100.0f;	// 0..100 %
	bool glowOnly = false;
	float luminance[3] = { 0.2126390f, 0.7151687f, 0.0721923f };
};

// Clamp finite values to the public ranges, replace NaN/Inf by the default.
Settings Sanitise(const Settings &in);

// Luminance coefficients of a manual Luminance Space (§3.12), normalised.
void LuminanceCoefficients(LuminanceSpace space, float out[3]);

// Host scale of the processing image relative to full resolution.
struct RenderScale {
	double downsampleX = 1.0;	// e.g. 0.5 at half-resolution preview
	double downsampleY = 1.0;
	double pixelAspect = 1.0;	// pixel width / height
};

//-----------------------------------------------------------------------------
// Coefficients (§7.3, §7.4)
//-----------------------------------------------------------------------------
struct Coefficients {
	float a[kMaxLevels + 1] = {};		// a[0] multiplies E, a[j] level j
	float c[kMaxLevels + 1] = {};		// c[j] multiplies Bin5(D_j); c[0] unused
	float b[kLocalBasisCount] = {};		// local FIR basis weights
	int topLevel = 0;					// highest j with a[j] or c[j] > 0
};

// Interpolates the built-in calibration table (HG_CoefficientTable_v1.inc)
// at log2(R90 px) and Falloff, then renormalises the sum to one.
Coefficients LookupCoefficients(double log2Radius, double falloff);

// Sigmas (px) of the local FIR bases on the calibration (major) axis.
extern const float kLocalBasisSigma[kLocalBasisCount];

// Pixel-area-integrated Gaussian on [-r, r], normalised; sigma 0 -> delta.
void IntegratedGaussian(double sigma, float out[kLocalSize]);

//-----------------------------------------------------------------------------
// Render plan
//-----------------------------------------------------------------------------
struct LevelPlan {
	bool halveX = false;	// this level halves x relative to level j-1
	bool halveY = false;
	bool blurX = false;		// Bin5 companion runs along x (x halved by now)
	bool blurY = false;
	Rect dDomain;			// where D_j may be non-zero
	Rect gDomain;			// where G_j is evaluated (output-driven, clipped)
};

struct PyramidPlan {
	float weight = 0.0f;					// blend weight (anisotropy, §7.2)
	int delayX = 0;							// Delta_x of the schedule
	int delayY = 0;
	int levelCount = 0;						// levels 1..levelCount are used
	LevelPlan level[kMaxLevels + 1];		// index 0 unused except domains
};

struct Plan {
	bool glowActive = false;		// false: Source Opacity only (§3.2, §3.3)
	HG_PixelParams pixel {};
	Coefficients coefficients;
	float localKernel[kLocalSize * kLocalSize] = {};	// W = a0 d + sum b_h H_h
	bool hasLocalSpread = false;	// any b_h > 0 (else W is a0 * delta)
	int pyramidCount = 0;
	PyramidPlan pyramid[2];
	Rect inputRect;					// E support (true input bounds)
	Rect outputRect;				// requested output
	Rect glowRect;					// where the glow may be non-zero at level 0
	double radiusX = 0.0;			// R90 along x in processing px
	double radiusY = 0.0;
};

// Builds the whole plan: schedule, domains, coefficients and pixel parameters.
// inputRect is the true input bounds; outputRect the requested output.
Plan BuildPlan(const Settings &settings, const RenderScale &scale,
			   const Rect &inputRect, const Rect &outputRect);

// Same plan with explicit coefficients (calibration and impulse tools).
Plan BuildPlanWithCoefficients(const Settings &settings, const RenderScale &scale,
							   const Rect &inputRect, const Rect &outputRect,
							   const Coefficients &coefficients);

// Pre-render helpers (§7.6, §9.1). Both walk the same tap graph as the render.
// MaxResultRect: union of the source rect and every pixel the glow can touch.
Rect MaxResultRect(const Settings &settings, const RenderScale &scale,
				   const Rect &inputRect);
// RequiredInputRect: every input pixel that can affect outputRect.
Rect RequiredInputRect(const Settings &settings, const RenderScale &scale,
					   const Rect &outputRect);

//-----------------------------------------------------------------------------
// CPU images: premultiplied RGBA FP32 with a layer-space origin.
//-----------------------------------------------------------------------------
struct Image {
	Rect rect;
	std::vector<HG_RGBA> pixels;

	void Reset(const Rect &r);
	HG_RGBA At(int x, int y) const;		// zero outside rect
	HG_RGBA *Row(int y) { return pixels.data() + std::size_t(y - rect.y0) * rect.Width(); }
	const HG_RGBA *Row(int y) const { return pixels.data() + std::size_t(y - rect.y0) * rect.Width(); }
};

// Spatial filter only: B over plan.outputRect from extracted emission E.
// Used by the renderer and by the calibration/impulse tools.
void FilterEmission(const Plan &plan, const Image &emission, Image &filtered);

// Full CPU reference render: source (premultiplied RGBA, layer space) to
// output over plan.outputRect.
void RenderCPU(const Plan &plan, const Image &source, Image &output);

// Fixed kernels (§7.2, §7.3), exposed for tests and GPU uploads.
extern const float kDown2D[6][6];	// 13-tap kernel as a 6x6 texel table
extern const float kDown1D[6];		// single-axis projection
extern const float kUpEven[4];		// tent taps for even fine x at m-2..m+1
extern const float kUpOdd[4];		// tent taps for odd fine x at m-1..m+2
extern const float kBinomial5[5];	// per-level half-octave basis, -2..2

} // namespace hg

#endif // HG_CORE_H
