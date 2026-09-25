#import "adapters/notify/MacSystemNotifier.hpp"

#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>
#import <dispatch/dispatch.h>

// Handles notification activation (user clicks the banner): brings the app
// to the front and opens the compiled PDF. Lives until process exit so
// UNUserNotificationCenter keeps delivering and activation keeps working.
@interface CompileNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation CompileNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       didReceiveNotificationResponse:(UNNotificationResponse*)response
                withCompletionHandler:(void (^)(void))completionHandler {
    NSString* path = response.notification.request.content.userInfo[@"openPath"];
    if (path.length > 0) {
        NSURL* url = [NSURL fileURLWithPath:path];
        if (url != nil) {
            [[NSWorkspace sharedWorkspace] openURL:url];
        }
    }
    [NSApp activateIgnoringOtherApps:YES];
    completionHandler();
}

// Presentation while the app is active: notifications are never posted in
// that state, so there is nothing to show inline.
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
        willPresentNotification:(UNNotification*)notification
          withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    completionHandler(UNNotificationPresentationOptionNone);
}

@end

namespace adapters {
namespace notify {

CompileNotificationDelegate* notificationDelegate() {
    static CompileNotificationDelegate* delegate = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        delegate = [CompileNotificationDelegate alloc];
    });
    return delegate;
}

} // namespace notify
} // namespace adapters

void adapters::notify::MacSystemNotifier::notify(const std::string& title, const std::string& body,
                                                 const std::string& open_path) {
    @autoreleasepool {
        NSString* ns_title = [NSString stringWithUTF8String:title.c_str()];
        NSString* ns_body = [NSString stringWithUTF8String:body.c_str()];
        NSString* ns_open = [NSString stringWithUTF8String:open_path.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([NSApp isActive]) {
                // The window already shows the result; a banner would be noise.
                return;
            }
            if ([[NSBundle mainBundle] bundleIdentifier].length == 0) {
                // Outside a proper .app bundle UNUserNotificationCenter has
                // no identity to attach and THROWS (observed: an escaping
                // ObjC exception that @catch cannot reliably intercept).
                // Stay silent instead of taking the process down.
                return;
            }
            @try {
                UNUserNotificationCenter* center =
                    [UNUserNotificationCenter currentNotificationCenter];
                center.delegate = adapters::notify::notificationDelegate();
                [center requestAuthorizationWithOptions:
                            (UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                          completionHandler:^(BOOL, NSError*) {}];
                UNMutableNotificationContent* content =
                    [[UNMutableNotificationContent alloc] init];
                content.title = ns_title;
                if (ns_body.length > 0) {
                    content.body = ns_body;
                }
                if (ns_open.length > 0) {
                    content.userInfo = @{@"openPath" : ns_open};
                }
                content.sound = [UNNotificationSound defaultSound];

                UNNotificationRequest* request = [UNNotificationRequest
                    requestWithIdentifier:@"com.Misaint20.latexcompiler.compile"
                                  content:content
                                  trigger:nil];
                [center addNotificationRequest:request withCompletionHandler:nil];
            } @catch (NSException* exception) {
                // The center throws outside a proper bundle (or on other
                // environment gaps). Toasting is fire-and-forget by contract:
                // never let it take the app down.
                NSLog(@"LatexCompiler notifications: %@", exception);
            }
        });
    }
}
