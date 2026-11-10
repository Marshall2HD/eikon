#include "pch.h"
#include "Core/S3.h"

#include "Core/Localization.h"

#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

namespace Eikon::Core::S3
{
    namespace
    {
        constexpr std::string_view kAlgorithm = "AWS4-HMAC-SHA256";
        constexpr std::string_view kService = "s3";

        std::vector<std::uint8_t> Hash(std::span<std::uint8_t const> key, bool hmac, std::span<std::uint8_t const> data)
        {
            std::vector<std::uint8_t> result(32);
            // BCryptHash (Windows 10+) runs the whole SHA-256 / HMAC in one call on the
            // pseudo-handles, so no provider has to be opened or cached.
            BCRYPT_ALG_HANDLE const algorithm = hmac ? BCRYPT_HMAC_SHA256_ALG_HANDLE : BCRYPT_SHA256_ALG_HANDLE;
            NTSTATUS const status = BCryptHash(algorithm, hmac ? const_cast<PUCHAR>(key.data()) : nullptr,
                                               hmac ? static_cast<ULONG>(key.size()) : 0,
                                               const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()),
                                               result.data(), static_cast<ULONG>(result.size()));
            if (!BCRYPT_SUCCESS(status))
            {
                result.clear();
            }
            return result;
        }

        std::span<std::uint8_t const> Bytes(std::string_view text) noexcept
        {
            return { reinterpret_cast<std::uint8_t const*>(text.data()), text.size() };
        }

        bool IsUnreserved(unsigned char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                   c == '_' || c == '~';
        }

        std::string Lower(std::string_view text)
        {
            std::string result{ text };
            for (char& c : result)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return result;
        }

        std::string Trim(std::string_view text)
        {
            auto const isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (!text.empty() && isSpace(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && isSpace(text.back()))
            {
                text.remove_suffix(1);
            }
            // SigV4 collapses runs of spaces inside header values to one.
            std::string result;
            bool lastSpace = false;
            for (char c : text)
            {
                bool const space = c == ' ';
                if (space && lastSpace)
                {
                    continue;
                }
                result.push_back(c);
                lastSpace = space;
            }
            return result;
        }

        bool EndsWith(std::wstring_view text, std::wstring_view suffix) noexcept
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // Howard Hinnant's days_from_civil.
        std::int64_t DaysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept
        {
            y -= m <= 2;
            std::int64_t const era = (y >= 0 ? y : y - 399) / 400;
            unsigned const yoe = static_cast<unsigned>(y - era * 400);
            unsigned const doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
        }

        struct Civil
        {
            std::int64_t year;
            unsigned month, day, hour, minute, second;
        };

        Civil CivilFromUnix(std::int64_t seconds) noexcept
        {
            std::int64_t days = seconds / 86400;
            std::int64_t rest = seconds % 86400;
            if (rest < 0)
            {
                rest += 86400;
                --days;
            }
            days += 719468;
            std::int64_t const era = (days >= 0 ? days : days - 146096) / 146097;
            unsigned const doe = static_cast<unsigned>(days - era * 146097);
            unsigned const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            unsigned const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            unsigned const mp = (5 * doy + 2) / 153;
            unsigned const d = doy - (153 * mp + 2) / 5 + 1;
            unsigned const m = mp < 10 ? mp + 3 : mp - 9;
            std::int64_t const y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2);
            return { y, m, d, static_cast<unsigned>(rest / 3600), static_cast<unsigned>(rest / 60 % 60), static_cast<unsigned>(rest % 60) };
        }
    }

    // ---- Hashing ----

    std::vector<std::uint8_t> Sha256(std::span<std::uint8_t const> data)
    {
        return Hash({}, false, data);
    }

    std::string Sha256Hex(std::span<std::uint8_t const> data)
    {
        return ToHex(Sha256(data));
    }

    std::string Sha256Hex(std::string_view data)
    {
        return Sha256Hex(Bytes(data));
    }

    std::vector<std::uint8_t> HmacSha256(std::span<std::uint8_t const> key, std::string_view data)
    {
        return Hash(key, true, Bytes(data));
    }

    std::string ToHex(std::span<std::uint8_t const> bytes)
    {
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (std::uint8_t b : bytes)
        {
            result.push_back(kDigits[b >> 4]);
            result.push_back(kDigits[b & 0xF]);
        }
        return result;
    }

    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int const size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(size, 0)), '\0');
        if (size > 0)
        {
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        }
        return result;
    }

    std::wstring FromUtf8(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int const size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>((std::max)(size, 0)), L'\0');
        if (size > 0)
        {
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        }
        return result;
    }

    std::string UriEncode(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            auto const c = static_cast<unsigned char>(ch);
            if (IsUnreserved(c))
            {
                result.push_back(static_cast<char>(c));
            }
            else
            {
                char buffer[4];
                std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
                result.append(buffer, 3);
            }
        }
        return result;
    }

    std::string EncodePath(std::string_view path)
    {
        std::string result;
        std::size_t start = 0;
        while (true)
        {
            std::size_t const slash = path.find('/', start);
            result += UriEncode(path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start));
            if (slash == std::string_view::npos)
            {
                break;
            }
            result.push_back('/');
            start = slash + 1;
        }
        return result;
    }

    // ---- SigV4 ----

    std::string SignedHeaders(std::vector<Header> const& headers)
    {
        std::vector<std::string> names;
        for (auto const& header : headers)
        {
            names.push_back(Lower(header.name));
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        std::string result;
        for (auto const& name : names)
        {
            if (!result.empty())
            {
                result.push_back(';');
            }
            result += name;
        }
        return result;
    }

    std::string CanonicalRequest(CanonicalInput const& input)
    {
        std::vector<std::pair<std::string, std::string>> headers;
        for (auto const& header : input.headers)
        {
            headers.emplace_back(Lower(header.name), Trim(header.value));
        }
        std::stable_sort(headers.begin(), headers.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

        std::string result = input.method + "\n" + input.uri + "\n" + input.query + "\n";
        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            // Repeated names are joined with "," (SigV4).
            if (i > 0 && headers[i].first == headers[i - 1].first)
            {
                result.pop_back();
                result += "," + headers[i].second + "\n";
                continue;
            }
            result += headers[i].first + ":" + headers[i].second + "\n";
        }
        result += "\n" + SignedHeaders(input.headers) + "\n" + input.payloadHash;
        return result;
    }

    std::string CredentialScope(std::string_view date, std::string_view region, std::string_view service)
    {
        return std::string{ date } + "/" + std::string{ region } + "/" + std::string{ service } + "/aws4_request";
    }

    std::string StringToSign(std::string_view amzDate, std::string_view scope, std::string_view canonicalRequest)
    {
        return std::string{ kAlgorithm } + "\n" + std::string{ amzDate } + "\n" + std::string{ scope } + "\n" + Sha256Hex(canonicalRequest);
    }

    std::vector<std::uint8_t> SigningKey(std::string_view secret, std::string_view date, std::string_view region, std::string_view service)
    {
        std::string const seed = "AWS4" + std::string{ secret };
        auto key = HmacSha256(Bytes(seed), date);
        key = HmacSha256(key, region);
        key = HmacSha256(key, service);
        return HmacSha256(key, "aws4_request");
    }

    std::string Signature(std::string_view secret, std::string_view date, std::string_view region, std::string_view service,
                          std::string_view stringToSign)
    {
        return ToHex(HmacSha256(SigningKey(secret, date, region, service), stringToSign));
    }

    std::string PresignQuery(std::string_view accessKey, std::string_view amzDate, std::string_view region, std::string_view service,
                             std::int64_t expires)
    {
        std::string const date{ amzDate.substr(0, 8) };
        std::string const credential = std::string{ accessKey } + "/" + CredentialScope(date, region, service);
        return "X-Amz-Algorithm=" + std::string{ kAlgorithm } + "&X-Amz-Credential=" + UriEncode(credential) +
               "&X-Amz-Date=" + std::string{ amzDate } + "&X-Amz-Expires=" + std::to_string(expires) + "&X-Amz-SignedHeaders=host";
    }

    // ---- Dates ----

    std::string AmzDate(std::int64_t unixSeconds)
    {
        Civil const c = CivilFromUnix(unixSeconds);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04lld%02u%02uT%02u%02u%02uZ", static_cast<long long>(c.year), c.month, c.day, c.hour,
                      c.minute, c.second);
        return buffer;
    }

    std::string ScopeDate(std::int64_t unixSeconds)
    {
        return AmzDate(unixSeconds).substr(0, 8);
    }

    std::optional<std::int64_t> ParseHttpDate(std::string_view text)
    {
        // "Sun, 08 Nov 2026 13:45:07 GMT": EEE ',' dd MMM yyyy HH ':' mm ':' ss 'GMT'
        static constexpr std::string_view kDays[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        static constexpr std::string_view kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        if (text.size() != 29 || text[3] != ',' || text[4] != ' ' || text[7] != ' ' || text[11] != ' ' || text[16] != ' ' ||
            text[19] != ':' || text[22] != ':' || text.substr(25) != " GMT")
        {
            return std::nullopt;
        }
        if (std::find(std::begin(kDays), std::end(kDays), text.substr(0, 3)) == std::end(kDays))
        {
            return std::nullopt;
        }
        auto const month = std::find(std::begin(kMonths), std::end(kMonths), text.substr(8, 3));
        if (month == std::end(kMonths))
        {
            return std::nullopt;
        }
        auto const number = [&](std::size_t at, std::size_t length) -> std::optional<int> {
            int value = 0;
            for (std::size_t i = at; i < at + length; ++i)
            {
                if (text[i] < '0' || text[i] > '9')
                {
                    return std::nullopt;
                }
                value = value * 10 + (text[i] - '0');
            }
            return value;
        };
        auto const day = number(5, 2);
        auto const year = number(12, 4);
        auto const hour = number(17, 2);
        auto const minute = number(20, 2);
        auto const second = number(23, 2);
        if (!day || !year || !hour || !minute || !second || *day < 1 || *day > 31 || *hour > 23 || *minute > 59 || *second > 60)
        {
            return std::nullopt;
        }
        unsigned const m = static_cast<unsigned>(month - std::begin(kMonths)) + 1;
        return DaysFromCivil(*year, m, static_cast<unsigned>(*day)) * 86400 + *hour * 3600 + *minute * 60 + *second;
    }

    // ---- Rules ----

    std::variant<Target, std::wstring> ResolveTarget(Settings const& settings)
    {
        std::wstring endpoint = settings.endpoint;
        std::wstring region = settings.region;
        if (endpoint.empty() && region.empty())
        {
            return Localization::Text(L"Upload_RegionRequired", L"Region is required when no endpoint is provided.");
        }
        Target target;
        target.bucket = ToUtf8(settings.bucket);
        if (endpoint.empty())
        {
            target.region = ToUtf8(region);
            target.host = target.bucket + ".s3." + target.region + ".amazonaws.com";
            target.pathStyle = false;
            return target;
        }
        if (endpoint.starts_with(L"https://"))
        {
            endpoint.erase(0, 8);
        }
        // Cloudflare R2: users often paste the bucket URL, ".../<bucket>".
        if (endpoint.find(L"cloudflarestorage.com") != std::wstring::npos && EndsWith(endpoint, L"/" + settings.bucket))
        {
            endpoint.erase(endpoint.size() - settings.bucket.size() - 1);
        }
        // A trailing "/" would end up in the Host header.
        while (!endpoint.empty() && endpoint.back() == L'/')
        {
            endpoint.pop_back();
        }
        target.region = region.empty() ? std::string{ "auto" } : ToUtf8(region);
        target.host = ToUtf8(endpoint);
        target.pathStyle = true;
        return target;
    }

    std::string CanonicalUri(Target const& target, std::string_view key)
    {
        std::string const path = target.pathStyle ? "/" + target.bucket + "/" + std::string{ key } : "/" + std::string{ key };
        return EncodePath(path);
    }

    std::string ContentTypeForKey(std::string_view key)
    {
        std::size_t const dot = key.rfind('.');
        std::size_t const slash = key.rfind('/');
        std::string extension;
        if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash))
        {
            extension = Lower(key.substr(dot + 1));
        }
        if (extension == "png") return "image/png";
        if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
        if (extension == "gif") return "image/gif";
        if (extension == "webp") return "image/webp";
        if (extension == "svg") return "image/svg+xml";
        if (extension == "pdf") return "application/pdf";
        if (extension == "txt") return "text/plain";
        if (extension == "json") return "application/json";
        return "application/octet-stream";
    }

    std::string ObjectName(LocalTime const& time, std::string_view extension)
    {
        char date[16];
        std::snprintf(date, sizeof(date), "%04d%02d%02d", time.year, time.month, time.day);
        // %T: base 36, lowercase, of round(secondOfDay / 2) + 36² (always at least 3 digits).
        auto value = static_cast<std::int64_t>(std::round(time.secondOfDay / 2.0)) + 1296;
        std::string token;
        do
        {
            int const digit = static_cast<int>(value % 36);
            token.insert(token.begin(), static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10));
            value /= 36;
        } while (value > 0);
        return "SCR-" + std::string{ date } + "-" + token + "." + std::string{ extension };
    }

    std::string ObjectKey(std::wstring_view prefix, std::string_view name)
    {
        while (!prefix.empty() && prefix.front() == L'/')
        {
            prefix.remove_prefix(1);
        }
        while (!prefix.empty() && prefix.back() == L'/')
        {
            prefix.remove_suffix(1);
        }
        if (prefix.empty())
        {
            return std::string{ name };
        }
        return ToUtf8(prefix) + "/" + std::string{ name };
    }

    SignedRequest SignRequest(std::string_view method, Target const& target, std::string_view uri, std::string_view accessKey,
                              std::string_view secretKey, std::string_view contentType, std::string_view payloadHash,
                              std::int64_t unixSeconds)
    {
        std::string const amzDate = AmzDate(unixSeconds);
        std::string const date = amzDate.substr(0, 8);

        CanonicalInput input;
        input.method = std::string{ method };
        input.uri = std::string{ uri };
        if (!contentType.empty())
        {
            input.headers.push_back({ "content-type", std::string{ contentType } });
        }
        input.headers.push_back({ "host", target.host });
        input.headers.push_back({ "x-amz-content-sha256", std::string{ payloadHash } });
        input.headers.push_back({ "x-amz-date", amzDate });
        input.payloadHash = std::string{ payloadHash };

        std::string const scope = CredentialScope(date, target.region, kService);
        std::string const signature = Signature(secretKey, date, target.region, kService, StringToSign(amzDate, scope, CanonicalRequest(input)));

        SignedRequest request;
        request.uri = std::string{ uri };
        request.authorization = std::string{ kAlgorithm } + " Credential=" + std::string{ accessKey } + "/" + scope +
                                ", SignedHeaders=" + SignedHeaders(input.headers) + ", Signature=" + signature;
        if (!contentType.empty())
        {
            request.headers.push_back({ "Content-Type", std::string{ contentType } });
        }
        request.headers.push_back({ "x-amz-content-sha256", std::string{ payloadHash } });
        request.headers.push_back({ "x-amz-date", amzDate });
        request.headers.push_back({ "Authorization", request.authorization });
        return request;
    }

    std::string PresignedUrl(Target const& target, std::string_view key, std::string_view accessKey, std::string_view secretKey,
                             std::int64_t expires, std::int64_t unixSeconds)
    {
        std::string const amzDate = AmzDate(unixSeconds);
        std::string const date = amzDate.substr(0, 8);
        CanonicalInput input;
        input.method = "GET";
        input.uri = CanonicalUri(target, key);
        input.query = PresignQuery(accessKey, amzDate, target.region, kService, expires);
        input.headers.push_back({ "host", target.host });
        input.payloadHash = "UNSIGNED-PAYLOAD";
        std::string const scope = CredentialScope(date, target.region, kService);
        std::string const signature = Signature(secretKey, date, target.region, kService, StringToSign(amzDate, scope, CanonicalRequest(input)));
        return "https://" + target.host + input.uri + "?" + input.query + "&X-Amz-Signature=" + signature;
    }

    std::optional<std::wstring> PublicUrl(std::wstring_view baseUrl, std::string_view key)
    {
        if (baseUrl.find(L'.') == std::wstring_view::npos || baseUrl.find(L"//") == std::wstring_view::npos)
        {
            return std::nullopt;
        }
        std::wstring url{ baseUrl };
        if (url.back() != L'/')
        {
            url.push_back(L'/');
        }
        return url + FromUtf8(key);
    }

    bool ShouldRetryForClockSkew(int status, int attempt, std::string_view body)
    {
        return status == 403 && attempt == 0 &&
               (body.find("Time") != std::string_view::npos || body.find("SignatureDoesNotMatch") != std::string_view::npos);
    }

    std::wstring KnownErrorMessage(int code)
    {
        using Localization::Text;
        switch (code)
        {
        case 400: return Text(L"Upload_Error400", L"Error 400. Try checking the Access Key or Endpoint.");
        case 401: return Text(L"Upload_Error401", L"Credentials rejected (401). Not common for S3, check proxy/gateway.");
        case 403: return Text(L"Upload_Error403", L"Access Denied (403). Are Secret Key and Bucket correct? Try again.");
        case 404: return Text(L"Upload_Error404", L"Not Found (404). Object key or bucket name/path is wrong.");
        case 409: return Text(L"Upload_Error409", L"Conflict (409). Bucket state or object versioning conflict.");
        case 411: return Text(L"Upload_Error411", L"Length Required (411). Missing or invalid Content-Length.");
        case 412: return Text(L"Upload_Error412", L"Precondition Failed (412). ETag/If-* headers didn\u2019t match.");
        case 429: return Text(L"Upload_Error429", L"Too Many Requests (429). You\u2019re being throttled; back off and retry.");
        case 500: return Text(L"Upload_Error500", L"Server Error (500). Try again; could be provider-side.");
        case 503: return Text(L"Upload_Error503", L"Service Unavailable (503). Throttled or server busy; retry with backoff.");
        case 301: return Text(L"Upload_Error301", L"Error 301. Wrong Region or Endpoint for this bucket.");
        case 307: return Text(L"Upload_Error307", L"Error 307. Bucket is in a different Region; update Region/Endpoint.");
        case -1001: return Text(L"Upload_ErrorTimedOut", L"Request timed out (-1001).");
        case -1003: return Text(L"Upload_ErrorHostNotFound", L"Cannot find host (-1003). Endpoint or Region likely wrong.");
        case -1004: return Text(L"Upload_ErrorCannotConnect", L"Cannot connect to host (-1004). Endpoint may be unreachable or blocked.");
        case -1005: return Text(L"Upload_ErrorConnectionLost", L"Network connection lost (-1005). Try again.");
        case -1009: return Text(L"Upload_ErrorOffline", L"No internet connection (-1009).");
        case -1200: return Text(L"Upload_ErrorSecureFailed", L"Secure connection failed (-1200). TLS/SSL issue (cert, TLS version, or MITM).");
        case -1201: return Text(L"Upload_ErrorCertDate", L"Secure connection: certificate expired/not yet valid (-1201). Check system date/time.");
        case -1202: return Text(L"Upload_ErrorCertUntrusted", L"Secure connection: certificate not trusted (-1202). System clock may be wrong.");
        default: return {};
        }
    }

    std::wstring ErrorMessage(int code, bool forDelete)
    {
        std::wstring known = KnownErrorMessage(code);
        if (!known.empty())
        {
            return known;
        }
        return forDelete ? Localization::Format(Localization::Text(L"Upload_DeleteFailedStatus", L"Delete failed with status: {0}"),
                                                { std::to_wstring(code) })
                         : Localization::Format(Localization::Text(L"Upload_FailedStatus", L"Upload failed with status: {0}"),
                                                { std::to_wstring(code) });
    }

    std::vector<EncodingStep> EncodingPlan(double dpr)
    {
        std::vector<EncodingStep> plan{ { false, false }, { true, false } };
        if (dpr >= 2.0)
        {
            plan.push_back({ false, true });
            plan.push_back({ true, true });
        }
        return plan;
    }
}
