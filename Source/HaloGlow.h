/*
	HaloGlow.h

	After Effects host adapter for the HaloGlow effect: parameter IDs, the
	pre-render payload and the entry points shared by the CPU and GPU paths.
	The image processing itself lives in the host-independent core (HG_Core.h).
*/

#pragma once
#ifndef HALOGLOW_H
#define HALOGLOW_H

#include "HG_Target.h"

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_EffectGPUSuites.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHelper.h"
#include "AEGP_SuiteHandler.h"
#include "Param_Utils.h"
#include "Smart_Utils.h"
#include "String_Utils.h"
#include "PrSDKAESupport.h"

#include "HG_Core.h"

#if defined(HG_HAS_CUDA) || defined(HG_HAS_OPENCL) || defined(HG_HAS_METAL)
	#define HG_GPU_ENABLED 1
#endif

//-----------------------------------------------------------------------------
// Parameter IDs (spec §18.2). Persisted by After Effects once released:
// never renumber or reuse an ID after that; append new ones before
// HG_NUM_PARAMS. In version 1 the UI order equals the ID order, so the param
// index equals the ID.
//-----------------------------------------------------------------------------
enum HG_ParamId {
	HG_INPUT = 0,
	HG_GPU_STATUS,			// read-only custom UI: GPU acceleration status
	HG_TOPIC_BASIC_START,
	HG_INTENSITY,
	HG_SIZE,
	HG_THRESHOLD,
	HG_THRESHOLD_SOFTNESS,
	HG_TOPIC_BASIC_END,
	HG_TOPIC_SHAPE_START,
	HG_WIDTH,
	HG_HEIGHT,
	HG_FALLOFF,
	HG_TOPIC_SHAPE_END,
	HG_TOPIC_TONE_START,
	HG_GAMMA,
	HG_SATURATION,
	HG_COLOUR_STRENGTH,
	HG_COLOUR_1,
	HG_COLOUR_2,
	HG_LUMINANCE_SPACE,
	HG_TOPIC_TONE_END,
	HG_TOPIC_OUTPUT_START,
	HG_GLOW_OPACITY,
	HG_SOURCE_OPACITY,
	HG_GLOW_ONLY,
	HG_TOPIC_OUTPUT_END,
	HG_ALGORITHM_VERSION,
	HG_NUM_PARAMS
};

struct HG_GlobalData {
	AEGP_PluginID plugin_id;
};

// Everything SmartRender needs, captured once in PreRender (immutable).
struct HG_PreRenderData {
	hg::Settings settings;
	hg::RenderScale scale;
	hg::Rect inputBounds;		// light-source rect: input bounds clipped to the layer (§8.3)
	int algorithmVersion = hg::kAlgorithmVersion;
};

// Rect conversions between AE PF_LRect and the core.
inline hg::Rect HG_FromLRect(const PF_LRect &r) { return { r.left, r.top, r.right, r.bottom }; }
inline PF_LRect HG_ToLRect(const hg::Rect &r)
{
	PF_LRect o;
	o.left = r.x0;
	o.top = r.y0;
	o.right = r.x1;
	o.bottom = r.y1;
	return o;
}

// UI (HaloGlow_UI.cpp)
PF_Err HG_ParamsSetup(PF_InData *in_data, PF_OutData *out_data);
PF_Err HG_UpdateParamsUI(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[]);
void HG_RefreshColourCache(PF_InData *in_data);

// GPU status control (HaloGlow_GpuStatusUI.cpp)
PF_Err HG_RegisterCustomUI(PF_InData *in_data);
PF_Err HG_HandleEvent(PF_InData *in_data, PF_OutData *out_data, PF_EventExtra *extra);

// CPU render (HaloGlow_CPU.cpp)
PF_Err HG_SmartRenderCPU(PF_InData *in_data, PF_EffectWorld *input, PF_EffectWorld *output,
						 const HG_PreRenderData &data);

#if defined(HG_GPU_ENABLED)
// GPU (HG_GPU.cpp)
PF_Err HG_GPUDeviceSetup(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetupExtra *extra);
PF_Err HG_GPUDeviceSetdown(PF_InData *in_data, PF_OutData *out_data, PF_GPUDeviceSetdownExtra *extra);
PF_Err HG_SmartRenderGPU(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra,
						 PF_EffectWorld *input, PF_EffectWorld *output, const HG_PreRenderData &data);
#endif

#endif // HALOGLOW_H
