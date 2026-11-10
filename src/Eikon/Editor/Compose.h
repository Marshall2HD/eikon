#pragma once

// The final image. The cropped
// screenshot with every object rasterised and, unless ignored, the backdrop around it. Copy, save,
// drag-out, pin, upload and print all use it.

#include "Core/Bitmap.h"
#include "Core/Document.h"

#include <memory>

namespace Eikon::Editor
{
    struct ComposeOptions
    {
        bool ignoreBackdrop = false;
    };

    std::shared_ptr<Core::Bitmap const> Compose(Core::Document const& document, ComposeOptions const& options = {});
}
