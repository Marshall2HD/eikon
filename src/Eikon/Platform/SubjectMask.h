#pragma once

// Cut Out's subject lifting. In order of preference:
//   1. Windows' own ImageObjectExtractor (Windows App SDK, Microsoft.Windows.AI.Imaging) where the system
//      has it ready (Copilot+ PCs). When it is supported but not ready yet, Lucida asks Windows to prepare
//      it (EnsureReadyAsync) and uses the next option this time.
//   2. The bundled U²-Net-p model (third_party/u2net), run on the CPU with the ONNX Runtime of the
//      Windows App SDK framework (Windows ML).
//   3. Core::CutOutBackground, border-colour fallback, when no model can run or neither
//      finds a subject.
// The mask's edge is feathered (Core::FeatherMask) before it becomes the image's alpha.

#include "Core/Bitmap.h"

#include <functional>
#include <memory>

namespace Eikon::SubjectMask
{
    // Runs off the UI thread and calls `done` back on the calling thread with the image whose
    // background is removed.
    winrt::fire_and_forget CutOut(std::shared_ptr<Core::Bitmap const> image, std::function<void(Core::Bitmap)> done);
}
