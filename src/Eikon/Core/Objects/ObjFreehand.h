#pragma once

// ObjFreehand and ObjHighlighter: a pen stroke outlined into a filled path by
// FreehandHelper. The raw points live in the points store under `pointsHash` (not in the Dump), in the
// coordinates of `rawRect`; `rect` is where they are shown now (resizing maps one onto the other).

#include "Core/Objects/FreehandHelper.h"
#include "Core/Objects/ProtoObject.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>

namespace Eikon::Core
{
    // Raw stroke points keyed by pointsHash.
    std::map<std::wstring, std::shared_ptr<std::vector<Point> const>>& PointsStore();

    class ObjFreehand : public ProtoObject
    {
    public:
        static constexpr std::array<double, 10> kWidths{ 1.5, 2.5, 3.8, 5.6, 8.4, 12.7, 19, 28.5, 42.7, 64 };

        std::wstring_view ClassName() const override { return L"ObjFreehand"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override { return BoxHandles(rect); }
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void MouseUp(Point point, Modifiers modifiers) override;
        bool IsEmpty() const override { return Points().empty(); }
        void ApplyDefaults(Settings& settings) override;
        void MoveBy(double dx, double dy) override;
        // "correction": "s" / "c" add a correction pass (strength 1 − 2^−n), anything
        // else replaces the list; the getter gives the last letter or "x" for none.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        double variability = 0.25;
        double smoothing = 0.6;
        bool realtimeShape = false;
        std::int32_t capStyle = 0;
        double resolution = 0;          // zoom / dpr when drawn
        Rect rawRect{ 0, 0, 1, 1 };
        std::int32_t lockDirection = 0; // Shift while drawing: 1 horizontal, 2 vertical
        std::wstring shapeCorrection;
        std::wstring pointsHash;

        // The raw points (from the store once saved, else the stroke being drawn).
        std::vector<Point> const& Points() const;
        // The outline, cached; `centreLine` receives the final samples.
        Path const& Outline(double dpr, Freehand::Samples* centreLine = nullptr) const;
        virtual double WidthFor(int index) const { return kWidths[std::clamp(index, 0, 9)]; }

    protected:
        std::vector<Point> m_drawing;
        Point m_lastPoint;

    private:
        mutable Path m_cache;
        mutable Freehand::Samples m_cacheLine;
        mutable std::wstring m_stamp;
    };

    // How a highlighter stroke is composited over the screenshot.
    struct HighlighterLook
    {
        enum class Blend { Normal, Multiply, Screen };
        Blend blend = Blend::Multiply;
        double opacity = 1.0;
        Color color;
    };

    class ObjHighlighter final : public ObjFreehand
    {
    public:
        // Eight widths for the eight steps of the highlighter's size slider.
        static constexpr std::array<double, 8> kHighlighterWidths{ 14, 20, 28, 38, 54, 76, 108, 154 };

        std::wstring_view ClassName() const override { return L"ObjHighlighter"; }
        void ApplyDefaults(Settings& settings) override;
        double WidthFor(int index) const override { return kHighlighterWidths[std::clamp(index, 0, 7)]; }
        // "color" is remembered per class (pref "ObjHighlighter: color", default FFF68D).
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        // Luminance of `image` under the centre line (at least 4 samples), sorted, middle half averaged:
        // > 0.66 multiply at 1 with the base colour; > 0.33 normal at 0.25 saturated; else screen at 0.33
        // saturated. With fewer samples: multiply.
        HighlighterLook Look(Bitmap const* image, double dpr) const;
        static HighlighterLook LookForLuminance(double mean, Color base);
    };
}
