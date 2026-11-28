#include "../game/mod_store/mobile_http.hpp"

#import <Foundation/Foundation.h>
#include <exception>
#include <stdexcept>

namespace {
struct Transfer {
    dkr::store::MobileHttpReceiver& receiver;
    std::exception_ptr error;

    bool headers(NSHTTPURLResponse* response) {
        try {
            receiver.status(static_cast<unsigned>(response.statusCode));
            for (id key in response.allHeaderFields) {
                const char* name = [[key description] UTF8String];
                const char* value = [[response.allHeaderFields[key] description] UTF8String];
                if (name && value)
                    receiver.header(name, value);
            }
            return true;
        } catch (...) {
            error = std::current_exception();
            return false;
        }
    }
};

NSString* text(std::string_view value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}
} // namespace

@interface DKRStoreHTTPDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic, assign) Transfer* transfer;
@property(nonatomic, strong) dispatch_semaphore_t finished;
@property(nonatomic, assign) BOOL connectionFailed;
@end

@implementation DKRStoreHTTPDelegate
- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))complete {
    (void)session;
    (void)task;
    BOOL valid =
        [response isKindOfClass:[NSHTTPURLResponse class]] && self.transfer->headers((NSHTTPURLResponse*)response);
    complete(valid ? NSURLSessionResponseAllow : NSURLSessionResponseCancel);
}
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))complete {
    (void)session;
    (void)task;
    (void)response;
    (void)request;
    // The shared library client validates and follows redirects.
    complete(nil);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    (void)session;
    try {
        self.transfer->receiver.write(static_cast<const char*>(data.bytes), data.length);
    } catch (...) {
        self.transfer->error = std::current_exception();
        [task cancel];
    }
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    (void)session;
    (void)task;
    self.connectionFailed = error != nil;
    dispatch_semaphore_signal(self.finished);
}
@end

void dkr::store::mobile_http(std::string_view url, std::string_view method, std::string_view body,
                             const HttpHeaders& headers, unsigned timeout_ms, MobileHttpReceiver& receiver) {
    @autoreleasepool {
        auto* endpoint = [NSURL URLWithString:text(url)];
        if (!endpoint || ![endpoint.scheme isEqualToString:@"https"] || receiver.stop.stop_requested())
            throw std::runtime_error("The secure library request could not start.");
        const double timeout = timeout_ms / 1000.0;
        auto* request = [NSMutableURLRequest requestWithURL:endpoint
                                                cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                            timeoutInterval:timeout];
        request.HTTPMethod = text(method);
        for (const auto& [name, value] : headers)
            [request setValue:text(value) forHTTPHeaderField:text(name)];
        if (method == "POST" || method == "PUT")
            request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        auto* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.HTTPCookieStorage = nil;
        config.URLCache = nil;
        config.URLCredentialStorage = nil;
        config.timeoutIntervalForRequest = timeout;
        auto* queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        Transfer transfer{receiver, {}};
        auto* delegate = [DKRStoreHTTPDelegate new];
        delegate.transfer = &transfer;
        delegate.finished = dispatch_semaphore_create(0);
        auto* session = [NSURLSession sessionWithConfiguration:config delegate:delegate delegateQueue:queue];
        auto* task = [session dataTaskWithRequest:request];
        [task resume];
        while (dispatch_semaphore_wait(delegate.finished, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)))
            if (receiver.stop.stop_requested())
                [task cancel];
        [session finishTasksAndInvalidate];
        if (transfer.error)
            std::rethrow_exception(transfer.error);
        if (delegate.connectionFailed || receiver.stop.stop_requested())
            throw std::runtime_error("The secure library request was interrupted.");
    }
}
