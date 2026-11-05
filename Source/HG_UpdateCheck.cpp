/*
	HG_UpdateCheck.cpp

	Windows half of the GitHub release check. The macOS half is
	HG_UpdateCheck_Mac.mm. Both call the same API and refuse any URL that
	is not this product's release page.
*/

#include "HG_UpdateCheck.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>

namespace {

bool ReadBody(HINTERNET request, std::string &body)
{
	body.clear();
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available)) return false;
		if (available == 0) return true;
		if (body.size() >= 262144) return true;
		if (available > 262144 - body.size()) available = static_cast<DWORD>(262144 - body.size());
		const std::size_t at = body.size();
		body.resize(at + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, body.data() + at, available, &read)) return false;
		body.resize(at + read);
		if (read == 0) return true;
	}
}

bool FetchLatest(std::string &body, int &httpStatus)
{
	body.clear();
	httpStatus = 0;
	HINTERNET session = WinHttpOpen(L"HaloGlow", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
									WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);

	HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = nullptr;
	if (connect) {
		request = WinHttpOpenRequest(connect, L"GET", L"/repos/palf-gh/HaloGlow/releases/latest", nullptr,
									 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	}
	bool ok = false;
	if (request) {
		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
		const wchar_t *headers = L"Accept: application/vnd.github+json\r\n";
		ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			 WinHttpReceiveResponse(request, nullptr);
		if (ok) {
			DWORD status = 0;
			DWORD size = sizeof(status);
			ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
									 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
			httpStatus = ok ? static_cast<int>(status) : 0;
			if (ok && !ReadBody(request, body)) {
				body.clear();
				ok = false;
			}
		}
	}
	if (request) WinHttpCloseHandle(request);
	if (connect) WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok && httpStatus != 0;
}

} // namespace

namespace hg {

ReleaseInfo QueryLatestRelease(ReleaseVersion local)
{
	std::string body;
	int status = 0;
	if (!FetchLatest(body, status)) return ReleaseInfo {};
	return InterpretReleaseBody(status, body.data(), body.size(), local);
}

} // namespace hg

#endif // _WIN32
