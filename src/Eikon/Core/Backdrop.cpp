#include "pch.h"
#include "Core/Backdrop.h"

#include "Core/Noise.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <map>
#include <random>

namespace Eikon::Core::Backdrop
{
    namespace
    {
        std::wstring_view Trim(std::wstring_view s) noexcept
        {
            auto const isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r'; };
            while (!s.empty() && isSpace(s.front()))
            {
                s.remove_prefix(1);
            }
            while (!s.empty() && isSpace(s.back()))
            {
                s.remove_suffix(1);
            }
            return s;
        }

        // Split on a separator; empty pieces are dropped.
        std::vector<std::wstring_view> Split(std::wstring_view s, wchar_t separator)
        {
            std::vector<std::wstring_view> parts;
            std::size_t start = 0;
            while (start <= s.size())
            {
                std::size_t const comma = s.find(separator, start);
                std::size_t const end = comma == std::wstring_view::npos ? s.size() : comma;
                if (end > start)
                {
                    parts.push_back(s.substr(start, end - start));
                }
                if (comma == std::wstring_view::npos)
                {
                    break;
                }
                start = comma + 1;
            }
            return parts;
        }

        // Strict number parse: the whole string must be a number (no surrounding spaces).
        std::optional<double> ParseStrictDouble(std::wstring_view text)
        {
            std::string narrow;
            for (wchar_t c : text)
            {
                if (static_cast<std::uint32_t>(c) > 0x7F)
                {
                    return std::nullopt;
                }
                narrow.push_back(static_cast<char>(c));
            }
            char const* first = narrow.data();
            char const* last = first + narrow.size();
            if (first != last && *first == '+')
            {
                ++first;
            }
            if (first == last)
            {
                return std::nullopt;
            }
            double value = 0;
            auto const [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last)
            {
                return std::nullopt;
            }
            return value;
        }

        // A JSON number as an integer: only integral values convert.
        std::optional<std::int64_t> AsInt(JsonValue const* value)
        {
            if (!value || !value->IsNumber())
            {
                return std::nullopt;
            }
            double const n = value->AsNumber();
            if (std::floor(n) != n || std::abs(n) > 9.0e18)
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(n);
        }

        std::uint8_t ToByte(double unit) noexcept
        {
            return static_cast<std::uint8_t>(std::clamp(std::lround(unit * 255.0), 0L, 255L));
        }

        JsonValue Dict(std::initializer_list<std::pair<wchar_t const*, JsonValue>> members)
        {
            JsonValue dict = JsonValue::Object();
            for (auto const& [key, value] : members)
            {
                dict.Set(key, value);
            }
            return dict;
        }

        JsonValue Str(wchar_t const* s) { return JsonValue::String(s); }
        JsonValue Num(double n) { return JsonValue::Number(n); }

        double Gauss(double x, double sigma) noexcept { return std::exp(-(x * x) / (2 * sigma * sigma)); }

        // Normalises a noise map (float maths):
        // (v - min) / (max - min) * scale + offset, truncated to an unsigned integer.
        std::vector<std::uint32_t> Normalise(std::vector<float> const& values, float scale, float offset)
        {
            float lo = 1e9f;
            float hi = -1e9f;
            for (float v : values)
            {
                if (v < lo)
                {
                    lo = v;
                }
                if (hi <= v)
                {
                    hi = v;
                }
            }
            if (hi <= lo)
            {
                hi = lo + 1.0f;
            }
            float const range = hi - lo;
            std::vector<std::uint32_t> out(values.size());
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                float const f = (values[i] - lo) / range * scale + offset;
                out[i] = f <= 0.0f ? 0u : static_cast<std::uint32_t>(f);
            }
            return out;
        }

        std::uint32_t RampAt(std::array<std::uint32_t, 256> const& ramp, std::uint32_t index) noexcept
        {
            return ramp[std::min<std::uint32_t>(index, 255)];
        }

        // RGBA ramp entry -> premultiplied BGRA (alpha is always 255 or the whole entry is 0).
        void PutRgba(Bitmap& bitmap, int x, int y, std::uint32_t rgba) noexcept
        {
            std::uint8_t* p = bitmap.pixels.data() + bitmap.Stride() * y + static_cast<std::size_t>(x) * 4;
            p[0] = static_cast<std::uint8_t>(rgba >> 16);
            p[1] = static_cast<std::uint8_t>(rgba >> 8);
            p[2] = static_cast<std::uint8_t>(rgba);
            p[3] = static_cast<std::uint8_t>(rgba >> 24);
        }

        std::vector<float> MeshField(int size, double frequency, std::int64_t seed)
        {
            Noise::Perlin const noise{ frequency, 1, 0.01, 1.0, static_cast<std::int32_t>(seed) };
            return Noise::Map(noise, size, size, 0, 0, size, size, true);
        }
    }

    // ---- Config ----

    std::int64_t RandomSeed()
    {
        static std::mt19937_64 engine{ std::random_device{}() };
        return std::uniform_int_distribution<std::int64_t>(0, 31'999'999)(engine);
    }

    Config DefaultConfig(std::int64_t seed)
    {
        Config config;
        config.seed = seed;
        return config;
    }

    JsonValue ToJson(Config const& c)
    {
        JsonValue json = JsonValue::Object();
        json.Set(L"name", JsonValue::String(c.name));
        json.Set(L"mode", JsonValue::String(c.mode));
        json.Set(L"colors", JsonValue::String(c.colors));
        json.Set(L"stops", JsonValue::String(c.stops));
        json.Set(L"gradStart", JsonValue::String(c.gradStart));
        json.Set(L"gradEnd", JsonValue::String(c.gradEnd));
        json.Set(L"gradType", JsonValue::String(c.gradType));
        if (c.meshType)
        {
            json.Set(L"meshType", JsonValue::String(*c.meshType));
        }
        json.Set(L"noise", Num(c.noise));
        json.Set(L"padding", Num(c.padding));
        json.Set(L"inset", Num(c.inset));
        json.Set(L"innerRadius", Num(c.innerRadius));
        json.Set(L"outerRadius", Num(c.outerRadius));
        json.Set(L"ratio", Num(c.ratio));
        json.Set(L"minWidth", Num(c.minWidth));
        json.Set(L"seed", Num(static_cast<double>(c.seed)));
        json.Set(L"shadowIntensity", Num(c.shadowIntensity));
        json.Set(L"isOn", Num(static_cast<double>(c.isOn)));
        json.Set(L"filepath", JsonValue::String(c.filepath));
        json.Set(L"blur", Num(c.blur));
        return json;
    }

    std::wstring Encode(Config const& config)
    {
        return ToJson(config).Serialize();
    }

    void Apply(Config& c, JsonValue const& dict)
    {
        bool const wasOn = c.isOn == 1;
        auto const str = [&](wchar_t const* key, std::wstring& field) {
            if (JsonValue const* v = dict.Find(key); v && v->IsString())
            {
                field = v->AsString();
            }
        };
        auto const num = [&](wchar_t const* key, double& field) {
            if (JsonValue const* v = dict.Find(key); v && v->IsNumber())
            {
                field = v->AsNumber();
            }
        };
        auto const integer = [&](wchar_t const* key, std::int64_t& field) {
            if (auto const v = AsInt(dict.Find(key)))
            {
                field = *v;
            }
        };
        str(L"name", c.name);
        str(L"mode", c.mode);
        str(L"colors", c.colors);
        str(L"stops", c.stops);
        str(L"gradStart", c.gradStart);
        str(L"gradEnd", c.gradEnd);
        str(L"gradType", c.gradType);
        if (JsonValue const* v = dict.Find(L"meshType"); v && v->IsString())
        {
            c.meshType = v->AsString();
        }
        else
        {
            c.meshType.reset();
        }
        num(L"noise", c.noise);
        num(L"padding", c.padding);
        num(L"inset", c.inset);
        num(L"innerRadius", c.innerRadius);
        num(L"outerRadius", c.outerRadius);
        num(L"ratio", c.ratio);
        num(L"minWidth", c.minWidth);
        integer(L"seed", c.seed);
        num(L"shadowIntensity", c.shadowIntensity);
        integer(L"isOn", c.isOn);
        str(L"filepath", c.filepath);
        num(L"blur", c.blur);
        if (!wasOn)
        {
            c.inset = -1;
            c.ratio = 0;
            c.minWidth = 0;
        }
    }

    // ---- Templates ----

    std::vector<JsonValue> const& GradientTemplates()
    {
        static std::vector<JsonValue> const templates = [] {
            auto mesh = [](wchar_t const* name, wchar_t const* colors, wchar_t const* stops, double noise = 0.04) {
                return Dict({ { L"name", Str(name) }, { L"colors", Str(colors) }, { L"stops", Str(stops) }, { L"noise", Num(noise) } });
            };
            auto grad = [](wchar_t const* name, wchar_t const* colors, wchar_t const* stops) {
                return Dict({ { L"name", Str(name) }, { L"colors", Str(colors) }, { L"stops", Str(stops) },
                              { L"meshType", Str(L"grad") }, { L"noise", Num(0.04) } });
            };
            return std::vector<JsonValue>{
                mesh(L"electric-blue", L"9AE6FF,56C3FF,2B91E2,144CA0,0D2162,091022", L"0.12,0.37,0.64,0.87"),
                mesh(L"deep-ocean", L"#001738,#0E3A69,#004C7E,#005985,#116D96,#3F9ABA,#7CC0D6,#A3D1E7", L"0.24,0.40,0.47,0.58,0.77,0.93"),
                grad(L"blue-orange", L"#0078A6,#004A70,#042952,#2C1E48,#4C1A48,#BA313E,#FF8D00", L"0.17,0.33,0.41,0.50,0.77"),
                mesh(L"yellow-purple-bright", L"#FFE995,#FF8C7C,#FF6B77,#E92D94,#C2008D,#98008A,#840488,#000177", L"0.20,0.27,0.41,0.56,0.67,0.71"),
                grad(L"yellow-purple-pale", L"#6568B5,#AB84C2,#D998B6,#F2ACA2,#F7B39C,#F7B494,#F7AD90,#F29C85,#E08379,#AD506D,#24134B",
                     L"0.15,0.25,0.35,0.41,0.45,0.49,0.55,0.62,0.76"),
                grad(L"pale-morning", L"#A5BCD8,#C0CCD9,#D9DBD8,#E6E1D3,#EFE4CA,#F0E1C5,#E9D7BB,#D9C9B0,#C2B49F,#A0A19A,#7B8894,#576C80,#364759,#0A1621",
                     L"0.11,0.24,0.31,0.38,0.44,0.50,0.56,0.63,0.69,0.75,0.81,0.88"),
                mesh(L"electric-purple", L"#000000,#23054E,#320870,#4B1F8C,#6425A1,#8036B3,#9D46C2,#BA57CF,#DA7FE6,#F8A8F8,#FCC8FC,#FFE8FF",
                     L"0.14,0.21,0.31,0.36,0.43,0.50,0.56,0.68,0.82,0.92"),
                mesh(L"quiet-blue", L"#26608C,#3981AF,#5B9DC5,#7CB8DA,#B6E0FC", L"0.26,0.48,0.72"),
                mesh(L"fun-blue", L"#015D8E,#1A87B2,#379DC3,#53B4D4,#81DCFA", L"0.29,0.47,0.65"),
                mesh(L"peach", L"#F996B2,#FFBEB8,#FFE3D1", L"0.57"),
                mesh(L"orange", L"FEB142,#F06A20,#B61B14", L"0.46"),
                mesh(L"green-sky", L"#304B54,#486D78,#618794,#9DBCC6,#D3E7F1", L"0.24,0.39,0.66"),
                mesh(L"gunpowder", L"#1F2A43,#758695", L"0.00,1.00"),
                mesh(L"n4", L"#7F3273,#B0457B,#D35A7D,#F49595,#F9BDA9", L"0.00,0.27,0.48,0.80,1.00"),
                mesh(L"oldbook", L"#C3E3E4,#E5F1EC,#EEF0E5,#F7EFDE", L"0.00,0.50,0.70,1.00"),
                mesh(L"lead-clouds", L"#F8FCFE,#E0E7EE", L"0.00,1.00", 0.12),
                mesh(L"custom-saved-gradient", L"#E2FEFE,#EEF3FA", L"0.00,1.00"),
            };
        }();
        return templates;
    }

    std::vector<JsonValue> const& SolidTemplates()
    {
        static std::vector<JsonValue> const templates = [] {
            auto solid = [](wchar_t const* name, wchar_t const* color, double noise = 0.06) {
                return Dict({ { L"name", Str(name) }, { L"mode", Str(L"flat") }, { L"colors", Str(color) }, { L"noise", Num(noise) } });
            };
            return std::vector<JsonValue>{
                solid(L"s-steel", L"#EFF0F1", 0.32),
                solid(L"s-antracite", L"#909498"),
                solid(L"s-charcoal", L"#545658", 0.12),
                solid(L"s-navy", L"#064F77"),
                solid(L"s-aegean-blue", L"#49596E"),
                solid(L"s-teal", L"#5E8B9B"),
                solid(L"s-kombu-green", L"#6F898B"),
                solid(L"s-aqua", L"#ADDFE7"),
                solid(L"s-yellow", L"#FEDD7C"),
                solid(L"s-beige", L"#E9E7E0"),
                solid(L"s-celadon-green", L"#D8DEC4"),
                solid(L"s-azure-green", L"#DCE7E7"),
                solid(L"custom-saved-color", L"#DCE7E7"),
            };
        }();
        return templates;
    }

    std::wstring TemplateName(JsonValue const& dict)
    {
        JsonValue const* name = dict.Find(L"name");
        return name ? name->AsString() : std::wstring{};
    }

    int TemplateTag(JsonValue const& dict, int index)
    {
        return TemplateName(dict).find(L"custom") != std::wstring::npos ? kCustomTag : index;
    }

    // ---- Colours ----

    std::optional<Rgb> ParseHex(std::wstring_view text)
    {
        std::wstring hex;
        for (wchar_t c : Trim(text))
        {
            if (c != L'#')
            {
                hex.push_back(static_cast<wchar_t>(std::towupper(c)));
            }
        }
        if (hex.size() != 6)
        {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (wchar_t c : hex)
        {
            int digit;
            if (c >= L'0' && c <= L'9')
            {
                digit = c - L'0';
            }
            else if (c >= L'A' && c <= L'F')
            {
                digit = c - L'A' + 10;
            }
            else
            {
                return std::nullopt;
            }
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        return Rgb{ ((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0 };
    }

    std::wstring ToHex(Rgb color)
    {
        wchar_t buffer[8];
        swprintf_s(buffer, L"#%02X%02X%02X", ToByte(color.r), ToByte(color.g), ToByte(color.b));
        return buffer;
    }

    std::vector<Rgb> ParseColors(std::wstring_view colors)
    {
        std::vector<Rgb> out;
        for (auto part : Split(colors, L','))
        {
            out.push_back(ParseHex(part).value_or(Rgb{}));
        }
        if (out.empty())
        {
            out.push_back(*ParseHex(L"#202020"));
        }
        return out;
    }

    std::vector<double> ParseStops(std::wstring_view stops)
    {
        std::vector<double> out;
        for (auto part : Split(stops, L','))
        {
            out.push_back(ParseStrictDouble(part).value_or(0.0));
        }
        return out;
    }

    Hsb ToHsb(Rgb c)
    {
        double const mx = std::max({ c.r, c.g, c.b });
        double const mn = std::min({ c.r, c.g, c.b });
        double const delta = mx - mn;
        Hsb out;
        out.b = mx;
        out.s = mx > 0 ? delta / mx : 0;
        if (delta > 0)
        {
            double h;
            if (mx == c.r)
            {
                h = (c.g - c.b) / delta;
            }
            else if (mx == c.g)
            {
                h = 2 + (c.b - c.r) / delta;
            }
            else
            {
                h = 4 + (c.r - c.g) / delta;
            }
            h /= 6;
            if (h < 0)
            {
                h += 1;
            }
            out.h = h;
        }
        return out;
    }

    Rgb FromHsb(Hsb c)
    {
        double const h = (c.h - std::floor(c.h)) * 6;
        int const sector = static_cast<int>(h) % 6;
        double const f = h - std::floor(h);
        double const p = c.b * (1 - c.s);
        double const q = c.b * (1 - c.s * f);
        double const t = c.b * (1 - c.s * (1 - f));
        switch (sector)
        {
        case 0: return { c.b, t, p };
        case 1: return { q, c.b, p };
        case 2: return { p, c.b, t };
        case 3: return { p, q, c.b };
        case 4: return { t, p, c.b };
        default: return { c.b, p, q };
        }
    }

    double Lightness(Rgb c)
    {
        auto const linear = [](double v) { return v >= 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92; };
        double const r = linear(c.r);
        double const g = linear(c.g);
        double const b = linear(c.b);
        float const l = std::cbrt(static_cast<float>(r * 0.4122214708 + g * 0.5363325363 + b * 0.0514459929));
        float const m = std::cbrt(static_cast<float>(r * 0.2119034982 + g * 0.6806995451 + b * 0.1073969566));
        float const s = std::cbrt(static_cast<float>(r * 0.0883024619 + g * 0.2817188376 + b * 0.6299787005));
        return static_cast<double>(l) * 0.2104542553 + static_cast<double>(m) * 0.793617785 + static_cast<double>(s) * -0.0040720468;
    }

    std::array<std::uint32_t, 256> Ramp(std::vector<Rgb> const& colors, std::vector<double> stops)
    {
        std::array<std::uint32_t, 256> ramp{};
        std::size_t const n = colors.size();
        if (stops.size() < n && (stops.empty() || stops.front() != 0.0))
        {
            stops.insert(stops.begin(), 0.0);
        }
        if (stops.size() < n && (stops.empty() || stops.back() != 1.0))
        {
            stops.push_back(1.0);
        }
        if (stops.size() != n || n == 0)
        {
            return ramp; // a mismatch returns the zeroed ramp
        }
        // One segment advance per entry at most loop.
        std::size_t segment = 0;
        Rgb c0 = colors[0];
        Rgb c1 = colors[0];
        double s0 = 0.0;
        double s1 = 1.0;
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            double const v = i / 255.0;
            if (segment + 1 < stops.size() && !(v < stops[segment]))
            {
                c0 = c1;
                c1 = colors[segment + 1];
                s0 = stops[segment];
                s1 = stops[segment + 1];
                ++segment;
            }
            double t = (v - s0) / (s1 - s0);
            if (!std::isfinite(t))
            {
                t = 0; // a zero-width segment
            }
            auto const channel = [t](double a, double b) {
                double const value = (a + t * (b - a)) * 255.0;
                return static_cast<std::uint32_t>(std::clamp(value, 0.0, 4294967295.0)) & 0xFF;
            };
            ramp[i] = channel(c0.r, c1.r) | channel(c0.g, c1.g) << 8 | channel(c0.b, c1.b) << 16 | 0xFF000000u;
        }
        return ramp;
    }

    Rgb BaseColor(std::vector<Rgb> const& colors, std::vector<double> const& stops)
    {
        if (colors.size() == 1)
        {
            return colors[0];
        }
        if (colors.empty())
        {
            return { 0.5, 0.5, 0.5 };
        }
        std::uint32_t const entry = Ramp(colors, stops)[128];
        return { (entry & 0xFF) / 255.0, ((entry >> 8) & 0xFF) / 255.0, ((entry >> 16) & 0xFF) / 255.0 };
    }

    Rgb DarkestColor(std::vector<Rgb> const& colors)
    {
        Rgb darkest{};
        double brightness = 1.0;
        for (Rgb const& c : colors)
        {
            double const b = ToHsb(c).b;
            if (b < brightness)
            {
                brightness = b;
                darkest = c;
            }
        }
        return darkest;
    }

    // ---- Gradients ----

    Point ParsePoint(std::wstring_view text)
    {
        auto const parts = Split(text, L',');
        Point p;
        p.x = parts.empty() ? 0.0 : ParseStrictDouble(parts[0]).value_or(0.0);
        p.y = parts.size() > 1 ? ParseStrictDouble(parts[1]).value_or(0.0) : 0.0;
        return p;
    }

    Gradient ConfigGradient(Config const& config)
    {
        Gradient gradient;
        gradient.radial = config.gradType == L"radial";
        gradient.start = ParsePoint(config.gradStart);
        gradient.end = ParsePoint(config.gradEnd);
        auto const colors = ParseColors(config.colors);
        std::vector<double> locations = ParseStops(config.stops);
        if (locations.empty() || locations.front() != 0.0)
        {
            locations.insert(locations.begin(), 0.0);
        }
        if (locations.back() != 1.0)
        {
            locations.push_back(1.0);
        }
        // With mismatched counts the colours are spread evenly.
        bool const even = locations.size() != colors.size();
        for (std::size_t i = 0; i < colors.size(); ++i)
        {
            double const location = even ? (colors.size() > 1 ? static_cast<double>(i) / (colors.size() - 1) : 0.0) : locations[i];
            gradient.stops.push_back({ location, colors[i] });
        }
        return gradient;
    }

    Gradient FlatGradient(Rgb base)
    {
        Gradient gradient;
        gradient.radial = true;
        gradient.start = { 0.5, 0.5 };
        gradient.end = { -0.19, -0.19 };
        double const l = Lightness(base);
        double const d = l > 0.99 ? 0.0 : 0.2 * std::pow(2.718281828459045, l * -1.9);
        Hsb const hsb = ToHsb(base);
        double const edgeBrightness = std::max(0.0, hsb.b - d);
        double const edgeSaturation = hsb.s == 0 ? 0.0 : std::min(1.0, hsb.s + d);
        for (int i = 0; i < 10; ++i)
        {
            double const location = i / 9.0;
            double const k = (std::pow(15.0, location) - 1.0) / 14.0;
            Hsb c{ hsb.h, hsb.s + (edgeSaturation - hsb.s) * k, hsb.b + (edgeBrightness - hsb.b) * k };
            gradient.stops.push_back({ location, FromHsb(c) });
        }
        return gradient;
    }

    void ApplyGradientType(Config& config, GradientType type)
    {
        switch (type)
        {
        case GradientType::Mesh:
            config.mode = L"mesh";
            break;
        case GradientType::Vertical:
            config.mode = L"gradient";
            config.gradType = L"linear";
            config.gradStart = L"0.5,1.0";
            config.gradEnd = L"0.5,0.0";
            break;
        case GradientType::Diagonal:
            config.mode = L"gradient";
            config.gradType = L"linear";
            config.gradStart = L"0.0,1.0";
            config.gradEnd = L"1.0,0.0";
            break;
        case GradientType::Radial:
            config.mode = L"gradient";
            config.gradType = L"radial";
            config.gradStart = L"0.5,1.1";
            config.gradEnd = L"-0.45,-0.15";
            break;
        }
    }

    GradientType SelectedGradientType(Config const& config)
    {
        if (config.mode != L"gradient")
        {
            return GradientType::Mesh;
        }
        if (config.gradStart == L"0.5,1.0")
        {
            return GradientType::Vertical;
        }
        if (config.gradStart == L"0.0,1.0")
        {
            return GradientType::Diagonal;
        }
        if (config.gradStart == L"0.5,1.1")
        {
            return GradientType::Radial;
        }
        return GradientType::Mesh;
    }

    void Reverse(Config& config)
    {
        auto colors = Split(config.colors, L',');
        std::reverse(colors.begin(), colors.end());
        std::wstring joined;
        for (std::size_t i = 0; i < colors.size(); ++i)
        {
            joined += (i ? L"," : L"");
            joined += colors[i];
        }
        std::wstring stops;
        auto parts = Split(config.stops, L',');
        std::reverse(parts.begin(), parts.end());
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            auto const parsed = ParseStrictDouble(parts[i]);
            double const value = parsed ? 1.0 - *parsed : 1.0;
            // The shortest round-trip digits, "1.0" for whole numbers.
            char narrow[32];
            auto const result = std::to_chars(narrow, narrow + sizeof(narrow) - 1, value);
            *result.ptr = 0;
            std::string text = narrow;
            if (text.find_first_of(".en") == std::string::npos)
            {
                text += ".0";
            }
            stops += (i ? L"," : L"");
            stops += std::wstring(text.begin(), text.end());
        }
        config.colors = joined;
        config.stops = stops;
    }

    // ---- Sliders ----

    double PaddingFromSlider(double v) { return std::floor(v * v / 2); }
    double SliderFromPadding(double padding) { return padding > 0 ? std::sqrt(2 * padding) : 0; }
    double ShadowFromSlider(double v) { return v / 20; }
    double SliderFromShadow(double intensity) { return intensity * 20; }
    double InsetFromSlider(double v) { return std::trunc(v * v / 1.8); }
    double SliderFromInset(double inset) { return inset > 0 ? std::sqrt(inset * 1.8) : 0; }
    bool InsetSliderApplies(double currentInset, double v) { return currentInset > 0 || v != 0; }
    double RadiusFromSlider(double v) { return 2 * v; }
    double SliderFromRadius(double radius) { return radius * 0.5; }
    double BlurFromSlider(double v) { return v > 0 ? v + 2 : 0; }
    double SliderFromBlur(double blur) { return std::max(blur - 2, 0.0); }
    bool InnerRadiusEnabled(Config const& config) { return config.padding > 0 || config.ratio > 0; }

    // ---- Aspect ----

    Aspect AspectForItem(std::wstring_view title)
    {
        std::wstring value;
        for (wchar_t c : title)
        {
            value.push_back(static_cast<wchar_t>(std::towlower(c)));
        }
        if (value.find(L"reset") != std::wstring::npos)
        {
            value = L"0";
        }
        if (value == L"twitter")
        {
            return { 1.777777778, 1200 };
        }
        if (value == L"youtube")
        {
            return { 1.777777778, 1280 };
        }
        if (value == L"pinterest")
        {
            return { 0.666666667, 1000 };
        }
        if (value == L"facebook")
        {
            return { 1.904761905, 1200 };
        }
        if (value == L"linkedin")
        {
            return { 1.913875598, 1200 };
        }
        auto const parts = Split(value, L':');
        if (parts.size() == 2)
        {
            auto const a = ParseStrictDouble(parts[0]);
            auto const b = ParseStrictDouble(parts[1]);
            if (a && b && *b != 0)
            {
                double const ratio = *a / *b;
                if (ratio >= 0.3 && ratio <= 3)
                {
                    return { ratio, 0 };
                }
            }
        }
        return {};
    }

    std::wstring AspectButtonTitle(std::wstring_view title)
    {
        std::wstring lower;
        for (wchar_t c : title)
        {
            lower.push_back(static_cast<wchar_t>(std::towlower(c)));
        }
        if (lower.find(L"reset") != std::wstring::npos)
        {
            return L"\u2022\u2022\u2022";
        }
        if (lower == L"twitter")
        {
            return L"Tw";
        }
        if (lower == L"facebook")
        {
            return L"Fb";
        }
        if (lower == L"linkedin")
        {
            return L"Ln";
        }
        if (lower == L"pinterest")
        {
            return L"Pi";
        }
        if (lower == L"youtube")
        {
            return L"Yt";
        }
        return std::wstring{ title };
    }

    // ---- Layout ----

    void ExpandCanvas(Pads& pads, Config const& config, double dpr, Rect const& source, Rect const& objects, bool expandable)
    {
        if (!expandable)
        {
            pads.ct = pads.cb = pads.cl = pads.cr = 0;
            return;
        }
        Rect const s = source.Standardized();
        Rect const o = objects.Standardized();
        double margin = config.inset * dpr;
        bool const inside = s.Left() <= o.Left() && o.Right() <= s.Right() && s.Top() <= o.Top() && o.Bottom() <= s.Bottom();
        if (!inside && margin < 0)
        {
            margin = 20 * dpr;
        }
        margin = std::max(0.0, margin);
        double const limitH = kCanvasLimitH * dpr;
        double const limitV = kCanvasLimitV * dpr;
        pads.cr = std::min(o.Right() + margin - s.Right(), limitH);
        pads.cl = std::min(margin + s.Left() - o.Left(), limitH);
        pads.cb = std::min(o.Bottom() + margin - s.Bottom(), limitV);
        pads.ct = std::min(margin + s.Top() - o.Top(), limitV);
    }

    void ApplyPadding(Pads& pads, Config const& config, double dpr, double srcW, double srcH)
    {
        double p = config.padding * dpr;
        if (config.ratio > 0 && p == 0)
        {
            p = 20 * dpr;
        }
        pads.bt = pads.bb = pads.bl = pads.br = p;
        double const w = srcW + pads.cl + pads.cr + pads.bl + pads.br;
        double const h = srcH + pads.ct + pads.cb + pads.bt + pads.bb;
        if (w < config.minWidth)
        {
            pads.bl = pads.br = std::floor((config.minWidth - srcW - pads.cl - pads.cr) / 2);
        }
        if (config.ratio > 0)
        {
            double const width = srcW + pads.cl + pads.cr + pads.bl + pads.br;
            if (width / h >= config.ratio)
            {
                pads.bt = pads.bb = std::floor((width / config.ratio - srcH - pads.ct - pads.cb) / 2);
            }
            else
            {
                pads.bl = pads.br = std::floor((config.ratio * h - srcW - pads.cl - pads.cr) / 2);
            }
        }
    }

    double CanvasWidth(Pads const& p, double srcW) noexcept { return srcW + p.bl + p.br + p.cl + p.cr; }
    double CanvasHeight(Pads const& p, double srcH) noexcept { return srcH + p.bt + p.bb + p.ct + p.cb; }

    double CanvasPixelsForLimits(Pads const& p, double srcW, double srcH) noexcept
    {
        return (srcW + p.cl + p.cr) * (srcH + p.cr + p.cb);
    }

    double Megapixels(double pixels) noexcept { return std::floor(pixels / 100000) / 10; }

    double OuterRadiusPx(Config const& config, double dpr, double canvasW, double canvasH)
    {
        return std::min(config.outerRadius * dpr, std::min(canvasW, canvasH) / 2);
    }

    double InnerRadiusPx(Config const& config, double dpr, Pads const& p, double srcW, double srcH, double outerPx)
    {
        double const m = std::min({ p.bt, p.bb, p.bl, p.br });
        if (m == 0)
        {
            return outerPx;
        }
        double const contentW = srcW + p.cl + p.cr;
        double const contentH = srcH + p.ct + p.cb;
        double r = std::min(config.innerRadius * dpr, std::min(contentW, contentH) / 2);
        r = std::max(r, outerPx - m);
        if (outerPx > 0 && m < outerPx)
        {
            r = std::min(r, outerPx - std::pow(m, 0.9));
        }
        if (outerPx > 0 && outerPx <= m)
        {
            r = std::min(0.5 * outerPx, r);
        }
        return r;
    }

    bool BackdropLayerVisible(Config const& config)
    {
        return config.isOn == 1 && !(config.padding == 0 && config.ratio == 0 && config.minWidth == 0);
    }

    Layout ComputeLayout(Config const& config, double dpr, Rect const& crop, Rect const& objects, bool expandable)
    {
        Layout layout;
        Rect const c = crop.Standardized();
        layout.srcW = c.width;
        layout.srcH = c.height;
        layout.on = config.isOn == 1;
        if (layout.on)
        {
            ExpandCanvas(layout.pads, config, dpr, c, objects, expandable);
            ApplyPadding(layout.pads, config, dpr, c.width, c.height);
            layout.layerVisible = BackdropLayerVisible(config);
        }
        Pads const& p = layout.pads;
        layout.canvasW = CanvasWidth(p, c.width);
        layout.canvasH = CanvasHeight(p, c.height);
        if (layout.on)
        {
            layout.outerPx = OuterRadiusPx(config, dpr, layout.canvasW, layout.canvasH);
            layout.innerPx = InnerRadiusPx(config, dpr, p, c.width, c.height, layout.outerPx);
        }
        layout.content = { p.bl, p.bt, c.width + p.cl + p.cr, c.height + p.ct + p.cb };
        layout.screenshot = { p.bl + p.cl, p.bt + p.ct, c.width, c.height };
        return layout;
    }

    // ---- Shadow ----

    Rgb ShadowColor(Rgb darkest)
    {
        Hsb const hsb = ToHsb(darkest);
        double const s = hsb.s == 0 ? 0.0 : 1 - 0.5 * std::pow(1 - hsb.s, 4.0);
        return FromHsb({ hsb.h, s, hsb.b * 0.25 });
    }

    double ShadowTargetOpacity(double lightness) { return ((1 - lightness) * 0.8 + 0.2) * 0.36; }

    std::vector<ShadowLayer> ShadowLayers(Config const& config, Pads const& p, double srcW, double srcH,
                                          double innerPx, double targetOpacity, bool canvasClear)
    {
        double intensity = std::clamp(config.shadowIntensity, 0.0, 1.0);
        double const pixels = CanvasPixelsForLimits(p, srcW, srcH);
        if (canvasClear || Megapixels(pixels) > 20)
        {
            intensity = 0;
        }
        std::vector<ShadowLayer> layers;
        if (intensity <= 0)
        {
            return layers;
        }
        double const size = std::min({ p.bb * 0.8, 2 * srcH, 80.0 });
        double const distance = 2 * intensity * size;
        double const baseBlur = 5 * intensity + 2;
        double const contentW = srcW + p.cl + p.cr;
        double const contentH = srcH + p.cb + p.ct;
        if (Megapixels(pixels) < 10)
        {
            double const q = 4 / (intensity + 0.001);
            for (int k = 1; k <= 5; ++k)
            {
                double const t = (std::pow(q, k / 5.0) - 1) / (q - 1);
                double const insetX = std::floor(distance * (0.6 - 2.4 * (intensity - 0.5) * (intensity - 0.5)) * t * t);
                ShadowLayer layer;
                layer.frame = { p.bl + 1 + insetX, p.bt + 1, std::max(0.0, contentW - 2 - 2 * insetX), std::max(0.0, contentH - 2) };
                layer.cornerRadius = std::min({ innerPx, layer.frame.width / 2, layer.frame.height / 2 });
                layer.offsetY = 0.7 * distance * t;
                layer.radius = baseBlur + std::floor(0.5 * distance * t);
                layer.opacity = targetOpacity * 1.5 * (intensity * std::sqrt(1 - t) + 0.5);
                layers.push_back(layer);
            }
        }
        else
        {
            ShadowLayer layer;
            layer.frame = { p.bl + 1, p.bt + 1, std::max(0.0, contentW - 2), std::max(0.0, contentH - 2) };
            layer.cornerRadius = innerPx;
            layer.offsetY = intensity * distance / 3 - 3; // (0, 3 - I·D/3) in y-up terms
            layer.radius = baseBlur + std::floor((0.5 * intensity + 0.5) * distance * 0.5);
            layer.opacity = 0.8 * intensity + 0.2;
            layers.push_back(layer);
        }
        return layers;
    }

    // ---- Bitmaps ----

    Bitmap MeshTemplate(int size, std::vector<Rgb> const& colors, std::vector<double> const& stops, std::int64_t seed, bool grad)
    {
        Bitmap out = Bitmap::Create(size, size);
        auto const ramp = Ramp(colors, stops);
        auto const grey = Ramp({ Rgb{ 0, 0, 0 }, Rgb{ 1, 1, 1 } }, { 0.0, 1.0 });
        if (grad)
        {
            // One field picks a row offset into a vertical ramp ("shiftGradient").
            auto const field = Normalise(MeshField(size, 1.0 / (size / 1.3), seed + 1000), 255.0f, 0.0f);
            long long const maxShift = static_cast<long long>(size) * 2 / 3;
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    std::uint32_t const tpl = RampAt(grey, field[static_cast<std::size_t>(y) * size + x]) & 0xFF;
                    long long const shift = static_cast<long long>(static_cast<double>(maxShift) / 255.0 * tpl) - maxShift / 2;
                    long long index = (y + shift) * 255 / size;
                    index = std::clamp<long long>(index, 0, 255);
                    PutRgba(out, x, y, ramp[static_cast<std::size_t>(index)]);
                }
            }
            return out;
        }
        // Field A through the colour ramp, then shifted sideways by field B
        // ("shiftPixels").
        auto const a = Normalise(MeshField(size, 1.0 / static_cast<double>(size / 2), seed), 255.0f, 0.0f);
        auto const b = Normalise(MeshField(size, 1.0 / (size / 2.0), seed + 1000), 255.0f, 0.0f);
        long long const maxShift = size / 3;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                std::uint32_t const tpl = RampAt(grey, b[static_cast<std::size_t>(y) * size + x]) & 0xFF;
                long long const shift = static_cast<long long>(static_cast<double>(maxShift) / 255.0 * tpl) - maxShift / 2;
                long long const sx = std::clamp<long long>(x + shift, 0, size - 1);
                PutRgba(out, x, y, RampAt(ramp, a[static_cast<std::size_t>(y) * size + static_cast<std::size_t>(sx)]));
            }
        }
        return out;
    }

    Bitmap BlurAndCrop(Bitmap const& image, double radius)
    {
        int const w = image.width;
        int const h = image.height;
        int const inset = static_cast<int>(std::lround(2 * radius));
        int const outW = std::max(0, w - 2 * inset);
        int const outH = std::max(0, h - 2 * inset);
        Bitmap out = Bitmap::Create(outW, outH, image.dpr);
        if (outW == 0 || outH == 0)
        {
            return out;
        }
        int const reach = static_cast<int>(std::ceil(radius * 3));
        std::vector<double> kernel(static_cast<std::size_t>(2 * reach + 1));
        double sum = 0;
        for (int i = -reach; i <= reach; ++i)
        {
            kernel[static_cast<std::size_t>(i + reach)] = radius > 0 ? Gauss(i, radius) : (i == 0 ? 1.0 : 0.0);
            sum += kernel[static_cast<std::size_t>(i + reach)];
        }
        for (double& k : kernel)
        {
            k /= sum;
        }
        // Horizontal pass over the whole image (outside is transparent), then vertical into the crop.
        std::vector<double> rows(static_cast<std::size_t>(w) * h * 4, 0.0);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                double acc[4] = {};
                for (int i = -reach; i <= reach; ++i)
                {
                    int const sx = x + i;
                    if (sx < 0 || sx >= w)
                    {
                        continue;
                    }
                    std::uint8_t const* p = image.pixels.data() + image.Stride() * y + static_cast<std::size_t>(sx) * 4;
                    double const k = kernel[static_cast<std::size_t>(i + reach)];
                    for (int c = 0; c < 4; ++c)
                    {
                        acc[c] += p[c] * k;
                    }
                }
                std::copy(acc, acc + 4, rows.begin() + (static_cast<std::ptrdiff_t>(y) * w + x) * 4);
            }
        }
        for (int y = 0; y < outH; ++y)
        {
            for (int x = 0; x < outW; ++x)
            {
                int const cx = x + inset;
                int const cy = y + inset;
                double acc[4] = {};
                for (int i = -reach; i <= reach; ++i)
                {
                    int const sy = cy + i;
                    if (sy < 0 || sy >= h)
                    {
                        continue;
                    }
                    double const k = kernel[static_cast<std::size_t>(i + reach)];
                    for (int c = 0; c < 4; ++c)
                    {
                        acc[c] += rows[(static_cast<std::size_t>(sy) * w + cx) * 4 + c] * k;
                    }
                }
                std::uint8_t* q = out.pixels.data() + out.Stride() * y + static_cast<std::size_t>(x) * 4;
                for (int c = 0; c < 4; ++c)
                {
                    q[c] = static_cast<std::uint8_t>(std::clamp(std::lround(acc[c]), 0L, 255L));
                }
            }
        }
        return out;
    }

    Bitmap Mesh(Config const& config)
    {
        constexpr int kSize = 80;
        bool const grad = config.meshType && *config.meshType == L"grad";
        Bitmap const tpl = MeshTemplate(kSize, ParseColors(config.colors), ParseStops(config.stops), config.seed, grad);
        return BlurAndCrop(tpl, kSize * 0.0375);
    }

    Bitmap Grain(std::int64_t seed)
    {
        Noise::Perlin const noise{ 1.4285714285714286, 2, 0.2, 0.175, static_cast<std::int32_t>(seed) };
        auto const values = Normalise(Noise::Map(noise, 256, 256, 0, 0, 256, 256, true), 255.0f, 0.0f);
        Bitmap out = Bitmap::Create(256, 256);
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            auto const v = static_cast<std::uint8_t>(std::min<std::uint32_t>(values[i], 255));
            out.pixels[i * 4 + 0] = v;
            out.pixels[i * 4 + 1] = v;
            out.pixels[i * 4 + 2] = v;
            out.pixels[i * 4 + 3] = 255;
        }
        return out;
    }

    Bitmap ShadowMask(std::int64_t seed)
    {
        Noise::Perlin const noise{ 1.25, 1, 0.1, 1.0, static_cast<std::int32_t>(seed) };
        auto const values = Normalise(Noise::Map(noise, 256, 256, 0, 0, 256, 256, true), 55.0f, 200.0f);
        Bitmap out = Bitmap::Create(256, 256);
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            auto const v = static_cast<std::uint8_t>(std::min<std::uint32_t>(values[i], 255));
            out.pixels[i * 4 + 0] = v;
            out.pixels[i * 4 + 1] = v;
            out.pixels[i * 4 + 2] = v;
            out.pixels[i * 4 + 3] = v;
        }
        return out;
    }

    Rgb AverageColor(Bitmap const& image)
    {
        if (image.IsEmpty())
        {
            return {};
        }
        double sum[3] = {};
        std::size_t const count = static_cast<std::size_t>(image.width) * image.height;
        for (std::size_t i = 0; i < count; ++i)
        {
            sum[0] += image.pixels[i * 4 + 2];
            sum[1] += image.pixels[i * 4 + 1];
            sum[2] += image.pixels[i * 4 + 0];
        }
        // The average is read back as bytes, so the channels are whole bytes.
        return { std::round(sum[0] / count) / 255.0, std::round(sum[1] / count) / 255.0, std::round(sum[2] / count) / 255.0 };
    }

    std::optional<Rgb> BestCanvasColor(Bitmap const& image, Rect const& crop)
    {
        if (image.IsEmpty())
        {
            return Rgb{ 1, 1, 1 };
        }
        Rect const c = crop.Standardized();
        int const left = std::clamp(static_cast<int>(c.Left()), 0, image.width - 1);
        int const top = std::clamp(static_cast<int>(c.Top()), 0, image.height - 1);
        int const right = std::clamp(static_cast<int>(c.Right()), left + 1, image.width);
        int const bottom = std::clamp(static_cast<int>(c.Bottom()) - 1, top, image.height - 1);
        std::uint8_t const* corner = image.pixels.data() + image.Stride() * bottom + static_cast<std::size_t>(left) * 4;
        if (corner[3] < 0xFA)
        {
            return std::nullopt;
        }
        std::map<std::uint32_t, int> counts;
        std::uint32_t best = 0;
        int bestCount = 0;
        for (int x = left; x < right; ++x)
        {
            std::uint8_t const* p = image.pixels.data() + image.Stride() * top + static_cast<std::size_t>(x) * 4;
            std::uint32_t const key = p[2] | p[1] << 8 | p[0] << 16;
            int const n = ++counts[key];
            if (n > bestCount)
            {
                bestCount = n;
                best = key;
            }
        }
        return Rgb{ (best & 0xFF) / 255.0, ((best >> 8) & 0xFF) / 255.0, ((best >> 16) & 0xFF) / 255.0 };
    }
}
