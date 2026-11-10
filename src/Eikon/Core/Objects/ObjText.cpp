#include "pch.h"
#include "Core/Objects/ObjText.h"

#include "Core/Bitmap.h"
#include "Core/Objects/ObjectSupport.h"
#include "Core/Settings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace Eikon::Core
{
    using namespace Support;

    namespace
    {
        double Linear(double c)
        {
            c /= 255.0;
            return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double RelativeLuminance(Color c)
        {
            return 0.2126 * Linear(c.r) + 0.7152 * Linear(c.g) + 0.0722 * Linear(c.b);
        }

        Path RoundedRect(Rect const& b, double radius)
        {
            Path path;
            double const r = (std::max)(0.0, (std::min)({ radius, b.width / 2, b.height / 2 }));
            double const l = b.Left(), t = b.Top(), rt = b.Right(), bt = b.Bottom();
            path.MoveTo({ l + r, t });
            path.LineTo({ rt - r, t });
            if (r > 0) path.ArcAround({ rt - r, t + r }, r, -kPi / 2, kPi / 2);
            path.LineTo({ rt, bt - r });
            if (r > 0) path.ArcAround({ rt - r, bt - r }, r, 0, kPi / 2);
            path.LineTo({ l + r, bt });
            if (r > 0) path.ArcAround({ l + r, bt - r }, r, kPi / 2, kPi / 2);
            path.LineTo({ l, t + r });
            if (r > 0) path.ArcAround({ l + r, t + r }, r, kPi, kPi / 2);
            path.Close();
            return path;
        }
    }

    double ContrastRatio(Color a, Color b)
    {
        double la = RelativeLuminance(a);
        double lb = RelativeLuminance(b);
        if (la < lb) std::swap(la, lb);
        double const ratio = (la + 0.05) / (lb + 0.05);
        double const scale = ratio >= 1.5 ? 10.0 : 100.0;
        return std::trunc(ratio * scale) / scale;
    }

    std::optional<Color> DetectBackground(Bitmap const& bitmap, Rect const& rect, int maxSamples)
    {
        Rect const r = rect.Standardized().Intersection({ 0, 0, static_cast<double>(bitmap.width), static_cast<double>(bitmap.height) });
        if (bitmap.IsEmpty() || r.width < 1 || r.height < 1 || maxSamples <= 0)
        {
            return std::nullopt;
        }
        double const step = (std::max)(1.0, std::sqrt(r.width * r.height / maxSamples));
        std::map<std::uint32_t, int> counts;
        int total = 0;
        int translucent = 0;
        for (double y = r.y + step / 2; y < r.Bottom(); y += step)
        {
            for (double x = r.x + step / 2; x < r.Right(); x += step)
            {
                auto const px = static_cast<std::size_t>(x);
                auto const py = static_cast<std::size_t>(y);
                std::uint8_t const* p = bitmap.pixels.data() + py * bitmap.Stride() + px * 4;
                ++total;
                if (p[3] < 250)
                {
                    ++translucent;
                    continue;
                }
                ++counts[(static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0]];
            }
        }
        if (total == 0 || translucent * 2 > total || counts.empty())
        {
            return std::nullopt;
        }
        auto const best = std::max_element(counts.begin(), counts.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
        return Color{ static_cast<std::uint8_t>(best->first >> 16), static_cast<std::uint8_t>(best->first >> 8), static_cast<std::uint8_t>(best->first), 255 };
    }

    double ObjText::FontSize(double dpr) const
    {
        return BaseFontSize() * dpr * (style == AnnotationStyle::Goofy ? 1.2 : 1.0);
    }

    double ObjText::LineHeight(double dpr) const
    {
        double const fs = FontSize(dpr);
        return style == AnnotationStyle::Goofy ? fs * 1.324 : std::round(fs * 1.15);
    }

    Point ObjText::Insets(double dpr) const
    {
        double const fs = BaseFontSize() * dpr;
        if (nobg)
        {
            return { std::round(fs * 0.03 + 4.0), std::round(fs * 0.11) };
        }
        std::size_t const n = text.size();
        double const f = n >= 161 ? 2.0 : n >= 121 ? 1.75 : n >= 81 ? 1.5 : n >= 41 ? 1.25 : 1.0;
        return { std::round(f * fs * 0.417), std::round(f * fs * 0.25) };
    }

    Color ObjText::TextColor() const
    {
        if (nobg)
        {
            return DrawColor();
        }
        return Luminance(DrawColor()) <= 0.6 ? Color{ 255, 255, 255, 255 } : Color{ 0, 0, 0, 255 };
    }

    bool ObjText::UsesMediumWeight() const
    {
        Color const c = TextColor();
        return c.r == 255 && c.g == 255 && c.b == 255;
    }

    std::wstring_view ObjText::FontFamily() const
    {
        return style == AnnotationStyle::Goofy ? L"Ink Free" : L"Segoe UI Variable Text";
    }

    void ObjText::Layout(double dpr)
    {
        Point const inset = Insets(dpr);
        double const wrap = (std::max)(maxWidth - 2 * inset.x, FontSize(dpr));
        Size measured;
        if (auto const& measure = Environment().measureText)
        {
            measured = measure(*this, wrap);
        }
        else
        {
            // Without DirectWrite (Core tests): an estimate of 0.55 em per character.
            double const fs = FontSize(dpr);
            double lineWidth = 0, widest = 0;
            int lines = 1;
            for (wchar_t ch : text)
            {
                if (ch == L'\n')
                {
                    ++lines;
                    widest = (std::max)(widest, lineWidth);
                    lineWidth = 0;
                    continue;
                }
                lineWidth += fs * 0.55;
                if (lineWidth > wrap)
                {
                    ++lines;
                    widest = wrap;
                    lineWidth = fs * 0.55;
                }
            }
            widest = (std::max)(widest, lineWidth);
            measured = { widest, lines * LineHeight(dpr) };
        }
        measured.height = (std::max)(measured.height, LineHeight(dpr));
        measured.width = (std::max)(measured.width, FontSize(dpr) * 0.5);
        rect.width = std::ceil(measured.width + 2 * inset.x);
        rect.height = std::ceil(measured.height + 2 * inset.y);
    }

    Rect ObjText::TextRect(double dpr) const
    {
        Point const inset = Insets(dpr);
        Rect const b = rect.Standardized();
        return { b.x + inset.x, b.y + inset.y, (std::max)(b.width - 2 * inset.x, 1.0), (std::max)(b.height - 2 * inset.y, 1.0) };
    }

    Point ObjText::PointerTip() const
    {
        Rect const b = rect.Standardized();
        double const t = pPerc;
        if (pSide == L"t") return { b.x + b.width * t, b.Top() - pDist };
        if (pSide == L"l") return { b.Left() - pDist, b.y + b.height * t };
        if (pSide == L"r") return { b.Right() + pDist, b.y + b.height * t };
        return { b.x + b.width * t, b.Bottom() + pDist };
    }

    Path ObjText::BoxPath(double dpr) const
    {
        return RoundedRect(rect.Standardized(), BaseFontSize() * dpr * 0.25);
    }

    Path ObjText::PointerPath(double dpr) const
    {
        Path path;
        Rect const b = rect.Standardized();
        bool const horizontal = pSide == L"t" || pSide == L"b";
        double const sideLength = horizontal ? b.width : b.height;
        if (!HasPointer() || sideLength < 15.0)
        {
            return path;
        }
        double const fs = BaseFontSize();
        double const r = fs * dpr * 0.25;
        double const shortLimit = ((fs / 14.0) * 0.4 + std::sqrt(fs / 14.0) * 0.6) * 12.0 * dpr;
        Point const tip = PointerTip();
        double const t = (std::clamp)(pPerc, 0.0, 1.0);
        // The base slides along the side inside the corner radii.
        double const along = sideLength * 0.1 + r + (sideLength * 0.8 - (r + r)) * t;
        double halfWidth;
        if (pDist < shortLimit)
        {
            halfWidth = (pDist + pDist) * 1.8 * 0.5;
        }
        else
        {
            double const reach = (std::max)(std::hypot(pDist, sideLength * (t - 0.5)), 400.0);
            halfWidth = fs * (std::sqrt(reach) / 35.0 + 0.9) * dpr * 0.5;
        }
        if (sharpTip)
        {
            halfWidth /= 1.6;
        }
        halfWidth = (std::min)(halfWidth, (sideLength - 2 * r) * 0.5);
        double const inset = r;   // the base sits inside the box so the fill joins seamlessly
        Point b1, b2;
        if (pSide == L"t")
        {
            b1 = { b.x + along - halfWidth, b.Top() + inset };
            b2 = { b.x + along + halfWidth, b.Top() + inset };
        }
        else if (pSide == L"l")
        {
            b1 = { b.Left() + inset, b.y + along + halfWidth };
            b2 = { b.Left() + inset, b.y + along - halfWidth };
        }
        else if (pSide == L"r")
        {
            b1 = { b.Right() - inset, b.y + along - halfWidth };
            b2 = { b.Right() - inset, b.y + along + halfWidth };
        }
        else
        {
            b1 = { b.x + along + halfWidth, b.Bottom() - inset };
            b2 = { b.x + along - halfWidth, b.Bottom() - inset };
        }
        path.MoveTo(b1);
        if (sharpTip)
        {
            path.LineTo(tip);
        }
        else
        {
            // Rounded tip of radius dpr·√fs·0.33.
            double const tipRadius = dpr * std::sqrt(fs) * 0.33;
            auto toward = [&](Point from) {
                double const d = Distance(from, tip);
                double const k = d > 0 ? (std::min)(tipRadius / d, 0.5) : 0;
                return Point{ tip.x + (from.x - tip.x) * k, tip.y + (from.y - tip.y) * k };
            };
            path.LineTo(toward(b1));
            path.QuadTo(tip, toward(b2));
        }
        path.LineTo(b2);
        path.Close();
        return path;
    }

    JsonValue ObjText::Dump() const
    {
        JsonValue json = DumpBase();
        SetBool(json, L"nobg", nobg);
        SetNumber(json, L"style", static_cast<double>(style));
        SetString(json, L"text", text);
        SetNumber(json, L"maxWidth", maxWidth);
        SetNumber(json, L"pPerc", pPerc);
        SetNumber(json, L"pDist", pDist);
        SetString(json, L"pSide", pSide);
        return json;
    }

    bool ObjText::Load(JsonValue const& json)
    {
        auto nb = GetBool(json, L"nobg");
        auto st = GetNumber(json, L"style"), mw = GetNumber(json, L"maxWidth"), pp = GetNumber(json, L"pPerc"), pd = GetNumber(json, L"pDist");
        auto tx = GetString(json, L"text"), ps = GetString(json, L"pSide");
        if (!nb || !st || !mw || !pp || !pd || !tx || !ps || !LoadBase(json))
        {
            return false;
        }
        nobg = *nb;
        style = *st == 1 ? AnnotationStyle::Goofy : AnnotationStyle::Pointy;
        text = *tx;
        maxWidth = *mw;
        pPerc = *pp;
        pDist = *pd;
        pSide = *ps;
        editing = false;
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjText::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        double const dpr = Environment().dpr;
        style = settings.ReadInt(PreferenceKey(L"style"), 0) == 1 ? AnnotationStyle::Goofy : AnnotationStyle::Pointy;
        nobg = style == AnnotationStyle::Goofy;
        sharpTip = false;
        if (settings.ReadString(PreferenceKey(L"pointerPresent"), L"true") == L"true")
        {
            pPerc = 0.38;
            pDist = 30.0;   // ObjText init: 30 (pixels, not scaled by dpr)
            pSide = L"b";
        }
        else
        {
            pPerc = 0.5;
            pDist = 0;
            pSide = L"t";
        }
        maxWidth = (BaseFontSize() * 8.0 + 188.0) * dpr;
        editing = false;   // TextEditOverlay::Begin sets it while the editor is open
    }

    void ObjText::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        double const dpr = Environment().dpr;
        if (name == L"style")
        {
            style = value == L"1" ? AnnotationStyle::Goofy : AnnotationStyle::Pointy;
            nobg = style == AnnotationStyle::Goofy;
            SavePreference(settings, L"style", value == L"1" ? L"1" : L"0");
            Layout(dpr);
            return;
        }
        if (name == L"nobg")
        {
            nobg = value == L"true";
            Layout(dpr);
            return;
        }
        ProtoObject::SetProperty(name, value);
        if (name == L"size")
        {
            maxWidth = (BaseFontSize() * 8.0 + 188.0) * dpr;
            Layout(dpr);
        }
    }

    std::wstring ObjText::Property(std::wstring_view name) const
    {
        if (name == L"style") return style == AnnotationStyle::Goofy ? L"1" : L"0";
        if (name == L"nobg") return nobg ? L"true" : L"false";
        return ProtoObject::Property(name);
    }

    std::vector<Handle> ObjText::Handles() const
    {
        Rect const b = rect.Standardized();
        std::vector<Handle> handles{
            { L"l-resize", { b.Left(), b.y + b.height / 2 }, L"cursor-l-resize" },
            { L"r-resize", { b.Right(), b.y + b.height / 2 }, L"cursor-l-resize" },
        };
        if (!nobg)
        {
            Point tip = PointerTip();
            if (pDist <= 0)
            {
                tip = { b.x + b.width * pPerc, pSide == L"t" ? b.Top() : b.Bottom() };
            }
            handles.push_back({ L"point-pointer", tip, L"cursor-move" });
        }
        return handles;
    }

    std::wstring ObjText::HitTest(Point point, double zoom) const
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
        return rect.Standardized().Contains(point) ? L"move" : L"";
    }

    void ObjText::MouseDown(Point point, Modifiers)
    {
        double const now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        m_editRequested = IsCreating() || (state == ObjectState::Move && mouseDownTime > 0 && now - mouseDownTime < 0.5);
        mouseDownTime = now;
        mouseDown = point;
        mouseDownRect = rect;
        m_downRect = rect.Standardized();
        m_downMaxWidth = maxWidth;
        if (IsCreating())
        {
            double const dpr = Environment().dpr;
            Point const inset = Insets(dpr);
            rect = { std::round(point.x - inset.x), std::round(point.y - inset.y - LineHeight(dpr) / 2), 0, 0 };
            Layout(dpr);
        }
    }

    void ObjText::MouseDragged(Point point, Modifiers modifiers)
    {
        std::wstring_view const s = state;
        double const dx = point.x - mouseDown.x;
        double const dy = point.y - mouseDown.y;
        double const dpr = Environment().dpr;
        double const minimum = FontSize(dpr) * 2 + 2 * Insets(dpr).x;
        if (s == ObjectState::Move)
        {
            double mx = dx, my = dy;
            if (HasFlag(modifiers, Modifiers::Shift))
            {
                (std::fabs(mx) < std::fabs(my) ? mx : my) = 0;
            }
            rect = m_downRect.Offset(std::round(mx), std::round(my));
        }
        else if (s == L"r-resize")
        {
            maxWidth = (std::max)(minimum, m_downRect.width + dx);
            Layout(dpr);
        }
        else if (s == L"l-resize")
        {
            maxWidth = (std::max)(minimum, m_downRect.width - dx);
            Layout(dpr);
            rect.x = m_downRect.Right() - rect.width;
        }
        else if (s == L"point-pointer")
        {
            Rect const b = rect.Standardized();
            double const over[4] = { b.Top() - point.y, point.y - b.Bottom(), b.Left() - point.x, point.x - b.Right() };
            int side = 1;
            for (int i = 0; i < 4; ++i)
            {
                if (over[i] > over[side]) side = i;
            }
            static constexpr wchar_t const* kSides[4] = { L"t", L"b", L"l", L"r" };
            pSide = kSides[side];
            pDist = (std::max)(0.0, over[side]);
            pPerc = side < 2 ? (point.x - b.x) / (std::max)(b.width, 1.0) : (point.y - b.y) / (std::max)(b.height, 1.0);
        }
    }

    void ObjText::MouseUp(Point point, Modifiers modifiers)
    {
        if (!IsCreating())
        {
            MouseDragged(point, modifiers);
        }
    }

    std::vector<Shape> ObjText::Shapes(double dpr, double) const
    {
        std::vector<Shape> shapes;
        if (nobg)
        {
            if (IsHighlighted() || editing)
            {
                Shape outline;
                outline.path = BoxPath(dpr);
                outline.stroke = true;
                outline.strokePaint = { kHoverBlue, editing ? 0.5 : 1.0 };
                outline.strokeWidth = dpr;
                outline.dashes = editing ? std::vector<double>{ 3 * dpr, 3 * dpr } : std::vector<double>{};
                shapes.push_back(std::move(outline));
            }
            return shapes;
        }
        Paint const paint{ DrawColor(), editing ? 0.3 : 1.0 };
        Shape box;
        box.path = BoxPath(dpr);
        if (HasPointer())
        {
            box.path.Append(PointerPath(dpr));
        }
        box.fill = true;
        box.fillPaint = paint;
        shapes.push_back(std::move(box));
        return shapes;
    }
}
