// Minimal self-registering test framework: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace EikonTests
{
    using TestFunction = void (*)();

    struct TestCase
    {
        char const* name;
        TestFunction function;
    };

    std::vector<TestCase>& Registry();
    void ReportFailure(char const* file, int line, std::string const& message);

    struct Registrar
    {
        Registrar(char const* name, TestFunction function) { Registry().push_back({ name, function }); }
    };

    std::string Narrow(std::wstring_view text);

    template <typename T>
    struct IsOptional : std::false_type {};
    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type {};

    template <typename T>
    struct IsVector : std::false_type {};
    template <typename T, typename A>
    struct IsVector<std::vector<T, A>> : std::true_type {};

    template <typename T>
    std::string ToString(T const& value)
    {
        using D = std::remove_cvref_t<T>;
        if constexpr (std::is_array_v<D>)
        {
            using E = std::remove_cv_t<std::remove_extent_t<D>>;
            if constexpr (std::is_same_v<E, wchar_t>)
            {
                return "L\"" + Narrow(std::wstring_view{ value }) + "\"";
            }
            else if constexpr (std::is_same_v<E, char>)
            {
                return "\"" + std::string{ value } + "\"";
            }
            else
            {
                return "<array>";
            }
        }
        else if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>)
        {
            return "\"" + std::string{ value } + "\"";
        }
        else if constexpr (std::is_same_v<D, std::wstring> || std::is_same_v<D, std::wstring_view>)
        {
            return "L\"" + Narrow(value) + "\"";
        }
        else if constexpr (std::is_same_v<D, wchar_t const*> || std::is_same_v<D, wchar_t*>)
        {
            return value ? "L\"" + Narrow(value) + "\"" : std::string{ "nullptr" };
        }
        else if constexpr (std::is_same_v<D, bool>)
        {
            return value ? "true" : "false";
        }
        else if constexpr (std::is_enum_v<D>)
        {
            return std::to_string(static_cast<long long>(value));
        }
        else if constexpr (std::is_same_v<D, wchar_t>)
        {
            return "L'" + Narrow(std::wstring_view{ &value, 1 }) + "'";
        }
        else if constexpr (std::is_arithmetic_v<D>)
        {
            std::ostringstream stream;
            stream.precision(17);
            stream << +value;
            return stream.str();
        }
        else if constexpr (IsOptional<D>::value)
        {
            return value ? ToString(*value) : std::string{ "nullopt" };
        }
        else if constexpr (IsVector<D>::value)
        {
            std::string result = "{";
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                result += (i ? ", " : "") + ToString(value[i]);
            }
            return result + "}";
        }
        else if constexpr (requires { value.start; value.end; })
        {
            return "[" + std::to_string(value.start) + ", " + std::to_string(value.end) + ")";
        }
        else if constexpr (requires { value.ToString(); })
        {
            return "L\"" + Narrow(value.ToString()) + "\"";
        }
        else
        {
            return "<value>";
        }
    }
}

#define EIKON_TEST_CONCAT2(a, b) a##b
#define EIKON_TEST_CONCAT(a, b) EIKON_TEST_CONCAT2(a, b)

#define TEST(name)                                                                                   \
    static void name();                                                                              \
    static ::EikonTests::Registrar EIKON_TEST_CONCAT(name, _registrar)(#name, &name);                \
    static void name()

#define CHECK(expression)                                                                            \
    do                                                                                               \
    {                                                                                                \
        if (!(expression))                                                                           \
        {                                                                                            \
            ::EikonTests::ReportFailure(__FILE__, __LINE__, "CHECK(" #expression ")");               \
        }                                                                                            \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                   \
    do                                                                                               \
    {                                                                                                \
        auto const& eikon_actual_ = (actual);                                                        \
        auto const& eikon_expected_ = (expected);                                                    \
        if (!(eikon_actual_ == eikon_expected_))                                                     \
        {                                                                                            \
            ::EikonTests::ReportFailure(__FILE__, __LINE__,                                          \
                "CHECK_EQ(" #actual ", " #expected "): " + ::EikonTests::ToString(eikon_actual_) +   \
                " != " + ::EikonTests::ToString(eikon_expected_));                                   \
        }                                                                                            \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                      \
    do                                                                                               \
    {                                                                                                \
        double const eikon_actual_ = static_cast<double>(actual);                                    \
        double const eikon_expected_ = static_cast<double>(expected);                                \
        if (!(std::fabs(eikon_actual_ - eikon_expected_) <= (tolerance)))                            \
        {                                                                                            \
            ::EikonTests::ReportFailure(__FILE__, __LINE__,                                          \
                "CHECK_NEAR(" #actual ", " #expected "): " + ::EikonTests::ToString(eikon_actual_) + \
                " != " + ::EikonTests::ToString(eikon_expected_));                                   \
        }                                                                                            \
    } while (0)
