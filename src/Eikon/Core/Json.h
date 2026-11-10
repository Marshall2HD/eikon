#pragma once

// Minimal JSON value, parser and writer for Core (no WinRT here, so Windows.Data.Json is not an
// option). Used for the objects' Dump/Load, undo snapshots and the Secrets file. Objects keep
// member order.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Eikon::Core
{
    class JsonValue
    {
    public:
        enum class Kind { Null, Bool, Number, String, Array, Object };

        JsonValue() = default;
        static JsonValue Bool(bool value);
        static JsonValue Number(double value);
        static JsonValue String(std::wstring value);
        static JsonValue Array(std::vector<JsonValue> items = {});
        static JsonValue Object();

        Kind GetKind() const noexcept { return m_kind; }
        bool IsNull() const noexcept { return m_kind == Kind::Null; }
        bool IsBool() const noexcept { return m_kind == Kind::Bool; }
        bool IsNumber() const noexcept { return m_kind == Kind::Number; }
        bool IsString() const noexcept { return m_kind == Kind::String; }
        bool IsArray() const noexcept { return m_kind == Kind::Array; }
        bool IsObject() const noexcept { return m_kind == Kind::Object; }

        // Typed reads; the fallback when the kind differs.
        bool AsBool(bool fallback = false) const noexcept { return m_kind == Kind::Bool ? m_bool : fallback; }
        double AsNumber(double fallback = 0) const noexcept { return m_kind == Kind::Number ? m_number : fallback; }
        std::wstring AsString(std::wstring_view fallback = {}) const { return m_kind == Kind::String ? m_string : std::wstring{ fallback }; }

        // Arrays.
        std::vector<JsonValue> const& Items() const noexcept { return m_items; }
        void Append(JsonValue value);

        // Objects. Find returns nullptr when the member is missing (or this is not an object).
        using Member = std::pair<std::wstring, JsonValue>;
        std::vector<Member> const& Members() const noexcept { return m_members; }
        JsonValue const* Find(std::wstring_view key) const noexcept;
        // Replaces an existing member or appends a new one. Turns a Null value into an object.
        void Set(std::wstring_view key, JsonValue value);

        // Compact JSON text. Numbers use the shortest round-trip form; integral values print without
        // a fraction ("12", not "12.0"). Non-finite numbers are written as null.
        std::wstring Serialize() const;
        // Strict RFC 8259 parsing of a complete document (surrounding whitespace allowed).
        static std::optional<JsonValue> Parse(std::wstring_view text);

        bool operator==(JsonValue const& other) const;

    private:
        void SerializeTo(std::wstring& out) const;

        Kind m_kind = Kind::Null;
        bool m_bool = false;
        double m_number = 0;
        std::wstring m_string;
        std::vector<JsonValue> m_items;
        std::vector<Member> m_members;
    };
}
