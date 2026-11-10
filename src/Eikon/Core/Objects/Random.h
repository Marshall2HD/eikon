#pragma once

// Seeded randomness of the hand-drawn arrow and shapes: xorshift64* from a stored UInt64 seed, 100 values in 0...1. The same
// seed always gives the same values, so a saved object re-renders identically.

#include <cstdint>
#include <string>
#include <vector>

namespace Eikon::Core::Random
{
    // The 100 values kept in `rnd`: s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    // s *= 0x2545F4914F6CDD1D; value = (s & 0xFFFFFFFF) / 4294967295.0.
    std::vector<double> Values(std::uint64_t seed, int count = 100);

    // A new random seed, below 2^53 so that it survives the
    // JSON number of the Dump exactly (Core::JsonValue stores doubles). Never 0 (xorshift's fixed point).
    std::uint64_t NewSeed();

    // Uniform 0 <= v < 1 and an integer in [0, bound), from the process generator (the pixelate grid and
    // neighbour choice, which are not seeded).
    double Unit();
    std::uint64_t Below(std::uint64_t bound);
    // `length` random characters from "0123456789abcdefghijklmnopqrstuvwxyz" (imageHash: 24, pointsHash: 25).
    std::wstring Hash(int length);
}
