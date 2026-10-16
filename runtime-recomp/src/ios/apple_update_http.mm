#include "../game/updates/update_http.hpp"
#import <Foundation/Foundation.h>
#include <stdexcept>

@interface DKRUpdateHTTPDelegate : NSObject <NSURLSessionDataDelegate, NSURLSessionTaskDelegate>
@property(nonatomic, assign) dkr::updates::HttpSink *sink;
@property(nonatomic, strong) dispatch_semaphore_t finished;
@property(nonatomic, strong) NSString *failure;
@property(nonatomic, assign) unsigned redirects;
@end

std::string dkr::updates::native_os_version() {
    auto value = [[NSProcessInfo processInfo] operatingSystemVersion];
    return std::to_string(value.majorVersion) + "." + std::to_string(value.minorVersion) + "." +
           std::to_string(value.patchVersion);
}
@implementation DKRUpdateHTTPDelegate
- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))complete {
    (void)session;
    (void)task;
    if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
        self.failure = @"Update server did not return HTTP.";
        complete(NSURLSessionResponseCancel);
        return;
    }
    auto *http = (NSHTTPURLResponse *)response;
    self.sink->response.status = unsigned(http.statusCode);
    for (id name in http.allHeaderFields) {
        NSString *value = [http.allHeaderFields[name] description];
        if (!self.sink->header([[name description] UTF8String], [value UTF8String])) {
            self.failure = @"Update response headers exceeded their limits.";
            complete(NSURLSessionResponseCancel);
            return;
        }
    }
    complete(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)task
    didReceiveData:(NSData *)data {
    (void)session;
    if (!self.sink->write(
            std::span(static_cast<const std::uint8_t *>(data.bytes), std::size_t(data.length))))
        [task cancel];
}
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))complete {
    (void)session;
    (void)task;
    (void)response;
    const char *url = request.URL.absoluteString.UTF8String;
    if (++self.redirects > 5 || !url || !dkr::updates::allowed_https_url(url)) {
        self.failure = @"Update redirect is not an approved HTTPS release endpoint.";
        complete(nil);
        return;
    }
    complete(request);
}
- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
    (void)session;
    (void)task;
    if (error && !self.failure)
        self.failure =
            @"Update connection interrupted or TLS verification failed. Nothing was installed.";
    dispatch_semaphore_signal(self.finished);
}
@end

dkr::updates::HttpResponse dkr::updates::https_get(const HttpRequest &request, std::stop_token stop,
                                                   const Progress &progress) {
    if (!allowed_https_url(request.url))
        throw std::runtime_error("Update URL is not an approved HTTPS release endpoint.");
    @autoreleasepool {
        HttpSink sink(request, stop, progress);
        auto *delegate = [DKRUpdateHTTPDelegate new];
        delegate.sink = &sink;
        delegate.finished = dispatch_semaphore_create(0);
        auto *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = 4;
        configuration.timeoutIntervalForResource = request.timeout_seconds;
        configuration.HTTPCookieStorage = nil;
        configuration.URLCache = nil;
        auto *queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        auto *session = [NSURLSession sessionWithConfiguration:configuration
                                                      delegate:delegate
                                                 delegateQueue:queue];
        auto *url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
        auto *query = [NSMutableURLRequest requestWithURL:url
                                              cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                          timeoutInterval:4];
        [query setValue:@"DKR-R-Updater/1" forHTTPHeaderField:@"User-Agent"];
        [query setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
        [query setValue:@"2022-11-28" forHTTPHeaderField:@"X-GitHub-Api-Version"];
        if (!request.etag.empty() && request.etag.find_first_of("\r\n") == request.etag.npos)
            [query setValue:[NSString stringWithUTF8String:request.etag.c_str()]
                forHTTPHeaderField:request.offset ? @"If-Range" : @"If-None-Match"];
        if (request.offset)
            [query setValue:[NSString
                                stringWithFormat:@"bytes=%llu-",
                                                 static_cast<unsigned long long>(request.offset)]
                forHTTPHeaderField:@"Range"];
        auto *task = [session dataTaskWithRequest:query];
        [task resume];
        while (dispatch_semaphore_wait(delegate.finished,
                                       dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0)
            if (sink.cancelled())
                [task cancel];
        [session finishTasksAndInvalidate];
        sink.finish();
        if (delegate.failure)
            throw std::runtime_error(delegate.failure.UTF8String);
        return sink.response;
    }
}
