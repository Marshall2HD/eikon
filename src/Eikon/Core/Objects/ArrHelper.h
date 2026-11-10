#pragma once

// ArrHelper: the SVG outline profiles of the "shape" arrow and of the
// counter pins, their parser, and the arrow geometry shared by
// ObjArrow and ObjLine. Image pixels, y down.

#include "Core/Objects/Path.h"

#include <array>
#include <string_view>
#include <vector>

namespace Eikon::Core::ArrHelper
{
    // Size tables, indexed by `size`.
    inline constexpr std::array<double, 10> kHeadScale{ 0.33, 0.5, 0.7, 0.9, 1.1, 1.4, 2.0, 2.8, 4.2, 6.4 };       // T1
    inline constexpr std::array<double, 10> kArrowWidth{ 1, 2, 3, 4, 6, 10, 14, 20, 28, 40 };                       // T2
    // arrProfiles (static): (w1 tail width, w2 shaft-at-head width, hs head length).
    struct CurvedProfile
    {
        double w1;
        double w2;
        double hs;
    };
    inline constexpr std::array<CurvedProfile, 4> kCurvedProfiles{ { { 28, 36, 98 }, { 22, 28, 78 }, { 24, 24, 46 }, { 60, 60, 43 } } };

    // The four arrow outlines: a 100-unit box, tip
    // at x = 100, shaft along y = 0. And the two counter pins:
    // a circle of radius 25 around (50, 50) with the point towards +y.
    std::wstring_view ArrowProfileSvg(int style);
    std::wstring_view PinProfileSvg(int index);

    struct SvgCommand
    {
        wchar_t op = L'M';           // M, L, Q, C or Z (absolute)
        std::vector<Point> points;   // 1 (M/L), 2 (Q), 3 (C), 0 (Z)
    };
    // The path-string parser: absolute M/L/C/Q/Z with comma or space separated numbers;
    // a command letter may be omitted for repeats. Unknown commands end parsing.
    std::vector<SvgCommand> ParseSvgPath(std::wstring_view text);
    std::vector<SvgCommand> const& ArrowProfile(int style);
    std::vector<SvgCommand> const& PinProfile(int index);

    // Centre of the circle through three points; their centroid when they are collinear.
    Point CircleCentre(Point a, Point b, Point c);

    // The filled straight "shape" arrow from `start` along `delta`. Profile points with
    // x > 50 hang off the tip (x − 100, scaled by `head`), the others off the start (scaled by `tail`);
    // each is turned to polar form, rotated by the arrow angle and scaled by T1[size] × dpr.
    Path ShapeArrowStraight(Point start, Point delta, double head, double tail, int size, double dpr, int style);
    // The curved "shape" arrow: a tapering band along the arc through `knob` (width w1·tail
    // to w2·head, × T1·dpr/2) and the profile's head turned to the arc tangent.
    Path ShapeArrowCurved(Point start, Point delta, Point knob, double midpa, double head, double tail, int size, double dpr, int style);

    // The stroked line/double arrow: the shaft (a line, or the arc through `knob` when
    // `curved`), arms min(0.25·len, 0.2·dpr + 7√w + 0.7w) long at ±45° (len < 20) or ±50°, at the end and,
    // for `doubleHead`, at the start. Curved heads follow the tangent, damped by 1 − 0.15(π − |a|)/π.
    Path LineArrow(Point start, Point delta, double width, double dpr, bool curved, Point knob, bool startHead, bool endHead);
    // The bullet cap of ObjLine: circle of radius min(len/10, w/2 + 1) around `at`.
    Path Bullet(Point at, double length, double width);

    // A counter pin (ArrHelper pin profile `index` 0/1) with its circle of `radius` around `centre`,
    // pointing along `angle` (radians, y down).
    Path PinPath(int index, Point centre, double radius, double angle);
}
