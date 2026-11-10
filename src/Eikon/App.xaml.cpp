#include "pch.h"
#include "App.xaml.h"

#include "AppController.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::Windows::AppLifecycle;
}

namespace
{
    // A command-line argument that is itself a URL of ours ("eikon.exe eikon://grab/fullscreen").
    std::optional<std::wstring> UrlArgument(std::wstring const& arguments)
    {
        int count = 0;
        std::wstring const commandLine = L"eikon.exe " + arguments;
        LPWSTR* argv = ::CommandLineToArgvW(commandLine.c_str(), &count);
        if (!argv)
        {
            return std::nullopt;
        }
        std::optional<std::wstring> url;
        for (int i = 1; i < count && !url; ++i)
        {
            std::wstring_view const argument{ argv[i] };
            if (Eikon::Core::ParseUrlCommand(argument))
            {
                url = std::wstring{ argument };
            }
        }
        ::LocalFree(argv);
        return url;
    }
}

namespace winrt::Eikon::implementation
{
    // Read everything out of the activation arguments immediately. For a redirected activation they
    // are a proxy into the second eikon.exe, which exits once the redirect completes; touching them
    // later on the UI thread fails with RPC_S_CALL_FAILED.
    App::Activation App::Snapshot(AppActivationArguments const& args)
    {
        Activation activation;
        activation.kind = args.Kind();
        if (activation.kind == ExtendedActivationKind::Launch)
        {
            if (auto launch = args.Data().try_as<winrt::Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs>())
            {
                activation.arguments = launch.Arguments();
            }
        }
        else if (activation.kind == ExtendedActivationKind::Protocol)
        {
            if (auto protocol = args.Data().try_as<winrt::Windows::ApplicationModel::Activation::IProtocolActivatedEventArgs>())
            {
                // RawUri keeps the URL as typed; AbsoluteUri would normalise it ("eikon://show/").
                activation.uri = protocol.Uri().RawUri();
            }
        }
        return activation;
    }

    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
        // Eikon lives in the notification area: hiding the editor or closing Settings must not end the app.
        DispatcherShutdownMode(winrt::Microsoft::UI::Xaml::DispatcherShutdownMode::OnExplicitShutdown);
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        ::Eikon::AppController::Current().Start();

        auto instance = AppInstance::GetCurrent();
        instance.Activated({ this, &App::OnActivated });
        HandleActivation(Snapshot(instance.GetActivatedEventArgs()), true);
    }

    void App::OnActivated(IInspectable const&, AppActivationArguments const& args)
    {
        Activation activation;
        try
        {
            activation = Snapshot(args);
        }
        catch (...)
        {
            return; // the redirecting process is already gone; nothing to act on
        }
        m_dispatcher.TryEnqueue([this, activation = std::move(activation)] {
            try
            {
                HandleActivation(activation, false);
            }
            catch (...)
            {
                OutputDebugStringW(L"Eikon: exception while handling an activation\n");
            }
        });
    }

    void App::HandleActivation(Activation const& activation, bool initial)
    {
        auto& controller = ::Eikon::AppController::Current();

        if (activation.kind == ExtendedActivationKind::Protocol)
        {
            controller.HandleUrl(activation.uri);
            return;
        }
        if (auto const url = UrlArgument(activation.arguments))
        {
            controller.HandleUrl(*url);
            return;
        }
        if (!initial)
        {
            // A second copy opens eikon://show on the running one.
            controller.HandleUrl(L"eikon://show");
            return;
        }
        // Start hidden when showIntro != 1 and the Hotkeys tab was visited; a
        // login launch never shows the window once the Hotkeys tab was visited.
        auto& settings = ::Eikon::Core::Settings::Current();
        bool const everVisitedHotkeys = settings.EverVisitedHotkeys();
        bool const startHidden = (settings.ShowIntro() != 1 && everVisitedHotkeys) ||
                                 (activation.kind == ExtendedActivationKind::StartupTask && everVisitedHotkeys);
        if (!startHidden)
        {
            controller.ShowEditor(); // empty, so it shows the intro panel
        }
    }
}
