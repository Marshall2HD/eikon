#pragma once

// Platform-neutral vector path for the annotation objects (drawn with a
// CanvasPathBuilder). Core builds the geometry; Editor/RenderObjects.cpp turns it into
// a CanvasGeometry. Coordinates are image pixels, y down.

#include "Core/Color.h"
#include "Core/Geometry.h"

#include <vector>

namespace Eikon::Core
{
    struct PathCommand
    {
        enum class Kind
        {
            Move,    // p
            Line,    // p
            Quad,    // c1 control, p end
            Cubic,   // c1, c2 controls, p end
            Arc,     // circle arc around p (centre) with `radius`, from `start` sweeping `sweep` radians
            Close,
        };
        Kind kind = Kind::Move;
        Point p;
        Point c1;
        Point c2;
        double radius = 0;
        double start = 0;
        double sweep = 0;
    };

    struct Path
    {
        std::vector<PathCommand> commands;

        bool Empty() const noexcept { return commands.empty(); }
        void MoveTo(Point p) { commands.push_back({ PathCommand::Kind::Move, p }); }
        void LineTo(Point p) { commands.push_back({ PathCommand::Kind::Line, p }); }
        void QuadTo(Point c, Point p) { commands.push_back({ PathCommand::Kind::Quad, p, c }); }
        void CubicTo(Point c1, Point c2, Point p) { commands.push_back({ PathCommand::Kind::Cubic, p, c1, c2 }); }
        // Arc of the circle (centre, radius) from angle `start` over `sweep` (radians; positive = clockwise
        // on screen, as angles grow with y down). Starts a line to the arc's first point when a figure is open.
        void ArcAround(Point centre, double radius, double start, double sweep)
        {
            PathCommand c{ PathCommand::Kind::Arc, centre };
            c.radius = radius;
            c.start = start;
            c.sweep = sweep;
            commands.push_back(c);
        }
        void Close() { commands.push_back({ PathCommand::Kind::Close }); }
        void Append(Path const& other) { commands.insert(commands.end(), other.commands.begin(), other.commands.end()); }
        // Bounds of every point including controls (a superset of the drawn area).
        Rect ControlBounds() const;
    };

    // Paint with float alpha (backgroundAlpha, spotlight transparency, ...).
    struct Paint
    {
        Color color;
        double alpha = 1.0;
    };

    // One filled and/or stroked path. Dashes are in pixels, not stroke widths.
    struct Shape
    {
        enum class Cap { Flat, Square, Round };
        enum class Join { Miter, Round };
        Path path;
        bool fill = false;
        Paint fillPaint;
        bool evenOdd = false;
        bool stroke = false;
        Paint strokePaint;
        double strokeWidth = 0;
        Cap cap = Cap::Flat;
        Join join = Join::Miter;
        std::vector<double> dashes;
    };

    // sRGB (0, 0.495, 0.85): the hover/selection blue every object uses in the "highlighted" state
    inline constexpr Color kHoverBlue{ 0, 126, 217, 255 };
}
