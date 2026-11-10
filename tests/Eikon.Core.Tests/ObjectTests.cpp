#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Document.h"
#include "Core/Objects/ProtoObject.h"
#include "Core/Settings.h"

using namespace Eikon;
using namespace Eikon::Core;

namespace
{
    // The smallest concrete object: base behaviour only.
    class TestObject final : public ProtoObject
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjTest"; }
        JsonValue Dump() const override { return DumpBase(); }
        bool Load(JsonValue const& json) override { return LoadBase(json); }
        std::wstring HitTest(Point point, double) const override { return rect.Contains(point) ? L"move" : L""; }
        std::vector<Handle> Handles() const override { return {}; }
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
    };

    constexpr std::uint32_t kLeft = 0x25;
    constexpr std::uint32_t kUp = 0x26;
    constexpr std::uint32_t kRight = 0x27;
    constexpr std::uint32_t kDown = 0x28;
    constexpr std::uint32_t kEscape = 0x1B;

    TestObject Selected(Rect rect)
    {
        TestObject object;
        object.rect = rect;
        object.state = std::wstring{ ObjectState::Selected };
        return object;
    }
}

TEST(ProtoObject_ArrowsMoveByOnePointOrOnePixel)
{
    KeyContext const retina{ 2.0, false };
    auto object = Selected({ 10, 10, 50, 40 });
    CHECK_EQ(object.KeyDown({ kRight, Modifiers::None }, retina), KeyResult::Handled);
    CHECK((object.rect == Rect{ 12, 10, 50, 40 }));
    object.KeyDown({ kUp, Modifiers::Shift }, retina); // ×10, and up is towards y = 0
    CHECK((object.rect == Rect{ 12, -10, 50, 40 }));
    object.KeyDown({ kLeft, Modifiers::None }, KeyContext{ 2.0, true }); // realPixels: 1 px
    CHECK((object.rect == Rect{ 11, -10, 50, 40 }));
    object.KeyDown({ kDown, Modifiers::None }, KeyContext{ 1.5, false });
    CHECK((object.rect == Rect{ 11, -8.5, 50, 40 }));
}

TEST(ProtoObject_CtrlArrowsResizeFromTheTopLeft)
{
    KeyContext const context{ 1.0, false };
    auto object = Selected({ 10, 10, 50, 40 });
    object.KeyDown({ kRight, Modifiers::Ctrl }, context);
    object.KeyDown({ kDown, Modifiers::Ctrl | Modifiers::Shift }, context);
    CHECK((object.rect == Rect{ 10, 10, 51, 50 }));
    object.KeyDown({ kUp, Modifiers::Ctrl }, context);
    CHECK((object.rect == Rect{ 10, 10, 51, 49 }));
    // Collapsing both dimensions to <= 0 is undone.
    auto tiny = Selected({ 0, 0, 1, 0 });
    tiny.KeyDown({ kLeft, Modifiers::Ctrl }, context);
    CHECK((tiny.rect == Rect{ 0, 0, 1, 0 }));
}

TEST(ProtoObject_KeysNeedSelectionButEscAlwaysDeselects)
{
    TestObject object;
    object.rect = { 0, 0, 10, 10 };
    object.state = std::wstring{ ObjectState::Highlighted };
    CHECK_EQ(object.KeyDown({ kRight, Modifiers::None }, {}), KeyResult::Ignored);
    CHECK((object.rect == Rect{ 0, 0, 10, 10 }));
    CHECK_EQ(object.KeyDown({ kEscape, Modifiers::None }, {}), KeyResult::Deselect);
    CHECK_EQ(Selected({}).KeyDown({ 'A', Modifiers::None }, {}), KeyResult::Ignored);
}

TEST(ProtoObject_DumpShapeAndLoad)
{
    auto object = Selected({ 1.5, 2, -30, 40 });
    object.size = 4;
    object.color = Color{ 0x12, 0xAB, 0xEF, 0xFF };
    std::wstring const json = object.Dump().Serialize();
    CHECK_EQ(json, std::wstring{ L"{\"rect\":{\"x1\":1.5,\"y1\":2,\"width\":-30,\"height\":40},\"size\":4,\"color\":\"#12ABEF\"}" });

    TestObject loaded;
    CHECK(loaded.Load(*JsonValue::Parse(json)));
    CHECK((loaded.rect == object.rect));
    CHECK_EQ(loaded.size, 4);
    CHECK((loaded.color == object.color));
    CHECK(!loaded.Load(*JsonValue::Parse(L"{\"rect\":{\"x1\":0},\"size\":1,\"color\":\"#000000\"}")));
}

TEST(ProtoObject_ApplyDefaultsReadsDefaultColorAndClassSize)
{
    EikonTests::ResetSettings({ { L"defaultColor", L"#00ff00" }, { L"Eikon.ObjTest: size", L"7" } });
    TestObject object;
    object.state = std::wstring{ ObjectState::Selected };
    object.ApplyDefaults(Settings::Current());
    CHECK_EQ(object.state, std::wstring{ ObjectState::Creating });
    CHECK((object.color == Color{ 0, 0xFF, 0, 0xFF }));
    CHECK_EQ(object.size, 7);
    CHECK((object.mouseDownRect == Rect{ 0, 0, 100, 100 }));
}

TEST(Color_HexFormatAndParse)
{
    CHECK_EQ((Color{ 0xFF, 0x0C, 0x01, 0x80 }.ToHex()), std::wstring{ L"#FF0C01" });
    CHECK((Color::FromHex(L" #f6f4e7 ") == std::optional<Color>{ Color{ 0xF6, 0xF4, 0xE7, 0xFF } }));
    CHECK((Color::FromHex(L"404448") == std::optional<Color>{ Color{ 0x40, 0x44, 0x48, 0xFF } }));
    CHECK(!Color::FromHex(L"#12345").has_value());
    CHECK(!Color::FromHex(L"#12345G").has_value());
}

TEST(Document_CropObjectsAndCounter)
{
    Document document{ Bitmap::Create(40, 30, 2.0) };
    CHECK((document.Source() == Rect{ 0, 0, 40, 30 }));
    CHECK((document.Crop() == document.Source()));
    CHECK(!document.IsCropped());
    int changes = 0;
    document.Changed.Add([&] { ++changes; });
    document.SetCrop({ 30, 20, -10, -10 });
    CHECK((document.Crop() == Rect{ 20, 10, 10, 10 }));
    CHECK(document.IsCropped());

    auto& first = document.Add(std::make_unique<TestObject>());
    auto& second = document.Add(std::make_unique<TestObject>());
    CHECK_EQ(first.id, std::int64_t{ 1 });
    CHECK_EQ(second.id, std::int64_t{ 2 });
    CHECK(document.Find(2) == &second);
    CHECK(document.Remove(1) != nullptr);
    CHECK(document.Find(1) == nullptr);
    CHECK_EQ(document.Objects().size(), std::size_t{ 1 });
    CHECK_EQ(document.NextCounter(), 1);
    CHECK_EQ(document.NextCounter(), 2);
    CHECK_EQ(changes, 4);
    CHECK_EQ(document.Image().pixels.size(), std::size_t{ 40 * 30 * 4 });
}
