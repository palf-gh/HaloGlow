/*
	hg_core_tests.cpp

	Host-independent checks of the HaloGlow core (spec §12). Uses its own CHECK
	macro so the checks also run in Release builds, where NDEBUG strips assert.
*/

#include "HG_Core.h"
#include "HG_UpdateCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond, ...)                                                        \
	do {                                                                        \
		++g_checks;                                                             \
		if (!(cond)) {                                                          \
			++g_failures;                                                       \
			std::printf("FAIL %s:%d: %s :: ", __FILE__, __LINE__, #cond);      \
			std::printf(__VA_ARGS__);                                           \
			std::printf("\n");                                                  \
		}                                                                       \
	} while (0)

HG_PixelParams PixelParams(float thresholdPct, float softnessPct)
{
	hg::Settings s;
	s.algorithmVersion = 1; // Existing extraction checks specify the legacy luminance model.
	s.threshold = thresholdPct;
	s.softness = softnessPct;
	const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, hg::Rect { 0, 0, 1, 1 }, hg::Rect { 0, 0, 1, 1 });
	return plan.pixel;
}

double Sum(const hg::Image &img, int channel)
{
	double s = 0.0;
	for (const HG_RGBA &p : img.pixels) s += (&p.r)[channel];
	return s;
}

//-----------------------------------------------------------------------------
void TestLuminance()
{
	const hg::LuminanceSpace spaces[] = { hg::LuminanceSpace::Rec709, hg::LuminanceSpace::DisplayP3,
										  hg::LuminanceSpace::ACEScg };
	for (hg::LuminanceSpace sp : spaces) {
		float l[3];
		hg::LuminanceCoefficients(sp, l);
		CHECK(std::fabs(l[0] + l[1] + l[2] - 1.0f) < 1e-6f, "sum %g", l[0] + l[1] + l[2]);
		CHECK(l[0] > 0 && l[1] > 0 && l[2] > 0, "positive");
	}
	float acescg[3];
	hg::LuminanceCoefficients(hg::LuminanceSpace::ACEScg, acescg);
	CHECK(std::fabs(acescg[0] - 0.2722287f) < 2e-7f, "ACEScg R %g", acescg[0]);
}

void TestExtraction()
{
	// Black and transparent pixels never emit (§12.3), even at T = 0.
	HG_PixelParams p0 = PixelParams(0.0f, 50.0f);
	HG_RGBA e = hg_extract(p0, HG_RGBA { 0.f, 0.f, 0.f, 1.f });
	CHECK(e.r == 0 && e.a == 0, "black emits at T=0");
	e = hg_extract(p0, HG_RGBA { 5.f, 5.f, 5.f, 0.f });
	CHECK(e.r == 0 && e.a == 0, "hidden RGB under alpha 0 emits");
	e = hg_extract(p0, HG_RGBA { 0.3f, 0.2f, 0.1f, 1.f });
	CHECK(e.r == 0.3f && e.g == 0.2f && e.b == 0.1f, "T=0 must pass visible pixels");

	// Hard knee (Softness 0) and soft knee stay finite and monotone.
	const HG_PixelParams hard = PixelParams(25.0f, 0.0f);
	const HG_PixelParams soft = PixelParams(25.0f, 50.0f);
	float prevHard = 0.0f, prevSoft = 0.0f;
	for (int i = 0; i <= 400; ++i) {
		const float v = i * 0.005f;
		const HG_RGBA c { v, v, v, 1.f };
		const HG_RGBA eh = hg_extract(hard, c);
		const HG_RGBA es = hg_extract(soft, c);
		CHECK(std::isfinite(eh.r) && std::isfinite(es.r), "finite at %g", v);
		CHECK(eh.r + 1e-7f >= prevHard && es.r + 1e-7f >= prevSoft, "monotone at %g", v);
		prevHard = eh.r;
		prevSoft = es.r;
		if (v < 0.25f) CHECK(eh.r == 0.0f, "hard knee leaks below T at %g", v);
		if (v < 0.125f) CHECK(es.r == 0.0f, "soft knee leaks below T-k at %g", v);
	}
	// Colour ratio of positive RGB is preserved (§6.3).
	const HG_RGBA c { 2.0f, 1.0f, 0.5f, 1.f };
	e = hg_extract(soft, c);
	CHECK(std::fabs(e.g / e.r - 0.5f) < 1e-6f && std::fabs(e.b / e.r - 0.25f) < 1e-6f, "ratio");
	// Non-finite input is zeroed.
	e = hg_extract(soft, HG_RGBA { NAN, INFINITY, 1.f, 1.f });
	CHECK(std::isfinite(e.r) && std::isfinite(e.g) && std::isfinite(e.a), "non-finite");

	// Version 2 also picks up saturated light whose overall luminance is below
	// the knee, without inventing red or green light in a blue source.
	HG_PixelParams v2 = soft;
	v2.algorithmVersion = 2;
	const HG_RGBA blue { 0.f, 0.f, 0.4f, 1.f };
	const HG_RGBA oldBlue = hg_extract(soft, blue);
	const HG_RGBA newBlue = hg_extract(v2, blue);
	CHECK(oldBlue.b == 0.0f && newBlue.b > 0.0f && newBlue.r == 0.0f && newBlue.g == 0.0f,
		"v2 detects saturated blue while v1 remains luminance-only");
	const HG_RGBA half = hg_extract(v2, HG_RGBA {0.f, 0.f, 0.2f, 0.5f});
	CHECK(std::fabs(half.b - 0.5f * newBlue.b) < 1e-6f &&
		std::fabs(half.a - 0.5f * newBlue.a) < 1e-6f, "premultiplied alpha scaling");
	float previousBlue = 0.0f;
	for (int i = 0; i <= 400; ++i) {
		const float value = i * 0.005f;
		const HG_RGBA light = hg_extract(v2, HG_RGBA {0.f, 0.f, value, 1.f});
		CHECK(std::isfinite(light.b) && light.b >= previousBlue && light.b <= value,
			"v2 channel extraction monotonic at %g", value);
		previousBlue = light.b;
	}
}

void TestTone()
{
	hg::Settings s;
	s.colourStrength = 100.0f;
	for (int i = 0; i < 3; ++i) s.colour1[i] = s.colour2[i] = 0.0f;
	hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, hg::Rect { 0, 0, 1, 1 }, hg::Rect { 0, 0, 1, 1 });
	HG_RGBA g = hg_glow_from_filtered(plan.pixel, HG_RGBA { 4.f, 2.f, 1.f, 1.f });
	CHECK(g.r == 0 && g.g == 0 && g.b == 0 && g.a == 0, "black tint must remove glow and alpha");

	// Gamma curve: value and slope continuous at Y = 1 (§8.1).
	const float gammas[] = { 0.1f, 1.0f, 3.0f };
	for (float gm : gammas) {
		hg::Settings t;
		t.gamma = gm;
		plan = hg::BuildPlan(t, hg::RenderScale {}, hg::Rect { 0, 0, 1, 1 }, hg::Rect { 0, 0, 1, 1 });
		const float below = hg_gamma_curve(plan.pixel, 1.0f - 1e-3f);
		const float at = hg_gamma_curve(plan.pixel, 1.0f);
		const float above = hg_gamma_curve(plan.pixel, 1.0f + 1e-3f);
		CHECK(std::fabs(at - 1.0f) < 1e-6f, "gamma(1)");
		CHECK(std::fabs((above - at) - (at - below)) < 2e-3f * (1.0f / gm), "slope gamma %g", gm);
		CHECK(std::isfinite(hg_gamma_curve(plan.pixel, 1048576.0f)), "HDR finite");
	}
}

//-----------------------------------------------------------------------------
void TestVersionedComposite()
{
	CHECK(hg::IsSupportedAlgorithmVersion(1) && hg::IsSupportedAlgorithmVersion(2), "known versions");
	CHECK(!hg::IsSupportedAlgorithmVersion(0) && !hg::IsSupportedAlgorithmVersion(3), "unknown versions");
	hg::Settings settings;
	settings.algorithmVersion = 1;
	const hg::Rect rect { 0, 0, 1, 1 };
	HG_PixelParams p = hg::BuildPlan(settings, hg::RenderScale {}, rect, rect).pixel;
	CHECK(p.algorithmVersion == 1, "saved version propagated");
	const HG_RGBA src { 0.8f, 0.4f, 0.2f, 0.5f };
	const HG_RGBA glow { 0.3f, 0.2f, 0.1f, 0.4f };
	const HG_RGBA old = hg_composite(p, src, glow);
	CHECK(old.r == src.r + glow.r && old.g == src.g + glow.g && old.b == src.b + glow.b,
		"version 1 preserves additive RGB");
	CHECK(old.a == src.a + (1.0f - src.a) * glow.a, "version 1 alpha union");
	settings.algorithmVersion = 2;
	p = hg::BuildPlan(settings, hg::RenderScale {}, rect, rect).pixel;
	CHECK(p.algorithmVersion == 2, "current version propagated");
	CHECK(std::fabs(hg_octave_tail(50.0f) - 0.12f) < 1e-6f, "default coarse-octave tail");
	CHECK(std::fabs(hg_scale_gain(0, 6, 50.0f) - 8.0f) < 1e-4f, "finest scale gain");
	CHECK(std::fabs(hg_scale_gain(6, 6, 50.0f) - 8.0f * 0.12f) < 1e-4f, "widest octave keeps a tail");
	CHECK(hg_scale_gain(6, 6, 0.0f) < hg_scale_gain(6, 6, 100.0f), "falloff lengthens the coarse octaves");
	CHECK(std::fabs(hg_scale_shoulder(8.0f, 0.0f)) < 1e-6f, "shoulder leaves black alone");
	CHECK(std::fabs(hg_scale_shoulder(8.0f, 1.0f) - 1.0f) < 1e-5f, "a full low-pass stays at one");
	CHECK(std::fabs(hg_scale_shoulder(8.0f, 0.01f) - (0.08f / 1.07f)) < 1e-5f,
		"a faint wing keeps nearly the full scale gain");
	CHECK(std::fabs(hg_screen(0.2f, 0.2f) - 0.36f) < 1e-6f, "screen compresses overlapping light");
	const HG_RGBA current = hg_composite(p, src, glow);
	CHECK(current.r < old.r && current.r > src.r, "highlight roll-off");
	CHECK(current.a == old.a, "alpha union unchanged");
	const HG_RGBA zero = hg_composite(p, src, HG_RGBA { 0, 0, 0, 0 });
	CHECK(zero.r == src.r && zero.g == src.g && zero.b == src.b && zero.a == src.a,
		"zero glow preserves source");
	const HG_RGBA translucent = hg_composite(p, HG_RGBA {0.5f,0.2f,0.1f,0.5f},
		HG_RGBA {0.5f,0.1f,0.05f,0.5f});
	CHECK(translucent.r <= translucent.a + 1e-6f, "SDR composite remains premultiplied");
	const HG_RGBA hdrSource {4.0f, 1.0f, 0.5f, 0.25f};
	const HG_RGBA hdrZero = hg_composite(p, hdrSource, HG_RGBA {0,0,0,0});
	CHECK(hdrZero.r == hdrSource.r && hdrZero.a == hdrSource.a, "zero glow preserves HDR source alpha");
	float previous = -100.0f;
	for (int i = 0; i <= 400; ++i) {
		const float v = static_cast<float>(i) * 0.025f;
		const float out = hg_composite(p, HG_RGBA {v,v,v,1}, HG_RGBA {2,2,2,1}).r;
		CHECK(out >= previous, "monotone in HDR source at %g", v);
		previous = out;
	}
	previous = -100.0f;
	for (int i = 0; i <= 400; ++i) {
		const float v = static_cast<float>(i) * 0.025f;
		const float out = hg_composite(p, HG_RGBA {0.8f,0.8f,0.8f,1}, HG_RGBA {v,v,v,1}).r;
		CHECK(out >= previous, "monotone in HDR glow at %g", v);
		previous = out;
	}
	p.glowOnly = 1;
	const HG_RGBA only = hg_composite(p, src, glow);
	CHECK(only.r == glow.r && only.g == glow.g && only.b == glow.b && only.a == glow.a,
		"glow-only output unchanged by compositing");
	for (float alpha : { 1e-4f, 0.01f, 0.5f }) {
		const HG_RGBA light = hg_glow_from_filtered(p, HG_RGBA { alpha, alpha, alpha, alpha });
		CHECK(std::isfinite(light.r) && std::isfinite(light.a) && light.a >= 0.0f && light.a <= 1.0f,
			"finite transparent glow at alpha %g", alpha);
		CHECK(light.r <= light.a + 1e-6f || light.r > 1.0f, "SDR light is premultiplied at alpha %g", alpha);
		if (light.r <= 1.0f) {
			const float overWhite = light.r + (1.0f - light.a);
			CHECK(overWhite <= 1.0f + 1e-6f, "SDR light over white at alpha %g", alpha);
		}
	}
}

hg::Image ConstantImage(const hg::Rect &r, HG_RGBA v)
{
	hg::Image img;
	img.Reset(r);
	std::fill(img.pixels.begin(), img.pixels.end(), v);
	return img;
}

void TestDcGain()
{
	// A constant emission far from the image edge keeps its value (§7.3).
	const float sizes[] = { 0.7f, 4.0f, 16.0f, 40.0f };
	for (float size : sizes) {
		hg::Settings s;
		s.algorithmVersion = 1; // Version 2 screens octaves, so a flat field is not a unit gain.
		s.size = size;
		const hg::Rect in { -300, -300, 300, 300 };
		const hg::Rect out { -4, -4, 4, 4 };
		const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, in, out);
		const hg::Image e = ConstantImage(in, HG_RGBA { 1.f, 0.5f, 0.25f, 1.f });
		hg::Image b;
		hg::FilterEmission(plan, e, b);
		for (const HG_RGBA &px : b.pixels)
			CHECK(std::fabs(px.r - 1.0f) < 2e-5f && std::fabs(px.a - 1.0f) < 2e-5f,
				  "DC gain size %g r=%g", size, px.r);
	}
}

void TestTiling()
{
	// Tiles with odd sizes and negative origins equal the full render (§12.5).
	hg::Settings s;
	s.size = 9.0f;
	s.width = 160.0f;
	s.height = 55.0f;
	s.threshold = 10.0f;
	const hg::Rect in { -37, -21, 58, 43 };
	hg::Image src;
	src.Reset(in);
	unsigned seed = 12345u;
	for (HG_RGBA &p : src.pixels) {
		seed = seed * 1664525u + 1013904223u;
		const float v = ((seed >> 8) & 0xffff) / 65535.0f;
		p = HG_RGBA { v * 2.0f, v, v * 0.5f, (seed & 1) ? 1.0f : 0.5f };
	}
	const hg::Rect full = hg::MaxResultRect(s, hg::RenderScale {}, in);
	hg::Image ref;
	hg::RenderCPU(hg::BuildPlan(s, hg::RenderScale {}, in, full), src, ref);

	const hg::Rect tiles[] = { { -61, -40, -20, -3 }, { -20, -3, 1, 30 }, { 1, 30, 90, 71 },
							   { 13, -7, 14, 60 }, { -50, 11, 70, 12 } };
	for (const hg::Rect &t : tiles) {
		const hg::Rect tile = hg::Intersect(t, full);
		const hg::Rect need = hg::RequiredInputRect(s, hg::RenderScale {}, tile);
		const hg::Rect avail = hg::Intersect(need, in);
		hg::Image cropped;
		cropped.Reset(avail);
		for (int y = avail.y0; y < avail.y1; ++y)
			for (int x = avail.x0; x < avail.x1; ++x)
				cropped.Row(y)[x - avail.x0] = src.At(x, y);
		hg::Image part;
		hg::RenderCPU(hg::BuildPlan(s, hg::RenderScale {}, in, tile), cropped, part);
		float worst = 0.0f;
		for (int y = tile.y0; y < tile.y1; ++y)
			for (int x = tile.x0; x < tile.x1; ++x) {
				const HG_RGBA a = part.At(x, y);
				const HG_RGBA b = ref.At(x, y);
				worst = std::max({ worst, std::fabs(a.r - b.r), std::fabs(a.a - b.a) });
			}
		CHECK(worst <= 1e-5f, "tile [%d,%d)x[%d,%d) differs by %g", t.x0, t.x1, t.y0, t.y1, worst);
	}
}

// Light sources outside the layer rectangle never emit (§8.3): an adjustment
// layer receives the composite below, which may extend past its own bounds.
void TestLayerBoundsLightSource()
{
	hg::Settings s;
	s.size = 12.0f;
	s.threshold = 0.0f;
	const hg::Rect layer { 0, 0, 64, 48 };
	const hg::Rect input { -40, -30, 104, 78 };	// host input reaches beyond the layer
	hg::Image src;
	src.Reset(input);
	src.Row(-20)[-30 - input.x0] = HG_RGBA { 50.f, 50.f, 50.f, 1.f };	// bright, outside the layer
	src.Row(60)[90 - input.x0] = HG_RGBA { 50.f, 50.f, 50.f, 1.f };
	const hg::Rect out = hg::MaxResultRect(s, hg::RenderScale {}, layer, input);
	CHECK(out.x0 <= input.x0 && out.y0 <= input.y0 && out.x1 >= input.x1 && out.y1 >= input.y1,
		  "source outside the layer must stay in the result rect");
	hg::Image dst;
	hg::RenderCPU(hg::BuildPlan(s, hg::RenderScale {}, layer, out), src, dst);
	double leaked = 0.0;
	for (int y = out.y0; y < out.y1; ++y)
		for (int x = out.x0; x < out.x1; ++x) {
			const HG_RGBA o = dst.At(x, y);
			const HG_RGBA i = src.At(x, y);
			leaked = std::max({ leaked, double(std::fabs(o.r - i.r)), double(std::fabs(o.a - i.a)) });
		}
	CHECK(leaked == 0.0, "glow from outside the layer leaked by %g", leaked);

	// The same pixel inside the layer does glow.
	src.Row(20)[30 - input.x0] = HG_RGBA { 50.f, 50.f, 50.f, 1.f };
	hg::RenderCPU(hg::BuildPlan(s, hg::RenderScale {}, layer, out), src, dst);
	CHECK(dst.At(36, 20).r > 0.0f, "inside-layer source must glow");

	// Pre-render: light needs are clipped, the output itself is still requested.
	const hg::Rect need = hg::RequiredInputRect(s, hg::RenderScale {}, hg::Rect { -40, -30, 0, 0 }, layer);
	CHECK(need.x0 == -40 && need.y0 == -30, "output area must stay requested");
	CHECK(need.x1 <= std::max(0, layer.x1) && need.y1 <= layer.y1, "light requests stay inside the layer");
}

void TestBypass()
{
	// Intensity 0 and Size 0 leave the source untouched at Source Opacity 100.
	hg::Settings variants[3];
	variants[0].intensity = 0.0f;
	variants[1].size = 0.0f;
	variants[2].width = 0.0f;
	variants[2].height = 0.0f;
	const hg::Rect r { 0, 0, 7, 5 };
	const hg::Image src = ConstantImage(r, HG_RGBA { 0.75f, 0.5f, 0.25f, 0.5f });
	for (const hg::Settings &s : variants) {
		const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, r, r);
		CHECK(!plan.glowActive, "glow should be inactive");
		hg::Image out;
		hg::RenderCPU(plan, src, out);
		for (std::size_t i = 0; i < out.pixels.size(); ++i)
			CHECK(out.pixels[i].r == src.pixels[i].r && out.pixels[i].a == src.pixels[i].a, "bypass");
		CHECK(hg::MaxResultRect(s, hg::RenderScale {}, r) == r, "bypass result rect");
	}
}

//-----------------------------------------------------------------------------
// PSF shape against the direct target (§12.1).
//-----------------------------------------------------------------------------
constexpr double kQ90At50 = 0.7192929521;
constexpr double kPi = 3.14159265358979323846;

double TargetProfile(double r, double falloff)
{
	const double f = falloff / 100.0;
	const double wc = 0.75 - 0.30 * f, wm = 0.23 + 0.14 * f, wh = 0.02 + 0.16 * f;
	auto g = [](double rr, double s) { return std::exp(-rr * rr / (2 * s * s)) / (2 * kPi * s * s); };
	const double a = 0.80, beta = 2.5;
	const double moffat = (beta - 1) / (kPi * a * a) * std::pow(1 + (r / a) * (r / a), -beta);
	return r > 4.0 ? 0.0 : wc * g(r, 0.10) + wm * g(r, 0.40) + wh * moffat;
}

struct ShapeResult {
	double energy = 0.0;
	double r90 = 0.0;
	double maxEefDiff = 0.0;
};

ShapeResult MeasureShape(float size, int imageHalf)
{
	hg::Settings s;
	s.algorithmVersion = 1; // Energy and R90 belong to the version-1 normalised PSF.
	s.size = size;
	s.threshold = 0.0f;
	const hg::Rect frame { -imageHalf, -imageHalf, imageHalf, imageHalf };
	const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, frame, frame);
	hg::Image e;
	e.Reset(frame);
	e.pixels[std::size_t(imageHalf) * frame.Width() + imageHalf] = HG_RGBA { 1.f, 1.f, 1.f, 1.f };
	hg::Image b;
	hg::FilterEmission(plan, e, b);

	// Target sampled 4x4 per pixel, normalised on the lattice.
	const double scaleR = size / kQ90At50;
	std::vector<double> target(b.pixels.size());
	double tsum = 0.0;
	for (int y = frame.y0; y < frame.y1; ++y)
		for (int x = frame.x0; x < frame.x1; ++x) {
			double acc = 0.0;
			for (int j = 0; j < 4; ++j)
				for (int i = 0; i < 4; ++i) {
					const double px = x + (i + 0.5) / 4 - 0.5, py = y + (j + 0.5) / 4 - 0.5;
					acc += TargetProfile(std::sqrt(px * px + py * py) / scaleR, 50.0);
				}
			const std::size_t idx = std::size_t(y - frame.y0) * frame.Width() + (x - frame.x0);
			target[idx] = acc;
			tsum += acc;
		}
	const int bins = imageHalf;
	std::vector<double> eA(bins + 1, 0.0), eT(bins + 1, 0.0);
	ShapeResult res;
	for (int y = frame.y0; y < frame.y1; ++y)
		for (int x = frame.x0; x < frame.x1; ++x) {
			const std::size_t idx = std::size_t(y - frame.y0) * frame.Width() + (x - frame.x0);
			const int r = std::min(bins, static_cast<int>(std::ceil(std::sqrt(double(x * x + y * y)))));
			eA[r] += b.pixels[idx].r;
			eT[r] += target[idx] / tsum;
			res.energy += b.pixels[idx].r;
		}
	double ca = 0.0, ct = 0.0;
	bool found = false;
	for (int r = 0; r <= bins; ++r) {
		ca += eA[r];
		ct += eT[r];
		res.maxEefDiff = std::max(res.maxEefDiff, std::fabs(ca - ct));
		if (!found && ca >= 0.9) {
			res.r90 = r;
			found = true;
		}
	}
	return res;
}

void TestShape()
{
	const struct { float size; int half; } cases[] = { { 4.0f, 64 }, { 16.0f, 160 }, { 64.0f, 512 }, { 256.0f, 1536 } };
	for (const auto &c : cases) {
		const ShapeResult r = MeasureShape(c.size, c.half);
		std::printf("  shape R=%g: energy=%.5f R90=%.2f maxEEF=%.4f\n", c.size, r.energy, r.r90, r.maxEefDiff);
		CHECK(std::fabs(r.energy - 1.0) <= 0.005, "energy R=%g %g", c.size, r.energy);
		// Shell binning is integer-radius, so allow one pixel on top of 5 %.
		CHECK(std::fabs(r.r90 - c.size) <= 0.05 * c.size + 1.0, "R90 R=%g %g", c.size, r.r90);
		CHECK(r.maxEefDiff <= 0.03, "EEF R=%g %g", c.size, r.maxEefDiff);
	}
}

void TestContinuity()
{
	// Total glow energy must not jump while Width sweeps 100 -> 400 % or Size
	// crosses octave boundaries (§12.4). Far from edges energy stays ~1.
	auto energy = [](float size, float width) {
		hg::Settings s;
		s.algorithmVersion = 1;
		s.size = size;
		s.width = width;
		s.threshold = 0.0f;
		const int half = 200;
		const hg::Rect frame { -half, -half, half, half };
		const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, frame, frame);
		hg::Image e;
		e.Reset(frame);
		e.pixels[std::size_t(half) * frame.Width() + half] = HG_RGBA { 1.f, 1.f, 1.f, 1.f };
		hg::Image b;
		hg::FilterEmission(plan, e, b);
		return Sum(b, 0);
	};
	double prev = energy(8.0f, 100.0f);
	for (int w = 101; w <= 400; w += 3) {
		const double cur = energy(8.0f, float(w));
		CHECK(std::fabs(cur - prev) < 2e-3, "energy jump at width %d: %g -> %g", w, prev, cur);
		prev = cur;
	}
	prev = energy(3.0f, 100.0f);
	for (int i = 1; i <= 80; ++i) {
		const float size = 3.0f * std::pow(2.0f, i / 32.0f);
		const double cur = energy(size, 100.0f);
		CHECK(std::fabs(cur - prev) < 2e-3, "energy jump at size %g: %g -> %g", size, prev, cur);
		prev = cur;
	}
}

void TestThinAxis()
{
	// Height 25 % must not spread more along y than the 1-D limit allows.
	hg::Settings s;
	s.algorithmVersion = 1;
	s.size = 16.0f;
	s.width = 400.0f;
	s.height = 25.0f;
	s.threshold = 0.0f;
	const int half = 256;
	const hg::Rect frame { -half, -half, half, half };
	const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, frame, frame);
	hg::Image e;
	e.Reset(frame);
	e.pixels[std::size_t(half) * frame.Width() + half] = HG_RGBA { 1.f, 1.f, 1.f, 1.f };
	hg::Image b;
	hg::FilterEmission(plan, e, b);
	double sx = 0, sy = 0, w = 0;
	for (int y = frame.y0; y < frame.y1; ++y)
		for (int x = frame.x0; x < frame.x1; ++x) {
			const double v = b.At(x, y).r;
			sx += v * x * x;
			sy += v * y * y;
			w += v;
		}
	const double ratio = std::sqrt(sy / sx);
	std::printf("  thin axis: y/x second-moment ratio %.4f (target %.4f)\n", ratio, 25.0 / 400.0);
	CHECK(ratio < 2.0 * (25.0 / 400.0), "thin axis fattened: %g", ratio);
}

float EncodedLuma(float x)
{
	if (!(x > 0.0f)) return 0.0f;
	if (x <= 0.0031308f) return 12.92f * x;
	return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

void TestScaleFalloff()
{
	// A flat emission is a low-pass of one at every octave. The shoulder keeps
	// that field from stacking above one, so a large highlight gains no rim.
	{
		hg::Settings s;
		s.threshold = 0.0f;
		const hg::Rect in { -80, -80, 80, 80 };
		const hg::Rect out { -8, -8, 8, 8 };
		const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, in, out);
		const hg::Image e = ConstantImage(in, HG_RGBA { 1.f, 1.f, 1.f, 1.f });
		hg::Image b;
		hg::FilterEmission(plan, e, b);
		for (const HG_RGBA &px : b.pixels)
			CHECK(px.r <= 1.02f && px.r >= 0.0f && std::isfinite(px.r), "flat scale %g", px.r);
	}

	// A small lamp falls smoothly. The photographed point crosses 0.02 at about
	// 2.8 times the radius where it crosses 0.2; a hard second kernel does not.
	hg::Settings s;
	s.glowOnly = true;
	s.threshold = 0.0f;
	s.size = 48.0f;
	const int half = 96;
	const hg::Rect frame { -half, -half, half, half };
	hg::Image src;
	src.Reset(frame);
	for (int y = -2; y <= 2; ++y) {
		for (int x = -2; x <= 2; ++x) {
			const std::size_t i = std::size_t(y + half) * frame.Width() + (x + half);
			src.pixels[i] = HG_RGBA { 1.f, 1.f, 1.f, 1.f };
		}
	}
	const hg::Plan plan = hg::BuildPlan(s, hg::RenderScale {}, frame, frame);
	hg::Image out;
	hg::RenderCPU(plan, src, out);
	std::vector<double> sum(half + 1, 0.0), count(half + 1, 0.0);
	for (int y = frame.y0; y < frame.y1; ++y) {
		for (int x = frame.x0; x < frame.x1; ++x) {
			const int r = std::min(half, static_cast<int>(std::lround(std::sqrt(double(x * x + y * y)))));
			sum[r] += EncodedLuma(out.At(x, y).r);
			count[r] += 1.0;
		}
	}
	double previous = 2.0;
	double r20 = -1.0, r02 = -1.0;
	for (int r = 3; r <= half; ++r) {
		if (!(count[r] > 0.0)) continue;
		const double y = sum[r] / count[r];
		CHECK(y <= previous + 0.004, "radial ridge at %d (%g -> %g)", r, previous, y);
		previous = y;
		if (r20 < 0.0 && y < 0.2) r20 = r;
		if (r02 < 0.0 && y < 0.02) r02 = r;
	}
	const double ratio = (r20 > 0.0 && r02 > 0.0) ? r02 / r20 : 0.0;
	std::printf("  scale falloff: r0.2=%.0f r0.02=%.0f ratio=%.3f\n", r20, r02, ratio);
	CHECK(ratio > 2.0 && ratio < 3.6, "encoded shoulder ratio %g", ratio);
}

void TestReleaseTag()
{
	const hg::ReleaseVersion plain = hg::ParseReleaseTag("1.2.3", 5);
	const hg::ReleaseVersion prefixed = hg::ParseReleaseTag("v0.2.0", 6);
	CHECK(plain.ok && plain.major == 1 && plain.minor == 2 && plain.bug == 3, "plain tag");
	CHECK(prefixed.ok && prefixed.major == 0 && prefixed.minor == 2 && prefixed.bug == 0, "v prefix");
	CHECK(!hg::ParseReleaseTag("v1.2", 4).ok, "short tag");
	CHECK(!hg::ParseReleaseTag("v1.2.3-beta", 10).ok, "prerelease tag");
	CHECK(!hg::ParseReleaseTag("01.2.3", 6).ok, "leading zero");
	CHECK(hg::CompareRelease(prefixed, plain) < 0, "0.2.0 is older than 1.2.3");

	hg::ReleaseVersion local;
	local.major = 0;
	local.minor = 1;
	local.bug = 0;
	local.ok = true;
	const char *body =
		"{\"html_url\":\"https://github.com/palf-gh\",\"tag_name\":\"v0.2.0\","
		"\"html_url\":\"https:\\/\\/github.com\\/palf-gh\\/HaloGlow\\/releases\\/tag\\/v0.2.0\"}";
	const hg::ReleaseInfo newer = hg::InterpretReleaseBody(200, body, std::char_traits<char>::length(body), local);
	CHECK(newer.status == hg::ReleaseInfo::Status::Available, "newer release");
	CHECK(newer.remote.minor == 2, "remote minor");
	CHECK(hg::IsHaloGlowReleasePage(newer.page), "release page");

	const hg::ReleaseInfo missing = hg::InterpretReleaseBody(404, "", 0, local);
	CHECK(missing.status == hg::ReleaseInfo::Status::NoRelease, "no release");
	const char sameBody[] =
		"{\"tag_name\":\"v0.1.0\",\"html_url\":\"https://github.com/palf-gh/HaloGlow/releases/tag/v0.1.0\"}";
	const hg::ReleaseInfo same =
		hg::InterpretReleaseBody(200, sameBody, sizeof(sameBody) - 1, local);
	CHECK(same.status == hg::ReleaseInfo::Status::Current, "same version");
}

} // namespace

int main(int argc, char **argv)
{
	const bool shape = !(argc > 1 && std::string(argv[1]) == "--no-shape");
	TestLuminance();
	TestExtraction();
	TestTone();
	TestVersionedComposite();
	TestScaleFalloff();
	TestReleaseTag();
	TestDcGain();
	TestTiling();
	TestBypass();
	TestLayerBoundsLightSource();
	if (shape) {
		TestShape();
		TestContinuity();
		TestThinAxis();
	}
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
