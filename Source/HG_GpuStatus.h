/*
	HG_GpuStatus.h

	Process-wide GPU status for the read-only "GPU Acceleration" control.
	Device setup and setdown record the framework and device names (mutex,
	host setup threads only); renders only store one atomic flag, so they never
	wait on the lock.
*/

#pragma once
#ifndef HG_GPU_STATUS_H
#define HG_GPU_STATUS_H

#include <string>

// The read-only GPU status row. A newer GitHub release adds one line below it.
constexpr int HG_kGpuStatusHeight = 40;
constexpr int HG_kUpdateLineHeight = 18;

void HG_RecordGpuDevice(const char *framework, const std::string &deviceName);
void HG_ClearGpuDevice();
bool HG_IsGpuDeviceReady();
bool HG_LastRenderUsedGpu();
void HG_SetLastRenderUsedGpu(bool usedGpu);
std::string HG_GpuFrameworkName();
std::string HG_GpuDeviceName();

#endif // HG_GPU_STATUS_H
