/*
	HG_UpdateCheck_Mac.mm

	macOS half of the GitHub release check. Redirects are refused, and the
	notice only reports the result.
*/

#include "HG_UpdateCheck.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <string>

namespace {

class FetchResult {
public:
	int status = 0;
	std::string body;
};

} // namespace

@interface HGUpdateSessionDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation HGUpdateSessionDelegate
- (void)URLSession:(NSURLSession *)session
					task:(NSURLSessionTask *)task
	willPerformHTTPRedirection:(NSHTTPURLResponse *)response
					newRequest:(NSURLRequest *)request
			 completionHandler:(void (^)(NSURLRequest *_Nullable))completionHandler
{
	(void)session;
	(void)task;
	(void)response;
	(void)request;
	completionHandler(nil);
}
@end

namespace {

bool FetchLatest(std::string &body, int &httpStatus)
{
	body.clear();
	httpStatus = 0;
	NSURL *url = [NSURL URLWithString:@"https://api.github.com/repos/palf-gh/HaloGlow/releases/latest"];
	if (!url) return false;

	NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
	request.HTTPMethod = @"GET";
	request.timeoutInterval = 8.0;
	request.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
	[request setValue:@"HaloGlow" forHTTPHeaderField:@"User-Agent"];
	[request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

	HGUpdateSessionDelegate *delegate = [HGUpdateSessionDelegate new];
	NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
	config.timeoutIntervalForRequest = 8.0;
	config.timeoutIntervalForResource = 8.0;
	NSURLSession *session = [NSURLSession sessionWithConfiguration:config delegate:delegate delegateQueue:nil];

	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	__block FetchResult result;
	NSURLSessionDataTask *task =
		[session dataTaskWithRequest:request
				   completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
					   (void)error;
					   if ([response isKindOfClass:[NSHTTPURLResponse class]])
						   result.status = static_cast<int>([(NSHTTPURLResponse *)response statusCode]);
					   if (data && data.length > 0 && data.length <= 262144)
						   result.body.assign(static_cast<const char *>(data.bytes), data.length);
					   dispatch_semaphore_signal(done);
				   }];
	[task resume];
	const long wait = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
	[session finishTasksAndInvalidate];
	if (wait != 0) {
		[task cancel];
		return false;
	}
	httpStatus = result.status;
	body.swap(result.body);
	return httpStatus != 0;
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

#endif // __APPLE__
