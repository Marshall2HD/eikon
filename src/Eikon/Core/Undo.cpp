#include "pch.h"
#include "Core/Undo.h"

namespace Eikon::Core
{
    namespace
    {
        JsonValue RectJson(Rect const& r)
        {
            JsonValue json = JsonValue::Object();
            json.Set(L"x1", JsonValue::Number(r.x));
            json.Set(L"y1", JsonValue::Number(r.y));
            json.Set(L"width", JsonValue::Number(r.width));
            json.Set(L"height", JsonValue::Number(r.height));
            return json;
        }

        std::optional<Rect> RectFrom(JsonValue const* json)
        {
            if (!json || !json->IsObject())
            {
                return std::nullopt;
            }
            auto number = [json](std::wstring_view key) -> std::optional<double> {
                auto const* v = json->Find(key);
                if (!v || !v->IsNumber())
                {
                    return std::nullopt;
                }
                return v->AsNumber();
            };
            auto const x = number(L"x1");
            auto const y = number(L"y1");
            auto const w = number(L"width");
            auto const h = number(L"height");
            if (!x || !y || !w || !h)
            {
                return std::nullopt;
            }
            return Rect{ *x, *y, *w, *h };
        }
    }

    JsonValue UndoSnapshot::ToJson(bool withSelectedObject) const
    {
        JsonValue json = JsonValue::Object();
        json.Set(L"objects", objects);
        if (withSelectedObject && selectedObj)
        {
            json.Set(L"selectedObj", JsonValue::Number(static_cast<double>(*selectedObj)));
        }
        if (selectionRect)
        {
            json.Set(L"selectionRect", RectJson(*selectionRect));
        }
        json.Set(L"cropRect", RectJson(cropRect));
        json.Set(L"counterCount", JsonValue::Number(counterCount));
        for (auto const& [name, value] : extras)
        {
            json.Set(name, value);
        }
        return json;
    }

    std::wstring UndoSnapshot::Full() const
    {
        return ToJson(true).Serialize();
    }

    std::wstring UndoSnapshot::Comparable() const
    {
        return ToJson(false).Serialize();
    }

    std::optional<UndoSnapshot> UndoSnapshot::Parse(std::wstring_view full)
    {
        auto json = JsonValue::Parse(full);
        if (!json || !json->IsObject())
        {
            return std::nullopt;
        }
        UndoSnapshot snapshot;
        auto const* objects = json->Find(L"objects");
        auto const crop = RectFrom(json->Find(L"cropRect"));
        if (!objects || !objects->IsArray() || !crop)
        {
            return std::nullopt;
        }
        snapshot.objects = *objects;
        snapshot.cropRect = *crop;
        snapshot.selectionRect = RectFrom(json->Find(L"selectionRect"));
        if (auto const* selected = json->Find(L"selectedObj"); selected && selected->IsNumber())
        {
            snapshot.selectedObj = static_cast<std::int64_t>(selected->AsNumber());
        }
        if (auto const* counter = json->Find(L"counterCount"); counter && counter->IsNumber())
        {
            snapshot.counterCount = static_cast<std::int32_t>(counter->AsNumber());
        }
        for (auto const& [name, value] : json->Members())
        {
            if (name != L"objects" && name != L"selectedObj" && name != L"selectionRect" && name != L"cropRect" && name != L"counterCount")
            {
                snapshot.extras.emplace_back(name, value);
            }
        }
        return snapshot;
    }

    bool Undo::Record(std::wstring label, std::wstring full, std::wstring comparable)
    {
        if (m_position >= 0 && static_cast<std::size_t>(m_position) < m_states.size() && m_states[m_position].comparable == comparable)
        {
            return false;
        }
        if (m_position + 1 < static_cast<std::ptrdiff_t>(m_states.size()))
        {
            m_states.resize(static_cast<std::size_t>(m_position + 1));
        }
        m_states.push_back({ std::move(label), std::move(full), std::move(comparable) });
        m_position = static_cast<std::ptrdiff_t>(m_states.size()) - 1;
        return true;
    }

    std::optional<std::wstring> Undo::StepBack()
    {
        if (!CanUndo())
        {
            return std::nullopt;
        }
        --m_position;
        return m_states[static_cast<std::size_t>(m_position)].full;
    }

    std::optional<std::wstring> Undo::StepForward()
    {
        if (!CanRedo())
        {
            return std::nullopt;
        }
        ++m_position;
        return m_states[static_cast<std::size_t>(m_position)].full;
    }

    void Undo::Clear() noexcept
    {
        m_states.clear();
        m_position = -1;
    }

    std::wstring_view Undo::CurrentLabel() const noexcept
    {
        if (m_position < 0 || static_cast<std::size_t>(m_position) >= m_states.size())
        {
            return {};
        }
        return m_states[static_cast<std::size_t>(m_position)].label;
    }
}
