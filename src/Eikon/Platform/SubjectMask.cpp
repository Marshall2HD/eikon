#include "pch.h"
#include "Platform/SubjectMask.h"

#include "Core/CutOut.h"
#include "resource.h"

#include <MemoryBuffer.h>
#include <winrt/Microsoft.Windows.AI.h>
#include <winrt/Microsoft.Windows.AI.Imaging.h>

// The Windows App SDK's Windows ML ships ONNX Runtime in its framework package; its auto-initializer
// (WindowsMLAutoInitializer.cpp, compiled in by the Microsoft.WindowsAppSDK.ML package) defines
// OrtGetApiBase to load that onnxruntime.dll. Manual init, so that a missing runtime is a null check
// here instead of a crash on first use.
#define ORT_API_MANUAL_INIT
#include <winml/onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Eikon::SubjectMask
{
    namespace
    {
        namespace AI = winrt::Microsoft::Windows::AI;
        namespace Imaging = winrt::Microsoft::Windows::AI::Imaging;
        using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
        using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
        using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
        using winrt::Windows::Graphics::Imaging::SoftwareBitmap;

        void Log(std::wstring const& message)
        {
            std::wstring line{ L"[Eikon] cut out: " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        std::wstring StateName(AI::AIFeatureReadyState state)
        {
            switch (state)
            {
            case AI::AIFeatureReadyState::Ready: return L"Ready";
            case AI::AIFeatureReadyState::NotReady: return L"NotReady";
            case AI::AIFeatureReadyState::NotSupportedOnCurrentSystem: return L"NotSupportedOnCurrentSystem";
            case AI::AIFeatureReadyState::DisabledByUser: return L"DisabledByUser";
            case AI::AIFeatureReadyState::CapabilityMissing: return L"CapabilityMissing";
            case AI::AIFeatureReadyState::NotCompatibleWithSystemHardware: return L"NotCompatibleWithSystemHardware";
            case AI::AIFeatureReadyState::OSUpdateNeeded: return L"OSUpdateNeeded";
            default: return std::to_wstring(static_cast<int>(state));
            }
        }

        int FeatherRadius(Core::Bitmap const& image)
        {
            return std::max(1, static_cast<int>(std::lround(image.dpr)));
        }

        // ---- 1. Windows AI: ImageObjectExtractor ----

        std::optional<std::vector<std::uint8_t>> WindowsAiMask(Core::Bitmap const& image)
        {
            try
            {
                auto const state = Imaging::ImageObjectExtractor::GetReadyState();
                if (state == AI::AIFeatureReadyState::NotReady)
                {
                    // Supported but not installed yet: ask Windows to prepare it for the next Cut Out.
                    static std::once_flag once;
                    std::call_once(once, [] {
                        static auto preparing = Imaging::ImageObjectExtractor::EnsureReadyAsync();
                        Log(L"Windows AI ImageObjectExtractor is not ready; preparing it (EnsureReadyAsync)");
                    });
                }
                if (state != AI::AIFeatureReadyState::Ready)
                {
                    Log(L"Windows AI ImageObjectExtractor ready state " + StateName(state));
                    return std::nullopt;
                }
                int const w = image.width;
                int const h = image.height;
                SoftwareBitmap bitmap{ BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Premultiplied };
                winrt::Windows::Storage::Streams::Buffer buffer{ static_cast<std::uint32_t>(image.pixels.size()) };
                std::memcpy(buffer.data(), image.pixels.data(), image.pixels.size());
                buffer.Length(static_cast<std::uint32_t>(image.pixels.size()));
                bitmap.CopyFromBuffer(buffer);

                auto extractor = Imaging::ImageObjectExtractor::CreateWithSoftwareBitmapAsync(bitmap).get();
                // The user drew the selection around the subject: hint with the selection, inset a little.
                int const dx = std::max(1, w * 3 / 100);
                int const dy = std::max(1, h * 3 / 100);
                winrt::Windows::Graphics::RectInt32 const rect{ dx, dy, std::max(1, w - 2 * dx), std::max(1, h - 2 * dy) };
                Imaging::ImageObjectExtractorHint hint{ winrt::single_threaded_vector<winrt::Windows::Graphics::RectInt32>({ rect }),
                                                        winrt::single_threaded_vector<winrt::Windows::Graphics::PointInt32>(),
                                                        winrt::single_threaded_vector<winrt::Windows::Graphics::PointInt32>() };
                SoftwareBitmap mask = extractor.GetSoftwareBitmapObjectMask(hint);
                extractor.Close();
                if (!mask)
                {
                    return std::nullopt;
                }
                if (mask.BitmapPixelFormat() != BitmapPixelFormat::Gray8)
                {
                    mask = SoftwareBitmap::Convert(mask, BitmapPixelFormat::Gray8);
                }
                // Greyscale-8, 255 on the object: read it plane by plane (rows may be padded) and scale
                // to the image if the sizes differ.
                std::vector<std::uint8_t> alpha(static_cast<std::size_t>(w) * h);
                {
                    auto locked = mask.LockBuffer(BitmapBufferAccessMode::Read);
                    auto const plane = locked.GetPlaneDescription(0);
                    auto reference = locked.CreateReference();
                    std::uint8_t* data = nullptr;
                    std::uint32_t capacity = 0;
                    winrt::check_hresult(reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&data, &capacity));
                    for (int y = 0; y < h; ++y)
                    {
                        int const sy = std::min(plane.Height - 1, y * plane.Height / h);
                        for (int x = 0; x < w; ++x)
                        {
                            int const sx = std::min(plane.Width - 1, x * plane.Width / w);
                            std::size_t const at = static_cast<std::size_t>(plane.StartIndex) + static_cast<std::size_t>(sy) * plane.Stride + sx;
                            alpha[static_cast<std::size_t>(y) * w + x] = at < capacity ? data[at] : 0;
                        }
                    }
                }
                if (!Core::MaskHasSubject(alpha))
                {
                    Log(L"Windows AI found no subject");
                    return std::nullopt;
                }
                Core::FeatherMask(alpha, w, h, FeatherRadius(image));
                return alpha;
            }
            catch (winrt::hresult_error const& error)
            {
                Log(L"Windows AI ImageObjectExtractor failed: 0x" + std::to_wstring(static_cast<std::uint32_t>(error.code())) + L" " + std::wstring{ error.message() });
            }
            catch (...)
            {
                Log(L"Windows AI ImageObjectExtractor failed");
            }
            return std::nullopt;
        }

        // ---- 2. U²-Net-p on ONNX Runtime ----

        struct Model
        {
            Ort::Env env{ nullptr };
            Ort::Session session{ nullptr };
            std::string input;
            std::string output;
        };

        Model* LoadModel()
        {
            static std::mutex lock;
            static std::unique_ptr<Model> model;
            static bool failed = false;
            std::lock_guard guard{ lock };
            if (model || failed)
            {
                return model.get();
            }
            failed = true;
            OrtApiBase const* base = OrtGetApiBase();
            OrtApi const* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
            if (!api)
            {
                Log(L"no ONNX Runtime (Windows ML) in the Windows App SDK framework");
                return nullptr;
            }
            Ort::InitApi(api);
            HMODULE const module = ::GetModuleHandleW(nullptr);
            HRSRC const resource = ::FindResourceW(module, MAKEINTRESOURCEW(IDR_EIKON_SUBJECT_MODEL), MAKEINTRESOURCEW(10));   // RT_RCDATA
            HGLOBAL const loaded = resource ? ::LoadResource(module, resource) : nullptr;
            void const* bytes = loaded ? ::LockResource(loaded) : nullptr;
            DWORD const size = resource ? ::SizeofResource(module, resource) : 0;
            if (!bytes || size == 0)
            {
                Log(L"the subject model resource is missing");
                return nullptr;
            }
            try
            {
                auto loadedModel = std::make_unique<Model>();
                loadedModel->env = Ort::Env{ ORT_LOGGING_LEVEL_WARNING, "Eikon" };
                Ort::SessionOptions options;
                unsigned const cores = std::max(1u, std::thread::hardware_concurrency());
                options.SetIntraOpNumThreads(static_cast<int>(std::clamp(cores / 2, 1u, 4u)));
                options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                loadedModel->session = Ort::Session{ loadedModel->env, bytes, size, options };
                Ort::AllocatorWithDefaultOptions allocator;
                loadedModel->input = loadedModel->session.GetInputNameAllocated(0, allocator).get();
                loadedModel->output = loadedModel->session.GetOutputNameAllocated(0, allocator).get();
                model = std::move(loadedModel);
                failed = false;
            }
            catch (Ort::Exception const& error)
            {
                Log(L"loading the subject model failed: " + std::wstring{ winrt::to_hstring(error.what()) });
            }
            return model.get();
        }

        std::optional<std::vector<std::uint8_t>> ModelMask(Core::Bitmap const& image)
        {
            Model* model = LoadModel();
            if (!model)
            {
                return std::nullopt;
            }
            try
            {
                int constexpr size = Core::kSubjectModelSize;
                auto input = Core::SubjectModelInput(image, size);
                std::int64_t const shape[]{ 1, 3, size, size };
                auto const memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape, 4);
                char const* inputs[]{ model->input.c_str() };
                char const* outputs[]{ model->output.c_str() };
                auto results = model->session.Run(Ort::RunOptions{ nullptr }, inputs, &tensor, 1, outputs, 1);
                if (results.empty() || !results[0].IsTensor())
                {
                    return std::nullopt;
                }
                std::size_t const count = results[0].GetTensorTypeAndShapeInfo().GetElementCount();
                std::span<float const> const map{ results[0].GetTensorData<float>(), count };
                auto mask = Core::SubjectMaskFromModel(map, size, image.width, image.height, FeatherRadius(image));
                if (mask.empty())
                {
                    Log(L"U2-Net-p found no subject");
                    return std::nullopt;
                }
                return mask;
            }
            catch (Ort::Exception const& error)
            {
                Log(L"running the subject model failed: " + std::wstring{ winrt::to_hstring(error.what()) });
            }
            return std::nullopt;
        }
    }

    winrt::fire_and_forget CutOut(std::shared_ptr<Core::Bitmap const> image, std::function<void(Core::Bitmap)> done)
    {
        auto const queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        co_await winrt::resume_background();
        auto const started = std::chrono::steady_clock::now();
        Core::Bitmap result;
        std::wstring method;
        if (image->IsEmpty())
        {
            result = *image;
            method = L"empty";
        }
        else if (auto mask = WindowsAiMask(*image))
        {
            result = Core::ApplySubjectMask(*image, *mask);
            method = L"Windows AI ImageObjectExtractor";
        }
        else if (auto modelMask = ModelMask(*image))
        {
            result = Core::ApplySubjectMask(*image, *modelMask);
            method = L"U2-Net-p (ONNX Runtime, CPU)";
        }
        else
        {
            result = Core::CutOutBackground(*image);
            method = L"border colour";
        }
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        Log(method + L", " + std::to_wstring(image->width) + L"x" + std::to_wstring(image->height) + L" in " + std::to_wstring(elapsed) + L" ms");
        co_await wil::resume_foreground(queue);
        done(std::move(result));
    }
}
