#include "pch.h"
#include "Core/Geometry.h"

namespace Eikon::Core
{
    Size InitialEditorContentSize(Size imagePixels, double dpr, Size screenPoints, bool preferLargeWindow) noexcept
    {
        constexpr double kMinWidth = 836;
        if (!(dpr > 0))
        {
            dpr = 1;
        }
        double const f = preferLargeWindow ? 0.8 : 0.5;
        double const maxW = (std::max)(kMinWidth, f * screenPoints.width);
        double const maxH = (std::max)(maxW / 2, f * screenPoints.height);
        double const imgW = imagePixels.width / dpr;
        double const imgH = imagePixels.height / dpr;
        double const w = imgW < maxW ? (std::max)(kMinWidth, imgW) : maxW;
        double const h = imgH < maxH ? (std::max)(w / 2, imgH) : maxH;
        return { w, frinta(h) };
    }

    double FitZoom(Size imagePixels, Size areaPixels) noexcept
    {
        if (!(imagePixels.width > 0) || !(imagePixels.height > 0) || !(areaPixels.width > 0) || !(areaPixels.height > 0))
        {
            return 1.0;
        }
        double const zoom = (std::min)(areaPixels.width / imagePixels.width, areaPixels.height / imagePixels.height);
        return (std::min)(zoom, 1.0);
    }
}
