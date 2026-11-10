#pragma once

// Undo singleton (record, undo, redo). States are (label, full, comparable) strings: `full` is the JSON of an
// UndoSnapshot, `comparable` the same without the parts that should not make a new step. A record
// whose comparable equals the current state's is dropped; otherwise the redo tail is cut and the
// state appended. There is no cap. Debouncing (0.33 s) is the editor's job.

#include "Core/Geometry.h"
#include "Core/Json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core
{
    // Undo.State (Codable): objects (each object's Dump, "creating" ones left out), selectedObj,
    // selectionRect, cropRect, counterCount, plus extra parts such as backdropConfig.
    struct UndoSnapshot
    {
        JsonValue objects = JsonValue::Array();
        std::optional<std::int64_t> selectedObj;     // index into objects
        std::optional<Rect> selectionRect;           // the crop selection, image pixels
        Rect cropRect;
        std::int32_t counterCount = 0;
        // Other members by name ("backdropConfig", ...), registered with Document::RegisterUndoPart.
        std::vector<std::pair<std::wstring, JsonValue>> extras;

        std::wstring Full() const;
        // Full() without selectedObj (selecting an object is not an undo step).
        std::wstring Comparable() const;
        static std::optional<UndoSnapshot> Parse(std::wstring_view full);

    private:
        JsonValue ToJson(bool withSelectedObject) const;
    };

    class Undo
    {
    public:
        // Returns false when the state equals the current one and was dropped.
        bool Record(std::wstring label, std::wstring full, std::wstring comparable);
        // Steps back/forward and returns the `full` string to restore, or nullopt at the end.
        std::optional<std::wstring> StepBack();
        std::optional<std::wstring> StepForward();
        void Clear() noexcept;

        bool CanUndo() const noexcept { return m_position > 0; }
        bool CanRedo() const noexcept { return m_position >= 0 && static_cast<std::size_t>(m_position) + 1 < m_states.size(); }
        std::size_t Count() const noexcept { return m_states.size(); }
        std::ptrdiff_t Position() const noexcept { return m_position; }
        std::wstring_view CurrentLabel() const noexcept;

    private:
        struct State
        {
            std::wstring label;
            std::wstring full;
            std::wstring comparable;
        };
        std::vector<State> m_states;
        std::ptrdiff_t m_position = -1;
    };
}
