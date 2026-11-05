/*
	HG_UpdateCheck.h

	GitHub Releases check (spec §18.4). The plug-in asks
	api.github.com whether a newer public release exists, then shows
	that result. It does not download, replace, or open a page, and it
	does not run during rendering.

	The tag on a full release must be vMAJOR.MINOR.BUG, matching
	MAJOR_VERSION / MINOR_VERSION / BUG_VERSION in HG_Target.h.
*/

#pragma once
#ifndef HG_UPDATE_CHECK_H
#define HG_UPDATE_CHECK_H

#include <cstddef>
#include <string>

namespace hg {

struct ReleaseVersion {
	int major = 0;
	int minor = 0;
	int bug = 0;
	bool ok = false;
};

// "v1.2.3" or "1.2.3". A leading v is optional. Extra text is rejected.
inline ReleaseVersion ParseReleaseTag(const char *tag, std::size_t length)
{
	ReleaseVersion v;
	if (!tag) return v;
	const char *p = tag;
	const char *end = tag + length;
	if (p < end && (*p == 'v' || *p == 'V')) ++p;

	auto component = [&](int &out) -> bool {
		if (p >= end || *p < '0' || *p > '9') return false;
		if (*p == '0' && (p + 1) < end && p[1] >= '0' && p[1] <= '9') return false;
		int value = 0;
		while (p < end && *p >= '0' && *p <= '9') {
			value = value * 10 + (*p - '0');
			if (value > 9999) return false;
			++p;
		}
		out = value;
		return true;
	};

	if (!component(v.major) || p >= end || *p != '.') return v;
	++p;
	if (!component(v.minor) || p >= end || *p != '.') return v;
	++p;
	if (!component(v.bug) || p != end) return v;
	v.ok = true;
	return v;
}

// Positive when a is newer than b. Both sides must be ok.
inline int CompareRelease(ReleaseVersion a, ReleaseVersion b)
{
	if (a.major != b.major) return a.major - b.major;
	if (a.minor != b.minor) return a.minor - b.minor;
	return a.bug - b.bug;
}

inline bool IsHaloGlowReleasePage(const std::string &url)
{
	static const char kPrefix[] = "https://github.com/palf-gh/HaloGlow/releases/tag/";
	const std::size_t prefix = sizeof(kPrefix) - 1;
	if (url.size() < prefix + 1 || url.size() > 500) return false;
	if (url.compare(0, prefix, kPrefix) != 0) return false;
	for (unsigned char c : url) {
		if (c <= 0x20 || c == '"' || c == '\\' || c == '<' || c == '>' || c == '@') return false;
	}
	return true;
}

struct ReleaseInfo {
	enum class Status { Failed, NoRelease, Current, Ahead, Available };
	Status status = Status::Failed;
	ReleaseVersion remote;
	std::string page;
};

// Reads one JSON string that follows "key". from is the scan start and is
// advanced past the value. GitHub escapes slashes as \/.
inline bool JsonStringForKey(const char *body, std::size_t length, const char *key, std::size_t &from,
							 std::string &out)
{
	out.clear();
	if (!body || !key) return false;
	const std::size_t keyLen = std::char_traits<char>::length(key);
	while (from + keyLen + 2 < length) {
		if (body[from] == '"' && from + 1 + keyLen < length &&
			std::char_traits<char>::compare(body + from + 1, key, keyLen) == 0 &&
			body[from + 1 + keyLen] == '"') {
			std::size_t i = from + 2 + keyLen;
			while (i < length && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) ++i;
			if (i >= length || body[i] != ':') {
				from = i;
				continue;
			}
			++i;
			while (i < length && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) ++i;
			if (i >= length || body[i] != '"') {
				from = i;
				continue;
			}
			++i;
			std::string value;
			while (i < length) {
				const char c = body[i++];
				if (c == '"') {
					from = i;
					out.swap(value);
					return true;
				}
				if (c != '\\') {
					value.push_back(c);
					continue;
				}
				if (i >= length) return false;
				const char e = body[i++];
				if (e == '/' || e == '\\' || e == '"') value.push_back(e);
				else if (e == 'n') value.push_back('\n');
				else if (e == 'r') value.push_back('\r');
				else if (e == 't') value.push_back('\t');
				else return false;
			}
			return false;
		}
		++from;
	}
	return false;
}

// httpStatus 404 is "no release". Any other non-200, or a 200 body that is
// not a usable HaloGlow release, is a failure. An older published tag is
// Ahead: this copy is newer than GitHub.
inline ReleaseInfo InterpretReleaseBody(int httpStatus, const char *body, std::size_t length, ReleaseVersion local)
{
	ReleaseInfo info;
	if (httpStatus == 404) {
		info.status = ReleaseInfo::Status::NoRelease;
		return info;
	}
	if (httpStatus != 200 || !body || !local.ok) return info;

	std::size_t from = 0;
	std::string tag;
	if (!JsonStringForKey(body, length, "tag_name", from, tag)) return info;
	info.remote = ParseReleaseTag(tag.data(), tag.size());
	if (!info.remote.ok) return info;

	from = 0;
	std::string page;
	while (JsonStringForKey(body, length, "html_url", from, page)) {
		if (IsHaloGlowReleasePage(page)) {
			info.page = page;
			break;
		}
	}

	const int cmp = CompareRelease(info.remote, local);
	if (cmp > 0) {
		if (info.page.empty()) return info;
		info.status = ReleaseInfo::Status::Available;
	} else if (cmp < 0) {
		info.status = ReleaseInfo::Status::Ahead;
	} else {
		info.status = ReleaseInfo::Status::Current;
	}
	return info;
}

// Blocks for a few seconds. Safe on a background thread. Never call it from a render.
ReleaseInfo QueryLatestRelease(ReleaseVersion local);

// One process-wide check, started from Global Setup. The Effect Controls
// grow by one line only after this reports a newer release.
void StartUpdateCheck();
void FinishUpdateCheck();
bool UpdateVersion(int &major, int &minor, int &bug);

} // namespace hg

#endif // HG_UPDATE_CHECK_H
