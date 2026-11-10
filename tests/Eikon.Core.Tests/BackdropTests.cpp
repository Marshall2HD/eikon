#include "pch.h"
#include "TestFramework.h"

#include "Core/Backdrop.h"
#include "Core/Document.h"
#include "Core/Noise.h"

#include <cmath>

using namespace Eikon::Core;
using namespace Eikon::Core::Backdrop;

// Expected values are worked out by hand, or are reference noise samples recorded once from a
// reference Perlin implementation.

namespace
{
    Rgb Bytes(int r, int g, int b) { return { r / 255.0, g / 255.0, b / 255.0 }; }

    bool Near(Rgb a, Rgb b, double tolerance = 1e-9)
    {
        return std::fabs(a.r - b.r) <= tolerance && std::fabs(a.g - b.g) <= tolerance && std::fabs(a.b - b.b) <= tolerance;
    }

    Pads Uniform(double p)
    {
        Pads pads;
        pads.bt = pads.bb = pads.bl = pads.br = p;
        return pads;
    }
}

// ---- Noise reference samples ----

TEST(Noise_MeshMapMatchesReferenceSamples)
{
    // Mesh field: frequency 1/40, one octave, persistence 0.01, lacunarity 1, 80 × 80 samples
    // over (0,0)…(80,80), seamless.
    Noise::Perlin const noise{ 1.0 / 40.0, 1, 0.01, 1.0, 12345 };
    auto const map = Noise::Map(noise, 80, 80, 0, 0, 80, 80, true);
    CHECK_EQ(map.size(), std::size_t{ 6400 });
    CHECK_NEAR(map[0], 0.0, 1e-7);
    CHECK_NEAR(map[1], -0.0467851944, 1e-7);            // (1, 0)
    CHECK_NEAR(map[81], -0.042751912, 1e-7);            // (1, 1)
    CHECK_NEAR(map[40 * 80 + 40], 0.0631760582, 1e-7);  // (40, 40)
    CHECK_NEAR(map[6399], 0.0, 1e-7);                   // (79, 79)
}

TEST(Noise_GrainMapMatchesReferenceSamples)
{
    // Grain: frequency 10/7, two octaves, persistence 0.2, lacunarity 0.175, 256 × 256.
    Noise::Perlin const noise{ 10.0 / 7.0, 2, 0.2, 0.175, 12345 };
    auto const map = Noise::Map(noise, 256, 256, 0, 0, 256, 256, true);
    CHECK_NEAR(map[1], 0.255446702, 1e-7);              // (1, 0)
    CHECK_NEAR(map[256 + 43], -0.182152346, 1e-7);      // (43, 1)
    CHECK_NEAR(map[156 * 256 + 63], -0.173659697, 1e-7);// (63, 156)
    CHECK_NEAR(map[65535], 0.240476057, 1e-7);          // (255, 255)
}

TEST(Noise_ClampsToMinusOne)
{
    // Exactly -1 at this sample, where the raw Perlin sum is about -1.27.
    Noise::Perlin const noise{ 1.25, 1, 0.1, 1.0, 4242 };
    auto const map = Noise::Map(noise, 256, 256, 0, 0, 256, 256, false);
    CHECK_EQ(map[2 * 256 + 29], -1.0f);
    CHECK(noise.Value(29.113725490196078, 0, 2.007843137254902) < -1.2);
}

TEST(Noise_MapSpansOriginToSize)
{
    // A map of size (2,2), origin (5,7), 3 samples samples x = 5, 3.5, 2 and y = 7, 4.5, 2: whole
    // numbers are lattice points (0); the reference gives 0.621565461 at (1, 0).
    Noise::Perlin const noise{ 1.0, 1, 0.01, 1.0, 0 };
    auto const map = Noise::Map(noise, 2, 2, 5, 7, 3, 3, false);
    CHECK_NEAR(map[0], 0.0, 1e-12);
    CHECK_NEAR(map[1], 0.621565461, 1e-7);
    CHECK_NEAR(map[2], 0.0, 1e-12);
    CHECK_NEAR(map[4], 0.366525739, 1e-7);
}

// ---- Config and JSON ----

TEST(Backdrop_DefaultConfigMatchesSpec)
{
    Config const c = DefaultConfig(77);
    CHECK_EQ(c.name, std::wstring{ L"default" });
    CHECK_EQ(c.mode, std::wstring{ L"mesh" });
    CHECK_EQ(c.colors, std::wstring{ L"091022,0D2162,144CA0,2B91E2,56C3FF,9AEBFF" });
    CHECK_EQ(c.stops, std::wstring{ L"0.15,0.3,0.6,0.8" });
    CHECK_EQ(c.gradStart, std::wstring{ L"0.5,1.1" });
    CHECK_EQ(c.gradEnd, std::wstring{ L"-0.33,-0.15" });
    CHECK_EQ(c.gradType, std::wstring{ L"radial" });
    CHECK(c.meshType && *c.meshType == L"mesh");
    CHECK_EQ(c.noise, 0.03);
    CHECK_EQ(c.padding, 68.0);
    CHECK_EQ(c.inset, -1.0);
    CHECK_EQ(c.innerRadius, 10.0);
    CHECK_EQ(c.outerRadius, 0.0);
    CHECK_EQ(c.shadowIntensity, 0.5);
    CHECK_EQ(c.isOn, std::int64_t{ 0 });
    CHECK_EQ(c.seed, std::int64_t{ 77 });
    for (int i = 0; i < 50; ++i)
    {
        auto const seed = RandomSeed();
        CHECK(seed >= 0 && seed < 32'000'000);
    }
}

TEST(Backdrop_JsonKeysInCodingKeysOrder)
{
    Config c = DefaultConfig(5);
    JsonValue const json = ToJson(c);
    std::vector<std::wstring> keys;
    for (auto const& [key, value] : json.Members())
    {
        keys.push_back(key);
    }
    std::vector<std::wstring> const expected = { L"name", L"mode", L"colors", L"stops", L"gradStart", L"gradEnd", L"gradType",
                                                 L"meshType", L"noise", L"padding", L"inset", L"innerRadius", L"outerRadius",
                                                 L"ratio", L"minWidth", L"seed", L"shadowIntensity", L"isOn", L"filepath", L"blur" };
    CHECK_EQ(keys, expected);
    c.meshType.reset();
    CHECK(ToJson(c).Find(L"meshType") == nullptr); // nil optionals are left out
    CHECK(Encode(DefaultConfig(5)).find(L"\"seed\":5,") != std::wstring::npos);
}

TEST(Backdrop_JsonRoundTrip)
{
    Config original = DefaultConfig(31'999'999);
    original.mode = L"gradient";
    original.padding = 40;
    original.ratio = 1.777777778;
    original.minWidth = 1200;
    original.inset = 32;
    original.isOn = 1;
    original.filepath = L"C:\\x\\backdrop-fs-1.jpeg";
    original.blur = 5;
    auto const parsed = JsonValue::Parse(Encode(original));
    CHECK(parsed.has_value());
    Config restored = DefaultConfig(1);
    restored.isOn = 1; // applied while on, so inset/ratio/minWidth are kept
    Apply(restored, *parsed);
    CHECK(restored == original);
}

TEST(Backdrop_ApplyWhileOffResetsInsetRatioMinWidth)
{
    // Tests isOn as it was before the dictionary was applied.
    Config saved = DefaultConfig(9);
    saved.isOn = 1;
    saved.ratio = 1.5;
    saved.inset = 32;
    saved.minWidth = 1000;
    saved.padding = 20;
    Config c = DefaultConfig(1);
    Apply(c, ToJson(saved));
    CHECK_EQ(c.isOn, std::int64_t{ 1 });
    CHECK_EQ(c.ratio, 0.0);
    CHECK_EQ(c.inset, -1.0);
    CHECK_EQ(c.minWidth, 0.0);
    CHECK_EQ(c.padding, 20.0);  // padding persists
    CHECK_EQ(c.seed, std::int64_t{ 9 }); // so does the seed
}

TEST(Backdrop_ApplyCopiesOnlyPresentKeysOfTheRightType)
{
    Config c = DefaultConfig(3);
    c.isOn = 1;
    c.ratio = 1.5;
    c.padding = 50;
    JsonValue dict = JsonValue::Object();
    dict.Set(L"colors", JsonValue::String(L"#112233"));
    dict.Set(L"noise", JsonValue::String(L"0.5"));  // wrong type: ignored
    dict.Set(L"seed", JsonValue::Number(12.5));     // not an Int: ignored
    dict.Set(L"padding", JsonValue::Number(10));
    Apply(c, dict);
    CHECK_EQ(c.colors, std::wstring{ L"#112233" });
    CHECK_EQ(c.noise, 0.03);
    CHECK_EQ(c.seed, std::int64_t{ 3 });
    CHECK_EQ(c.padding, 10.0);
    CHECK_EQ(c.ratio, 1.5);       // on before: kept
    CHECK(!c.meshType.has_value()); // meshType missing -> nil
}

// ---- Templates ----

TEST(Backdrop_TemplatesMatchSpec)
{
    auto const& gradients = GradientTemplates();
    auto const& solids = SolidTemplates();
    CHECK_EQ(gradients.size(), std::size_t{ 17 });
    CHECK_EQ(solids.size(), std::size_t{ 13 });
    CHECK_EQ(TemplateName(gradients[0]), std::wstring{ L"electric-blue" });
    CHECK_EQ(TemplateName(gradients[15]), std::wstring{ L"lead-clouds" });
    CHECK_EQ(TemplateName(gradients[16]), std::wstring{ L"custom-saved-gradient" });
    CHECK_EQ(TemplateName(solids[3]), std::wstring{ L"s-navy" });
    CHECK_EQ(TemplateName(solids[12]), std::wstring{ L"custom-saved-color" });
    CHECK_EQ(TemplateTag(gradients[16], 16), kCustomTag);
    CHECK_EQ(TemplateTag(solids[12], 12), 888);
    CHECK_EQ(TemplateTag(gradients[5], 5), 5);

    // Gradient templates carry no mode; meshType only on the "grad" ones.
    CHECK(gradients[0].Find(L"mode") == nullptr);
    CHECK(gradients[0].Find(L"meshType") == nullptr);
    CHECK_EQ(gradients[2].Find(L"meshType")->AsString(), std::wstring{ L"grad" });
    CHECK_EQ(gradients[4].Find(L"meshType")->AsString(), std::wstring{ L"grad" });
    CHECK_EQ(gradients[5].Find(L"meshType")->AsString(), std::wstring{ L"grad" });
    CHECK_EQ(gradients[0].Find(L"noise")->AsNumber(), 0.04);
    CHECK_EQ(gradients[15].Find(L"noise")->AsNumber(), 0.12);
    CHECK_EQ(solids[0].Find(L"noise")->AsNumber(), 0.32);
    CHECK_EQ(solids[2].Find(L"noise")->AsNumber(), 0.12);
    CHECK_EQ(solids[3].Find(L"noise")->AsNumber(), 0.06);
    CHECK_EQ(solids[3].Find(L"mode")->AsString(), std::wstring{ L"flat" });
    CHECK_EQ(solids[3].Find(L"colors")->AsString(), std::wstring{ L"#064F77" });
}

TEST(Backdrop_EveryTemplateParses)
{
    // Colours as stored (some lack "#"): every entry must be valid hex, and colours/stops must line
    // up so the ramp is not the zeroed mismatch ramp.
    for (auto const& t : GradientTemplates())
    {
        std::wstring const colors = t.Find(L"colors")->AsString();
        auto const parsed = ParseColors(colors);
        std::size_t start = 0;
        for (auto const& c : parsed)
        {
            std::size_t const comma = colors.find(L',', start);
            auto const raw = colors.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            CHECK(ParseHex(raw).has_value());
            CHECK(Near(c, *ParseHex(raw)));
            start = comma + 1;
        }
        auto const ramp = Ramp(parsed, ParseStops(t.Find(L"stops")->AsString()));
        CHECK_EQ(ramp[0] >> 24, 0xFFu);
        CHECK_EQ(ramp[255] >> 24, 0xFFu);
    }
    // orange's first colour has no "#".
    CHECK(Near(ParseColors(L"FEB142,#F06A20,#B61B14")[0], Bytes(0xFE, 0xB1, 0x42)));
}

// ---- Colours ----

TEST(Backdrop_ParseColorsAndStops)
{
    auto const colors = ParseColors(L"#ff0000, 00ff00 ,nothex,#12345");
    CHECK_EQ(colors.size(), std::size_t{ 4 });
    CHECK(Near(colors[0], Bytes(255, 0, 0)));
    CHECK(Near(colors[1], Bytes(0, 255, 0)));
    CHECK(Near(colors[2], Rgb{}));  // invalid -> black
    CHECK(Near(colors[3], Rgb{}));
    CHECK(Near(ParseColors(L"")[0], Bytes(0x20, 0x20, 0x20)));
    CHECK_EQ(ParseColors(L"").size(), std::size_t{ 1 });

    CHECK_EQ(ParseStops(L"0.15,0.3,x,1"), (std::vector<double>{ 0.15, 0.3, 0.0, 1.0 }));
    CHECK_EQ(ParseStops(L" 0.5"), (std::vector<double>{ 0.0 })); // no surrounding spaces
    CHECK(ParseStops(L"").empty());
}

TEST(Backdrop_RampFollowsStops)
{
    // Black -> white with no stops: 0 and 1 are added; the ramp is the identity.
    auto const grey = Ramp({ Rgb{ 0, 0, 0 }, Rgb{ 1, 1, 1 } }, {});
    CHECK_EQ(grey[0], 0xFF000000u);
    CHECK_EQ(grey[255], 0xFFFFFFFFu);
    CHECK_EQ(grey[128] & 0xFF, 128u);
    CHECK_EQ(grey[51] & 0xFF, 51u);
    // Red -> blue: R falls, B rises, packed R | G << 8 | B << 16 | A << 24.
    auto const rb = Ramp({ Bytes(255, 0, 0), Bytes(0, 0, 255) }, {});
    CHECK_EQ(rb[0], 0xFF0000FFu);
    CHECK_EQ(rb[255], 0xFFFF0000u);
    // Counts that cannot line up give the zeroed ramp (transparent).
    auto const mismatch = Ramp({ Bytes(1, 2, 3), Bytes(4, 5, 6), Bytes(7, 8, 9) }, { 0.2, 0.4, 0.6, 0.8 });
    CHECK_EQ(mismatch[0], 0u);
    CHECK_EQ(mismatch[200], 0u);
    auto const noStops = Ramp({ Bytes(1, 2, 3), Bytes(4, 5, 6), Bytes(7, 8, 9) }, {});
    CHECK_EQ(noStops[100], 0u);
}

TEST(Backdrop_BaseAndDarkestColor)
{
    // electric-blue: entry 128 lies between 2B91E2 (0.37) and 144CA0 (0.64), t = 0.4887.
    auto const colors = ParseColors(L"9AE6FF,56C3FF,2B91E2,144CA0,0D2162,091022");
    auto const stops = ParseStops(L"0.12,0.37,0.64,0.87");
    CHECK(Near(BaseColor(colors, stops), Bytes(31, 111, 193)));
    CHECK(Near(DarkestColor(colors), Bytes(0x09, 0x10, 0x22)));
    CHECK(Near(BaseColor({ Bytes(10, 20, 30) }, {}), Bytes(10, 20, 30)));
    CHECK(Near(BaseColor({}, {}), Rgb{ 0.5, 0.5, 0.5 }));
    // Nothing below brightness 1: black.
    CHECK(Near(DarkestColor({ Rgb{ 1, 1, 1 }, Rgb{ 1, 0, 0 } }), Rgb{}));
}

TEST(Backdrop_LightnessIsOklab)
{
    CHECK_NEAR(Lightness({ 1, 1, 1 }), 1.0, 1e-4);
    CHECK_NEAR(Lightness({ 0, 0, 0 }), 0.0, 1e-9);
    CHECK_NEAR(Lightness(Bytes(128, 128, 128)), 0.5999, 1e-3); // OKLab L of #808080
    CHECK_NEAR(Lightness({ 1, 0, 0 }), 0.6279, 1e-3);          // OKLab L of pure red
}

// ---- Gradients ----

TEST(Backdrop_GradientTypeMenu)
{
    Config c = DefaultConfig(1);
    ApplyGradientType(c, GradientType::Vertical);
    CHECK_EQ(c.mode, std::wstring{ L"gradient" });
    CHECK_EQ(c.gradType, std::wstring{ L"linear" });
    CHECK_EQ(c.gradStart, std::wstring{ L"0.5,1.0" });
    CHECK_EQ(c.gradEnd, std::wstring{ L"0.5,0.0" });
    CHECK_EQ(SelectedGradientType(c), GradientType::Vertical);
    ApplyGradientType(c, GradientType::Diagonal);
    CHECK_EQ(c.gradStart, std::wstring{ L"0.0,1.0" });
    CHECK_EQ(c.gradEnd, std::wstring{ L"1.0,0.0" });
    CHECK_EQ(SelectedGradientType(c), GradientType::Diagonal);
    ApplyGradientType(c, GradientType::Radial);
    CHECK_EQ(c.gradType, std::wstring{ L"radial" });
    CHECK_EQ(c.gradStart, std::wstring{ L"0.5,1.1" });
    CHECK_EQ(c.gradEnd, std::wstring{ L"-0.45,-0.15" });
    CHECK_EQ(SelectedGradientType(c), GradientType::Radial);
    ApplyGradientType(c, GradientType::Mesh);
    CHECK_EQ(c.mode, std::wstring{ L"mesh" });
    CHECK_EQ(c.gradStart, std::wstring{ L"0.5,1.1" }); // the rest is unchanged
    CHECK_EQ(SelectedGradientType(c), GradientType::Mesh);
}

TEST(Backdrop_ConfigGradientLocations)
{
    Config c = DefaultConfig(1);
    ApplyGradientType(c, GradientType::Radial);
    Gradient const g = ConfigGradient(c);
    CHECK(g.radial);
    CHECK_EQ(g.stops.size(), std::size_t{ 6 });
    std::vector<double> locations;
    for (auto const& s : g.stops)
    {
        locations.push_back(s.location);
    }
    CHECK_EQ(locations, (std::vector<double>{ 0.0, 0.15, 0.3, 0.6, 0.8, 1.0 }));
    CHECK_NEAR(g.start.x, 0.5, 1e-12);
    CHECK_NEAR(g.start.y, 1.1, 1e-12);
    CHECK_NEAR(g.end.x, -0.45, 1e-12);
    // Stops that already start with 0 and end with 1 are kept as they are.
    c.colors = L"#1F2A43,#758695";
    c.stops = L"0.00,1.00";
    Gradient const two = ConfigGradient(c);
    CHECK_EQ(two.stops.size(), std::size_t{ 2 });
    CHECK_EQ(two.stops[1].location, 1.0);
}

TEST(Backdrop_FlatGradient)
{
    // Radial from the centre (0.5,0.5) to (-0.19,-0.19), ten locations i/9.
    Rgb const navy = Bytes(0x06, 0x4F, 0x77);
    Gradient const g = FlatGradient(navy);
    CHECK(g.radial);
    CHECK_NEAR(g.start.x, 0.5, 1e-12);
    CHECK_NEAR(g.end.x, -0.19, 1e-12);
    CHECK_EQ(g.stops.size(), std::size_t{ 10 });
    CHECK_NEAR(g.stops[3].location, 3.0 / 9.0, 1e-12);
    CHECK(Near(g.stops[0].color, navy, 1e-9));
    double const d = 0.2 * std::exp(-1.9 * Lightness(navy));
    Hsb const base = ToHsb(navy);
    Hsb const edge = ToHsb(g.stops[9].color);
    CHECK_NEAR(edge.b, base.b - d, 1e-9);
    CHECK_NEAR(edge.s, std::min(1.0, base.s + d), 1e-9);
    CHECK_NEAR(edge.h, base.h, 1e-9);
    // Easing (15^t − 1) / 14 at stop 1.
    double const k1 = (std::pow(15.0, 1.0 / 9.0) - 1) / 14;
    CHECK_NEAR(ToHsb(g.stops[1].color).b, base.b - d * k1, 1e-9);
    // White (L > 0.99) is flat; greys keep saturation 0.
    for (auto const& stop : FlatGradient({ 1, 1, 1 }).stops)
    {
        CHECK(Near(stop.color, Rgb{ 1, 1, 1 }, 1e-12));
    }
    CHECK_NEAR(ToHsb(FlatGradient(Bytes(0x90, 0x94, 0x98)).stops[9].color).s,
               std::min(1.0, ToHsb(Bytes(0x90, 0x94, 0x98)).s + 0.2 * std::exp(-1.9 * Lightness(Bytes(0x90, 0x94, 0x98)))), 1e-9);
    CHECK_NEAR(ToHsb(FlatGradient(Bytes(0x80, 0x80, 0x80)).stops[9].color).s, 0.0, 1e-12);
}

TEST(Backdrop_Reverse)
{
    Config c = DefaultConfig(1);
    c.colors = L"9AE6FF,56C3FF,2B91E2,144CA0,0D2162,091022";
    c.stops = L"0.12,0.37,0.64,0.87";
    Reverse(c);
    CHECK_EQ(c.colors, std::wstring{ L"091022,0D2162,144CA0,2B91E2,56C3FF,9AE6FF" });
    CHECK_EQ(c.stops, std::wstring{ L"0.13,0.36,0.63,0.88" });
    c.stops = L"0.00,1.00";
    Reverse(c);
    CHECK_EQ(c.stops, std::wstring{ L"0.0,1.0" });
    c.stops = L"x";
    Reverse(c);
    CHECK_EQ(c.stops, std::wstring{ L"1.0" }); // unparsable -> 1.0
}

// ---- Sliders ----

TEST(Backdrop_SliderMaps)
{
    CHECK_EQ(PaddingFromSlider(12), 72.0);                  // floor(144 / 2)
    CHECK_EQ(PaddingFromSlider(11.66), 67.0);               // floor(67.98)
    CHECK_EQ(PaddingFromSlider(20), 200.0);
    CHECK_NEAR(SliderFromPadding(68), std::sqrt(136.0), 1e-12);
    CHECK_EQ(ShadowFromSlider(10), 0.5);
    CHECK_EQ(SliderFromShadow(0.5), 10.0);
    CHECK_EQ(InsetFromSlider(12), 80.0);                    // 144 / 1.8
    CHECK_EQ(InsetFromSlider(20), 222.0);                   // 222.2
    CHECK_EQ(SliderFromInset(-1), 0.0);
    CHECK_NEAR(SliderFromInset(80), 12.0, 1e-12);
    CHECK(!InsetSliderApplies(-1, 0));                      // a fresh auto inset stays -1
    CHECK(InsetSliderApplies(-1, 0.5));
    CHECK(InsetSliderApplies(32, 0));
    CHECK(!InsetSliderApplies(0, 0));
    CHECK_EQ(RadiusFromSlider(10), 20.0);
    CHECK_EQ(SliderFromRadius(10), 5.0);
    CHECK_EQ(BlurFromSlider(0), 0.0);
    CHECK_EQ(BlurFromSlider(1), 3.0);
    CHECK_EQ(BlurFromSlider(7), 9.0);
    CHECK_EQ(SliderFromBlur(0), 0.0);
    CHECK_EQ(SliderFromBlur(3), 1.0);
    CHECK_EQ(SliderFromBlur(9), 7.0);
    Config c = DefaultConfig(1);
    c.padding = 0;
    CHECK(!InnerRadiusEnabled(c));
    c.ratio = 1;
    CHECK(InnerRadiusEnabled(c));
}

// ---- Aspect popup ----

TEST(Backdrop_AspectPresets)
{
    CHECK_EQ(AspectForItem(L"Reset").ratio, 0.0);
    CHECK_EQ(AspectForItem(L"1:1").ratio, 1.0);
    CHECK_NEAR(AspectForItem(L"4:3").ratio, 4.0 / 3.0, 1e-12);
    CHECK_NEAR(AspectForItem(L"3:2").ratio, 1.5, 1e-12);
    CHECK_NEAR(AspectForItem(L"16:9").ratio, 16.0 / 9.0, 1e-12);
    CHECK_EQ(AspectForItem(L"16:9").minWidth, 0.0);
    CHECK_EQ(AspectForItem(L"Twitter").ratio, 1.777777778);
    CHECK_EQ(AspectForItem(L"Twitter").minWidth, 1200.0);
    CHECK_EQ(AspectForItem(L"YouTube").minWidth, 1280.0);
    CHECK_EQ(AspectForItem(L"Pinterest").ratio, 0.666666667);
    CHECK_EQ(AspectForItem(L"Pinterest").minWidth, 1000.0);
    CHECK_EQ(AspectForItem(L"Facebook").ratio, 1.904761905);
    CHECK_EQ(AspectForItem(L"LinkedIn").ratio, 1.913875598);
    CHECK_EQ(AspectForItem(L"LinkedIn").minWidth, 1200.0);
    // a/b only within 0.3…3.
    CHECK_EQ(AspectForItem(L"3:1").ratio, 3.0);
    CHECK_EQ(AspectForItem(L"4:1").ratio, 0.0);
    CHECK_NEAR(AspectForItem(L"3:10").ratio, 0.3, 1e-12);
    CHECK_EQ(AspectForItem(L"1:4").ratio, 0.0);

    CHECK_EQ(AspectButtonTitle(L"Reset"), std::wstring{ L"\u2022\u2022\u2022" });
    CHECK_EQ(AspectButtonTitle(L"Twitter"), std::wstring{ L"Tw" });
    CHECK_EQ(AspectButtonTitle(L"Facebook"), std::wstring{ L"Fb" });
    CHECK_EQ(AspectButtonTitle(L"LinkedIn"), std::wstring{ L"Ln" });
    CHECK_EQ(AspectButtonTitle(L"Pinterest"), std::wstring{ L"Pi" });
    CHECK_EQ(AspectButtonTitle(L"YouTube"), std::wstring{ L"Yt" });
    CHECK_EQ(AspectButtonTitle(L"16:9"), std::wstring{ L"16:9" });
}

// ---- Layout ----

TEST(Backdrop_PaddingRatioMinWidth)
{
    Config c = DefaultConfig(1); // padding 68
    Pads pads;
    ApplyPadding(pads, c, 2, 1000, 500);
    CHECK(pads == Uniform(136));
    CHECK_EQ(CanvasWidth(pads, 1000), 1272.0);
    CHECK_EQ(CanvasHeight(pads, 500), 772.0);

    c.ratio = 16.0 / 9.0; // 1272/772 = 1.648 < 1.778: widen
    pads = {};
    ApplyPadding(pads, c, 2, 1000, 500);
    CHECK_EQ(pads.bl, 186.0); // floor((1.7778 × 772 − 1000) / 2)
    CHECK_EQ(pads.br, 186.0);
    CHECK_EQ(pads.bt, 136.0);

    c.ratio = 1; // 1.648 >= 1: grow vertically
    pads = {};
    ApplyPadding(pads, c, 2, 1000, 500);
    CHECK_EQ(pads.bt, 386.0); // floor((1272 − 500) / 2)
    CHECK_EQ(pads.bl, 136.0);

    // Twitter on 800 × 300 at 1× with padding 0: 20 pt padding, widen to 1200, then the ratio.
    c.padding = 0;
    c.ratio = 1.777777778;
    c.minWidth = 1200;
    pads = {};
    ApplyPadding(pads, c, 1, 800, 300);
    CHECK_EQ(pads.bl, 200.0);
    CHECK_EQ(pads.bt, 187.0); // floor((674.99999992 − 300) / 2)

    // Padding 0 and no ratio: no padding at all.
    c.ratio = 0;
    c.minWidth = 0;
    pads = {};
    ApplyPadding(pads, c, 2, 100, 100);
    CHECK(pads == Uniform(0));
}

TEST(Backdrop_CanvasExpansion)
{
    Config c = DefaultConfig(1); // inset -1 (auto)
    Rect const source{ 0, 0, 100, 100 };
    Pads pads;
    // An object overflowing on the left: auto margin 20 pt on every side.
    ExpandCanvas(pads, c, 1, source, Rect{ -30, 0, 130, 100 }, true);
    CHECK_EQ(pads.cl, 50.0);
    CHECK_EQ(pads.cr, 20.0);
    CHECK_EQ(pads.ct, 20.0);
    CHECK_EQ(pads.cb, 20.0);
    // Nothing overflows and the inset is auto: no expansion.
    ExpandCanvas(pads, c, 1, source, source, true);
    CHECK(pads.ct == 0 && pads.cb == 0 && pads.cl == 0 && pads.cr == 0);
    // A set inset applies without overflow, × dpr.
    c.inset = 10;
    ExpandCanvas(pads, c, 2, source, source, true);
    CHECK_EQ(pads.cl, 20.0);
    CHECK_EQ(pads.cb, 20.0);
    // Caps: 1920 × dpr sideways, 1280 × dpr vertically.
    c.inset = -1;
    ExpandCanvas(pads, c, 1, source, Rect{ 0, 0, 5000, 5000 }, true);
    CHECK_EQ(pads.cr, 1920.0);
    CHECK_EQ(pads.cb, 1280.0);
    ExpandCanvas(pads, c, 2, source, Rect{ 0, 0, 5000, 5000 }, true);
    CHECK_EQ(pads.cr, 3840.0);
    CHECK_EQ(pads.cb, 2560.0);
    // expandableCanvas off: zeros.
    ExpandCanvas(pads, c, 1, source, Rect{ -30, 0, 130, 100 }, false);
    CHECK_EQ(pads.cl, 0.0);
}

// The output canvas is crop + canvas expansion + padding; the screenshot sits at
// (padBL + padCL, padBT + padCT) with y down.
TEST(Backdrop_LayoutPlacesScreenshotOnCanvas)
{
    Config c = DefaultConfig(1); // padding 68, inner radius 10, inset auto
    c.isOn = 1;
    Rect const crop{ 10, 20, 400, 300 };
    Layout const plain = ComputeLayout(c, 2, crop, crop, true);
    CHECK_EQ(plain.canvasW, 672.0); // 400 + 2 × 136
    CHECK_EQ(plain.canvasH, 572.0);
    CHECK(plain.screenshot == (Rect{ 136, 136, 400, 300 }));
    CHECK(plain.content == plain.screenshot);
    CHECK_EQ(plain.outerPx, 0.0);
    CHECK_EQ(plain.innerPx, 20.0); // 10 pt × dpr 2
    CHECK(plain.layerVisible);

    // An object 50 px past the right edge: auto margin 40 px (20 pt × 2) on every side.
    Layout const grown = ComputeLayout(c, 2, crop, Rect{ 10, 20, 450, 300 }, true);
    CHECK_EQ(grown.pads.cr, 90.0);
    CHECK_EQ(grown.pads.cl, 40.0);
    CHECK_EQ(grown.canvasW, 802.0); // 400 + 40 + 90 + 272
    CHECK(grown.content == (Rect{ 136, 136, 530, 380 }));
    CHECK(grown.screenshot == (Rect{ 176, 176, 400, 300 }));

    // Off: the crop itself, no radii.
    c.isOn = 0;
    Layout const off = ComputeLayout(c, 2, crop, Rect{ 10, 20, 450, 300 }, true);
    CHECK_EQ(off.canvasW, 400.0);
    CHECK_EQ(off.canvasH, 300.0);
    CHECK(off.screenshot == (Rect{ 0, 0, 400, 300 }));
    CHECK_EQ(off.innerPx, 0.0);
    CHECK(!off.layerVisible);
}

TEST(Backdrop_CanvasPixelCountUsesTheRightPadForHeight)
{
    Pads pads;
    pads.cr = 1000; // padCR is counted in the height term instead of padCT
    CHECK_EQ(CanvasPixelsForLimits(pads, 1000, 1000), 4'000'000.0);
    pads.cr = 0;
    pads.ct = 1000;
    CHECK_EQ(CanvasPixelsForLimits(pads, 1000, 1000), 1'000'000.0);
    CHECK_EQ(Megapixels(12'345'678), 12.3);
    CHECK_EQ(Megapixels(15'099'999), 15.0);
    CHECK_EQ(Megapixels(15'100'000), 15.1);
}

TEST(Backdrop_CornerRadii)
{
    Config c = DefaultConfig(1);
    c.outerRadius = 20;
    CHECK_EQ(OuterRadiusPx(c, 2, 60, 1000), 30.0); // half the shorter side caps 40
    CHECK_EQ(OuterRadiusPx(c, 2, 600, 1000), 40.0);

    Pads p = Uniform(136);
    c.innerRadius = 10;
    CHECK_EQ(InnerRadiusPx(c, 2, p, 1000, 500, 0), 20.0);
    CHECK_EQ(InnerRadiusPx(c, 2, p, 1000, 500, 40), 20.0);        // outer <= m: min(0.5 outer, r)
    c.innerRadius = 30;
    CHECK_EQ(InnerRadiusPx(c, 2, p, 1000, 500, 40), 20.0);
    // Little padding: r >= outer − m, then r <= outer − m^0.9.
    p = Uniform(10);
    c.innerRadius = 5;
    CHECK_EQ(InnerRadiusPx(c, 2, p, 1000, 500, 40), 30.0);
    c.innerRadius = 40;
    CHECK_NEAR(InnerRadiusPx(c, 2, p, 1000, 500, 40), 40 - std::pow(10.0, 0.9), 1e-12);
    // No padding: the screenshot shares the outer radius.
    CHECK_EQ(InnerRadiusPx(c, 2, Uniform(0), 1000, 500, 24), 24.0);
}

// ---- Shadow ----

TEST(Backdrop_ShadowColourAndOpacity)
{
    CHECK_NEAR(ShadowTargetOpacity(0.5), 0.216, 1e-12);
    CHECK_NEAR(ShadowTargetOpacity(0), 0.36, 1e-12);
    CHECK_NEAR(ShadowTargetOpacity(1), 0.072, 1e-12);
    Hsb const s = ToHsb(ShadowColor(FromHsb({ 0.6, 0.5, 0.4 })));
    CHECK_NEAR(s.h, 0.6, 1e-9);
    CHECK_NEAR(s.s, 1 - 0.5 * std::pow(0.5, 4), 1e-9); // 0.96875
    CHECK_NEAR(s.b, 0.1, 1e-9);
    CHECK_NEAR(ToHsb(ShadowColor(Bytes(100, 100, 100))).s, 0.0, 1e-12); // grey stays grey
}

TEST(Backdrop_FiveShadowLayers)
{
    Config c = DefaultConfig(1); // intensity 0.5
    Pads const p = Uniform(136);
    auto const layers = ShadowLayers(c, p, 1000, 500, 20, 0.216, false);
    CHECK_EQ(layers.size(), std::size_t{ 5 });
    // S = min(136 × 0.8, 1000, 80) = 80, D = 80, base blur 4.5, q = 4 / 0.501.
    double const q = 4 / 0.501;
    double const t1 = (std::pow(q, 0.2) - 1) / (q - 1);
    CHECK_EQ(layers[0].frame.x, 137.0); // insetX = floor(48 × t1²) = 0
    CHECK_EQ(layers[0].frame.y, 137.0);
    CHECK_EQ(layers[0].frame.width, 998.0);
    CHECK_EQ(layers[0].frame.height, 498.0);
    CHECK_NEAR(layers[0].offsetY, 56 * t1, 1e-9);
    CHECK_EQ(layers[0].radius, 6.5);   // 4.5 + floor(40 × 0.0738)
    CHECK_NEAR(layers[0].opacity, 0.216 * 1.5 * (0.5 * std::sqrt(1 - t1) + 0.5), 1e-9);
    // Last layer: t = 1.
    CHECK_EQ(layers[4].frame.x, 185.0); // 136 + 1 + floor(80 × 0.6)
    CHECK_EQ(layers[4].frame.width, 902.0);
    CHECK_NEAR(layers[4].offsetY, 56.0, 1e-9);
    CHECK_EQ(layers[4].radius, 44.5);
    CHECK_NEAR(layers[4].opacity, 0.216 * 0.75, 1e-9);
    CHECK_EQ(layers[4].cornerRadius, 20.0);
    // Corner radius is capped by half the layer.
    auto const flat = ShadowLayers(c, p, 1000, 30, 50, 0.216, false);
    CHECK_EQ(flat[0].cornerRadius, 14.0);
}

TEST(Backdrop_ShadowThresholds)
{
    Config c = DefaultConfig(1);
    Pads p = Uniform(50);
    // 4000 × 3000 = 12 MP: one cheap layer. S = 40, D = 40.
    auto const big = ShadowLayers(c, p, 4000, 3000, 0, 0.216, false);
    CHECK_EQ(big.size(), std::size_t{ 1 });
    CHECK_NEAR(big[0].offsetY, 0.5 * 40 / 3 - 3, 1e-9);   // 3 − I·D/3 in y-up terms
    CHECK_EQ(big[0].radius, 19.5);                         // 4.5 + floor(0.75 × 40 × 0.5)
    CHECK_NEAR(big[0].opacity, 0.6, 1e-12);
    CHECK_EQ(big[0].frame.x, 51.0);
    // 9.9 MP still five layers.
    CHECK_EQ(ShadowLayers(c, p, 3300, 3000, 0, 0.216, false).size(), std::size_t{ 5 });
    // Over 20 MP, a transparent canvas, or intensity 0: none.
    CHECK(ShadowLayers(c, p, 5000, 4100, 0, 0.216, false).empty());
    CHECK(ShadowLayers(c, p, 100, 100, 0, 0.216, true).empty());
    c.shadowIntensity = 0;
    CHECK(ShadowLayers(c, p, 100, 100, 0, 0.216, false).empty());
}

// ---- Bitmaps ----

TEST(Backdrop_MeshIsDeterministicPerSeed)
{
    Config c = DefaultConfig(1234);
    Bitmap const a = Mesh(c);
    Bitmap const b = Mesh(c);
    CHECK_EQ(a.width, 68);  // 80 − 2 × (2 × 3)
    CHECK_EQ(a.height, 68);
    CHECK(a.pixels == b.pixels);
    c.seed = 1235;
    CHECK(Mesh(c).pixels != a.pixels);
    c.meshType = L"grad";
    c.seed = 1234;
    CHECK(Mesh(c).pixels != a.pixels);
}

TEST(Backdrop_MeshUsesOnlyPaletteColours)
{
    // A one-colour palette gives a one-colour template, whatever the seed and generator.
    for (bool grad : { false, true })
    {
        Bitmap const tpl = MeshTemplate(80, { Bytes(10, 200, 30) }, {}, 777, grad);
        CHECK_EQ(tpl.width, 80);
        bool same = true;
        for (std::size_t i = 0; i < tpl.pixels.size(); i += 4)
        {
            same = same && tpl.pixels[i] == 30 && tpl.pixels[i + 1] == 200 && tpl.pixels[i + 2] == 10 && tpl.pixels[i + 3] == 255;
        }
        CHECK(same);
    }
    // "grad": rows run down the ramp, so the top row is near the first colour and the bottom row near
    // the last.
    Bitmap const grad = MeshTemplate(80, { Bytes(0, 0, 0), Bytes(255, 255, 255) }, {}, 42, true);
    int top = 0;
    int bottom = 0;
    for (int x = 0; x < 80; ++x)
    {
        top += grad.pixels[static_cast<std::size_t>(x) * 4];
        bottom += grad.pixels[grad.Stride() * 79 + static_cast<std::size_t>(x) * 4];
    }
    CHECK(top < bottom);
}

TEST(Backdrop_GrainAndShadowMaskRanges)
{
    Bitmap const grain = Grain(12345);
    Bitmap const mask = ShadowMask(12345);
    CHECK_EQ(grain.width, 256);
    CHECK_EQ(mask.height, 256);
    int grainMin = 255, grainMax = 0, maskMin = 255, maskMax = 0;
    for (std::size_t i = 0; i < grain.pixels.size(); i += 4)
    {
        grainMin = std::min<int>(grainMin, grain.pixels[i]);
        grainMax = std::max<int>(grainMax, grain.pixels[i]);
        maskMin = std::min<int>(maskMin, mask.pixels[i + 3]);
        maskMax = std::max<int>(maskMax, mask.pixels[i + 3]);
    }
    // Normalised to the full 0…255 for the grain and 200…255 for the mask.
    CHECK_EQ(grainMin, 0);
    CHECK(grainMax >= 254);
    CHECK_EQ(maskMin, 200);
    CHECK(maskMax >= 254);
}

TEST(Backdrop_BestCanvasColor)
{
    Bitmap image = Bitmap::Create(4, 3);
    // Top row: two white pixels and one red, so white wins.
    auto put = [&](int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a) {
        std::uint8_t* p = image.pixels.data() + image.Stride() * y + static_cast<std::size_t>(x) * 4;
        p[0] = b; p[1] = g; p[2] = r; p[3] = a;
    };
    put(0, 0, 255, 255, 255, 255);
    put(1, 0, 255, 255, 255, 255);
    put(2, 0, 0, 0, 255, 255);
    auto const best = BestCanvasColor(image, Rect{ 0, 0, 3, 3 });
    CHECK(best.has_value());
    CHECK(Near(*best, Rgb{ 1, 1, 1 }));
    put(0, 2, 0, 0, 0, 249); // bottom-left corner below 250/255 opaque: clear
    CHECK(!BestCanvasColor(image, Rect{ 0, 0, 3, 3 }).has_value());
}

// Undo.State carries backdropConfig, so undo/redo restores the backdrop look.
TEST(Backdrop_UndoSnapshotRestoresConfig)
{
    Document d{ Bitmap::Create(100, 80) };
    Config on = DefaultConfig(42);
    on.isOn = 1;
    on.padding = 32;
    on.ratio = 1.5;
    d.backdrop.config = on;
    UndoSnapshot const snapshot = d.Snapshot(std::nullopt, std::nullopt);
    auto const parsed = UndoSnapshot::Parse(snapshot.Full());
    CHECK(parsed.has_value());

    int changes = 0;
    d.backdrop.Changed.Add([&] { ++changes; });
    d.backdrop.config = DefaultConfig(7); // off, other seed and padding
    d.Restore(*parsed);
    CHECK(d.backdrop.config == on);
    CHECK_EQ(changes, 1);
    // Restoring the same look again changes nothing.
    d.Restore(*parsed);
    CHECK_EQ(changes, 1);
}
