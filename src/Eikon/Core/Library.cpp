#include "pch.h"
#include "Core/Library.h"

#include "Core/Json.h"
#include "Core/Localization.h"

#include <algorithm>
#include <charconv>
#include <cwchar>
#include <cwctype>
#include <regex>
#include <unordered_map>

namespace Eikon::Core::Library
{
    namespace
    {
        std::wstring FromUtf8(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }
            int const length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0)
            {
                return {};
            }
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        std::string ToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }
            int const length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (length <= 0)
            {
                return {};
            }
            std::string narrow(static_cast<std::size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
            return narrow;
        }

        bool IsSpace(wchar_t c) noexcept
        {
            return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f' || c == 0x00A0 || c == 0x2028 ||
                   c == 0x2029 || c == 0x3000 || c == 0x200B || c == 0xFEFF || (c >= 0x2000 && c <= 0x200A) || c == 0x0085;
        }

        std::wstring Lower(std::wstring_view text)
        {
            std::wstring out{ text };
            for (auto& c : out)
            {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            return out;
        }

        std::optional<Record> FromJson(std::string_view json)
        {
            auto const value = JsonValue::Parse(FromUtf8(json));
            if (!value || !value->IsObject())
            {
                return std::nullopt;
            }
            Record record;
            for (auto const& [key, member] : value->Members())
            {
                if (!member.IsString())
                {
                    return std::nullopt;   // values must be strings
                }
                record[key] = member.AsString();
            }
            return record;
        }

        std::string Trimmed(std::string_view text)
        {
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
            {
                ++begin;
            }
            while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
            {
                --end;
            }
            return std::string{ text.substr(begin, end - begin) };
        }

        std::optional<int> Number(std::wstring_view text)
        {
            if (text.empty())
            {
                return std::nullopt;
            }
            int value = 0;
            for (wchar_t c : text)
            {
                if (c < L'0' || c > L'9')
                {
                    return std::nullopt;
                }
                value = value * 10 + (c - L'0');
            }
            return value;
        }

        bool ValidDate(int year, int month, int day) noexcept
        {
            static constexpr int kDays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            if (month < 1 || month > 12 || day < 1)
            {
                return false;
            }
            bool const leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
            int const days = month == 2 ? (leap ? 29 : 28) : kDays[month - 1];
            return day <= days;
        }

        std::optional<LocalTime> DateFromDigits(std::wstring_view digits)   // "YYYYMMDD"
        {
            auto const year = Number(digits.substr(0, 4));
            auto const month = Number(digits.substr(4, 2));
            auto const day = Number(digits.substr(6, 2));
            if (!year || !month || !day || !ValidDate(*year, *month, *day))
            {
                return std::nullopt;
            }
            return LocalTime{ *year, *month, *day, 0, 0, 0 };
        }
    }

    std::wstring Field(Record const& record, std::wstring_view key)
    {
        auto const it = record.find(key);
        return it == record.end() ? std::wstring{} : it->second;
    }

    std::optional<std::int64_t> IntField(Record const& record, std::wstring_view key)
    {
        auto const it = record.find(key);
        if (it == record.end() || it->second.empty())
        {
            return std::nullopt;
        }
        std::wstring_view text = it->second;
        bool negative = false;
        if (text.front() == L'+' || text.front() == L'-')
        {
            negative = text.front() == L'-';
            text.remove_prefix(1);
        }
        if (text.empty())
        {
            return std::nullopt;
        }
        std::int64_t value = 0;
        for (wchar_t c : text)
        {
            if (c < L'0' || c > L'9' || value > (INT64_MAX - 9) / 10)
            {
                return std::nullopt;
            }
            value = value * 10 + (c - L'0');
        }
        return negative ? -value : value;
    }

    // ---- Line format ----

    std::optional<Record> ParseLine(std::string_view line)
    {
        std::string const text = Trimmed(line);
        if (text.empty())
        {
            return std::nullopt;
        }
        // Only plain JSON lines are read; anything else (an encrypted line, garbage) is skipped.
        if (text.front() != '{')
        {
            return std::nullopt;
        }
        return FromJson(text);
    }

    std::string RecordJson(Record const& record)
    {
        JsonValue object = JsonValue::Object();
        for (auto const& [key, value] : record)
        {
            object.Set(key, JsonValue::String(value));
        }
        std::wstring json = object.Serialize();
        // The solidus is escaped; every "/" of an object of strings is inside a string.
        std::wstring escaped;
        escaped.reserve(json.size());
        for (wchar_t c : json)
        {
            if (c == L'/')
            {
                escaped += L"\\/";
            }
            else
            {
                escaped += c;
            }
        }
        return ToUtf8(escaped);
    }

    std::string EncodeLine(Record const& record)
    {
        return RecordJson(record);
    }

    std::vector<Record> ParseDatabase(std::string_view content)
    {
        std::vector<Record> records;
        std::size_t start = 0;
        while (start <= content.size())
        {
            std::size_t end = content.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = content.size();
            }
            if (auto record = ParseLine(content.substr(start, end - start)))
            {
                records.push_back(std::move(*record));
            }
            start = end + 1;
        }
        return records;
    }

    std::string EncodeDatabase(std::vector<Record> const& records)
    {
        std::string out;
        for (auto const& record : records)
        {
            out += EncodeLine(record);
            out += '\n';
        }
        return out;
    }

    // ---- File names ----

    std::optional<int> SecondOfDayFromCode(std::wstring_view code)
    {
        if (code.size() != 4)
        {
            return std::nullopt;
        }
        int n = 0;
        for (wchar_t c : code)
        {
            if (c < L'a' || c > L'z')
            {
                return std::nullopt;
            }
            n = n * 26 + (c - L'a');
        }
        int const quarters = n - 17576;   // 26^3
        if (quarters < 0)
        {
            return std::nullopt;
        }
        return quarters / 4;
    }

    std::optional<LocalTime> TimeFromFileName(std::wstring_view fileName)
    {
        std::wstring const name{ fileName };
        std::wsmatch match;

        // "SCR-20240102-kvqe.png".
        static std::wregex const originalCode{ LR"(\-(20[0-9]{6})\-([a-z]{4}))" };
        if (std::regex_search(name, match, originalCode))
        {
            if (auto date = DateFromDigits(match[1].str()))
            {
                if (auto const seconds = SecondOfDayFromCode(match[2].str()); seconds && *seconds < 86400)
                {
                    date->hour = *seconds / 3600;
                    date->minute = (*seconds % 3600) / 60;
                    date->second = *seconds % 60;
                }
                return date;
            }
        }
        static std::wregex const originalDate{ LR"(\-(20[0-9]{6})\-)" };
        if (std::regex_search(name, match, originalDate))
        {
            if (auto date = DateFromDigits(match[1].str()))
            {
                return date;
            }
        }

        // "Screenshot 2024-01-02 at 10.11.12" (12-hour clocks add " AM"/" PM"), Windows
        // Snipping Tool "Screenshot 2026-11-08 143012".
        static std::wregex const dashed{ LR"((20[0-9]{2})\-([0-9]{2})\-([0-9]{2}))" };
        if (!std::regex_search(name, match, dashed))
        {
            return std::nullopt;
        }
        auto const year = Number(match[1].str());
        auto const month = Number(match[2].str());
        auto const day = Number(match[3].str());
        if (!year || !month || !day || !ValidDate(*year, *month, *day))
        {
            return std::nullopt;
        }
        LocalTime time{ *year, *month, *day, 0, 0, 0 };
        std::wstring const rest = match.suffix().str();
        std::wsmatch clock;
        static std::wregex const macClock{ LR"(^ at ([0-9]{1,2})\.([0-9]{2})\.([0-9]{2})(?:[ \u202F]?([AaPp])[Mm])?)" };
        static std::wregex const windowsClock{ LR"(^ ([0-9]{2})([0-9]{2})([0-9]{2}))" };
        if (std::regex_search(rest, clock, macClock) || std::regex_search(rest, clock, windowsClock))
        {
            int hour = Number(clock[1].str()).value_or(0);
            int const minute = Number(clock[2].str()).value_or(0);
            int const second = Number(clock[3].str()).value_or(0);
            if (clock.size() > 4 && clock[4].matched)
            {
                bool const pm = clock[4].str() == L"P" || clock[4].str() == L"p";
                hour = hour % 12 + (pm ? 12 : 0);
            }
            if (hour < 24 && minute < 60 && second < 60)
            {
                time.hour = hour;
                time.minute = minute;
                time.second = second;
            }
        }
        return time;
    }

    bool IsImageFile(std::wstring_view fileName)
    {
        auto const dot = fileName.find_last_of(L'.');
        if (dot == std::wstring_view::npos)
        {
            return false;
        }
        std::wstring const extension = Lower(fileName.substr(dot + 1));
        return extension == L"png" || extension == L"jpg" || extension == L"jpeg";
    }

    bool IsScreenshotName(std::wstring_view fileName)
    {
        std::wstring const name{ fileName };
        static std::wregex const original{ LR"(\-20[0-9]{6}\-[a-z]{4})" };
        static std::wregex const system{ LR"(^Screenshot 20[0-9]{2}\-[0-9]{2}\-[0-9]{2}( at |[ _])[0-9])", std::regex::icase };
        return std::regex_search(name, original) || std::regex_search(name, system);
    }

    // ---- Status line ----

    std::wstring CountText(std::size_t count)
    {
        std::wstring const n = std::to_wstring(count);
        return count == 1 ? Localization::Format(Localization::Text(L"Library_CountOne", L"{0} image"), { n })
                          : Localization::Format(Localization::Text(L"Library_CountMany", L"{0} images"), { n });
    }

    std::wstring FoundText(std::size_t count)
    {
        return Localization::Format(Localization::Text(L"Library_Found", L"{0} images found"), { std::to_wstring(count) });
    }

    std::wstring SelectedText(std::size_t count)
    {
        return Localization::Format(Localization::Text(L"Library_Selected", L"{0} images selected"), { std::to_wstring(count) });
    }

    std::wstring DateText(LocalTime const& time)
    {
        static constexpr wchar_t const* kMonths[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                                       L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
        int const month = (time.month >= 1 && time.month <= 12) ? time.month : 1;
        int const hour12 = time.hour % 12 == 0 ? 12 : time.hour % 12;
        wchar_t minutes[4]{};
        swprintf_s(minutes, L"%02d", time.minute);
        return std::wstring{ kMonths[month - 1] } + L" " + std::to_wstring(time.day) + L", " + std::to_wstring(time.year) + L" at " +
               std::to_wstring(hour12) + L":" + minutes + (time.hour < 12 ? L"am" : L"pm");
    }

    std::wstring SizeText(std::int64_t bytes)
    {
        if (bytes == 0)
        {
            return {};
        }
        if (bytes > 1048576)
        {
            // Tenths of a MiB, truncated ("1.5", "2.0").
            auto const tenths = static_cast<std::int64_t>(static_cast<double>(bytes) * 10.0 * 0.0009765625 * 0.0009765625);
            return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10) + L"mb";
        }
        return std::to_wstring(static_cast<std::int64_t>(static_cast<double>(bytes) / 1024.0)) + L"kb";
    }

    std::wstring SingleText(LocalTime const& time, std::int64_t bytes)
    {
        std::wstring const size = SizeText(bytes);
        return size.empty() ? DateText(time) : DateText(time) + L"  \u2219  " + size;
    }

    // ---- Search ----

    std::wstring TrimQuery(std::wstring_view query)
    {
        std::size_t begin = 0;
        std::size_t end = query.size();
        while (begin < end && IsSpace(query[begin]))
        {
            ++begin;
        }
        while (end > begin && IsSpace(query[end - 1]))
        {
            --end;
        }
        return std::wstring{ query.substr(begin, end - begin) };
    }

    bool Matches(Record const& record, std::wstring_view trimmedQuery)
    {
        auto const it = record.find(kOcr);
        if (it == record.end() || trimmedQuery.empty() || it->second.empty())
        {
            return false;
        }
        int const found = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE, it->second.c_str(),
                                          static_cast<int>(it->second.size()), trimmedQuery.data(), static_cast<int>(trimmedQuery.size()),
                                          nullptr, nullptr, nullptr, 0);
        return found >= 0;
    }

    std::wstring DomainsFromText(std::wstring_view ocr)
    {
        static std::wregex const url{
            LR"(\b(https?://)?[-a-zA-Z0-9\._]{2,256}\.(com|net|ru|org|info|in|ir|uk|au|de|ua|ca|tr|co|jp|vn|cn|gr|fr|tk|tw|id|br|io|xyz|it|nl|pl|za|us|eu|mx|ch|biz|me|il|es|by|nz|kr|cz|ro|cf|ar|my|tv|kz|cl|pk|th|se|sg|cc|be|rs|ga|ma|hu|ae|su|dk|hk|at|ml|ng|np|no|app|pe|ph|ie|lk|gq|edu|fi|ai|sa|pw|bd|sk|ke|pt|az|mk|ge|tn|lt|dev|to|gov|md|lv|uz|hr|mn|am|ws|mobi|ee|bg|la|ec|blog|cloud|si|uy|nu|ba|icu|im|do|bz|kg|is|al|ug|cm|mu|one|so|sh|tj|gg|ac|eg|fm|ms|cx|sc|ve|dz|sx|vc|qa|nf|py|ki|ps|gt|ly|bo|art|tz|zw|jo|cr|ltd|nyc|lu|cy|ci|af|li|kh|pa|om|re|kw)(/([-a-zA-Z0-9()@:%_\+.~#?&//=]*))?\b)"
        };
        // One match per OCR line (the first), lower-cased without "www.", one per line.
        std::wstring out;
        std::size_t start = 0;
        while (start <= ocr.size())
        {
            std::size_t end = ocr.find(L'\n', start);
            if (end == std::wstring_view::npos)
            {
                end = ocr.size();
            }
            std::wstring const line{ ocr.substr(start, end - start) };
            std::wsmatch match;
            if (std::regex_search(line, match, url))
            {
                std::wstring domain = Lower(match[0].str());
                for (std::size_t at = domain.find(L"www."); at != std::wstring::npos; at = domain.find(L"www.", at))
                {
                    domain.erase(at, 4);
                }
                out += L"\n";
                out += domain;
            }
            start = end + 1;
        }
        return TrimQuery(out);
    }

    // ---- Facets ----

    Facets BuildFacets(std::vector<Record> const& records, std::vector<LocalTime> const& times)
    {
        static constexpr wchar_t const* kMonthNames[] = { L"January", L"February", L"March",     L"April",   L"May",      L"June",
                                                          L"July",    L"August",   L"September", L"October", L"November", L"December" };
        Facets facets;
        auto add = [](std::vector<std::wstring>& list, std::wstring const& value) {
            if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end())
            {
                list.push_back(value);
            }
        };
        auto addList = [&](std::vector<std::wstring>& list, std::wstring const& joined) {
            std::size_t start = 0;
            while (start <= joined.size())
            {
                std::size_t end = joined.find_first_of(L",\n", start);
                if (end == std::wstring::npos)
                {
                    end = joined.size();
                }
                add(list, TrimQuery(std::wstring_view{ joined }.substr(start, end - start)));
                start = end + 1;
            }
        };
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (i < times.size() && times[i].month >= 1 && times[i].month <= 12)
            {
                add(facets.months, std::wstring{ kMonthNames[times[i].month - 1] } + L" " + std::to_wstring(times[i].year));
            }
            addList(facets.domains, Field(records[i], kDomains));
            addList(facets.drawings, Field(records[i], kObjects));
            add(facets.types, Field(records[i], kMode));
            add(facets.apps, Field(records[i], kApp));
        }
        return facets;
    }

    namespace
    {
        std::wstring Hex(std::uint32_t r, std::uint32_t g, std::uint32_t b)
        {
            wchar_t buffer[8]{};
            swprintf_s(buffer, L"#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
            return buffer;
        }
    }

    std::wstring DominantColors(std::span<std::uint8_t const> bgra, std::uint32_t width, std::uint32_t height, std::size_t maxColors)
    {
        std::size_t const total = static_cast<std::size_t>(width) * height;
        if (total == 0 || bgra.size() < total * 4)
        {
            return L"#000000";
        }
        std::unordered_map<std::uint32_t, std::size_t> counts;
        for (std::size_t i = 0; i < total; ++i)
        {
            auto const* p = bgra.data() + i * 4;
            if (p[3] == 0)
            {
                continue;
            }
            ++counts[(static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0]];
        }
        auto const threshold = static_cast<std::size_t>(static_cast<double>(total) * 0.0001);
        std::vector<std::pair<std::uint32_t, std::size_t>> candidates;
        for (auto const& [rgb, count] : counts)
        {
            if (count > threshold)
            {
                candidates.emplace_back(rgb, count);
            }
        }
        // Descending frequency; ties by value so the result does not depend on hash order.
        auto byFrequency = [](auto const& a, auto const& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; };
        std::sort(candidates.begin(), candidates.end(), byFrequency);
        if (candidates.size() > 500)
        {
            candidates.resize(500);
        }
        auto component = [](std::uint32_t rgb, int shift) { return static_cast<double>((rgb >> shift) & 0xFF) / 255.0; };
        std::vector<std::pair<std::uint32_t, std::size_t>> kept;
        for (auto const& candidate : candidates)
        {
            double best = 1e9;
            std::size_t bestIndex = 0;
            for (std::size_t k = 0; k < kept.size(); ++k)
            {
                double const distance = std::abs(component(candidate.first, 16) - component(kept[k].first, 16)) +
                                        std::abs(component(candidate.first, 8) - component(kept[k].first, 8)) +
                                        std::abs(component(candidate.first, 0) - component(kept[k].first, 0));
                if (distance < best)
                {
                    best = distance;
                    bestIndex = k;
                }
            }
            if (!kept.empty() && best < 0.2)
            {
                kept[bestIndex].second += 1;
            }
            else
            {
                kept.push_back(candidate);
            }
        }
        if (kept.size() > maxColors)
        {
            kept.resize(maxColors);
        }
        std::stable_sort(kept.begin(), kept.end(), byFrequency);
        std::wstring joined;
        for (auto const& [rgb, count] : kept)
        {
            if (!joined.empty())
            {
                joined += L',';
            }
            joined += Hex(rgb >> 16, rgb >> 8, rgb);
        }
        return joined.empty() ? std::wstring{ L"#000000" } : joined;
    }

    std::wstring AverageColor(std::span<std::uint8_t const> bgra, std::uint32_t width, std::uint32_t height)
    {
        std::size_t const total = static_cast<std::size_t>(width) * height;
        if (total == 0 || bgra.size() < total * 4)
        {
            return L"#000000";
        }
        std::uint64_t r = 0, g = 0, b = 0, n = 0;
        for (std::size_t i = 0; i < total; ++i)
        {
            auto const* p = bgra.data() + i * 4;
            if (p[3] == 0)
            {
                continue;
            }
            b += p[0];
            g += p[1];
            r += p[2];
            ++n;
        }
        if (n == 0)
        {
            return L"#000000";
        }
        return Hex(static_cast<std::uint32_t>((r + n / 2) / n), static_cast<std::uint32_t>((g + n / 2) / n), static_cast<std::uint32_t>((b + n / 2) / n));
    }

    std::wstring ObjectsField(std::span<std::wstring_view const> classNames)
    {
        std::wstring letters;
        for (std::wstring_view name : classNames)
        {
            auto const obj = name.find(L"Obj");
            std::wstring_view const kind = obj == std::wstring_view::npos ? name : name.substr(obj + 3);
            if (kind.empty())
            {
                continue;
            }
            wchar_t letter = kind.front();
            if (name.find(L"Ruler") != std::wstring_view::npos) letter = L'I';
            if (name.find(L"Conceal") != std::wstring_view::npos) letter = L'B';
            if (name.find(L"Image") != std::wstring_view::npos) letter = L'P';
            letters.push_back(letter);
        }
        std::sort(letters.begin(), letters.end());
        return letters;
    }

    std::wstring LabelsField(std::span<std::wstring_view const> texts)
    {
        std::wstring joined;
        for (std::size_t i = 0; i < texts.size(); ++i)
        {
            if (i > 0)
            {
                joined += L"\n\n";
            }
            joined += texts[i];
        }
        return joined;
    }

    std::wstring DprField(double dpr)
    {
        char buffer[64];
        auto const [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), dpr);
        if (error != std::errc{})
        {
            return L"1.0";
        }
        std::wstring text(buffer, end);
        if (text.find_first_of(L".en") == std::wstring::npos)
        {
            text += L".0";
        }
        return text;
    }
}
