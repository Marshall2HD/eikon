#include "pch.h"
#include "Core/Objects/Random.h"

#include <random>

namespace Eikon::Core::Random
{
    namespace
    {
        std::mt19937_64& Engine()
        {
            static std::mt19937_64 engine{ std::random_device{}() ^ (static_cast<std::uint64_t>(std::random_device{}()) << 32) };
            return engine;
        }
    }

    std::vector<double> Values(std::uint64_t seed, int count)
    {
        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
        std::uint64_t s = seed;
        for (int i = 0; i < count; ++i)
        {
            s ^= s >> 12;
            s ^= s << 25;
            s ^= s >> 27;
            s *= 0x2545F4914F6CDD1DULL;
            values.push_back(static_cast<double>(s & 0xFFFFFFFFULL) / 4294967295.0);
        }
        return values;
    }

    std::uint64_t NewSeed()
    {
        std::uint64_t seed = 0;
        while (seed == 0)
        {
            seed = Engine()() & ((1ULL << 53) - 1);
        }
        return seed;
    }

    double Unit()
    {
        return std::uniform_real_distribution<double>{ 0.0, 1.0 }(Engine());
    }

    std::uint64_t Below(std::uint64_t bound)
    {
        if (bound == 0)
        {
            return 0;
        }
        return std::uniform_int_distribution<std::uint64_t>{ 0, bound - 1 }(Engine());
    }

    std::wstring Hash(int length)
    {
        static constexpr wchar_t kAlphabet[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
        std::wstring hash;
        for (int i = 0; i < length; ++i)
        {
            hash += kAlphabet[Below(36)];
        }
        return hash;
    }
}
