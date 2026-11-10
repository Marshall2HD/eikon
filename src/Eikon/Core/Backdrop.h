#pragma once

// The gradient / solid / image backdrop behind a screenshot: the
// Backdrop.Config and its JSON (`bdrConfig`), the templates, the panel's slider maps and aspect
// presets, the canvas layout (padding, ratio, canvas expansion, radii), the shadow parameters and the
// bitmaps generated on the CPU (mesh, grain, shadow mask). Platform-neutral; Editor/BackdropRender
// draws it with Win2D.
//
// Coordinates are canvas pixels with y down: "top" below is the top on screen (padBT is the top
// too; its y-up maths measures from the bottom).

#include "Core/Bitmap.h"
#include "Core/Event.h"
#include "Core/Geometry.h"
#include "Core/Json.h"

#include <array>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core::Backdrop
{
    // sRGB components, 0…1.
    struct Rgb
    {
        double r = 0;
        double g = 0;
        double b = 0;
        bool operator==(Rgb const&) const = default;
    };

    struct Hsb
    {
        double h = 0; // 0…1
        double s = 0;
        double b = 0;
    };

    // Field order is the JSON key order.
    struct Config
    {
        std::wstring name = L"default";
        std::wstring mode = L"mesh";           // mesh, gradient, flat, image (wallpaper: legacy)
        std::wstring colors = L"091022,0D2162,144CA0,2B91E2,56C3FF,9AEBFF";
        std::wstring stops = L"0.15,0.3,0.6,0.8";
        std::wstring gradStart = L"0.5,1.1";
        std::wstring gradEnd = L"-0.33,-0.15";
        std::wstring gradType = L"radial";     // radial or linear
        std::optional<std::wstring> meshType = L"mesh";
        double noise = 0.03;
        double padding = 68;                   // points
        double inset = -1;                     // points, -1 = auto
        double innerRadius = 10;               // points
        double outerRadius = 0;                // points
        double ratio = 0;                      // W / H, 0 = free
        double minWidth = 0;                   // pixels
        std::int64_t seed = 0;
        double shadowIntensity = 0.5;
        std::int64_t isOn = 0;
        std::wstring filepath;
        double blur = 0;

        bool operator==(Config const&) const = default;
    };

    // A random seed in 0..<32 000 000.
    std::int64_t RandomSeed();
    // The default config with the given seed.
    Config DefaultConfig(std::int64_t seed);

    // The JSON object stored in `bdrConfig`. `meshType` is left out when unset.
    JsonValue ToJson(Config const& config);
    std::wstring Encode(Config const& config);
    // Copies every key of `dict` that is present with the right type over `config`.
    // `meshType` is always replaced (nil when missing). If the backdrop was off before the call
    // (config.isOn != 1 on entry), inset, ratio and minWidth reset to -1, 0, 0.
    void Apply(Config& config, JsonValue const& dict);

    // ---- Templates ----
    // Each template is the dictionary applies. The custom swatches carry tag 888.
    inline constexpr int kCustomTag = 888;
    std::vector<JsonValue> const& GradientTemplates(); // 17, the last is custom-saved-gradient
    std::vector<JsonValue> const& SolidTemplates();    // 13, the last is custom-saved-color
    std::wstring TemplateName(JsonValue const& dict);
    // The tag of a template's swatch: its index, or 888 for the custom ones.
    int TemplateTag(JsonValue const& dict, int index);

    // ---- Colours ----
    // Entries split on ",", "#" removed, trimmed, uppercased; anything but six hex digits
    // is opaque black. An empty string gives #202020.
    std::vector<Rgb> ParseColors(std::wstring_view colors);
    // Double(…) of each entry, 0 when it does not parse. An empty string gives no stops.
    std::vector<double> ParseStops(std::wstring_view stops);
    // "#RRGGBB" (or "RRGGBB") -> colour.
    std::optional<Rgb> ParseHex(std::wstring_view hex);
    std::wstring ToHex(Rgb color); // "#RRGGBB"
    Hsb ToHsb(Rgb color);
    Rgb FromHsb(Hsb color);
    // OKLab lightness of an sRGB colour.
    double Lightness(Rgb color);

    // The 256-entry colour ramp, entries packed R | G << 8 | B << 16 | A << 24.
    // 0 is prepended to the stops unless they start with 0 and 1 appended unless they end with 1
    // (only while there are fewer stops than colours). If the counts then differ, every entry is 0.
    std::array<std::uint32_t, 256> Ramp(std::vector<Rgb> const& colors, std::vector<double> stops);
    // The only colour, 50 % grey for none, else ramp entry 128.
    Rgb BaseColor(std::vector<Rgb> const& colors, std::vector<double> const& stops);
    // The colour with the lowest HSB brightness below 1 (first wins); black if none.
    Rgb DarkestColor(std::vector<Rgb> const& colors);

    // ---- Gradients ----
    struct GradientStop
    {
        double location = 0;
        Rgb color;
    };
    struct Gradient
    {
        bool radial = false;
        Point start; // unit square, y up
        Point end;
        std::vector<GradientStop> stops;
    };
    // "x,y" -> point; parts that don't parse are 0.
    Point ParsePoint(std::wstring_view text);
    // Mode "gradient".
    Gradient ConfigGradient(Config const& config);
    // A solid colour drawn as a subtle radial gradient.
    Gradient FlatGradient(Rgb base);

    // Gradient type popup (pickGradientType →) and its selection.
    enum class GradientType { Mesh, Vertical, Diagonal, Radial };
    void ApplyGradientType(Config& config, GradientType type);
    GradientType SelectedGradientType(Config const& config);
    // Colours reversed, stops become 1 − s in reverse order.
    void Reverse(Config& config);

    // ---- Panel sliders (0…20) ----
    double PaddingFromSlider(double v);     // floor(v² / 2)
    double SliderFromPadding(double padding);
    double ShadowFromSlider(double v);      // v / 20
    double SliderFromShadow(double intensity);
    double InsetFromSlider(double v);       // trunc(v² / 1.8)
    double SliderFromInset(double inset);   // 0 for the auto inset (-1)
    // slideInset only writes when the inset is already set or the slider is off zero.
    bool InsetSliderApplies(double currentInset, double v);
    double RadiusFromSlider(double v);      // 2v (outer and inner)
    double SliderFromRadius(double radius);
    double BlurFromSlider(double v);        // v > 0 ? v + 2 : 0 (slider 0…7)
    double SliderFromBlur(double blur);
    bool InnerRadiusEnabled(Config const& config); // padding > 0 || ratio > 0

    // ---- Aspect popup ----
    struct Aspect
    {
        double ratio = 0;
        double minWidth = 0;
    };
    // From the picked item's title ("Reset", "1:1", "Twitter", …).
    Aspect AspectForItem(std::wstring_view title);
    // The pull-down's visible title after picking `title`: "•••", "Tw", "Fb", "Ln", "Pi", "Yt" or the
    // ratio text.
    std::wstring AspectButtonTitle(std::wstring_view title);

    // ---- Layout ----
    struct Pads
    {
        double bt = 0, bb = 0, bl = 0, br = 0; // backdrop padding
        double ct = 0, cb = 0, cl = 0, cr = 0; // canvas expansion
        bool operator==(Pads const&) const = default;
    };
    inline constexpr double kCanvasLimitH = 1920;
    inline constexpr double kCanvasLimitV = 1280;

    // `source` is the crop in image pixels, `objects` the union of the source and every
    // object's bounds. Sets ct/cb/cl/cr (all 0 when the canvas is not expandable).
    void ExpandCanvas(Pads& pads, Config const& config, double dpr, Rect const& source, Rect const& objects, bool expandable);
    // Padding × dpr: sets bt/bb/bl/br from padding, minWidth and ratio.
    void ApplyPadding(Pads& pads, Config const& config, double dpr, double srcW, double srcH);
    double CanvasWidth(Pads const& pads, double srcW) noexcept;
    double CanvasHeight(Pads const& pads, double srcH) noexcept;
    // Pixel count for the size limits. padCR stands in for padCT in the height term (kept for
    // compatibility of the limits).
    double CanvasPixelsForLimits(Pads const& pads, double srcW, double srcH) noexcept;
    // floor(pixels / 100000) / 10.
    double Megapixels(double pixels) noexcept;
    double OuterRadiusPx(Config const& config, double dpr, double canvasW, double canvasH);
    double InnerRadiusPx(Config const& config, double dpr, Pads const& pads, double srcW, double srcH, double outerPx);
    // The backdrop layer is hidden while there is no visible margin.
    bool BackdropLayerVisible(Config const& config);

    // Where everything sits on the output canvas (pixels, y down).
    struct Layout
    {
        bool on = false;            // isOn
        bool layerVisible = false;  // the backdrop layer itself
        Pads pads;
        double srcW = 0;            // the crop
        double srcH = 0;
        double canvasW = 0;
        double canvasH = 0;
        double outerPx = 0;
        double innerPx = 0;
        Rect content;               // screenshot + canvas expansion
        Rect screenshot;
    };
    // Canvas growth and padding plus the radii. With the backdrop off the canvas is the crop.
    Layout ComputeLayout(Config const& config, double dpr, Rect const& crop, Rect const& objects, bool expandable);

    // ---- Shadow ----
    Rgb ShadowColor(Rgb darkest);
    double ShadowTargetOpacity(double lightness);
    struct ShadowLayer
    {
        Rect frame;           // canvas pixels, y down
        double cornerRadius = 0;
        double offsetY = 0;   // positive = down
        double radius = 0;    // shadow blur radius (σ = radius / 2)
        double opacity = 0;
    };
    // Empty when the intensity is 0, the canvas colour probe found transparency
    // (`canvasClear`) or the canvas is over 20 MP; one layer from 10 MP on, else five.
    std::vector<ShadowLayer> ShadowLayers(Config const& config, Pads const& pads, double srcW, double srcH,
                                          double innerPx, double targetOpacity, bool canvasClear);

    // ---- Bitmaps ----
    // (meshType != "grad") / ("grad") before the blur: size × size.
    Bitmap MeshTemplate(int size, std::vector<Rgb> const& colors, std::vector<double> const& stops,
                        std::int64_t seed, bool grad);
    // A Gaussian blur of `radius` over transparent surroundings, cropped by 2 × radius.
    Bitmap BlurAndCrop(Bitmap const& image, double radius);
    // The 80 px mesh, blurred by 3 px and cropped to 68 × 68.
    Bitmap Mesh(Config const& config);
    // The 256 × 256 grain tile (grey) and the shadow-group mask (grey, alpha 200…255).
    Bitmap Grain(std::int64_t seed);
    Bitmap ShadowMask(std::int64_t seed);

    // The backdrop of one document: global Backdrop instance, kept per image here (Lucida has
    // one document open at a time). `config` is saved to `bdrConfig` after every render.
    struct State
    {
        Config config = DefaultConfig(RandomSeed());
        // Image mode: the decoded picture (backdropImage) and the path it came from.
        std::shared_ptr<Bitmap const> image;
        std::wstring imagePath;
        // Raised after the config or the image changed (the editor repaints).
        Event<> Changed;
    };

    // getAvgColor: the image's average colour (image mode's palette).
    Rgb AverageColor(Bitmap const& image);
    // ("best canvas colour"): nullopt (clear) when the crop's bottom-left pixel is less
    // than 250/255 opaque, else the most frequent colour of the crop's top row.
    std::optional<Rgb> BestCanvasColor(Bitmap const& image, Rect const& crop);
}
