#include "pch.h"
#include "Core/Objects/ObjFreehand.h"

#include "Core/Bitmap.h"
#include "Core/Objects/ObjectSupport.h"
#include "Core/Objects/Random.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Eikon::Core
{
    using namespace Support;

    std::map<std::wstring, std::shared_ptr<std::vector<Point> const>>& PointsStore()
    {
        static std::map<std::wstring, std::shared_ptr<std::vector<Point> const>> store;
        return store;
    }

    std::vector<Point> const& ObjFreehand::Points() const
    {
        if (!pointsHash.empty())
        {
            auto const& store = PointsStore();
            if (auto const it = store.find(pointsHash); it != store.end() && it->second)
            {
                return *it->second;
            }
        }
        return m_drawing;
    }

    JsonValue ObjFreehand::Dump() const
    {
        JsonValue json = DumpBase();
        SetNumber(json, L"resolution", resolution);
        SetNumber(json, L"smoothing", smoothing);
        SetNumber(json, L"capStyle", capStyle);
        SetString(json, L"shapeCorrection", shapeCorrection);
        json.Set(L"rawRect", RectJson(rawRect));
        SetString(json, L"pointsHash", pointsHash);
        return json;
    }

    bool ObjFreehand::Load(JsonValue const& json)
    {
        auto res = GetNumber(json, L"resolution"), sm = GetNumber(json, L"smoothing"), cap = GetNumber(json, L"capStyle");
        auto corr = GetString(json, L"shapeCorrection"), hash = GetString(json, L"pointsHash");
        auto raw = GetRect(json, L"rawRect");
        if (!res || !sm || !cap || !corr || !hash || !raw || !LoadBase(json))
        {
            return false;
        }
        resolution = *res;
        smoothing = *sm;
        capStyle = static_cast<std::int32_t>(*cap);
        shapeCorrection = *corr;
        rawRect = *raw;
        pointsHash = *hash;
        m_drawing.clear();
        m_stamp.clear();
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjFreehand::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        smoothing = 0.6;
        variability = 0.25;
        realtimeShape = false;
        capStyle = 0;
        resolution = 0;
        rawRect = { 0, 0, 1, 1 };
        lockDirection = 0;
        shapeCorrection.clear();
        pointsHash.clear();
    }

    void ObjFreehand::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        if (name == L"correction")
        {
            if (value == L"s" || value == L"c")
            {
                shapeCorrection += value;
            }
            else
            {
                shapeCorrection = value == L"x" ? std::wstring{} : std::wstring{ value };
            }
            m_stamp.clear();
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjFreehand::Property(std::wstring_view name) const
    {
        if (name == L"correction")
        {
            return shapeCorrection.empty() ? L"x" : shapeCorrection.substr(shapeCorrection.size() - 1);
        }
        return ProtoObject::Property(name);
    }

    void ObjFreehand::MoveBy(double dx, double dy)
    {
        rect = rect.Offset(dx, dy);
    }

    std::wstring ObjFreehand::HitTest(Point point, double zoom) const
    {
        double const z = zoom > 0 ? zoom : 1.0;
        if (IsSelected())
        {
            if (std::wstring const hit = HitBox(rect, point, zoom, false); !hit.empty())
            {
                return hit;
            }
        }
        Freehand::Samples line;
        Outline(Environment().dpr, &line);
        double const tolerance = (std::max)(WidthFor(size) * Environment().dpr * 0.5 + 2.0, 6.0 / z);
        for (std::size_t i = 1; i < line.size(); ++i)
        {
            if (DistanceToSegment(point, line[i - 1].P(), line[i].P()) <= tolerance)
            {
                return L"move";
            }
        }
        if (line.size() == 1 && Distance(point, line[0].P()) <= tolerance)
        {
            return L"move";
        }
        return L"";
    }

    void ObjFreehand::MouseDown(Point point, Modifiers modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (IsCreating())
        {
            auto const& env = Environment();
            resolution = env.zoom / (env.dpr > 0 ? env.dpr : 1.0);
            m_drawing = { point };
            m_lastPoint = point;
            lockDirection = 0;
            (void)modifiers;
        }
    }

    void ObjFreehand::MouseDragged(Point point, Modifiers modifiers)
    {
        if (IsCreating())
        {
            Point p = point;
            if (HasFlag(modifiers, Modifiers::Shift))
            {
                // Shift locks the stroke to the direction of its first move.
                if (lockDirection == 0)
                {
                    double const dx = std::fabs(point.x - m_drawing.front().x);
                    double const dy = std::fabs(point.y - m_drawing.front().y);
                    if (dx != dy) lockDirection = dx > dy ? 1 : 2;
                }
                if (lockDirection == 1) p.y = m_drawing.front().y;
                if (lockDirection == 2) p.x = m_drawing.front().x;
            }
            if (p != m_lastPoint)
            {
                m_drawing.push_back(p);
                m_lastPoint = p;
            }
            // While drawing the points are shown where they were captured.
            double l = p.x, t = p.y, r = p.x, b = p.y;
            for (Point const& q : m_drawing)
            {
                l = (std::min)(l, q.x);
                t = (std::min)(t, q.y);
                r = (std::max)(r, q.x);
                b = (std::max)(b, q.y);
            }
            rawRect = { l, t, (std::max)(r - l, 1.0), (std::max)(b - t, 1.0) };
            rect = rawRect;
            return;
        }
        DragBox(point, modifiers);
    }

    void ObjFreehand::MouseUp(Point point, Modifiers modifiers)
    {
        if (IsCreating())
        {
            MouseDragged(point, modifiers);
            if (m_drawing.size() == 1)
            {
                rawRect = { point.x, point.y, 1, 1 };
                rect = rawRect;
            }
            pointsHash = Random::Hash(25);
            PointsStore()[pointsHash] = std::make_shared<std::vector<Point> const>(m_drawing);
            m_drawing.clear();
            m_stamp.clear();
            return;
        }
        DragBox(point, modifiers);
    }

    Path const& ObjFreehand::Outline(double dpr, Freehand::Samples* centreLine) const
    {
        auto const& points = Points();
        std::wstring stamp = std::to_wstring(points.size()) + L"|" + std::to_wstring(rect.x) + L"," + std::to_wstring(rect.y) + L"," +
                             std::to_wstring(rect.width) + L"," + std::to_wstring(rect.height) + L"|" + std::to_wstring(rawRect.x) + L"," +
                             std::to_wstring(rawRect.y) + L"," + std::to_wstring(rawRect.width) + L"," + std::to_wstring(rawRect.height) + L"|" +
                             std::to_wstring(size) + L"|" + std::to_wstring(dpr) + L"|" + shapeCorrection + L"|" + std::to_wstring(capStyle) + L"|" +
                             std::to_wstring(smoothing) + L"|" + pointsHash;
        if (stamp != m_stamp)
        {
            Freehand::StrokeParams params;
            params.width = WidthFor(size) * dpr;
            params.smoothing = smoothing;
            params.variability = variability;
            params.resolution = resolution > 0 ? resolution : 1.0 / (dpr > 0 ? dpr : 1.0);
            params.dpr = dpr;
            params.capStyle = capStyle;
            params.corrections = shapeCorrection;
            params.rawRect = rawRect;
            params.rect = rect;
            m_cacheLine.clear();
            m_cache = points.empty() ? Path{} : Freehand::StrokePath(points, params, &m_cacheLine);
            m_stamp = std::move(stamp);
        }
        if (centreLine)
        {
            *centreLine = m_cacheLine;
        }
        return m_cache;
    }

    std::vector<Shape> ObjFreehand::Shapes(double dpr, double) const
    {
        Path const& path = Outline(dpr);
        if (path.Empty())
        {
            return {};
        }
        Shape shape;
        shape.path = path;
        shape.fill = true;
        shape.fillPaint = { DrawColor(), 1.0 };
        return { shape };
    }

    // ---------------- ObjHighlighter ----------------

    void ObjHighlighter::ApplyDefaults(Settings& settings)
    {
        ObjFreehand::ApplyDefaults(settings);
        color = Color::FromHex(settings.ReadString(PreferenceKey(L"color"), L"FFF68D")).value_or(Color{ 0xFF, 0xF6, 0x8D, 0xFF });
        preferredPalette = Palette::Highlighter;
        smoothing = 0.9;
        variability = 0.05;
        realtimeShape = true;
        capStyle = static_cast<std::int32_t>(settings.ReadInt(PreferenceKey(L"capStyle"), 3));
        shapeCorrection = L"h";
        size = (std::min)(size, 7);
    }

    void ObjHighlighter::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        ObjFreehand::SetProperty(name, value);
        if (name == L"color")
        {
            SavePreference(Settings::Current(), L"color", color.ToHex());
        }
    }

    HighlighterLook ObjHighlighter::LookForLuminance(double mean, Color base)
    {
        if (mean > 0.66)
        {
            return { HighlighterLook::Blend::Multiply, 1.0, base };
        }
        if (mean > 0.33)
        {
            return { HighlighterLook::Blend::Normal, 0.25, Saturate(base) };
        }
        return { HighlighterLook::Blend::Screen, 0.33, Saturate(base) };
    }

    HighlighterLook ObjHighlighter::Look(Bitmap const* image, double dpr) const
    {
        Color const base = DrawColor();
        if (!image || image->IsEmpty())
        {
            return LookForLuminance(1.0, base);
        }
        Freehand::Samples line;
        Outline(dpr, &line);
        std::vector<double> lum;
        lum.reserve(line.size());
        for (auto const& s : line)
        {
            auto const x = static_cast<long long>(std::floor(s.x));
            auto const y = static_cast<long long>(std::floor(s.y));
            if (x < 0 || y < 0 || x >= image->width || y >= image->height)
            {
                continue;
            }
            std::uint8_t const* p = image->pixels.data() + static_cast<std::size_t>(y) * image->Stride() + static_cast<std::size_t>(x) * 4;
            lum.push_back(Luminance(Color{ p[2], p[1], p[0], 255 }));
        }
        if (lum.size() < 4)
        {
            return LookForLuminance(1.0, base);
        }
        std::sort(lum.begin(), lum.end());
        std::size_t const from = lum.size() / 4;
        std::size_t const to = lum.size() - lum.size() / 4;
        double sum = 0;
        for (std::size_t i = from; i < to; ++i)
        {
            sum += lum[i];
        }
        return LookForLuminance(sum / static_cast<double>(to - from), base);
    }

    std::vector<Shape> ObjHighlighter::Shapes(double dpr, double) const
    {
        Path const& path = Outline(dpr);
        if (path.Empty())
        {
            return {};
        }
        // Colour, blend and opacity come from Look(); the renderer composites it.
        HighlighterLook const look = Look(Environment().bitmap, dpr);
        Shape shape;
        shape.path = path;
        shape.fill = true;
        shape.fillPaint = { look.color, 1.0 };
        return { shape };
    }
}
