#pragma once

// CtrlHelper (defaults, show, hide):
// a panel inside the window that springs in (scaleY from 1 − jump plus a downward offset of
// jump·h·(jump + 1), cubic-bezier (0.465, 1.6, 0.69, 1.0) over `animation` s, opacity 0 → target
// over 1.5 × `animation` ease-out) and fades out over 1.33 × `animation`. The look (corner radius,
// curtain, borders) is in the XAML; this class runs the state machine and the Composition animations
// on the element's handout visual.

#include <functional>

namespace Eikon::Editor
{
    class CtrlHelper
    {
    public:
        struct Options
        {
            double animation = 0.15;
            double jump = 0.1;
            double showDelay = 0;
            double targetOpacity = 1;
            bool transient = true;
            // Candy: the height (Translation Z) the card floats at, so its ThemeShadow shows; 0 = flat.
            float elevation = 0;
        };

        enum class State
        {
            Hidden,
            Showing,
            Visible,
            Hiding,
        };

        CtrlHelper(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, Options options);
        CtrlHelper(CtrlHelper const&) = delete;   // the timer and animation callbacks hold `this`
        CtrlHelper& operator=(CtrlHelper const&) = delete;

        void Show();
        void Hide();
        void Toggle();
        // Showing or visible (or waiting for its show delay).
        bool IsOpen() const noexcept { return m_pending || m_state == State::Showing || m_state == State::Visible; }
        bool IsTransient() const noexcept { return m_options.transient; }
        State CurrentState() const noexcept { return m_state; }
        winrt::Microsoft::UI::Xaml::FrameworkElement const& Element() const noexcept { return m_element; }

        // Called after the helper became visible or hidden.
        std::function<void(bool shown)> changed;

    private:
        void StartShow();
        void Finish(State state);

        winrt::Microsoft::UI::Xaml::FrameworkElement m_element{ nullptr };
        Options m_options;
        State m_state = State::Hidden;
        bool m_pending = false;           // waiting for showDelay
        unsigned m_generation = 0;        // ignores completions of superseded animations
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_delayTimer{ nullptr };
    };
}
