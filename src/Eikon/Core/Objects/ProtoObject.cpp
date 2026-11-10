#include "pch.h"
#include "Core/Objects/ProtoObject.h"

#include "Core/Objects/Path.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>

namespace Eikon::Core
{
    namespace
    {
        double Number(JsonValue const& object, std::wstring_view key, bool& ok)
        {
            auto const* value = object.Find(key);
            if (!value || !value->IsNumber())
            {
                ok = false;
                return 0;
            }
            return value->AsNumber();
        }

        // Knob hit radius: about the 9 × 9 pt knob plus a little slop, in image pixels.
        double KnobRadius(double zoom)
        {
            return 8.0 / (zoom > 0 ? zoom : 1.0);
        }
    }

    ObjectEnvironment& Environment()
    {
        static ObjectEnvironment environment;
        return environment;
    }

    std::vector<Handle> ProtoObject::HandlesAt(double, double) const
    {
        return Handles();
    }

    Color ProtoObject::DrawColor() const noexcept
    {
        return IsHighlighted() ? kHoverBlue : color;
    }

    void ProtoObject::ApplyDefaults(Settings& settings)
    {
        state = std::wstring{ ObjectState::Creating };
        mouseDownRect = Rect{ 0, 0, 100, 100 };
        color = Color::FromHex(settings.DefaultColor()).value_or(Color{ 0xFF, 0x0C, 0x01, 0xFF });
        size = static_cast<std::int32_t>(settings.ReadInt(PreferenceKey(L"size"), 2));
    }

    std::wstring ProtoObject::PreferenceKey(std::wstring_view property) const
    {
        std::wstring key = L"Eikon.";
        key += ClassName();
        key += L": ";
        key += property;
        return key;
    }

    void ProtoObject::SavePreference(Settings& settings, std::wstring_view property, std::wstring_view value) const
    {
        settings.WriteString(PreferenceKey(property), value);
    }

    void ProtoObject::KeepInside(Rect const& bounds)
    {
        if (bounds.width <= 0 || bounds.height <= 0)
        {
            return;
        }
        Rect const s = rect.Standardized();
        double dx = 0;
        double dy = 0;
        if (s.Right() > bounds.Right()) dx = bounds.Right() - s.Right();
        if (s.Left() + dx < bounds.Left()) dx = bounds.Left() - s.Left();
        if (s.Bottom() > bounds.Bottom()) dy = bounds.Bottom() - s.Bottom();
        if (s.Top() + dy < bounds.Top()) dy = bounds.Top() - s.Top();
        MoveBy(dx, dy);
    }

    void ProtoObject::ClipInside(Rect const& bounds)
    {
        Rect const clipped = rect.Standardized().Intersection(bounds);
        rect = clipped;
    }

    JsonValue ProtoObject::DumpBase() const
    {
        JsonValue json = JsonValue::Object();
        JsonValue r = JsonValue::Object();
        r.Set(L"x1", JsonValue::Number(rect.x));
        r.Set(L"y1", JsonValue::Number(rect.y));
        r.Set(L"width", JsonValue::Number(rect.width));
        r.Set(L"height", JsonValue::Number(rect.height));
        json.Set(L"rect", std::move(r));
        json.Set(L"size", JsonValue::Number(size));
        json.Set(L"color", JsonValue::String(color.ToHex()));
        return json;
    }

    bool ProtoObject::LoadBase(JsonValue const& json)
    {
        auto const* r = json.Find(L"rect");
        auto const* s = json.Find(L"size");
        auto const* c = json.Find(L"color");
        if (!r || !r->IsObject() || !s || !s->IsNumber() || !c || !c->IsString())
        {
            return false;
        }
        bool ok = true;
        Rect const loaded{ Number(*r, L"x1", ok), Number(*r, L"y1", ok), Number(*r, L"width", ok), Number(*r, L"height", ok) };
        auto const parsedColor = Color::FromHex(c->AsString());
        if (!ok || !parsedColor)
        {
            return false;
        }
        rect = loaded;
        size = static_cast<std::int32_t>(frintaInt(s->AsNumber()));
        color = *parsedColor;
        return true;
    }

    KeyResult ProtoObject::BaseKeyDown(KeyEvent const& key, KeyContext const& context)
    {
        if (key.key == Keys::Escape)
        {
            return KeyResult::Deselect;
        }
        if (state != ObjectState::Selected)
        {
            return KeyResult::Ignored;
        }
        if (key.key != Keys::Left && key.key != Keys::Right && key.key != Keys::Up && key.key != Keys::Down)
        {
            return KeyResult::Ignored;
        }
        double step = context.realPixels ? 1.0 : (context.dpr > 0 ? context.dpr : 1.0);
        if (HasFlag(key.modifiers, Modifiers::Shift))
        {
            step *= 10;
        }
        if (HasFlag(key.modifiers, Modifiers::Ctrl))
        {
            Rect const before = rect;
            switch (key.key)
            {
            case Keys::Left: rect.width -= step; break;
            case Keys::Right: rect.width += step; break;
            case Keys::Up: rect.height -= step; break;
            case Keys::Down: rect.height += step; break;
            default: break;
            }
            if (rect.width <= 0 && rect.height <= 0)
            {
                rect = before;
            }
            return KeyResult::Handled;
        }
        switch (key.key)
        {
        case Keys::Left: MoveBy(-step, 0); break;
        case Keys::Right: MoveBy(step, 0); break;
        case Keys::Up: MoveBy(0, -step); break;
        case Keys::Down: MoveBy(0, step); break;
        default: break;
        }
        return KeyResult::Handled;
    }

    // Creating sets the origin on mouse-down and the size on drag; a drag
    // inside a selected object moves it. Shift keeps only the dominant axis. Subclasses with handles
    // or point lists override these.
    void ProtoObject::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (state == ObjectState::Creating)
        {
            rect = { point.x, point.y, 0, 0 };
        }
    }

    void ProtoObject::MouseDragged(Point point, Modifiers modifiers)
    {
        double dx = point.x - mouseDown.x;
        double dy = point.y - mouseDown.y;
        if (HasFlag(modifiers, Modifiers::Shift))
        {
            (std::abs(dx) < std::abs(dy) ? dx : dy) = 0;
        }
        if (state == ObjectState::Creating)
        {
            rect = { mouseDown.x, mouseDown.y, dx, dy };
        }
        else if (state == ObjectState::Move)
        {
            rect = mouseDownRect.Offset(dx, dy);
        }
    }

    void ProtoObject::MouseUp(Point point, Modifiers modifiers)
    {
        MouseDragged(point, modifiers);
    }

    void ProtoObject::DragBox(Point point, Modifiers modifiers)
    {
        bool const shift = HasFlag(modifiers, Modifiers::Shift);
        double const px = std::round(point.x);
        double const py = std::round(point.y);
        if (state == ObjectState::Creating)
        {
            rect.width = px - rect.x;
            rect.height = py - rect.y;
            if (shift)
            {
                double const w = std::fabs(rect.width);
                double const h = std::fabs(rect.height);
                if (w <= h)
                {
                    rect.width = rect.width <= 0 ? -h : h;
                }
                else
                {
                    rect.height = rect.height <= 0 ? -w : w;
                }
            }
            return;
        }
        double dx = std::round(point.x - mouseDown.x);
        double dy = std::round(point.y - mouseDown.y);
        Rect r = mouseDownRect.Standardized();
        if (state == ObjectState::Move)
        {
            if (shift)
            {
                (std::fabs(dx) <= std::fabs(dy) ? dx : dy) = 0;
            }
            rect = mouseDownRect.Offset(dx, dy);
            return;
        }
        std::wstring_view const s = state;
        double left = r.Left(), top = r.Top(), right = r.Right(), bottom = r.Bottom();
        if (s == L"tl-resize" || s == L"l-resize" || s == L"bl-resize") left += dx;
        if (s == L"tr-resize" || s == L"r-resize" || s == L"br-resize") right += dx;
        if (s == L"tl-resize" || s == L"t-resize" || s == L"tr-resize") top += dy;
        if (s == L"bl-resize" || s == L"b-resize" || s == L"br-resize") bottom += dy;
        if (s.find(L"resize") == std::wstring_view::npos)
        {
            return;
        }
        rect = { left, top, right - left, bottom - top };
    }

    std::vector<Handle> ProtoObject::BoxHandles(Rect const& box)
    {
        Rect const b = box.Standardized();
        double const cx = b.x + b.width / 2;
        double const cy = b.y + b.height / 2;
        return {
            { L"tl-resize", { b.Left(), b.Top() }, L"cursor-tl-resize" },
            { L"t-resize", { cx, b.Top() }, L"cursor-t-resize" },
            { L"tr-resize", { b.Right(), b.Top() }, L"cursor-bl-resize" },
            { L"r-resize", { b.Right(), cy }, L"cursor-l-resize" },
            { L"br-resize", { b.Right(), b.Bottom() }, L"cursor-tl-resize" },
            { L"b-resize", { cx, b.Bottom() }, L"cursor-t-resize" },
            { L"bl-resize", { b.Left(), b.Bottom() }, L"cursor-bl-resize" },
            { L"l-resize", { b.Left(), cy }, L"cursor-l-resize" },
        };
    }

    std::wstring ProtoObject::HitBox(Rect const& box, Point point, double zoom, bool includeInside)
    {
        double const r = KnobRadius(zoom);
        for (auto const& h : BoxHandles(box))
        {
            if (std::fabs(h.position.x - point.x) <= r && std::fabs(h.position.y - point.y) <= r)
            {
                return h.name;
            }
        }
        Rect const b = box.Standardized();
        if (includeInside && point.x >= b.Left() - r && point.x <= b.Right() + r && point.y >= b.Top() - r && point.y <= b.Bottom() + r)
        {
            return L"move";
        }
        return L"";
    }

    namespace Keys
    {
        int Digit(std::uint32_t key) noexcept
        {
            if (key >= '0' && key <= '9')
            {
                return static_cast<int>(key - '0');
            }
            if (key >= 0x60 && key <= 0x69) // VK_NUMPAD0..9
            {
                return static_cast<int>(key - 0x60);
            }
            return -1;
        }
    }

    double Luminance(Color c) noexcept
    {
        return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
    }

    Color Saturate(Color c) noexcept
    {
        double const r = c.r / 255.0;
        double const g = c.g / 255.0;
        double const b = c.b / 255.0;
        double const mx = (std::max)({ r, g, b });
        double const mn = (std::min)({ r, g, b });
        double const range = (std::max)(mx - mn, 0.01);
        auto channel = [&](double v) -> std::uint8_t {
            double out;
            if (v == mx) out = 1.0;
            else if (v == mn) out = 0.0;
            else out = (v - mn) / range;
            return static_cast<std::uint8_t>(std::lround((std::clamp)(out, 0.0, 1.0) * 255.0));
        };
        return { channel(r), channel(g), channel(b), c.a };
    }

    std::wstring ProtoObject::Property(std::wstring_view key) const
    {
        if (key == L"size")
        {
            return std::to_wstring(size);
        }
        if (key == L"color")
        {
            return color.ToHex();
        }
        return {};
    }

    void ProtoObject::SetProperty(std::wstring_view key, std::wstring_view value)
    {
        if (key == L"size")
        {
            int parsed = 0;
            bool any = false;
            for (wchar_t c : value)
            {
                if (c < L'0' || c > L'9')
                {
                    return;
                }
                parsed = parsed * 10 + (c - L'0');
                any = true;
            }
            if (any)
            {
                size = std::clamp(parsed, 0, 9);
            }
        }
        else if (key == L"color")
        {
            if (auto const parsed = Color::FromHex(value))
            {
                color = *parsed;
            }
        }
    }
}
