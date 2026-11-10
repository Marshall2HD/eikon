#include "pch.h"
#include "Platform/Uploader.h"

#include "Core/Localization.h"
#include "Core/Settings.h"
#include "Platform/ImageCodec.h"
#include "Platform/Secrets.h"

#include <netlistmgr.h>
#include <winhttp.h>

#include <ctime>
#include <variant>

#pragma comment(lib, "winhttp.lib")

namespace Eikon::Uploader
{
    namespace
    {
        using Eikon::Localization::Text;
        namespace S3 = Core::S3;

        using WinHttpHandle = wil::unique_any<HINTERNET, decltype(&::WinHttpCloseHandle), ::WinHttpCloseHandle>;

        // Seconds added to the clock when signing, learnt from a server's Date header.
        std::atomic<std::int64_t> g_clockSkew{ 0 };

        void Log(std::wstring const& text)
        {
            OutputDebugStringW((L"[Eikon] upload: " + text + L"\n").c_str());
        }

        struct Config
        {
            S3::Settings settings;
            std::wstring prefix;
            std::wstring sharingMode;
            std::wstring publicUrl;
            std::int64_t expiration = 604800;
        };

        // UI thread: Settings and Secrets are UI-thread objects.
        Config ReadConfig()
        {
            auto& settings = Core::Settings::Current();
            auto& secrets = Secrets::Current();
            Config config;
            config.settings.accessKey = secrets.S3Access();
            config.settings.secretKey = secrets.S3Secret();
            config.settings.region = settings.UploadS3Region();
            config.settings.endpoint = settings.UploadS3Endpoint();
            config.settings.bucket = settings.UploadS3Bucket();
            config.prefix = settings.UploadS3Prefix();
            config.sharingMode = settings.UploadS3SharingMode();
            config.publicUrl = settings.UploadS3PublicUrl();
            config.expiration = settings.UploadS3PresignExpiration();
            return config;
        }

        std::int64_t Now()
        {
            return static_cast<std::int64_t>(std::time(nullptr));
        }

        // ---- WinHTTP ----

        struct Response
        {
            int status = 0;
            DWORD error = 0;            // WinHTTP / Win32 error when no response arrived
            DWORD secureFlags = 0;      // WINHTTP_CALLBACK_STATUS_FLAG_* from a secure failure
            std::string body;
            std::string date;           // raw Date header
        };

        void CALLBACK OnStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD length)
        {
            if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE && context && info && length >= sizeof(DWORD))
            {
                reinterpret_cast<Response*>(context)->secureFlags = *static_cast<DWORD*>(info);
            }
        }

        std::wstring QueryString(HINTERNET request, DWORD info)
        {
            DWORD size = 0;
            WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
            {
                return {};
            }
            std::wstring value(size / sizeof(wchar_t), L'\0');
            if (!WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size, WINHTTP_NO_HEADER_INDEX))
            {
                return {};
            }
            value.resize(size / sizeof(wchar_t));
            return value;
        }

        // One HTTPS request. `host` may carry a port ("minio.local:9000"); WinHTTP then sends it in
        // the Host header too, which matches the signed host.
        Response Send(wchar_t const* verb, std::string const& host, std::string const& path, std::vector<S3::Header> const& headers,
                      std::span<std::uint8_t const> body, DWORD timeoutMs)
        {
            Response response;
            std::wstring hostName = S3::FromUtf8(host);
            INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
            if (auto const colon = hostName.rfind(L':'); colon != std::wstring::npos && hostName.find(L']') == std::wstring::npos)
            {
                port = static_cast<INTERNET_PORT>(std::wcstoul(hostName.c_str() + colon + 1, nullptr, 10));
                hostName.resize(colon);
            }
            WinHttpHandle session{ WinHttpOpen(L"Eikon", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                                               WINHTTP_NO_PROXY_BYPASS, 0) };
            if (!session)
            {
                response.error = GetLastError();
                return response;
            }
            WinHttpSetTimeouts(session.get(), static_cast<int>(timeoutMs), static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
                               static_cast<int>(timeoutMs));
            WinHttpHandle connection{ WinHttpConnect(session.get(), hostName.c_str(), port, 0) };
            if (!connection)
            {
                response.error = GetLastError();
                return response;
            }
            std::wstring const object = S3::FromUtf8(path);
            // The path is already percent-encoded: WinHTTP must send it as is.
            WinHttpHandle request{ WinHttpOpenRequest(connection.get(), verb, object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                      WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE |
                                                                          WINHTTP_FLAG_ESCAPE_DISABLE_QUERY) };
            if (!request)
            {
                response.error = GetLastError();
                return response;
            }
            WinHttpSetStatusCallback(request.get(), &OnStatus, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0);
            std::wstring headerBlock;
            for (auto const& header : headers)
            {
                headerBlock += S3::FromUtf8(header.name) + L": " + S3::FromUtf8(header.value) + L"\r\n";
            }
            DWORD const length = static_cast<DWORD>(body.size());
            BOOL sent = WinHttpSendRequest(request.get(), headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                                           headerBlock.empty() ? 0 : static_cast<DWORD>(-1L),
                                           length ? const_cast<std::uint8_t*>(body.data()) : WINHTTP_NO_REQUEST_DATA, length, length,
                                           reinterpret_cast<DWORD_PTR>(&response));
            if (!sent || !WinHttpReceiveResponse(request.get(), nullptr))
            {
                response.error = GetLastError();
                return response;
            }
            DWORD status = 0;
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);
            response.status = static_cast<int>(status);
            response.date = S3::ToUtf8(QueryString(request.get(), WINHTTP_QUERY_DATE));
            while (true)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0)
                {
                    break;
                }
                std::size_t const offset = response.body.size();
                response.body.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request.get(), response.body.data() + offset, available, &read))
                {
                    response.body.resize(offset);
                    break;
                }
                response.body.resize(offset + read);
                if (response.body.size() > 64 * 1024)
                {
                    break; // error documents are small; only "Time"/"SignatureDoesNotMatch" matter
                }
            }
            return response;
        }

        bool IsOffline()
        {
            winrt::com_ptr<INetworkListManager> manager;
            if (FAILED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(manager.put()))))
            {
                return false;
            }
            VARIANT_BOOL connected = VARIANT_TRUE;
            return SUCCEEDED(manager->get_IsConnectedToInternet(&connected)) && connected == VARIANT_FALSE;
        }

        // The network error code (negative) for a WinHTTP failure, for the error texts.
        std::optional<int> UrlErrorCode(Response const& response)
        {
            switch (response.error)
            {
            case ERROR_WINHTTP_TIMEOUT:
                return -1001;
            case ERROR_WINHTTP_NAME_NOT_RESOLVED:
                return IsOffline() ? -1009 : -1003;
            case ERROR_WINHTTP_CANNOT_CONNECT:
                return IsOffline() ? -1009 : -1004;
            case ERROR_WINHTTP_CONNECTION_ERROR:
                return -1005;
            case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
                return -1201;
            case ERROR_WINHTTP_SECURE_INVALID_CA:
            case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
            case ERROR_WINHTTP_SECURE_INVALID_CERT:
            case ERROR_WINHTTP_SECURE_CERT_REVOKED:
                return -1202;
            case ERROR_WINHTTP_SECURE_FAILURE:
                if (response.secureFlags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID)
                {
                    return -1201;
                }
                if (response.secureFlags & (WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA | WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID |
                                            WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT | WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED))
                {
                    return -1202;
                }
                return -1200;
            case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
            case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
                return -1200;
            default:
                return std::nullopt;
            }
        }

        std::wstring NetworkErrorMessage(Response const& response)
        {
            if (auto const code = UrlErrorCode(response))
            {
                return S3::ErrorMessage(*code);
            }
            wchar_t* buffer = nullptr;
            DWORD const length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
                                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                                GetModuleHandleW(L"winhttp.dll"), response.error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
            std::wstring message;
            if (length && buffer)
            {
                message.assign(buffer, length);
                while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
                {
                    message.pop_back();
                }
            }
            if (buffer)
            {
                LocalFree(buffer);
            }
            return message.empty() ? Text(L"Upload_NoResponse", L"No HTTP response received") : message;
        }

        // The upload with its one clock-skew retry.
        // Returns an error message, or nothing on success.
        std::optional<std::wstring> Request(wchar_t const* verb, Config const& config, S3::Target const& target, std::string const& key,
                                            std::span<std::uint8_t const> body, std::string const& contentType, bool forDelete)
        {
            std::string const access = S3::ToUtf8(config.settings.accessKey);
            std::string const secret = S3::ToUtf8(config.settings.secretKey);
            std::string const uri = S3::CanonicalUri(target, key);
            std::string const payloadHash = S3::Sha256Hex(body);
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                std::int64_t const now = Now() + g_clockSkew.load();
                auto const signedRequest = S3::SignRequest(S3::ToUtf8(verb), target, uri, access, secret, contentType, payloadHash, now);
                if (signedRequest.authorization.empty())
                {
                    return Text(L"Upload_SignFailed", L"Failed to sign the request");
                }
                Response const response = Send(verb, target.host, uri, signedRequest.headers, body, 60000);
                if (response.status == 0)
                {
                    return NetworkErrorMessage(response);
                }
                if (response.status >= 200 && response.status < 300)
                {
                    return std::nullopt;
                }
                if (S3::ShouldRetryForClockSkew(response.status, attempt, response.body))
                {
                    Response const head = Send(L"HEAD", target.host, "/", {}, {}, 10000);
                    if (auto const serverTime = S3::ParseHttpDate(head.date))
                    {
                        g_clockSkew = *serverTime - Now();
                    }
                    Log(L"RequestTimeTooSkewed: " + std::to_wstring(g_clockSkew.load()));
                    continue;
                }
                Log(L"Failed S3 uploading. clockSkew: " + std::to_wstring(g_clockSkew.load()) + L", status" + std::to_wstring(response.status) +
                    L", attempt" + std::to_wstring(attempt));
                return S3::ErrorMessage(response.status, forDelete);
            }
            return S3::ErrorMessage(403, forDelete);
        }

        // ---- Encoding ----

        std::vector<std::uint8_t> EncodeJpeg(std::uint32_t width, std::uint32_t height, std::span<std::uint8_t const> pixels, double dpi)
        {
            std::vector<std::uint8_t> result;
            try
            {
                winrt::com_ptr<IWICImagingFactory> wic;
                winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())));
                winrt::com_ptr<IWICBitmap> source;
                winrt::check_hresult(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
                                                                 static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()), source.put()));
                winrt::com_ptr<IWICBitmapSource> bgr;
                winrt::check_hresult(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR, source.get(), bgr.put()));
                winrt::com_ptr<IStream> stream;
                winrt::check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()));
                winrt::com_ptr<IWICBitmapEncoder> encoder;
                winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put()));
                winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
                winrt::com_ptr<IWICBitmapFrameEncode> frame;
                winrt::com_ptr<IPropertyBag2> options;
                winrt::check_hresult(encoder->CreateNewFrame(frame.put(), options.put()));
                PROPBAG2 option{};
                option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                VARIANT quality;
                VariantInit(&quality);
                quality.vt = VT_R4;
                quality.fltVal = 0.85f; // JPEG quality 0.85
                options->Write(1, &option, &quality);
                winrt::check_hresult(frame->Initialize(options.get()));
                winrt::check_hresult(frame->SetSize(width, height));
                winrt::check_hresult(frame->SetResolution(dpi, dpi));
                WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
                winrt::check_hresult(frame->SetPixelFormat(&format));
                winrt::check_hresult(frame->WriteSource(bgr.get(), nullptr));
                winrt::check_hresult(frame->Commit());
                winrt::check_hresult(encoder->Commit());
                HGLOBAL memory = nullptr;
                winrt::check_hresult(GetHGlobalFromStream(stream.get(), &memory));
                STATSTG stat{};
                winrt::check_hresult(stream->Stat(&stat, STATFLAG_NONAME));
                auto const* data = static_cast<std::uint8_t const*>(GlobalLock(memory));
                if (data)
                {
                    result.assign(data, data + stat.cbSize.QuadPart);
                    GlobalUnlock(memory);
                }
            }
            catch (...)
            {
                result.clear();
            }
            return result;
        }

        std::vector<std::uint8_t> Halve(Core::Bitmap const& image, std::uint32_t& width, std::uint32_t& height)
        {
            std::vector<std::uint8_t> pixels;
            try
            {
                width = (std::max)(1u, static_cast<std::uint32_t>(image.width / 2));
                height = (std::max)(1u, static_cast<std::uint32_t>(image.height / 2));
                winrt::com_ptr<IWICImagingFactory> wic;
                winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())));
                winrt::com_ptr<IWICBitmap> source;
                winrt::check_hresult(wic->CreateBitmapFromMemory(static_cast<UINT>(image.width), static_cast<UINT>(image.height),
                                                                 GUID_WICPixelFormat32bppPBGRA, static_cast<UINT>(image.Stride()),
                                                                 static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()),
                                                                 source.put()));
                winrt::com_ptr<IWICBitmapScaler> scaler;
                winrt::check_hresult(wic->CreateBitmapScaler(scaler.put()));
                winrt::check_hresult(scaler->Initialize(source.get(), width, height, WICBitmapInterpolationModeHighQualityCubic));
                pixels.resize(static_cast<std::size_t>(width) * height * 4);
                winrt::check_hresult(scaler->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
            }
            catch (...)
            {
                pixels.clear();
            }
            return pixels;
        }

        struct Encoded
        {
            std::vector<std::uint8_t> bytes;
            std::string extension;
        };

        // nullopt + error text when nothing fits.
        std::variant<Encoded, std::wstring> Encode(Core::Bitmap const& image)
        {
            std::vector<std::uint8_t> halfPixels;
            std::uint32_t halfWidth = 0;
            std::uint32_t halfHeight = 0;
            bool anyEncoded = false;
            for (auto const& step : S3::EncodingPlan(image.dpr))
            {
                std::uint32_t width = static_cast<std::uint32_t>(image.width);
                std::uint32_t height = static_cast<std::uint32_t>(image.height);
                std::span<std::uint8_t const> pixels = image.pixels;
                double dpi = 96.0 * image.dpr; // dpr·72 is 96·scale on Windows
                if (step.halved)
                {
                    if (halfPixels.empty())
                    {
                        halfPixels = Halve(image, halfWidth, halfHeight);
                    }
                    if (halfPixels.empty())
                    {
                        continue;
                    }
                    width = halfWidth;
                    height = halfHeight;
                    pixels = halfPixels;
                    dpi = 96.0 * image.dpr / 2;
                }
                std::vector<std::uint8_t> bytes = step.jpeg ? EncodeJpeg(width, height, pixels, dpi)
                                                            : ImageCodec::BgraToPng(width, height, pixels, true, dpi);
                if (bytes.empty())
                {
                    continue;
                }
                anyEncoded = true;
                if (bytes.size() <= S3::kMaxUploadBytes)
                {
                    return Encoded{ std::move(bytes), step.jpeg ? "jpeg" : "png" };
                }
            }
            return anyEncoded ? Text(L"Upload_TooLarge", L"Image is too large") : Text(L"Upload_NoImageData", L"Can't get image data");
        }

        S3::LocalTime ToLocalTime(std::chrono::system_clock::time_point time)
        {
            std::time_t const t = std::chrono::system_clock::to_time_t(time);
            std::tm local{};
            localtime_s(&local, &t);
            return { local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec };
        }

        winrt::fire_and_forget RunUpload(std::shared_ptr<Core::Bitmap const> image, Config config, S3::LocalTime time,
                                         std::function<void(Result const&)> done)
        {
            auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            Result result;
            co_await winrt::resume_background();
            try
            {
                auto target = S3::ResolveTarget(config.settings);
                if (auto const* error = std::get_if<std::wstring>(&target))
                {
                    result.message = *error;
                }
                else
                {
                    auto encoded = Encode(*image);
                    if (auto const* error2 = std::get_if<std::wstring>(&encoded))
                    {
                        result.message = *error2;
                    }
                    else
                    {
                        auto const& data = std::get<Encoded>(encoded);
                        std::string const key = S3::ObjectKey(config.prefix, S3::ObjectName(time, data.extension));
                        S3::Target const& resolved = std::get<S3::Target>(target);
                        if (auto error3 = Request(L"PUT", config, resolved, key, data.bytes, S3::ContentTypeForKey(key), false))
                        {
                            result.message = *error3;
                        }
                        else if (config.sharingMode == L"presign")
                        {
                            std::string const url = S3::PresignedUrl(resolved, key, S3::ToUtf8(config.settings.accessKey),
                                                                     S3::ToUtf8(config.settings.secretKey), config.expiration,
                                                                     Now() + g_clockSkew.load());
                            if (url.empty())
                            {
                                result.message = Text(L"Upload_PresignFailed", L"URL presigning failed");
                            }
                            else
                            {
                                result.ok = true;
                                result.url = S3::FromUtf8(url);
                            }
                        }
                        else if (config.sharingMode == L"public")
                        {
                            if (auto url = S3::PublicUrl(config.publicUrl, key))
                            {
                                result.ok = true;
                                result.url = *url;
                            }
                            else
                            {
                                result.message = Text(L"Upload_NoPublicUrl", L"Failed to generate URL, no public bucket URL is provided");
                            }
                        }
                        else
                        {
                            result.ok = true;
                            result.message = Text(L"Upload_Uploaded", L"File uploaded successfully");
                        }
                    }
                }
            }
            catch (...)
            {
                result = {};
                result.message = Text(L"Upload_NoResponse", L"No HTTP response received");
            }
            if (queue)
            {
                co_await wil::resume_foreground(queue);
            }
            if (done)
            {
                done(result);
            }
        }
    }

    std::optional<MissingSetting> CheckSettings()
    {
        Config const config = ReadConfig();
        if (config.settings.accessKey.empty())
        {
            return MissingSetting{ Text(L"Upload_MissingAccessTitle", L"S3 Access Key is Missing"),
                                   Text(L"Upload_MissingAccessMessage", L"Enter S3 Access Key in the settings") };
        }
        if (config.settings.secretKey.empty())
        {
            return MissingSetting{ Text(L"Upload_MissingSecretTitle", L"S3 Secret Key is Missing"),
                                   Text(L"Upload_MissingSecretMessage", L"Enter S3 Secret Key in the settings") };
        }
        if (config.settings.region.empty() && config.settings.endpoint.empty())
        {
            return MissingSetting{ Text(L"Upload_MissingRegionTitle", L"Region or Endpoint is Needed"),
                                   Text(L"Upload_MissingRegionMessage", L"Enter S3 Region or Endpoint in the settings") };
        }
        if (config.settings.bucket.empty())
        {
            return MissingSetting{ Text(L"Upload_MissingBucketTitle", L"S3 Bucket is Missing"),
                                   Text(L"Upload_MissingBucketMessage", L"Enter S3 Bucket in the settings") };
        }
        return std::nullopt;
    }

    void Upload(std::shared_ptr<Core::Bitmap const> image, std::chrono::system_clock::time_point captureTime,
                std::function<void(Result const&)> done)
    {
        if (!image || image->IsEmpty())
        {
            if (done)
            {
                done({ false, {}, Text(L"Upload_NoImageData", L"Can't get image data") });
            }
            return;
        }
        RunUpload(std::move(image), ReadConfig(), ToLocalTime(captureTime), std::move(done));
    }

    void TestConnection(std::function<void(bool ok, std::wstring const& message)> done)
    {
        if (auto const missing = CheckSettings())
        {
            if (done)
            {
                done(false, missing->title);
            }
            return;
        }
        [](Config config, std::function<void(bool, std::wstring const&)> done) -> winrt::fire_and_forget {
            auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            bool ok = false;
            std::wstring message;
            co_await winrt::resume_background();
            try
            {
                auto target = S3::ResolveTarget(config.settings);
                if (auto const* error = std::get_if<std::wstring>(&target))
                {
                    message = *error;
                }
                else
                {
                    GUID guid{};
                    CoCreateGuid(&guid);
                    wchar_t hex[16];
                    swprintf_s(hex, L"%08lX", guid.Data1); // first 8 characters of a UUID
                    std::string const key = S3::ObjectKey(config.prefix, "test_" + S3::ToUtf8(hex) + ".txt");
                    std::string_view const body = "hello world";
                    std::span<std::uint8_t const> bytes{ reinterpret_cast<std::uint8_t const*>(body.data()), body.size() };
                    S3::Target const& resolved = std::get<S3::Target>(target);
                    if (auto error2 = Request(L"PUT", config, resolved, key, bytes, "text/plain", false))
                    {
                        message = *error2;
                    }
                    else if (auto error3 = Request(L"DELETE", config, resolved, key, {}, S3::ContentTypeForKey(key), true))
                    {
                        message = *error3;
                    }
                    else
                    {
                        ok = true;
                    }
                }
            }
            catch (...)
            {
                message = Text(L"Upload_NoResponse", L"No HTTP response received");
            }
            if (queue)
            {
                co_await wil::resume_foreground(queue);
            }
            if (done)
            {
                done(ok, message);
            }
        }(ReadConfig(), std::move(done));
    }
}
