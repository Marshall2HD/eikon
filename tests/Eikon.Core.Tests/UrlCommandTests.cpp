#include "pch.h"
#include "TestFramework.h"

#include "Core/UrlCommand.h"

using namespace Eikon::Core;

namespace
{
    UrlCommand Parse(std::wstring_view url)
    {
        auto parsed = ParseUrlCommand(url);
        if (!parsed)
        {
            EikonTests::ReportFailure(__FILE__, __LINE__, "did not parse: " + EikonTests::Narrow(url));
            return {};
        }
        return *parsed;
    }

    std::optional<std::wstring> Value(std::wstring_view url) { return Parse(url).value; }
    std::wstring Command(std::wstring_view url) { return Parse(url).command; }
}

TEST(UrlCommand_SchemeMustBeEikon)
{
    CHECK(ParseUrlCommand(L"eikon://show").has_value());
    CHECK(ParseUrlCommand(L"EiKoN://show").has_value());
    CHECK_EQ(Parse(L"EIKON://show").scheme, std::wstring{ L"eikon" });
    CHECK(!ParseUrlCommand(L"http://show").has_value());
    CHECK(!ParseUrlCommand(L"eikonx://show").has_value());
    CHECK(!ParseUrlCommand(L"show").has_value());
    CHECK(!ParseUrlCommand(L"").has_value());
    CHECK(!ParseUrlCommand(L"://show").has_value());
}

TEST(UrlCommand_CommandIsCutAtQuestionMarkThenEquals)
{
    CHECK_EQ(Command(L"eikon://grab/fullscreen"), std::wstring{ L"grab/fullscreen" });
    CHECK_EQ(Command(L"eikon://grab/delayed=5"), std::wstring{ L"grab/delayed" });
    CHECK_EQ(Command(L"eikon://grab/fullscreen?then=copy"), std::wstring{ L"grab/fullscreen" });
    CHECK_EQ(Command(L"eikon://grab/delayed=5?then=copy"), std::wstring{ L"grab/delayed" });
    // '?' is cut first, so an '=' after it never reaches the command.
    CHECK_EQ(Command(L"eikon://settings/library/limit?x=1"), std::wstring{ L"settings/library/limit" });
    CHECK_EQ(Command(L"eikon://a=b?c"), std::wstring{ L"a" });
}

TEST(UrlCommand_CommandIsTrimmedAndLowercasedButNotDecoded)
{
    CHECK_EQ(Command(L"eikon://Grab/FullScreen"), std::wstring{ L"grab/fullscreen" });
    CHECK_EQ(Command(L"eikon://  show \t"), std::wstring{ L"show" });
    CHECK_EQ(Command(L"eikon://grab%2Ffullscreen"), std::wstring{ L"grab%2ffullscreen" });
    // Windows adds a "/" for an empty path; trailing slashes are dropped, inner ones kept.
    CHECK_EQ(Command(L"eikon://show/"), std::wstring{ L"show" });
    CHECK_EQ(Command(L"eikon://settings/?x=1"), std::wstring{ L"settings" });
    CHECK_EQ(Command(L"eikon://grab/area//"), std::wstring{ L"grab/area" });
    CHECK_EQ(Command(L"eikon:///?then=copy"), std::wstring{});
    CHECK_EQ(Command(L"eikon://"), std::wstring{});
    CHECK_EQ(Command(L"eikon://?then=copy"), std::wstring{});
    // Without "://" nothing is dropped: the whole (lower-cased) string is compared.
    CHECK_EQ(Command(L"eikon:show"), std::wstring{ L"eikon:show" });
}

TEST(UrlCommand_ValueIsTextAfterTheFirstEqualsOfTheWholeUrl)
{
    CHECK(!Value(L"eikon://grab/fullscreen").has_value());
    CHECK_EQ(Value(L"eikon://grab/delayed=5"), (std::optional<std::wstring>{ L"5" }));
    CHECK_EQ(Value(L"eikon://settings/token=AbC%20d"), (std::optional<std::wstring>{ L"AbC d" }));
    // The first '=' may be in the query, and the value runs to the end of the URL.
    CHECK_EQ(Value(L"eikon://grab/fullscreen?then=copy,save"), (std::optional<std::wstring>{ L"copy,save" }));
    CHECK_EQ(Value(L"eikon://grab/delayed=5?then=copy"), (std::optional<std::wstring>{ L"5?then=copy" }));
    CHECK_EQ(Value(L"eikon://lib/search="), (std::optional<std::wstring>{ L"" }));
    CHECK_EQ(Value(L"eikon://lib/search=a=b"), (std::optional<std::wstring>{ L"a=b" }));
    // '+' is not a space; UTF-8 escapes decode.
    CHECK_EQ(Value(L"eikon://lib/search=a+b"), (std::optional<std::wstring>{ L"a+b" }));
    CHECK_EQ(Value(L"eikon://lib/search=%C3%A9t%C3%A9"), (std::optional<std::wstring>{ L"\u00E9t\u00E9" }));
}

TEST(UrlCommand_InvalidPercentEncodingGivesNoValue)
{
    CHECK(!Value(L"eikon://lib/search=%zz").has_value());
    CHECK(!Value(L"eikon://lib/search=100%").has_value());
    CHECK(!Value(L"eikon://lib/search=%4").has_value());
    CHECK(!Value(L"eikon://lib/search=%C3").has_value()); // truncated UTF-8
    CHECK(!Value(L"eikon://lib/search=%FF").has_value());
}

TEST(UrlCommand_QueryItemsAreADictionary)
{
    auto const command = Parse(L"eikon://lib/search?search=a%2Bb+c&x=1&x=2&flag&empty=");
    CHECK_EQ(command.query.size(), std::size_t{ 3 });
    CHECK_EQ(command.query.at(L"search"), std::wstring{ L"a+b+c" });
    CHECK_EQ(command.query.at(L"x"), std::wstring{ L"2" });         // last one wins
    CHECK_EQ(command.query.at(L"empty"), std::wstring{});
    CHECK(command.query.find(std::wstring_view{ L"flag" }) == command.query.end()); // no '=' -> no value

    // An item without a value removes an earlier one (dict[name] = nil).
    auto const removed = Parse(L"eikon://x?then=copy&then");
    CHECK(removed.query.empty());
    // The fragment is not part of the query.
    auto const fragment = Parse(L"eikon://x?then=copy#frag");
    CHECK_EQ(fragment.query.at(L"then"), std::wstring{ L"copy" });
    CHECK(Parse(L"eikon://grab/delayed=5").query.empty());
}

TEST(UrlCommand_Host)
{
    CHECK_EQ(Parse(L"eikon://flop/connect").host, std::wstring{ L"flop" });
    CHECK_EQ(Parse(L"eikon://show").host, std::wstring{ L"show" });
    CHECK_EQ(Parse(L"eikon://user@host:8080/x").host, std::wstring{ L"host" });
}

TEST(UrlCommand_ThenAbsentUsesSettings)
{
    auto const then = ParseThenParameter(Parse(L"eikon://grab/fullscreen"));
    CHECK(then.actions.useSettings);
    CHECK(!then.unrecognized);
    CHECK(ParseThenParameter(Parse(L"eikon://grab/fullscreen?other=1")).actions.useSettings);
}

TEST(UrlCommand_ThenFlags)
{
    auto const copy = ParseThenParameter(Parse(L"eikon://grab/fullscreen?then=copy")).actions;
    CHECK(!copy.useSettings);
    CHECK(copy.copy);
    CHECK(!copy.save && !copy.edit && !copy.pin && !copy.thumbnail);

    auto const all = ParseThenParameter(Parse(L"eikon://grab/area?then=Copy,SAVE,edit,pin,thumbnail")).actions;
    CHECK(all.copy && all.save && all.edit && all.pin && all.thumbnail);

    // Items are not trimmed: " save" is not "save".
    auto const spaced = ParseThenParameter(Parse(L"eikon://grab/area?then=copy,%20save"));
    CHECK(spaced.actions.copy);
    CHECK(!spaced.actions.save);
    CHECK(!spaced.unrecognized);

    // Unknown items next to known ones are ignored without a warning.
    auto const mixed = ParseThenParameter(Parse(L"eikon://grab/area?then=bogus,edit"));
    CHECK(mixed.actions.edit);
    CHECK(!mixed.unrecognized);
}

TEST(UrlCommand_ThenUnrecognizedWarnsAndDoesNothing)
{
    for (auto url : { L"eikon://grab/fullscreen?then=bogus", L"eikon://grab/fullscreen?then=", L"eikon://grab/fullscreen?then=,," })
    {
        auto const then = ParseThenParameter(Parse(url));
        CHECK(then.unrecognized);
        CHECK(!then.actions.useSettings);
        CHECK((then.actions == CaptureActions{ false, false, false, false, false, false }));
    }
    // The name is case-sensitive.
    CHECK(ParseThenParameter(Parse(L"eikon://grab/fullscreen?THEN=copy")).actions.useSettings);
}

TEST(UrlCommand_DelayedSeconds)
{
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed")), 3.0);
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed=5")), 5.0);
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed=1.5")), 1.5);
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed=abc")), 3.0);
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed=-2")), 0.0);
    // The value runs to the end of the URL, so a then= after it spoils the number.
    CHECK_EQ(DelayedCaptureSeconds(Parse(L"eikon://grab/delayed=5?then=copy")), 3.0);
}
