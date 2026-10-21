/*
	hg_impulse.cpp

	Dumps the impulse responses of every basis of the shipped CPU pyramid
	(level 0 delta, pyramid levels 1..L and the local FIR bases) for a set of
	sub-lattice phases. tools/calibrate_v1.py fits the coefficient table to
	these responses, so the table is calibrated against the exact filter the
	plug-in runs (spec §7.4), not a re-implementation.

	Usage: hg_impulse <out_dir> <size> <max_level> [phase_x phase_y]...
	Writes basis_<phase>_<k>.f32 (size*size little-endian float32, red channel)
	and manifest.json.
*/

#include "HG_Core.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool WriteFloats(const std::string &path, const std::vector<float> &data)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	const bool ok = std::fwrite(data.data(), sizeof(float), data.size(), f) == data.size();
	std::fclose(f);
	return ok;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: hg_impulse <out_dir> <size> <max_level> [px py]...\n");
		return 2;
	}
	const std::string outDir = argv[1];
	const int size = std::atoi(argv[2]);
	const int maxLevel = std::atoi(argv[3]);
	if (size < 16 || maxLevel < 1 || maxLevel > hg::kMaxLevels) {
		std::fprintf(stderr, "bad size or level count\n");
		return 2;
	}
	std::vector<std::pair<int, int>> phases;
	for (int i = 4; i + 1 < argc; i += 2) phases.emplace_back(std::atoi(argv[i]), std::atoi(argv[i + 1]));
	if (phases.empty()) phases.emplace_back(0, 0);

	const hg::Rect frame { 0, 0, size, size };
	hg::Settings settings;			// isotropic, 100 % / 100 %
	const hg::RenderScale scale;
	// Basis order: 0 delta, 1..H local FIRs, then a_1..a_L, then c_1..c_L.
	const int localCount = hg::kLocalBasisCount;
	const int basisCount = 1 + localCount + 2 * maxLevel;

	FILE *manifest = std::fopen((outDir + "/manifest.json").c_str(), "w");
	if (!manifest) {
		std::fprintf(stderr, "cannot write manifest\n");
		return 1;
	}
	std::fprintf(manifest, "{\n  \"size\": %d,\n  \"max_level\": %d,\n  \"algorithm_version\": %d,\n"
						   "  \"basis_count\": %d,\n  \"local_count\": %d,\n  \"phases\": [", size, maxLevel,
				 hg::kAlgorithmVersion, basisCount, localCount);

	for (std::size_t p = 0; p < phases.size(); ++p) {
		const int cx = size / 2 + phases[p].first;
		const int cy = size / 2 + phases[p].second;
		std::fprintf(manifest, "%s[%d, %d]", p ? ", " : "", phases[p].first, phases[p].second);

		hg::Image emission;
		emission.Reset(frame);
		emission.pixels[std::size_t(cy) * size + cx] = HG_RGBA { 1.f, 0.f, 0.f, 0.f };

		for (int k = 0; k < basisCount; ++k) {
			hg::Coefficients c;
			if (k == 0) {
				c.a[0] = 1.0f;
			} else if (k <= localCount) {
				c.b[k - 1] = 1.0f;
			} else if (k <= localCount + maxLevel) {
				const int j = k - localCount;
				c.a[j] = 1.0f;
				c.topLevel = j;
			} else {
				const int j = k - localCount - maxLevel;
				c.c[j] = 1.0f;
				c.topLevel = j;
			}
			const hg::Plan plan = hg::BuildPlanWithCoefficients(settings, scale, frame, frame, c);
			hg::Image filtered;
			hg::FilterEmission(plan, emission, filtered);
			std::vector<float> red(std::size_t(size) * size);
			for (std::size_t i = 0; i < red.size(); ++i) red[i] = filtered.pixels[i].r;
			const std::string path = outDir + "/basis_" + std::to_string(p) + "_" + std::to_string(k) + ".f32";
			if (!WriteFloats(path, red)) {
				std::fprintf(stderr, "cannot write %s\n", path.c_str());
				std::fclose(manifest);
				return 1;
			}
		}
	}
	std::fprintf(manifest, "]\n}\n");
	std::fclose(manifest);
	return 0;
}
