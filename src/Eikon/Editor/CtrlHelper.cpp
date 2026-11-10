#include "pch.h"
#include "Editor/CtrlHelper.h"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace Eikon::Editor
{
    namespace xaml = winrt::Microsoft::UI::Xaml;
    namespace comp = winrt::Microsoft::UI::Composition;
    using winrt::Windows::Foundation::Numerics::float2;
    using winrt::Windows::Foundation::Numerics::float3;

    namespace
    {
        std::chrono::milliseconds Ms(double seconds)
        {
            return std::chrono::milliseconds(static_cast<std::int64_t>(seconds * 1000 + 0.5));
        }
    }

    CtrlHelper::CtrlHelper(xaml::FrameworkElement const& element, Options options) : m_element(element), m_options(options)
    {
        xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(m_element, true);
        m_delayTimer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_delayTimer.IsRepeating(false);
        m_delayTimer.Tick([this](auto&&, auto&&) {
            m_pending = false;
            StartShow();
        });
    }

    void CtrlHelper::Show()
    {
        if (!m_element)
        {
            return;
        }
        if (m_state == State::Showing || m_state == State::Visible)
        {
            return;
        }
        m_delayTimer.Stop();
        if (m_options.showDelay > 0)
        {
            m_pending = true;
            m_delayTimer.Interval(Ms(m_options.showDelay));
            m_delayTimer.Start();
            return;
        }
        StartShow();
    }

    void CtrlHelper::StartShow()
    {
        ++m_generation;
        m_element.Visibility(xaml::Visibility::Visible);
        auto visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(m_element);
        auto compositor = visual.Compositor();
        visual.StopAnimation(L"Opacity");
        visual.StopAnimation(L"Scale");
        visual.StopAnimation(L"Translation");
        float const target = static_cast<float>(m_options.targetOpacity);
        if (m_options.animation <= 0.01)
        {
            visual.Opacity(target);
            visual.Scale({ 1, 1, 1 });
            visual.Properties().InsertVector3(L"Translation", { 0, 0, m_options.elevation });
            Finish(State::Visible);
            return;
        }
        m_state = State::Showing;
        m_element.UpdateLayout();
        double height = m_element.ActualHeight();
        if (!(height > 0))
        {
            height = m_element.Height() > 0 ? m_element.Height() : 0;
        }
        double const width = m_element.ActualWidth();
        float const jump = static_cast<float>(m_options.jump);
        float const offset = static_cast<float>(m_options.jump * height * (m_options.jump + 1));
        // Anchored at the bottom edge, so the scale pulls toward it.
        visual.CenterPoint({ static_cast<float>(width / 2), static_cast<float>(height), 0 });

        auto const spring = compositor.CreateCubicBezierEasingFunction(float2{ 0.465f, 1.6f }, float2{ 0.69f, 1.0f });
        auto const easeOut = compositor.CreateCubicBezierEasingFunction(float2{ 0.0f, 0.0f }, float2{ 0.58f, 1.0f });
        auto const duration = Ms(m_options.animation);

        auto scale = compositor.CreateVector3KeyFrameAnimation();
        scale.InsertKeyFrame(0.0f, float3{ 1, 1 - jump, 1 });
        scale.InsertKeyFrame(1.0f, float3{ 1, 1, 1 }, spring);
        scale.Duration(duration);
        auto translation = compositor.CreateVector3KeyFrameAnimation();
        translation.InsertKeyFrame(0.0f, float3{ 0, offset, m_options.elevation });
        translation.InsertKeyFrame(1.0f, float3{ 0, 0, m_options.elevation }, spring);
        translation.Duration(duration);
        auto opacity = compositor.CreateScalarKeyFrameAnimation();
        opacity.InsertKeyFrame(0.0f, 0.0f);
        opacity.InsertKeyFrame(1.0f, target, easeOut);
        opacity.Duration(Ms(m_options.animation * 1.5));

        auto batch = compositor.CreateScopedBatch(comp::CompositionBatchTypes::Animation);
        visual.StartAnimation(L"Scale", scale);
        visual.StartAnimation(L"Translation", translation);
        batch.End();
        visual.StartAnimation(L"Opacity", opacity);
        unsigned const generation = m_generation;
        batch.Completed([this, generation](auto&&, auto&&) {
            if (generation == m_generation && m_state == State::Showing)
            {
                Finish(State::Visible);
            }
        });
    }

    void CtrlHelper::Hide()
    {
        if (!m_element)
        {
            return;
        }
        m_delayTimer.Stop();
        m_pending = false;
        if (m_state == State::Hidden || m_state == State::Hiding)
        {
            return;
        }
        ++m_generation;
        auto visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(m_element);
        double const duration = m_options.animation * 1.33;
        if (duration <= 0.01 || m_state == State::Showing)
        {
            // A helper still springing in is dropped at once.
            visual.StopAnimation(L"Opacity");
            visual.StopAnimation(L"Scale");
            visual.StopAnimation(L"Translation");
            m_element.Visibility(xaml::Visibility::Collapsed);
            Finish(State::Hidden);
            return;
        }
        m_state = State::Hiding;
        auto compositor = visual.Compositor();
        auto opacity = compositor.CreateScalarKeyFrameAnimation();
        opacity.InsertExpressionKeyFrame(0.0f, L"this.StartingValue");
        opacity.InsertKeyFrame(1.0f, 0.0f);
        opacity.Duration(Ms(duration));
        auto batch = compositor.CreateScopedBatch(comp::CompositionBatchTypes::Animation);
        visual.StartAnimation(L"Opacity", opacity);
        batch.End();
        unsigned const generation = m_generation;
        batch.Completed([this, generation](auto&&, auto&&) {
            if (generation == m_generation && m_state == State::Hiding)
            {
                m_element.Visibility(xaml::Visibility::Collapsed);
                Finish(State::Hidden);
            }
        });
    }

    void CtrlHelper::Toggle()
    {
        if (IsOpen())
        {
            Hide();
        }
        else
        {
            Show();
        }
    }

    void CtrlHelper::Finish(State state)
    {
        m_state = state;
        if (changed)
        {
            changed(state == State::Visible);
        }
    }
}
