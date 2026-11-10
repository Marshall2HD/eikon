#include "pch.h"
#include "Platform/Secrets.h"

#include "Core/Json.h"
#include "Core/Settings.h"

#include <wincrypt.h>
#include <dpapi.h>

#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace Eikon
{
    namespace
    {
        constexpr wchar_t kFileName[] = L"secrets.dat";

        void Log(std::wstring_view message)
        {
            std::wstring line{ L"Eikon Secrets: " };
            line.append(message);
            line.push_back(L'\n');
            OutputDebugStringW(line.c_str());
        }

        std::filesystem::path FilePath()
        {
            return Core::DataFolder() / kFileName;
        }

        std::string ToUtf8(std::wstring const& text)
        {
            if (text.empty())
            {
                return {};
            }
            int const size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<std::size_t>((std::max)(size, 0)), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring FromUtf8(std::string const& text)
        {
            if (text.empty())
            {
                return {};
            }
            int const size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring result(static_cast<std::size_t>((std::max)(size, 0)), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
            return result;
        }
    }

    Secrets& Secrets::Current()
    {
        static Secrets instance;
        return instance;
    }

    void Secrets::EnsureLoaded()
    {
        if (m_loaded)
        {
            return;
        }
        m_loaded = true;
        try
        {
            std::ifstream file{ FilePath(), std::ios::binary };
            if (!file)
            {
                return; // nothing stored yet
            }
            std::vector<char> encrypted{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
            DATA_BLOB input{ static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data()) };
            DATA_BLOB output{};
            if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
            {
                Log(L"CryptUnprotectData failed");
                return;
            }
            std::string const plain{ reinterpret_cast<char const*>(output.pbData), output.cbData };
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
            auto const json = Core::JsonValue::Parse(FromUtf8(plain));
            if (!json || !json->IsObject())
            {
                Log(L"unreadable secrets file");
                return;
            }
            for (auto const& [key, value] : json->Members())
            {
                if (value.IsString())
                {
                    m_values.insert_or_assign(key, value.AsString());
                }
            }
        }
        catch (...)
        {
            Log(L"loading failed");
        }
    }

    void Secrets::Save() const
    {
        try
        {
            Core::JsonValue json = Core::JsonValue::Object();
            for (auto const& [key, value] : m_values)
            {
                json.Set(key, Core::JsonValue::String(value));
            }
            std::string plain = ToUtf8(json.Serialize());
            DATA_BLOB input{ static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data()) };
            DATA_BLOB output{};
            BOOL const ok = CryptProtectData(&input, L"Eikon secrets", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
            SecureZeroMemory(plain.data(), plain.size());
            if (!ok)
            {
                Log(L"CryptProtectData failed");
                return;
            }
            auto const path = FilePath();
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            auto temporary = path;
            temporary += L".tmp";
            {
                std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
                file.write(reinterpret_cast<char const*>(output.pbData), static_cast<std::streamsize>(output.cbData));
            }
            LocalFree(output.pbData);
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            {
                Log(L"could not replace the secrets file");
            }
        }
        catch (...)
        {
            Log(L"saving failed");
        }
    }

    std::wstring Secrets::Get(std::wstring_view key)
    {
        EnsureLoaded();
        auto const it = m_values.find(key);
        return it == m_values.end() ? std::wstring{} : it->second;
    }

    void Secrets::Set(std::wstring_view key, std::wstring const& value)
    {
        EnsureLoaded();
        auto const it = m_values.find(key);
        if (value.empty())
        {
            if (it == m_values.end())
            {
                return;
            }
            m_values.erase(it);
        }
        else
        {
            if (it != m_values.end() && it->second == value)
            {
                return;
            }
            m_values.insert_or_assign(std::wstring{ key }, value);
        }
        Save();
        Changed(key);
    }

    bool Secrets::AllowDeeplinks()
    {
        return Get(kAllowDeeplinks) == L"true";
    }

    void Secrets::AllowDeeplinks(bool value)
    {
        Set(kAllowDeeplinks, value ? L"true" : L"false");
    }

    std::wstring Secrets::S3Access() { return Get(kS3Access); }
    void Secrets::S3Access(std::wstring const& value) { Set(kS3Access, value); }
    std::wstring Secrets::S3Secret() { return Get(kS3Secret); }
    void Secrets::S3Secret(std::wstring const& value) { Set(kS3Secret, value); }
}
