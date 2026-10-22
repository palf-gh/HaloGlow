/*
	HG_Core.cpp

	Plan construction and the FP32 CPU reference pyramid (spec §5–§9).
	Every backend consumes the Plan built here, so the level schedule, domains
	and coefficients exist in exactly one place.
*/

#include "HG_Core.h"
#include "HG_Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hg {

//-----------------------------------------------------------------------------
// Fixed kernels
//-----------------------------------------------------------------------------
// 13-tap downsample (§7.2). Each tap sits on a half-texel position, i.e. it is a
// 2x2 box average, so the kernel is exactly this 6x6 texel table over input
// indices 2u-2 .. 2u+3 (weights /512, total 1). Not separable (rank 2).
const float kDown2D[6][6] = {
	{ 4.f / 512.f,  4.f / 512.f,  8.f / 512.f,  8.f / 512.f,  4.f / 512.f, 4.f / 512.f },
	{ 4.f / 512.f, 20.f / 512.f, 24.f / 512.f, 24.f / 512.f, 20.f / 512.f, 4.f / 512.f },
	{ 8.f / 512.f, 24.f / 512.f, 32.f / 512.f, 32.f / 512.f, 24.f / 512.f, 8.f / 512.f },
	{ 8.f / 512.f, 24.f / 512.f, 32.f / 512.f, 32.f / 512.f, 24.f / 512.f, 8.f / 512.f },
	{ 4.f / 512.f, 20.f / 512.f, 24.f / 512.f, 24.f / 512.f, 20.f / 512.f, 4.f / 512.f },
	{ 4.f / 512.f,  4.f / 512.f,  8.f / 512.f,  8.f / 512.f,  4.f / 512.f, 4.f / 512.f },
};
// Single-axis projection: taps 1/8,1/4,1/4,1/4,1/8 at -2..2 around 2u+0.5.
const float kDown1D[6] = { 1.f / 16.f, 3.f / 16.f, 4.f / 16.f, 4.f / 16.f, 3.f / 16.f, 1.f / 16.f };
// 3-tap tent [1,2,1]/4 per axis, sampled bilinearly at (x-0.5)/2 (§7.2, §7.3).
const float kUpEven[4] = { 1.f / 16.f, 5.f / 16.f, 7.f / 16.f, 3.f / 16.f };
const float kUpOdd[4] = { 3.f / 16.f, 7.f / 16.f, 5.f / 16.f, 1.f / 16.f };

// Half-octave companion of every level (§7.3): G_j also adds c_j Bin5(D_j).
const float kBinomial5[5] = { 1.f / 16.f, 4.f / 16.f, 6.f / 16.f, 4.f / 16.f, 1.f / 16.f };

const float kLocalBasisSigma[kLocalBasisCount] = { 0.5f, 0.75f, 1.0f, 1.5f };

//-----------------------------------------------------------------------------
// Rect helpers
//-----------------------------------------------------------------------------
Rect Union(const Rect &a, const Rect &b)
{
	if (a.Empty()) return b;
	if (b.Empty()) return a;
	return { std::min(a.x0, b.x0), std::min(a.y0, b.y0),
			 std::max(a.x1, b.x1), std::max(a.y1, b.y1) };
}

Rect Intersect(const Rect &a, const Rect &b)
{
	Rect r { std::max(a.x0, b.x0), std::max(a.y0, b.y0),
			 std::min(a.x1, b.x1), std::min(a.y1, b.y1) };
	if (r.Empty()) return {};
	return r;
}

Rect Inflate(const Rect &r, int dx, int dy)
{
	if (r.Empty()) return r;
	return { r.x0 - dx, r.y0 - dy, r.x1 + dx, r.y1 + dy };
}

namespace {

constexpr int kHuge = 1 << 27;
const Rect kHugeRect { -kHuge, -kHuge, kHuge, kHuge };

// D_j domain from D_{j-1}: output u reads input 2u-2 .. 2u+3.
void DownDomainAxis(int lo, int hi, int &olo, int &ohi)
{
	olo = CeilDiv2(lo - 3);
	ohi = FloorDiv2(hi + 1) + 1;
}

// Fine indices read when evaluating coarse [lo,hi).
void DownFootprintAxis(int lo, int hi, int &olo, int &ohi)
{
	olo = 2 * lo - 2;
	ohi = 2 * hi + 2;
}

// Coarse indices read when upsampling into fine [lo,hi).
void UpNeedAxis(int lo, int hi, int &olo, int &ohi)
{
	olo = FloorDiv2(lo) - 2;
	ohi = FloorDiv2(hi - 1) + 3;
}

// Fine indices influenced by coarse [lo,hi).
void UpReachAxis(int lo, int hi, int &olo, int &ohi)
{
	olo = 2 * lo - 3;
	ohi = 2 * hi + 3;
}

template <void (*F)(int, int, int &, int &)>
Rect MapRect(const Rect &r, bool alongX, bool alongY)
{
	if (r.Empty()) return r;
	Rect o = r;
	if (alongX) F(r.x0, r.x1, o.x0, o.x1);
	if (alongY) F(r.y0, r.y1, o.y0, o.y1);
	return o;
}

float ClampFinite(float v, float lo, float hi, float fallback)
{
	if (!std::isfinite(v)) return fallback;
	return std::min(std::max(v, lo), hi);
}

double Smoothstep01(double v)
{
	v = std::min(std::max(v, 0.0), 1.0);
	return v * v * (3.0 - 2.0 * v);
}

void FillPixelParams(const Settings &s, float gain, HG_PixelParams &p)
{
	std::memset(&p, 0, sizeof(p));
	p.lumR = s.luminance[0];
	p.lumG = s.luminance[1];
	p.lumB = s.luminance[2];
	p.threshold = s.threshold / 100.0f;
	p.knee = p.threshold * (s.softness / 100.0f);
	p.gain = gain;
	p.saturation = s.saturation / 100.0f;
	p.gamma = s.gamma;
	p.invGamma = 1.0f / s.gamma;
	p.colourStrength = s.colourStrength / 100.0f;
	p.colour1R = s.colour1[0];
	p.colour1G = s.colour1[1];
	p.colour1B = s.colour1[2];
	p.colour2R = s.colour2[0];
	p.colour2G = s.colour2[1];
	p.colour2B = s.colour2[2];
	p.glowOpacity = s.glowOpacity / 100.0f;
	p.sourceOpacity = s.sourceOpacity / 100.0f;
	p.glowOnly = s.glowOnly ? 1 : 0;
}

void BuildLocalKernel(const Coefficients &c, double sigmaScaleX, double sigmaScaleY,
					  float out[kLocalSize * kLocalSize], bool &hasSpread)
{
	std::fill(out, out + kLocalSize * kLocalSize, 0.0f);
	out[kLocalRadius * kLocalSize + kLocalRadius] = c.a[0];
	hasSpread = false;
	for (int h = 0; h < kLocalBasisCount; ++h) {
		if (!(c.b[h] > 0.0f)) continue;
		hasSpread = true;
		float hx[kLocalSize];
		float hy[kLocalSize];
		IntegratedGaussian(kLocalBasisSigma[h] * sigmaScaleX, hx);
		IntegratedGaussian(kLocalBasisSigma[h] * sigmaScaleY, hy);
		for (int y = 0; y < kLocalSize; ++y)
			for (int x = 0; x < kLocalSize; ++x)
				out[y * kLocalSize + x] += c.b[h] * hx[x] * hy[y];
	}
}

void PlanPyramidDomains(PyramidPlan &pyr, const Rect &inputRect, const Rect &outputRect)
{
	const int L = pyr.levelCount;
	Rect s[kMaxLevels + 1];
	Rect z[kMaxLevels + 2];
	Rect n[kMaxLevels + 1];
	s[0] = inputRect;
	n[0] = outputRect;
	for (int j = 1; j <= L; ++j) {
		LevelPlan &lv = pyr.level[j];
		lv.halveX = j > pyr.delayX;
		lv.halveY = j > pyr.delayY;
		lv.blurX = lv.halveX;	// halved at least once by level j
		lv.blurY = lv.halveY;
		s[j] = MapRect<DownDomainAxis>(s[j - 1], lv.halveX, lv.halveY);
		n[j] = MapRect<UpNeedAxis>(n[j - 1], lv.halveX, lv.halveY);
	}
	z[L + 1] = Rect {};
	for (int j = L; j >= 1; --j) {
		const LevelPlan &lv = pyr.level[j];
		Rect reach = (j < L)
			? MapRect<UpReachAxis>(z[j + 1], pyr.level[j + 1].halveX, pyr.level[j + 1].halveY)
			: Rect {};
		z[j] = Union(Inflate(s[j], lv.blurX ? 2 : 0, lv.blurY ? 2 : 0), reach);
		pyr.level[j].gDomain = Intersect(n[j], z[j]);
	}
	// D_j is needed for its own G term and for the next level's downsample.
	Rect need {};
	for (int j = L; j >= 1; --j) {
		const LevelPlan &lv = pyr.level[j];
		Rect want = Inflate(lv.gDomain, lv.blurX ? 2 : 0, lv.blurY ? 2 : 0);
		if (j < L)
			want = Union(want, MapRect<DownFootprintAxis>(need, pyr.level[j + 1].halveX,
														  pyr.level[j + 1].halveY));
		need = Intersect(want, s[j]);
		pyr.level[j].dDomain = need;
	}
	// Level 0 entries: what the first downsample reads, and the glow reach.
	pyr.level[0].dDomain = (L >= 1)
		? Intersect(MapRect<DownFootprintAxis>(pyr.level[1].dDomain, pyr.level[1].halveX,
											   pyr.level[1].halveY), inputRect)
		: Rect {};
	pyr.level[0].gDomain = (L >= 1)
		? MapRect<UpReachAxis>(z[1], pyr.level[1].halveX, pyr.level[1].halveY)
		: Rect {};
}

} // namespace

//-----------------------------------------------------------------------------
// Settings
//-----------------------------------------------------------------------------
Settings Sanitise(const Settings &in)
{
	const Settings d;
	Settings s = in;
	s.intensity = ClampFinite(in.intensity, 0.0f, 8.0f, d.intensity);
	s.size = ClampFinite(in.size, 0.0f, 512.0f, d.size);
	s.threshold = ClampFinite(in.threshold, 0.0f, 1600.0f, d.threshold);
	s.softness = ClampFinite(in.softness, 0.0f, 100.0f, d.softness);
	s.width = ClampFinite(in.width, 0.0f, 400.0f, d.width);
	s.height = ClampFinite(in.height, 0.0f, 400.0f, d.height);
	s.falloff = ClampFinite(in.falloff, 0.0f, 100.0f, d.falloff);
	s.gamma = ClampFinite(in.gamma, 0.1f, 3.0f, d.gamma);
	s.saturation = ClampFinite(in.saturation, 0.0f, 200.0f, d.saturation);
	s.colourStrength = ClampFinite(in.colourStrength, 0.0f, 100.0f, d.colourStrength);
	s.glowOpacity = ClampFinite(in.glowOpacity, 0.0f, 100.0f, d.glowOpacity);
	s.sourceOpacity = ClampFinite(in.sourceOpacity, 0.0f, 100.0f, d.sourceOpacity);
	for (int i = 0; i < 3; ++i) {
		const float big = std::numeric_limits<float>::max();
		s.colour1[i] = ClampFinite(in.colour1[i], 0.0f, big, 0.0f);
		s.colour2[i] = ClampFinite(in.colour2[i], 0.0f, big, 0.0f);
		s.luminance[i] = ClampFinite(in.luminance[i], 0.0f, 1.0f, d.luminance[i]);
	}
	const float sum = s.luminance[0] + s.luminance[1] + s.luminance[2];
	if (sum > 0.0f) {
		for (float &v : s.luminance) v /= sum;
	} else {
		std::copy(d.luminance, d.luminance + 3, s.luminance);
	}
	return s;
}

void LuminanceCoefficients(LuminanceSpace space, float out[3])
{
	// Y rows of RGB->XYZ from each standard's xy primaries and white (§3.12).
	static const double kRec709[3] = { 0.2126390, 0.7151687, 0.0721923 };
	static const double kP3[3] = { 0.2289746, 0.6917385, 0.0792869 };
	static const double kAP1[3] = { 0.2722287, 0.6740818, 0.0536895 };
	const double *src = kRec709;
	if (space == LuminanceSpace::DisplayP3) src = kP3;
	else if (space == LuminanceSpace::ACEScg) src = kAP1;
	const double sum = src[0] + src[1] + src[2];
	for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(src[i] / sum);
}

void IntegratedGaussian(double sigma, float out[kLocalSize])
{
	if (!(sigma > 1.0e-6)) {
		std::fill(out, out + kLocalSize, 0.0f);
		out[kLocalRadius] = 1.0f;
		return;
	}
	double w[kLocalSize];
	double sum = 0.0;
	const double k = 1.0 / (std::sqrt(2.0) * sigma);
	for (int i = 0; i < kLocalSize; ++i) {
		const double x = i - kLocalRadius;
		w[i] = 0.5 * (std::erf((x + 0.5) * k) - std::erf((x - 0.5) * k));
		sum += w[i];
	}
	for (int i = 0; i < kLocalSize; ++i) out[i] = static_cast<float>(w[i] / sum);
}

//-----------------------------------------------------------------------------
// Plan
//-----------------------------------------------------------------------------
namespace {

Plan BuildPlanImpl(const Settings &raw, const RenderScale &scale, const Rect &inputRect,
				   const Rect &outputRect, const Coefficients *override)
{
	const Settings s = Sanitise(raw);
	Plan plan;
	plan.inputRect = inputRect;
	plan.outputRect = outputRect;

	const double wx = s.width / 100.0;
	const double wy = s.height / 100.0;
	const double rmaxFull = std::max(s.size * wx, s.size * wy);
	const double gain = s.intensity * Smoothstep01(rmaxFull);
	FillPixelParams(s, static_cast<float>(gain), plan.pixel);

	const double par = (scale.pixelAspect > 0.0 && std::isfinite(scale.pixelAspect))
		? scale.pixelAspect : 1.0;
	plan.radiusX = s.size * wx * scale.downsampleX / par;
	plan.radiusY = s.size * wy * scale.downsampleY;

	plan.glowActive = gain > 0.0 && s.glowOpacity > 0.0f && rmaxFull > 0.0;
	if (!plan.glowActive) return plan;

	const bool xMajor = plan.radiusX >= plan.radiusY;
	const double major = xMajor ? plan.radiusX : plan.radiusY;
	const double minor = xMajor ? plan.radiusY : plan.radiusX;

	plan.coefficients = override ? *override : LookupCoefficients(std::log2(major), s.falloff);
	const Coefficients &c = plan.coefficients;

	const double ratio = minor > 0.0 ? minor / major : 0.0;
	BuildLocalKernel(c, xMajor ? 1.0 : ratio, xMajor ? ratio : 1.0,
					 plan.localKernel, plan.hasLocalSpread);

	const int top = c.topLevel;
	if (top >= 1) {
		// Thin-axis delay (§7.2): integer Delta keeps the per-level aspect at
		// 2^Delta; a fractional Delta blends the two neighbouring schedules.
		const double delta = minor > 0.0 ? std::log2(major / minor)
										 : std::numeric_limits<double>::infinity();
		int delays[2] = { top + 1, top + 1 };
		float weights[2] = { 1.0f, 0.0f };
		int count = 1;
		if (std::isfinite(delta) && delta < top) {
			const int d = static_cast<int>(std::floor(delta));
			const double t = delta - d;
			delays[0] = d;
			if (t > 1.0e-6) {
				delays[1] = d + 1;
				weights[0] = static_cast<float>(1.0 - t);
				weights[1] = static_cast<float>(t);
				count = 2;
			}
		}
		plan.pyramidCount = count;
		for (int p = 0; p < count; ++p) {
			PyramidPlan &pyr = plan.pyramid[p];
			pyr.weight = weights[p];
			pyr.levelCount = top;
			pyr.delayX = xMajor ? 0 : std::min(delays[p], top + 1);
			pyr.delayY = xMajor ? std::min(delays[p], top + 1) : 0;
			PlanPyramidDomains(pyr, inputRect, outputRect);
		}
	}

	Rect reach = plan.hasLocalSpread ? Inflate(inputRect, kLocalRadius, kLocalRadius) : inputRect;
	for (int p = 0; p < plan.pyramidCount; ++p)
		reach = Union(reach, plan.pyramid[p].level[0].gDomain);
	plan.glowRect = reach;
	return plan;
}

} // namespace

Plan BuildPlan(const Settings &settings, const RenderScale &scale,
			   const Rect &inputRect, const Rect &outputRect)
{
	return BuildPlanImpl(settings, scale, inputRect, outputRect, nullptr);
}

Plan BuildPlanWithCoefficients(const Settings &settings, const RenderScale &scale,
							   const Rect &inputRect, const Rect &outputRect,
							   const Coefficients &coefficients)
{
	return BuildPlanImpl(settings, scale, inputRect, outputRect, &coefficients);
}

Rect MaxResultRect(const Settings &settings, const RenderScale &scale, const Rect &lightRect,
				   const Rect &sourceRect)
{
	const Plan plan = BuildPlan(settings, scale, lightRect, kHugeRect);
	const Settings s = Sanitise(settings);
	Rect r {};
	if (!s.glowOnly && s.sourceOpacity > 0.0f) r = sourceRect;
	if (plan.glowActive && !lightRect.Empty()) r = Union(r, plan.glowRect);
	if (r.Empty()) r = sourceRect;
	return r;
}

Rect RequiredInputRect(const Settings &settings, const RenderScale &scale, const Rect &outputRect,
					   const Rect &lightLimit)
{
	const Plan plan = BuildPlan(settings, scale, kHugeRect, outputRect);
	if (!plan.glowActive) return outputRect;
	Rect light = plan.hasLocalSpread ? Inflate(outputRect, kLocalRadius, kLocalRadius) : outputRect;
	for (int p = 0; p < plan.pyramidCount; ++p)
		light = Union(light, plan.pyramid[p].level[0].dDomain);
	return Union(outputRect, Intersect(light, lightLimit));
}

Rect RequiredInputRect(const Settings &settings, const RenderScale &scale, const Rect &outputRect)
{
	return RequiredInputRect(settings, scale, outputRect, kHugeRect);
}

//-----------------------------------------------------------------------------
// Images
//-----------------------------------------------------------------------------
void Image::Reset(const Rect &r)
{
	rect = r.Empty() ? Rect {} : r;
	pixels.assign(std::size_t(rect.Width()) * rect.Height(), HG_RGBA { 0.f, 0.f, 0.f, 0.f });
}

HG_RGBA Image::At(int x, int y) const
{
	if (x < rect.x0 || y < rect.y0 || x >= rect.x1 || y >= rect.y1)
		return HG_RGBA { 0.f, 0.f, 0.f, 0.f };
	return pixels[std::size_t(y - rect.y0) * rect.Width() + (x - rect.x0)];
}

namespace {

inline void Mad(HG_RGBA &acc, float w, const HG_RGBA &v)
{
	acc.r += w * v.r;
	acc.g += w * v.g;
	acc.b += w * v.b;
	acc.a += w * v.a;
}

void Downsample(const Image &src, bool halveX, bool halveY, const Rect &dst, Image &out)
{
	out.Reset(dst);
	if (dst.Empty()) return;
	ParallelRows(dst.y0, dst.y1, [&](int v) {
		HG_RGBA *row = out.Row(v);
		for (int u = dst.x0; u < dst.x1; ++u) {
			HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
			if (halveX && halveY) {
				for (int j = 0; j < 6; ++j)
					for (int i = 0; i < 6; ++i)
						Mad(acc, kDown2D[j][i], src.At(2 * u - 2 + i, 2 * v - 2 + j));
			} else if (halveX) {
				for (int i = 0; i < 6; ++i) Mad(acc, kDown1D[i], src.At(2 * u - 2 + i, v));
			} else if (halveY) {
				for (int j = 0; j < 6; ++j) Mad(acc, kDown1D[j], src.At(u, 2 * v - 2 + j));
			} else {
				acc = src.At(u, v);
			}
			row[u - dst.x0] = acc;
		}
	});
}

// Upsample tap set along one axis for fine index x: start index and weights.
inline void UpTaps(int x, bool halve, int &first, const float *&w, int &count)
{
	if (!halve) {
		static const float one[1] = { 1.0f };
		first = x;
		w = one;
		count = 1;
		return;
	}
	const int m = FloorDiv2(x);
	if (x - 2 * m == 0) {
		first = m - 2;
		w = kUpEven;
	} else {
		first = m - 1;
		w = kUpOdd;
	}
	count = 4;
}

inline HG_RGBA UpsampleAt(const Image &coarse, bool halveX, bool halveY, int x, int y)
{
	int fx, fy, cx, cy;
	const float *wx;
	const float *wy;
	UpTaps(x, halveX, fx, wx, cx);
	UpTaps(y, halveY, fy, wy, cy);
	HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
	for (int j = 0; j < cy; ++j) {
		HG_RGBA rowAcc { 0.f, 0.f, 0.f, 0.f };
		for (int i = 0; i < cx; ++i) Mad(rowAcc, wx[i], coarse.At(fx + i, fy + j));
		Mad(acc, wy[j], rowAcc);
	}
	return acc;
}

// Separable [1,4,6,4,1]/16 along the axes that level has halved.
inline HG_RGBA Binomial5At(const Image &img, bool alongX, bool alongY, int x, int y)
{
	const int rx = alongX ? 2 : 0;
	const int ry = alongY ? 2 : 0;
	static const float one[1] = { 1.0f };
	const float *wx = alongX ? kBinomial5 : one;
	const float *wy = alongY ? kBinomial5 : one;
	HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
	for (int j = -ry; j <= ry; ++j) {
		HG_RGBA rowAcc { 0.f, 0.f, 0.f, 0.f };
		for (int i = -rx; i <= rx; ++i) Mad(rowAcc, wx[i + rx], img.At(x + i, y + j));
		Mad(acc, wy[j + ry], rowAcc);
	}
	return acc;
}

// Local term W * E with W = a0 delta + sum_h b_h hx_h (x) hy_h, evaluated
// separably per basis (the same W the GPU applies as a direct 13x13 tap set).
void LocalTerm(const Plan &plan, const Image &e, const Rect &dst, Image &out)
{
	out.Reset(dst);
	if (dst.Empty()) return;
	const Coefficients &c = plan.coefficients;
	const float a0 = c.a[0];
	ParallelRows(dst.y0, dst.y1, [&](int y) {
		HG_RGBA *row = out.Row(y);
		for (int x = dst.x0; x < dst.x1; ++x) {
			HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
			Mad(acc, a0, e.At(x, y));
			row[x - dst.x0] = acc;
		}
	});
	if (!plan.hasLocalSpread) return;

	const bool xMajor = plan.radiusX >= plan.radiusY;
	const double major = std::max(plan.radiusX, plan.radiusY);
	const double minor = std::min(plan.radiusX, plan.radiusY);
	const double ratio = major > 0.0 ? minor / major : 0.0;
	const Rect mid { dst.x0, dst.y0 - kLocalRadius, dst.x1, dst.y1 + kLocalRadius };
	Image tmp;
	for (int h = 0; h < kLocalBasisCount; ++h) {
		if (!(c.b[h] > 0.0f)) continue;
		float hx[kLocalSize];
		float hy[kLocalSize];
		IntegratedGaussian(kLocalBasisSigma[h] * (xMajor ? 1.0 : ratio), hx);
		IntegratedGaussian(kLocalBasisSigma[h] * (xMajor ? ratio : 1.0), hy);
		tmp.Reset(mid);
		ParallelRows(mid.y0, mid.y1, [&](int y) {
			HG_RGBA *row = tmp.Row(y);
			for (int x = mid.x0; x < mid.x1; ++x) {
				HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
				for (int i = 0; i < kLocalSize; ++i)
					Mad(acc, hx[i], e.At(x + i - kLocalRadius, y));
				row[x - mid.x0] = acc;
			}
		});
		const float bh = c.b[h];
		ParallelRows(dst.y0, dst.y1, [&](int y) {
			HG_RGBA *row = out.Row(y);
			for (int x = dst.x0; x < dst.x1; ++x) {
				HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
				for (int j = 0; j < kLocalSize; ++j)
					Mad(acc, hy[j], tmp.At(x, y + j - kLocalRadius));
				Mad(row[x - dst.x0], bh, acc);
			}
		});
	}
}

} // namespace

void FilterEmission(const Plan &plan, const Image &emission, Image &filtered)
{
	const Rect &out = plan.outputRect;
	LocalTerm(plan, emission, out, filtered);
	if (out.Empty()) return;

	for (int p = 0; p < plan.pyramidCount; ++p) {
		const PyramidPlan &pyr = plan.pyramid[p];
		const int L = pyr.levelCount;
		std::vector<Image> d(L + 1);
		for (int j = 1; j <= L; ++j) {
			const Image &prev = (j == 1) ? emission : d[j - 1];
			Downsample(prev, pyr.level[j].halveX, pyr.level[j].halveY, pyr.level[j].dDomain, d[j]);
		}
		Image g, gPrev;
		for (int j = L; j >= 1; --j) {
			const LevelPlan &lv = pyr.level[j];
			g.Reset(lv.gDomain);
			const float aj = plan.coefficients.a[j];
			const float cj = plan.coefficients.c[j];
			const bool hasCoarse = j < L;
			const bool hx = hasCoarse && pyr.level[j + 1].halveX;
			const bool hy = hasCoarse && pyr.level[j + 1].halveY;
			ParallelRows(lv.gDomain.y0, lv.gDomain.y1, [&](int y) {
				HG_RGBA *row = g.Row(y);
				for (int x = lv.gDomain.x0; x < lv.gDomain.x1; ++x) {
					HG_RGBA acc { 0.f, 0.f, 0.f, 0.f };
					if (aj > 0.0f) Mad(acc, aj, d[j].At(x, y));
					if (cj > 0.0f) Mad(acc, cj, Binomial5At(d[j], lv.blurX, lv.blurY, x, y));
					if (hasCoarse) Mad(acc, 1.0f, UpsampleAt(gPrev, hx, hy, x, y));
					row[x - lv.gDomain.x0] = acc;
				}
			});
			std::swap(g, gPrev);
		}
		// gPrev now holds G_1.
		const float w = pyr.weight;
		const bool hx = pyr.level[1].halveX;
		const bool hy = pyr.level[1].halveY;
		ParallelRows(out.y0, out.y1, [&](int y) {
			HG_RGBA *row = filtered.Row(y);
			for (int x = out.x0; x < out.x1; ++x)
				Mad(row[x - out.x0], w, UpsampleAt(gPrev, hx, hy, x, y));
		});
	}
}

void RenderCPU(const Plan &plan, const Image &source, Image &output)
{
	const Rect &out = plan.outputRect;
	output.Reset(out);
	if (out.Empty()) return;

	Image filtered;
	if (plan.glowActive) {
		Rect eRect = plan.hasLocalSpread ? Inflate(out, kLocalRadius, kLocalRadius) : out;
		for (int p = 0; p < plan.pyramidCount; ++p)
			eRect = Union(eRect, plan.pyramid[p].level[0].dDomain);
		eRect = Intersect(eRect, plan.inputRect);
		Image emission;
		emission.Reset(eRect);
		ParallelRows(eRect.y0, eRect.y1, [&](int y) {
			HG_RGBA *row = emission.Row(y);
			for (int x = eRect.x0; x < eRect.x1; ++x)
				row[x - eRect.x0] = hg_extract(plan.pixel, source.At(x, y));
		});
		FilterEmission(plan, emission, filtered);
	}

	const HG_RGBA zero { 0.f, 0.f, 0.f, 0.f };
	ParallelRows(out.y0, out.y1, [&](int y) {
		HG_RGBA *row = output.Row(y);
		for (int x = out.x0; x < out.x1; ++x) {
			HG_RGBA glow = zero;
			if (plan.glowActive) glow = hg_glow_from_filtered(plan.pixel, filtered.At(x, y));
			row[x - out.x0] = hg_composite(plan.pixel, source.At(x, y), glow);
		}
	});
}

} // namespace hg
