#pragma once

// Base of every annotation object. Subclasses are
// the Obj* classes, one file each under Core/Objects/. Core holds geometry,
// state, Dump/Load, hit testing, handles and keys; drawing is Editor::Render::Draw(session, object,
// context), so Core stays free of Win2D.
//
// Coordinates: image pixels, y down. `rect` is Rect: origin + size, where width/height
// may be negative while creating; for lines and arrows it is start point + (dx, dy). (object
// rects are in backing pixels too: the arrow width divides the length by dpr, the conceal bitmap is
// rect-sized.)

#include "Core/Color.h"
#include "Core/Geometry.h"
#include "Core/Json.h"
#include "Core/KeyShortcut.h"
#include "Core/Objects/Path.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core
{
    class Settings;
    struct Bitmap;
    class ProtoObject;

    // Values of ProtoObject::state other than a handle name.
    namespace ObjectState
    {
        inline constexpr std::wstring_view Creating = L"creating";
        inline constexpr std::wstring_view Move = L"move";
        inline constexpr std::wstring_view Selected = L"selected";
        inline constexpr std::wstring_view Highlighted = L"highlighted";
        inline constexpr std::wstring_view Unselected = L"unselected";
    }

    // Which palette the colour picker shows for the object (`preferredPalette`).
    enum class Palette : std::int32_t
    {
        Drawing,
        Highlighter,
    };

    // A knob: `name` is also the state while it is dragged
    // ("point-1", "tl-resize", ...). `cursor` names the cursor-* asset shown over it.
    struct Handle
    {
        std::wstring name;
        Point position;          // image pixels
        std::wstring cursor;
        bool shown = true;
    };

    // A key press routed to the selected object. `key` is a virtual-key code; Modifiers::Ctrl stands
    // for Command.
    struct KeyEvent
    {
        std::uint32_t key = 0;
        Modifiers modifiers = Modifiers::None;
    };

    struct KeyContext
    {
        double dpr = 1.0;        // the image's backing scale
        bool realPixels = false; // Settings::RealPixels
    };

    enum class KeyResult
    {
        Ignored,   // not handled; the editor may use the key
        Handled,
        Deselect,  // Esc: hand control back to the editor
    };

    // What objects read from the Framer and ObjectHandlers singletons while they are created
    // and edited: the backing scale, the editor zoom, the image bounds (keep-inside), the source image
    // (conceal, magnifier, highlighter, text background) and the counter number. The editor keeps it
    // current (Environment() is a process-wide singleton, as the Framer is). UI thread only.
    struct ObjectEnvironment
    {
        double dpr = 1.0;
        double zoom = 1.0;
        Rect image;                                   // Framer source (0, 0, W, H) in pixels
        Bitmap const* bitmap = nullptr;               // the screenshot, BGRA premultiplied
        std::function<std::int32_t()> nextCounter;    // ObjectHandlers.counterCount += 1 (Document::NextCounter)
        // ObjImage's G key / "saveGIF" action: the two-frame GIF export (output area).
        std::function<void(ProtoObject const&)> saveGif;
        // Text layout for ObjText (DirectWrite in Editor/RenderText.cpp): the size of `text` laid out
        // with the object's font, wrapped at `maxWidth` pixels. Unset in Core tests (a fixed-pitch estimate is used).
        std::function<Size(ProtoObject const& text, double maxWidth)> measureText;
    };
    ObjectEnvironment& Environment();

    class ProtoObject
    {
    public:
        virtual ~ProtoObject() = default;

        // Class name ("ObjArrow", "ObjText", ...): selects the renderer, the Dump "type" and the
        // per-class preference keys.
        virtual std::wstring_view ClassName() const = 0;

        // ---- JSON ----
        // Every Dump starts with rect, size, color (DumpBase), then the subclass's own keys.
        virtual JsonValue Dump() const = 0;
        // Restores what Dump wrote; false when the JSON is not this class's shape.
        virtual bool Load(JsonValue const& json) = 0;

        // ---- Pointer ----
        // What a press at `point` (image pixels) would grab: "" for nothing, "move" for the body, or a
        // handle name. `zoom` (device pixels per image pixel) sizes the hit tolerances.
        virtual std::wstring HitTest(Point point, double zoom) const = 0;
        // The knobs shown while selected, at their geometric anchors.
        virtual std::vector<Handle> Handles() const = 0;
        // The knobs where they are drawn at this zoom (some sit outside the stroke or are pushed off
        // an arrow tip by a zoom-dependent distance). Defaults to Handles().
        virtual std::vector<Handle> HandlesAt(double zoom, double dpr) const;

        // ---- Keyboard ----
        // Subclasses call BaseKeyDown first and then add
        // their own keys.
        virtual KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) = 0;

        // Moves the whole object (arrow keys, drags). Objects with point lists override it.
        virtual void MouseDown(Point point, Modifiers modifiers);
        virtual void MouseDragged(Point point, Modifiers modifiers);
        virtual void MouseUp(Point point, Modifiers modifiers);
        // True when creation ended without a usable shape (a click without a drag): the editor drops it.
        virtual bool IsEmpty() const { return rect.width == 0 && rect.height == 0; }

        virtual void MoveBy(double dx, double dy) { rect = rect.Offset(dx, dy); }

        // ---- Options bar ----
        // String-keyed property access, used by the editor's
        // options bar through ObjectHandlers. The base handles "size"
        // ("0"–"9") and "color" ("#RRGGBB"); subclasses add their own keys ("arrowType", "lineType",
        // "concealType", "rulerPixels", "transparency", "fill", "shape", "correction", "style") and
        // call the base for the rest. Unknown keys read as "" and are ignored when set. Persisted setters
        // store the per-class preference in Settings::Current().
        virtual std::wstring Property(std::wstring_view key) const;
        virtual void SetProperty(std::wstring_view key, std::wstring_view value);

        // ---- Geometry for the renderer ----
        // The vector shapes the object draws, in image pixels, at backing scale `dpr` and editor `zoom`.
        // Objects drawn from pixels or text (conceal, magnifier lens, image,
        // text) return only their vector parts; Editor/RenderObjects.cpp adds the rest.
        virtual std::vector<Shape> Shapes(double dpr, double zoom) const { (void)dpr; (void)zoom; return {}; }

        // ---- Fields ----
        std::int64_t id = 0;                     // unique within its Document (Document::Add)
        Rect rect;
        std::wstring state{ ObjectState::Creating };
        Point mouseDown;                         // mdsx, mdsy
        Rect mouseDownRect{ 0, 0, 100, 100 };
        Color color{ 0xFF, 0x0C, 0x01, 0xFF };   // defaultColor #FF0C01
        std::int32_t size = 2;                   // toolbar size step 0–9
        Palette preferredPalette = Palette::Drawing;
        bool wantsSpotlight = false;             // true only for ObjSpotlight

        bool IsSelected() const noexcept { return state == ObjectState::Selected; }
        bool IsHighlighted() const noexcept { return state == ObjectState::Highlighted; }
        bool IsCreating() const noexcept { return state == ObjectState::Creating; }
        // The colour drawn now: the hover blue while highlighted, else `color`.
        Color DrawColor() const noexcept;

        // Init: state "creating", colour from `defaultColor`, size from
        // "<Type>: size" (default 2). Call right after constructing a new object (not for Load).
        // Subclasses read their own preferences on top (ObjArrow: arrowType, ...).
        virtual void ApplyDefaults(Settings& settings);
        // Per-class preference key "Eikon.<ClassName>: <property>"; the cursor code builds the
        // same prefix + name + ": size".
        std::wstring PreferenceKey(std::wstring_view property) const;
        // Stores a per-class preference; every one is stored as a string.
        void SavePreference(Settings& settings, std::wstring_view property, std::wstring_view value) const;

        // Moves the object back inside `bounds` keeping its size.
        void KeepInside(Rect const& bounds);
        // Clips the object to `bounds` instead.
        void ClipInside(Rect const& bounds);

    protected:
        // {"rect":{"x1","y1","width","height"},"size":N,"color":"#RRGGBB"}
        JsonValue DumpBase() const;
        bool LoadBase(JsonValue const& json);

        // ProtoObject.keyDown: Esc deselects; while selected the arrow keys move by one
        // image point (dpr pixels; 1 pixel with realPixels), ×10 with Shift, and Ctrl+arrows resize
        // (left/right move the right edge, up/down the bottom edge). A rect that collapses to ≤ 0 in
        // both dimensions is restored.
        KeyResult BaseKeyDown(KeyEvent const& key, KeyContext const& context);

        // ProtoObject.mouseDragged for box objects: creating sizes the rect from the
        // press (Shift: a square of the larger side), "move" offsets it (Shift: dominant axis), and the
        // eight "*-resize" states move the matching edges of mouseDownRect. Positions are whole pixels.
        void DragBox(Point point, Modifiers modifiers);
        // The eight box knobs (tl, t, tr, r, br, b, bl, l) at `box` (standardized).
        static std::vector<Handle> BoxHandles(Rect const& box);
        // "move" inside `box` (grown by `slop`), else a knob within the knob radius, else "".
        static std::wstring HitBox(Rect const& box, Point point, double zoom, bool includeInside = true);
    };

    using ObjectPtr = std::unique_ptr<ProtoObject>;

    // Virtual-key codes the objects handle (winuser.h values, spelled out so Core needs no <windows.h>).
    namespace Keys
    {
        inline constexpr std::uint32_t Escape = 0x1B;
        inline constexpr std::uint32_t Left = 0x25;
        inline constexpr std::uint32_t Up = 0x26;
        inline constexpr std::uint32_t Right = 0x27;
        inline constexpr std::uint32_t Down = 0x28;
        inline constexpr std::uint32_t B = 'B';
        inline constexpr std::uint32_t F = 'F';
        inline constexpr std::uint32_t G = 'G';
        inline constexpr std::uint32_t R = 'R';
        // Digit value of a main-row or keypad digit key, or -1.
        int Digit(std::uint32_t key) noexcept;
    }

    // Luminance: 0.299 R + 0.587 G + 0.114 B in 0...1.
    double Luminance(Color color) noexcept;
    // "Saturate": largest channel to 1, smallest to 0, the middle one
    // (mid − min) / max(max − min, 0.01).
    Color Saturate(Color color) noexcept;
}
