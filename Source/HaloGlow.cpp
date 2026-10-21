/*
	HaloGlow.cpp

	Entry point, global setup and the SmartFX pre-render / render dispatch.
	Pre-render captures every parameter into an immutable HG_PreRenderData so
	SmartRender (CPU or GPU, possibly on several MFR threads) never touches AE
	parameter state, sequence data or AEGP.
*/

#include "HaloGlow.h"
#include "HG_ColourCache.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

double RationalToDouble(const PF_RationalScale &r)
{
	return r.den ? static_cast<double>(r.num) / static_cast<double>(r.den) : 1.0;
}

// Checks out one parameter at the current time, copies it out, checks it in.
PF_Err ReadParam(PF_InData *in_data, A_long index, PF_ParamDef &out)
{
	AEFX_CLR_STRUCT(out);
	PF_Err err = PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step,
								   in_data->time_scale, &out);
	if (!err) {
		PF_ParamDef copy = out;
		err = PF_CHECKIN_PARAM(in_data, &out);
		out = copy;
	}
	return err;
}

PF_Err ReadFloat(PF_InData *in_data, A_long index, float &v)
{
	PF_ParamDef def;
	const PF_Err err = ReadParam(in_data, index, def);
	if (!err) v = static_cast<float>(def.u.fs_d.value);
	return err;
}

PF_Err ReadColour(PF_InData *in_data, PF_OutData *out_data, A_long index, float rgb[3])
{
	PF_ParamDef def;
	PF_Err err = ReadParam(in_data, index, def);
	if (err) return err;
	AEFX_SuiteScoper<PF_ColorParamSuite1> colours(in_data, kPFColorParamSuite, kPFColorParamSuiteVersion1,
												   out_data);
	PF_PixelFloat c;
	err = colours->PF_GetFloatingPointColorFromColorDef(in_data->effect_ref, &def, &c);
	if (!err) {
		// Working-space values as the host returns them (§3.10); Sanitise
		// clamps negatives to 0 and non-finite values to 0.
		rgb[0] = c.red;
		rgb[1] = c.green;
		rgb[2] = c.blue;
	}
	return err;
}

PF_Err ReadSettings(PF_InData *in_data, PF_OutData *out_data, hg::Settings &s, int &version,
					bool &autoLuminance, hg::ColourSnapshot &snapshot)
{
	PF_Err err = PF_Err_NONE;
	ERR(ReadFloat(in_data, HG_INTENSITY, s.intensity));
	ERR(ReadFloat(in_data, HG_SIZE, s.size));
	ERR(ReadFloat(in_data, HG_THRESHOLD, s.threshold));
	ERR(ReadFloat(in_data, HG_THRESHOLD_SOFTNESS, s.softness));
	ERR(ReadFloat(in_data, HG_WIDTH, s.width));
	ERR(ReadFloat(in_data, HG_HEIGHT, s.height));
	ERR(ReadFloat(in_data, HG_FALLOFF, s.falloff));
	ERR(ReadFloat(in_data, HG_GAMMA, s.gamma));
	ERR(ReadFloat(in_data, HG_SATURATION, s.saturation));
	ERR(ReadFloat(in_data, HG_COLOUR_STRENGTH, s.colourStrength));
	ERR(ReadColour(in_data, out_data, HG_COLOUR_1, s.colour1));
	ERR(ReadColour(in_data, out_data, HG_COLOUR_2, s.colour2));
	ERR(ReadFloat(in_data, HG_GLOW_OPACITY, s.glowOpacity));
	ERR(ReadFloat(in_data, HG_SOURCE_OPACITY, s.sourceOpacity));

	PF_ParamDef def;
	if (!err && !(err = ReadParam(in_data, HG_GLOW_ONLY, def))) s.glowOnly = def.u.bd.value != 0;

	A_long space = static_cast<A_long>(hg::LuminanceSpace::Auto);
	if (!err && !(err = ReadParam(in_data, HG_LUMINANCE_SPACE, def))) space = def.u.pd.value;
	if (space < 1 || space > 4) space = static_cast<A_long>(hg::LuminanceSpace::Auto);
	autoLuminance = space == static_cast<A_long>(hg::LuminanceSpace::Auto);
	if (autoLuminance) {
		// One snapshot per render (§3.12.2); never refreshed from here.
		snapshot = *hg::CurrentColourSnapshot();
		std::memcpy(s.luminance, snapshot.luminance, sizeof(s.luminance));
	} else {
		hg::LuminanceCoefficients(static_cast<hg::LuminanceSpace>(space), s.luminance);
	}

	version = hg::kAlgorithmVersion;
	if (!err && !(err = ReadParam(in_data, HG_ALGORITHM_VERSION, def))) version = def.u.sd.value;
	return err;
}

void DisposePreRenderData(void *p)
{
	delete static_cast<HG_PreRenderData *>(p);
}

//-----------------------------------------------------------------------------
PF_Err About(PF_InData *in_data, PF_OutData *out_data)
{
	(void)in_data;
	PF_SPRINTF(out_data->return_msg, "%s, v%d.%d.%d\r%s", NAME, MAJOR_VERSION, MINOR_VERSION, BUG_VERSION,
			   DESCRIPTION);
	return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data)
{
	PF_Err err = PF_Err_NONE;
	out_data->my_version = HG_VERSION_PACKED;
	out_data->out_flags = OUT_FLAGS;
	out_data->out_flags2 = OUT_FLAGS2;

	// AEGP registration: the plug-in id is needed for the colour settings
	// queries made from main-thread UI commands.
	if (in_data && in_data->pica_basicP) {
		AEGP_SuiteHandler suites(in_data->pica_basicP);
		out_data->global_data = suites.HandleSuite1()->host_new_handle(sizeof(HG_GlobalData));
		if (out_data->global_data) {
			HG_GlobalData *g = reinterpret_cast<HG_GlobalData *>(
				suites.HandleSuite1()->host_lock_handle(out_data->global_data));
			if (g) {
				g->plugin_id = 0;
				ERR(suites.UtilitySuite5()->AEGP_RegisterWithAEGP(nullptr, NAME, &g->plugin_id));
				suites.HandleSuite1()->host_unlock_handle(out_data->global_data);
			}
		}
	}
	return err;
}

PF_Err GlobalSetdown(PF_InData *in_data)
{
	if (in_data && in_data->global_data && in_data->pica_basicP) {
		AEGP_SuiteHandler suites(in_data->pica_basicP);
		suites.HandleSuite1()->host_dispose_handle(in_data->global_data);
	}
	return PF_Err_NONE;
}

//-----------------------------------------------------------------------------
PF_Err PreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	auto *data = new (std::nothrow) HG_PreRenderData();
	if (!data) return PF_Err_OUT_OF_MEMORY;

	bool autoLuminance = false;
	hg::ColourSnapshot snapshot;
	err = ReadSettings(in_data, out_data, data->settings, data->algorithmVersion, autoLuminance, snapshot);
	if (!err && data->algorithmVersion != hg::kAlgorithmVersion) {
		// A project saved by a newer HaloGlow: never draw it with another
		// version's coefficients (§18.2).
		err = PF_Err_BAD_CALLBACK_PARAM;
	}
	data->settings = hg::Sanitise(data->settings);
	data->scale.downsampleX = RationalToDouble(in_data->downsample_x);
	data->scale.downsampleY = RationalToDouble(in_data->downsample_y);
	data->scale.pixelAspect = RationalToDouble(in_data->pixel_aspect_ratio);

	PF_RenderRequest req = extra->input->output_request;
	const hg::Rect requested = HG_FromLRect(req.rect);
	req.rect = HG_ToLRect(hg::RequiredInputRect(data->settings, data->scale, requested));
	req.preserve_rgb_of_zero_alpha = FALSE;

	PF_CheckoutResult in_result;
	AEFX_CLR_STRUCT(in_result);
	ERR(extra->cb->checkout_layer(in_data->effect_ref, HG_INPUT, HG_INPUT, &req, in_data->current_time,
								  in_data->time_step, in_data->time_scale, &in_result));
	if (!err) {
		data->inputBounds = HG_FromLRect(in_result.max_result_rect);
		const hg::Rect maxRect = hg::MaxResultRect(data->settings, data->scale, data->inputBounds);
		extra->output->max_result_rect = HG_ToLRect(maxRect);
		extra->output->result_rect = HG_ToLRect(hg::Intersect(maxRect, requested));
		extra->output->solid = FALSE;
#if defined(HG_GPU_ENABLED)
		extra->output->flags |= PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
#endif
		if (autoLuminance && extra->cb->GuidMixInPtr) {
			// Auto coefficients are state outside the parameters: mix them into
			// the frame cache key so a refreshed cache re-renders (§3.12.2).
			// Only the coefficients change pixels; a Verified -> Cached state
			// change with the same values must not invalidate cached frames.
			const float key[3] = { snapshot.luminance[0], snapshot.luminance[1], snapshot.luminance[2] };
			ERR(extra->cb->GuidMixInPtr(in_data->effect_ref, sizeof(key), key));
		}
	}
	if (err) {
		delete data;
		return err;
	}
	extra->output->pre_render_data = data;
	extra->output->delete_pre_render_data_func = DisposePreRenderData;
	return err;
}

PF_Err SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra, bool gpu)
{
	PF_Err err = PF_Err_NONE;
	const HG_PreRenderData *data = static_cast<const HG_PreRenderData *>(extra->input->pre_render_data);
	if (!data) return PF_Err_BAD_CALLBACK_PARAM;

	PF_EffectWorld *input = nullptr;
	PF_EffectWorld *output = nullptr;
	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, HG_INPUT, &input));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
	if (!err && output) {
#if defined(HG_GPU_ENABLED)
		if (gpu) {
			err = HG_SmartRenderGPU(in_data, out_data, extra, input, output, *data);
		} else
#endif
		{
			(void)gpu;
			err = HG_SmartRenderCPU(in_data, input, output, *data);
		}
	}
	if (input) {
		const PF_Err checkinErr = extra->cb->checkin_layer_pixels(in_data->effect_ref, HG_INPUT);
		if (!err) err = checkinErr;
	}
	return err;
}

PF_Err UserChangedParam(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
						const PF_UserChangedParamExtra *extra)
{
	if (!extra) return PF_Err_NONE;
	if (extra->param_index == HG_LUMINANCE_SPACE) {
		HG_RefreshColourCache(in_data);
		out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
	}
	if (extra->param_index == HG_GLOW_ONLY) return HG_UpdateParamsUI(in_data, out_data, params);
	return PF_Err_NONE;
}

} // namespace

//-----------------------------------------------------------------------------
extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
													  SPBasicSuite *inSPBasicSuitePtr, const char *inHostName,
													  const char *inHostVersion)
{
	(void)inSPBasicSuitePtr;
	(void)inHostName;
	(void)inHostVersion;
	PF_Err result = (*inPluginDataCallBackPtr)(
		inPtr, reinterpret_cast<const A_u_char *>(NAME), reinterpret_cast<const A_u_char *>(MATCHNAME),
		reinterpret_cast<const A_u_char *>(CATEGORY), reinterpret_cast<const A_u_char *>(AE_ENTRY_POINT), 'eFKT',
		PF_AE_PLUG_IN_VERSION, PF_AE_PLUG_IN_SUBVERS, 0, reinterpret_cast<const A_u_char *>(SUPPORT_URL));
	return result == A_Err_NONE ? PF_Err_NONE : result;
}

extern "C" DllExport PF_Err PluginDataEntryFunction(PF_PluginDataPtr inPtr, PF_PluginDataCB inPluginDataCallBackPtr,
													 SPBasicSuite *inSPBasicSuitePtr, const char *inHostName,
													 const char *inHostVersion)
{
	(void)inSPBasicSuitePtr;
	(void)inHostName;
	(void)inHostVersion;
	PF_Err result = (*inPluginDataCallBackPtr)(
		inPtr, reinterpret_cast<const A_u_char *>(NAME), reinterpret_cast<const A_u_char *>(MATCHNAME),
		reinterpret_cast<const A_u_char *>(CATEGORY), reinterpret_cast<const A_u_char *>(AE_ENTRY_POINT), 'eFKT',
		HG_LEGACY_PLUGIN_API_VERSION, HG_LEGACY_PLUGIN_API_SUBVERS, 0);
	return result == A_Err_NONE ? PF_Err_NONE : result;
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
									   PF_LayerDef *output, void *extra)
{
	(void)output;
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
		case PF_Cmd_ABOUT:
			err = About(in_data, out_data);
			break;
		case PF_Cmd_GLOBAL_SETUP:
			err = GlobalSetup(in_data, out_data);
			break;
		case PF_Cmd_GLOBAL_SETDOWN:
			err = GlobalSetdown(in_data);
			break;
		case PF_Cmd_PARAMS_SETUP:
			err = HG_ParamsSetup(in_data, out_data);
			break;
#if defined(HG_GPU_ENABLED)
		case PF_Cmd_GPU_DEVICE_SETUP:
			err = HG_GPUDeviceSetup(in_data, out_data, static_cast<PF_GPUDeviceSetupExtra *>(extra));
			break;
		case PF_Cmd_GPU_DEVICE_SETDOWN:
			err = HG_GPUDeviceSetdown(in_data, out_data, static_cast<PF_GPUDeviceSetdownExtra *>(extra));
			break;
#endif
		case PF_Cmd_SMART_PRE_RENDER:
			err = PreRender(in_data, out_data, static_cast<PF_PreRenderExtra *>(extra));
			break;
		case PF_Cmd_SMART_RENDER:
			err = SmartRender(in_data, out_data, static_cast<PF_SmartRenderExtra *>(extra), false);
			break;
		case PF_Cmd_SMART_RENDER_GPU:
			err = SmartRender(in_data, out_data, static_cast<PF_SmartRenderExtra *>(extra), true);
			break;
		case PF_Cmd_UPDATE_PARAMS_UI:
			err = HG_UpdateParamsUI(in_data, out_data, params);
			break;
		case PF_Cmd_EVENT:
			err = HG_HandleEvent(in_data, out_data, static_cast<PF_EventExtra *>(extra));
			break;
		case PF_Cmd_USER_CHANGED_PARAM:
			err = UserChangedParam(in_data, out_data, params, static_cast<PF_UserChangedParamExtra *>(extra));
			break;
		default:
			break;
		}
	} catch (PF_Err &thrown) {
		err = thrown;
	} catch (...) {
		err = PF_Err_INTERNAL_STRUCT_DAMAGED;
	}
	return err;
}
