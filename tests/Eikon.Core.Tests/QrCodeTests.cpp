#include "pch.h"
#include "TestFramework.h"

#include "Core/QrCode.h"

#include <string_view>
#include <vector>

using namespace Eikon::Core;

// QR detection through the vendored, QR-only ZXing build.
// The symbols were generated with segno: "#" is a dark module.

namespace
{
    // "https://eikon.example/qr-test?id=42", QR version 3-M.
    std::vector<std::wstring_view> const kUrl{
            L"#######.#.##..####....#######",
            L"#.....#..##.###.....#.#.....#",
            L"#.###.#.#.#...####....#.###.#",
            L"#.###.#...#.#.#..###..#.###.#",
            L"#.###.#....#.#####..#.#.###.#",
            L"#.....#.#####....#..#.#.....#",
            L"#######.#.#.#.#.#.#.#.#######",
            L".........##...##....#........",
            L"#.#...##...#.###..###..#..#.#",
            L"....#..###...###.#.##.##...##",
            L".#...###...#...###.##....##.#",
            L"###.##.#..##.#..#.#.#.####...",
            L"#.###.#.#..###.#....####....#",
            L"#..#....#..#...#..###.##...##",
            L"..#...#.###..###.#.##.......#",
            L".####...####..#.#.....#.#....",
            L"##....#..######....#..##....#",
            L"....#.....#..###...##.##..###",
            L"####..#.#.#.#..###.#..#.##..#",
            L"....#.......##..#.#.##..#....",
            L"##.##.##..#.##.##...######.#.",
            L"........#####..##.#.#...###.#",
            L"#######.#.##.###..#.#.#.#...#",
            L"#.....#..##.#.#...###...#..##",
            L"#.###.#..##..##.#..#######.##",
            L"#.###.#...#...###.##.#..###.#",
            L"#.###.#.#..###.#.#.##...#..##",
            L"#.....#..##...###...#.##.#...",
            L"#######.##....#...#.###.##..#",
    };

    // "  two words\n", QR version 1-M.
    std::vector<std::wstring_view> const kSpaces{
            L"#######.......#######",
            L"#.....#.##....#.....#",
            L"#.###.#..#.#..#.###.#",
            L"#.###.#...#.#.#.###.#",
            L"#.###.#.##..#.#.###.#",
            L"#.....#..#..#.#.....#",
            L"#######.#.#.#.#######",
            L".........####........",
            L"#.#.#.#....#....#..#.",
            L"#.#..#.###.#..####.##",
            L".###..#.#...##.##.#.#",
            L"...........#....#..#.",
            L".##...###.#.##.###..#",
            L"........#..#.#..#..##",
            L"#######..##.#..##.#.#",
            L"#.....#..####..#...#.",
            L"#.###.#.#.......##.#.",
            L"#.###.#..###...##..#.",
            L"#.###.#.#.#.#.#.##..#",
            L"#.....#..##.#...#..#.",
            L"#######.###.##.#...##",
    };

    // "12345", Micro QR M1: not a QR code, so not read.
    std::vector<std::wstring_view> const kMicro{
            L"#######.#.#",
            L"#.....#.##.",
            L"#.###.#.#..",
            L"#.###.#....",
            L"#.###.#.###",
            L"#.....#..##",
            L"#######.#..",
            L".........##",
            L"##..###..##",
            L".#.#...##..",
            L"####.....##",
    };

    // Draws `symbols` side by side, 4 px per module with a 4-module quiet zone, on opaque white BGRA.
    Bitmap Render(std::vector<std::vector<std::wstring_view> const*> const& symbols)
    {
        constexpr int kModule = 4;
        constexpr int kQuiet = 4;
        int width = kQuiet;
        int height = 0;
        for (auto const* symbol : symbols)
        {
            width += static_cast<int>(symbol->front().size()) + kQuiet;
            height = (std::max)(height, static_cast<int>(symbol->size()));
        }
        Bitmap bitmap = Bitmap::Create(width * kModule, (height + 2 * kQuiet) * kModule);
        std::fill(bitmap.pixels.begin(), bitmap.pixels.end(), static_cast<std::uint8_t>(0xFF));
        int left = kQuiet;
        for (auto const* symbol : symbols)
        {
            for (std::size_t row = 0; row < symbol->size(); ++row)
            {
                for (std::size_t column = 0; column < (*symbol)[row].size(); ++column)
                {
                    if ((*symbol)[row][column] != L'#')
                    {
                        continue;
                    }
                    for (int dy = 0; dy < kModule; ++dy)
                    {
                        for (int dx = 0; dx < kModule; ++dx)
                        {
                            std::size_t const x = static_cast<std::size_t>((left + static_cast<int>(column)) * kModule + dx);
                            std::size_t const y = static_cast<std::size_t>((kQuiet + static_cast<int>(row)) * kModule + dy);
                            std::uint8_t* pixel = bitmap.pixels.data() + y * bitmap.Stride() + x * 4;
                            pixel[0] = pixel[1] = pixel[2] = 0;
                        }
                    }
                }
            }
            left += static_cast<int>(symbol->front().size()) + kQuiet;
        }
        return bitmap;
    }
}

TEST(QrCode_ReadsAQrCode)
{
    CHECK_EQ(QrCode::Read(Render({ &kUrl })), std::wstring{ L"https://eikon.example/qr-test?id=42" });
}

TEST(QrCode_ResultIsTrimmed)
{
    CHECK_EQ(QrCode::Read(Render({ &kSpaces })), std::wstring{ L"two words" });
}

TEST(QrCode_OnlyQrCodes)
{
    // Micro QR is another symbology: detector ignores it, so OCR would run instead.
    CHECK_EQ(QrCode::Read(Render({ &kMicro })), std::wstring{});
    CHECK_EQ(QrCode::Read(Bitmap::Create(200, 200)), std::wstring{});
}

TEST(QrCode_SeveralCodesJoinedWithNewline)
{
    // Joined with "\n" in detection order, then trimmed only at the ends of the whole text.
    std::wstring const both = QrCode::Read(Render({ &kUrl, &kMicro, &kSpaces }));
    bool const either = both == L"https://eikon.example/qr-test?id=42\n  two words" || both == L"two words\n\nhttps://eikon.example/qr-test?id=42";
    CHECK(either);
}
