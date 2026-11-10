#pragma once

// The selection's "Cut Out" button. Lucida finds the subject with Windows' ImageObjectExtractor where the system has it,
// else with the bundled U²-Net-p model (Platform/SubjectMask); this file holds the platform-neutral
// parts: the model's pre- and post-processing, the feathered mask, and the border-colour fallback.

#include "Core/Bitmap.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Eikon::Core
{
    // The most common colour on the image's border (top and bottom rows, left and right columns),
    // compared exactly, is flood-filled (4-neighbour) from every border pixel of that colour; the
    // filled pixels become transparent. Everything else, including enclosed areas of the same colour,
    // stays.
    // Lucida's last resort, when no subject model can run or the model finds no subject.
    Bitmap CutOutBackground(Bitmap const& image);

    // ---- The subject model (U²-Net-p, third_party/u2net) ----

    // The model's square input and output size.
    inline constexpr int kSubjectModelSize = 320;

    // The model input, NCHW float (3 planes of size × size, R, G, B): the image resampled to size × size
    // (area average when shrinking, bilinear when growing), divided by its brightest channel value, then
    // normalised with ImageNet's mean (0.485, 0.456, 0.406) and std (0.229, 0.224, 0.225), as rembg does.
    std::vector<float> SubjectModelInput(Bitmap const& image, int size = kSubjectModelSize);

    // The model's first output (size × size, sigmoid 0..1) as an alpha mask of width × height (0
    // background .. 255 subject): min-max stretched, scaled up bilinearly, the edge tightened
    // (smoothstep 0.3..0.7) and feathered by `feather` pixels. Empty when the output holds no subject
    // (its maximum below 0.5, or under 0.5 % of the mask above half).
    std::vector<std::uint8_t> SubjectMaskFromModel(std::span<float const> output, int size, int width, int height, int feather);

    // Whether a mask (0..255 per pixel) has a subject: at least 0.5 % of its pixels at 128 or more.
    bool MaskHasSubject(std::span<std::uint8_t const> mask);

    // Softens a mask's edges in place: two box-blur passes of `radius` pixels (about a Gaussian of
    // σ ≈ 0.8 × radius). Interior and exterior pixels further than 2 × radius from an edge keep their value.
    void FeatherMask(std::vector<std::uint8_t>& mask, int width, int height, int radius);

    // The image with `mask` (width × height bytes) as its coverage: every premultiplied channel is
    // scaled by mask / 255.
    Bitmap ApplySubjectMask(Bitmap const& image, std::span<std::uint8_t const> mask);
}
