/*
	HG_MetalBackend.h

	C++ interface of the Objective-C++ Metal backend (HG_Metal.mm), so that
	HG_GPU.cpp stays plain C++.
*/

#pragma once
#ifndef HG_METAL_BACKEND_H
#define HG_METAL_BACKEND_H

#if defined(HG_HAS_METAL)

#include "HaloGlow.h"
#include "HG_GpuRender.h"

PF_Err HG_MetalDeviceSetup(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetupExtra *extra);
PF_Err HG_MetalDeviceSetdown(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetdownExtra *extra);
PF_Err HG_MetalRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra, const hg::Plan &plan,
					  const hg::gpu::World &src, const hg::gpu::World &dst);

#endif // HG_HAS_METAL

#endif // HG_METAL_BACKEND_H
