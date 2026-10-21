/*
	HG_Metal.mm

	Metal backend (macOS). The kernel library is compiled once per device at
	PF_Cmd_GPU_DEVICE_SETUP from the embedded source with fast math disabled;
	the three immutable pipeline states are shared by every render. Each launch
	gets its own command buffer on the host queue, so renders on several MFR
	threads never share encoder state.

	Ownership: pipelines live in a host handle that ARC does not track, so they
	are stored with CFBridgingRetain and released with CFBridgingRelease at
	device setdown.
*/

#if defined(HG_HAS_METAL)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "HG_MetalBackend.h"
#include "HG_GpuStatus.h"
#include "HG_Kernel_metal.h"

#include <new>

namespace {

struct MetalData {
	const void *down;
	const void *accum;
	const void *final;
};

id<MTLComputePipelineState> Pipeline(const void *p)
{
	return (__bridge id<MTLComputePipelineState>)p;
}

class MetalBackend : public hg::gpu::Backend {
public:
	MetalBackend(PF_InData *in_data, const PF_GPUDeviceSuite1 *suite, A_u_long device, id<MTLCommandQueue> queue,
				 const MetalData *data)
		: in_data_(in_data), suite_(suite), device_(device), queue_(queue), data_(data) {}

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
	bool Down(const void *s, void *d, const HG_DownArgs &a) override
	{
		return Launch(Pipeline(data_->down), a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:Buf(s) offset:0 atIndex:0];
			[e setBuffer:Buf(d) offset:0 atIndex:1];
			[e setBytes:&a length:sizeof(a) atIndex:2];
		});
	}
	bool Accum(const void *d, const void *c, void *o, const HG_AccumArgs &a) override
	{
		return Launch(Pipeline(data_->accum), a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:Buf(d) offset:0 atIndex:0];
			[e setBuffer:Buf(c) offset:0 atIndex:1];
			[e setBuffer:Buf(o) offset:0 atIndex:2];
			[e setBytes:&a length:sizeof(a) atIndex:3];
		});
	}
	bool Final(const void *s, void *d, const void *g0, const void *g1, const HG_FinalArgs &a) override
	{
		return Launch(Pipeline(data_->final), a.dst, ^(id<MTLComputeCommandEncoder> e) {
			[e setBuffer:Buf(s) offset:0 atIndex:0];
			[e setBuffer:Buf(d) offset:0 atIndex:1];
			[e setBuffer:Buf(g0) offset:0 atIndex:2];
			[e setBuffer:Buf(g1) offset:0 atIndex:3];
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

private:
	static id<MTLBuffer> Buf(const void *p) { return (__bridge id<MTLBuffer>)p; }

	bool Launch(id<MTLComputePipelineState> pipeline, const HG_Buf &dst, void (^bind)(id<MTLComputeCommandEncoder>))
	{
		id<MTLCommandBuffer> cb = [queue_ commandBuffer];
		if (!cb) return false;
		id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
		if (!e) return false;
		[e setComputePipelineState:pipeline];
		bind(e);
		// 16x16 when the pipeline allows it, else 8x8 (the final stage carries a
		// large argument block and may get a lower thread limit).
		const NSUInteger side = pipeline.maxTotalThreadsPerThreadgroup >= 256 ? 16 : 8;
		const MTLSize group = MTLSizeMake(side, side, 1);
		const MTLSize grid = MTLSizeMake((dst.w + side - 1) / side, (dst.h + side - 1) / side, 1);
		[e dispatchThreadgroups:grid threadsPerThreadgroup:group];
		[e endEncoding];
		[cb commit];	// same queue: command buffers run in commit order
		last_ = cb;
		return true;
	}

	PF_InData *in_data_;
	const PF_GPUDeviceSuite1 *suite_;
	A_u_long device_;
	id<MTLCommandQueue> queue_;
	const MetalData *data_;
	id<MTLCommandBuffer> last_ = nil;
};

} // namespace

PF_Err HG_MetalDeviceSetup(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetupExtra *extra)
{
	AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
	AEFX_SuiteScoper<PF_HandleSuite1> handles(in_data, kPFHandleSuite, kPFHandleSuiteVersion1, out_data);
	PF_GPUDeviceInfo info;
	AEFX_CLR_STRUCT(info);
	PF_Err err = gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info);
	if (err) return err;

	@autoreleasepool {
		id<MTLDevice> device = (__bridge id<MTLDevice>)info.devicePV;
		if (!device) return PF_Err_INTERNAL_STRUCT_DAMAGED;
		NSString *source = [[NSString alloc] initWithBytes:kHG_MetalSource
													length:kHG_MetalSource_size
												  encoding:NSUTF8StringEncoding];
		MTLCompileOptions *options = [MTLCompileOptions new];
		// Precise maths on the reference path (§12.5).
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
		id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
		if (!library) {
			NSLog(@"HaloGlow: Metal library build failed: %@", error);
			return PF_Err_NONE;	// GPU left unconfirmed: the CPU path renders
		}
		const char *names[3] = { "hg_down", "hg_accum", "hg_final" };
		id<MTLComputePipelineState> pipelines[3] = { nil, nil, nil };
		for (int i = 0; i < 3; ++i) {
			id<MTLFunction> fn = [library newFunctionWithName:[NSString stringWithUTF8String:names[i]]];
			if (!fn) return PF_Err_NONE;
			pipelines[i] = [device newComputePipelineStateWithFunction:fn error:&error];
			if (!pipelines[i] || pipelines[i].maxTotalThreadsPerThreadgroup < 64) return PF_Err_NONE;
		}
		PF_Handle h = handles->host_new_handle(sizeof(MetalData));
		if (!h) return PF_Err_OUT_OF_MEMORY;
		MetalData *d = reinterpret_cast<MetalData *>(*h);
		d->down = CFBridgingRetain(pipelines[0]);
		d->accum = CFBridgingRetain(pipelines[1]);
		d->final = CFBridgingRetain(pipelines[2]);
		extra->output->gpu_data = h;
		out_data->out_flags2 = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
		const char *deviceName = device.name.UTF8String;
		HG_RecordGpuDevice("Metal", deviceName ? deviceName : "");
	}
	return PF_Err_NONE;
}

PF_Err HG_MetalDeviceSetdown(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetdownExtra *extra)
{
	if (!extra->input->gpu_data) return PF_Err_NONE;
	PF_Handle h = static_cast<PF_Handle>(const_cast<void *>(extra->input->gpu_data));
	MetalData *d = reinterpret_cast<MetalData *>(*h);
	if (d->down) CFBridgingRelease(d->down);
	if (d->accum) CFBridgingRelease(d->accum);
	if (d->final) CFBridgingRelease(d->final);
	AEFX_SuiteScoper<PF_HandleSuite1> handles(in_data, kPFHandleSuite, kPFHandleSuiteVersion1, out_data);
	handles->host_dispose_handle(h);
	return PF_Err_NONE;
}

PF_Err HG_MetalRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra, const hg::Plan &plan,
					  const hg::gpu::World &src, const hg::gpu::World &dst)
{
	PF_Handle h = static_cast<PF_Handle>(const_cast<void *>(extra->input->gpu_data));
	if (!h) return PF_Err_INTERNAL_STRUCT_DAMAGED;
	AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
	PF_GPUDeviceInfo info;
	AEFX_CLR_STRUCT(info);
	PF_Err err = gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info);
	if (err) return err;

	hg::gpu::Status status = hg::gpu::Status::LaunchFailed;
	@autoreleasepool {
		id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)info.command_queuePV;
		if (!queue) return PF_Err_INTERNAL_STRUCT_DAMAGED;
		try {
			MetalBackend backend(in_data, gpu.get(), extra->input->device_index, queue,
								 reinterpret_cast<const MetalData *>(*h));
			status = hg::gpu::Render(plan, backend, src, dst);
		} catch (const std::bad_alloc &) {
			return PF_Err_OUT_OF_MEMORY;
		}
	}
	if (status == hg::gpu::Status::OutOfMemory) return PF_Err_OUT_OF_MEMORY;
	if (status != hg::gpu::Status::Ok) return PF_Err_INTERNAL_STRUCT_DAMAGED;
	return PF_Err_NONE;
}

#endif // HG_HAS_METAL
