#pragma once

// Included by the Objective-C++ recorder backends after Foundation imports.
namespace trussc::internal {
inline SaveResult videoOpenFailure(NSError* error, const char* operation) {
    SaveError code = SaveError::WriteFailed;
    // AVFoundation may wrap a Cocoa or POSIX file error. Bound malformed chains.
    NSError* cause = error;
    for (int depth = 0; cause && depth < 16; ++depth) {
        if ([cause.domain isEqualToString:NSCocoaErrorDomain]) {
            if (cause.code == NSFileWriteNoPermissionError) code = SaveError::AccessDenied;
            else if (cause.code == NSFileWriteOutOfSpaceError) code = SaveError::NoSpace;
        } else if ([cause.domain isEqualToString:NSPOSIXErrorDomain]) {
            code = saveFailure({}, operation,
                std::error_code((int)cause.code, std::generic_category())).error;
        }
        if (code != SaveError::WriteFailed) break;
        cause = cause.userInfo[NSUnderlyingErrorKey];
    }
    std::string message = std::string(operation) + ": " +
        (error ? error.localizedDescription.UTF8String : "unknown");
    if (code != SaveError::WriteFailed && cause && cause != error)
        message += " (" + std::string(cause.localizedDescription.UTF8String) + ")";
    return SaveResult::fail(code, std::move(message));
}
} // namespace trussc::internal
