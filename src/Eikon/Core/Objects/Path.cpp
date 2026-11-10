#include "pch.h"
#include "Core/Objects/Path.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Eikon::Core
{
    Rect Path::ControlBounds() const
    {
        double l = std::numeric_limits<double>::infinity();
        double t = l;
        double r = -l;
        double b = -l;
        auto add = [&](Point p) {
            l = (std::min)(l, p.x);
            t = (std::min)(t, p.y);
            r = (std::max)(r, p.x);
            b = (std::max)(b, p.y);
        };
        for (auto const& c : commands)
        {
            switch (c.kind)
            {
            case PathCommand::Kind::Move:
            case PathCommand::Kind::Line:
                add(c.p);
                break;
            case PathCommand::Kind::Quad:
                add(c.c1);
                add(c.p);
                break;
            case PathCommand::Kind::Cubic:
                add(c.c1);
                add(c.c2);
                add(c.p);
                break;
            case PathCommand::Kind::Arc:
                add({ c.p.x - c.radius, c.p.y - c.radius });
                add({ c.p.x + c.radius, c.p.y + c.radius });
                break;
            case PathCommand::Kind::Close:
                break;
            }
        }
        if (r < l)
        {
            return {};
        }
        return { l, t, r - l, b - t };
    }
}
