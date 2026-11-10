#pragma once

#include "Core/Settings.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace EikonTests
{
    // In-memory SettingsStore that counts writes (to verify the first-read write-back).
    class MemoryStore final : public Eikon::Core::SettingsStore
    {
    public:
        std::optional<std::wstring> Read(std::wstring_view key) const override;
        void Write(std::wstring_view key, std::wstring_view value) override;
        void Remove(std::wstring_view key) override;

        std::map<std::wstring, std::wstring, std::less<>> values;
        int writes = 0;
    };

    // Installs a fresh in-memory store into Settings::Current() and returns it (owned by Settings).
    MemoryStore& ResetSettings(std::map<std::wstring, std::wstring, std::less<>> values = {});
}
