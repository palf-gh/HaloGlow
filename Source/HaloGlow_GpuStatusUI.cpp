/*
	HaloGlow_GpuStatusUI.cpp

	Draws the read-only "GPU Acceleration" control with Drawbot: one status
	line (green when the last render ran on the GPU, red otherwise) and, when
	active, "framework · device". Same layout as the Palf plug-ins' shared
	status control, kept in-tree so HaloGlow stays self-contained.

	PF_Cmd_EVENT must never return an error: AE sends lifecycle events where
	params are absent, and an error drops the whole Effect Controls row.
*/

#include "HaloGlow.h"
#include "HG_GpuStatus.h"

#include "AE_EffectUI.h"
#include "AE_EffectSuites.h"

#include "Localise/LocKeys.h"
#include "Localise/Strings_en_US.h"
#include "Localise/Strings_ja_JP.h"
#include "Localise/Strings_zh_CN.h"
#include "Localise/Strings_ko_KR.h"
#include "Localise/AELocalise.h"

#include <algorithm>
#include <string>
#include <vector>

#ifdef AE_OS_MAC
	#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

std::vector<DRAWBOT_UTF16Char> Utf8ToDrawbot(const std::string &text)
{
	std::vector<DRAWBOT_UTF16Char> utf16;
#ifdef AE_OS_WIN
	const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (count > 1) {
		std::vector<wchar_t> wide(static_cast<std::size_t>(count));
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
		utf16.assign(wide.begin(), wide.end());
	}
#else
	CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);
	if (value) {
		const CFIndex length = CFStringGetLength(value);
		utf16.resize(static_cast<std::size_t>(length) + 1);
		CFStringGetCharacters(value, CFRangeMake(0, length), reinterpret_cast<UniChar *>(utf16.data()));
		utf16[static_cast<std::size_t>(length)] = 0;
		CFRelease(value);
	}
#endif
	if (utf16.empty()) utf16.push_back(0);
	return utf16;
}

void DrawLine(const DRAWBOT_Suites &s, DRAWBOT_SurfaceRef surface, DRAWBOT_BrushRef brush, DRAWBOT_FontRef font,
			  const std::string &text, float x, float baseline, float width)
{
	if (text.empty()) return;
	const std::vector<DRAWBOT_UTF16Char> utf16 = Utf8ToDrawbot(text);
	DRAWBOT_PointF32 origin { x, baseline };
	s.surface_suiteP->DrawString(surface, brush, font, utf16.data(), &origin, kDRAWBOT_TextAlignment_Left,
								 kDRAWBOT_TextTruncation_EndEllipsis, width);
}

void DrawStatus(PF_InData *in_data, PF_OutData *out_data, PF_EventExtra *extra)
{
	DRAWBOT_Suites suites;
	if (AEFX_AcquireDrawbotSuites(in_data, out_data, &suites) != PF_Err_NONE) return;

	DRAWBOT_DrawRef draw = nullptr;
	PF_EffectCustomUISuite1 *custom = nullptr;
	if (AEFX_AcquireSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, nullptr,
						  reinterpret_cast<void **>(&custom)) == PF_Err_NONE && custom) {
		custom->PF_GetDrawingReference(extra->contextH, &draw);
		AEFX_ReleaseSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, nullptr);
	}
	DRAWBOT_SupplierRef supplier = nullptr;
	DRAWBOT_SurfaceRef surface = nullptr;
	if (draw) {
		suites.drawbot_suiteP->GetSupplier(draw, &supplier);
		suites.drawbot_suiteP->GetSurface(draw, &surface);
	}
	if (supplier && surface) {
		const bool ready = HG_IsGpuDeviceReady();
		const bool active = ready && HG_LastRenderUsedGpu();
		// GetStringUTF8 returns a shared buffer: copy before the next call.
		const std::string line = AELocalise::GetStringUTF8(
			active ? LocKey::STR_GPU_STATUS_ACTIVE
				   : (ready ? LocKey::STR_GPU_STATUS_READY : LocKey::STR_GPU_STATUS_UNAVAILABLE),
			in_data);
		std::string detail = HG_GpuFrameworkName();
		const std::string device = HG_GpuDeviceName();
		if (!device.empty()) {
			if (!detail.empty()) detail += " \xC2\xB7 ";
			detail += device;
		}

		float fontSize = 10.0f;
		suites.supplier_suiteP->GetDefaultFontSize(supplier, &fontSize);
		DRAWBOT_FontRef font = nullptr;
		suites.supplier_suiteP->NewDefaultFont(supplier, fontSize, &font);
		DRAWBOT_ColorRGBA colour {};
		colour.red = active ? 0.0f : 0.9f;
		colour.green = active ? 0.8f : 0.1f;
		colour.blue = active ? 0.0f : 0.1f;
		colour.alpha = 1.0f;
		DRAWBOT_BrushRef brush = nullptr;
		suites.supplier_suiteP->NewBrush(supplier, &colour, &brush);
		if (font && brush) {
			const PF_Rect &frame = extra->effect_win.current_frame;
			const float x = static_cast<float>(frame.left) + 4.0f;
			const float width = (std::max)(0.0f, static_cast<float>(frame.right - frame.left) - 8.0f);
			const float top = static_cast<float>(frame.top);
			DrawLine(suites, surface, brush, font, line, x, top + fontSize + 2.0f, width);
			if (active) DrawLine(suites, surface, brush, font, detail, x, top + fontSize * 2.0f + 5.0f, width);
		}
		if (brush) suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(brush));
		if (font) suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(font));
	}
	AEFX_ReleaseDrawbotSuites(in_data, out_data);
}

} // namespace

PF_Err HG_RegisterCustomUI(PF_InData *in_data)
{
	PF_CustomUIInfo ci;
	AEFX_CLR_STRUCT(ci);
	ci.events = PF_CustomEFlag_EFFECT;
	ci.comp_ui_alignment = PF_UIAlignment_NONE;
	ci.layer_ui_alignment = PF_UIAlignment_NONE;
	ci.preview_ui_alignment = PF_UIAlignment_NONE;
	return (*in_data->inter.register_ui)(in_data->effect_ref, &ci);
}

PF_Err HG_HandleEvent(PF_InData *in_data, PF_OutData *out_data, PF_EventExtra *extra)
{
	if (!in_data || !out_data || !extra) return PF_Err_NONE;
	if (extra->e_type == PF_Event_DRAW && extra->effect_win.area == PF_EA_CONTROL &&
		extra->effect_win.index == HG_GPU_STATUS) {
		DrawStatus(in_data, out_data, extra);
		extra->evt_out_flags |= PF_EO_HANDLED_EVENT;
	}
	return PF_Err_NONE;
}
