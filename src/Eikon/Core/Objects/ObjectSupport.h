#pragma once

// Small helpers shared by the Obj* classes: typed Dump/Load members and plane geometry.

#include "Core/Geometry.h"
#include "Core/Json.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Eikon::Core::Support
{
    inline constexpr double kPi = 3.141592653589793;

    // Dump members. Bools are JSON booleans and integers JSON numbers.
    inline void SetNumber(JsonValue& json, std::wstring_view key, double value) { json.Set(key, JsonValue::Number(value)); }
    inline void SetBool(JsonValue& json, std::wstring_view key, bool value) { json.Set(key, JsonValue::Bool(value)); }
    inline void SetString(JsonValue& json, std::wstring_view key, std::wstring value) { json.Set(key, JsonValue::String(std::move(value))); }

    // Load members: nullopt when missing or of another kind (the Load then fails, as Decodable does).
    inline std::optional<double> GetNumber(JsonValue const& json, std::wstring_view key)
    {
        auto const* v = json.Find(key);
        if (!v || !v->IsNumber()) return std::nullopt;
        return v->AsNumber();
    }
    inline std::optional<bool> GetBool(JsonValue const& json, std::wstring_view key)
    {
        auto const* v = json.Find(key);
        if (!v || !v->IsBool()) return std::nullopt;
        return v->AsBool();
    }
    inline std::optional<std::wstring> GetString(JsonValue const& json, std::wstring_view key)
    {
        auto const* v = json.Find(key);
        if (!v || !v->IsString()) return std::nullopt;
        return v->AsString();
    }
    inline JsonValue RectJson(Rect const& r)
    {
        JsonValue j = JsonValue::Object();
        SetNumber(j, L"x1", r.x);
        SetNumber(j, L"y1", r.y);
        SetNumber(j, L"width", r.width);
        SetNumber(j, L"height", r.height);
        return j;
    }
    inline std::optional<Rect> GetRect(JsonValue const& json, std::wstring_view key)
    {
        auto const* v = json.Find(key);
        if (!v || !v->IsObject()) return std::nullopt;
        auto x = GetNumber(*v, L"x1"), y = GetNumber(*v, L"y1"), w = GetNumber(*v, L"width"), h = GetNumber(*v, L"height");
        if (!x || !y || !w || !h) return std::nullopt;
        return Rect{ *x, *y, *w, *h };
    }

    inline double Distance(Point a, Point b) noexcept { return std::hypot(a.x - b.x, a.y - b.y); }
    // Wraps to (−π, π].
    inline double WrapAngle(double a) noexcept
    {
        while (a > kPi) a -= 2 * kPi;
        while (a <= -kPi) a += 2 * kPi;
        return a;
    }
    inline double DistanceToSegment(Point p, Point a, Point b) noexcept
    {
        double const dx = b.x - a.x, dy = b.y - a.y;
        double const l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
        t = (std::clamp)(t, 0.0, 1.0);
        return Distance(p, { a.x + t * dx, a.y + t * dy });
    }
    // Rotation by `angle` (radians, clockwise on screen with y down) about `centre`.
    inline Point Rotate(Point p, Point centre, double angle) noexcept
    {
        double const c = std::cos(angle), s = std::sin(angle);
        double const x = p.x - centre.x, y = p.y - centre.y;
        return { centre.x + x * c - y * s, centre.y + x * s + y * c };
    }
    inline bool ContainsHandle(std::wstring_view state) noexcept
    {
        return state.find(L"resize") != std::wstring_view::npos || state.rfind(L"point-", 0) == 0;
    }
}
