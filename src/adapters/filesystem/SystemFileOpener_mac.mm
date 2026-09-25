#import "adapters/filesystem/SystemFileOpener.hpp"

#import <Cocoa/Cocoa.h>

#include <filesystem>

namespace adapters {
namespace filesystem {

bool SystemFileOpener::open(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return false;
    }

    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:fs::absolute(path).generic_string().c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        if (url == nil) {
            return false;
        }
        return [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

} // namespace filesystem
} // namespace adapters
