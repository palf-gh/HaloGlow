/*
	HG_PixelMath.h

	Per-pixel HaloGlow mathematics shared verbatim by the C++ CPU backend, CUDA,
	OpenCL C and Metal Shading Language. The build embeds this header in front
	of the OpenCL and Metal kernel sources, so it must stay inside the common
	subset of those languages:

	  - scalar float / int only, no pointers, no references, no templates;
	  - every literal carries an `f` suffix so no backend promotes to double;
	  - no fast-math intrinsics; pow/exp are the standard precise forms.

	Formulae follow docs/HaloGlow_仕様書.md §6.3 (extraction) and §8.1 (tone,
	tint, alpha and composite). Colours are premultiplied RGBA in working RGB.
*/

#ifndef HG_PIXEL_MATH_H
#define HG_PIXEL_MATH_H

#if defined(__CUDACC__)
	#define HG_FN static __host__ __device__ __forceinline__
	#define HG_ISFINITE(x) isfinite(x)
	#define HG_POW(x, y) powf((x), (y))
	#define HG_EXP(x) expf(x)
#elif defined(__OPENCL_VERSION__)
	#define HG_FN static inline
	#define HG_ISFINITE(x) isfinite(x)
	#define HG_POW(x, y) pow((x), (y))
	#define HG_EXP(x) exp(x)
#elif defined(__METAL_VERSION__)
	#include <metal_stdlib>
	#define HG_FN static inline
	#define HG_ISFINITE(x) metal::isfinite(x)
	#define HG_POW(x, y) metal::precise::pow((x), (y))
	#define HG_EXP(x) metal::precise::exp(x)
#else
	#include <cmath>
	#define HG_FN static inline
	#define HG_ISFINITE(x) std::isfinite(x)
	#define HG_POW(x, y) std::pow((x), (y))
	#define HG_EXP(x) std::exp(x)
#endif

typedef struct HG_RGBA {
	float r;
	float g;
	float b;
	float a;
} HG_RGBA;

// Everything the per-pixel stages need. Plain 4-byte fields only, so the
// layout is identical in C++, CUDA, OpenCL and Metal; keep this order in sync
// with every kernel and the host upload (AGENTS.md "GPU Parity").
typedef struct HG_PixelParams {
	float lumR;				// luminance coefficients l (§3.12), sum 1
	float lumG;
	float lumB;
	float threshold;		// T = Threshold/100 (linear white 1)
	float knee;				// k = T * Softness/100
	float gain;				// g = Intensity * fR (§3.3)
	float saturation;		// s = Saturation/100
	float gamma;			// Gamma (0.1..3)
	float invGamma;			// 1/Gamma
	float colourStrength;	// c = Colour Strength/100
	float colour1R;			// Colour 1 in working RGB (non-negative)
	float colour1G;
	float colour1B;
	float colour2R;
	float colour2G;
	float colour2B;
	float glowOpacity;		// q
	float sourceOpacity;	// p
	int glowOnly;			// 1 = output glow RGBA only (§8.2)
	int algorithmVersion;	// persisted rendering behaviour
} HG_PixelParams;

HG_FN float hg_clamp(float x, float lo, float hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

HG_FN float hg_finite_or_zero(float x)
{
	return HG_ISFINITE(x) ? x : 0.0f;
}

HG_FN float hg_luma(HG_PixelParams p, float r, float g, float b)
{
	return p.lumR * r + p.lumG * g + p.lumB * b;
}

// Input adapter numeric step (§6.1): non-finite to 0, alpha into [0,1].
HG_FN HG_RGBA hg_sanitise(HG_RGBA c)
{
	HG_RGBA o;
	o.r = hg_finite_or_zero(c.r);
	o.g = hg_finite_or_zero(c.g);
	o.b = hg_finite_or_zero(c.b);
	o.a = hg_clamp(hg_finite_or_zero(c.a), 0.0f, 1.0f);
	return o;
}

// Glow source extraction on one premultiplied pixel (§6.3). The result is the
// premultiplied emission E_rgb with its auxiliary coverage E_alpha.
HG_FN HG_RGBA hg_extract(HG_PixelParams p, HG_RGBA src)
{
	HG_RGBA c = hg_sanitise(src);
	HG_RGBA e;
	e.r = 0.0f;
	e.g = 0.0f;
	e.b = 0.0f;
	e.a = 0.0f;
	if (c.a <= 0.0f) {
		return e;
	}

	const float pr = c.r > 0.0f ? c.r : 0.0f;
	const float pg = c.g > 0.0f ? c.g : 0.0f;
	const float pb = c.b > 0.0f ? c.b : 0.0f;
	if (p.algorithmVersion >= 2 && p.threshold > 0.0f) {
		const float t = c.a * p.threshold;
		const float k = c.a * p.knee;
		const float zr = k > 0.0f ? hg_clamp(pr - t + k, 0.0f, 2.0f * k) : 0.0f;
		const float zg = k > 0.0f ? hg_clamp(pg - t + k, 0.0f, 2.0f * k) : 0.0f;
		const float zb = k > 0.0f ? hg_clamp(pb - t + k, 0.0f, 2.0f * k) : 0.0f;
		const float kr = k > 0.0f ? zr * (0.25f * (zr / k)) : 0.0f;
		const float kg = k > 0.0f ? zg * (0.25f * (zg / k)) : 0.0f;
		const float kb = k > 0.0f ? zb * (0.25f * (zb / k)) : 0.0f;
		e.r = hg_clamp(pr - t > kr ? pr - t : kr, 0.0f, pr);
		e.g = hg_clamp(pg - t > kg ? pg - t : kg, 0.0f, pg);
		e.b = hg_clamp(pb - t > kb ? pb - t : kb, 0.0f, pb);
		const float y = hg_luma(p, pr, pg, pb);
		const float z = k > 0.0f ? hg_clamp(y - t + k, 0.0f, 2.0f * k) : 0.0f;
		const float ky = k > 0.0f ? z * (0.25f * (z / k)) : 0.0f;
		const float excess = y - t > ky ? y - t : ky;
		const float mask = y > 0.0f ? hg_clamp(excess / y, 0.0f, 1.0f) : 0.0f;
		e.r = 0.85f * (mask * pr) + 0.15f * e.r;
		e.g = 0.85f * (mask * pg) + 0.15f * e.g;
		e.b = 0.85f * (mask * pb) + 0.15f * e.b;
		const float ye = hg_luma(p, e.r, e.g, e.b);
		e.a = c.a < ye ? c.a : ye;
		return e;
	}
	const float y = hg_luma(p, pr, pg, pb);

	float m = 1.0f;
	if (p.threshold > 0.0f) {
		const float t = c.a * p.threshold;
		const float kappa = c.a * p.knee;
		float excess;
		if (kappa > 0.0f) {
			const float z = hg_clamp(y - t + kappa, 0.0f, 2.0f * kappa);
			const float kneeValue = z * (0.25f * (z / kappa));
			const float linear = y - t;
			excess = linear > kneeValue ? linear : kneeValue;
		} else {
			excess = (y - t) > 0.0f ? (y - t) : 0.0f;
		}
		m = y > 0.0f ? hg_clamp(excess / y, 0.0f, 1.0f) : 0.0f;
	}

	e.r = m * pr;
	e.g = m * pg;
	e.b = m * pb;
	const float ye = hg_luma(p, e.r, e.g, e.b);
	const float ya = ye > 0.0f ? ye : 0.0f;
	e.a = c.a < ya ? c.a : ya;
	return e;
}

// 1 - exp(-x) for x >= 0 without cancellation near 0 (§6.3). The same branch is
// used on every backend so CPU/GPU agree even where expm1 is missing (Metal).
HG_FN float hg_one_minus_exp_neg(float x)
{
	if (x < 1.0e-3f) {
		return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f)));
	}
	return 1.0f - HG_EXP(-x);
}

// Tone curve G_Gamma (§8.1): power below 1, slope-continuous line above 1.
HG_FN float hg_gamma_curve(HG_PixelParams p, float y)
{
	if (y <= 1.0f) {
		return HG_POW(y, p.invGamma);
	}
	return 1.0f + (y - 1.0f) / p.gamma;
}

// Gain left on the widest octave. Falloff 0 keeps the halo near the source.
// Falloff 50 is the photographed point. Falloff 100 leaves a longer tail.
HG_FN float hg_octave_tail(float falloff)
{
	const float f = hg_clamp(falloff, 0.0f, 100.0f) * 0.01f;
	return 0.04f + 0.16f * f;
}

// Octave 0 is the local kernel and octaveCount is the pyramid depth. The
// fraction octave/octaveCount keeps the same shape when Size adds octaves,
// so the halo grows with Size instead of dying after the first few pixels.
HG_FN float hg_scale_gain(int octave, int octaveCount, float falloff)
{
	if (octave < 0) return 0.0f;
	const float span = octaveCount > 0 ? (float)octaveCount : 1.0f;
	const float t = hg_clamp((float)octave / span, 0.0f, 1.0f);
	return 8.0f * HG_POW(hg_octave_tail(falloff), t);
}

// Linear for a faint wing, and exactly 1 when a full-scale low-pass is 1
// and the gain is above 1. A flat highlight therefore does not stack.
HG_FN float hg_scale_shoulder(float gain, float x)
{
	const float u = gain * x;
	if (!(gain > 1.0f)) return u;
	return u / (1.0f + (gain - 1.0f) * hg_clamp(x, 0.0f, 1.0f));
}

HG_FN float hg_tone_channel(HG_PixelParams p, float x, float gain)
{
	const float v = x > 0.0f ? x : 0.0f;
	return hg_gamma_curve(p, hg_scale_shoulder(gain, v));
}

HG_FN HG_RGBA hg_tone_scale(HG_PixelParams p, HG_RGBA v, float gain)
{
	HG_RGBA o;
	o.r = hg_tone_channel(p, v.r, gain);
	o.g = hg_tone_channel(p, v.g, gain);
	o.b = hg_tone_channel(p, v.b, gain);
	o.a = hg_tone_channel(p, v.a, gain);
	return o;
}

// HDR-safe screen: values above 1 stay monotone, SDR values compress (§8.1).
HG_FN float hg_screen(float a, float b)
{
	return a + b - hg_clamp(a, 0.0f, 1.0f) * hg_clamp(b, 0.0f, 1.0f);
}

HG_FN HG_RGBA hg_screen_rgba(HG_RGBA a, HG_RGBA b)
{
	HG_RGBA o;
	o.r = hg_screen(a.r, b.r);
	o.g = hg_screen(a.g, b.g);
	o.b = hg_screen(a.b, b.b);
	o.a = hg_screen(a.a, b.a);
	return o;
}

// Spatially filtered emission B (premultiplied RGBA) -> final glow RGBA after
// Intensity, Saturation, Gamma, tint and Glow Opacity (§8.1).
HG_FN HG_RGBA hg_glow_from_filtered(HG_PixelParams p, HG_RGBA bIn)
{
	const float ur = p.gain * bIn.r;
	const float ug = p.gain * bIn.g;
	const float ub = p.gain * bIn.b;
	const float yu = hg_luma(p, ur, ug, ub);
	const float s = p.saturation;
	float vr = s * ur + (1.0f - s) * yu;
	float vg = s * ug + (1.0f - s) * yu;
	float vb = s * ub + (1.0f - s) * yu;
	vr = vr > 0.0f ? vr : 0.0f;
	vg = vg > 0.0f ? vg : 0.0f;
	vb = vb > 0.0f ? vb : 0.0f;
	const float yv = hg_luma(p, vr, vg, vb);

	float l0r = 0.0f;
	float l0g = 0.0f;
	float l0b = 0.0f;
	if (yv > 0.0f) {
		// Version 2 already toned each octave before screening it.
		const float toned = p.algorithmVersion >= 2 ? yv : hg_gamma_curve(p, yv);
		l0r = (vr / yv) * toned;
		l0g = (vg / yv) * toned;
		l0b = (vb / yv) * toned;
	}

	const float y0 = hg_luma(p, l0r, l0g, l0b);
	const float u = hg_clamp(y0, 0.0f, 1.0f);
	const float tr = (1.0f - u) * p.colour1R + u * p.colour2R;
	const float tg = (1.0f - u) * p.colour1G + u * p.colour2G;
	const float tb = (1.0f - u) * p.colour1B + u * p.colour2B;
	const float c = p.colourStrength;
	const float lr = (1.0f - c) * l0r + c * (y0 * tr);
	const float lg = (1.0f - c) * l0g + c * (y0 * tg);
	const float lb = (1.0f - c) * l0b + c * (y0 * tb);

	const float aux = p.gain * bIn.a;
	const float ylight = hg_luma(p, lr, lg, lb);
	float xAlpha = aux < ylight ? aux : ylight;
	xAlpha = xAlpha > 0.0f ? xAlpha : 0.0f;
	float aLight = hg_one_minus_exp_neg(xAlpha);
	if (p.algorithmVersion >= 2) {
		// Preserve premultiplied SDR glow on transparent backgrounds; values
		// above one remain HDR and are not clipped to the alpha channel.
		const float maxRG = lr > lg ? lr : lg;
		const float maxRGB = maxRG > lb ? maxRG : lb;
		const float coverage = hg_clamp(maxRGB, 0.0f, 1.0f);
		aLight = aLight > coverage ? aLight : coverage;
	}

	HG_RGBA o;
	o.r = p.glowOpacity * lr;
	o.g = p.glowOpacity * lg;
	o.b = p.glowOpacity * lb;
	o.a = p.glowOpacity * aLight;
	return o;
}

// Final composite of the faded source with the glow (§8.1, §8.2).
HG_FN HG_RGBA hg_composite(HG_PixelParams p, HG_RGBA srcIn, HG_RGBA glow)
{
	HG_RGBA o;
	if (p.glowOnly) {
		return glow;
	}
	const HG_RGBA src = hg_sanitise(srcIn);
	const float sa = p.sourceOpacity * src.a;
	const float sr = p.sourceOpacity * src.r;
	const float sg = p.sourceOpacity * src.g;
	const float sb = p.sourceOpacity * src.b;
	if (p.algorithmVersion >= 2) {
		// Screen-like highlight roll-off while keeping HDR values monotone.
		o.r = sr + glow.r - 0.95f * hg_clamp(glow.r, 0.0f, 1.0f) * hg_clamp(sr, 0.0f, 1.0f);
		o.g = sg + glow.g - 0.95f * hg_clamp(glow.g, 0.0f, 1.0f) * hg_clamp(sg, 0.0f, 1.0f);
		o.b = sb + glow.b - 0.95f * hg_clamp(glow.b, 0.0f, 1.0f) * hg_clamp(sb, 0.0f, 1.0f);
	} else {
		o.r = sr + glow.r;
		o.g = sg + glow.g;
		o.b = sb + glow.b;
	}
	o.a = sa + (1.0f - sa) * glow.a;
	if (p.algorithmVersion >= 2) {
		// Keep a valid SDR source premultiplied after adding the light. For
		// HDR or already-unbounded source pixels, do not reinterpret their
		// existing RGB/alpha relationship when the glow tends to zero.
		const float srcMaxRG = sr > sg ? sr : sg;
		const float srcMax = srcMaxRG > sb ? srcMaxRG : sb;
		const float outMaxRG = o.r > o.g ? o.r : o.g;
		const float outMax = outMaxRG > o.b ? outMaxRG : o.b;
		const float base = hg_clamp(srcMax, 0.0f, 1.0f) > sa ? hg_clamp(srcMax, 0.0f, 1.0f) : sa;
		const float excess = hg_clamp(outMax, 0.0f, 1.0f) - base;
		const float coverage = hg_clamp(sa + (excess > 0.0f ? excess : 0.0f), 0.0f, 1.0f);
		o.a = o.a > coverage ? o.a : coverage;
	}
	return o;
}

#endif // HG_PIXEL_MATH_H
