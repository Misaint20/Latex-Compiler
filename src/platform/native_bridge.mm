#import "platform/native_bridge.h"

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <cstring>
#include <string>

// Root directory served under app://. Set once before navigation happens;
// scheme handler tasks arrive on the main thread afterwards.
static std::string& assets_root() {
    static std::string root;
    return root;
}

static NSString* mime_for_extension(NSString* extension) {
    static NSDictionary<NSString*, NSString*>* map = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        map = @{
            @"html" : @"text/html",
            @"js" : @"text/javascript",
            @"mjs" : @"text/javascript",
            @"css" : @"text/css",
            @"json" : @"application/json",
            @"map" : @"application/json",
            @"svg" : @"image/svg+xml",
            @"png" : @"image/png",
            @"jpg" : @"image/jpeg",
            @"jpeg" : @"image/jpeg",
            @"gif" : @"image/gif",
            @"webp" : @"image/webp",
            @"ico" : @"image/x-icon",
            @"woff" : @"font/woff",
            @"woff2" : @"font/woff2",
            @"ttf" : @"font/ttf",
            @"otf" : @"font/otf",
            @"txt" : @"text/plain",
            @"wasm" : @"application/wasm",
        };
    });
    return map[extension.lowercaseString];
}

// Serves the embedded frontend from disk under app:// so the page keeps a
// stable origin (no file:// null-origin CORS) while Vite stays free to emit
// separate module chunks for lazy loading. Objective-C class declarations
// must live at global scope, so no anonymous namespace here.
@interface AppSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation AppSchemeHandler

- (void)webView:(WKWebView*)webview startURLSchemeTask:(id<WKURLSchemeTask>)task {
    NSURL* url = task.request.URL;
    NSString* path = url.path;
    if (path.length == 0 || [path hasSuffix:@"/"]) {
        path = [path stringByAppendingString:@"index.html"];
    }
    if ([path containsString:@".."]) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorFileDoesNotExist
                                               userInfo:nil]];
        return;
    }

    NSString* root = [NSString stringWithUTF8String:assets_root().c_str()];
    NSString* full = [root stringByAppendingString:path];
    NSString* canonicalRoot = [root stringByResolvingSymlinksInPath];
    NSString* canonicalFull = [full stringByResolvingSymlinksInPath];
    if (![canonicalFull hasPrefix:canonicalRoot]) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorFileDoesNotExist
                                               userInfo:nil]];
        return;
    }

    NSData* data = [NSData dataWithContentsOfFile:canonicalFull];
    if (data == nil) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorFileDoesNotExist
                                               userInfo:nil]];
        return;
    }

    NSString* mime = mime_for_extension(canonicalFull.pathExtension) ?: @"application/octet-stream";
    NSURLResponse* response = [[NSURLResponse alloc] initWithURL:url
                                                        MIMEType:mime
                                           expectedContentLength:data.length
                                                textEncodingName:@"utf-8"];
    [task didReceiveResponse:response];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)webView:(WKWebView*)webview stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    // Delivery is synchronous, so there is nothing to cancel.
}

@end

void platform_integrate_title_bar(void* ns_window) {
  if (!ns_window) {
    return;
  }
  NSWindow* window = (__bridge NSWindow*)ns_window;
  window.titlebarAppearsTransparent = YES;
  window.styleMask |= NSWindowStyleMaskFullSizeContentView;
  window.titleVisibility = NSWindowTitleHidden;
  window.backgroundColor = [NSColor colorWithSRGBRed:0.055 green:0.059
                                                blue:0.075 alpha:1.0];
  window.movableByWindowBackground = YES;
}

void platform_set_assets_root(const char* path) {
    assets_root() = path != nullptr ? path : "";
}

void platform_on_webview_configuration(void* wk_webview_configuration) {
    if (wk_webview_configuration == nullptr) {
        return;
    }
    WKWebViewConfiguration* config =
        (__bridge WKWebViewConfiguration*)wk_webview_configuration;
    [config setURLSchemeHandler:[[AppSchemeHandler alloc] init] forURLScheme:@"app"];
}

void platform_load_bundle_url(void* webview_widget, const char* html_file_path) {
    if (webview_widget == nullptr || html_file_path == nullptr) {
        return;
    }

    WKWebView* webview = (__bridge WKWebView*)webview_widget;
    NSString* path = [NSString stringWithUTF8String:html_file_path];
    NSURL* pageUrl = [NSURL fileURLWithPath:path];
    NSURL* directoryUrl = [pageUrl URLByDeletingLastPathComponent];
    [webview loadFileURL:pageUrl allowingReadAccessToURL:directoryUrl];
}
