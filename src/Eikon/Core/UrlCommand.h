#pragma once

// The eikon:// URL scheme. Parsing only; the router
// is AppController::HandleUrl.

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Eikon::Core
{
    // After-capture actions. `useSettings` is packed value 2: take
    // copy/save/edit/thumbnail from the After-screenshot settings. Otherwise the flags are used as is
    // (a URL's then=), and all-false means "do nothing after the capture".
    struct CaptureActions
    {
        bool useSettings = true;
        bool copy = false;
        bool save = false;
        bool edit = false;
        bool pin = false;
        bool thumbnail = false;

        static CaptureActions FromSettings() noexcept { return {}; }
        bool operator==(CaptureActions const&) const = default;
    };

    struct UrlCommand
    {
        // Lower-cased scheme: always "eikon".
        std::wstring scheme;
        // The URL cut at the first '?', then at the first '=', then everything
        // after "://", trimmed of whitespace and lower-cased. "eikon://grab/delayed=5" -> "grab/delayed".
        std::wstring command;
        // Text after the first '=' anywhere in the URL (query included), percent-decoded, case kept.
        // nullopt when there is no '=' or the percent-encoding is invalid.
        std::optional<std::wstring> value;
        // The query items as a dictionary: percent-decoded names and values, '+' kept, a
        // repeated name keeps its last value, an item without '=' removes the name.
        std::map<std::wstring, std::wstring, std::less<>> query;
        // The host, percent-decoded.
        std::wstring host;
    };

    // nullopt unless the scheme is eikon (case-insensitive) and the URL has one.
    std::optional<UrlCommand> ParseUrlCommand(std::wstring_view url);

    // Strict percent-decoding: nullopt on a malformed escape or when
    // the decoded bytes are not UTF-8.
    std::optional<std::wstring> PercentDecode(std::wstring_view text);

    // The then= parameter (router): absent -> use settings. Present:
    // lower-cased, split at ',' (items are not trimmed), each of copy/save/edit/pin/thumbnail sets its
    // flag. `unrecognized` is true when the parameter is present but sets no flag; the router then shows
    // the "Unrecognized "then" parameter" warning and still captures, with no after-capture action.
    struct ThenParameter
    {
        CaptureActions actions;
        bool unrecognized = false;
    };
    ThenParameter ParseThenParameter(UrlCommand const& command);

    // grab/delayed[=<seconds>]: the value as a number, else 3 s. Negative values fire at once (0).
    double DelayedCaptureSeconds(UrlCommand const& command);
}
