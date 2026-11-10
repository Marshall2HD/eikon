#pragma once

#include "App.xaml.g.h"

namespace winrt::Eikon::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        // What HandleActivation needs, copied out of AppActivationArguments right away.
        struct Activation
        {
            Microsoft::Windows::AppLifecycle::ExtendedActivationKind kind{};
            std::wstring arguments;   // launch arguments (the lucida.exe alias passes the command line)
            std::wstring uri;         // protocol activation: the raw lucida:// URL
        };
        static Activation Snapshot(Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);

        // Activations redirected from a second launch (see Main.cpp). Raised on a background thread;
        // dispatched to the UI thread.
        void OnActivated(winrt::Windows::Foundation::IInspectable const&,
                         Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);
        // Protocol URLs (and URL arguments of the alias) go to the router. A plain second launch opens
        // lucida://show in the running copy. `initial` = first launch of the process.
        void HandleActivation(Activation const& activation, bool initial);

        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}
