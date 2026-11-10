#include "pch.h"
#include "Core/Objects/ObjConceal.h"

#include "Core/Objects/Conceal.h"
#include "Core/Objects/ObjectSupport.h"
#include "Core/Objects/Random.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>

namespace Eikon::Core
{
    using namespace Support;

    namespace
    {
        bool IsMode(std::wstring_view v)
        {
            return v == L"pixelate" || v == L"pixelate-text" || v == L"erase" || v == L"erase-text";
        }
    }

    JsonValue ObjConceal::Dump() const
    {
        JsonValue json = JsonValue::Object();
        json.Set(L"rect", RectJson(rect));
        SetNumber(json, L"size", size);
        SetString(json, L"fillMode", fillMode);
        return json;
    }

    bool ObjConceal::Load(JsonValue const& json)
    {
        auto r = GetRect(json, L"rect");
        auto s = GetNumber(json, L"size");
        auto m = GetString(json, L"fillMode");
        if (!r || !s || !m)
        {
            return false;
        }
        rect = *r;
        size = static_cast<std::int32_t>(frintaInt(*s));
        fillMode = *m;
        m_bitmap.reset();
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjConceal::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        std::wstring const mode = settings.ReadString(PreferenceKey(L"fillMode"), L"pixelate");
        fillMode = IsMode(mode) ? mode : L"pixelate";
    }

    void ObjConceal::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"concealType")
        {
            fillMode = std::wstring{ value };
            SavePreference(settings, L"fillMode", value);
            m_bitmap.reset();
            return;
        }
        ProtoObject::SetProperty(name, value);
        if (name == L"size")
        {
            m_bitmap.reset();
        }
    }

    std::wstring ObjConceal::Property(std::wstring_view name) const
    {
        if (name == L"concealType") return fillMode;
        return ProtoObject::Property(name);
    }

    Rect ObjConceal::Area() const
    {
        auto const& env = Environment();
        double const w = env.image.width > 0 ? env.image.width : (env.bitmap ? env.bitmap->width : 0);
        double const h = env.image.height > 0 ? env.image.height : (env.bitmap ? env.bitmap->height : 0);
        if (w <= 0 || h <= 0)
        {
            return rect.Standardized();
        }
        return Conceal::ClampArea(rect, w, h);
    }

    std::wstring ObjConceal::HitTest(Point point, double zoom) const
    {
        if (IsSelected())
        {
            return HitBox(rect, point, zoom);
        }
        return rect.Standardized().Contains(point) ? L"move" : L"";
    }

    void ObjConceal::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (IsCreating())
        {
            rect = { std::round(point.x), std::round(point.y), 0, 0 };
        }
    }

    void ObjConceal::MouseUp(Point point, Modifiers modifiers)
    {
        DragBox(point, modifiers);
        auto const& env = Environment();
        if (env.image.width > 0 && env.image.height > 0)
        {
            rect = Conceal::ClampArea(rect, env.image.width, env.image.height);
        }
    }

    std::optional<Rect> ObjConceal::PendingOcr() const
    {
        if (!IsTextMode() || IsCreating())
        {
            return std::nullopt;
        }
        Rect const area = Area();
        if (area == m_boxesArea)
        {
            return std::nullopt;
        }
        return area;
    }

    void ObjConceal::SetTextBoxes(Rect const& forArea, std::vector<Rect> boxes)
    {
        m_boxesArea = forArea;
        m_boxes = std::move(boxes);
        m_bitmap.reset();
    }

    std::shared_ptr<Bitmap const> ObjConceal::Concealed(Bitmap const& source, double dpr) const
    {
        if (source.IsEmpty() || IsCreating())
        {
            return nullptr;
        }
        Rect const area = Conceal::ClampArea(rect, source.width, source.height);
        if (IsTextMode() && area != m_boxesArea)
        {
            return nullptr;
        }
        if (!m_bitmap || m_bitmapArea != area || m_bitmapMode != fillMode || m_bitmapSize != size)
        {
            static std::vector<Rect> const none;
            m_bitmap = std::make_shared<Bitmap const>(
                Conceal::Generate(source, area, fillMode, size, dpr, IsTextMode() ? m_boxes : none, [] { return Random::Unit(); }));
            m_bitmapArea = area;
            m_bitmapMode = fillMode;
            m_bitmapSize = size;
        }
        return m_bitmap;
    }

    std::vector<Shape> ObjConceal::Shapes(double dpr, double) const
    {
        Rect const b = rect.Standardized();
        if (b.width <= 0 && b.height <= 0)
        {
            return {};
        }
        Shape shape;
        shape.path.MoveTo({ b.Left(), b.Top() });
        shape.path.LineTo({ b.Right(), b.Top() });
        shape.path.LineTo({ b.Right(), b.Bottom() });
        shape.path.LineTo({ b.Left(), b.Bottom() });
        shape.path.Close();
        if (IsCreating())
        {
            shape.fill = true;
            shape.fillPaint = { Color{ 0, 0, 0, 255 }, 0.1 };
            shape.stroke = true;
            shape.strokePaint = { Color{ 255, 255, 255, 255 }, 1.0 };
            shape.strokeWidth = dpr;
            return { shape };
        }
        if (IsHighlighted())
        {
            shape.stroke = true;
            shape.strokePaint = { kHoverBlue, 1.0 };
            shape.strokeWidth = 2.0 * dpr;
            return { shape };
        }
        return {};
    }
}
