#include "pch.h"
#include "Core/Json.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>

namespace Eikon::Core
{
    JsonValue JsonValue::Bool(bool value)
    {
        JsonValue v;
        v.m_kind = Kind::Bool;
        v.m_bool = value;
        return v;
    }

    JsonValue JsonValue::Number(double value)
    {
        JsonValue v;
        v.m_kind = Kind::Number;
        v.m_number = value;
        return v;
    }

    JsonValue JsonValue::String(std::wstring value)
    {
        JsonValue v;
        v.m_kind = Kind::String;
        v.m_string = std::move(value);
        return v;
    }

    JsonValue JsonValue::Array(std::vector<JsonValue> items)
    {
        JsonValue v;
        v.m_kind = Kind::Array;
        v.m_items = std::move(items);
        return v;
    }

    JsonValue JsonValue::Object()
    {
        JsonValue v;
        v.m_kind = Kind::Object;
        return v;
    }

    void JsonValue::Append(JsonValue value)
    {
        if (m_kind == Kind::Null)
        {
            m_kind = Kind::Array;
        }
        if (m_kind == Kind::Array)
        {
            m_items.push_back(std::move(value));
        }
    }

    JsonValue const* JsonValue::Find(std::wstring_view key) const noexcept
    {
        if (m_kind != Kind::Object)
        {
            return nullptr;
        }
        for (auto const& member : m_members)
        {
            if (member.first == key)
            {
                return &member.second;
            }
        }
        return nullptr;
    }

    void JsonValue::Set(std::wstring_view key, JsonValue value)
    {
        if (m_kind == Kind::Null)
        {
            m_kind = Kind::Object;
        }
        if (m_kind != Kind::Object)
        {
            return;
        }
        for (auto& member : m_members)
        {
            if (member.first == key)
            {
                member.second = std::move(value);
                return;
            }
        }
        m_members.emplace_back(std::wstring{ key }, std::move(value));
    }

    bool JsonValue::operator==(JsonValue const& other) const
    {
        if (m_kind != other.m_kind)
        {
            return false;
        }
        switch (m_kind)
        {
        case Kind::Null: return true;
        case Kind::Bool: return m_bool == other.m_bool;
        case Kind::Number: return m_number == other.m_number;
        case Kind::String: return m_string == other.m_string;
        case Kind::Array: return m_items == other.m_items;
        case Kind::Object: return m_members == other.m_members;
        }
        return false;
    }

    namespace
    {
        void AppendEscaped(std::wstring& out, std::wstring const& text)
        {
            static constexpr wchar_t kHex[] = L"0123456789abcdef";
            out += L'"';
            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'"': out += L"\\\""; break;
                case L'\\': out += L"\\\\"; break;
                case L'\n': out += L"\\n"; break;
                case L'\r': out += L"\\r"; break;
                case L'\t': out += L"\\t"; break;
                case L'\b': out += L"\\b"; break;
                case L'\f': out += L"\\f"; break;
                default:
                    if (static_cast<std::uint32_t>(c) < 0x20)
                    {
                        auto const code = static_cast<std::uint32_t>(c);
                        out += L"\\u00";
                        out += kHex[(code >> 4) & 0xF];
                        out += kHex[code & 0xF];
                    }
                    else
                    {
                        out += c;
                    }
                    break;
                }
            }
            out += L'"';
        }

        void AppendNumber(std::wstring& out, double value)
        {
            if (!std::isfinite(value))
            {
                out += L"null";
                return;
            }
            char buffer[64] = {};
            std::to_chars_result result{};
            if (value == std::trunc(value) && std::fabs(value) < 1e15)
            {
                result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, static_cast<std::int64_t>(value));
            }
            else
            {
                result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
            }
            if (result.ec != std::errc{})
            {
                out += L"0";
                return;
            }
            out.append(buffer, result.ptr);
        }

        class Parser
        {
        public:
            explicit Parser(std::wstring_view text) : m_text(text) {}

            std::optional<JsonValue> Document()
            {
                auto value = Value(0);
                if (!value)
                {
                    return std::nullopt;
                }
                SkipWhitespace();
                if (m_pos != m_text.size())
                {
                    return std::nullopt;
                }
                return value;
            }

        private:
            static constexpr int kMaxDepth = 256;

            void SkipWhitespace() noexcept
            {
                while (m_pos < m_text.size() &&
                       (m_text[m_pos] == L' ' || m_text[m_pos] == L'\t' || m_text[m_pos] == L'\n' || m_text[m_pos] == L'\r'))
                {
                    ++m_pos;
                }
            }

            bool Consume(wchar_t c) noexcept
            {
                if (m_pos < m_text.size() && m_text[m_pos] == c)
                {
                    ++m_pos;
                    return true;
                }
                return false;
            }

            bool ConsumeWord(std::wstring_view word) noexcept
            {
                if (m_text.substr(m_pos, word.size()) == word)
                {
                    m_pos += word.size();
                    return true;
                }
                return false;
            }

            std::optional<JsonValue> Value(int depth)
            {
                if (depth > kMaxDepth)
                {
                    return std::nullopt;
                }
                SkipWhitespace();
                if (m_pos >= m_text.size())
                {
                    return std::nullopt;
                }
                wchar_t const c = m_text[m_pos];
                if (c == L'{')
                {
                    return ObjectValue(depth);
                }
                if (c == L'[')
                {
                    return ArrayValue(depth);
                }
                if (c == L'"')
                {
                    auto text = StringValue();
                    if (!text)
                    {
                        return std::nullopt;
                    }
                    return JsonValue::String(std::move(*text));
                }
                if (ConsumeWord(L"true"))
                {
                    return JsonValue::Bool(true);
                }
                if (ConsumeWord(L"false"))
                {
                    return JsonValue::Bool(false);
                }
                if (ConsumeWord(L"null"))
                {
                    return JsonValue{};
                }
                return NumberValue();
            }

            std::optional<JsonValue> ObjectValue(int depth)
            {
                ++m_pos; // {
                JsonValue object = JsonValue::Object();
                SkipWhitespace();
                if (Consume(L'}'))
                {
                    return object;
                }
                while (true)
                {
                    SkipWhitespace();
                    auto key = StringValue();
                    if (!key)
                    {
                        return std::nullopt;
                    }
                    SkipWhitespace();
                    if (!Consume(L':'))
                    {
                        return std::nullopt;
                    }
                    auto value = Value(depth + 1);
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    object.Set(*key, std::move(*value));
                    SkipWhitespace();
                    if (Consume(L','))
                    {
                        continue;
                    }
                    if (Consume(L'}'))
                    {
                        return object;
                    }
                    return std::nullopt;
                }
            }

            std::optional<JsonValue> ArrayValue(int depth)
            {
                ++m_pos; // [
                JsonValue array = JsonValue::Array();
                SkipWhitespace();
                if (Consume(L']'))
                {
                    return array;
                }
                while (true)
                {
                    auto value = Value(depth + 1);
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    array.Append(std::move(*value));
                    SkipWhitespace();
                    if (Consume(L','))
                    {
                        continue;
                    }
                    if (Consume(L']'))
                    {
                        return array;
                    }
                    return std::nullopt;
                }
            }

            static int HexValue(wchar_t c) noexcept
            {
                if (c >= L'0' && c <= L'9') return c - L'0';
                if (c >= L'a' && c <= L'f') return c - L'a' + 10;
                if (c >= L'A' && c <= L'F') return c - L'A' + 10;
                return -1;
            }

            std::optional<std::wstring> StringValue()
            {
                if (!Consume(L'"'))
                {
                    return std::nullopt;
                }
                std::wstring value;
                while (m_pos < m_text.size())
                {
                    wchar_t const c = m_text[m_pos++];
                    if (c == L'"')
                    {
                        return value;
                    }
                    if (static_cast<std::uint32_t>(c) < 0x20)
                    {
                        return std::nullopt;
                    }
                    if (c != L'\\')
                    {
                        value += c;
                        continue;
                    }
                    if (m_pos >= m_text.size())
                    {
                        return std::nullopt;
                    }
                    wchar_t const escape = m_text[m_pos++];
                    switch (escape)
                    {
                    case L'"': value += L'"'; break;
                    case L'\\': value += L'\\'; break;
                    case L'/': value += L'/'; break;
                    case L'b': value += L'\b'; break;
                    case L'f': value += L'\f'; break;
                    case L'n': value += L'\n'; break;
                    case L'r': value += L'\r'; break;
                    case L't': value += L'\t'; break;
                    case L'u':
                    {
                        if (m_pos + 4 > m_text.size())
                        {
                            return std::nullopt;
                        }
                        std::uint32_t code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            int const digit = HexValue(m_text[m_pos++]);
                            if (digit < 0)
                            {
                                return std::nullopt;
                            }
                            code = (code << 4) | static_cast<std::uint32_t>(digit);
                        }
                        value += static_cast<wchar_t>(code);
                        break;
                    }
                    default:
                        return std::nullopt;
                    }
                }
                return std::nullopt; // unterminated
            }

            std::optional<JsonValue> NumberValue()
            {
                std::size_t const start = m_pos;
                auto digits = [this] {
                    std::size_t const from = m_pos;
                    while (m_pos < m_text.size() && m_text[m_pos] >= L'0' && m_text[m_pos] <= L'9')
                    {
                        ++m_pos;
                    }
                    return m_pos - from;
                };
                Consume(L'-');
                if (m_pos < m_text.size() && m_text[m_pos] == L'0')
                {
                    ++m_pos;
                }
                else if (digits() == 0)
                {
                    return std::nullopt;
                }
                if (Consume(L'.') && digits() == 0)
                {
                    return std::nullopt;
                }
                if (Consume(L'e') || Consume(L'E'))
                {
                    if (!Consume(L'+'))
                    {
                        Consume(L'-');
                    }
                    if (digits() == 0)
                    {
                        return std::nullopt;
                    }
                }
                std::string narrow;
                for (std::size_t i = start; i < m_pos; ++i)
                {
                    narrow.push_back(static_cast<char>(m_text[i]));
                }
                double value = 0;
                auto const result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
                if (result.ec != std::errc{} || result.ptr != narrow.data() + narrow.size())
                {
                    return std::nullopt;
                }
                return JsonValue::Number(value);
            }

            std::wstring_view m_text;
            std::size_t m_pos = 0;
        };
    }

    void JsonValue::SerializeTo(std::wstring& out) const
    {
        switch (m_kind)
        {
        case Kind::Null:
            out += L"null";
            break;
        case Kind::Bool:
            out += m_bool ? L"true" : L"false";
            break;
        case Kind::Number:
            AppendNumber(out, m_number);
            break;
        case Kind::String:
            AppendEscaped(out, m_string);
            break;
        case Kind::Array:
        {
            out += L'[';
            bool first = true;
            for (auto const& item : m_items)
            {
                if (!first)
                {
                    out += L',';
                }
                first = false;
                item.SerializeTo(out);
            }
            out += L']';
            break;
        }
        case Kind::Object:
        {
            out += L'{';
            bool first = true;
            for (auto const& [key, value] : m_members)
            {
                if (!first)
                {
                    out += L',';
                }
                first = false;
                AppendEscaped(out, key);
                out += L':';
                value.SerializeTo(out);
            }
            out += L'}';
            break;
        }
        }
    }

    std::wstring JsonValue::Serialize() const
    {
        std::wstring out;
        SerializeTo(out);
        return out;
    }

    std::optional<JsonValue> JsonValue::Parse(std::wstring_view text)
    {
        return Parser{ text }.Document();
    }
}
