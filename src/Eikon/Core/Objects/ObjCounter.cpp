#include "pch.h"
#include "Core/Objects/ObjCounter.h"

#include "Core/Objects/ArrHelper.h"
#include "Core/Objects/ObjectSupport.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Eikon::Core
{
    using namespace Support;

    double ObjCounter::Angle() const noexcept
    {
        return std::atan2(rect.height, rect.width);
    }

    double ObjCounter::Radius(double dpr) const
    {
        return kDiameters[std::clamp(size, 0, 9)] / 2.0 * dpr;
    }

    double ObjCounter::FontSize(double dpr) const
    {
        return Radius(dpr) * 1.1 * (count < 10 ? 1.0 : 0.9);
    }

    Point ObjCounter::TextCentre(double dpr) const
    {
        double const k = dpr * kDiameters[std::clamp(size, 0, 9)] / 28.0;
        return { rect.x + 0.3 * k, rect.y - 0.5 * k };
    }

    Color ObjCounter::TextColor() const
    {
        return Luminance(DrawColor()) <= 0.6 ? Color{ 255, 255, 255, 255 } : Color{ 0, 0, 0, 255 };
    }

    bool ObjCounter::UsesMediumWeight() const
    {
        return TextColor().r == 255;
    }

    void ObjCounter::SetAngle(double angle, double length)
    {
        rect.width = std::cos(angle) * length;
        rect.height = std::sin(angle) * length;
    }

    JsonValue ObjCounter::Dump() const
    {
        JsonValue json = DumpBase();
        SetNumber(json, L"style", style);
        SetNumber(json, L"count", count);
        return json;
    }

    bool ObjCounter::Load(JsonValue const& json)
    {
        auto s = GetNumber(json, L"style"), c = GetNumber(json, L"count");
        if (!s || !c || !LoadBase(json))
        {
            return false;
        }
        style = static_cast<std::int32_t>(*s);
        count = static_cast<std::int32_t>(*c);
        prevDigitHit = false;
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjCounter::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        m_settings = &settings;
        style = static_cast<std::int32_t>(std::clamp<std::int64_t>(settings.ReadInt(PreferenceKey(L"counterStyle"), 1), 0, 2));
        double const up = settings.ReadDouble(PreferenceKey(L"counterAngle"), kDefaultAngle);
        SetAngle(-(std::isfinite(up) ? up : kDefaultAngle), 1.0);
        count = 1;
        prevDigitHit = false;
    }

    void ObjCounter::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"changeStyle")
        {
            style = (style + 1) % 3;
            SavePreference(settings, L"counterStyle", std::to_wstring(style));
            return;
        }
        if (name == L"style")
        {
            if (value == L"0" || value == L"1" || value == L"2")
            {
                style = value[0] - L'0';
                SavePreference(settings, L"counterStyle", std::to_wstring(style));
            }
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjCounter::Property(std::wstring_view name) const
    {
        if (name == L"style") return std::to_wstring(style);
        return ProtoObject::Property(name);
    }

    std::vector<Handle> ObjCounter::Handles() const
    {
        if (style == 0)
        {
            return {};
        }
        return { { L"point-rotate", { rect.x + rect.width, rect.y + rect.height }, L"cursor-move" } };
    }

    std::wstring ObjCounter::HitTest(Point point, double zoom) const
    {
        double const z = zoom > 0 ? zoom : 1.0;
        if (IsSelected())
        {
            for (auto const& h : Handles())
            {
                if (Distance(h.position, point) <= 8.0 / z)
                {
                    return h.name;
                }
            }
        }
        double const r = Radius(Environment().dpr);
        if (Distance(point, Centre()) <= r)
        {
            return L"move";
        }
        if (style != 0 && DistanceToSegment(point, Centre(), { rect.x + rect.width, rect.y + rect.height }) <= r * 0.4)
        {
            return L"move";
        }
        return L"";
    }

    KeyResult ObjCounter::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        KeyResult const base = BaseKeyDown(key, context);
        if (base != KeyResult::Ignored)
        {
            return base;
        }
        int const digit = Keys::Digit(key.key);
        if (digit < 0 || HasFlag(key.modifiers, Modifiers::Ctrl) || !IsSelected())
        {
            return KeyResult::Ignored;
        }
        count = prevDigitHit && count < 100000 ? count * 10 + digit : digit;
        prevDigitHit = true;
        return KeyResult::Handled;
    }

    void ObjCounter::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (IsCreating())
        {
            double const dpr = Environment().dpr;
            double const angle = Angle();
            double const r = Radius(dpr);
            if (style != 0)
            {
                // The click is the pin tip.
                rect.x = point.x - std::cos(angle) * r * 1.65;
                rect.y = point.y - std::sin(angle) * r * 1.65;
            }
            else
            {
                rect.x = point.x;
                rect.y = point.y;
            }
            SetAngle(angle, r * 1.5);
            // The number is taken when the counter is placed (not for the editor's template object).
            auto const& next = Environment().nextCounter;
            count = next ? next() : count;
        }
        prevDigitHit = IsCreating();
    }

    void ObjCounter::MouseDragged(Point point, Modifiers modifiers)
    {
        std::wstring_view const s = state;
        if (s == ObjectState::Move)
        {
            double dx = point.x - mouseDown.x;
            double dy = point.y - mouseDown.y;
            if (HasFlag(modifiers, Modifiers::Shift))
            {
                (std::fabs(dx) < std::fabs(dy) ? dx : dy) = 0;
            }
            rect = mouseDownRect.Offset(dx, dy);
            prevDigitHit = false;
        }
        else if (s == L"point-rotate")
        {
            double angle = std::atan2(point.y - rect.y, point.x - rect.x);
            if (HasFlag(modifiers, Modifiers::Shift))
            {
                angle = std::round(angle / (kPi / 4)) * (kPi / 4);
            }
            SetAngle(angle, Radius(Environment().dpr) * 1.5);
            Settings& settings = m_settings ? *m_settings : Settings::Current();
            SavePreference(settings, L"counterAngle", std::to_wstring(-angle));
            prevDigitHit = false;
        }
    }

    void ObjCounter::MouseUp(Point point, Modifiers modifiers)
    {
        if (!IsCreating())
        {
            MouseDragged(point, modifiers);
        }
    }

    std::vector<Shape> ObjCounter::Shapes(double dpr, double zoom) const
    {
        double const r = Radius(dpr);
        Shape body;
        if (style == 0)
        {
            body.path.MoveTo({ rect.x + r, rect.y });
            body.path.ArcAround(Centre(), r, 0, 2 * kPi);
            body.path.Close();
        }
        else
        {
            body.path = ArrHelper::PinPath(style - 1, Centre(), r, Angle());
        }
        body.fill = true;
        body.fillPaint = { color, 1.0 };
        std::vector<Shape> shapes{ body };
        if (IsHighlighted())
        {
            Shape ring = body;
            ring.fill = false;
            ring.stroke = true;
            ring.strokePaint = { kHoverBlue, 1.0 };
            ring.strokeWidth = 3.0 * dpr / std::sqrt(zoom > 0 ? zoom : 1.0);
            ring.join = Shape::Join::Round;
            shapes.push_back(std::move(ring));
        }
        return shapes;
    }
}
