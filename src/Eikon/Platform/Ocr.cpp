#include "pch.h"
#include "Platform/Ocr.h"

#include "Core/OcrText.h"
#include "Core/QrCode.h"
#include "Core/Settings.h"


#include <mutex>

namespace Eikon
{
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeTextAsync(std::vector<std::uint8_t> png)
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::Ocr;
        using namespace winrt::Windows::Storage::Streams;

        std::wstring text;
        try
        {
            co_await winrt::resume_background();

            if (png.empty())
            {
                co_return winrt::hstring{};
            }

            const OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine)
            {
                co_return winrt::hstring{};
            }

            InMemoryRandomAccessStream stream;
            DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<std::uint8_t const>{ png.data(), static_cast<std::uint32_t>(png.size()) });
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            const BitmapDecoder decoder = co_await BitmapDecoder::CreateAsync(stream);
            const std::uint32_t width = decoder.PixelWidth();
            const std::uint32_t height = decoder.PixelHeight();
            const std::uint32_t maxDimension = OcrEngine::MaxImageDimension();
            if (width == 0 || height == 0)
            {
                co_return winrt::hstring{};
            }

            SoftwareBitmap bitmap{ nullptr };
            if (width > maxDimension || height > maxDimension)
            {
                const double scale = (std::min)(static_cast<double>(maxDimension) / width, static_cast<double>(maxDimension) / height);
                BitmapTransform transform;
                transform.ScaledWidth((std::max)(1u, static_cast<std::uint32_t>(width * scale)));
                transform.ScaledHeight((std::max)(1u, static_cast<std::uint32_t>(height * scale)));
                transform.InterpolationMode(BitmapInterpolationMode::Fant);
                bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, transform,
                    ExifOrientationMode::RespectExifOrientation, ColorManagementMode::DoNotColorManage);
            }
            else
            {
                bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
            }

            const OcrResult result = co_await engine.RecognizeAsync(bitmap);
            for (auto const& line : result.Lines())
            {
                if (!text.empty())
                {
                    text += L'\n';
                }
                text += std::wstring_view{ line.Text() };
            }
        }
        catch (...)
        {
            text.clear();
        }
        co_return winrt::hstring{ text };
    }

    winrt::fire_and_forget RecognizeWordBoxes(std::vector<std::uint8_t> bgra, std::int32_t width, std::int32_t height,
                                              std::function<void(std::vector<Core::Rect>)> done)
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::Ocr;

        winrt::apartment_context caller;
        std::vector<Core::Rect> boxes;
        try
        {
            co_await winrt::resume_background();
            OcrEngine const engine = OcrEngine::TryCreateFromUserProfileLanguages();
            std::uint32_t const maxDimension = OcrEngine::MaxImageDimension();
            if (engine && width > 0 && height > 0 && static_cast<std::uint32_t>(width) <= maxDimension &&
                static_cast<std::uint32_t>(height) <= maxDimension && bgra.size() >= static_cast<std::size_t>(width) * height * 4)
            {
                SoftwareBitmap bitmap{ BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied };
                winrt::Windows::Storage::Streams::Buffer buffer{ static_cast<std::uint32_t>(bgra.size()) };
                std::memcpy(buffer.data(), bgra.data(), bgra.size());
                buffer.Length(static_cast<std::uint32_t>(bgra.size()));
                bitmap.CopyFromBuffer(buffer);
                OcrResult const result = co_await engine.RecognizeAsync(bitmap);
                for (auto const& line : result.Lines())
                {
                    for (auto const& word : line.Words())
                    {
                        auto const r = word.BoundingRect();
                        boxes.push_back({ r.X, r.Y, r.Width, r.Height });
                    }
                }
            }
        }
        catch (...)
        {
            boxes.clear();
        }
        co_await caller;
        if (done)
        {
            done(std::move(boxes));
        }
    }
}

namespace Eikon::Ocr
{
    namespace
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::Ocr;

        std::mutex g_lock;
        std::vector<OcrEngine> g_engines;      // in the order they are tried
        std::atomic<bool> g_ready{ false };
        std::atomic<std::uint64_t> g_generation{ 0 };

        void Log(std::wstring const& text)
        {
            OutputDebugStringW((L"[Eikon] OCR: " + text + L"\n").c_str());
        }

        // One engine per language of the plan that Windows has, then
        // the user's profile languages when none of them is installed.
        std::vector<OcrEngine> CreateEngines(std::wstring const& primaryOcrLang)
        {
            std::vector<std::wstring> available;
            for (auto const& language : OcrEngine::AvailableRecognizerLanguages())
            {
                available.emplace_back(language.LanguageTag());
            }
            std::vector<OcrEngine> engines;
            std::vector<std::size_t> used;
            for (auto const& wanted : Core::OcrText::EngineLanguages(primaryOcrLang))
            {
                auto const index = Core::OcrText::MatchLanguage(wanted, available);
                if (!index || std::find(used.begin(), used.end(), *index) != used.end())
                {
                    continue;
                }
                if (auto engine = OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language{ available[*index] }))
                {
                    used.push_back(*index);
                    engines.push_back(engine);
                    Log(L"engine " + available[*index] + L" for " + wanted);
                }
            }
            if (engines.empty())
            {
                if (auto engine = OcrEngine::TryCreateFromUserProfileLanguages())
                {
                    engines.push_back(engine);
                    Log(L"engine from the user's languages: " + std::wstring{ engine.RecognizerLanguage().LanguageTag() });
                }
            }
            return engines;
        }

        std::vector<OcrEngine> Engines()
        {
            std::lock_guard lock{ g_lock };
            return g_engines;
        }

        SoftwareBitmap ToSoftwareBitmap(Core::Bitmap const& image, double& factor)
        {
            std::uint32_t const maxDimension = OcrEngine::MaxImageDimension();
            std::uint32_t width = static_cast<std::uint32_t>(image.width);
            std::uint32_t height = static_cast<std::uint32_t>(image.height);
            std::vector<std::uint8_t> scaled;
            std::uint8_t const* pixels = image.pixels.data();
            factor = 1.0;
            if (width > maxDimension || height > maxDimension)
            {
                // OcrEngine.MaxImageDimension: scale down to fit (high-quality WIC scaler).
                factor = (std::min)(static_cast<double>(maxDimension) / width, static_cast<double>(maxDimension) / height);
                std::uint32_t const w = (std::max)(1u, static_cast<std::uint32_t>(width * factor));
                std::uint32_t const h = (std::max)(1u, static_cast<std::uint32_t>(height * factor));
                winrt::com_ptr<IWICImagingFactory> wic;
                winrt::com_ptr<IWICBitmap> source;
                winrt::com_ptr<IWICBitmapScaler> scaler;
                winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())));
                winrt::check_hresult(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
                                                                 static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(pixels), source.put()));
                winrt::check_hresult(wic->CreateBitmapScaler(scaler.put()));
                winrt::check_hresult(scaler->Initialize(source.get(), w, h, WICBitmapInterpolationModeHighQualityCubic));
                scaled.resize(static_cast<std::size_t>(w) * h * 4);
                winrt::check_hresult(scaler->CopyPixels(nullptr, w * 4, static_cast<UINT>(scaled.size()), scaled.data()));
                pixels = scaled.data();
                width = w;
                height = h;
            }
            std::uint32_t const size = width * height * 4;
            winrt::Windows::Storage::Streams::Buffer buffer{ size };
            std::memcpy(buffer.data(), pixels, size);
            buffer.Length(size);
            SoftwareBitmap bitmap{ BitmapPixelFormat::Bgra8, static_cast<int32_t>(width), static_cast<int32_t>(height), BitmapAlphaMode::Premultiplied };
            bitmap.CopyFromBuffer(buffer);
            return bitmap;
        }
    }

    void WarmUp()
    {
        g_ready = false;
        std::uint64_t const generation = ++g_generation;
        std::wstring const primary = Core::Settings::Current().PrimaryOCRLang();
        [](std::wstring primary, std::uint64_t generation) -> winrt::fire_and_forget {
            co_await winrt::resume_background();
            try
            {
                auto engines = CreateEngines(primary);
                // Run each engine once (on a small blank image) so the first real run is fast.
                SoftwareBitmap blank{ BitmapPixelFormat::Bgra8, 64, 32, BitmapAlphaMode::Premultiplied };
                for (auto const& engine : engines)
                {
                    co_await engine.RecognizeAsync(blank);
                }
                if (generation == g_generation)
                {
                    std::lock_guard lock{ g_lock };
                    g_engines = std::move(engines);
                }
            }
            catch (...)
            {
                Log(L"warm-up failed");
            }
            if (generation == g_generation)
            {
                g_ready = true;
            }
        }(primary, generation);
    }

    bool IsReady() noexcept
    {
        return g_ready;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeAsync(std::shared_ptr<Core::Bitmap const> image)
    {
        co_await winrt::resume_background();
        if (!image || image->width < 4 || image->height < 4 || image->IsEmpty())
        {
            co_return winrt::hstring{};
        }
        // 1. QR first; text recognition is skipped when there is one.
        std::wstring const qr = Core::QrCode::Read(*image);
        if (!qr.empty())
        {
            co_return winrt::hstring{ qr };
        }
        // 2. Text: each engine of the plan until one returns text.
        std::wstring text;
        try
        {
            auto engines = Engines();
            if (engines.empty())
            {
                engines = CreateEngines(L"en-US"); // warm-up not finished: English
            }
            double factor = 1.0;
            SoftwareBitmap const bitmap = ToSoftwareBitmap(*image, factor);
            // minimumTextHeight = 6 pt at the capture's scale: lines whose words are all shorter are ignored.
            double const minimumHeight = 6.0 * image->dpr * factor;
            for (auto const& engine : engines)
            {
                OcrResult const result = co_await engine.RecognizeAsync(bitmap);
                std::vector<std::wstring> lines;
                for (auto const& line : result.Lines())
                {
                    std::vector<std::wstring> words;
                    double tallest = 0;
                    for (auto const& word : line.Words())
                    {
                        words.emplace_back(word.Text());
                        tallest = (std::max)(tallest, static_cast<double>(word.BoundingRect().Height));
                    }
                    if (tallest < minimumHeight)
                    {
                        continue;
                    }
                    std::wstring joined = Core::OcrText::JoinWords(words);
                    if (!joined.empty())
                    {
                        lines.push_back(std::move(joined));
                    }
                }
                for (auto const& line : lines)
                {
                    if (!text.empty())
                    {
                        text += L'\n';
                    }
                    text += line;
                }
                if (!text.empty())
                {
                    break;
                }
            }
        }
        catch (...)
        {
            Log(L"Unable to perform the OCR requests");
            text.clear();
        }
        co_return winrt::hstring{ text.empty() ? std::wstring{} : Core::OcrText::CleanUp(std::move(text)) };
    }
}
