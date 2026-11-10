#pragma once

// The pixel algorithms of ObjConceal: the randomized mosaic "Blur" and the gradient "Erase", plus the
// "-text" variants that apply them only inside OCR word boxes. Platform-neutral, on Core::Bitmap.

#include "Core/Bitmap.h"
#include "Core/Geometry.h"

#include <functional>
#include <vector>

namespace Eikon::Core::Conceal
{
    // A block of the mosaic, in pixels.
    struct Cell
    {
        double x = 0, y = 0, w = 0, h = 0;
        bool operator==(Cell const&) const = default;
    };

    // Uniform 0 ≤ v < 1 (Double.random(in: 0..<1)); injectable for tests.
    using Rng = std::function<double()>;

    // b = round(min(round(minSide / 6), 5·dpr) × 1.35^size).
    double BlockSize(double minSide, double dpr, int size);

    // B' = max(2, b); ceil(w / 3b' / 2) columns and ceil(h / 3b' / 2) rows with rounded
    // edges, then, from the last cell back, each side longer than 2b' is cut at
    // min(3b', b' + round((side − 2b')·rand)) and the remainders go back on the stack.
    std::vector<Cell> Grid(Rect const& area, double block, Rng const& rng);

    // The mosaic of `area` (pixels of `source`), written into `out` at `area` − `outOrigin`. Each cell is
    // filled with the integer mean of its pixels; when the block is larger than minSide/7 a random
    // k in 0…4 picks the neighbour across the bottom edge (2) or the left edge (3) instead.
    void Pixelate(Bitmap const& source, Rect const& area, int size, double dpr, Rng const& rng, Bitmap& out, Point outOrigin);

    // Horizontal cost = Σ rows |left − right| / h + 0.5·(top + bottom edge variation) / w,
    // vertical cost = Σ columns |top − bottom| / w + 0.5·(left + right edge variation) / h (|Δ| = |ΔR| +
    // |ΔG| + |ΔB|). The cheaper direction wins (horizontal on a tie): each row (column) becomes a linear
    // gradient between its two border pixels.
    void Erase(Bitmap const& source, Rect const& area, Bitmap& out, Point outOrigin);
    // The two costs, for tests.
    void EraseCosts(Bitmap const& source, Rect const& area, double& horizontal, double& vertical);

    // The concealing bitmap for an object at `rect` (whole pixels inside the image) with `fillMode`
    // ("pixelate", "pixelate-text", "erase", "erase-text"). Text modes apply the base mode only inside
    // `textBoxes` (relative to `rect`, from OCR), each grown by 2 px, on a transparent bitmap.
    Bitmap Generate(Bitmap const& source, Rect const& rect, std::wstring_view fillMode, int size, double dpr,
                    std::vector<Rect> const& textBoxes, Rng const& rng);

    // Object rect → the pixel area it conceals: clamped into the image, at least 1 × 1 (ObjConceal).
    Rect ClampArea(Rect const& rect, double imageWidth, double imageHeight);
}
