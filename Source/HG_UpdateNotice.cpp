/*
	HG_UpdateNotice.cpp

	One background check per process (spec §18.4). The thread only talks to
	GitHub and stores a version. Effect Controls read that result on the UI
	thread and, when a newer release exists, grow the GPU status row by one line.
*/

#include "HG_UpdateCheck.h"
#include "HG_Target.h"

#include <atomic>
#include <thread>

namespace {

std::atomic<int> g_phase { 0 }; // 0 idle, 1 running, 2 finished with nothing to show, 3 available
std::atomic<int> g_major { 0 };
std::atomic<int> g_minor { 0 };
std::atomic<int> g_bug { 0 };
std::thread g_thread;

void Check()
{
	try {
		hg::ReleaseVersion local;
		local.major = MAJOR_VERSION;
		local.minor = MINOR_VERSION;
		local.bug = BUG_VERSION;
		local.ok = true;
		const hg::ReleaseInfo info = hg::QueryLatestRelease(local);
		if (info.status == hg::ReleaseInfo::Status::Available) {
			g_major.store(info.remote.major, std::memory_order_relaxed);
			g_minor.store(info.remote.minor, std::memory_order_relaxed);
			g_bug.store(info.remote.bug, std::memory_order_relaxed);
			g_phase.store(3, std::memory_order_release);
			return;
		}
	} catch (...) {
	}
	g_phase.store(2, std::memory_order_release);
}

} // namespace

namespace hg {

void StartUpdateCheck()
{
	int idle = 0;
	if (!g_phase.compare_exchange_strong(idle, 1, std::memory_order_acq_rel)) return;
	try {
		g_thread = std::thread(Check);
	} catch (...) {
		g_phase.store(2, std::memory_order_release);
	}
}

void FinishUpdateCheck()
{
	if (g_thread.joinable()) g_thread.join();
}

bool UpdateVersion(int &major, int &minor, int &bug)
{
	if (g_phase.load(std::memory_order_acquire) != 3) return false;
	major = g_major.load(std::memory_order_relaxed);
	minor = g_minor.load(std::memory_order_relaxed);
	bug = g_bug.load(std::memory_order_relaxed);
	return major >= 0 && minor >= 0 && bug >= 0;
}

} // namespace hg
