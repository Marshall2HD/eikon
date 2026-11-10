#pragma once

// FreehandHelper: the stroke machinery behind freehand, the highlighter, the
// hand-drawn arrow and the hand-drawn rectangle/oval. A stroke is a list of Samples; `a`/`b`
// are the incoming/outgoing Bézier controls, −999 when unset. Widths are in pixels.

#include "Core/Objects/Path.h"

#include <string_view>
#include <vector>

namespace Eikon::Core::Freehand
{
    inline constexpr double kUnset = -999.0;

    struct Sample
    {
        double x = 0;
        double y = 0;
        double weight = 0;
        double speed = 0;
        double width = 0;
        bool keep = false;
        Point a{ kUnset, kUnset };
        Point b{ kUnset, kUnset };
        double len = 0;
        double cumLen = 0;

        Point P() const noexcept { return { x, y }; }
    };
    using Samples = std::vector<Sample>;

    Sample At(Point p);

    // Len and speed = distance from the previous sample, cumLen = running length.
    void Measure(Samples& samples);
    // The width profiles of the hand-drawn arrow. 0 linear, 1 (cos 2πt)/2 + 1/2,
    // 2 2t − t², 3 (1 − t)², 4 −4(t² − t), 5 √(1 − (2t − 1)²), 6 the "bump".
    double Ease(double t, int profile);
    // Catmull-like controls. Middle samples get handles along (next − prev), 0.1 of the
    // neighbour distance at corners (cos ≥ −0.1), else 0.3 + 0.04 (1 − weight); the ends point 0.2 of
    // the way to the neighbour's handle (halfway for two samples).
    void SetControls(Samples& samples);
    // Samples each cubic segment every `step` pixels
    // using 1.2× the chord length to count steps; weights are interpolated.
    Samples Resample(Samples const& samples, double step);
    // Drops samples closer than 0.33·`distance` to the last kept one; the last sample
    // always survives (it replaces a kept sample that is too close).
    Samples Simplify(Samples const& samples, double distance);
    // Turns a centre line with per-sample widths into a closed outline (left and right
    // offsets, a 6-point round fan on the outer side of sharp corners, caps by `capStyle` 0–6), then
    // gives it Bézier controls. `width` is the stroke's nominal width (simplification distance).
    Samples Outline(Samples samples, double width, int capStyle);
    // Move to the first sample, then cubic curves through the controls.
    Path Emit(Samples const& samples, bool close);

    // ---- Freehand pipeline pieces ----
    // Speed = segment length; weight = 1 − (speed − min)/(max − min) (slow = heavy);
    // the first weight is 0.7, the last copies the one before; two samples get weight 1.
    void SpeedWeights(Samples& samples);
    // Speed-aware low-pass filter of the positions with `smoothing` at `resolution`
    // (zoom/dpr while drawing).
    Samples Smooth(Samples const& samples, double smoothing, double resolution);
    // Running average of the weights, w = k·w_prev + (1 − k)·w.
    void SmoothWeights(Samples& samples, double k);
    // W = w(1 − k) + k·(lo + (w − min)/(max − min)·(hi − lo)); all 0.5 if constant.
    void NormaliseWeights(Samples& samples, double lo, double hi, double k);
    // Width = W·((1 − v) + 2v·weight), with v scaled by clamp((length/W − 1)/4, 0, 1).
    void AssignWidths(Samples& samples, double width, double variability);
    // Circular moving average of the weights over `window` samples.
    void CircularAverageWeights(Samples& samples, int window);
    // Ramer–Douglas–Peucker with tolerance `epsilon`.
    Samples Reduce(Samples const& samples, double epsilon);

    // ---- Shape corrections ----
    // 'h': a stroke within 10° of horizontal from its start is pulled flat.
    void StraightenHorizontal(Samples& samples);
    // 'c': morph towards the bounding ellipse with `strength` (needs ≥ 10 samples).
    void MorphEllipse(Samples& samples, double strength);
    // 's': morph towards a rectangle whose edges are the medians of the samples in the
    // middle third of each side; every sample moves to its nearest edge by `strength` (needs ≥ 10).
    void MorphBox(Samples& samples, double strength);

    // The whole freehand/highlighter pipeline of for `raw` points captured in `rawRect`
    // and shown in `rect` (pixels). Returns the filled outline.
    struct StrokeParams
    {
        double width = 1;          // widths[size] × dpr
        double smoothing = 0.6;
        double variability = 0.25;
        double resolution = 1;     // zoom / dpr when drawn
        double dpr = 1;            // resampling step is 2·dpr
        int capStyle = 0;
        std::wstring_view corrections;
        Rect rawRect;
        Rect rect;
    };
    Path StrokePath(std::vector<Point> const& raw, StrokeParams const& params, Samples* centreLine = nullptr);
}
