# HaloGlow - self-contained After Effects glow effect.
#
# References ONLY the Adobe After Effects SDK (the Examples tree this repository
# sits in) plus the files in this repository.
#
#   cmake -S . -B build/Win
#   cmake --build build/Win --config Debug
#   ctest --test-dir build/Win -C Debug

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version" FORCE)
endif()

cmake_minimum_required(VERSION 3.18)
project(HaloGlow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

#------------------------------------------------------------------------------
# Host-independent core: shared by the plug-in, tests and calibration tools.
#------------------------------------------------------------------------------
set(HG_CORE_SOURCES
	GPU/HG_PixelMath.h
	Source/HG_Core.h
	Source/HG_Core.cpp
	Source/HG_Coefficients.cpp
	Source/HG_CoefficientTable_v1.inc
	Source/HG_Parallel.h)

add_library(haloglow_core STATIC ${HG_CORE_SOURCES})
target_include_directories(haloglow_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set_target_properties(haloglow_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(haloglow_core PUBLIC Threads::Threads)
if(MSVC)
	# /fp:precise is the MSVC default; spelled out because §12.5 forbids
	# unsafe fast-math on the reference path.
	target_compile_options(haloglow_core PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/fp:precise>")
	target_compile_definitions(haloglow_core PUBLIC $<$<COMPILE_LANGUAGE:C,CXX>:_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>)
else()
	target_compile_options(haloglow_core PUBLIC $<$<COMPILE_LANGUAGE:C,CXX>:-fno-fast-math>)
endif()

add_executable(hg_core_tests tests/hg_core_tests.cpp)
target_link_libraries(hg_core_tests PRIVATE haloglow_core)
add_test(NAME hg_core_tests COMMAND hg_core_tests)

add_executable(hg_impulse tools/hg_impulse.cpp)
target_link_libraries(hg_impulse PRIVATE haloglow_core)

#------------------------------------------------------------------------------
# After Effects plug-in
#------------------------------------------------------------------------------
option(HG_FORCE_CPU_ONLY "Disable all GPU backends (pure CPU build)" OFF)
option(HG_ENABLE_CUDA "Build the CUDA backend when the toolkit is present" ON)
option(HG_ENABLE_OPENCL "Build the OpenCL backend when the SDK is present" ON)
option(HG_ENABLE_METAL "Build the Metal backend on macOS" ON)

if(NOT DEFINED AESDK_ROOT OR NOT EXISTS "${AESDK_ROOT}/Headers")
	get_filename_component(_examples_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
	if(EXISTS "${_examples_dir}/Headers")
		set(AESDK_ROOT "${_examples_dir}" CACHE PATH "After Effects SDK Examples root" FORCE)
	endif()
endif()
if(NOT EXISTS "${AESDK_ROOT}/Headers")
	message(FATAL_ERROR "AESDK_ROOT (${AESDK_ROOT}) has no Headers/ directory. Place this repository "
		"inside the After Effects SDK Examples tree or pass -DAESDK_ROOT=<Examples>.")
endif()
message(STATUS "HaloGlow: AESDK_ROOT = ${AESDK_ROOT}")

find_package(Python3 COMPONENTS Interpreter QUIET)
set(HG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(HG_CUDA_AVAILABLE FALSE)
set(HG_OPENCL_AVAILABLE FALSE)
set(HG_METAL_AVAILABLE FALSE)
if(NOT HG_FORCE_CPU_ONLY)
	if(HG_ENABLE_CUDA AND NOT APPLE)
		include(CheckLanguage)
		check_language(CUDA)
		if(CMAKE_CUDA_COMPILER)
			if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
				set(CMAKE_CUDA_ARCHITECTURES 52 60 70 75 80 86 89)
			endif()
			enable_language(CUDA)
			find_package(CUDAToolkit REQUIRED)
			set(HG_CUDA_AVAILABLE TRUE)
		endif()
	endif()
	if(HG_ENABLE_OPENCL AND NOT APPLE AND Python3_Interpreter_FOUND)
		find_package(OpenCL QUIET)
		if(OpenCL_FOUND)
			set(HG_OPENCL_AVAILABLE TRUE)
		endif()
	endif()
	if(HG_ENABLE_METAL AND APPLE AND Python3_Interpreter_FOUND)
		find_library(METAL_LIBRARY Metal)
		find_library(FOUNDATION_LIBRARY Foundation)
		if(METAL_LIBRARY AND FOUNDATION_LIBRARY)
			set(HG_METAL_AVAILABLE TRUE)
		endif()
	endif()
endif()
message(STATUS "HaloGlow: CUDA=${HG_CUDA_AVAILABLE} OpenCL=${HG_OPENCL_AVAILABLE} Metal=${HG_METAL_AVAILABLE}")

set(HG_PLUGIN_SOURCES
	Source/HaloGlow.h
	Source/HaloGlow.cpp
	Source/HaloGlow_UI.cpp
	Source/HaloGlow_CPU.cpp
	Source/HG_ColourCache.h
	Source/HG_ColourCache.cpp
	Source/HG_Target.h
	Localise/AELocalise.h
	Localise/AE_SDK.h
	Localise/LocKeys.h
	Localise/Strings_en_US.h
	Localise/Strings_ja_JP.h
	Localise/Strings_zh_CN.h
	Localise/Strings_ko_KR.h
	"${AESDK_ROOT}/Util/AEGP_SuiteHandler.cpp"
	"${AESDK_ROOT}/Util/MissingSuiteError.cpp"
	"${AESDK_ROOT}/Util/AEFX_SuiteHelper.c"
	"${AESDK_ROOT}/Util/Smart_Utils.cpp")

# OpenCL and Metal kernels are embedded as byte arrays made of the shared
# pixel math, the shared kernel bodies and that language's entry points.
function(hg_embed_kernel out_header symbol)
	add_custom_command(
		OUTPUT "${out_header}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${HG_GENERATED_DIR}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_kernel.py"
			-o "${out_header}" -n ${symbol} ${ARGN}
		DEPENDS ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_kernel.py"
		COMMENT "Embedding ${symbol}"
		VERBATIM)
endfunction()

set(HG_SHARED_KERNEL_HEADERS
	"${CMAKE_CURRENT_SOURCE_DIR}/GPU/HG_PixelMath.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GPU/HG_Kernels.h")

if(HG_CUDA_AVAILABLE OR HG_OPENCL_AVAILABLE OR HG_METAL_AVAILABLE)
	list(APPEND HG_PLUGIN_SOURCES Source/HG_GPU.cpp Source/HG_GpuRender.h Source/HG_GpuRender.cpp
		GPU/HG_Kernels.h)
endif()
if(HG_CUDA_AVAILABLE)
	list(APPEND HG_PLUGIN_SOURCES GPU/HG_Kernel.cu)
endif()
if(HG_OPENCL_AVAILABLE)
	hg_embed_kernel("${HG_GENERATED_DIR}/HG_Kernel_cl.h" kHG_OpenCLSource
		${HG_SHARED_KERNEL_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/GPU/HG_Kernel.cl")
	list(APPEND HG_PLUGIN_SOURCES GPU/HG_Kernel.cl "${HG_GENERATED_DIR}/HG_Kernel_cl.h")
endif()
if(HG_METAL_AVAILABLE)
	hg_embed_kernel("${HG_GENERATED_DIR}/HG_Kernel_metal.h" kHG_MetalSource
		${HG_SHARED_KERNEL_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/GPU/HG_Kernel.metal")
	list(APPEND HG_PLUGIN_SOURCES GPU/HG_Kernel.metal Source/HG_Metal.mm "${HG_GENERATED_DIR}/HG_Kernel_metal.h")
endif()

set(HG_PIPL_R "${CMAKE_CURRENT_SOURCE_DIR}/PiPL/HaloGlowPiPL.r")
set(HG_TARGET_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Source/HG_Target.h")
set(HG_OUTPUT_NAME "$<IF:$<CONFIG:Debug>,HaloGlow_debug,HaloGlow>")

if(WIN32)
	set(_pipl_tool "${AESDK_ROOT}/Resources/PiPLtool.exe")
	set(_pipl_pre "${CMAKE_CURRENT_SOURCE_DIR}/cmake/preprocess_pipl_win.cmake")
	foreach(_cfg Debug Release)
		set(_i "${CMAKE_CURRENT_BINARY_DIR}/HaloGlowPiPL_${_cfg}.i")
		set(_rc "${CMAKE_CURRENT_BINARY_DIR}/HaloGlowPiPL_${_cfg}.rc")
		if(_cfg STREQUAL "Debug")
			set(_dbg 1)
		else()
			set(_dbg 0)
		endif()
		add_custom_command(
			OUTPUT "${_rc}"
			COMMAND ${CMAKE_COMMAND}
				-D "HG_PIPL_COMPILER=${CMAKE_CXX_COMPILER}"
				-D "HG_PIPL_OUTPUT=${_i}"
				-D "HG_PIPL_SOURCE=${HG_PIPL_R}"
				-D "HG_PIPL_AESDK=${AESDK_ROOT}"
				-D "HG_PIPL_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
				-D "HG_PIPL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
				-D "HG_PIPL_DEBUG=${_dbg}"
				-D "HG_PIPL_HAS_CUDA=${HG_CUDA_AVAILABLE}"
				-D "HG_PIPL_HAS_OPENCL=${HG_OPENCL_AVAILABLE}"
				-P "${_pipl_pre}"
			COMMAND "${_pipl_tool}" "${_i}" "${_rc}"
			DEPENDS "${HG_PIPL_R}" "${HG_TARGET_HEADER}" "${_pipl_pre}"
			COMMENT "Generating PiPL resource (${_cfg})"
			VERBATIM)
		set(HG_PIPL_RC_${_cfg} "${_rc}")
	endforeach()
	list(APPEND HG_PLUGIN_SOURCES "$<IF:$<CONFIG:Debug>,${HG_PIPL_RC_Debug},${HG_PIPL_RC_Release}>")
	add_library(HaloGlow SHARED ${HG_PLUGIN_SOURCES})
	set_target_properties(HaloGlow PROPERTIES SUFFIX ".aex" OUTPUT_NAME "${HG_OUTPUT_NAME}")
	target_compile_definitions(HaloGlow PRIVATE MSWindows _WINDOWS)
elseif(APPLE)
	find_program(REZ_TOOL Rez)
	if(NOT REZ_TOOL)
		message(FATAL_ERROR "Rez not found. Install Xcode or the Command Line Tools.")
	endif()
	set(HG_PIPL_DEFINES)
	if(HG_METAL_AVAILABLE)
		list(APPEND HG_PIPL_DEFINES -D HG_HAS_METAL=1)
	endif()
	foreach(_cfg Debug Release)
		set(_i "${CMAKE_CURRENT_BINARY_DIR}/HaloGlowPiPL_${_cfg}.i")
		set(_rsrc "${CMAKE_CURRENT_BINARY_DIR}/HaloGlowPiPL_${_cfg}.rsrc")
		if(_cfg STREQUAL "Debug")
			set(_dbg -D DEBUG=1 -D HG_DEBUG_BUILD=1)
		else()
			set(_dbg)
		endif()
		add_custom_command(
			OUTPUT "${_i}"
			COMMAND "${CMAKE_CXX_COMPILER}" -E -P -x c -I "${AESDK_ROOT}/Headers" -I "${AESDK_ROOT}/Resources"
				-I "${CMAKE_CURRENT_SOURCE_DIR}/Source" -D __MACH__ -D AE_OS_MAC ${_dbg} ${HG_PIPL_DEFINES}
				-o "${_i}" "${HG_PIPL_R}"
			DEPENDS "${HG_PIPL_R}" "${HG_TARGET_HEADER}")
		add_custom_command(
			OUTPUT "${_rsrc}"
			COMMAND "${REZ_TOOL}" -o "${_rsrc}" -useDF -i "${AESDK_ROOT}/Headers" -i "${AESDK_ROOT}/Resources"
				-i "${CMAKE_CURRENT_SOURCE_DIR}/Source" "${_i}"
			DEPENDS "${_i}")
		set(HG_PIPL_RSRC_${_cfg} "${_rsrc}")
	endforeach()
	add_custom_target(HaloGlow_pipl DEPENDS "${HG_PIPL_RSRC_Debug}" "${HG_PIPL_RSRC_Release}")
	add_library(HaloGlow MODULE ${HG_PLUGIN_SOURCES})
	add_dependencies(HaloGlow HaloGlow_pipl)
	set(BUNDLE_EXECUTABLE "HaloGlow")
	set(BUNDLE_NAME "HaloGlow")
	set(BUNDLE_IDENTIFIER "com.palf.haloglow")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Mac/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
	set_target_properties(HaloGlow PROPERTIES
		BUNDLE TRUE
		BUNDLE_EXTENSION "plugin"
		OUTPUT_NAME "${HG_OUTPUT_NAME}"
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.palf.haloglow"
		OSX_ARCHITECTURES "x86_64;arm64")
	set(_exports "${CMAKE_CURRENT_BINARY_DIR}/exported_symbols.txt")
	file(WRITE "${_exports}" "_EffectMain\n_PluginDataEntryFunction\n_PluginDataEntryFunction2\n")
	target_link_options(HaloGlow PRIVATE -Wl,-exported_symbols_list,${_exports}
		-Wl,-sectcreate,__TEXT,__plgin,$<IF:$<CONFIG:Debug>,${HG_PIPL_RSRC_Debug},${HG_PIPL_RSRC_Release}>)
	# The bundle (not only Contents/MacOS) gets a fresh mtime so the AE plug-in
	# cache sees the new PiPL; the ad-hoc signature must be the last step.
	add_custom_command(TARGET HaloGlow POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HaloGlow>/Contents/Resources"
		COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
			"$<TARGET_BUNDLE_DIR:HaloGlow>/Contents/Info.plist"
		COMMAND ${CMAKE_COMMAND} -E copy "$<IF:$<CONFIG:Debug>,${HG_PIPL_RSRC_Debug},${HG_PIPL_RSRC_Release}>"
			"$<TARGET_BUNDLE_DIR:HaloGlow>/Contents/Resources/HaloGlowPiPL.rsrc"
		COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_BUNDLE_DIR:HaloGlow>"
		COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:HaloGlow>"
		COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:HaloGlow>"
		COMMENT "Assembling .plugin bundle")
	find_library(COCOA_LIBRARY Cocoa)
	target_link_libraries(HaloGlow PRIVATE ${COCOA_LIBRARY})
	target_compile_definitions(HaloGlow PRIVATE MAC_ENV)
endif()

target_include_directories(HaloGlow PRIVATE
	"${AESDK_ROOT}/Headers" "${AESDK_ROOT}/Headers/SP" "${AESDK_ROOT}/Util" "${AESDK_ROOT}/Resources"
	"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_CURRENT_SOURCE_DIR}/GPU"
	"${HG_GENERATED_DIR}")
target_link_libraries(HaloGlow PRIVATE haloglow_core)
target_compile_definitions(HaloGlow PRIVATE $<$<CONFIG:Debug>:HG_DEBUG_BUILD=1>)
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${_cfg}" _up)
	set_target_properties(HaloGlow PROPERTIES
		"RUNTIME_OUTPUT_DIRECTORY_${_up}" "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_SYSTEM_NAME}/${_cfg}"
		"LIBRARY_OUTPUT_DIRECTORY_${_up}" "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CMAKE_SYSTEM_NAME}/${_cfg}")
endforeach()

if(HG_CUDA_AVAILABLE)
	target_compile_definitions(HaloGlow PRIVATE HG_HAS_CUDA=1)
	set_target_properties(HaloGlow PROPERTIES CUDA_RUNTIME_LIBRARY Static CUDA_SEPARABLE_COMPILATION OFF)
	if(MSVC)
		# nvcc host objects use the same dynamic CRT as the rest of the plug-in.
		target_compile_options(HaloGlow PRIVATE
			$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
			$<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Debug>>:-Xcompiler=/MDd>
			$<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<NOT:$<CONFIG:Debug>>>:-Xcompiler=/MD>)
		target_link_options(HaloGlow PRIVATE /NODEFAULTLIB:LIBCMT)
	endif()
	# No --use_fast_math and no FMA contraction on the reference path (§12.5).
	target_compile_options(HaloGlow PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false>)
endif()
if(HG_OPENCL_AVAILABLE)
	target_compile_definitions(HaloGlow PRIVATE HG_HAS_OPENCL=1 CL_TARGET_OPENCL_VERSION=120)
	target_link_libraries(HaloGlow PRIVATE OpenCL::OpenCL)
	if(MSVC)
		target_link_options(HaloGlow PRIVATE "/DELAYLOAD:OpenCL.dll")
		target_link_libraries(HaloGlow PRIVATE delayimp.lib)
	endif()
endif()
if(HG_METAL_AVAILABLE)
	target_compile_definitions(HaloGlow PRIVATE HG_HAS_METAL=1)
	target_link_libraries(HaloGlow PRIVATE ${METAL_LIBRARY} ${FOUNDATION_LIBRARY})
	set_source_files_properties(Source/HG_Metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
