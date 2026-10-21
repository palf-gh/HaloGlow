/*
	HaloGlow_CPU.cpp

	CPU SmartRender: the numeric-format adapter (§6.1) around the core.
	8-bit (0..255), 16-bit (0..PF_MAX_CHAN16) and 32-bit float worlds arrive
	premultiplied like the core, so they are only normalised to FP32 RGBA,
	rendered by hg::RenderCPU and stored back with rounding and range limits
	in one place.
*/

#include "HaloGlow.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace {

template <typename Pixel>
struct Channel;

template <>
struct Channel<PF_Pixel8> {
	static constexpr double kMax = PF_MAX_CHAN8;
	static A_u_char Store(double v)
	{
		return static_cast<A_u_char>(std::min(std::max(v * kMax + 0.5, 0.0), kMax));
	}
};

template <>
struct Channel<PF_Pixel16> {
	static constexpr double kMax = PF_MAX_CHAN16;
	static A_u_short Store(double v)
	{
		return static_cast<A_u_short>(std::min(std::max(v * kMax + 0.5, 0.0), kMax));
	}
};

template <>
struct Channel<PF_PixelFloat> {
	static constexpr double kMax = 1.0;
	static PF_FpShort Store(double v)
	{
		// No clamp at 1: HDR is kept (§2.2); only overflow to infinity is cut.
		const double big = 3.4028234663852886e38;
		return static_cast<PF_FpShort>(std::min(std::max(v, -big), big));
	}
};

template <typename Pixel>
Pixel *PixelAt(PF_EffectWorld *w, int x, int y)
{
	return reinterpret_cast<Pixel *>(reinterpret_cast<char *>(w->data) + std::ptrdiff_t(y) * w->rowbytes) + x;
}

hg::Rect WorldRect(const PF_EffectWorld *w)
{
	return { w->origin_x, w->origin_y, w->origin_x + w->width, w->origin_y + w->height };
}

template <typename Pixel>
void ReadWorld(PF_EffectWorld *w, hg::Image &img)
{
	img.Reset(WorldRect(w));
	const double inv = 1.0 / Channel<Pixel>::kMax;
	for (int y = 0; y < w->height; ++y) {
		const Pixel *row = PixelAt<Pixel>(w, 0, y);
		HG_RGBA *dst = img.Row(img.rect.y0 + y);
		for (int x = 0; x < w->width; ++x) {
			dst[x] = hg_sanitise(HG_RGBA { static_cast<float>(row[x].red * inv),
										   static_cast<float>(row[x].green * inv),
										   static_cast<float>(row[x].blue * inv),
										   static_cast<float>(row[x].alpha * inv) });
		}
	}
}

template <typename Pixel>
void WriteWorld(const hg::Image &img, PF_EffectWorld *w)
{
	for (int y = 0; y < w->height; ++y) {
		Pixel *row = PixelAt<Pixel>(w, 0, y);
		for (int x = 0; x < w->width; ++x) {
			const HG_RGBA p = img.At(w->origin_x + x, w->origin_y + y);
			row[x].red = Channel<Pixel>::Store(p.r);
			row[x].green = Channel<Pixel>::Store(p.g);
			row[x].blue = Channel<Pixel>::Store(p.b);
			row[x].alpha = Channel<Pixel>::Store(std::min(1.0, static_cast<double>(p.a)));
		}
	}
}

// Bypass (§3.2): at Source Opacity 100 the output is the input byte for byte;
// otherwise premultiplied RGB and alpha are faded together.
template <typename Pixel>
void Bypass(PF_EffectWorld *in, PF_EffectWorld *out, const hg::Settings &s)
{
	const hg::Rect inRect = in ? WorldRect(in) : hg::Rect {};
	const double p = s.sourceOpacity / 100.0;
	for (int y = 0; y < out->height; ++y) {
		Pixel *row = PixelAt<Pixel>(out, 0, y);
		for (int x = 0; x < out->width; ++x) {
			const int lx = out->origin_x + x - inRect.x0;
			const int ly = out->origin_y + y - inRect.y0;
			Pixel v {};
			if (!s.glowOnly && in && lx >= 0 && ly >= 0 && lx < in->width && ly < in->height) {
				v = *PixelAt<Pixel>(in, lx, ly);
				if (p != 1.0) {
					const double k = p / Channel<Pixel>::kMax;
					v.red = Channel<Pixel>::Store(k * v.red);
					v.green = Channel<Pixel>::Store(k * v.green);
					v.blue = Channel<Pixel>::Store(k * v.blue);
					v.alpha = Channel<Pixel>::Store(k * v.alpha);
				}
			}
			row[x] = v;
		}
	}
}

template <typename Pixel>
PF_Err Render(PF_EffectWorld *input, PF_EffectWorld *output, const HG_PreRenderData &data)
{
	const hg::Rect outRect = WorldRect(output);
	const hg::Plan plan = hg::BuildPlan(data.settings, data.scale, data.inputBounds, outRect);
	if (!plan.glowActive) {
		Bypass<Pixel>(input, output, data.settings);
		return PF_Err_NONE;
	}
	hg::Image source;
	if (input) ReadWorld<Pixel>(input, source);
	hg::Image result;
	hg::RenderCPU(plan, source, result);
	WriteWorld<Pixel>(result, output);
	return PF_Err_NONE;
}

} // namespace

PF_Err HG_SmartRenderCPU(PF_InData *in_data, PF_EffectWorld *input, PF_EffectWorld *output,
						 const HG_PreRenderData &data)
{
	AEFX_SuiteScoper<PF_WorldSuite2> worlds(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, nullptr);
	PF_PixelFormat format = PF_PixelFormat_INVALID;
	PF_Err err = worlds->PF_GetPixelFormat(output, &format);
	if (err) return err;
	try {
		switch (format) {
		case PF_PixelFormat_ARGB32:
			return Render<PF_Pixel8>(input, output, data);
		case PF_PixelFormat_ARGB64:
			return Render<PF_Pixel16>(input, output, data);
		case PF_PixelFormat_ARGB128:
			return Render<PF_PixelFloat>(input, output, data);
		default:
			return PF_Err_BAD_CALLBACK_PARAM;
		}
	} catch (const std::bad_alloc &) {
		return PF_Err_OUT_OF_MEMORY;
	}
}
