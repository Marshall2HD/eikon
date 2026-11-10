#include "pch.h"
#include "Core/Objects/FreehandHelper.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Eikon::Core::Freehand
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;

        double Dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
        double Dist2(Point a, Point b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
        bool IsUnset(Point p) { return p.x == kUnset && p.y == kUnset; }
        bool IsNan(Sample const& s) { return std::isnan(s.x) || std::isnan(s.y); }

        // The bump profile.
        double Bump(double t)
        {
            double v = (1.0 - t) * (1.0 - t) * -50.0 + 1.0;
            if (t < 0.9) v = ((t - 0.8) * (t - 0.8)) / 0.02;
            if (t < 0.8) v = ((t - 0.8) * (t - 0.8)) / 0.32;
            if (t < 0.4) v = (t * t) / -0.32 + 1.0;
            return v;
        }

        // One cubic segment of the resampler, returned without its end sample.
        void SegmentInto(Samples& out, Sample const& p0, Sample const& p1, double step)
        {
            double const length = Dist(p0.P(), p1.P()) * 1.2;
            out.push_back(p0);
            if (length == 0 || length < step || step <= 0)
            {
                return;
            }
            double const n = std::trunc(length / step);
            if (n < 1)
            {
                return;
            }
            Point last = p0.P();
            Point const c1 = IsUnset(p0.b) ? p0.P() : p0.b;
            Point const c2 = IsUnset(p1.a) ? p1.P() : p1.a;
            auto const count = static_cast<long long>(n);
            for (long long i = 0; i < count; ++i)
            {
                double const t = static_cast<double>(i) / n;
                double const u = 1.0 - t;
                double const k0 = u * u * u;
                double const k1 = 3.0 * t * u * u;
                double const k2 = 3.0 * t * t * u;
                double const k3 = t * t * t;
                Point const q{ p0.x * k0 + c1.x * k1 + c2.x * k2 + p1.x * k3, p0.y * k0 + c1.y * k1 + c2.y * k2 + p1.y * k3 };
                if (Dist2(q, last) >= step * step)
                {
                    Sample s = At(q);
                    s.weight = p1.weight * t + p0.weight * u;
                    out.push_back(s);
                    last = q;
                }
            }
        }

        struct CapPoint
        {
            double degrees;
            double radius;
        };

        std::vector<CapPoint> CapShape(int style)
        {
            switch (style)
            {
            case 1: return { { -89, 1 }, { -66, 1.03 }, { -35, 1 }, { 35, 1 }, { 66, 1.03 }, { 89, 1 } };
            case 2: return { { -89, 1 }, { -66, 1.06 }, { -45, 1 }, { 0, 0.65 }, { 45, 1 }, { 66, 1.06 }, { 89, 1 } };
            case 3: return { { -89, 1 }, { -66, 1.08 }, { -49, 0.993 }, { -39, 0.666 }, { 0, 0.693 }, { 39, 0.666 }, { 49, 0.993 }, { 66, 1.08 }, { 89, 1 } };
            case 4: return { { -90, 0.99 }, { -90, 0.98 }, { 0, 0 }, { 90, 0.98 }, { 90, 0.99 } };
            case 5: return { { -84, 0.95 }, { -80, 0.8 }, { 0, 0.15 }, { 80, 0.8 }, { 84, 0.945 }, { 87, 0.986 }, { 89.9, 1 } };
            case 6: return { { -20, 1 }, { 20, 1 } };
            default: return { { -89, 1 }, { -66, 1.01 }, { -35, 1 }, { 0, 1 }, { 35, 1 }, { 66, 1.01 }, { 89, 1 } };
            }
        }

        // The cap around `end`, `outward` being the direction away from the stroke.
        void CapInto(Samples& out, Sample const& end, double outward, int style)
        {
            double const hw = end.width * 0.5;
            double const degrees = outward * 180.0 / 3.1415926;
            for (auto const& c : CapShape(style))
            {
                double const a = (degrees + c.degrees) * 3.1415926 / 180.0;
                Sample s = At({ end.x + hw * c.radius * std::cos(a), end.y + hw * c.radius * std::sin(a) });
                s.width = end.width;
                s.weight = end.weight;
                out.push_back(s);
            }
        }

        void Side(Samples& left, Samples& right, Sample const& p, Point d)
        {
            if (std::fabs(d.x) < 1e-6 && std::fabs(d.y) < 1e-6)
            {
                return;
            }
            double const len = (std::max)(std::hypot(d.x, d.y), 1e-6);
            double const hw = p.width * 0.5;
            double const nx = hw * d.y / len;
            double const ny = -(hw * d.x) / len;
            Sample l = At({ p.x - nx, p.y - ny });
            Sample r = At({ p.x + nx, p.y + ny });
            l.weight = r.weight = p.weight;
            l.speed = r.speed = p.speed;
            l.width = r.width = hw * 2;
            left.push_back(l);
            right.push_back(r);
        }
    }

    Sample At(Point p)
    {
        Sample s;
        s.x = p.x;
        s.y = p.y;
        return s;
    }

    void Measure(Samples& samples)
    {
        if (samples.empty())
        {
            return;
        }
        double cum = samples.front().cumLen;
        Point prev = samples.front().P();
        for (auto& s : samples)
        {
            double const d = Dist(prev, s.P());
            s.speed = d;
            s.len = d;
            cum += d;
            s.cumLen = cum;
            prev = s.P();
        }
    }

    double Ease(double t, int profile)
    {
        switch (profile)
        {
        case 1: return std::cos(t * 6.283185307179586) * 0.5 + 0.5;
        case 2: return (t + t) - t * t;
        case 3: return (t * t - (t + t)) + 1.0;
        case 4: return (t * t - t) * -4.0;
        case 5:
        {
            double const v = t + t - 1.0;
            double const r = 1.0 - v * v;
            return r < 0 ? 0 : std::sqrt(r);
        }
        case 6: return Bump(t);
        default: return t;
        }
    }

    void SetControls(Samples& s)
    {
        std::size_t const n = s.size();
        if (n < 2)
        {
            return;
        }
        for (std::size_t i = 1; i + 1 < n; ++i)
        {
            Point const prev = s[i - 1].P();
            Point const p = s[i].P();
            Point const next = s[i + 1].P();
            double const l1 = Dist(prev, p);
            double const l2 = Dist(next, p);
            double const cosine = ((prev.x - p.x) * (next.x - p.x) + (prev.y - p.y) * (next.y - p.y)) / l1 / l2;
            double t = 0.09999999999999999;
            if (cosine <= -0.1)
            {
                t = (1.0 - s[i].weight) * 0.040000000000000036 + 0.3;
            }
            double const dx = next.x - prev.x;
            double const dy = next.y - prev.y;
            double const dl = std::hypot(dx, dy);
            if (dl == 0)
            {
                s[i].a = p;
                s[i].b = p;
                continue;
            }
            s[i].b = { p.x + dx * (l2 * t) / dl, p.y + dy * (l2 * t) / dl };
            s[i].a = { p.x - dx * (l1 * t) / dl, p.y - dy * (l1 * t) / dl };
        }
        s.front().a = s.front().P();
        s.back().b = s.back().P();
        if (n < 3)
        {
            s.front().b = { (s[0].x + s[1].x) * 0.5, (s[0].y + s[1].y) * 0.5 };
            s.back().a = { (s[n - 2].x + s[n - 1].x) * 0.5, (s[n - 2].y + s[n - 1].y) * 0.5 };
        }
        else
        {
            s.front().b = { s[1].a.x * 0.2 + s[0].x * 0.8, s[0].y * 0.8 + s[1].a.y * 0.2 };
            s.back().a = { s[n - 1].x * 0.8 + s[n - 2].b.x * 0.2, s[n - 1].y * 0.8 + s[n - 2].b.y * 0.2 };
        }
    }

    Samples Resample(Samples const& input, double step)
    {
        Samples pts;
        pts.reserve(input.size());
        for (auto const& s : input)
        {
            if (!IsNan(s))
            {
                pts.push_back(s);
            }
        }
        if (pts.size() < 2)
        {
            return pts;
        }
        for (auto& s : pts)
        {
            if (IsUnset(s.a)) s.a = s.P();
            if (IsUnset(s.b)) s.b = s.P();
        }
        Samples out;
        for (std::size_t i = 1; i < pts.size(); ++i)
        {
            SegmentInto(out, pts[i - 1], pts[i], step);
        }
        out.push_back(pts.back());
        out.front().b = pts.front().b;
        return out;
    }

    Samples Simplify(Samples const& s, double distance)
    {
        std::size_t const n = s.size();
        if (n < 3)
        {
            return s;
        }
        double const d2 = (distance * 0.33) * (distance * 0.33);
        Samples out;
        out.push_back(s.front());
        Point last = s.front().P();
        for (std::size_t i = 1; i < n; ++i)
        {
            if (d2 < Dist2(last, s[i].P()))
            {
                out.push_back(s[i]);
                last = s[i].P();
            }
        }
        if (out.size() < 2 || d2 <= Dist2(last, s.back().P()))
        {
            out.push_back(s.back());
        }
        else
        {
            out.back() = s.back();
        }
        return out;
    }

    Samples Outline(Samples s, double width, int capStyle)
    {
        std::size_t const n = s.size();
        if (n < 2)
        {
            return s;
        }
        for (auto& p : s)
        {
            if (std::isnan(p.weight))
            {
                p.weight = 0.5;
            }
        }
        Samples left;
        Samples right;
        Side(left, right, s[0], { s[1].x - s[0].x, s[1].y - s[0].y });
        for (std::size_t i = 1; i + 1 < n; ++i)
        {
            Sample const& prev = s[i - 1];
            Sample const& p = s[i];
            Sample const& next = s[i + 1];
            Side(left, right, p, { next.x - prev.x, next.y - prev.y });
            double const inX = p.x - prev.x;
            double const inY = p.y - prev.y;
            double const l1 = std::hypot(inX, inY);
            double const l2 = Dist(p.P(), next.P());
            if (l1 * l2 == 0)
            {
                continue;
            }
            double const cosine = ((prev.x - p.x) * (next.x - p.x) + (prev.y - p.y) * (next.y - p.y)) / l1 / l2;
            if (!(-0.3 < cosine))
            {
                continue;
            }
            // A sharp corner: a fan of six points around the travel direction on the outer side.
            double const angle = std::atan2(inY, inX);
            bool const toRight = (next.x - p.x) * inY <= (next.y - p.y) * inX;
            for (int k = 1; k < 7; ++k)
            {
                double const a = toRight ? angle + (k / 7.0 - 0.5) * kPi * 0.5 : angle + (k / -7.0 + 0.5) * kPi * 0.5;
                Sample f = At({ p.x + p.width * std::cos(a) * 0.5, p.y + p.width * std::sin(a) * 0.5 });
                f.weight = p.weight;
                f.speed = p.speed;
                f.width = p.width;
                (toRight ? right : left).push_back(f);
            }
        }
        Side(left, right, s[n - 1], { s[n - 1].x - s[n - 2].x, s[n - 1].y - s[n - 2].y });

        std::reverse(left.begin(), left.end());
        Samples const l = Simplify(left, width);
        Samples const r = Simplify(right, width);

        Samples outline = r;
        double const endDirection = std::atan2(s[n - 1].y - s[n - 2].y, s[n - 1].x - s[n - 2].x);
        CapInto(outline, s[n - 1], endDirection, capStyle);
        outline.insert(outline.end(), l.begin(), l.end());
        double const startDirection = std::atan2(s[0].y - s[1].y, s[0].x - s[1].x);
        CapInto(outline, s[0], startDirection, capStyle);
        SetControls(outline);
        return outline;
    }

    Path Emit(Samples const& input, bool close)
    {
        Samples s;
        s.reserve(input.size());
        for (auto const& p : input)
        {
            if (!IsNan(p))
            {
                s.push_back(p);
            }
        }
        Path path;
        if (s.size() < 2)
        {
            return path;
        }
        path.MoveTo(s[0].P());
        for (std::size_t i = 0; i + 1 < s.size(); ++i)
        {
            Point const c1 = IsUnset(s[i].b) ? s[i].P() : s[i].b;
            Point const c2 = IsUnset(s[i + 1].a) ? s[i + 1].P() : s[i + 1].a;
            path.CubicTo(c1, c2, s[i + 1].P());
        }
        if (close)
        {
            path.Close();
        }
        return path;
    }

    void SpeedWeights(Samples& s)
    {
        std::size_t const n = s.size();
        if (n < 2)
        {
            return;
        }
        if (n == 2)
        {
            s[0].weight = 1.0;
            s[1].weight = 1.0;
            return;
        }
        Measure(s);
        double lo = s[0].speed;
        double hi = s[0].speed;
        for (auto const& p : s)
        {
            lo = (std::min)(lo, p.speed);
            hi = (std::max)(hi, p.speed);
        }
        double const range = hi - lo;
        for (auto& p : s)
        {
            p.weight = range > 0 ? 1.0 - (p.speed - lo) / range : 0.5;
        }
        s.front().weight = 0.7;
        s.back().weight = s[n - 2].weight;
    }

    Samples Smooth(Samples const& s, double smoothing, double resolution)
    {
        std::size_t const n = s.size();
        if (n < 2)
        {
            return s;
        }
        double const r = resolution > 0 ? resolution : 1.0;
        Samples out;
        out.reserve(n);
        Point prevOut = s[0].P();
        for (std::size_t i = 0; i < n; ++i)
        {
            Sample const& raw = s[i];
            double const speed = raw.speed;
            double const k = r * speed;
            double const f17 = k / -3.0 + 1.666666;
            double a = smoothing;
            auto blend = [&](double f) { a = (1.0 - (1.0 - smoothing) * (1.0 - smoothing)) * f + smoothing * (1.0 - f); };
            if (1.0 <= f17)
            {
                if (speed != 0 && k < 5.0) blend(0.5);
            }
            else if (0.0 <= f17)
            {
                if (speed != 0 && k < 5.0) blend(f17 * 0.5);
            }
            auto const index = static_cast<long long>(i);
            auto const count = static_cast<long long>(n);
            if (count - 5 < index)
            {
                a = std::pow(a / 1.5, static_cast<double>(index - count + 5));
            }
            double const d = Dist(prevOut, raw.P());
            if (10.0 / r < d)
            {
                a = a * 10.0 / r / d;
            }
            double applied = a;
            if (i + 1 < n)
            {
                Point const next = s[i + 1].P();
                double const l1 = Dist(raw.P(), prevOut);
                double const l2 = Dist(raw.P(), next);
                if (l1 * l2 != 0)
                {
                    double const cosine = ((prevOut.x - raw.x) * (next.x - raw.x) + (prevOut.y - raw.y) * (next.y - raw.y)) / l1 / l2;
                    if (0.3 < cosine) applied = 0;
                    else if (-0.966 <= cosine) applied = a;
                    else applied = a * 1.03;
                }
                else
                {
                    applied = a * 1.03;
                }
            }
            Sample o = raw;
            o.x = prevOut.x * applied + raw.x * (1.0 - applied);
            o.y = prevOut.y * applied + raw.y * (1.0 - applied);
            out.push_back(o);
            prevOut = o.P();
        }
        return out;
    }

    void SmoothWeights(Samples& s, double k)
    {
        if (s.size() < 3)
        {
            return;
        }
        double w = s[0].weight;
        for (auto& p : s)
        {
            w = k * w + (1.0 - k) * p.weight;
            p.weight = w;
        }
    }

    void NormaliseWeights(Samples& s, double lo, double hi, double k)
    {
        if (s.empty())
        {
            return;
        }
        double mx = s[0].weight;
        double mn = s[0].weight;
        for (auto const& p : s)
        {
            mx = (std::max)(mx, p.weight);
            mn = (std::min)(mn, p.weight);
        }
        if (mx == mn)
        {
            for (auto& p : s)
            {
                p.weight = (lo + hi) * 0.5;
            }
            return;
        }
        for (auto& p : s)
        {
            p.weight = (1.0 - k) * p.weight + k * (lo + ((hi - lo) * (p.weight - mn)) / (mx - mn));
        }
    }

    void AssignWidths(Samples& s, double width, double variability)
    {
        std::size_t const n = s.size();
        if (n < 2)
        {
            return;
        }
        double v = variability;
        double const total = s.back().cumLen;
        if ((n != 2 || s[0].weight != s[1].weight) && total != 0 && width > 0)
        {
            double const f = (total / width - 1.0) * 0.25;
            v *= f < 0 ? 0 : (std::min)(f, 1.0);
        }
        for (auto& p : s)
        {
            double const w = std::isnan(p.weight) ? 0.5 : p.weight;
            p.weight = w;
            p.width = width * ((1.0 - v) + v * w + v * w);
        }
    }

    void CircularAverageWeights(Samples& s, int window)
    {
        auto const n = static_cast<long long>(s.size());
        if (n == 0)
        {
            return;
        }
        long long w = (std::min)(static_cast<long long>(window), n - 1);
        long long const half = w / 2;
        std::vector<double> source(s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            source[i] = s[i].weight;
        }
        for (long long i = 0; i < n; ++i)
        {
            double sum = 0;
            long long count = 0;
            for (long long j = i - half; j <= i + half; ++j)
            {
                sum += source[static_cast<std::size_t>(((j % n) + n) % n)];
                ++count;
            }
            s[static_cast<std::size_t>(i)].weight = sum / static_cast<double>(count);
        }
    }

    Samples Reduce(Samples const& s, double epsilon)
    {
        if (s.size() < 3)
        {
            return s;
        }
        std::vector<bool> keep(s.size(), false);
        keep.front() = keep.back() = true;
        std::vector<std::pair<std::size_t, std::size_t>> stack{ { 0, s.size() - 1 } };
        while (!stack.empty())
        {
            auto const [from, to] = stack.back();
            stack.pop_back();
            if (to <= from + 1)
            {
                continue;
            }
            Point const a = s[from].P();
            Point const b = s[to].P();
            double const dx = b.x - a.x;
            double const dy = b.y - a.y;
            double const len = std::hypot(dx, dy);
            double best = -1;
            std::size_t index = from;
            for (std::size_t i = from + 1; i < to; ++i)
            {
                double const d = len == 0 ? Dist(a, s[i].P()) : std::fabs(dy * s[i].x - dx * s[i].y + b.x * a.y - b.y * a.x) / len;
                if (d > best)
                {
                    best = d;
                    index = i;
                }
            }
            if (best > epsilon)
            {
                keep[index] = true;
                stack.push_back({ from, index });
                stack.push_back({ index, to });
            }
        }
        Samples out;
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            if (keep[i])
            {
                out.push_back(s[i]);
                out.back().keep = true;
            }
        }
        return out;
    }

    void StraightenHorizontal(Samples& s)
    {
        if (s.size() < 3)
        {
            return;
        }
        double const x0 = s[0].x;
        double const y0 = s[0].y;
        for (std::size_t i = 1; i < s.size(); ++i)
        {
            double const angle = std::atan2(std::fabs(s[i].y - y0), std::fabs(s[i].x - x0));
            double f = angle / -0.174533 + 1.0;
            f = f < 0 ? 0 : (std::min)(f, 1.0);
            s[i].y = y0 * f + s[i].y * (1.0 - f);
        }
    }

    void MorphEllipse(Samples& s, double strength)
    {
        if (s.size() < 10)
        {
            return;
        }
        double xmin = s[0].x, xmax = s[0].x, ymin = s[0].y, ymax = s[0].y;
        for (auto const& p : s)
        {
            xmin = (std::min)(xmin, p.x);
            xmax = (std::max)(xmax, p.x);
            ymin = (std::min)(ymin, p.y);
            ymax = (std::max)(ymax, p.y);
        }
        double const cx = (xmin + xmax) * 0.5;
        double const cy = (ymin + ymax) * 0.5;
        double const rx = (xmax - xmin) * 0.5;
        if (rx <= 0)
        {
            return;
        }
        double const ratio = ((ymax - ymin) * 0.5) / rx;
        if (ratio <= 0)
        {
            return;
        }
        for (auto& p : s)
        {
            double const dx = p.x - cx;
            double const dy = (p.y - cy) / ratio;
            double const d = (std::max)(std::hypot(dx, dy), 0.1);
            double const tx = cx + (rx * dx) / d;
            double const ty = cy + ratio * ((rx * dy) / d);
            double const mx = (p.x * (1.0 - strength) + tx * strength) - p.x;
            double const my = (p.y * (1.0 - strength) + ty * strength) - p.y;
            p.x += mx;
            p.y += my;
            if (!IsUnset(p.a)) p.a = { p.a.x + mx, p.a.y + my };
            if (!IsUnset(p.b)) p.b = { p.b.x + mx, p.b.y + my };
        }
    }

    void MorphBox(Samples& s, double strength)
    {
        if (s.size() < 10)
        {
            return;
        }
        double xmin = s[0].x, xmax = s[0].x, ymin = s[0].y, ymax = s[0].y;
        for (auto const& p : s)
        {
            xmin = (std::min)(xmin, p.x);
            xmax = (std::max)(xmax, p.x);
            ymin = (std::min)(ymin, p.y);
            ymax = (std::max)(ymax, p.y);
        }
        // Each edge sits at the median of the samples in the middle third of that side (the
        // bounds are split in thirds and each side's samples sorted).
        double const w = xmax - xmin, h = ymax - ymin;
        double const cx = xmin + w / 2, cy = ymin + h / 2;
        std::vector<double> top, bottom, left, right;
        for (auto const& p : s)
        {
            bool const midX = p.x > xmin + w / 3 && p.x < xmax - w / 3;
            bool const midY = p.y > ymin + h / 3 && p.y < ymax - h / 3;
            if (midX) (p.y < cy ? top : bottom).push_back(p.y);
            if (midY) (p.x < cx ? left : right).push_back(p.x);
        }
        auto median = [](std::vector<double>& v, double fallback) {
            if (v.empty()) return fallback;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        double const et = median(top, ymin), eb = median(bottom, ymax), el = median(left, xmin), er = median(right, xmax);
        for (auto& p : s)
        {
            double const dl = std::fabs(p.x - el), dr = std::fabs(er - p.x), dt = std::fabs(p.y - et), db = std::fabs(eb - p.y);
            double const m = (std::min)((std::min)(dl, dr), (std::min)(dt, db));
            Point target = p.P();
            if (m == dl) target.x = el;
            else if (m == dr) target.x = er;
            else if (m == dt) target.y = et;
            else target.y = eb;
            double const mx = (target.x - p.x) * strength;
            double const my = (target.y - p.y) * strength;
            p.x += mx;
            p.y += my;
            if (!IsUnset(p.a)) p.a = { p.a.x + mx, p.a.y + my };
            if (!IsUnset(p.b)) p.b = { p.b.x + mx, p.b.y + my };
        }
    }

    Path StrokePath(std::vector<Point> const& raw, StrokeParams const& params, Samples* centreLine)
    {
        Samples pts;
        pts.reserve(raw.size());
        for (Point p : raw)
        {
            pts.push_back(At(p));
        }
        if (pts.empty())
        {
            return {};
        }
        SpeedWeights(pts);
        double const strength = 1.0 - std::exp2(-static_cast<double>(params.corrections.size()));
        if (params.corrections.find(L'h') != std::wstring_view::npos) StraightenHorizontal(pts);
        if (params.corrections.find(L'c') != std::wstring_view::npos) MorphEllipse(pts, strength);
        if (params.corrections.find(L's') != std::wstring_view::npos) MorphBox(pts, strength);

        // Map from where the points were captured to the object's current rect (resizing scales the stroke).
        Rect const& from = params.rawRect;
        Rect const& to = params.rect;
        double const sx = from.width != 0 ? to.width / from.width : 1.0;
        double const sy = from.height != 0 ? to.height / from.height : 1.0;
        for (auto& p : pts)
        {
            p.x = to.x + (p.x - from.x) * sx;
            p.y = to.y + (p.y - from.y) * sy;
        }

        double const res = params.resolution > 0 ? params.resolution : 1.0;
        double const sm = params.smoothing;
        pts = Smooth(pts, sm, res);
        pts = Smooth(pts, sm * 0.8, res);
        pts = Smooth(pts, sm * 0.6, res);
        pts = Simplify(pts, 10.0 / res);
        SetControls(pts);
        SmoothWeights(pts, 0.65);
        SmoothWeights(pts, 0.65);
        pts = Resample(pts, 2.0 * params.dpr);
        SmoothWeights(pts, 0.65);
        SmoothWeights(pts, 0.65);
        NormaliseWeights(pts, 0, 1.0, 0.3);
        pts = Smooth(pts, sm * 0.6, res);
        pts = Smooth(pts, sm * 0.2, res);
        Measure(pts);
        AssignWidths(pts, params.width, params.variability);
        if (centreLine)
        {
            *centreLine = pts;
        }
        if (pts.size() == 1)
        {
            // A click: a dot of the stroke width.
            Path dot;
            dot.ArcAround(pts[0].P(), params.width * 0.5, 0, 2 * kPi);
            dot.Close();
            return dot;
        }
        return Emit(Outline(pts, params.width, params.capStyle), true);
    }
}
