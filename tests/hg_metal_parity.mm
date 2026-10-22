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
	bool tile = false;	// input world smaller than the layer, output tile
	bool clipLight = false;	// host input larger than the layer: light only inside (§8.3)
};

int RunCase(MetalDevice &dev, const Case &c, unsigned seed)
{
	const hg::Rect in { -23, 11, 197, 163 };
	// AE hands out padded rows and, for tiles, a checked-out input world that is
	// only part of the layer; the plan still uses the true layer bounds.
	const hg::Rect world = c.tile ? hg::Rect { in.x0 + 9, in.y0 + 5, in.x1 - 3, in.y1 - 11 } : in;
	const int srcPitch = world.Width() + 7;
	std::vector<float> bgra(std::size_t(in.Width()) * in.Height() * 4);
	for (std::size_t i = 0; i < bgra.size() / 4; ++i) {
		seed = seed * 1664525u + 1013904223u;
		const float v = ((seed >> 8) & 0xffff) / 65535.0f;
		const bool bright = (seed % 53u) == 0u;
		bgra[i * 4 + 0] = bright ? 12.0f * v : 0.6f * v;
		bgra[i * 4 + 1] = bright ? 30.0f : v;
		bgra[i * 4 + 2] = bright ? 65504.0f * v : v * v;
		bgra[i * 4 + 3] = (seed % 7u) == 0u ? 0.0f : ((seed % 5u) == 0u ? 0.37f : 1.0f);
		for (int ch = 0; ch < 3; ++ch) bgra[i * 4 + ch] *= bgra[i * 4 + 3];	// premultiplied world
		if ((seed % 997u) == 0u) bgra[i * 4 + 2] = NAN;
	}
	const hg::Rect light = c.clipLight ? hg::Rect { in.x0 + 20, in.y0 + 15, in.x1 - 30, in.y1 - 25 } : in;
	const hg::Rect full = hg::MaxResultRect(c.s, hg::RenderScale {}, light, in);
	const hg::Rect out = c.tile ? hg::Intersect(full, hg::Rect { 40, 30, 151, 140 }) : full;
	const int dstPitch = out.Width() + 3;
	const hg::Plan plan = hg::BuildPlan(c.s, hg::RenderScale {}, light, out);
	std::vector<float> upload(std::size_t(srcPitch) * world.Height() * 4, 0.0f);
	for (int y = world.y0; y < world.y1; ++y)
		for (int x = world.x0; x < world.x1; ++x)
			for (int ch = 0; ch < 4; ++ch)
				upload[(std::size_t(y - world.y0) * srcPitch + (x - world.x0)) * 4 + ch] =
					bgra[(std::size_t(y - in.y0) * in.Width() + (x - in.x0)) * 4 + ch];
	hg::Image src;
	src.Reset(world);
	for (int y = world.y0; y < world.y1; ++y)
		for (int x = world.x0; x < world.x1; ++x) {
			const std::size_t i = (std::size_t(y - in.y0) * in.Width() + (x - in.x0)) * 4;
			src.Row(y)[x - world.x0] = hg_sanitise(HG_RGBA { bgra[i + 2], bgra[i + 1], bgra[i], bgra[i + 3] });
		}
	hg::Image ref;
	hg::RenderCPU(plan, src, ref);

	void *dSrc = dev.Allocate(upload.size() * sizeof(float));
	void *dDst = dev.Allocate(std::size_t(dstPitch) * out.Height() * 16);
	std::memcpy(MetalDevice::B(dSrc).contents, upload.data(), upload.size() * sizeof(float));
	const hg::gpu::World ws { dSrc, world.x0, world.y0, world.Width(), world.Height(), srcPitch };
	const hg::gpu::World wd { dDst, out.x0, out.y0, out.Width(), out.Height(), dstPitch };
	const hg::gpu::Status st = hg::gpu::Render(plan, dev, ws, wd);
	std::vector<float> got(std::size_t(dstPitch) * out.Height() * 4);
	std::memcpy(got.data(), MetalDevice::B(dDst).contents, got.size() * sizeof(float));
	dev.Free(dSrc);
	dev.Free(dDst);
	if (st != hg::gpu::Status::Ok) {
		std::printf("FAIL %s: GPU status %d\n", c.name.c_str(), static_cast<int>(st));
		return 1;
	}
	double worst = 0.0;
	long bad = 0;
	for (int y = out.y0; y < out.y1; ++y)
		for (int x = out.x0; x < out.x1; ++x) {
			const HG_RGBA p = ref.At(x, y);
			float r = 0, g = 0, b = 0, a4 = 0;
			if (!plan.glowActive) {
				// Bypass: the input itself, premultiplied RGB and alpha faded by
				// Source Opacity; transparent black for Glow Only.
				const int lx = x - world.x0, ly = y - world.y0;
				if (lx >= 0 && ly >= 0 && lx < world.Width() && ly < world.Height() && !c.s.glowOnly) {
					const std::size_t k = (std::size_t(ly) * srcPitch + lx) * 4;
					const float q = c.s.sourceOpacity / 100.0f;
					b = upload[k] * q;
					g = upload[k + 1] * q;
					r = upload[k + 2] * q;
					a4 = upload[k + 3] * q;
				}
			} else {
				r = p.r;
				g = p.g;
				b = p.b;
				a4 = p.a;
			}
			const float refv[4] = { b, g, r, a4 };
			const std::size_t k = (std::size_t(y - out.y0) * dstPitch + (x - out.x0)) * 4;
			for (int ch = 0; ch < 4; ++ch) {
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
	std::printf("  %-28s worst/tol=%.3f bad=%ld\n", c.name.c_str(), worst, bad);
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
	add("tile, padded pitch", [](hg::Settings &s) { s.size = 9.0f; s.width = 130.0f; s.threshold = 5.0f; });
	cases.back().tile = true;
	add("tile, small radius", [](hg::Settings &s) { s.size = 2.5f; });
	cases.back().tile = true;
	add("light clipped to layer", [](hg::Settings &s) { s.size = 6.0f; s.threshold = 0.0f; });
	cases.back().clipLight = true;
	add("light clipped, tile", [](hg::Settings &s) { s.size = 2.0f; s.width = 300.0f; });
	cases.back().clipLight = true;
	cases.back().tile = true;
		int failures = 0;
		unsigned seed = 7u;
		for (const Case &c : cases) failures += RunCase(dev, c, seed++);
		std::printf("%d failing case(s)\n", failures);
		return failures == 0 ? 0 : 1;
	}
}
