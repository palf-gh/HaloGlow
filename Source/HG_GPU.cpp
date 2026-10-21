/*
	HG_GPU.cpp

	GPU device setup and SmartRender dispatch for CUDA (Windows), OpenCL
	(Windows) and Metal (macOS, HG_Metal.mm). Each framework only implements
	hg::gpu::Backend; the stage sequence lives in HG_GpuRender.cpp and matches
	the CPU reference. Buffers come from the host (AllocateDeviceMemory) and
	are released only after the queue has finished.

	Version 1 allocates per render. The pooled, device-budgeted allocator of
	spec §7.5 is an open item (§17.3).
*/

// Toolkit headers first: HG_Target.h defines MAJOR_VERSION / MINOR_VERSION
// macros (PiPL needs them) that would collide with enumerators in the CUDA
// headers.
#if defined(HG_HAS_CUDA)
	#include <cuda.h>
	#include <cuda_runtime.h>
#endif
#if defined(HG_HAS_OPENCL)
	#include <CL/cl.h>
#endif

#include "HaloGlow.h"
#include "HG_GpuRender.h"
#include "HG_GpuStatus.h"

#include <cstdint>
#include <cstring>
#include <new>

#if defined(HG_HAS_CUDA)
extern "C" cudaError_t HG_CudaDown(const void *, void *, const HG_DownArgs *, cudaStream_t);
extern "C" cudaError_t HG_CudaAccum(const void *, const void *, void *, const HG_AccumArgs *, cudaStream_t);
extern "C" cudaError_t HG_CudaFinal(const void *, void *, const void *, const void *, const HG_FinalArgs *,
									cudaStream_t);
extern "C" cudaError_t HG_CudaSynchronize(cudaStream_t);
#endif

#if defined(HG_HAS_OPENCL)
	#include "HG_Kernel_cl.h"
#endif

#if defined(HG_HAS_METAL)
	#include "HG_MetalBackend.h"
#endif

namespace {

#if defined(HG_HAS_CUDA)
// Driver API only (nvcuda.dll is delay-loaded and only touched on CUDA hosts);
// no CUDA runtime call happens during device setup.
std::string CudaDeviceName(void *device)
{
	char name[256] = {};
	const CUdevice d = static_cast<CUdevice>(reinterpret_cast<std::uintptr_t>(device));
	return cuDeviceGetName(name, static_cast<int>(sizeof(name)), d) == CUDA_SUCCESS ? std::string(name)
																					  : std::string();
}
#endif

#if defined(HG_HAS_OPENCL)
std::string OpenCLDeviceName(cl_device_id device)
{
	size_t size = 0;
	if (!device || clGetDeviceInfo(device, CL_DEVICE_NAME, 0, nullptr, &size) != CL_SUCCESS || size <= 1)
		return std::string();
	std::string name(size, ' ');
	if (clGetDeviceInfo(device, CL_DEVICE_NAME, size, &name[0], nullptr) != CL_SUCCESS) return std::string();
	name.resize(std::strlen(name.c_str()));
	return name;
}
#endif

// Device memory through the host so After Effects can account for it.
class HostMemoryBackend : public hg::gpu::Backend {
public:
	HostMemoryBackend(PF_InData *in_data, const PF_GPUDeviceSuite1 *suite, A_u_long device)
		: in_data_(in_data), suite_(suite), device_(device) {}
	void *Allocate(std::size_t bytes) override
	{
		void *mem = nullptr;
		if (suite_->AllocateDeviceMemory(in_data_->effect_ref, device_, bytes, &mem) != PF_Err_NONE) return nullptr;
		return mem;
	}
	void Free(void *mem) override
	{
		if (mem) suite_->FreeDeviceMemory(in_data_->effect_ref, device_, mem);
	}

protected:
	PF_InData *in_data_;
	const PF_GPUDeviceSuite1 *suite_;
	A_u_long device_;
};

#if defined(HG_HAS_CUDA)
class CudaBackend : public HostMemoryBackend {
public:
	CudaBackend(PF_InData *in_data, const PF_GPUDeviceSuite1 *suite, A_u_long device, cudaStream_t stream)
		: HostMemoryBackend(in_data, suite, device), stream_(stream) {}
	bool Down(const void *s, void *d, const HG_DownArgs &a) override
	{
		return HG_CudaDown(s, d, &a, stream_) == cudaSuccess;
	}
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		return HG_CudaAccum(d, c, o, &a, stream_) == cudaSuccess;
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		return HG_CudaFinal(s, d, g0, g1, &a, stream_) == cudaSuccess;
	}
	bool Finish() override { return HG_CudaSynchronize(stream_) == cudaSuccess; }

private:
	cudaStream_t stream_;
};
#endif

#if defined(HG_HAS_OPENCL)
// Only the built program is shared between renders. cl_kernel objects carry
// argument state and clSetKernelArg is not thread-safe, so every render (MFR
// threads included) creates its own kernels from the immutable program.
struct OpenCLData {
	cl_program program;
};

void ReleaseOpenCL(OpenCLData *d)
{
	if (!d) return;
	if (d->program) clReleaseProgram(d->program);
	std::memset(d, 0, sizeof(*d));
}

struct OpenCLKernels {
	cl_kernel down = nullptr;
	cl_kernel accum = nullptr;
	cl_kernel final = nullptr;
	explicit OpenCLKernels(cl_program program)
	{
		cl_int res = CL_SUCCESS;
		down = clCreateKernel(program, "hg_down", &res);
		if (res == CL_SUCCESS) accum = clCreateKernel(program, "hg_accum", &res);
		if (res == CL_SUCCESS) final = clCreateKernel(program, "hg_final", &res);
	}
	~OpenCLKernels()
	{
		if (down) clReleaseKernel(down);
		if (accum) clReleaseKernel(accum);
		if (final) clReleaseKernel(final);
	}
	bool Valid() const { return down && accum && final; }
	OpenCLKernels(const OpenCLKernels &) = delete;
	OpenCLKernels &operator=(const OpenCLKernels &) = delete;
};

class OpenCLBackend : public HostMemoryBackend {
public:
	OpenCLBackend(PF_InData *in_data, const PF_GPUDeviceSuite1 *suite, A_u_long device, cl_command_queue queue,
				  const OpenCLKernels *k)
		: HostMemoryBackend(in_data, suite, device), queue_(queue), k_(k) {}
	bool Down(const void *s, void *d, const HG_DownArgs &a) override
	{
		cl_mem ms = Mem(s), md = Mem(d);
		bool ok = clSetKernelArg(k_->down, 0, sizeof(cl_mem), &ms) == CL_SUCCESS &&
				  clSetKernelArg(k_->down, 1, sizeof(cl_mem), &md) == CL_SUCCESS &&
				  clSetKernelArg(k_->down, 2, sizeof(a), &a) == CL_SUCCESS;
		return ok && Run(k_->down, a.dst);
	}
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		cl_mem md = Mem(d), mc = Mem(c), mo = Mem(o);
		bool ok = clSetKernelArg(k_->accum, 0, sizeof(cl_mem), &md) == CL_SUCCESS &&
				  clSetKernelArg(k_->accum, 1, sizeof(cl_mem), &mc) == CL_SUCCESS &&
				  clSetKernelArg(k_->accum, 2, sizeof(cl_mem), &mo) == CL_SUCCESS &&
				  clSetKernelArg(k_->accum, 3, sizeof(a), &a) == CL_SUCCESS;
		return ok && Run(k_->accum, a.dst);
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		cl_mem ms = Mem(s), md = Mem(d), m0 = Mem(g0), m1 = Mem(g1);
		bool ok = clSetKernelArg(k_->final, 0, sizeof(cl_mem), &ms) == CL_SUCCESS &&
				  clSetKernelArg(k_->final, 1, sizeof(cl_mem), &md) == CL_SUCCESS &&
				  clSetKernelArg(k_->final, 2, sizeof(cl_mem), &m0) == CL_SUCCESS &&
				  clSetKernelArg(k_->final, 3, sizeof(cl_mem), &m1) == CL_SUCCESS &&
				  clSetKernelArg(k_->final, 4, sizeof(a), &a) == CL_SUCCESS;
		return ok && Run(k_->final, a.dst);
	}
	bool Finish() override { return clFinish(queue_) == CL_SUCCESS; }

private:
	static cl_mem Mem(const void *p) { return static_cast<cl_mem>(const_cast<void *>(p)); }
	bool Run(cl_kernel k, const HG_Buf &dst)
	{
		const size_t local[2] = { 16, 16 };
		const size_t global[2] = { size_t((dst.w + 15) / 16) * 16, size_t((dst.h + 15) / 16) * 16 };
		return clEnqueueNDRangeKernel(queue_, k, 2, nullptr, global, local, 0, nullptr, nullptr) == CL_SUCCESS;
	}
	cl_command_queue queue_;
	const OpenCLKernels *k_;
};

PF_Err SetupOpenCL(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetupExtra *extra)
{
	AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
	AEFX_SuiteScoper<PF_HandleSuite1> handles(in_data, kPFHandleSuite, kPFHandleSuiteVersion1, out_data);
	PF_GPUDeviceInfo info;
	AEFX_CLR_STRUCT(info);
	PF_Err err = gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info);
	if (err) return err;
	cl_context context = static_cast<cl_context>(info.contextPV);
	cl_device_id device = static_cast<cl_device_id>(info.devicePV);
	if (!context || !device) return PF_Err_INTERNAL_STRUCT_DAMAGED;

	PF_Handle h = handles->host_new_handle(sizeof(OpenCLData));
	if (!h) return PF_Err_OUT_OF_MEMORY;
	OpenCLData *d = reinterpret_cast<OpenCLData *>(*h);
	std::memset(d, 0, sizeof(*d));

	cl_int res = CL_SUCCESS;
	const char *src = reinterpret_cast<const char *>(kHG_OpenCLSource);
	const size_t len = kHG_OpenCLSource_size;
	d->program = clCreateProgramWithSource(context, 1, &src, &len, &res);
	// Precise build: no -cl-fast-relaxed-math, no -cl-mad-enable (§12.5).
	if (res == CL_SUCCESS) res = clBuildProgram(d->program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr);
	if (res == CL_SUCCESS && !OpenCLKernels(d->program).Valid()) res = CL_INVALID_KERNEL;
	if (res != CL_SUCCESS) {
		ReleaseOpenCL(d);
		handles->host_dispose_handle(h);
		// Leave GPU unconfirmed: the host renders this device on the CPU path.
		return PF_Err_NONE;
	}
	extra->output->gpu_data = h;
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
	HG_RecordGpuDevice("OpenCL", OpenCLDeviceName(device));
	return PF_Err_NONE;
}
#endif

} // namespace

//-----------------------------------------------------------------------------
PF_Err HG_GPUDeviceSetup(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetupExtra *extra)
{
	if (!extra || !extra->input) return PF_Err_BAD_CALLBACK_PARAM;
	if (in_data->appl_id == kAppID_Premiere) return PF_Err_NONE;	// After Effects only (§2.1)
	switch (extra->input->what_gpu) {
#if defined(HG_HAS_CUDA)
	case PF_GPU_Framework_CUDA: {
		// Kernels are linked statically; no CUDA runtime call is needed here.
		AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
		PF_GPUDeviceInfo info;
		AEFX_CLR_STRUCT(info);
		const PF_Err err = gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info);
		if (err) return err;
		out_data->out_flags2 = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
		HG_RecordGpuDevice("CUDA", CudaDeviceName(info.devicePV));
		return PF_Err_NONE;
	}
#endif
#if defined(HG_HAS_OPENCL)
	case PF_GPU_Framework_OPENCL:
		return SetupOpenCL(in_data, out_data, extra);
#endif
#if defined(HG_HAS_METAL)
	case PF_GPU_Framework_METAL:
		return HG_MetalDeviceSetup(in_data, out_data, extra);
#endif
	default:
		// DirectX and anything else: not confirmed, so the CPU path renders.
		return PF_Err_NONE;
	}
}

PF_Err HG_GPUDeviceSetdown(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetdownExtra *extra)
{
	(void)out_data;
	if (!extra || !extra->input) return PF_Err_NONE;
	HG_ClearGpuDevice();
#if defined(HG_HAS_OPENCL)
	if (extra->input->what_gpu == PF_GPU_Framework_OPENCL && extra->input->gpu_data) {
		PF_Handle h = static_cast<PF_Handle>(const_cast<void *>(extra->input->gpu_data));
		ReleaseOpenCL(reinterpret_cast<OpenCLData *>(*h));
		AEFX_SuiteScoper<PF_HandleSuite1> handles(in_data, kPFHandleSuite, kPFHandleSuiteVersion1, out_data);
		handles->host_dispose_handle(h);
	}
#endif
#if defined(HG_HAS_METAL)
	if (extra->input->what_gpu == PF_GPU_Framework_METAL) return HG_MetalDeviceSetdown(in_data, out_data, extra);
#endif
	(void)in_data;
	return PF_Err_NONE;
}

PF_Err HG_SmartRenderGPU(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra,
						 PF_EffectWorld *input, PF_EffectWorld *output, const HG_PreRenderData &data)
{
	AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
	AEFX_SuiteScoper<PF_WorldSuite2> worlds(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
	PF_PixelFormat format = PF_PixelFormat_INVALID;
	PF_Err err = worlds->PF_GetPixelFormat(output, &format);
	if (err) return err;
	if (format != PF_PixelFormat_GPU_BGRA128) return PF_Err_UNRECOGNIZED_PARAM_TYPE;

	PF_GPUDeviceInfo info;
	AEFX_CLR_STRUCT(info);
	err = gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info);
	if (err) return err;

	hg::gpu::World src;
	hg::gpu::World dst;
	if (input) {
		void *mem = nullptr;
		err = gpu->GetGPUWorldData(in_data->effect_ref, input, &mem);
		if (err) return err;
		src = { mem, input->origin_x, input->origin_y, input->width, input->height,
				static_cast<int>(input->rowbytes / 16) };
	}
	{
		void *mem = nullptr;
		err = gpu->GetGPUWorldData(in_data->effect_ref, output, &mem);
		if (err) return err;
		if (!mem) return PF_Err_INTERNAL_STRUCT_DAMAGED;
		dst = { mem, output->origin_x, output->origin_y, output->width, output->height,
				static_cast<int>(output->rowbytes / 16) };
	}

	const hg::Rect outRect { dst.x0, dst.y0, dst.x0 + dst.width, dst.y0 + dst.height };
	const hg::Plan plan = hg::BuildPlan(data.settings, data.scale, data.inputBounds, outRect);

	hg::gpu::Status status = hg::gpu::Status::LaunchFailed;
	try {
		switch (extra->input->what_gpu) {
#if defined(HG_HAS_CUDA)
		case PF_GPU_Framework_CUDA: {
			CudaBackend backend(in_data, gpu.get(), extra->input->device_index,
								static_cast<cudaStream_t>(info.command_queuePV));
			status = hg::gpu::Render(plan, backend, src, dst);
			break;
		}
#endif
#if defined(HG_HAS_OPENCL)
		case PF_GPU_Framework_OPENCL: {
			PF_Handle h = static_cast<PF_Handle>(const_cast<void *>(extra->input->gpu_data));
			if (!h) return PF_Err_INTERNAL_STRUCT_DAMAGED;
			const OpenCLKernels kernels(reinterpret_cast<const OpenCLData *>(*h)->program);
			if (!kernels.Valid()) return PF_Err_INTERNAL_STRUCT_DAMAGED;
			OpenCLBackend backend(in_data, gpu.get(), extra->input->device_index,
								  static_cast<cl_command_queue>(info.command_queuePV), &kernels);
			status = hg::gpu::Render(plan, backend, src, dst);
			break;
		}
#endif
#if defined(HG_HAS_METAL)
		case PF_GPU_Framework_METAL: {
			const PF_Err metalErr = HG_MetalRender(in_data, out_data, extra, plan, src, dst);
			HG_SetLastRenderUsedGpu(metalErr == PF_Err_NONE);
			return metalErr;
		}
#endif
		default:
			return PF_Err_UNRECOGNIZED_PARAM_TYPE;
		}
	} catch (const std::bad_alloc &) {
		return PF_Err_OUT_OF_MEMORY;
	}
	// Only GPU renders update the status; interleaved CPU renders leave the
	// device Ready until the host sends GPU_DEVICE_SETDOWN.
	HG_SetLastRenderUsedGpu(status == hg::gpu::Status::Ok);
	// Never confirm an incomplete GPU image (§10.3).
	if (status == hg::gpu::Status::OutOfMemory) return PF_Err_OUT_OF_MEMORY;
	if (status != hg::gpu::Status::Ok) return PF_Err_INTERNAL_STRUCT_DAMAGED;
	return PF_Err_NONE;
}
