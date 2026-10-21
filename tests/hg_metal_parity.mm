/*
	hg_metal_parity.mm

	CPU FP32 reference vs the Metal backend on the default device, without
	After Effects. Compiles the embedded kernel source with the same options as
	HG_Metal.mm (fast math off) and runs the shared GPU driver on the cases of
	tests/hg_gpu_parity.cpp. Tolerance per channel: 1e-4 + 1e-3 * |reference|.
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "HG_Core.h"
#include "HG_GpuRender.h"
#include "HG_Kernel_metal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

class MetalDevice : public hg::gpu::Backend {
public:
	bool Init()
	{
		device_ = MTLCreateSystemDefaultDevice();
		if (!device_) return false;
		queue_ = [device_ newCommandQueue];
		NSString *src = [[NSString alloc] initWithBytes:kHG_MetalSource length:kHG_MetalSource_size
											   encoding:NSUTF8StringEncoding];
		MTLCompileOptions *options = [MTLCompileOptions new];
#if defined(__MAC_15_0) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_15_0
		if (@available(macOS 15.0, *)) {
			options.mathMode = MTLMathModeSafe;
		} else
#endif
		{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			options.fastMathEnabled = NO;
#pragma clang diagnostic pop
		}
		NSError *error = nil;
		id<MTLLibrary> lib = [device_ newLibraryWithSource:src options:options error:&error];
		if (!lib) {
			std::printf("Metal library build failed:\n%s\n", error.localizedDescription.UTF8String);
			return false;
		}
		down_ = [device_ newComputePipelineStateWithFunction:[lib newFunctionWithName:@"hg_down"] error:&error];
		accum_ = [device_ newComputePipelineStateWithFunction:[lib newFunctionWithName:@"hg_accum"] error:&error];
		final_ = [device_ newComputePipelineStateWithFunction:[lib newFunctionWithName:@"hg_final"] error:&error];
		return down_ && accum_ && final_;
	}
	std::string Name() const { return std::string("Metal (") + device_.name.UTF8String + ")"; }

	void *Allocate(std::size_t bytes) override
	{
		id<MTLBuffer> b = [device_ newBufferWithLength:bytes options:MTLResourceStorageModeShared];
		return b ? (void *)CFBridgingRetain(b) : nullptr;
	}
	void Free(void *mem) override
	{
		if (mem) CFBridgingRelease(mem);
	}
	bool Down(const void *s, void *d, const HG_DownArgs &a) override
	{
		return Launch(down_, a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:B(s) offset:0 atIndex:0];
			[e setBuffer:B(d) offset:0 atIndex:1];
			[e setBytes:&a length:sizeof(a) atIndex:2];
		});
	}
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		return Launch(accum_, a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:B(d) offset:0 atIndex:0];
			[e setBuffer:B(c) offset:0 atIndex:1];
			[e setBuffer:B(o) offset:0 atIndex:2];
			[e setBytes:&a length:sizeof(a) atIndex:3];
		});
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		return Launch(final_, a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:B(s) offset:0 atIndex:0];
			[e setBuffer:B(d) offset:0 atIndex:1];
			[e setBuffer:B(g0) offset:0 atIndex:2];
			[e setBuffer:B(g1) offset:0 atIndex:3];
			[e setBytes:&a length:sizeof(a) atIndex:4];
		});
	}
	bool Finish() override
	{
		if (!last_) return true;
		[last_ waitUntilCompleted];
		const bool ok = last_.status == MTLCommandBufferStatusCompleted;
		last_ = nil;
		return ok;
	}
	static id<MTLBuffer> B(const void *p) { return (__bridge id<MTLBuffer>)p; }

private:
	bool Launch(id<MTLComputePipelineState> p, const HG_Buf &dst, void (^bind)(id<MTLComputeCommandEncoder>))
	{
		id<MTLCommandBuffer> cb = [queue_ commandBuffer];
		id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
		[e setComputePipelineState:p];
		bind(e);
		const NSUInteger side = p.maxTotalThreadsPerThreadgroup >= 256 ? 16 : 8;
		[e dispatchThreadgroups:MTLSizeMake((dst.w + side - 1) / side, (dst.h + side - 1) / side, 1)
		  threadsPerThreadgroup:MTLSizeMake(side, side, 1)];
		[e endEncoding];
		[cb commit];
		last_ = cb;
		return true;
	}
	id<MTLDevice> device_ = nil;
	id<MTLCommandQueue> queue_ = nil;
	id<MTLComputePipelineState> down_ = nil, accum_ = nil, final_ = nil;
	id<MTLCommandBuffer> last_ = nil;
};

struct Case {
	std::string name;
	hg::Settings s;
};

int RunCase(MetalDevice &dev, const Case &c, unsigned seed)
{
	const hg::Rect in { -23, 11, 197, 163 };
	std::vector<float> bgra(std::size_t(in.Width()) * in.Height() * 4);
	for (std::size_t i = 0; i < bgra.size() / 4; ++i) {
		seed = seed * 1664525u + 1013904223u;
		const float v = ((seed >> 8) & 0xffff) / 65535.0f;
		const bool bright = (seed % 53u) == 0u;
		bgra[i * 4 + 0] = bright ? 12.0f * v : 0.6f * v;
		bgra[i * 4 + 1] = bright ? 30.0f : v;
		bgra[i * 4 + 2] = bright ? 65504.0f * v : v * v;
		bgra[i * 4 + 3] = (seed % 7u) == 0u ? 0.0f : ((seed % 5u) == 0u ? 0.37f : 1.0f);
		if ((seed % 997u) == 0u) bgra[i * 4 + 2] = NAN;
	}
	const hg::Rect out = hg::MaxResultRect(c.s, hg::RenderScale {}, in);
	const hg::Plan plan = hg::BuildPlan(c.s, hg::RenderScale {}, in, out);
	hg::Image src;
	src.Reset(in);
	for (std::size_t i = 0; i < src.pixels.size(); ++i) {
		const HG_RGBA s = hg_sanitise(HG_RGBA { bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4 + 0], bgra[i * 4 + 3] });
		src.pixels[i] = HG_RGBA { s.r * s.a, s.g * s.a, s.b * s.a, s.a };
	}
	hg::Image ref;
	hg::RenderCPU(plan, src, ref);

	void *dSrc = dev.Allocate(bgra.size() * sizeof(float));
	void *dDst = dev.Allocate(std::size_t(out.Width()) * out.Height() * 16);
	std::memcpy(MetalDevice::B(dSrc).contents, bgra.data(), bgra.size() * sizeof(float));
	const hg::gpu::World ws { dSrc, in.x0, in.y0, in.Width(), in.Height(), in.Width() };
	const hg::gpu::World wd { dDst, out.x0, out.y0, out.Width(), out.Height(), out.Width() };
	const hg::gpu::Status st = hg::gpu::Render(plan, dev, ws, wd);
	std::vector<float> got(std::size_t(out.Width()) * out.Height() * 4);
	std::memcpy(got.data(), MetalDevice::B(dDst).contents, got.size() * sizeof(float));
	dev.Free(dSrc);
	dev.Free(dDst);
	if (st != hg::gpu::Status::Ok) {
		std::printf("FAIL %s: GPU status %d\n", c.name.c_str(), static_cast<int>(st));
		return 1;
	}
	double worst = 0.0;
	long bad = 0, tinyAlpha = 0;
	for (int y = out.y0; y < out.y1; ++y)
		for (int x = out.x0; x < out.x1; ++x) {
			const HG_RGBA p = ref.At(x, y);
			float r = 0, g = 0, b = 0;
			if (!plan.glowActive) {
				const int lx = x - in.x0, ly = y - in.y0;
				if (lx >= 0 && ly >= 0 && lx < in.Width() && ly < in.Height() && !c.s.glowOnly) {
					const std::size_t k = (std::size_t(ly) * in.Width() + lx) * 4;
					b = bgra[k];
					g = bgra[k + 1];
					r = bgra[k + 2];
				}
			} else if (p.a > 0.0f) {
				const double big = 3.402823466e38;
				r = static_cast<float>(std::min(big, double(p.r) / p.a));
				g = static_cast<float>(std::min(big, double(p.g) / p.a));
				b = static_cast<float>(std::min(big, double(p.b) / p.a));
			}
			const float refv[4] = { b, g, r, plan.glowActive ? p.a : 0.0f };
			const std::size_t k = (std::size_t(y - out.y0) * out.Width() + (x - out.x0)) * 4;
			if (plan.glowActive && p.a > 0.0f && p.a < 1e-30f) {
				++tinyAlpha;
				if (!(std::fabs(double(got[k + 3]) - p.a) <= 1e-4)) ++bad;
				continue;
			}
			for (int ch = 0; ch < (plan.glowActive ? 4 : 3); ++ch) {
				const double a = refv[ch], v = got[k + ch];
				if (std::isnan(a) && std::isnan(v)) continue;
				const double err = std::fabs(a - v);
				const double tol = 1e-4 + 1e-3 * std::fabs(a);
				worst = std::max(worst, err / tol);
				if (!(err <= tol)) {
					if (bad < 3) std::printf("  mismatch (%d,%d) ch%d ref=%g gpu=%g\n", x, y, ch, a, v);
					++bad;
				}
			}
		}
	std::printf("  %-28s worst/tol=%.3f bad=%ld tiny-alpha=%ld\n", c.name.c_str(), worst, bad, tinyAlpha);
	return bad == 0 ? 0 : 1;
}

} // namespace

int main()
{
	@autoreleasepool {
		MetalDevice dev;
		if (!dev.Init()) {
			std::printf("no usable Metal device; skipped\n");
			return 1;
		}
		std::printf("%s\n", dev.Name().c_str());
		std::vector<Case> cases;
		auto add = [&](const char *n, auto f) {
			Case c;
			c.name = n;
			f(c.s);
			cases.push_back(c);
		};
		add("default", [](hg::Settings &) {});
		add("small radius local FIR", [](hg::Settings &s) { s.size = 3.0f; });
		add("subpixel fade", [](hg::Settings &s) { s.size = 0.4f; });
		add("anisotropic 400/25", [](hg::Settings &s) { s.size = 12.0f; s.width = 400.0f; s.height = 25.0f; });
		add("anisotropic blend 150/100", [](hg::Settings &s) { s.size = 20.0f; s.width = 150.0f; });
		add("height zero", [](hg::Settings &s) { s.size = 30.0f; s.height = 0.0f; });
		add("tone and tint", [](hg::Settings &s) {
			s.gamma = 0.4f; s.saturation = 170.0f; s.colourStrength = 60.0f; s.threshold = 0.0f;
			s.colour1[0] = 0.2f; s.colour1[1] = 0.5f; s.colour1[2] = 1.0f;
		});
		add("glow only", [](hg::Settings &s) { s.glowOnly = true; s.glowOpacity = 70.0f; });
		add("source fade", [](hg::Settings &s) { s.sourceOpacity = 30.0f; s.gamma = 2.5f; });
		add("ACEScg luminance", [](hg::Settings &s) { hg::LuminanceCoefficients(hg::LuminanceSpace::ACEScg, s.luminance); });
		add("bypass intensity 0", [](hg::Settings &s) { s.intensity = 0.0f; s.sourceOpacity = 60.0f; });
		int failures = 0;
		unsigned seed = 7u;
		for (const Case &c : cases) failures += RunCase(dev, c, seed++);
		std::printf("%d failing case(s)\n", failures);
		return failures == 0 ? 0 : 1;
	}
}
