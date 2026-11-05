// HG_Target.h
//
// Plug-in identity, version and global out-flags (spec §18.1). Preprocessor
// #defines only: the PiPL resource (.r) includes this through a C
// preprocessor (MSVC /EP, clang -E) before PiPLtool / Rez.
//
// clang-format off
#pragma once
#ifndef HG_TARGET_H
#define HG_TARGET_H

//-----------------------------------------------------------------------------
// Identity. Match names are persisted in projects: never change them.
//-----------------------------------------------------------------------------
#if defined(_DEBUG) || defined(DEBUG) || defined(HG_DEBUG_BUILD)
	#define NAME			"HaloGlow debug"
	#define MATCHNAME		"PALF HaloGlow debug"
#else
	#define NAME			"HaloGlow"
	#define MATCHNAME		"PALF HaloGlow"
#endif

#define DESCRIPTION		"\nScreen glow built one octave at a time.\rMIT licence."
#define CATEGORY		"Stylize"
#define SUPPORT_URL		"https://x.com/PALF_MovieWorks"

//-----------------------------------------------------------------------------
// Host compatibility: PiPL requirement at the AE 2022 floor (API 13.27).
//-----------------------------------------------------------------------------
#define HG_PIPL_SPEC_VERSION			13
#define HG_PIPL_SPEC_SUBVERS			27
#define HG_LEGACY_PLUGIN_API_VERSION	13
#define HG_LEGACY_PLUGIN_API_SUBVERS	27
// AE_Effect_Support_URL needs API 13.28; the v2 entry point still supplies it.
#define HG_PIPL_HAS_SUPPORT_URL			0

//-----------------------------------------------------------------------------
// Version (PF_Stage: DEVELOP=0, ALPHA=1, BETA=2, RELEASE=3)
//-----------------------------------------------------------------------------
#define	MAJOR_VERSION	1
#define	MINOR_VERSION	0
#define	BUG_VERSION		0
#define	STAGE_VERSION	3		// PF_Stage_RELEASE
#define	BUILD_VERSION	1

// PF_VERSION(1,0,0,3,1) = (1<<19)|(0<<15)|(0<<11)|(3<<9)|1 = 525825.
// PiPLtool rejects shifts, so the packed value is a literal.
#define	HG_VERSION_PACKED	525825

//-----------------------------------------------------------------------------
// Global out-flags. Literals only, identical in the PiPL and GlobalSetup, or
// After Effects rejects the plug-in with a version mismatch.
//
// OUT_FLAGS = PF_OutFlag_DEEP_COLOR_AWARE (1<<25 = 33554432)
//   | PF_OutFlag_SEND_UPDATE_PARAMS_UI (1<<26 = 67108864)
//   | PF_OutFlag_CUSTOM_UI (1<<15 = 32768, the GPU status control)
//   | PF_OutFlag_I_EXPAND_BUFFER (1<<9 = 512)
//   = 100696576.
// Never PF_OutFlag_PIX_INDEPENDENT: the glow spreads pixels.
#define OUT_FLAGS		100696576

// OUT_FLAGS2 base = PF_OutFlag2_SUPPORTS_SMART_RENDER (1<<10 = 1024)
//   | PF_OutFlag2_FLOAT_COLOR_AWARE (1<<12 = 4096)
//   | PF_OutFlag2_I_MIX_GUID_DEPENDENCIES (1<<21 = 2097152)
//   | PF_OutFlag2_SUPPORTS_THREADED_RENDERING (1<<27 = 134217728)
//   | PF_OutFlag2_STRINGS_ARE_UTF8 (1<<30 = 1073741824)
//   = 1210061824; with PF_OutFlag2_SUPPORTS_GPU_RENDER_F32 (1<<25) = 1243616256.
//
// STRINGS_ARE_UTF8 matches AELocalise::SetUTF8FlagDeclared(true). AE 26 uses
// UTF-8 without this flag; AE 27 and later use it only when the flag is set.
// PiPL and GlobalSetup must carry the same literal.
//
// Multi-Frame Rendering is supported: render selectors never call AEGP and
// share no mutable state (spec §3.12.2, §10.3). MIX_GUID lets the Luminance
// Space Auto coefficients enter the frame cache key.
#if defined(HG_HAS_CUDA) || defined(HG_HAS_OPENCL) || defined(HG_HAS_METAL)
	#define OUT_FLAGS2		1243616256
#else
	#define OUT_FLAGS2		1210061824
#endif

#endif // HG_TARGET_H
// clang-format on
