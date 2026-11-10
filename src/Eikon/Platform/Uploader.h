#pragma once

// S3 upload over WinHTTP: image encoding with the 6 MB fallback
// chain, the signed PUT with one clock-skew retry, the sharing URL and
// Test Connection. Signing and the rules live in Core/S3. Calls return on the UI thread.

#include "Core/Bitmap.h"
#include "Core/S3.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Eikon::Uploader
{
    struct Result
    {
        bool ok = false;
        std::wstring url;       // the sharing URL (presign/public), empty for sharing mode "none"
        std::wstring message;   // the error, or "File uploaded successfully" for "none"
    };

    // A missing setting notification title and message, in order:
    // access key, secret key, region or endpoint, bucket.
    struct MissingSetting
    {
        std::wstring title;
        std::wstring message;
    };
    std::optional<MissingSetting> CheckSettings();

    // Encodes `image`, uploads it to the key "<prefix>/SCR-%Y%m%d-%T.<ext>" for `captureTime`,
    // and builds the sharing URL. `done` runs on the UI thread.
    void Upload(std::shared_ptr<Core::Bitmap const> image, std::chrono::system_clock::time_point captureTime,
                std::function<void(Result const&)> done);

    // PUT "test_<8 hex>.txt" ("hello world", text/plain) under the prefix, then DELETE it.
    // `done(ok, message)` on the UI thread; `message` is the mapped error when it failed.
    void TestConnection(std::function<void(bool ok, std::wstring const& message)> done);
}
