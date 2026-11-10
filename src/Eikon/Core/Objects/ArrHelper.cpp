#include "pch.h"
#include "Core/Objects/ArrHelper.h"

#include "Core/Objects/FreehandHelper.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace Eikon::Core::ArrHelper
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;

        // The four arrow outlines, as SVG path data in a 100-unit box.
        constexpr std::wstring_view kArrowProfiles[4] = {
            L"M53.7652875,-22.0862065 L99.3160599,-1.02738847 C99.889116,-0.790629559 100.159829,-0.138721055 99.9207148,0.428689928 C99.8066677,0.699320215 99.5893827,0.914464797 99.3160599,1.02738847 L53.7652875,22.0862065 C53.0012127,22.401885 52.1233537,22.0444897 51.8045345,21.2879418 C51.717018,21.0802678 51.6783351,20.8556096 51.6914109,20.6309546 L52.2225383,10.5056801 C52.267371,9.7354128 51.7106733,9.06853969 50.9559232,8.95342719 L50.788311,8.93724569 L1.57924106,7.07345251 C0.479810951,7.02811733 0.00771303772,6.50969906 0.00771303772,5.42249362 L0.00771303772,-5.42249362 C0.00771303772,-6.39307591 0.479810951,-7.02811733 1.57924106,-7.07345251 L50.788311,-8.93724569 C51.5668575,-8.96934918 52.1819275,-9.58389119 52.2228741,-10.3389323 L52.2225383,-10.5056801 L51.6914109,-20.6309546 C51.6437763,-21.4493636 52.2752129,-22.1510503 53.1017641,-22.1982157 C53.3286541,-22.2111627 53.5555474,-22.1728609 53.7652875,-22.0862065",
            L"M66.6199422,-20.4497473 C67.4098289,-20.1253199 76.0424066,-15.6504186 82.7171847,-11.8170792 C89.3919628,-7.98373971 94.2883667,-5.10382968 99.0168535,-1.72767884 C99.8472491,-1.13477443 100.317864,-0.0246084742 99.752824,0.9295622 C99.5762545,1.22773101 99.2831266,1.54423891 99.0168535,1.72789337 C94.7865041,4.64565982 90.1950052,7.50560371 83.7380537,11.1781619 C77.2811023,14.85072 69.6457673,19.0763064 66.4981664,20.4947551 C65.5617448,20.9167483 64.2849599,20.6377095 63.7199195,19.6835389 C63.5676139,19.4263439 63.4755399,19.138906 63.4504165,18.8422011 L62.600339,8.80287021 C62.5136665,7.77927675 61.6537362,6.98675348 60.6108754,6.96935395 L2.51153548,6 C-0.837178493,6 -0.837178493,-6 2.51153548,-6 L60.6108754,-6.96935395 C61.6537362,-6.98675348 62.5136665,-7.77927675 62.600339,-8.80287021 L63.4504165,-18.8422011 C63.5436221,-19.9429503 64.5252959,-20.760875 65.6430464,-20.669087 C65.944334,-20.6443458 66.3024024,-20.5801693 66.6199422,-20.4497473",
            L"M99.6584753,-0.743776003 C100.069251,-0.374612889 100.102985,0.257652804 99.7338224,0.668428947 L99.6971532,0.707106781 L77.2600935,20.8731251 C76.6439293,21.4268697 75.6955308,21.3762687 75.1417861,20.7601045 C75.1028245,20.716751 75.066423,20.6711634 75.0327669,20.6235736 L71.6833728,15.8875304 C71.3089196,15.3580536 71.3171308,14.6478971 71.7037263,14.1272198 L77.1455208,6.79806599 C77.3101381,6.57635502 77.2638545,6.26317408 77.0421436,6.09855677 C76.9559459,6.03455622 76.8514373,6 76.7440776,6 L2,6 C0.8954305,6 2.84217094e-14,5.1045695 2.84217094e-14,4 L2.84217094e-14,-4 C2.84217094e-14,-5.1045695 0.8954305,-6 2,-6 L76.7440776,-6 C77.02022,-6 77.2440776,-6.22385763 77.2440776,-6.5 C77.2440776,-6.6073597 77.2095214,-6.71186834 77.1455208,-6.79806599 L71.7037263,-14.1272198 C71.3171308,-14.6478971 71.3089196,-15.3580536 71.6833728,-15.8875304 L75.0327669,-20.6235736 C75.5111077,-21.2999475 76.4471888,-21.460485 77.1235627,-20.9821443 C77.1552892,-20.9597068 84.6669268,-14.2135841 99.6584753,-0.743776003",
            L"M78.3648697,-15 C79.5276274,-15 80.6597256,-14.6323217 81.594799,-13.9509966 L99.5590887,-0.861627898 C100.041792,-0.509914622 100.143882,0.160969272 99.7871141,0.63683322 C99.7228677,0.722526338 99.6460134,0.798291712 99.5590887,0.861627898 L81.594799,13.9509966 C80.6597256,14.6323217 79.5276274,15 78.3648697,15 L2.17424539,15 C0.973769132,15 0,14.0406102 0,12.8571429 C0,12.6238141 0.0392464699,12.3920517 0.115031274,12.1710074 L3.346886,2.74454163 C3.95723496,0.964315086 3.95723496,-0.964315086 3.346886,-2.74454163 L0.115031274,-12.1710074 C-0.26935705,-13.2921666 0.340976207,-14.5082389 1.47824876,-14.887181 C1.70246987,-14.9618921 1.93756323,-15 2.17424539,-15 L2.17424539,-15",
        };
        // The two counter pins.
        constexpr std::wstring_view kPinProfiles[2] = {
            L"M25,50 C25,36.1928813 36.1928813,25 50,25 C63.8071187,25 75,36.1928813 75,50 C75,69.9607329 55,73.75 52.5,85 C52.0565531,86.9955109 51.0150513,87.5 50,87.5 C48.9849487,87.5 47.9371276,86.9670742 47.5,85 C45,73.75 25,69.998403 25,50",
            L"M49.9987059,88.7457631 C49.9987059,88.7457631 75.2211928,68.1295281 74.9970444,49.9983385 C74.9970444,36.1921374 63.804907,25 49.9987059,25 C36.1925048,25 25.0003675,36.1921374 25.0003675,49.9983385 C24.7508105,68.2337185 49.9987059,88.7457631 49.9987059,88.7457631",
        };

        double Wrap(double a)
        {
            while (a > kPi) a -= 2 * kPi;
            while (a < -kPi) a += 2 * kPi;
            return a;
        }

        // Signed sweep from angle `from` to `to` around a circle that passes `via`.
        double SweepThrough(double from, double via, double to)
        {
            double cw = to - from;
            while (cw < 0) cw += 2 * kPi;
            double viaCw = via - from;
            while (viaCw < 0) viaCw += 2 * kPi;
            return viaCw <= cw ? cw : cw - 2 * kPi;
        }

        // Twice the signed area of the polygon through a path's on-curve points.
        double SignedArea(Path const& path)
        {
            double area = 0;
            Point first{}, prev{};
            bool have = false;
            for (auto const& c : path.commands)
            {
                if (c.kind == PathCommand::Kind::Close || c.kind == PathCommand::Kind::Arc) continue;
                if (!have)
                {
                    first = prev = c.p;
                    have = true;
                    continue;
                }
                area += prev.x * c.p.y - c.p.x * prev.y;
                prev = c.p;
            }
            return area + (prev.x * first.y - first.x * prev.y);
        }

        // The same closed Move/Line/Cubic figure traced the other way round.
        Path Reversed(Path const& path)
        {
            std::vector<PathCommand> segs;
            for (auto const& c : path.commands)
            {
                if (c.kind != PathCommand::Kind::Close) segs.push_back(c);
            }
            Path out;
            if (segs.empty()) return out;
            out.MoveTo(segs.back().p);
            for (std::size_t i = segs.size() - 1; i > 0; --i)
            {
                PathCommand const& c = segs[i];
                Point const to = segs[i - 1].p;
                if (c.kind == PathCommand::Kind::Cubic) out.CubicTo(c.c2, c.c1, to);
                else if (c.kind == PathCommand::Kind::Quad) out.QuadTo(c.c1, to);
                else out.LineTo(to);
            }
            out.Close();
            return out;
        }

        double Damp(double a)
        {
            return a * (((3.1415926 - std::fabs(a)) * -0.15) / 3.1415926 + 1.0);
        }

        // Rotates profile point `p` by `angle` and scales it.
        Point Place(Point anchor, double px, double py, double angle, double scale)
        {
            double const y = -py;
            double const a = std::atan2(y, px);
            double const d = std::hypot(px, y);
            return { anchor.x + d * scale * std::cos(angle + a), anchor.y + d * scale * std::sin(angle + a) };
        }

        template <typename Map>
        Path BuildProfile(std::vector<SvgCommand> const& commands, Map map)
        {
            Path path;
            for (auto const& c : commands)
            {
                switch (c.op)
                {
                case L'M': path.MoveTo(map(c.points[0])); break;
                case L'L': path.LineTo(map(c.points[0])); break;
                case L'Q': path.QuadTo(map(c.points[0]), map(c.points[1])); break;
                case L'C': path.CubicTo(map(c.points[0]), map(c.points[1]), map(c.points[2])); break;
                case L'Z': path.Close(); break;
                default: break;
                }
            }
            path.Close();
            return path;
        }
    }

    std::wstring_view ArrowProfileSvg(int style)
    {
        return kArrowProfiles[std::clamp(style, 0, 3)];
    }

    std::wstring_view PinProfileSvg(int index)
    {
        return kPinProfiles[std::clamp(index, 0, 1)];
    }

    std::vector<SvgCommand> ParseSvgPath(std::wstring_view text)
    {
        std::vector<SvgCommand> commands;
        std::size_t i = 0;
        wchar_t op = 0;
        auto skip = [&] {
            while (i < text.size() && (std::iswspace(text[i]) || text[i] == L','))
            {
                ++i;
            }
        };
        auto number = [&](double& out) {
            skip();
            std::size_t const begin = i;
            if (i < text.size() && (text[i] == L'-' || text[i] == L'+')) ++i;
            while (i < text.size() && (std::iswdigit(text[i]) || text[i] == L'.')) ++i;
            if (i < text.size() && (text[i] == L'e' || text[i] == L'E'))
            {
                ++i;
                if (i < text.size() && (text[i] == L'-' || text[i] == L'+')) ++i;
                while (i < text.size() && std::iswdigit(text[i])) ++i;
            }
            if (i == begin)
            {
                return false;
            }
            out = std::wcstod(std::wstring{ text.substr(begin, i - begin) }.c_str(), nullptr);
            return true;
        };
        while (true)
        {
            skip();
            if (i >= text.size())
            {
                break;
            }
            if (std::iswalpha(text[i]))
            {
                op = static_cast<wchar_t>(std::towupper(text[i]));
                ++i;
                if (op == L'Z')
                {
                    commands.push_back({ L'Z', {} });
                    continue;
                }
            }
            int count = 0;
            switch (op)
            {
            case L'M': case L'L': count = 1; break;
            case L'Q': count = 2; break;
            case L'C': count = 3; break;
            default: return commands;
            }
            SvgCommand command{ op, {} };
            for (int k = 0; k < count; ++k)
            {
                Point p;
                if (!number(p.x) || !number(p.y))
                {
                    return commands;
                }
                command.points.push_back(p);
            }
            commands.push_back(std::move(command));
            if (op == L'M')
            {
                op = L'L'; // implicit lineto after moveto
            }
        }
        return commands;
    }

    std::vector<SvgCommand> const& ArrowProfile(int style)
    {
        static std::vector<SvgCommand> const parsed[4] = {
            ParseSvgPath(kArrowProfiles[0]), ParseSvgPath(kArrowProfiles[1]), ParseSvgPath(kArrowProfiles[2]), ParseSvgPath(kArrowProfiles[3]),
        };
        return parsed[std::clamp(style, 0, 3)];
    }

    std::vector<SvgCommand> const& PinProfile(int index)
    {
        static std::vector<SvgCommand> const parsed[2] = { ParseSvgPath(kPinProfiles[0]), ParseSvgPath(kPinProfiles[1]) };
        return parsed[std::clamp(index, 0, 1)];
    }

    Point CircleCentre(Point a, Point b, Point c)
    {
        double d = c.x * (a.y - b.y) + a.x * (b.y - c.y) + b.x * (c.y - a.y);
        d += d;
        double const sa = a.x * a.x + a.y * a.y;
        double const sb = b.x * b.x + b.y * b.y;
        double const sc = c.x * c.x + c.y * c.y;
        if (!std::isfinite(d) || std::fabs(d) < 1e-09)
        {
            return { (a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0 };
        }
        return { (sa * (b.y - c.y) + sb * (c.y - a.y) + (a.y - b.y) * sc) / d,
                 (sa * (c.x - b.x) + sb * (a.x - c.x) + (b.x - a.x) * sc) / d };
    }

    Path ShapeArrowStraight(Point start, Point delta, double head, double tail, int size, double dpr, int style)
    {
        double const angle = std::atan2(delta.y, delta.x);
        double const k = kHeadScale[std::clamp(size, 0, 9)] * dpr;
        Point const tip{ start.x + delta.x, start.y + delta.y };
        return BuildProfile(ArrowProfile(style), [&](Point p) {
            bool const atTip = p.x > 50.0;
            return Place(atTip ? tip : start, atTip ? p.x - 100.0 : p.x, p.y, angle, k * (atTip ? head : tail));
        });
    }

    Path ShapeArrowCurved(Point start, Point delta, Point knob, double midpa, double head, double tail, int size, double dpr, int style)
    {
        (void)midpa;
        Point const end{ start.x + delta.x, start.y + delta.y };
        Point const centre = CircleCentre(start, knob, end);
        double const radius = std::hypot(start.x - centre.x, start.y - centre.y);
        if (!std::isfinite(radius) || radius <= 0)
        {
            return ShapeArrowStraight(start, delta, head, tail, size, dpr, style);
        }
        auto const& profile = kCurvedProfiles[std::clamp(style, 0, 3)];
        double const s = kHeadScale[std::clamp(size, 0, 9)] * dpr * 0.5;
        double const tailWidth = profile.w1 * tail * s;
        double const headWidth = profile.w2 * head * s;
        double const headLength = head * profile.hs * s;

        double const aS = std::atan2(start.y - centre.y, start.x - centre.x);
        double const aK = std::atan2(knob.y - centre.y, knob.x - centre.x);
        double const aE = std::atan2(end.y - centre.y, end.x - centre.x);
        double const sweep = SweepThrough(aS, aK, aE);
        double const dir = sweep >= 0 ? 1.0 : -1.0;
        double const baseAngle = aE - dir * headLength / radius;
        // The band runs a little into the head so the two figures overlap instead of meeting at a seam.
        double const bandEnd = aE - dir * headLength * 0.7 / radius;
        double const startAngle = aS - dir * (tailWidth * 0.1) / radius;

        Freehand::Samples band;
        for (int i = 0; i <= 50; ++i)
        {
            double const a = startAngle + (bandEnd - startAngle) * (i / 50.0);
            band.push_back(Freehand::At({ centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) }));
        }
        Freehand::Measure(band);
        double const total = band.back().cumLen;
        for (auto& p : band)
        {
            double const t = total > 0 ? p.cumLen / total : 0;
            p.weight = t;
            p.width = tailWidth + (headWidth - tailWidth) * t;
        }
        Path path = Freehand::Emit(Freehand::Outline(band, (headWidth + tailWidth) * 0.5, 5), true);

        // The head: the profile's tip part along the tangent at the head base.
        Point const base{ centre.x + radius * std::cos(baseAngle), centre.y + radius * std::sin(baseAngle) };
        double const tangent = baseAngle + dir * kPi * 0.5;
        double const k = kHeadScale[std::clamp(size, 0, 9)] * dpr * head;
        Point const tip{ base.x + std::cos(tangent) * 50.0 * k, base.y + std::sin(tangent) * 50.0 * k };
        Path const headPath = BuildProfile(ArrowProfile(style), [&](Point p) {
            return Place(tip, (std::max)(p.x, 50.0) - 100.0, p.y, tangent, k);
        });
        // Both figures must wind the same way, or the overlap cancels out under the non-zero rule.
        if ((SignedArea(path) < 0) != (SignedArea(headPath) < 0))
        {
            path = Reversed(path);
        }
        path.Append(headPath);
        return path;
    }

    Path LineArrow(Point start, Point delta, double width, double dpr, bool curved, Point knob, bool startHead, bool endHead)
    {
        Path path;
        double const length = std::hypot(delta.x, delta.y);
        double const angle = std::atan2(delta.y, delta.x);
        double const arm = (std::min)(0.25 * length, 0.2 * dpr + 7.0 * std::sqrt(width) + 0.7 * width);
        double const spread = length < 20.0 ? 0.78539815 : 0.8726646111111112;
        double endCorrection = 0;
        double startCorrection = 0;
        Point s = start;
        Point e{ start.x + delta.x, start.y + delta.y };
        if (curved)
        {
            Point const centre = CircleCentre(s, knob, e);
            double const radius = std::hypot(s.x - centre.x, s.y - centre.y);
            double const aS = std::atan2(s.y - centre.y, s.x - centre.x);
            double const aK = std::atan2(knob.y - centre.y, knob.x - centre.x);
            double const aE = std::atan2(e.y - centre.y, e.x - centre.x);
            double const sweep = SweepThrough(aS, aK, aE);
            path.MoveTo(s);
            path.ArcAround(centre, radius, aS, sweep);
            double const dir = sweep >= 0 ? 1.0 : -1.0;
            endCorrection = Damp(Wrap(aE + dir * kPi * 0.5 - angle));
            startCorrection = Damp(Wrap(aS - dir * kPi * 0.5 - (angle + kPi)));
        }
        else
        {
            // Pixel snapping at dpr 1: half a pixel for odd widths plus half the width's fraction.
            if (dpr == 1.0)
            {
                double offset = (static_cast<long long>(std::round(width)) & 1) ? 0.5 : 0.0;
                offset += (static_cast<double>(static_cast<long long>(width * 100.0) % 100) / 100.0) * 0.5;
                s = { s.x + offset, s.y + offset };
                e = { e.x + offset, e.y + offset };
            }
            path.MoveTo(s);
            path.LineTo(e);
        }
        auto headAt = [&](Point tip, double direction) {
            double const a1 = (3.1415926 - spread) + direction;
            double const a2 = spread + spread + a1;
            path.MoveTo({ tip.x + arm * std::cos(a1), tip.y + arm * std::sin(a1) });
            path.LineTo(tip);
            path.LineTo({ tip.x + arm * std::cos(a2), tip.y + arm * std::sin(a2) });
        };
        if (startHead)
        {
            headAt(s, angle + kPi + startCorrection);
        }
        if (endHead)
        {
            headAt(e, angle + endCorrection);
        }
        return path;
    }

    Path Bullet(Point at, double length, double width)
    {
        Path path;
        double const r = (std::min)(length / 10.0, width / 2.0 + 1.0);
        path.MoveTo({ at.x + r, at.y });
        path.ArcAround(at, r, 0, 2 * kPi);
        path.Close();
        return path;
    }

    Path PinPath(int index, Point centre, double radius, double angle)
    {
        double const k = radius / 25.0;
        double const rot = angle - kPi * 0.5;
        double const c = std::cos(rot);
        double const s = std::sin(rot);
        return BuildProfile(PinProfile(index), [&](Point p) {
            double const x = (p.x - 50.0) * k;
            double const y = (p.y - 50.0) * k;
            return Point{ centre.x + x * c - y * s, centre.y + x * s + y * c };
        });
    }
}
