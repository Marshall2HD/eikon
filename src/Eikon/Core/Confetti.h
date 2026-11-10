#pragma once

// The status icon's success confetti: a
// particle emitter centred on the icon, simulated here so the tray window only draws dots.
// Units are points relative to the icon's centre, y down.

#include <functional>
#include <vector>

namespace Eikon::Core::Confetti
{
    inline constexpr int kCells = 150;                    // 150 identical particles
    inline constexpr double kBirthRate = 2.0 * 5.0;       // cell 2/s × layer 5
    inline constexpr double kEmitDuration = 0.18;         // cell duration
    inline constexpr double kLifetime = 3.0 * 0.1;        // cell 3 s × layer 0.1
    inline constexpr double kVelocity = 140.0;            // ± 5 pt/s, any direction (range π)
    inline constexpr double kVelocityRange = 5.0;
    inline constexpr double kEmitterRadius = 8.0;         // circle shape, outline mode, size 16
    inline constexpr double kImageSize = 6.0;             // 6-circle, 6 × 6 pt
    inline constexpr double kScale = 0.11;                // ± 0.125
    inline constexpr double kScaleRange = 0.125;
    inline constexpr double kAlphaSpeed = -6.0;           // per second, from 1
    inline constexpr double kMaskRadius = 8.0;            // a 16 pt circle in the middle is hidden
    inline constexpr double kMaskMargin = 20.0;           // the mask reaches 20 pt past the icon
    inline constexpr double kShowSeconds = 1.0;           // the layer is removed after 1 s

    struct Particle
    {
        double birth = 0;        // seconds after the start
        double x = 0, y = 0;     // birth position
        double vx = 0, vy = 0;   // pt/s
        double diameter = 0;     // pt
    };

    struct Dot
    {
        double x = 0, y = 0, diameter = 0, alpha = 0;
    };

    // `random` returns uniform values in [0, 1).
    std::vector<Particle> Emit(std::function<double()> const& random);

    // The dots visible at `t` seconds: alive, still opaque enough to draw, outside the mask circle
    // and inside the icon box (half-size `halfWidth` × `halfHeight`) grown by the 20 pt margin.
    std::vector<Dot> Frame(std::vector<Particle> const& particles, double t, double halfWidth, double halfHeight);
}
