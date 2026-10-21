/*
	hg_gpu_parity.cpp

	CPU FP32 reference vs each compiled GPU backend (CUDA, OpenCL) through the
	shared GPU driver, without After Effects (spec §12.5: harness before host
	integration). Worlds are BGRA straight alpha as After Effects supplies
	them. Tolerance per channel: 1e-4 + 1e-3 * |reference|.
*/

#if defined(HG_TEST_CUDA)
	#include <cuda_runtime.h>
#endif
#if defined(HG_TEST_OPENCL)
	#include <CL/cl.h>
	#include "HG_Kernel_cl.h"
#endif

#include "HG_Core.h"
#include "HG_GpuRender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#if defined(HG_TEST_CUDA)
extern "C" cudaError_t HG_CudaDown(const void *, void *, const HG_DownArgs *, cudaStream_t);
extern "C" cudaError_t HG_CudaAccum(const void *, const void *, void *, const HG_AccumArgs *, cudaStream_t);
extern "C" cudaError_t HG_CudaFinal(const void *, void *, const void *, const void *, const HG_FinalArgs *,
									cudaStream_t);
extern "C" cudaError_t HG_CudaSynchronize(cudaStream_t);
#endif

namespace {

class TestDevice : public hg::gpu::Backend {
public:
	virtual const char *Name() const = 0;
	virtual bool Upload(void *dst, const void *src, std::size_t bytes) = 0;
	virtual bool Download(void *dst, const void *src, std::size_t bytes) = 0;
};

#if defined(HG_TEST_CUDA)
class CudaDevice : public TestDevice {
public:
	const char *Name() const override { return "CUDA"; }
	void *Allocate(std::size_t bytes) override
	{
		void *p = nullptr;
		return cudaMalloc(&p, bytes) == cudaSuccess ? p : nullptr;
	}
	void Free(void *mem) override { cudaFree(mem); }
	bool Down(const void *s, void *d, const HG_DownArgs &a) override { return HG_CudaDown(s, d, &a, 0) == cudaSuccess; }
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		return HG_CudaAccum(d, c, o, &a, 0) == cudaSuccess;
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		return HG_CudaFinal(s, d, g0, g1, &a, 0) == cudaSuccess;
	}
	bool Finish() override { return HG_CudaSynchronize(0) == cudaSuccess; }
	bool Upload(void *dst, const void *src, std::size_t n) override
	{
		return cudaMemcpy(dst, src, n, cudaMemcpyHostToDevice) == cudaSuccess;
	}
	bool Download(void *dst, const void *src, std::size_t n) override
	{
		return cudaMemcpy(dst, src, n, cudaMemcpyDeviceToHost) == cudaSuccess;
	}
};

std::unique_ptr<TestDevice> MakeCuda()
{
	int count = 0;
	if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) return nullptr;
	return std::unique_ptr<TestDevice>(new CudaDevice());
}
#endif

#if defined(HG_TEST_OPENCL)
class OpenCLDevice : public TestDevice {
public:
	~OpenCLDevice() override
	{
		if (down_) clReleaseKernel(down_);
		if (accum_) clReleaseKernel(accum_);
		if (final_) clReleaseKernel(final_);
		if (program_) clReleaseProgram(program_);
		if (queue_) clReleaseCommandQueue(queue_);
		if (context_) clReleaseContext(context_);
	}
	bool Init(std::string &log)
	{
		cl_uint np = 0;
		if (clGetPlatformIDs(0, nullptr, &np) != CL_SUCCESS || np == 0) return false;
		std::vector<cl_platform_id> platforms(np);
		clGetPlatformIDs(np, platforms.data(), nullptr);
		for (cl_platform_id p : platforms) {
			if (clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 1, &device_, nullptr) == CL_SUCCESS) break;
			device_ = nullptr;
		}
		if (!device_) return false;
		char name[256] = {};
		clGetDeviceInfo(device_, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
		name_ = std::string("OpenCL (") + name + ")";
		cl_int res = CL_SUCCESS;
		context_ = clCreateContext(nullptr, 1, &device_, nullptr, nullptr, &res);
		if (res != CL_SUCCESS) return false;
		queue_ = clCreateCommandQueue(context_, device_, 0, &res);
		if (res != CL_SUCCESS) return false;
		const char *src = reinterpret_cast<const char *>(kHG_OpenCLSource);
		const size_t len = kHG_OpenCLSource_size;
		program_ = clCreateProgramWithSource(context_, 1, &src, &len, &res);
		if (res != CL_SUCCESS) return false;
		// Same options as the plug-in (HG_GPU.cpp): precise, no fast math.
		res = clBuildProgram(program_, 1, &device_, "-cl-std=CL1.2", nullptr, nullptr);
		if (res != CL_SUCCESS) {
			size_t n = 0;
			clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
			log.resize(n);
			clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, n, &log[0], nullptr);
			return false;
		}
		down_ = clCreateKernel(program_, "hg_down", &res);
		accum_ = clCreateKernel(program_, "hg_accum", &res);
		final_ = clCreateKernel(program_, "hg_final", &res);
		return down_ && accum_ && final_;
	}
	const char *Name() const override { return name_.c_str(); }
	void *Allocate(std::size_t bytes) override
	{
		cl_int res = CL_SUCCESS;
		cl_mem m = clCreateBuffer(context_, CL_MEM_READ_WRITE, bytes, nullptr, &res);
		return res == CL_SUCCESS ? m : nullptr;
	}
	void Free(void *mem) override { clReleaseMemObject(static_cast<cl_mem>(mem)); }
	bool Down(const void *s, void *d, const HG_DownArgs &a) override
	{
		cl_mem ms = M(s), md = M(d);
		return clSetKernelArg(down_, 0, sizeof(cl_mem), &ms) == CL_SUCCESS &&
			   clSetKernelArg(down_, 1, sizeof(cl_mem), &md) == CL_SUCCESS &&
			   clSetKernelArg(down_, 2, sizeof(a), &a) == CL_SUCCESS && Run(down_, a.dst);
	}
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		cl_mem md = M(d), mc = M(c), mo = M(o);
		return clSetKernelArg(accum_, 0, sizeof(cl_mem), &md) == CL_SUCCESS &&
			   clSetKernelArg(accum_, 1, sizeof(cl_mem), &mc) == CL_SUCCESS &&
			   clSetKernelArg(accum_, 2, sizeof(cl_mem), &mo) == CL_SUCCESS &&
			   clSetKernelArg(accum_, 3, sizeof(a), &a) == CL_SUCCESS && Run(accum_, a.dst);
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		cl_mem ms = M(s), md = M(d), m0 = M(g0), m1 = M(g1);
		return clSetKernelArg(final_, 0, sizeof(cl_mem), &ms) == CL_SUCCESS &&
			   clSetKernelArg(final_, 1, sizeof(cl_mem), &md) == CL_SUCCESS &&
			   clSetKernelArg(final_, 2, sizeof(cl_mem), &m0) == CL_SUCCESS &&
			   clSetKernelArg(final_, 3, sizeof(cl_mem), &m1) == CL_SUCCESS &&
			   clSetKernelArg(final_, 4, sizeof(a), &a) == CL_SUCCESS && Run(final_, a.dst);
	}
	bool Finish() override { return clFinish(queue_) == CL_SUCCESS; }
	bool Upload(void *dst, const void *src, std::size_t n) override
	{
		return clEnqueueWriteBuffer(queue_, M(dst), CL_TRUE, 0, n, src, 0, nullptr, nullptr) == CL_SUCCESS;
	}
	bool Download(void *dst, const void *src, std::size_t n) override
	{
		return clEnqueueReadBuffer(queue_, M(src), CL_TRUE, 0, n, dst, 0, nullptr, nullptr) == CL_SUCCESS;
	}

private:
	static cl_mem M(const void *p) { return static_cast<cl_mem>(const_cast<void *>(p)); }
	bool Run(cl_kernel k, const HG_Buf &dst)
	{
		const size_t local[2] = { 16, 16 };
		const size_t global[2] = { size_t((dst.w + 15) / 16) * 16, size_t((dst.h + 15) / 16) * 16 };
		return clEnqueueNDRangeKernel(queue_, k, 2, nullptr, global, local, 0, nullptr, nullptr) == CL_SUCCESS;
	}
	cl_device_id device_ = nullptr;
	cl_context context_ = nullptr;
	cl_command_queue queue_ = nullptr;
	cl_program program_ = nullptr;
	cl_kernel down_ = nullptr;
	cl_kernel accum_ = nullptr;
	cl_kernel final_ = nullptr;
	std::string name_;
};

std::unique_ptr<TestDevice> MakeOpenCL()
{
	std::unique_ptr<OpenCLDevice> d(new OpenCLDevice());
	std::string log;
	if (!d->Init(log)) {
		if (!log.empty()) std::printf("OpenCL build log:\n%s\n", log.c_str());
		return nullptr;
	}
	return std::unique_ptr<TestDevice>(d.release());
}
#endif

struct Case {
	std::string name;
	hg::Settings s;
};

int RunCase(TestDevice &dev, const Case &c, unsigned seed)
{
	const hg::Rect in { -23, 11, 197, 163 };
	// Straight BGRA input with HDR, transparent and NaN pixels.
	std::vector<float> bgra(std::size_t(in.Width()) * in.Height() * 4);
	for (std::size_t i = 0; i < bgra.size() / 4; ++i) {
		seed = seed * 1664525u + 1013904223u;
		const float v = ((seed >> 8) & 0xffff) / 65535.0f;
		const bool bright = (seed % 53u) == 0u;
		bgra[i * 4 + 0] = bright ? 12.0f * v : 0.6f * v;		// B
		bgra[i * 4 + 1] = bright ? 30.0f : v;					// G
		bgra[i * 4 + 2] = bright ? 65504.0f * v : v * v;		// R
		bgra[i * 4 + 3] = (seed % 7u) == 0u ? 0.0f : ((seed % 5u) == 0u ? 0.37f : 1.0f);
		if ((seed % 997u) == 0u) bgra[i * 4 + 2] = NAN;
	}
	const hg::Rect out = hg::MaxResultRect(c.s, hg::RenderScale {}, in);
	const hg::Plan plan = hg::BuildPlan(c.s, hg::RenderScale {}, in, out);

	// CPU reference on premultiplied RGBA.
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
	std::vector<float> got(std::size_t(out.Width()) * out.Height() * 4);
	hg::gpu::Status st = hg::gpu::Status::OutOfMemory;
	if (dSrc && dDst && dev.Upload(dSrc, bgra.data(), bgra.size() * sizeof(float))) {
		const hg::gpu::World ws { dSrc, in.x0, in.y0, in.Width(), in.Height(), in.Width() };
		const hg::gpu::World wd { dDst, out.x0, out.y0, out.Width(), out.Height(), out.Width() };
		st = hg::gpu::Render(plan, dev, ws, wd);
		if (st == hg::gpu::Status::Ok && !dev.Download(got.data(), dDst, got.size() * sizeof(float)))
			st = hg::gpu::Status::LaunchFailed;
	}
	if (dSrc) dev.Free(dSrc);
	if (dDst) dev.Free(dDst);
	if (st != hg::gpu::Status::Ok) {
		std::printf("FAIL %s: GPU status %d\n", c.name.c_str(), static_cast<int>(st));
		return 1;
	}

	double worst = 0.0;
	long bad = 0;
	long tinyAlpha = 0;
	for (int y = out.y0; y < out.y1; ++y)
		for (int x = out.x0; x < out.x1; ++x) {
			const HG_RGBA p = ref.At(x, y);
			float r = 0, g = 0, b = 0;
			if (!plan.glowActive) {
				// Bypass keeps straight RGB; compare against the input itself.
				const int lx = x - in.x0, ly = y - in.y0;
				const bool inside = lx >= 0 && ly >= 0 && lx < in.Width() && ly < in.Height();
				if (inside && !c.s.glowOnly) {
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
				// Straight RGB at an underflowing alpha is a ratio of denormals;
				// FTZ differs per backend and spec 6.3 records it instead of
				// requiring a match. The alpha itself must still agree.
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
					if (bad < 3)
						std::printf("  mismatch (%d,%d) ch%d ref=%g gpu=%g premult=(%g %g %g %g)\n", x, y, ch, a, v,
									p.r, p.g, p.b, p.a);
					++bad;
				}
			}
		}
	std::printf("  %-28s levels=%d pyramids=%d local=%d worst/tol=%.3f bad=%ld tiny-alpha=%ld\n", c.name.c_str(),
				plan.pyramidCount ? plan.pyramid[0].levelCount : 0, plan.pyramidCount, plan.hasLocalSpread ? 1 : 0,
				worst, bad, tinyAlpha);
	return bad == 0 ? 0 : 1;
}

} // namespace

int main()
{
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

	std::vector<std::unique_ptr<TestDevice>> devices;
#if defined(HG_TEST_CUDA)
	if (auto d = MakeCuda()) devices.push_back(std::move(d));
	else std::printf("no CUDA device; CUDA skipped\n");
#endif
#if defined(HG_TEST_OPENCL)
	if (auto d = MakeOpenCL()) devices.push_back(std::move(d));
	else std::printf("no OpenCL GPU device or build failed; OpenCL skipped\n");
#endif
	int failures = 0;
	for (auto &dev : devices) {
		std::printf("%s\n", dev->Name());
		unsigned seed = 7u;
		for (const Case &c : cases) failures += RunCase(*dev, c, seed++);
	}
	std::printf("%zu backend(s), %d failing case(s)\n", devices.size(), failures);
	return failures == 0 ? 0 : 1;
}
