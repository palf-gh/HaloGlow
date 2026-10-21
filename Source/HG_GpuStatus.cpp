/*
	HG_GpuStatus.cpp

	See HG_GpuStatus.h.
*/

#include "HG_GpuStatus.h"

#include <atomic>
#include <mutex>

namespace {

std::mutex &StateMutex()
{
	static std::mutex m;
	return m;
}

std::string &FrameworkStorage()
{
	static std::string s;
	return s;
}

std::string &DeviceStorage()
{
	static std::string s;
	return s;
}

std::atomic<bool> &ReadyStorage()
{
	static std::atomic<bool> v { false };
	return v;
}

std::atomic<bool> &LastGpuStorage()
{
	static std::atomic<bool> v { false };
	return v;
}

} // namespace

void HG_RecordGpuDevice(const char *framework, const std::string &deviceName)
{
	std::lock_guard<std::mutex> lock(StateMutex());
	FrameworkStorage() = framework ? framework : "";
	DeviceStorage() = deviceName;
	ReadyStorage().store(!FrameworkStorage().empty(), std::memory_order_release);
	LastGpuStorage().store(false, std::memory_order_release);
}

void HG_ClearGpuDevice()
{
	std::lock_guard<std::mutex> lock(StateMutex());
	FrameworkStorage().clear();
	DeviceStorage().clear();
	ReadyStorage().store(false, std::memory_order_release);
	LastGpuStorage().store(false, std::memory_order_release);
}

bool HG_IsGpuDeviceReady()
{
	return ReadyStorage().load(std::memory_order_acquire);
}

bool HG_LastRenderUsedGpu()
{
	return LastGpuStorage().load(std::memory_order_acquire);
}

void HG_SetLastRenderUsedGpu(bool usedGpu)
{
	LastGpuStorage().store(usedGpu, std::memory_order_release);
}

std::string HG_GpuFrameworkName()
{
	std::lock_guard<std::mutex> lock(StateMutex());
	return FrameworkStorage();
}

std::string HG_GpuDeviceName()
{
	std::lock_guard<std::mutex> lock(StateMutex());
	return DeviceStorage();
}
