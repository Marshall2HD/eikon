#pragma once

// ObjText with AnnotationStyles: a text box with an optional callout pointer,
// or bare text (nobg / goofy) with a contrast halo. `rect` is the box in image pixels; Layout() sizes
// it from the text (Environment().measureText, DirectWrite in the editor).

#include "Core/Objects/ProtoObject.h"

#include <algorithm>
#include <array>
#include <optional>

namespace Eikon::Core
{
    enum class AnnotationStyle : std::int32_t
    {
        Pointy = 0,
        Goofy = 1,
    };

    class ObjText final : public ProtoObject
    {
    public:
        // ObjText 0xb0.
        static constexpr std::array<double, 10> kFontSizes{ 12, 14, 17, 20, 28, 40, 55, 77, 110, 150 };

        std::wstring_view ClassName() const override { return L"ObjText"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void MouseUp(Point point, Modifiers modifiers) override;
        bool IsEmpty() const override { return false; }   // a click places a text box
        void ApplyDefaults(Settings& settings) override;
        // "style": 1 = goofy (forces nobg), else pointy; saved. "nobg": "true"/"false".
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        // Box and pointer (none with nobg); the text and halo are drawn by the renderer.
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        std::wstring text;
        double maxWidth = 300;          // wrapping width of the box in pixels (fontsize × 8 + 188, × dpr)
        bool nobg = false;
        bool sharpTip = false;
        AnnotationStyle style = AnnotationStyle::Pointy;
        double pPerc = 0.38;            // pointer position along its side
        double pDist = 30;              // pointer length (pixels)
        std::wstring pSide = L"b";      // "t", "b", "l", "r"
        bool editing = false;           // textMode "editor": the box is drawn at 0.3 alpha
        double mouseDownTime = 0;       // seconds (steady clock) of the last press: double-click-to-edit

        // True once after a press that should open the text editor: a new text object, or a second
        // press on a selected one within 0.5 s *(inferred from mouseDownTime)*. The editor then starts
        // Editor::TextEditOverlay.
        bool TakeEditRequest() noexcept { bool const r = m_editRequested; m_editRequested = false; return r; }

        // ---- Typography ----
        // Font size in pixels: fontsizes[size] × dpr (× 1.2 for goofy).
        double FontSize(double dpr) const;
        double BaseFontSize() const { return kFontSizes[std::clamp(size, 0, 9)]; }
        // Line height in pixels: round(fs × 1.15), goofy fs × 1.324.
        double LineHeight(double dpr) const;
        // Horizontal / vertical text insets in pixels (rounded): nobg h = fs·0.03 + 4, v = fs·0.11; else
        // f·fs·0.417 and f·fs·0.25 with f = 1, 1.25, 1.5, 1.75, 2 from 41, 81, 121, 161 characters.
        Point Insets(double dpr) const;
        // The colour of the glyphs: on a box white if luminance(color) ≤ 0.6 else black; nobg: `color`.
        Color TextColor() const;
        // Medium for white text, Regular for black.
        bool UsesMediumWeight() const;
        // The typeface: Segoe UI Variable or the handwriting face.
        std::wstring_view FontFamily() const;
        // Sizes `rect` from the text: measured size + insets, at least one line tall.
        void Layout(double dpr);
        Rect TextRect(double dpr) const;   // where the glyphs go inside the box

        // ---- Pointer (OTHelper, approximation) ----
        bool HasPointer() const { return !nobg && pDist > 0; }
        Point PointerTip() const;
        Path PointerPath(double dpr) const;
        Path BoxPath(double dpr) const;

    private:
        Rect m_downRect;
        double m_downMaxWidth = 0;
        bool m_editRequested = false;
    };

    // WCAG contrast ratio of two colours: (L1 + 0.05)/(L2 + 0.05) of the relative
    // luminances, truncated to 0.1 (≥ 1.5) or 0.01.
    double ContrastRatio(Color a, Color b);
    // detectBG(rect, maxSamples: 400): a grid of about 400 pixels inside `rect`;
    // nullopt (clear) when more than half are translucent (alpha < 250), else the most frequent colour.
    std::optional<Color> DetectBackground(Bitmap const& bitmap, Rect const& rect, int maxSamples = 400);
}
