#pragma once

// Secrets: a small string dictionary that must not sit in plain
// settings. Keys: allowDeeplinks, s3access, s3secret. Stored as JSON encrypted with DPAPI
// (CryptProtectData, current user) in LocalState\secrets.dat, loaded lazily. UI thread only.

#include "Core/Event.h"

#include <map>
#include <string>
#include <string_view>

namespace Eikon
{
    class Secrets
    {
    public:
        static Secrets& Current();

        static constexpr std::wstring_view kAllowDeeplinks = L"allowDeeplinks";
        static constexpr std::wstring_view kS3Access = L"s3access";
        static constexpr std::wstring_view kS3Secret = L"s3secret";

        // Gates the URL scheme's routes. Absent = false.
        bool AllowDeeplinks();
        void AllowDeeplinks(bool value);
        std::wstring S3Access();
        void S3Access(std::wstring const& value);
        std::wstring S3Secret();
        void S3Secret(std::wstring const& value);

        // "" when missing.
        std::wstring Get(std::wstring_view key);
        // Writes the file right away; an empty value removes the key.
        void Set(std::wstring_view key, std::wstring const& value);

        // Raised after Set changed a key.
        Event<std::wstring_view> Changed;

    private:
        Secrets() = default;
        void EnsureLoaded();
        void Save() const;

        bool m_loaded = false;
        std::map<std::wstring, std::wstring, std::less<>> m_values;
    };
}
