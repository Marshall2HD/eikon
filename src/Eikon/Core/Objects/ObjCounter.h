#pragma once

// ObjCounter: a numbered circle or pin. `rect.origin` is the circle centre and
// `rect.size` the direction vector from the centre to the pin tip (its angle is the pin angle).

#include "Core/Objects/ProtoObject.h"

#include <array>

namespace Eikon::Core
{
    class ObjCounter final : public ProtoObject
    {
    public:
        static constexpr std::array<double, 10> kDiameters{ 22, 28, 36, 48, 64, 84, 112, 150, 200, 266 };
        static constexpr double kDefaultAngle = -2.356194490192345;   // counterAngle

        std::wstring_view ClassName() const override { return L"ObjCounter"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        // Digits typed right after placing change the number (prevDigitHit).
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void MouseUp(Point point, Modifiers modifiers) override;
        bool IsEmpty() const override { return false; }
        void ApplyDefaults(Settings& settings) override;
        // "changeStyle": style = (style + 1) mod 3, saved as counterStyle; "style": 0–2.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        // The disc or pin (the number is drawn by the renderer) and the hover ring.
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        std::int32_t style = 1;   // 0 circle, 1 and 2 the two pins
        std::int32_t count = 1;
        bool prevDigitHit = false;

        Point Centre() const noexcept { return rect.Origin(); }
        double Angle() const noexcept;                       // radians, y down
        double Radius(double dpr) const;                     // d/2 × dpr
        // Number font: d/2 × dpr × 1.1, × 0.9 from 10 on.
        double FontSize(double dpr) const;
        // Where the number's centre goes: the centre offset by (+0.3 k, −0.5 k), k = dpr·d/28.
        Point TextCentre(double dpr) const;
        Color TextColor() const;
        bool UsesMediumWeight() const;

    private:
        void SetAngle(double angle, double length);
        Settings* m_settings = nullptr;
    };
}
