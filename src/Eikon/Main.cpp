// Custom entry point (DISABLE_XAML_GENERATED_MAIN) so that Lucida stays single-instance: a
// second launch (Start menu, the lucida.exe alias, the startup task, a lucida://
// URL) is redirected to the running instance, which handles it in App::OnActivated.
#include "pch.h"
#include "App.xaml.h"

#include "Strings.h"

#include <thread>

using winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments;
using winrt::Microsoft::Windows::AppLifecycle::AppInstance;

namespace
{
    constexpr wchar_t InstanceKey[] = L"Eikon.Main";

    // RedirectActivationToAsync must not be awaited on the STA thread; run it on a worker thread.
    // Bounded wait: if the running instance quits while handling the activation, the operation can
    // stay pending forever and this process would never exit.
    void RedirectTo(AppInstance const& instance, AppActivationArguments const& args)
    {
        std::thread worker([&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try
            {
                // Lets the running copy bring its editor to the front.
                ::AllowSetForegroundWindow(static_cast<DWORD>(instance.ProcessId()));
                instance.RedirectActivationToAsync(args).wait_for(std::chrono::seconds{ 5 });
            }
            catch (...)
            {
            }
            winrt::uninit_apartment();
        });
        worker.join();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // Same as the XAML-generated main: fail fast with a clear message if the runtime is missing.
    if (HMODULE module = ::LoadLibraryW(L"Microsoft.UI.Xaml.dll"))
    {
        using CheckFn = void(WINAPI*)();
        if (auto check = reinterpret_cast<CheckFn>(::GetProcAddress(module, "XamlCheckProcessRequirements")))
        {
            check();
        }
        ::FreeLibrary(module);
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Eikon::Strings::Initialize();

    auto const args = AppInstance::GetCurrent().GetActivatedEventArgs();
    auto const keyInstance = AppInstance::FindOrRegisterForKey(InstanceKey);
    if (!keyInstance.IsCurrent())
    {
        RedirectTo(keyInstance, args);
        return 0;
    }

    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Eikon::implementation::App>();
    });
    return 0;
}
