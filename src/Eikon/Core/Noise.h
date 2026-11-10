#pragma once

// Perlin noise source and noise map, so that a backdrop seed always gives the same mesh and grain.

#include <cstdint>
#include <vector>

namespace Eikon::Core::Noise
{
    // GKPerlinNoiseSource(frequency:octaveCount:persistence:lacunarity:seed:).
    struct Perlin
    {
        double frequency = 1.0;
        int octaveCount = 1;
        double persistence = 0.5;
        double lacunarity = 2.0;
        std::int32_t seed = 0;

        // The 3-D noise; GKNoise.value(atPosition: (x, y)) is Value(x, 0, y).
        double Value(double x, double y, double z) const noexcept;
    };

    // GKNoiseMap(noise, size:, origin:, sampleCount:, seamless:).value(at: (i, j)) for every sample,
    // row-major (j * countX + i).
    std::vector<float> Map(Perlin const& noise, double sizeX, double sizeY, double originX, double originY,
                           int countX, int countY, bool seamless);
}
