#include "pch.h"
#include "Core/Settings.h"

// The only Core translation unit that uses WinRT: the ApplicationData-backed settings store and the
// app's data folder. Excluded from tests/Eikon.Core.Tests.

namespace Eikon::Core
{
    namespace
    {
        using winrt::Windows::Foundation::Collections::IPropertySet;
        using winrt::Windows::Storage::ApplicationData;

        void Log(std::wstring const& message)
        {
            OutputDebugStringW((L"Eikon AppData: " + message + L"\n").c_str());
        }

        // Settings persisted as string values in ApplicationData::Current().LocalSettings().Values().
        // When the process has no package identity (e.g. unpackaged debug runs) it degrades to an
        // in-memory map so that the app still works.
        class LocalSettingsStore final : public SettingsStore
        {
        public:
            LocalSettingsStore()
            {
                try
                {
                    m_values = ApplicationData::Current().LocalSettings().Values();
                }
                catch (winrt::hresult_error const& e)
                {
                    Log(L"LocalSettings unavailable: " + std::wstring{ e.message() });
                    m_values = nullptr;
                }
                catch (...)
                {
                    Log(L"LocalSettings unavailable");
                    m_values = nullptr;
                }
            }

            std::optional<std::wstring> Read(std::wstring_view key) const override
            {
                if (!m_values)
                {
                    auto it = m_fallback.find(std::wstring{ key });
                    if (it == m_fallback.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                }
                try
                {
                    winrt::Windows::Foundation::IInspectable value = m_values.TryLookup(winrt::hstring{ key });
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    if (auto text = value.try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>())
                    {
                        return std::wstring{ text.Value() };
                    }
                    // Values written by something else (e.g. a boxed number): use its string form.
                    if (auto property = value.try_as<winrt::Windows::Foundation::IPropertyValue>())
                    {
                        switch (property.Type())
                        {
                        case winrt::Windows::Foundation::PropertyType::Boolean:
                            return property.GetBoolean() ? std::wstring{ L"1" } : std::wstring{ L"0" };
                        case winrt::Windows::Foundation::PropertyType::Int32:
                            return std::to_wstring(property.GetInt32());
                        case winrt::Windows::Foundation::PropertyType::Int64:
                            return std::to_wstring(property.GetInt64());
                        default:
                            break;
                        }
                    }
                }
                catch (winrt::hresult_error const& e)
                {
                    Log(L"Read failed: " + std::wstring{ e.message() });
                }
                catch (...)
                {
                    Log(L"Read failed");
                }
                return std::nullopt;
            }

            void Write(std::wstring_view key, std::wstring_view value) override
            {
                if (!m_values)
                {
                    m_fallback[std::wstring{ key }] = std::wstring{ value };
                    return;
                }
                try
                {
                    m_values.Insert(winrt::hstring{ key }, winrt::box_value(winrt::hstring{ value }));
                }
                catch (winrt::hresult_error const& e)
                {
                    Log(L"Write failed: " + std::wstring{ e.message() });
                }
                catch (...)
                {
                    Log(L"Write failed");
                }
            }

            void Remove(std::wstring_view key) override
            {
                if (!m_values)
                {
                    m_fallback.erase(std::wstring{ key });
                    return;
                }
                try
                {
                    const winrt::hstring name{ key };
                    if (m_values.HasKey(name))
                    {
                        m_values.Remove(name);
                    }
                }
                catch (winrt::hresult_error const& e)
                {
                    Log(L"Remove failed: " + std::wstring{ e.message() });
                }
                catch (...)
                {
                    Log(L"Remove failed");
                }
            }

        private:
            IPropertySet m_values{ nullptr };
            std::map<std::wstring, std::wstring> m_fallback;
        };
    }

    std::unique_ptr<SettingsStore> CreateDefaultSettingsStore()
    {
        return std::make_unique<LocalSettingsStore>();
    }

    std::filesystem::path DataFolder()
    {
        try
        {
            return std::filesystem::path{ std::wstring{ ApplicationData::Current().LocalFolder().Path() } };
        }
        catch (...)
        {
            Log(L"LocalFolder unavailable");
        }
        wchar_t buffer[MAX_PATH] = {};
        DWORD const length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return std::filesystem::path{ std::wstring(buffer, length) } / L"Eikon";
        }
        std::error_code ec;
        return std::filesystem::temp_directory_path(ec) / L"Eikon";
    }
}
