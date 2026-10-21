/*
	HaloGlow_UI.cpp

	Parameter registration (spec §3, §18.2, §18.3), Source Opacity greying in
	Glow Only mode, and the main-thread refresh of the Luminance Space Auto
	cache (§3.12.1). Nothing here runs on render threads.
*/

#include "HaloGlow.h"
#include "HG_ColourCache.h"

#include "Localise/LocKeys.h"
#include "Localise/Strings_en_US.h"
#include "Localise/Strings_ja_JP.h"
#include "Localise/Strings_zh_CN.h"
#include "Localise/Strings_ko_KR.h"
#include "Localise/AELocalise.h"

#include <string>

namespace {

std::string Loc(LocKey::Key key, PF_InData *in_data)
{
	// GetStringForAE returns a thread-local buffer: copy before the next call.
	return AELocalise::GetStringForAE(key, in_data);
}

PF_Err AddPercent(PF_InData *in_data, LocKey::Key key, float validMax, float sliderMax, float dflt,
				  A_long id, A_long flags = 0)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	const std::string name = Loc(key, in_data);
	PF_ADD_FLOAT_SLIDERX(name.c_str(), 0, validMax, 0, sliderMax, dflt, PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT, flags, id);
	return PF_Err_NONE;
}

PF_Err AddFloat(PF_InData *in_data, LocKey::Key key, float validMin, float validMax, float sliderMin,
				float sliderMax, float dflt, PF_Precision precision, A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	const std::string name = Loc(key, in_data);
	PF_ADD_FLOAT_SLIDERX(name.c_str(), validMin, validMax, sliderMin, sliderMax, dflt, precision, 0, 0, id);
	return PF_Err_NONE;
}

PF_Err AddTopic(PF_InData *in_data, LocKey::Key key, A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	const std::string name = Loc(key, in_data);
	PF_ADD_TOPIC(name.c_str(), id);
	return PF_Err_NONE;
}

PF_Err EndTopic(PF_InData *in_data, A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(id);
	return PF_Err_NONE;
}

PF_Err AddColour(PF_InData *in_data, LocKey::Key key, A_u_char r, A_u_char g, A_u_char b, A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	const std::string name = Loc(key, in_data);
	PF_ADD_COLOR(name.c_str(), r, g, b, id);
	return PF_Err_NONE;
}

AEGP_PluginID PluginId(PF_InData *in_data)
{
	if (!in_data || !in_data->global_data) return 0;
	const HG_GlobalData *g = reinterpret_cast<const HG_GlobalData *>(
		PF_LOCK_HANDLE(in_data->global_data));
	const AEGP_PluginID id = g ? g->plugin_id : 0;
	PF_UNLOCK_HANDLE(in_data->global_data);
	return id;
}

std::string MemHandleText(AEGP_MemorySuite1 *mem, AEGP_MemHandle h, bool utf16)
{
	std::string out;
	if (!mem || !h) return out;
	void *p = nullptr;
	AEGP_MemSize size = 0;
	if (mem->AEGP_GetMemHandleSize(h, &size) == A_Err_NONE && mem->AEGP_LockMemHandle(h, &p) == A_Err_NONE && p) {
		if (utf16) {
			const A_UTF16Char *s = static_cast<const A_UTF16Char *>(p);
			const std::size_t n = size / sizeof(A_UTF16Char);
			for (std::size_t i = 0; i < n && s[i]; ++i)
				out.push_back(s[i] < 128 ? static_cast<char>(s[i]) : '?');	// matching is ASCII-only
		} else {
			const char *s = static_cast<const char *>(p);
			for (AEGP_MemSize i = 0; i < size && s[i]; ++i) out.push_back(s[i]);
		}
		mem->AEGP_UnlockMemHandle(h);
	}
	mem->AEGP_FreeMemHandle(h);
	return out;
}

} // namespace

//-----------------------------------------------------------------------------
PF_Err HG_ParamsSetup(PF_InData *in_data, PF_OutData *out_data)
{
	PF_Err err = PF_Err_NONE;
	const hg::Settings d;

	// Read-only GPU status, drawn by HaloGlow_GpuStatusUI.cpp.
	ERR(HG_RegisterCustomUI(in_data));
	if (!err) {
		PF_ParamDef def;
		AEFX_CLR_STRUCT(def);
		const std::string name = Loc(LocKey::STR_GPU_STATUS_NAME, in_data);
		def.param_type = PF_Param_NO_DATA;
		PF_STRNNCPY(def.PF_DEF_NAME, name.c_str(), sizeof(def.PF_DEF_NAME));
		def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
		def.ui_flags = PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL;
		def.ui_width = 280;
		def.ui_height = 40;
		def.uu.id = HG_GPU_STATUS;
		ERR(PF_ADD_PARAM(in_data, -1, &def));
	}

	ERR(AddTopic(in_data, LocKey::STR_TOPIC_BASIC, HG_TOPIC_BASIC_START));
	ERR(AddFloat(in_data, LocKey::STR_INTENSITY, 0, 8, 0, 8, d.intensity, PF_Precision_HUNDREDTHS, HG_INTENSITY));
	ERR(AddFloat(in_data, LocKey::STR_SIZE, 0, 512, 0, 512, d.size, PF_Precision_TENTHS, HG_SIZE));
	ERR(AddPercent(in_data, LocKey::STR_THRESHOLD, 1600, 100, d.threshold, HG_THRESHOLD));
	ERR(AddPercent(in_data, LocKey::STR_THRESHOLD_SOFTNESS, 100, 100, d.softness, HG_THRESHOLD_SOFTNESS));
	ERR(EndTopic(in_data, HG_TOPIC_BASIC_END));

	ERR(AddTopic(in_data, LocKey::STR_TOPIC_SHAPE, HG_TOPIC_SHAPE_START));
	ERR(AddPercent(in_data, LocKey::STR_WIDTH, 400, 400, d.width, HG_WIDTH));
	ERR(AddPercent(in_data, LocKey::STR_HEIGHT, 400, 400, d.height, HG_HEIGHT));
	ERR(AddFloat(in_data, LocKey::STR_FALLOFF, 0, 100, 0, 100, d.falloff, PF_Precision_TENTHS, HG_FALLOFF));
	ERR(EndTopic(in_data, HG_TOPIC_SHAPE_END));

	ERR(AddTopic(in_data, LocKey::STR_TOPIC_TONE, HG_TOPIC_TONE_START));
	ERR(AddFloat(in_data, LocKey::STR_GAMMA, 0.1f, 3, 0.1f, 3, d.gamma, PF_Precision_HUNDREDTHS, HG_GAMMA));
	ERR(AddPercent(in_data, LocKey::STR_SATURATION, 200, 200, d.saturation, HG_SATURATION));
	ERR(AddPercent(in_data, LocKey::STR_COLOUR_STRENGTH, 100, 100, d.colourStrength, HG_COLOUR_STRENGTH));
	ERR(AddColour(in_data, LocKey::STR_COLOUR_1, 0xFF, 0xE4, 0xC4, HG_COLOUR_1));
	ERR(AddColour(in_data, LocKey::STR_COLOUR_2, 0xFF, 0xFF, 0xFF, HG_COLOUR_2));
	if (!err) {
		PF_ParamDef def;
		AEFX_CLR_STRUCT(def);
		const std::string name = Loc(LocKey::STR_LUMINANCE_SPACE, in_data);
		const std::string items = Loc(LocKey::STR_LUMINANCE_SPACE_ITEMS, in_data);
		def.flags = PF_ParamFlag_SUPERVISE;
		PF_ADD_POPUP(name.c_str(), 4, static_cast<A_long>(hg::LuminanceSpace::Auto), items.c_str(),
					 HG_LUMINANCE_SPACE);
	}
	ERR(EndTopic(in_data, HG_TOPIC_TONE_END));

	ERR(AddTopic(in_data, LocKey::STR_TOPIC_OUTPUT, HG_TOPIC_OUTPUT_START));
	ERR(AddPercent(in_data, LocKey::STR_GLOW_OPACITY, 100, 100, d.glowOpacity, HG_GLOW_OPACITY));
	ERR(AddPercent(in_data, LocKey::STR_SOURCE_OPACITY, 100, 100, d.sourceOpacity, HG_SOURCE_OPACITY));
	if (!err) {
		PF_ParamDef def;
		AEFX_CLR_STRUCT(def);
		const std::string name = Loc(LocKey::STR_GLOW_ONLY, in_data);
		static std::string label;	// AE keeps the pointer until PARAMS_SETUP returns
		label = Loc(LocKey::STR_GLOW_ONLY_CHECKBOX, in_data);
		PF_ADD_CHECKBOX(name.c_str(), label.c_str(), FALSE, PF_ParamFlag_SUPERVISE, HG_GLOW_ONLY);
	}
	ERR(EndTopic(in_data, HG_TOPIC_OUTPUT_END));

	if (!err) {
		// Hidden algorithm version (§18.2): new instances store the current
		// version; existing projects keep theirs. Non-X macro keeps ui_flags.
		PF_ParamDef def;
		AEFX_CLR_STRUCT(def);
		const std::string name = Loc(LocKey::STR_ALGORITHM_VERSION, in_data);
		def.ui_flags = PF_PUI_INVISIBLE;
		def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
		PF_ADD_SLIDER(name.c_str(), 1, 1000, 1, 1000, hg::kAlgorithmVersion, HG_ALGORITHM_VERSION);
	}

	out_data->num_params = HG_NUM_PARAMS;
	return err;
}

//-----------------------------------------------------------------------------
PF_Err HG_UpdateParamsUI(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[])
{
	(void)out_data;
	HG_RefreshColourCache(in_data);
	if (!params || !params[HG_GLOW_ONLY] || !params[HG_SOURCE_OPACITY]) return PF_Err_NONE;

	const bool glowOnly = params[HG_GLOW_ONLY]->u.bd.value != 0;
	PF_ParamDef def = *params[HG_SOURCE_OPACITY];
	const bool disabled = (def.ui_flags & PF_PUI_DISABLED) != 0;
	if (disabled == glowOnly) return PF_Err_NONE;
	if (glowOnly) def.ui_flags |= PF_PUI_DISABLED;
	else def.ui_flags &= ~PF_PUI_DISABLED;

	AEFX_SuiteScoper<PF_ParamUtilsSuite3> paramUtils(in_data, kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3,
													  out_data);
	return paramUtils->PF_UpdateParamUI(in_data->effect_ref, HG_SOURCE_OPACITY, &def);
}

//-----------------------------------------------------------------------------
// Main thread only (UPDATE_PARAMS_UI, USER_CHANGED_PARAM). Never call from a
// render selector: AEGP is illegal there under Multi-Frame Rendering.
void HG_RefreshColourCache(PF_InData *in_data)
{
	if (!in_data || !in_data->pica_basicP) return;
	const AEGP_PluginID pluginId = PluginId(in_data);
	if (!pluginId) return;

	// Suite version 6 (AE 25.1+) is the first with the OCIO queries; older
	// hosts leave Auto on its Rec.709 compatibility fallback.
	SPBasicSuite *sp = in_data->pica_basicP;
	const AEGP_ColorSettingsSuite6 *colour = nullptr;
	if (sp->AcquireSuite(kAEGPColorSettingsSuite, kAEGPColorSettingsSuiteVersion6,
						 reinterpret_cast<const void **>(&colour)) != kSPNoError || !colour) {
		return;
	}
	std::string name;
	try {
		AEGP_SuiteHandler suites(sp);
		AEGP_MemorySuite1 *mem = suites.MemorySuite1();
		A_Boolean ocio = FALSE;
		if (colour->AEGP_IsOCIOColorManagementUsed &&
			colour->AEGP_IsOCIOColorManagementUsed(pluginId, &ocio) == A_Err_NONE && ocio) {
			AEGP_MemHandle h = nullptr;
			if (colour->AEGPD_GetOCIOWorkingColorSpace(pluginId, &h) == A_Err_NONE)
				name = MemHandleText(mem, h, false);
		} else {
			AEGP_LayerH layer = nullptr;
			AEGP_CompH comp = nullptr;
			if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layer) == A_Err_NONE &&
				layer && suites.LayerSuite9()->AEGP_GetLayerParentComp(layer, &comp) == A_Err_NONE && comp) {
				AEGP_ColorProfileP profile = nullptr;
				if (colour->AEGP_GetNewWorkingSpaceColorProfile(pluginId, comp, &profile) == A_Err_NONE && profile) {
					AEGP_MemHandle h = nullptr;
					if (colour->AEGP_GetNewColorProfileDescription(pluginId, profile, &h) == A_Err_NONE)
						name = MemHandleText(mem, h, true);
					colour->AEGP_DisposeColorProfile(profile);
				}
			}
		}
	} catch (...) {
		name.clear();	// a missing suite only means "nothing readable"
	}
	sp->ReleaseSuite(kAEGPColorSettingsSuite, kAEGPColorSettingsSuiteVersion6);
	hg::PublishWorkingSpaceName(name);
}
