#import "adapters/dialog/NativeFolderPicker.hpp"

#import <Cocoa/Cocoa.h>

namespace adapters {
namespace dialog {

namespace {

// AppKit window creation is main-thread-only; the picker now runs on a
// dispatcher worker thread, so the panel must be marshalled onto the main
// queue while the worker blocks for the result. runModal pumps the main run
// loop, so webview events keep flowing while the dialog is up.
std::optional<std::string> run_panel_on_main(const std::string& title,
                                             const std::string& start_path) {
    __block std::optional<std::string> result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setCanChooseFiles:NO];
            [panel setCanChooseDirectories:YES];
            [panel setAllowsMultipleSelection:NO];
            [panel setCanCreateDirectories:YES];
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];

            if (!start_path.empty()) {
                NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start_path.c_str()]];
                if (url) {
                    [panel setDirectoryURL:url];
                }
            }

            if ([panel runModal] == NSModalResponseOK) {
                NSURL* chosen = [[panel URLs] firstObject];
                if (chosen) {
                    result = std::optional{std::string{[chosen fileSystemRepresentation]}};
                }
            }
        }
    });
    return result;
}

} // namespace

std::optional<std::string> NativeFolderPicker::pickFolder(const std::string& title,
                                                          const std::string& start_path) {
    if ([NSThread isMainThread]) {
        // Legacy path: invoked from the main thread directly.
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setCanChooseFiles:NO];
            [panel setCanChooseDirectories:YES];
            [panel setAllowsMultipleSelection:NO];
            [panel setCanCreateDirectories:YES];
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];

            if (!start_path.empty()) {
                NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start_path.c_str()]];
                if (url) {
                    [panel setDirectoryURL:url];
                }
            }

            if ([panel runModal] != NSModalResponseOK) {
                return std::nullopt;
            }
            NSURL* chosen = [[panel URLs] firstObject];
            if (!chosen) {
                return std::nullopt;
            }
            return std::optional{std::string{[chosen fileSystemRepresentation]}};
        }
    }
    return run_panel_on_main(title, start_path);
}

} // namespace dialog
} // namespace adapters
