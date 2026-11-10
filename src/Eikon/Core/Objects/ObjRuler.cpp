#include "pch.h"
#include "Core/Objects/ObjRuler.h"

#include "Core/Objects/ObjectSupport.h"

#include <algorithm>
#include <cmath>

namespace Eikon::Core
{
    using namespace Support;

    JsonValue ObjRuler::Dump() const
    {
        JsonValue json = DumpBase();
        SetBool(json, L"vertical", vertical);
        SetNumber(json, L"tperc", tperc);
        SetNumber(json, L"tdist", tdist);
        SetNumber(json, L"dpr", dpr);
        return json;
    }

    bool ObjRuler::Load(JsonValue const& json)
    {
        auto v = GetBool(json, L"vertical");
        auto tp = GetNumber(json, L"tperc"), td = GetNumber(json, L"tdist"), d = GetNumber(json, L"dpr");
        if (!v || !tp || !td || !d || !LoadBase(json))
        {
            return false;
        }
        vertical = *v;
        tperc = *tp;
        tdist = *td;
        dpr = *d;
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjRuler::SetProperty(std::wstring_view key, std::wstring_view value)
    {
        if (key == L"rulerPixels")
        {
            dpr = value == L"px" ? 1.0 : Environment().dpr;
            return;
        }
        ProtoObject::SetProperty(key, value);
    }

    std::wstring ObjRuler::Property(std::wstring_view key) const
    {
        if (key == L"rulerPixels") return dpr == 1.0 ? L"px" : L"pt";
        return ProtoObject::Property(key);
    }

    std::vector<Rect> ObjRuler::BarRects() const
    {
        double const crossX = vertical ? -0.5 : 0.0;
        double const crossY = vertical ? 0.0 : -0.5;
        double const x = std::trunc(rect.x + crossX);
        double const y = std::trunc(rect.y + crossY);
        double const w = (std::max)(1.0, std::trunc(rect.width));
        double const h = (std::max)(1.0, std::trunc(rect.height));
        std::vector<Rect> rects{ { x, y, w, h } };
        if (vertical)
        {
            rects.push_back({ x - 3.0, y, 7.0, 1.0 });
            rects.push_back({ x - 3.0, y + h - 1.0, 7.0, 1.0 });
        }
        else
        {
            rects.push_back({ x, y - 3.0, 1.0, 7.0 });
            rects.push_back({ x + w - 1.0, y - 3.0, 1.0, 7.0 });
        }
        return rects;
    }

    std::vector<Handle> ObjRuler::Handles() const
    {
        Rect const b = rect.Standardized();
        if (vertical)
        {
            return { { L"t-resize", { b.x + b.width / 2, b.Top() }, L"cursor-t-resize" },
                     { L"b-resize", { b.x + b.width / 2, b.Bottom() }, L"cursor-t-resize" } };
        }
        return { { L"l-resize", { b.Left(), b.y + b.height / 2 }, L"cursor-l-resize" },
                 { L"r-resize", { b.Right(), b.y + b.height / 2 }, L"cursor-l-resize" } };
    }

    std::wstring ObjRuler::HitTest(Point point, double zoom) const
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
        double const slop = 4.0 / z;
        Rect const b = rect.Standardized();
        return (point.x >= b.Left() - slop && point.x <= b.Right() + slop && point.y >= b.Top() - slop - 3 && point.y <= b.Bottom() + slop + 3)
                   ? L"move"
                   : L"";
    }

    void ObjRuler::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
    }

    void ObjRuler::MouseDragged(Point point, Modifiers modifiers)
    {
        double const dx = std::round(point.x - mouseDown.x);
        double const dy = std::round(point.y - mouseDown.y);
        Rect const r = mouseDownRect;
        std::wstring_view const s = state;
        if (s == ObjectState::Move)
        {
            ProtoObject::MouseDragged(point, modifiers);
        }
        else if (s == L"l-resize")
        {
            rect = { r.x + dx, r.y, r.width - dx, r.height };
        }
        else if (s == L"r-resize")
        {
            rect = { r.x, r.y, r.width + dx, r.height };
        }
        else if (s == L"t-resize")
        {
            rect = { r.x, r.y + dy, r.width, r.height - dy };
        }
        else if (s == L"b-resize")
        {
            rect = { r.x, r.y, r.width, r.height + dy };
        }
    }

    std::vector<Shape> ObjRuler::Shapes(double, double) const
    {
        Shape shape;
        for (Rect const& r : BarRects())
        {
            shape.path.MoveTo({ r.x, r.y });
            shape.path.LineTo({ r.x + r.width, r.y });
            shape.path.LineTo({ r.x + r.width, r.y + r.height });
            shape.path.LineTo({ r.x, r.y + r.height });
            shape.path.Close();
        }
        shape.fill = true;
        shape.fillPaint = { DrawColor(), 1.0 };
        return { shape };
    }
}
