#pragma once

// The text-editing overlay of ObjText: a transparent TextBox laid exactly over the text box's glyph area,
// with the object's font, size, colour and wrapping width, so typing looks in-place.
// The editor owns the host Canvas (a XAML Canvas above the CanvasControl) and decides when
// editing starts (a new text object, or a double click). UI thread only.

#include "Core/Objects/ObjText.h"
#include "Editor/Render.h"

#include <functional>

namespace Eikon::Editor
{
    class TextEditOverlay
    {
    public:
        explicit TextEditOverlay(winrt::Microsoft::UI::Xaml::Controls::Canvas const& host);
        ~TextEditOverlay();
        TextEditOverlay(TextEditOverlay const&) = delete;
        TextEditOverlay& operator=(TextEditOverlay const&) = delete;

        // Starts editing `text` (sets editing, focuses the box). `changed` runs after every edit (the
        // object is re-laid out first: redraw the canvas); `finished` once editing ends (Esc, focus
        // lost or End()). Ends any edit in progress first.
        void Begin(Core::ObjText& text, Render::Context const& context, std::function<void()> changed, std::function<void()> finished);
        // Follows zoom and pan changes.
        void Reposition(Render::Context const& context);
        // Commits the text and removes the box.
        void End();
        bool IsEditing() const noexcept { return m_text != nullptr; }
        Core::ObjText* Editing() const noexcept { return m_text; }

    private:
        void Apply(Render::Context const& context);

        winrt::Microsoft::UI::Xaml::Controls::Canvas m_host{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_box{ nullptr };
        winrt::event_token m_textChanged{};
        winrt::event_token m_lostFocus{};
        winrt::event_token m_keyDown{};
        Core::ObjText* m_text = nullptr;
        Render::Context m_context;
        std::function<void()> m_changed;
        std::function<void()> m_finished;
    };
}
