#include "pch.h"
#include "Editor/TextEditOverlay.h"

namespace Eikon::Editor
{
    namespace xaml = winrt::Microsoft::UI::Xaml;

    TextEditOverlay::TextEditOverlay(xaml::Controls::Canvas const& host) : m_host(host)
    {
    }

    TextEditOverlay::~TextEditOverlay()
    {
        m_finished = nullptr;
        End();
    }

    void TextEditOverlay::Begin(Core::ObjText& text, Render::Context const& context, std::function<void()> changed, std::function<void()> finished)
    {
        End();
        m_text = &text;
        m_context = context;
        m_changed = std::move(changed);
        m_finished = std::move(finished);
        text.editing = true;

        m_box = xaml::Controls::TextBox{};
        m_box.AcceptsReturn(true);
        m_box.TextWrapping(xaml::TextWrapping::Wrap);
        m_box.BorderThickness({ 0, 0, 0, 0 });
        m_box.Padding({ 0, 0, 0, 0 });
        m_box.MinWidth(0);
        m_box.MinHeight(0);
        m_box.Background(xaml::Media::SolidColorBrush{ winrt::Windows::UI::Colors::Transparent() });
        m_box.IsSpellCheckEnabled(false);
        // The TextBox's own focus visuals would read as a second box; the object draws its own.
        m_box.Resources().Insert(winrt::box_value(L"TextControlBackgroundFocused"), xaml::Media::SolidColorBrush{ winrt::Windows::UI::Colors::Transparent() });
        m_box.Resources().Insert(winrt::box_value(L"TextControlBackgroundPointerOver"), xaml::Media::SolidColorBrush{ winrt::Windows::UI::Colors::Transparent() });
        m_box.Resources().Insert(winrt::box_value(L"TextControlBorderThemeThicknessFocused"), winrt::box_value(xaml::Thickness{ 0, 0, 0, 0 }));
        m_box.Text(winrt::hstring{ text.text });
        Apply(context);

        m_textChanged = m_box.TextChanged([this](auto const&, auto const&) {
            if (!m_text)
            {
                return;
            }
            m_text->text = std::wstring{ m_box.Text() };
            m_text->Layout(m_context.dpr);
            Apply(m_context);
            if (m_changed) m_changed();
        });
        m_lostFocus = m_box.LostFocus([this](auto const&, auto const&) { End(); });
        m_keyDown = m_box.KeyDown([this](auto const&, xaml::Input::KeyRoutedEventArgs const& args) {
            if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
            {
                args.Handled(true);
                End();
            }
        });
        m_host.Children().Append(m_box);
        m_box.Focus(xaml::FocusState::Programmatic);
        m_box.SelectionStart(static_cast<std::int32_t>(m_box.Text().size()));
    }

    void TextEditOverlay::Reposition(Render::Context const& context)
    {
        m_context = context;
        if (m_text && m_box)
        {
            Apply(context);
        }
    }

    void TextEditOverlay::Apply(Render::Context const& context)
    {
        Core::ObjText const& t = *m_text;
        double const dpr = context.dpr;
        double const k = context.zoom / context.rasterScale;   // DIPs per image pixel
        Core::Rect const area = t.TextRect(dpr);
        auto const topLeft = context.ToCanvas({ area.x, area.y });
        m_box.FontFamily(xaml::Media::FontFamily{ winrt::hstring{ t.FontFamily() } });
        m_box.FontSize((std::max)(1.0, t.FontSize(dpr) * k));
        m_box.FontWeight({ static_cast<std::uint16_t>(t.style == Core::AnnotationStyle::Pointy && t.UsesMediumWeight() ? 500 : 400) });
        Core::Color const ink = t.TextColor();
        winrt::Windows::UI::Color const color{ 255, ink.r, ink.g, ink.b };
        m_box.Foreground(xaml::Media::SolidColorBrush{ color });
        m_box.Resources().Insert(winrt::box_value(L"TextControlForegroundFocused"), xaml::Media::SolidColorBrush{ color });
        // Wrap where the object wraps; the height follows the text.
        Core::Point const inset = t.Insets(dpr);
        double const wrap = (std::max)(t.maxWidth - 2 * inset.x, t.FontSize(dpr));
        m_box.Width((std::max)(area.width, wrap) * k + 2);
        m_box.Height((std::max)(area.height, t.LineHeight(dpr)) * k + 2);
        xaml::Controls::Canvas::SetLeft(m_box, topLeft.X);
        xaml::Controls::Canvas::SetTop(m_box, topLeft.Y);
    }

    void TextEditOverlay::End()
    {
        if (!m_box)
        {
            m_text = nullptr;
            return;
        }
        auto box = m_box;
        m_box = nullptr;
        box.TextChanged(m_textChanged);
        box.LostFocus(m_lostFocus);
        box.KeyDown(m_keyDown);
        if (m_text)
        {
            m_text->text = std::wstring{ box.Text() };
            m_text->editing = false;
            m_text->Layout(m_context.dpr);
        }
        uint32_t index = 0;
        if (m_host.Children().IndexOf(box, index))
        {
            m_host.Children().RemoveAt(index);
        }
        m_text = nullptr;
        if (auto finished = std::move(m_finished))
        {
            m_finished = nullptr;
            finished();
        }
    }
}
