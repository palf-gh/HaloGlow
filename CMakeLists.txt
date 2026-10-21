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
	target_compile_options(haloglow_core PUBLIC /utf-8 /fp:precise)
	target_compile_definitions(haloglow_core PUBLIC _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
else()
	target_compile_options(haloglow_core PUBLIC -fno-fast-math)
endif()

add_executable(hg_core_tests tests/hg_core_tests.cpp)
target_link_libraries(hg_core_tests PRIVATE haloglow_core)
add_test(NAME hg_core_tests COMMAND hg_core_tests)

add_executable(hg_impulse tools/hg_impulse.cpp)
target_link_libraries(hg_impulse PRIVATE haloglow_core)
