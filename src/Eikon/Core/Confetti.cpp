#include "pch.h"
#include "Core/Confetti.h"

#include <cmath>
#include <numbers>

namespace Eikon::Core::Confetti
{
    std::vector<Particle> Emit(std::function<double()> const& random)
    {
        std::vector<Particle> particles;
        double const interval = 1.0 / kBirthRate;
        for (int cell = 0; cell < kCells; ++cell)
        {
            // Each cell emits every 0.1 s for 0.18 s from its own phase: one or two particles.
            for (double birth = random() * interval; birth < kEmitDuration; birth += interval)
            {
                Particle p;
                p.birth = birth;
                double const at = random() * 2 * std::numbers::pi;
                p.x = kEmitterRadius * std::cos(at);
                p.y = kEmitterRadius * std::sin(at);
                // emissionLongitude 0, emissionRange π: any direction.
                double const heading = (random() * 2 - 1) * std::numbers::pi;
                double const speed = kVelocity + (random() * 2 - 1) * kVelocityRange;
                p.vx = speed * std::cos(heading);
                p.vy = speed * std::sin(heading);
                double const scale = kScale + (random() * 2 - 1) * kScaleRange;
                p.diameter = kImageSize * std::abs(scale);
                particles.push_back(p);
            }
        }
        return particles;
    }

    std::vector<Dot> Frame(std::vector<Particle> const& particles, double t, double halfWidth, double halfHeight)
    {
        std::vector<Dot> dots;
        double const maxX = halfWidth + kMaskMargin;
        double const maxY = halfHeight + kMaskMargin;
        for (Particle const& p : particles)
        {
            double const age = t - p.birth;
            if (age < 0 || age >= kLifetime)
            {
                continue;
            }
            double const alpha = 1.0 + kAlphaSpeed * age;
            if (alpha <= 0 || p.diameter <= 0)
            {
                continue;
            }
            Dot dot{ p.x + p.vx * age, p.y + p.vy * age, p.diameter, alpha };
            if (std::hypot(dot.x, dot.y) < kMaskRadius || std::abs(dot.x) > maxX || std::abs(dot.y) > maxY)
            {
                continue;
            }
            dots.push_back(dot);
        }
        return dots;
    }
}
