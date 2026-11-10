#include "pch.h"
#include "Core/Objects/ObjOval.h"

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
        struct OvalNode
        {
            Point p, a, b;   // on-curve point, incoming and outgoing control (unit box, y down)
        };
        // Eight on-curve points with their controls; the 0.22 / 0.1533 / 0.0934 / 0.78 /
        // 0.38 handle constants make a slightly squarish ellipse.
        constexpr OvalNode kOval[8] = {
            { { 0.22, 0.06 }, { 0.1533, 0.0934 }, { 0.38, -0.02 } },
            { { 0.78, 0.06 }, { 0.62, -0.02 }, { 0.8467, 0.0934 } },
            { { 0.94, 0.22 }, { 0.9066, 0.1533 }, { 1.02, 0.38 } },
            { { 0.94, 0.78 }, { 1.02, 0.62 }, { 0.9066, 0.8467 } },
            { { 0.78, 0.94 }, { 0.8467, 0.9066 }, { 0.62, 1.02 } },
            { { 0.22, 0.94 }, { 0.38, 1.02 }, { 0.1533, 0.9066 } },
            { { 0.06, 0.78 }, { 0.0934, 0.8467 }, { -0.02, 0.62 } },
            { { 0.06, 0.22 }, { -0.02, 0.38 }, { 0.0934, 0.1533 } },
        };

        // Coefficients near an edge are pulled towards it by k on the long axis.
        double Coefficient(double c, double k)
        {
            return c < 0.5 ? k * c : 1.0 - k * (1.0 - c);
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

        // A hand-drawn ribbon along `samples`: weights 1 + 12·rnd[(84 + i) % 100] +
        // cos(9.42·i/n), circularly averaged (41) four times, normalised to 0…1, widths with `variability`.
        Path Ribbon(Freehand::Samples samples, double stroke, double variability, std::vector<double> const& rnd)
        {
            if (samples.size() < 2)
            {
                return {};
            }
            std::size_t const n = samples.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                samples[i].weight = 1.0 + rnd[(84 + i) % rnd.size()] * 12.0 + std::cos(9.42 * static_cast<double>(i) / static_cast<double>(n));
            }
            for (int k = 0; k < 4; ++k)
            {
                Freehand::CircularAverageWeights(samples, 41);
            }
            Freehand::NormaliseWeights(samples, 0.0, 1.0, 1.0);
            Freehand::Measure(samples);
            Freehand::AssignWidths(samples, stroke, variability);
            return Freehand::Emit(Freehand::Outline(std::move(samples), stroke, 0), true);
        }

        // 1000 samples of the box's ellipse as an inner ellipse offset by half the short
        // side, from angle rnd[37]·6.28; with `spiral` the sweep is 2π(0.98 + 0.06·rnd[8]) and the offset
        // drifts by ±1.3·stroke so the ends overlap like a pen stroke. Then RDP (dpr/8) and controls.
        Freehand::Samples HanddrawnOvalSamples(Rect const& b, double stroke, bool spiral, std::vector<double> const& rnd, double dpr)
        {
            double const W = b.width, H = b.height;
            double const m = (std::min)(W, H) * 0.5;
            double const a = (W - m) * 0.5;
            double const ratio = (H - m) / (W - m);
            double const sweep = spiral ? (rnd[8] * 0.06 + 0.98) * 2.0 * 3.1415926 : 6.2831852;
            double const drift = spiral ? stroke * 2.6 : 0.0;
            Point const c = b.Center();
            Freehand::Samples s;
            s.reserve(1000);
            for (int i = 0; i < 1000; ++i)
            {
                double const t = i / 999.0;
                double const theta = rnd[37] * 6.28 + sweep * t;
                double const cs = std::cos(theta);
                double const sn = -std::sin(theta);   // y-up angle
                double const r = m * 0.5 + drift * (t - 0.5);
                double const q = std::sqrt(cs * cs + (sn * sn) / (ratio * ratio));
                s.push_back(Freehand::At({ c.x + a * cs + r * (cs / q), c.y + a * ratio * sn + r * (sn / (ratio * q)) }));
            }
            s = Freehand::Reduce(s, dpr * 0.125);
            Freehand::SetControls(s);
            return s;
        }

        // Jitter 1.3: the rounded-rect outline as a
        // pen stroke from the top-left corner, every corner nudged by up to ±1.3·dpr with rnd[56…63], the
        // last edge running a little past the start.
        Freehand::Samples HanddrawnRectSamples(Rect const& b, double radius, std::vector<double> const& rnd, double dpr)
        {
            double const r = (std::max)(0.0, (std::min)({ radius, b.width / 2, b.height / 2 }));
            double const l = b.Left(), t = b.Top(), rt = b.Right(), bt = b.Bottom();
            Point corners[4] = { { rt - r, t + r }, { rt - r, bt - r }, { l + r, bt - r }, { l + r, t + r } };
            for (int k = 0; k < 4; ++k)
            {
                corners[k].x += (rnd[56 + 2 * k] - 0.5) * 2.0 * 1.3 * dpr;
                corners[k].y += (rnd[57 + 2 * k] - 0.5) * 2.0 * 1.3 * dpr;
            }
            Freehand::Samples s;
            auto arc = [&](Point centre, double start) {
                for (int i = 0; i <= 4; ++i)
                {
                    double const a = start + (kPi / 2) * (i / 4.0);
                    s.push_back(Freehand::At({ centre.x + r * std::cos(a), centre.y + r * std::sin(a) }));
                }
            };
            arc(corners[0], -kPi / 2);
            arc(corners[1], 0);
            arc(corners[2], kPi / 2);
            arc(corners[3], kPi);
            // Overshoot along the top edge.
            Point const first = s.front().P();
            Point const second{ corners[0].x, corners[0].y - r };
            s.push_back(Freehand::At({ first.x + (second.x - first.x) * 0.15, first.y + (second.y - first.y) * 0.15 }));
            Freehand::SetControls(s);
            return Freehand::Resample(s, 2.0 * dpr);
        }

        Point BoundaryPoint(Rect const& box, bool oval, Point towards)
        {
            Point const c = box.Center();
            double const ux = towards.x - c.x, uy = towards.y - c.y;
            double const d = std::hypot(ux, uy);
            if (d <= 0) return c;
            double const nx = ux / d, ny = uy / d;
            double const a = box.width / 2, b = box.height / 2;
            double t;
            if (oval)
            {
                t = 1.0 / std::sqrt((nx / a) * (nx / a) + (ny / b) * (ny / b));
            }
            else
            {
                t = (std::min)(nx != 0 ? a / std::fabs(nx) : 1e300, ny != 0 ? b / std::fabs(ny) : 1e300);
            }
            return { c.x + nx * t, c.y + ny * t };
        }
    }

    // ---------------- ObjOval ----------------

    double ObjOval::WidthAt(int index) const
    {
        return Widths()[std::clamp(index, 0, 9)];
    }

    double ObjOval::StrokeWidth(double dpr) const
    {
        Rect const b = rect.Standardized();
        double const minSide = (std::min)(b.width, b.height);
        return (std::min)(minSide / 10.0, WidthAt(size)) * dpr;
    }

    std::vector<double> const& ObjOval::Rnd() const
    {
        std::uint64_t const s = seed != 0 ? seed : 1;
        if (m_rnd.empty() || m_rndSeed != s)
        {
            m_rnd = Random::Values(s, 100);
            m_rndSeed = s;
        }
        return m_rnd;
    }

    Path ObjOval::OutlinePath(Rect const& box, double dpr, double zoom) const
    {
        Rect const b = box.Standardized();
        if (shape == L"rect")
        {
            double const minSide = (std::min)(b.width, b.height);
            double const w = WidthAt(size);
            double const radius = w == 0 ? 12.0 * dpr : (std::min)(minSide / 3.0, 2.0 * w * dpr);
            return RoundedRect(b, radius);
        }
        double W = b.width;
        double const H = b.height;
        if (zoom < 1.0)
        {
            W = 2.0 * std::trunc(W * 0.5);
        }
        double const r = H > 0 ? b.width / H : 1.0;
        double const kx = r <= 1.0 ? 1.0 : (H / b.width) * 0.75 + 0.25;
        double const ky = r >= 1.0 ? 1.0 : r * 0.75 + 0.25;
        auto map = [&](Point u) { return Point{ b.x + W * Coefficient(u.x, kx), b.y + H * Coefficient(u.y, ky) }; };
        Path path;
        path.MoveTo(map(kOval[0].p));
        for (int i = 0; i < 8; ++i)
        {
            OvalNode const& from = kOval[i];
            OvalNode const& to = kOval[(i + 1) % 8];
            path.CubicTo(map(from.b), map(to.a), map(to.p));
        }
        path.Close();
        return path;
    }

    JsonValue ObjOval::Dump() const
    {
        JsonValue json = DumpBase();
        SetBool(json, L"showBackground", showBackground);
        SetBool(json, L"showBorder", showBorder);
        SetNumber(json, L"backgroundAlpha", backgroundAlpha);
        SetString(json, L"shape", shape);
        return json;
    }

    bool ObjOval::Load(JsonValue const& json)
    {
        auto bg = GetBool(json, L"showBackground"), border = GetBool(json, L"showBorder");
        auto alpha = GetNumber(json, L"backgroundAlpha");
        auto sh = GetString(json, L"shape");
        if (!bg || !border || !alpha || !sh || !LoadBase(json))
        {
            return false;
        }
        showBackground = *bg;
        showBorder = *border;
        backgroundAlpha = *alpha;
        shape = *sh;
        // handdrawn and seed are not in the Dump: the shape comes back as the tool default with a new seed.
        handdrawn = AllowsHanddrawn() && Settings::Current().ReadString(PreferenceKey(L"handdrawn"), L"true") == L"true";
        seed = Random::NewSeed();
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    void ObjOval::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        showBorder = true;
        showBackground = false;
        backgroundAlpha = 0.3;
        handdrawn = AllowsHanddrawn() && settings.ReadString(PreferenceKey(L"handdrawn"), L"true") == L"true";
        seed = Random::NewSeed();
    }

    void ObjOval::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"fill")
        {
            std::optional<double> parsed;
            try
            {
                std::size_t used = 0;
                double const v = std::stod(std::wstring{ value }, &used);
                if (used == value.size()) parsed = v;
            }
            catch (...)
            {
            }
            double alpha = parsed ? (std::max)(*parsed / 100.0, 0.0) : 0.01;
            backgroundAlpha = alpha;
            showBackground = alpha > 0;
            showBorder = true;
            handdrawn = parsed && *parsed < 0;
            SavePreference(settings, L"handdrawn", handdrawn ? L"true" : L"false");
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjOval::Property(std::wstring_view name) const
    {
        if (name == L"fill")
        {
            if (handdrawn) return L"-1";
            if (showBackground && backgroundAlpha != 0) return backgroundAlpha == 1.0 ? L"100" : L"40";
            return L"0";
        }
        if (name == L"shape") return shape;
        return ProtoObject::Property(name);
    }

    std::vector<Handle> ObjOval::Handles() const
    {
        return BoxHandles(rect);
    }

    std::wstring ObjOval::HitTest(Point point, double zoom) const
    {
        if (IsSelected())
        {
            return HitBox(rect, point, zoom);
        }
        double const r = 4.0 / (zoom > 0 ? zoom : 1.0);
        Rect const b = rect.Standardized();
        return (point.x >= b.Left() - r && point.x <= b.Right() + r && point.y >= b.Top() - r && point.y <= b.Bottom() + r) ? L"move" : L"";
    }

    void ObjOval::ToggleKeys(KeyEvent const& key, KeyResult& result)
    {
        bool const ctrl = HasFlag(key.modifiers, Modifiers::Ctrl);
        if (key.key == Keys::R && ctrl && handdrawn)
        {
            seed = Random::NewSeed();   // "editType: randomize"
            result = KeyResult::Handled;
            return;
        }
        if (ctrl)
        {
            return;
        }
        if (key.key == Keys::B)
        {
            if (!showBorder || showBackground)
            {
                showBorder = !showBorder;
            }
            result = KeyResult::Handled;
        }
        else if (key.key == Keys::F)
        {
            if (!showBackground || showBorder)
            {
                showBackground = !showBackground;
            }
            result = KeyResult::Handled;
        }
        else if (int const digit = Keys::Digit(key.key); digit >= 0)
        {
            showBackground = true;
            backgroundAlpha = digit == 0 ? 1.0 : digit / 10.0;
            result = KeyResult::Handled;
        }
    }

    KeyResult ObjOval::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        Rect const before = rect;
        KeyResult result = BaseKeyDown(key, context);
        if (result == KeyResult::Ignored)
        {
            ToggleKeys(key, result);
        }
        if (rect.width <= 0 && rect.height <= 0)
        {
            rect = before;
        }
        return result;
    }

    void ObjOval::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (IsCreating())
        {
            rect = { std::round(point.x), std::round(point.y), 0, 0 };
        }
    }

    void ObjOval::MouseDragged(Point point, Modifiers modifiers)
    {
        DragBox(point, modifiers);
    }

    std::vector<Shape> ObjOval::Shapes(double dpr, double zoom) const
    {
        Rect const b = rect.Standardized();
        if (b.width <= 0 || b.height <= 0)
        {
            return {};
        }
        double const minSide = (std::min)(b.width, b.height);
        double const stroke = StrokeWidth(dpr);
        Color const c = DrawColor();
        std::vector<Shape> shapes;
        Path const outline = OutlinePath(b, dpr, zoom);
        if (showBackground)
        {
            Shape fill;
            fill.path = outline;
            fill.fill = true;
            fill.fillPaint = { color, backgroundAlpha };
            shapes.push_back(std::move(fill));
        }
        if (showBorder && stroke > 0)
        {
            if (handdrawn && AllowsHanddrawn() && minSide > 40.0)
            {
                Shape ribbon;
                if (shape == L"rect")
                {
                    double const w = WidthAt(size);
                    double const r0 = w == 0 ? 12.0 : w * 6.0;
                    double const variability = (std::min)((std::max)(stroke / -150.0 + 0.3, 0.08), 0.3);
                    ribbon.path = Ribbon(HanddrawnRectSamples(b, r0 * 1.1 + r0 * r0 * -0.004, Rnd(), dpr), stroke, variability, Rnd());
                }
                else
                {
                    ribbon.path = Ribbon(HanddrawnOvalSamples(b, stroke, stroke * 5.0 < minSide, Rnd(), dpr), stroke, 0.25, Rnd());
                }
                ribbon.fill = true;
                ribbon.fillPaint = { c, 1.0 };
                shapes.push_back(std::move(ribbon));
            }
            else
            {
                Shape border;
                border.path = outline;
                border.stroke = true;
                border.strokePaint = { c, 1.0 };
                border.strokeWidth = stroke;
                border.join = Shape::Join::Round;
                shapes.push_back(std::move(border));
            }
        }
        if (IsHighlighted())
        {
            Shape hover;
            hover.path = outline;
            hover.stroke = true;
            hover.strokePaint = { kHoverBlue, 1.0 };
            hover.strokeWidth = (std::max)(stroke, 2.0 * dpr);
            hover.join = Shape::Join::Round;
            shapes.push_back(std::move(hover));
        }
        return shapes;
    }

    // ---------------- ObjRectangle ----------------

    void ObjRectangle::ApplyDefaults(Settings& settings)
    {
        ObjOval::ApplyDefaults(settings);
        shape = L"rect";
    }

    // ---------------- ObjSpotlight ----------------

    void ObjSpotlight::ApplyDefaults(Settings& settings)
    {
        ObjOval::ApplyDefaults(settings);
        wantsSpotlight = true;
        handdrawn = false;
        shape = settings.ReadString(PreferenceKey(L"shape"), L"rect") == L"oval" ? L"oval" : L"rect";
    }

    double ObjSpotlight::Transparency(Settings& settings)
    {
        double const v = settings.ReadDouble(L"Eikon.ObjSpotlight: transparency", kDefaultTransparency);
        return std::isfinite(v) ? v : kDefaultTransparency;
    }

    KeyResult ObjSpotlight::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        KeyResult result = ObjOval::KeyDown(key, context);
        showBorder = true;
        showBackground = false;
        if (HasFlag(key.modifiers, Modifiers::Ctrl))
        {
            return result;
        }
        int const digit = Keys::Digit(key.key);
        if (digit > 0)
        {
            Settings::Current().WriteString(PreferenceKey(L"transparency"), std::to_wstring(digit / 14.0));
            result = KeyResult::Handled;
        }
        return result;
    }

    void ObjSpotlight::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"shape")
        {
            // Toggles rect ↔ oval; the options bar passes the segment's code, so a code
            // picks that shape and an empty value toggles.
            if (value == L"rect" || value == L"oval") shape = std::wstring{ value };
            else shape = shape == L"oval" ? L"rect" : L"oval";
            SavePreference(settings, L"shape", shape);
            return;
        }
        ObjOval::SetProperty(name, value);
    }

    void ObjSpotlight::MouseUp(Point point, Modifiers modifiers)
    {
        ObjOval::MouseUp(point, modifiers);
        auto const& env = Environment();
        if (env.image.width > 0 && env.image.height > 0)
        {
            ClipInside(env.image);
        }
    }

    std::vector<Shape> ObjSpotlight::Shapes(double dpr, double zoom) const
    {
        // The cut-out itself is the curtain's business (Editor::Render draws one curtain for all
        // spotlights); the object draws only its optional border.
        Rect const b = rect.Standardized();
        if (b.width <= 0 || b.height <= 0)
        {
            return {};
        }
        std::vector<Shape> shapes;
        double const w = WidthAt(size) * dpr;
        if (w > 0 || IsHighlighted())
        {
            Shape border;
            border.path = OutlinePath(b, dpr, zoom);
            border.stroke = true;
            border.strokePaint = { DrawColor(), 1.0 };
            border.strokeWidth = IsHighlighted() ? (std::max)(w, 2.0 * dpr) : w;
            border.join = Shape::Join::Round;
            shapes.push_back(std::move(border));
        }
        return shapes;
    }

    // ---------------- ObjMagnifier ----------------

    Rect ObjMagnifier::LensRect() const
    {
        Rect const b = rect.Standardized();
        double const w = b.width * factor;
        double const h = b.height * factor;
        Point const c{ b.x + b.width / 2 + lensX * b.width, b.y + b.height / 2 - lensY * b.height };
        return { c.x - w / 2, c.y - h / 2, w, h };
    }

    JsonValue ObjMagnifier::Dump() const
    {
        JsonValue json = ObjOval::Dump();
        SetNumber(json, L"lensX", lensX);
        SetNumber(json, L"lensY", lensY);
        SetNumber(json, L"factor", factor);
        return json;
    }

    bool ObjMagnifier::Load(JsonValue const& json)
    {
        auto x = GetNumber(json, L"lensX"), y = GetNumber(json, L"lensY"), f = GetNumber(json, L"factor");
        if (!x || !y || !f || !ObjOval::Load(json))
        {
            return false;
        }
        lensX = *x;
        lensY = *y;
        factor = *f;
        handdrawn = false;
        return true;
    }

    void ObjMagnifier::ApplyDefaults(Settings& settings)
    {
        ObjOval::ApplyDefaults(settings);
        handdrawn = false;
        showBorder = true;
        showBackground = false;
        factor = 3;
        lensX = 2.2;
        lensY = -1.4;
        shape = settings.ReadString(PreferenceKey(L"shape"), L"oval") == L"rect" ? L"rect" : L"oval";
    }

    void ObjMagnifier::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        Settings& settings = Settings::Current();
        if (name == L"shape")
        {
            shape = value == L"rect" ? L"rect" : L"oval";
            SavePreference(settings, L"shape", shape);
            return;
        }
        ObjOval::SetProperty(name, value);
    }

    std::wstring ObjMagnifier::Property(std::wstring_view name) const
    {
        if (name == L"shape") return shape;
        return ObjOval::Property(name);
    }

    std::vector<Handle> ObjMagnifier::Handles() const
    {
        auto handles = BoxHandles(rect);
        Rect const lens = LensRect();
        handles.push_back({ L"lens-h-resize", { lens.Right(), lens.y + lens.height / 2 }, L"cursor-l-resize" });
        return handles;
    }

    std::wstring ObjMagnifier::HitTest(Point point, double zoom) const
    {
        double const z = zoom > 0 ? zoom : 1.0;
        if (IsSelected())
        {
            Rect const lens = LensRect();
            if (Distance(point, { lens.Right(), lens.y + lens.height / 2 }) <= 8.0 / z)
            {
                return L"lens-h-resize";
            }
            if (std::wstring const hit = HitBox(rect, point, zoom); !hit.empty())
            {
                return hit;
            }
        }
        if (LensRect().Contains(point))
        {
            return L"move-lens";
        }
        return rect.Standardized().Inset(-4.0 / z, -4.0 / z).Contains(point) ? L"move" : L"";
    }

    void ObjMagnifier::MouseDown(Point point, Modifiers modifiers)
    {
        ObjOval::MouseDown(point, modifiers);
        m_downRect = rect;
        m_downLensX = lensX;
        m_downLensY = lensY;
    }

    void ObjMagnifier::MouseDragged(Point point, Modifiers modifiers)
    {
        Rect const b = m_downRect.Standardized();
        if (state == L"move-lens")
        {
            if (b.width > 0 && b.height > 0)
            {
                lensX = m_downLensX + (point.x - mouseDown.x) / b.width;
                lensY = m_downLensY - (point.y - mouseDown.y) / b.height;
            }
            return;
        }
        if (state == L"lens-h-resize")
        {
            if (b.width > 0)
            {
                Rect const lens = LensRect();
                double const ratio = 2.0 * std::fabs(point.x - (lens.x + lens.width / 2)) / b.width;
                factor = (std::max)(2.0, std::floor(ratio));
            }
            return;
        }
        // The source stays square while it is created.
        DragBox(point, IsCreating() ? (modifiers | Modifiers::Shift) : modifiers);
    }

    std::vector<Shape> ObjMagnifier::Shapes(double dpr, double zoom) const
    {
        Rect const source = rect.Standardized();
        if (source.width <= 0 || source.height <= 0)
        {
            return {};
        }
        Rect const lens = LensRect();
        double const w = WidthAt(size) * dpr;
        Color const c = DrawColor();
        bool const oval = shape != L"rect";
        auto stroke = [&](Path path) {
            Shape s;
            s.path = std::move(path);
            s.stroke = true;
            s.strokePaint = { c, 1.0 };
            s.strokeWidth = IsHighlighted() ? (std::max)(w, 2.0 * dpr) : w;
            s.join = Shape::Join::Round;
            s.cap = Shape::Cap::Round;
            return s;
        };
        std::vector<Shape> shapes;
        Point const a = BoundaryPoint(source, oval, lens.Center());
        Point const z = BoundaryPoint(lens, oval, source.Center());
        if (!lens.Intersects(source))
        {
            Path line;
            line.MoveTo(a);
            line.LineTo(z);
            shapes.push_back(stroke(std::move(line)));
        }
        shapes.push_back(stroke(OutlinePath(source, dpr, zoom)));
        shapes.push_back(stroke(OutlinePath(lens, dpr, zoom)));
        return shapes;
    }
}
