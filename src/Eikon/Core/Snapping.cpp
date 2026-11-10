#include "pch.h"
#include "Core/Snapping.h"

#include <cmath>

namespace Eikon::Core
{
    namespace
    {
        struct AxisBest
        {
            double delta = 0;
            double distance = 0;
            bool found = false;
            double edge = 0;           // the candidate edge snapped to
            Rect candidate;
        };

        void Consider(AxisBest& best, double movingEdge, double candidateEdge, Rect const& candidate, double threshold)
        {
            double const d = candidateEdge - movingEdge;
            if (std::abs(d) <= threshold && (!best.found || std::abs(d) < best.distance))
            {
                best = { d, std::abs(d), true, candidateEdge, candidate };
            }
        }
    }

    SnapResult Snap(Rect moving, std::vector<Rect> const& candidates, double threshold)
    {
        SnapResult result;
        Rect const m = moving.Standardized();
        AxisBest bx;
        AxisBest by;
        for (Rect const& raw : candidates)
        {
            Rect const c = raw.Standardized();
            for (double const edge : { c.Left(), c.Right() })
            {
                Consider(bx, m.Left(), edge, c, threshold);
                Consider(bx, m.Right(), edge, c, threshold);
            }
            for (double const edge : { c.Top(), c.Bottom() })
            {
                Consider(by, m.Top(), edge, c, threshold);
                Consider(by, m.Bottom(), edge, c, threshold);
            }
        }
        result.snappedX = bx.found;
        result.snappedY = by.found;
        result.dx = bx.found ? bx.delta : 0;
        result.dy = by.found ? by.delta : 0;
        Rect const snapped = m.Offset(result.dx, result.dy);

        // Guides through every aligned edge pair of the winning candidates (two per axis at most).
        if (bx.found)
        {
            Rect const& c = bx.candidate;
            double const top = (std::min)(snapped.Top(), c.Top());
            double const bottom = (std::max)(snapped.Bottom(), c.Bottom());
            for (double const x : { snapped.Left(), snapped.Right() })
            {
                if (std::abs(x - c.Left()) < 0.5 || std::abs(x - c.Right()) < 0.5)
                {
                    result.guides.push_back({ { x, top }, { x, bottom } });
                }
            }
        }
        if (by.found)
        {
            Rect const& c = by.candidate;
            double const left = (std::min)(snapped.Left(), c.Left());
            double const right = (std::max)(snapped.Right(), c.Right());
            for (double const y : { snapped.Top(), snapped.Bottom() })
            {
                if (std::abs(y - c.Top()) < 0.5 || std::abs(y - c.Bottom()) < 0.5)
                {
                    result.guides.push_back({ { left, y }, { right, y } });
                }
            }
        }
        return result;
    }

    Insets CanvasGrowth(Rect crop, std::vector<Rect> const& objectBounds, double dpr)
    {
        Rect const c = crop.Standardized();
        double const margin = 20 * dpr;
        double const capX = 1920 * dpr;
        double const capY = 1280 * dpr;
        Insets insets;
        for (Rect const& raw : objectBounds)
        {
            Rect const o = raw.Standardized();
            if (o.Left() < c.Left())
            {
                insets.left = (std::max)(insets.left, (std::min)(c.Left() - o.Left() + margin, capX));
            }
            if (o.Right() > c.Right())
            {
                insets.right = (std::max)(insets.right, (std::min)(o.Right() - c.Right() + margin, capX));
            }
            if (o.Top() < c.Top())
            {
                insets.top = (std::max)(insets.top, (std::min)(c.Top() - o.Top() + margin, capY));
            }
            if (o.Bottom() > c.Bottom())
            {
                insets.bottom = (std::max)(insets.bottom, (std::min)(o.Bottom() - c.Bottom() + margin, capY));
            }
        }
        return insets;
    }
}
