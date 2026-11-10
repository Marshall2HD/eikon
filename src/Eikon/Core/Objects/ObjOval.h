#pragma once

// ObjOval and its subclasses: ObjRectangle (shape "rect"), ObjSpotlight (a
// cut-out of the shared curtain) and ObjMagnifier (a zoomed lens of its source area). `rect` is the
// box in image pixels.

#include "Core/Objects/ProtoObject.h"

#include <array>
#include <optional>

namespace Eikon::Core
{
    class ObjOval : public ProtoObject
    {
    public:
        // ObjOval init (0xb0).
        static constexpr std::array<double, 10> kWidths{ 1, 2, 3, 4, 6, 8, 11, 15, 21, 30 };

        std::wstring_view ClassName() const override { return L"ObjOval"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "fill": a number string; alpha = n/100 (0.01 if unparseable), fill on when > 0,
        // border on, hand-drawn when n < 0 (saved as the handdrawn preference). Getter: "-1" hand-drawn,
        // "100" alpha 1, "40" any other fill, "0" none.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        bool showBorder = true;
        bool showBackground = false;
        double backgroundAlpha = 0.3;
        std::wstring shape = L"oval";      // "oval" / "rect"
        bool handdrawn = true;             // pref "handdrawn", default true; not saved in the Dump
        std::uint64_t seed = 0;            // not saved: a reopened shape gets a new one

        double WidthAt(int index) const;
        // stroke = min(minSide/10, widths[size]) × dpr.
        double StrokeWidth(double dpr) const;
        // The outline path at `box` (rounded rect with radius min(minSide/3, 2·widths[size]·dpr), 12·dpr
        // for a zero width; or squarish oval).
        Path OutlinePath(Rect const& box, double dpr, double zoom) const;
        std::vector<double> const& Rnd() const;

    protected:
        // Overridable per-class widths (spotlight: 0, 2, 3, ...).
        virtual std::array<double, 10> const& Widths() const { return kWidths; }
        virtual bool AllowsHanddrawn() const { return true; }
        void ToggleKeys(KeyEvent const& key, KeyResult& result);

    private:
        mutable std::vector<double> m_rnd;
        mutable std::uint64_t m_rndSeed = 0;
    };

    // ObjRectangle: an ObjOval with shape "rect" and no fields of its own.
    class ObjRectangle final : public ObjOval
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjRectangle"; }
        void ApplyDefaults(Settings& settings) override;
    };

    // ObjSpotlight: dims everything outside its shape with the shared curtain.
    class ObjSpotlight final : public ObjOval
    {
    public:
        static constexpr std::array<double, 10> kSpotlightWidths{ 0, 2, 3, 4, 6, 8, 11, 15, 21, 30 };
        static constexpr double kDefaultTransparency = 0.214;

        std::wstring_view ClassName() const override { return L"ObjSpotlight"; }
        void ApplyDefaults(Settings& settings) override;
        // Oval keys, then border on / fill off, and digits 1–9 set the curtain to n/14 (saved as
        // `transparency`).
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        // "shape": "rect" / "oval" (empty toggles), saved as the shape preference.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        void MouseUp(Point point, Modifiers modifiers) override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;
        // Black curtain alpha: pref "ObjSpotlight: transparency", default 0.214.
        static double Transparency(Settings& settings);

    protected:
        std::array<double, 10> const& Widths() const override { return kSpotlightWidths; }
        bool AllowsHanddrawn() const override { return false; }
    };

    // ObjMagnifier: `rect` is the source area; the lens is `factor` times larger, offset by
    // (lensX·w, lensY·h) from the source centre.
    class ObjMagnifier final : public ObjOval
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjMagnifier"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "shape": "rect" or anything else → "oval", saved.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        // Source border, lens border and the connecting line (the lens pixels are drawn by the renderer).
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        double factor = 3;
        double lensX = 2.2;
        double lensY = -1.4;

        Rect LensRect() const;

    private:
        Rect m_downRect;
        double m_downLensX = 0;
        double m_downLensY = 0;
    };
}
