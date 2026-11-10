#include "pch.h"
#include "TestFramework.h"

#include "Core/Confetti.h"

#include <cmath>

namespace Confetti = Eikon::Core::Confetti;

namespace
{
    // A fixed pseudo-random sequence (LCG) so the counts are repeatable.
    std::function<double()> Lcg()
    {
        auto state = std::make_shared<std::uint64_t>(12345);
        return [state] {
            *state = *state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<double>(*state >> 11) / 9007199254740992.0;
        };
    }
}

TEST(Confetti_OnePuffOfAboutTwoParticlesPerCell)
{
    auto const particles = Confetti::Emit(Lcg());
    // 150 cells at 10/s for 0.18 s: 1 or 2 each, 270 expected.
    CHECK(particles.size() >= 150u);
    CHECK(particles.size() <= 300u);
    CHECK(std::abs(static_cast<double>(particles.size()) - 270.0) < 30.0);
    for (auto const& p : particles)
    {
        CHECK(p.birth >= 0 && p.birth < 0.18);
        CHECK(std::abs(std::hypot(p.x, p.y) - 8.0) < 1e-9);          // on the emitter circle
        double const speed = std::hypot(p.vx, p.vy);
        CHECK(speed >= 135.0 - 1e-9 && speed <= 145.0 + 1e-9);
        CHECK(p.diameter >= 0 && p.diameter <= 6 * 0.235 + 1e-9);    // 6 pt × |0.11 ± 0.125|
    }
}

TEST(Confetti_DotsFadeInASixthOfASecondAndStayOutsideTheMask)
{
    std::vector<Confetti::Particle> particles(3);
    particles[0] = { 0.0, 8, 0, 140, 0, 0.66 };     // outwards
    particles[1] = { 0.0, 8, 0, -140, 0, 0.66 };    // inwards: hidden while inside the 8 pt circle
    particles[2] = { 0.1, 0, -8, 0, -140, 0.66 };   // born later
    auto dots = Confetti::Frame(particles, 0.05, 12, 12);
    CHECK_EQ(dots.size(), std::size_t{ 1 });
    CHECK(std::abs(dots[0].x - 15.0) < 1e-9);
    CHECK(std::abs(dots[0].alpha - 0.7) < 1e-9);
    // At 0.15 s the first dot (x = 29) is still inside 12 + 20, alpha 0.1; the inward one has crossed
    // to x = -13; the late one is 0.05 s old.
    dots = Confetti::Frame(particles, 0.15, 12, 12);
    CHECK_EQ(dots.size(), std::size_t{ 3 });
    // Past 1/6 s nothing of the first two is left.
    dots = Confetti::Frame(particles, 0.17, 12, 12);
    CHECK_EQ(dots.size(), std::size_t{ 1 });
    // Past the 32 pt box edge a dot is masked even while still visible.
    dots = Confetti::Frame({ { 0.0, 8, 0, 300, 0, 0.66 } }, 0.1, 12, 12);
    CHECK(dots.empty());
}
