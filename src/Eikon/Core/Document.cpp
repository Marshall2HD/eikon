#include "pch.h"
#include "Core/Document.h"

#include "Core/Objects/Factory.h"

#include <algorithm>

namespace Eikon::Core
{
    Document::Document(Bitmap image) : m_image(std::make_shared<Bitmap const>(std::move(image)))
    {
        m_crop = Source();
        // Undo.State.backdropConfig: the backdrop undoes with the image.
        RegisterUndoPart({ L"backdropConfig", [this] { return Backdrop::ToJson(backdrop.config); },
                           [this](JsonValue const& value) {
                               Backdrop::Config restored = backdrop.config;
                               restored.isOn = 1; // Apply resets inset/ratio/minWidth only for a config that was off
                               Backdrop::Apply(restored, value);
                               if (restored != backdrop.config)
                               {
                                   backdrop.config = restored;
                                   backdrop.Changed();
                               }
                           } });
    }

    void Document::ReplaceImage(Bitmap image, Point cropOffset)
    {
        m_image = std::make_shared<Bitmap const>(std::move(image));
        m_crop = m_crop.Offset(cropOffset.x, cropOffset.y).Intersection(Source());
        if (m_crop.IsEmpty())
        {
            m_crop = Source();
        }
        m_growth = {};
        Changed();
    }

    Rect Document::Source() const noexcept
    {
        return { 0, 0, static_cast<double>(m_image->width), static_cast<double>(m_image->height) };
    }

    void Document::SetCrop(Rect crop)
    {
        crop = crop.Standardized();
        if (crop == m_crop)
        {
            return;
        }
        m_crop = crop;
        Changed();
    }

    void Document::SetCanvasGrowth(Insets growth)
    {
        if (growth == m_growth)
        {
            return;
        }
        m_growth = growth;
        Changed();
    }

    Rect Document::Canvas() const noexcept
    {
        Rect const c = m_crop.Standardized();
        return { c.x - m_growth.left, c.y - m_growth.top, c.width + m_growth.left + m_growth.right, c.height + m_growth.top + m_growth.bottom };
    }

    ProtoObject& Document::Add(ObjectPtr object)
    {
        object->id = m_nextId++;
        m_objects.push_back(std::move(object));
        Changed();
        return *m_objects.back();
    }

    ObjectPtr Document::Remove(std::int64_t id)
    {
        auto it = std::find_if(m_objects.begin(), m_objects.end(), [id](ObjectPtr const& o) { return o->id == id; });
        if (it == m_objects.end())
        {
            return nullptr;
        }
        ObjectPtr removed = std::move(*it);
        m_objects.erase(it);
        Changed();
        return removed;
    }

    ProtoObject* Document::Find(std::int64_t id) const noexcept
    {
        for (auto const& object : m_objects)
        {
            if (object->id == id)
            {
                return object.get();
            }
        }
        return nullptr;
    }

    std::ptrdiff_t Document::IndexOf(std::int64_t id) const noexcept
    {
        for (std::size_t i = 0; i < m_objects.size(); ++i)
        {
            if (m_objects[i]->id == id)
            {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    }

    void Document::ReplaceObjects(std::vector<ObjectPtr> objects)
    {
        m_objects.clear();
        for (auto& object : objects)
        {
            if (object)
            {
                object->id = m_nextId++;
                m_objects.push_back(std::move(object));
            }
        }
        Changed();
    }

    void Document::RegisterUndoPart(UndoPart part)
    {
        std::erase_if(m_undoParts, [&](UndoPart const& p) { return p.name == part.name; });
        m_undoParts.push_back(std::move(part));
    }

    UndoSnapshot Document::Snapshot(std::optional<Rect> selection, std::optional<std::int64_t> selectedId) const
    {
        UndoSnapshot snapshot;
        std::int64_t index = 0;
        for (auto const& object : m_objects)
        {
            // Leaves out objects still being created.
            if (object->state == ObjectState::Creating)
            {
                continue;
            }
            if (selectedId && object->id == *selectedId)
            {
                snapshot.selectedObj = index;
            }
            snapshot.objects.Append(DumpWithType(*object));
            ++index;
        }
        snapshot.selectionRect = selection;
        snapshot.cropRect = m_crop;
        snapshot.counterCount = m_counterCount;
        for (auto const& part : m_undoParts)
        {
            if (part.save)
            {
                snapshot.extras.emplace_back(part.name, part.save());
            }
        }
        return snapshot;
    }

    std::optional<std::int64_t> Document::Restore(UndoSnapshot const& snapshot)
    {
        std::vector<ObjectPtr> objects;
        std::optional<std::size_t> selectedIndex;
        for (std::size_t i = 0; i < snapshot.objects.Items().size(); ++i)
        {
            ObjectPtr object = LoadObject(snapshot.objects.Items()[i]);
            if (!object)
            {
                continue;
            }
            bool const selected = snapshot.selectedObj && static_cast<std::size_t>(*snapshot.selectedObj) == i;
            object->state = std::wstring{ selected ? ObjectState::Selected : ObjectState::Unselected };
            if (selected)
            {
                selectedIndex = objects.size();
            }
            objects.push_back(std::move(object));
        }
        m_crop = snapshot.cropRect.Standardized();
        m_counterCount = snapshot.counterCount;
        for (auto const& part : m_undoParts)
        {
            if (!part.restore)
            {
                continue;
            }
            for (auto const& [name, value] : snapshot.extras)
            {
                if (name == part.name)
                {
                    part.restore(value);
                }
            }
        }
        ReplaceObjects(std::move(objects));
        if (selectedIndex)
        {
            return m_objects[*selectedIndex]->id;
        }
        return std::nullopt;
    }
}
