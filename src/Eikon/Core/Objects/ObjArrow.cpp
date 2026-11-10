#include "pch.h"
#include "Core/Objects/ObjArrow.h"

#include "Core/Objects/ArrHelper.h"
#include "Core/Objects/FreehandHelper.h"
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
        // Width profiles of the hand-drawn strokes ({1, 3, 6}).
        constexpr int kHanddrawnProfiles[3] = { 1, 3, 6 };

        double R(std::vector<double> const& rnd, std::size_t i)
        {
            return i < rnd.size() ? rnd[i] : 0.5;
        }

        int ProfileFor(std::vector<double> const& rnd)
        {
            long long const i = static_cast<long long>(R(rnd, 18) * 2.99);
            return kHanddrawnProfiles[(std::clamp)(i, 0LL, 2LL)];
        }

        // A rotation in y-up space; the same rotation seen on screen is the negative angle here.
        Point TurnUp(Point p, Point centre, double angleUp)
        {
            return Rotate(p, centre, -angleUp);
        }

        Point Lerp(Point a, Point b, double t)
        {
            return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
        }

        // Widths 0.8 w … 1.2 w along the stroke by the chosen profile, outline and emit (shared tail of
        // the hand-drawn arrows).
        Path WidthAndOutline(Freehand::Samples samples, double w, int profile)
        {
            if (samples.size() < 2)
            {
                return {};
            }
            Freehand::Measure(samples);
            double const total = samples.back().cumLen;
            for (auto& s : samples)
            {
                double const t = total > 0 ? s.cumLen / total : 0;
                s.weight = t;
                s.width = w * 0.8 + (w * 1.2 - w * 0.8) * Freehand::Ease(t, profile);
            }
            return Freehand::Emit(Freehand::Outline(std::move(samples), w, profile == 5 ? 6 : 0), true);
        }

        // The two barbs from `tip` towards `toward` (|toward − tip| = H), one stroke
        // b1 → tip → b2, hooked 36° at the tip; `closed` adds the edge between the barb ends.
        Path HanddrawnHead(Point tip, Point toward, double w, std::vector<double> const& rnd, double dpr, bool closed)
        {
            double const vx = toward.x - tip.x;
            double const vy = toward.y - tip.y;
            double const length = std::hypot(vx, vy);
            double const a0 = (std::max)(std::sqrt(w) - 2.0, 0.0);
            double angle1 = (a0 + 9.0 + R(rnd, 13) * 2.0) * kPi / 180.0;
            double angle2 = (a0 + 9.0 + R(rnd, 14) * 2.0) * kPi / 180.0;
            double f1 = R(rnd, 15) * 0.4 + 0.8;
            double f2 = R(rnd, 16) * 0.4 + 0.8;
            if (closed)
            {
                angle1 *= 0.66;
                angle2 = angle1;
                f1 = f2 = 1.0;
            }
            double const base = std::atan2(-vy, vx); // y-up angle of v
            // The angle (deg·π/180) is turned by a further factor π (sincos(angle·π)).
            double const up1 = base + angle1 * kPi;
            double const up2 = base - angle2 * kPi;
            Point const b1{ tip.x + length * f1 * std::cos(up1), tip.y - length * f1 * std::sin(up1) };
            Point const b2{ tip.x + length * f2 * std::cos(up2), tip.y - length * f2 * std::sin(up2) };

            Freehand::Samples s{ Freehand::At(b1), Freehand::At(tip), Freehand::At(b2) };
            s[0].b = TurnUp(Lerp(b1, tip, 0.5), b1, (R(rnd, 19) * 0.1 - 0.08) * kPi);
            s[1].a = TurnUp(Lerp(tip, b1, 0.1), tip, 0.6283185307179586);   // cos 0.80902, sin 0.58779
            s[1].b = TurnUp(Lerp(tip, b2, 0.1), tip, -0.6283185307179586);
            s[2].a = TurnUp(Lerp(b2, tip, 0.5), b2, (R(rnd, 20) * 0.1 - 0.02) * kPi);
            if (closed)
            {
                Point const end = R(rnd, 88) < 0.25 ? Lerp(b2, b1, 0.66) : b1;
                s.push_back(Freehand::At(end));
                s[2].b = TurnUp(Lerp(b2, end, 0.3), b2, 0.4014257279586958);     // 23°: cos 0.92106, sin 0.38942
                s[3].a = TurnUp(Lerp(end, b2, 0.3), end, -0.4014257279586958);
            }
            Freehand::Samples resampled = Freehand::Simplify(Freehand::Resample(s, 2.0 * dpr), dpr);
            return WidthAndOutline(std::move(resampled), w, ProfileFor(rnd));
        }
    }

    Path HanddrawnArrowPath(Point start, Point delta, double w, double head, std::vector<double> const& rnd, double dpr,
                            bool curved, Point knob)
    {
        double const length = std::hypot(delta.x, delta.y);
        if (length <= 0 || w <= 0)
        {
            return {};
        }
        Point const end{ start.x + delta.x, start.y + delta.y };
        double const H = head * std::sqrt(w) * 20.0;
        bool const closedHead = R(rnd, 62) < 0.25;
        double shaft = length - w * 0.8;
        if (closedHead)
        {
            shaft += w * 0.5 - H * 0.89;
        }
        if (R(rnd, 10) < 0.25)
        {
            shaft -= w + w;
        }
        shaft = (std::max)(shaft, 1.0);

        Freehand::Samples samples;
        Point headToward;
        if (curved)
        {
            // A curved arrow follows the circle through the knob: the shaft is the
            // arc shortened by the same amount, and the head looks back along the tangent at the tip.
            Point const centre = ArrHelper::CircleCentre(start, knob, end);
            double const radius = Distance(start, centre);
            double const aS = std::atan2(start.y - centre.y, start.x - centre.x);
            double const aK = std::atan2(knob.y - centre.y, knob.x - centre.x);
            double const aE = std::atan2(end.y - centre.y, end.x - centre.x);
            double sweep = aE - aS;
            {
                double cw = aE - aS;
                while (cw < 0) cw += 2 * kPi;
                double viaCw = aK - aS;
                while (viaCw < 0) viaCw += 2 * kPi;
                sweep = viaCw <= cw ? cw : cw - 2 * kPi;
            }
            double const arcLength = std::fabs(sweep) * radius;
            if (!std::isfinite(radius) || radius <= 0 || arcLength <= 0)
            {
                return HanddrawnArrowPath(start, delta, w, head, rnd, dpr, false, knob);
            }
            double const shaftSweep = sweep * (std::max)(arcLength - (length - shaft), 1.0) / arcLength;
            for (int i = 0; i <= 8; ++i)
            {
                double const a = aS + shaftSweep * (i / 8.0);
                samples.push_back(Freehand::At({ centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) }));
            }
            Freehand::SetControls(samples);
            double const tangent = aE + (sweep >= 0 ? kPi * 0.5 : -kPi * 0.5);
            headToward = { end.x - H * std::cos(tangent), end.y - H * std::sin(tangent) };
        }
        else
        {
            double const ux = delta.x / length;
            double const uy = delta.y / length;
            Point const shaftEnd{ start.x + ux * shaft, start.y + uy * shaft };
            if (R(rnd, 11) >= 0.25)
            {
                samples = { Freehand::At(start), Freehand::At(shaftEnd) };
                samples[0].b = TurnUp(Lerp(start, shaftEnd, 0.33), start, (R(rnd, 11) * 0.06 - 0.03) * kPi);
                samples[1].a = TurnUp(Lerp(shaftEnd, start, 0.33), shaftEnd, (R(rnd, 12) * 0.02 - 0.01) * kPi);
            }
            else
            {
                // Nine points at i/8 of the shaft, jittered by ±(len·0.03·0.125); none at the ends, 0.3 next to them.
                double const j = shaft * 0.03 * 0.125;
                for (int i = 0; i <= 8; ++i)
                {
                    double const jx = (j + j) * R(rnd, (87 + 2 * i) % 100) - j;
                    double const jy = (j + j) * R(rnd, (88 + 2 * i) % 100) - j;
                    double const m = (i == 0 || i == 8) ? 0.0 : (i == 1 || i == 7) ? 0.3 : 1.0;
                    double const d = shaft * (i / 8.0);
                    samples.push_back(Freehand::At({ start.x + ux * d + jx * m, start.y + uy * d - jy * m }));
                }
                Freehand::SetControls(samples);
            }
            headToward = { end.x - ux * H, end.y - uy * H };
        }
        Path path = WidthAndOutline(Freehand::Resample(samples, 2.0 * dpr), w, ProfileFor(rnd));
        path.Append(HanddrawnHead(end, headToward, w, rnd, dpr, closedHead));
        return path;
    }

    Point ObjArrow::CurveKnob() const noexcept
    {
        double const len = std::hypot(rect.width, rect.height);
        double const angle = std::atan2(rect.height, rect.width) + midpa;
        return { rect.x + len * midpd * std::cos(angle), rect.y + len * midpd * std::sin(angle) };
    }

    double ObjArrow::Width(double length, int size, double dpr)
    {
        double width = (std::min)(length / 10.0, ArrHelper::kArrowWidth[std::clamp(size, 0, 9)]) * dpr;
        double const grow = std::sqrt((length / dpr + 550.0) / 600.0);
        if (grow >= 1.5)
        {
            width *= 1.5;
        }
        else if (grow >= 1.0)
        {
            width *= grow;
        }
        if (width > 4.0 && dpr == 1.0)
        {
            width = std::round(width);   // frinta
        }
        return width;
    }

    void ObjArrow::HeadTail(double length, int size, double& head, double& tail)
    {
        head = 1.0;
        tail = 1.0;
        if (length > 520.0)
        {
            tail = (std::max)(0.8, (length - 520.0) / -1000.0 + 1.0);
            head = 1.0 / tail;
        }
        double const hs = ArrHelper::kHeadScale[std::clamp(size, 0, 9)] * 56.0;
        double const limit = hs >= 100.0 ? hs + hs : 200.0;
        if (length < limit)
        {
            head = tail = length / limit;
        }
    }

    std::vector<double> const& ObjArrow::Rnd() const
    {
        if (!seed)
        {
            seed = Random::NewSeed();
        }
        if (m_rnd.empty() || m_rndSeed != *seed)
        {
            m_rnd = Random::Values(*seed, 100);
            m_rndSeed = *seed;
        }
        return m_rnd;
    }

    JsonValue ObjArrow::Dump() const
    {
        JsonValue json = DumpBase();
        SetNumber(json, L"type", type);
        SetNumber(json, L"style", style);
        SetNumber(json, L"midpa", midpa);
        SetNumber(json, L"midpd", midpd);
        if (seed)
        {
            SetNumber(json, L"seed", static_cast<double>(*seed));
        }
        return json;
    }

    bool ObjArrow::Load(JsonValue const& json)
    {
        auto t = GetNumber(json, L"type"), s = GetNumber(json, L"style"), a = GetNumber(json, L"midpa"), d = GetNumber(json, L"midpd");
        if (!t || !s || !a || !d || !LoadBase(json))
        {
            return false;
        }
        type = static_cast<std::int32_t>(*t);
        style = static_cast<std::int32_t>(*s);
        midpa = *a;
        midpd = *d;
        if (auto sd = GetNumber(json, L"seed"))
        {
            seed = static_cast<std::uint64_t>(*sd);
        }
        else
        {
            seed.reset();
        }
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjArrow::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        style = static_cast<std::int32_t>(std::clamp<std::int64_t>(settings.ReadInt(PreferenceKey(L"arrowStyle"), 0), 0, 3));
        type = static_cast<std::int32_t>(settings.ReadInt(PreferenceKey(L"arrowType"), 0));
        if (type != Shape && type != Line && type != Double && type != Handdrawn)
        {
            type = Shape;
        }
        midpa = 0;
        midpd = 0.5;
        if (type == Handdrawn)
        {
            seed = Random::NewSeed();
        }
    }

    void ObjArrow::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"arrowType")
        {
            int v = 0;
            try
            {
                v = std::stoi(std::wstring{ value });
            }
            catch (...)
            {
                return;
            }
            type = v;
            if (type == Handdrawn && !seed)
            {
                seed = Random::NewSeed();
            }
            SavePreference(settings, L"arrowType", std::to_wstring(type));
            return;
        }
        if (name == L"changeStyle")
        {
            if (type == Handdrawn)
            {
                seed = Random::NewSeed();
            }
            else if (type == Shape)
            {
                style = (style + 1) % 4;
                SavePreference(settings, L"arrowStyle", std::to_wstring(style));
            }
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjArrow::Property(std::wstring_view name) const
    {
        if (name == L"arrowType") return std::to_wstring(type);
        if (name == L"arrowStyle") return std::to_wstring(style);
        return ProtoObject::Property(name);
    }

    std::vector<Handle> ObjArrow::Handles() const
    {
        return {
            { L"point-1", Start(), L"cursor-move" },
            { L"point-2", End(), L"cursor-move" },
            { L"point-curve", CurveKnob(), L"cursor-move" },
        };
    }

    std::vector<Handle> ObjArrow::HandlesAt(double zoom, double dpr) const
    {
        auto handles = Handles();
        double const len = std::hypot(rect.width, rect.height);
        if (zoom < 2.0 && len > 0)
        {
            // The end knobs sit 7 points beyond the ends so they don't cover the tip.
            double const ux = rect.width / len * 7.0 * dpr;
            double const uy = rect.height / len * 7.0 * dpr;
            handles[0].position = { handles[0].position.x - ux, handles[0].position.y - uy };
            handles[1].position = { handles[1].position.x + ux, handles[1].position.y + uy };
        }
        return handles;
    }

    std::wstring ObjArrow::HitTest(Point point, double zoom) const
    {
        double const z = zoom > 0 ? zoom : 1.0;
        double const knob = 8.0 / z;
        auto const& env = Environment();
        if (IsSelected())
        {
            for (auto const& h : HandlesAt(z, env.dpr))
            {
                if (Distance(h.position, point) <= knob)
                {
                    return h.name;
                }
            }
        }
        double const len = std::hypot(rect.width, rect.height);
        double const tolerance = (std::max)(Width(len, size, env.dpr) * 0.5 + 2.0, 6.0 / z) +
                                 (type == Shape ? ArrHelper::kHeadScale[std::clamp(size, 0, 9)] * env.dpr * 10.0 : 0.0);
        double distance;
        if (IsCurved())
        {
            Point const centre = ArrHelper::CircleCentre(Start(), CurveKnob(), End());
            double const radius = Distance(Start(), centre);
            // Distance to the circle, limited to the polyline hull of start, knob and end.
            double const toCircle = std::fabs(Distance(point, centre) - radius);
            double const hull = (std::min)(DistanceToSegment(point, Start(), CurveKnob()), DistanceToSegment(point, CurveKnob(), End()));
            distance = hull <= Distance(Start(), End()) * 0.25 ? toCircle : hull;
        }
        else
        {
            distance = DistanceToSegment(point, Start(), End());
        }
        return distance <= tolerance ? L"move" : L"";
    }

    KeyResult ObjArrow::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        KeyResult const base = BaseKeyDown(key, context);
        if (base != KeyResult::Ignored)
        {
            return base;
        }
        if (key.key == Keys::R && HasFlag(key.modifiers, Modifiers::Ctrl) && type == Handdrawn)
        {
            seed = Random::NewSeed();   // "editType: randomize"
            return KeyResult::Handled;
        }
        return KeyResult::Ignored;
    }

    void ObjArrow::MouseDown(Point point, Modifiers modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        m_downRect = rect;
        if (IsCreating())
        {
            rect = { point.x, point.y, 0, 0 };
            reversed = HasFlag(modifiers, Modifiers::Ctrl);
        }
    }

    void ObjArrow::MouseDragged(Point point, Modifiers modifiers)
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
            reversed = HasFlag(modifiers, Modifiers::Ctrl) || reversed;
            Point const p = lock(mouseDown, point);
            rect = { mouseDown.x, mouseDown.y, p.x - mouseDown.x, p.y - mouseDown.y };
        }
        else if (s == ObjectState::Move)
        {
            double dx = point.x - mouseDown.x;
            double dy = point.y - mouseDown.y;
            if (shift)
            {
                (std::fabs(dx) < std::fabs(dy) ? dx : dy) = 0;
            }
            rect = m_downRect.Offset(dx, dy);
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
        else if (s == L"point-curve")
        {
            double const len = std::hypot(rect.width, rect.height);
            if (len > 0)
            {
                midpd = Distance(Start(), point) / len;
                midpa = WrapAngle(std::atan2(point.y - rect.y, point.x - rect.x) - std::atan2(rect.height, rect.width));
            }
        }
    }

    void ObjArrow::MouseUp(Point point, Modifiers modifiers)
    {
        MouseDragged(point, modifiers);
        if (IsCreating() && reversed)
        {
            // The head was drawn at the press point; keep it there for good (the Dump has no `reversed`).
            rect = { rect.x + rect.width, rect.y + rect.height, -rect.width, -rect.height };
            reversed = false;
        }
    }

    std::vector<Core::Shape> ObjArrow::Shapes(double dpr, double zoom) const
    {
        Point start = Start();
        Point delta{ rect.width, rect.height };
        if (reversed)
        {
            start = End();
            delta = { -delta.x, -delta.y };
        }
        if (zoom < 1.0)
        {
            delta.x = 2.0 * std::round(delta.x / 2.0);
        }
        double const len = std::hypot(delta.x, delta.y);
        if (len <= 0)
        {
            return {};
        }
        double const width = Width(len, size, dpr);
        double head = 1, tail = 1;
        HeadTail(len, size, head, tail);
        Point knob{};
        if (IsCurved())
        {
            double const a = std::atan2(delta.y, delta.x) + midpa;
            knob = { start.x + len * midpd * std::cos(a), start.y + len * midpd * std::sin(a) };
        }
        Core::Shape shape;
        switch (type)
        {
        case Line:
        case Double:
            shape.path = ArrHelper::LineArrow(start, delta, width, dpr, IsCurved(), knob, type == Double, true);
            shape.stroke = true;
            shape.strokePaint = { DrawColor(), 1.0 };
            shape.strokeWidth = width;
            shape.cap = Core::Shape::Cap::Square;
            shape.join = Core::Shape::Join::Miter;
            break;
        case Handdrawn:
        {
            double const w = (std::min)(len / 15.0, (ArrHelper::kArrowWidth[std::clamp(size, 0, 9)] * 0.7 + 1.3) * dpr);
            shape.path = HanddrawnArrowPath(start, delta, w, head, Rnd(), dpr, IsCurved(), knob);
            shape.fill = true;
            shape.fillPaint = { DrawColor(), 1.0 };
            break;
        }
        default:
            shape.path = IsCurved() ? ArrHelper::ShapeArrowCurved(start, delta, knob, midpa, head, tail, size, dpr, style)
                                    : ArrHelper::ShapeArrowStraight(start, delta, head, tail, size, dpr, style);
            shape.fill = true;
            shape.fillPaint = { DrawColor(), 1.0 };
            break;
        }
        return { shape };
    }
}
