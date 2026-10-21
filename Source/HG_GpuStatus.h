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

void HG_RecordGpuDevice(const char *framework, const std::string &deviceName);
void HG_ClearGpuDevice();
bool HG_IsGpuDeviceReady();
bool HG_LastRenderUsedGpu();
void HG_SetLastRenderUsedGpu(bool usedGpu);
std::string HG_GpuFrameworkName();
std::string HG_GpuDeviceName();

#endif // HG_GPU_STATUS_H
