#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include <cstdio>
#include <exception>

namespace EikonTests
{
    namespace
    {
        int g_currentFailures = 0;
        int g_totalFailures = 0;
    }

    std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    void ReportFailure(char const* file, int line, std::string const& message)
    {
        ++g_currentFailures;
        ++g_totalFailures;
        std::printf("    %s(%d): %s\n", file, line, message.c_str());
    }

    std::string Narrow(std::wstring_view text)
    {
        std::string result;
        for (wchar_t c : text)
        {
            auto const code = static_cast<std::uint32_t>(c);
            if (code >= 0x20 && code < 0x7F)
            {
                result += static_cast<char>(code);
            }
            else
            {
                char buffer[16] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(code));
                result += buffer;
            }
        }
        return result;
    }

    // ---- TestSupport ----

    std::optional<std::wstring> MemoryStore::Read(std::wstring_view key) const
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void MemoryStore::Write(std::wstring_view key, std::wstring_view value)
    {
        ++writes;
        values.insert_or_assign(std::wstring{ key }, std::wstring{ value });
    }

    void MemoryStore::Remove(std::wstring_view key)
    {
        if (auto it = values.find(key); it != values.end())
        {
            values.erase(it);
        }
    }

    MemoryStore& ResetSettings(std::map<std::wstring, std::wstring, std::less<>> values)
    {
        auto store = std::make_unique<MemoryStore>();
        store->values = std::move(values);
        MemoryStore& reference = *store;
        Eikon::Core::Settings::UseStore(std::move(store));
        return reference;
    }
}

// The app defines these in Core/AppData.cpp (WinRT); the tests provide WinRT-free versions.
namespace Eikon::Core
{
    std::unique_ptr<SettingsStore> CreateDefaultSettingsStore()
    {
        return std::make_unique<EikonTests::MemoryStore>();
    }

    std::filesystem::path DataFolder()
    {
        std::error_code ec;
        return std::filesystem::temp_directory_path(ec) / "eikon-core-tests";
    }
}

int main()
{
    using namespace EikonTests;
    int failedTests = 0;
    for (auto const& test : Registry())
    {
        g_currentFailures = 0;
        try
        {
            test.function();
        }
        catch (std::exception const& e)
        {
            ReportFailure(test.name, 0, std::string{ "unhandled exception: " } + e.what());
        }
        catch (...)
        {
            ReportFailure(test.name, 0, "unhandled non-standard exception");
        }
        if (g_currentFailures > 0)
        {
            ++failedTests;
            std::printf("[FAIL] %s\n", test.name);
        }
        else
        {
            std::printf("[ OK ] %s\n", test.name);
        }
    }
    std::printf("\n%zu tests, %d failed, %d failed checks\n", Registry().size(), failedTests, g_totalFailures);
    std::fflush(stdout);
    return g_totalFailures;
}
