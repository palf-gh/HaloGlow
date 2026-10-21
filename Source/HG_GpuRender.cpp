/*
	HG_GpuRender.cpp

	See HG_GpuRender.h. Stage order and buffer domains mirror hg::RenderCPU /
	hg::FilterEmission exactly; kernels are issued on one in-order queue, so
	each stage sees the previous one complete (spec §7.3).
*/

#include "HG_GpuRender.h"

#include <cstring>
#include <vector>

namespace hg {
namespace gpu {
namespace {

HG_Buf Desc(const Rect &r)
{
	HG_Buf b;
	std::memset(&b, 0, sizeof(b));
	b.x0 = r.x0;
	b.y0 = r.y0;
	b.w = r.Width();
	b.h = r.Height();
	b.pitch = b.w;
	return b;
}

HG_Buf Desc(const World &w)
{
	HG_Buf b;
	std::memset(&b, 0, sizeof(b));
	if (!w.mem) return b;
	b.x0 = w.x0;
	b.y0 = w.y0;
	b.w = w.width;
	b.h = w.height;
	b.pitch = w.pitch;
	return b;
}

// Owns every allocation of one render; frees them on scope exit.
class Pool {
public:
	explicit Pool(Backend &b) : backend_(b) {}
	~Pool()
	{
		// Wait for the queue first: a buffer may still be read by a kernel.
		if (!owned_.empty()) backend_.Finish();
		for (void *m : owned_) backend_.Free(m);
	}
	void *Get(std::size_t pixels)
	{
		void *m = backend_.Allocate((pixels > 0 ? pixels : 1) * 16);
		if (m) owned_.push_back(m);
		return m;
	}

private:
	Backend &backend_;
	std::vector<void *> owned_;
};

struct Level {
	void *d = nullptr;
	void *g = nullptr;
	HG_Buf dDesc {};
	HG_Buf gDesc {};
};

} // namespace

Status Render(const Plan &plan, Backend &backend, const World &src, const World &dst)
{
	Pool pool(backend);
	void *dummy = pool.Get(1);
	if (!dummy) return Status::OutOfMemory;
	const void *srcMem = src.mem ? src.mem : dummy;
	const HG_Buf srcDesc = Desc(src);

	HG_FinalArgs fa;
	std::memset(&fa, 0, sizeof(fa));
	fa.src = srcDesc;
	fa.dst = Desc(dst);
	fa.glowActive = plan.glowActive ? 1 : 0;
	fa.pixel = plan.pixel;
	fa.hasLocal = plan.hasLocalSpread ? 1 : 0;
	fa.a0 = plan.coefficients.a[0];
	std::memcpy(fa.localWeights, plan.localKernel, sizeof(fa.localWeights));
	const void *g1[2] = { dummy, dummy };

	if (plan.glowActive) {
		fa.pyramidCount = plan.pyramidCount;
		for (int p = 0; p < plan.pyramidCount; ++p) {
			const PyramidPlan &pyr = plan.pyramid[p];
			const int L = pyr.levelCount;
			std::vector<Level> lv(L + 2);
			for (int j = 1; j <= L; ++j) {
				lv[j].dDesc = Desc(pyr.level[j].dDomain);
				lv[j].gDesc = Desc(pyr.level[j].gDomain);
				lv[j].d = pool.Get(std::size_t(lv[j].dDesc.w) * lv[j].dDesc.h);
				lv[j].g = pool.Get(std::size_t(lv[j].gDesc.w) * lv[j].gDesc.h);
				if (!lv[j].d || !lv[j].g) return Status::OutOfMemory;
			}
			for (int j = 1; j <= L; ++j) {
				if (lv[j].dDesc.w == 0 || lv[j].dDesc.h == 0) continue;
				HG_DownArgs da;
				std::memset(&da, 0, sizeof(da));
				da.src = (j == 1) ? srcDesc : lv[j - 1].dDesc;
				da.dst = lv[j].dDesc;
				da.halveX = pyr.level[j].halveX ? 1 : 0;
				da.halveY = pyr.level[j].halveY ? 1 : 0;
				da.srcIsWorld = (j == 1) ? 1 : 0;
				da.pixel = plan.pixel;
				if (!backend.Down(j == 1 ? srcMem : lv[j - 1].d, lv[j].d, da)) return Status::LaunchFailed;
			}
			for (int j = L; j >= 1; --j) {
				if (lv[j].gDesc.w == 0 || lv[j].gDesc.h == 0) continue;
				HG_AccumArgs aa;
				std::memset(&aa, 0, sizeof(aa));
				aa.d = lv[j].dDesc;
				aa.dst = lv[j].gDesc;
				aa.a = plan.coefficients.a[j];
				aa.c = plan.coefficients.c[j];
				aa.blurX = pyr.level[j].blurX ? 1 : 0;
				aa.blurY = pyr.level[j].blurY ? 1 : 0;
				aa.hasCoarse = j < L ? 1 : 0;
				const void *coarse = dummy;
				if (j < L) {
					aa.coarse = lv[j + 1].gDesc;
					aa.upX = pyr.level[j + 1].halveX ? 1 : 0;
					aa.upY = pyr.level[j + 1].halveY ? 1 : 0;
					coarse = lv[j + 1].g;
				}
				if (!backend.Accum(lv[j].d, coarse, lv[j].g, aa)) return Status::LaunchFailed;
			}
			if (L >= 1) {
				g1[p] = lv[1].g;
				if (p == 0) {
					fa.g0 = lv[1].gDesc;
					fa.up0X = pyr.level[1].halveX ? 1 : 0;
					fa.up0Y = pyr.level[1].halveY ? 1 : 0;
					fa.weight0 = pyr.weight;
				} else {
					fa.g1 = lv[1].gDesc;
					fa.up1X = pyr.level[1].halveX ? 1 : 0;
					fa.up1Y = pyr.level[1].halveY ? 1 : 0;
					fa.weight1 = pyr.weight;
				}
			}
		}
	}
	if (fa.dst.w == 0 || fa.dst.h == 0) return Status::Ok;
	if (!backend.Final(srcMem, dst.mem, g1[0], g1[1], fa)) return Status::LaunchFailed;
	return backend.Finish() ? Status::Ok : Status::LaunchFailed;
}

} // namespace gpu
} // namespace hg
