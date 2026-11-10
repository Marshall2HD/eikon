#include "pch.h"
#include "Core/Objects/ObjLine.h"

#include "Core/Objects/ArrHelper.h"
#include "Core/Objects/ObjectSupport.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>

namespace Eikon::Core
{
    using namespace Support;

    JsonValue ObjLine::Dump() const
    {
        JsonValue json = DumpBase();
        SetString(json, L"startCap", startCap);
        SetString(json, L"endCap", endCap);
        SetBool(json, L"dotted", dotted);
        return json;
    }

    bool ObjLine::Load(JsonValue const& json)
    {
        auto s = GetString(json, L"startCap"), e = GetString(json, L"endCap");
        auto d = GetBool(json, L"dotted");
        if (!s || !e || !d || !LoadBase(json))
        {
            return false;
        }
        startCap = *s;
        endCap = *e;
        dotted = *d;
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjLine::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        startCap = L"none";
        endCap = L"none";
        dotted = settings.ReadString(PreferenceKey(L"lineType"), L"line") == L"dash";
    }

    void ObjLine::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"lineType")
        {
            if (value != L"line" && value != L"dash")
            {
                return;
            }
            startCap = L"none";
            endCap = L"none";
            dotted = value == L"dash";
            SavePreference(settings, L"lineType", value);
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjLine::Property(std::wstring_view name) const
    {
        if (name == L"lineType") return dotted ? L"dash" : L"line";
        return ProtoObject::Property(name);
    }

    std::vector<Handle> ObjLine::Handles() const
    {
        return { { L"point-1", Start(), L"cursor-move" }, { L"point-2", End(), L"cursor-move" } };
    }

    std::wstring ObjLine::HitTest(Point point, double zoom) const
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
        double const w = kWidths[std::clamp(size, 0, 9)] * Environment().dpr;
        return DistanceToSegment(point, Start(), End()) <= (std::max)(w * 0.5 + 2.0, 6.0 / z) ? L"move" : L"";
    }

    void ObjLine::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        m_downRect = rect;
        if (IsCreating())
        {
            rect = { point.x, point.y, 0, 0 };
        }
    }

    void ObjLine::MouseDragged(Point point, Modifiers modifiers)
    {
        bool const shift = HasFlag(modifiers, Modifiers::Shift);
        auto lock = [&](Point anchor, Point p) {
            double dx = p.x - anchor.x;
            double dy = p.y - anchor.y;
            if (shift)
            {
                (std::fabs(dx) < std::fabs(dy) ? dx : dy) = 0;
            }
            return Point{ anchor.x + dx, anchor.y + dy };
        };
        Point const start = m_downRect.Origin();
        Point const end{ m_downRect.x + m_downRect.width, m_downRect.y + m_downRect.height };
        std::wstring_view const s = state;
        if (s == ObjectState::Creating)
        {
            Point const p = lock(mouseDown, point);
            rect = { mouseDown.x, mouseDown.y, p.x - mouseDown.x, p.y - mouseDown.y };
        }
        else if (s == ObjectState::Move)
        {
            Point const p = lock(mouseDown, point);
            rect = m_downRect.Offset(p.x - mouseDown.x, p.y - mouseDown.y);
        }
        else if (s == L"point-1")
        {
            Point const p = lock(end, point);
            rect = { p.x, p.y, end.x - p.x, end.y - p.y };
        }
        else if (s == L"point-2")
        {
            Point const p = lock(start, point);
            rect = { start.x, start.y, p.x - start.x, p.y - start.y };
        }
    }

    std::vector<Shape> ObjLine::Shapes(double dpr, double) const
    {
        Point const start = Start();
        Point const delta{ rect.width, rect.height };
        double const length = std::hypot(delta.x, delta.y);
        if (length <= 0)
        {
            return {};
        }
        double const base = kWidths[std::clamp(size, 0, 9)];
        double const w = base * dpr;
        Paint const paint{ DrawColor(), 1.0 };
        std::vector<Shape> shapes;
        Shape line;
        line.stroke = true;
        line.strokePaint = paint;
        line.strokeWidth = w;
        line.cap = Shape::Cap::Round;
        line.join = Shape::Join::Round;
        if (dotted)
        {
            // Caps are suppressed; 1.5 dpr dashes with round caps make dots.
            line.path.MoveTo(start);
            line.path.LineTo(End());
            line.dashes = { 1.5 * dpr, (1.5 * base + 2.0) * dpr };
            shapes.push_back(std::move(line));
            return shapes;
        }
        line.path = ArrHelper::LineArrow(start, delta, w, dpr, false, {}, startCap == L"arrow", endCap == L"arrow");
        shapes.push_back(std::move(line));
        auto bullet = [&](Point at) {
            Shape b;
            b.path = ArrHelper::Bullet(at, length, w);
            b.fill = true;
            b.fillPaint = paint;
            shapes.push_back(std::move(b));
        };
        if (startCap == L"bullet") bullet(start);
        if (endCap == L"bullet") bullet(End());
        return shapes;
    }
}
