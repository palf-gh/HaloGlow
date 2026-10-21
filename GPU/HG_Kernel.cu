/*
	HG_Kernel.cu

	CUDA entry points. All arithmetic lives in HG_PixelMath.h / HG_Kernels.h,
	shared with OpenCL and Metal. Built without --use_fast_math and with
	--fmad=false so results stay close to the FP32 CPU reference (§12.5).
*/

#include <cuda_runtime.h>

#include "HG_PixelMath.h"
#include "HG_Kernels.h"

namespace {

constexpr int kBlock = 16;

__global__ void DownKernel(const float4 *src, float4 *dst, HG_DownArgs a)
{
	const int x = blockIdx.x * blockDim.x + threadIdx.x;
	const int y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_down_body(src, dst, a, x, y);
}

__global__ void AccumKernel(const float4 *d, const float4 *coarse, float4 *dst, HG_AccumArgs a)
{
	const int x = blockIdx.x * blockDim.x + threadIdx.x;
	const int y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_accum_body(d, coarse, dst, a, x, y);
}

__global__ void FinalKernel(const float4 *src, float4 *dst, const float4 *g0, const float4 *g1, HG_FinalArgs a)
{
	const int x = blockIdx.x * blockDim.x + threadIdx.x;
	const int y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x >= a.dst.w || y >= a.dst.h) return;
	hg_final_body(src, dst, g0, g1, a, x, y);
}

dim3 Grid(const HG_Buf &b)
{
	return dim3((b.w + kBlock - 1) / kBlock, (b.h + kBlock - 1) / kBlock, 1);
}

} // namespace

extern "C" cudaError_t HG_CudaDown(const void *src, void *dst, const HG_DownArgs *a, cudaStream_t stream)
{
	DownKernel<<<Grid(a->dst), dim3(kBlock, kBlock, 1), 0, stream>>>(
		static_cast<const float4 *>(src), static_cast<float4 *>(dst), *a);
	return cudaGetLastError();
}

extern "C" cudaError_t HG_CudaAccum(const void *d, const void *coarse, void *dst, const HG_AccumArgs *a,
									cudaStream_t stream)
{
	AccumKernel<<<Grid(a->dst), dim3(kBlock, kBlock, 1), 0, stream>>>(
		static_cast<const float4 *>(d), static_cast<const float4 *>(coarse), static_cast<float4 *>(dst), *a);
	return cudaGetLastError();
}

extern "C" cudaError_t HG_CudaFinal(const void *src, void *dst, const void *g0, const void *g1, const HG_FinalArgs *a,
									cudaStream_t stream)
{
	FinalKernel<<<Grid(a->dst), dim3(kBlock, kBlock, 1), 0, stream>>>(
		static_cast<const float4 *>(src), static_cast<float4 *>(dst), static_cast<const float4 *>(g0),
		static_cast<const float4 *>(g1), *a);
	return cudaGetLastError();
}

extern "C" cudaError_t HG_CudaSynchronize(cudaStream_t stream)
{
	return cudaStreamSynchronize(stream);
}
