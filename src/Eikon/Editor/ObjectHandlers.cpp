#include "pch.h"
#include <winrt/Microsoft.Graphics.Canvas.Brushes.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include "Editor/ObjectHandlers.h"

#include "Core/Objects/Factory.h"
#include "Core/Settings.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <cmath>

namespace Eikon::Editor
{
    namespace
    {
        constexpr double kClickSlop = 5;   // points: a shorter drag is a click
    }

    ObjectHandlers::ObjectHandlers(Host host) : m_host(std::move(host)) {}

    void ObjectHandlers::SetDocument(std::shared_ptr<Core::Document> document)
    {
        m_document = std::move(document);
        m_active.reset();
        m_hovered.reset();
        m_dragId.reset();
        m_guides.clear();
    }

    Core::ProtoObject* ObjectHandlers::Find(std::optional<std::int64_t> id) const noexcept
    {
        return (m_document && id) ? m_document->Find(*id) : nullptr;
    }

    Core::ProtoObject* ObjectHandlers::Active() const noexcept
    {
        return Find(m_active);
    }

    void ObjectHandlers::Select(std::int64_t id)
    {
        if (!m_document)
        {
            return;
        }
        for (auto const& object : m_document->Objects())
        {
            if (object->id == id)
            {
                object->state = std::wstring{ Core::ObjectState::Selected };
            }
            else if (object->state == Core::ObjectState::Selected || object->state == Core::ObjectState::Highlighted)
            {
                object->state = std::wstring{ Core::ObjectState::Unselected };
            }
        }
        m_active = id;
        m_hovered.reset();
        if (m_host.activeChanged)
        {
            m_host.activeChanged();
        }
    }

    bool ObjectHandlers::Deselect()
    {
        if (!m_active)
        {
            return false;
        }
        if (auto* object = Active())
        {
            object->state = std::wstring{ Core::ObjectState::Unselected };
        }
        m_active.reset();
        m_guides.clear();
        if (m_host.activeChanged)
        {
            m_host.activeChanged();
        }
        return true;
    }

    Core::ProtoObject* ObjectHandlers::HitTest(Core::Point image, double zoom, std::wstring* part) const
    {
        if (!m_document)
        {
            return nullptr;
        }
        // The selected object's handles first, then the stack from the top.
        if (auto* active = Active())
        {
            std::wstring hit = active->HitTest(image, zoom);
            if (!hit.empty())
            {
                if (part)
                {
                    *part = std::move(hit);
                }
                return active;
            }
        }
        auto const& objects = m_document->Objects();
        for (auto it = objects.rbegin(); it != objects.rend(); ++it)
        {
            std::wstring hit = (*it)->HitTest(image, zoom);
            if (!hit.empty())
            {
                if (part)
                {
                    *part = std::move(hit);
                }
                return it->get();
            }
        }
        return nullptr;
    }

    std::wstring ObjectHandlers::CursorAt(Core::Point image, double zoom) const
    {
        std::wstring part;
        auto* object = HitTest(image, zoom, &part);
        if (!object)
        {
            return {};
        }
        if (object->IsSelected())
        {
            for (auto const& handle : object->Handles())
            {
                if (handle.name == part)
                {
                    return handle.cursor.empty() ? std::wstring{ L"move" } : handle.cursor;
                }
            }
            return L"move";
        }
        return L"point";
    }

    void ObjectHandlers::SetHovered(std::optional<std::int64_t> id)
    {
        if (auto* previous = Find(m_hovered); previous && previous->state == Core::ObjectState::Highlighted)
        {
            previous->state = std::wstring{ Core::ObjectState::Unselected };
        }
        m_hovered = id;
        if (auto* next = Find(m_hovered); next && next->state == Core::ObjectState::Unselected)
        {
            next->state = std::wstring{ Core::ObjectState::Highlighted };
        }
    }

    bool ObjectHandlers::Hover(Core::Point image, double zoom)
    {
        if (m_dragId)
        {
            return false;
        }
        auto* object = HitTest(image, zoom);
        std::optional<std::int64_t> id;
        if (object && !object->IsSelected())
        {
            id = object->id;
        }
        if (id == m_hovered)
        {
            return false;
        }
        SetHovered(id);
        return true;
    }

    bool ObjectHandlers::MouseDown(Core::Point image, Modifiers modifiers, double zoom, std::wstring_view tool)
    {
        if (!m_document)
        {
            return false;
        }
        m_guides.clear();
        std::wstring part;
        Core::ProtoObject* object = HitTest(image, zoom, &part);
        if (object)
        {
            if (object->id != m_active)
            {
                Select(object->id);
                if (part != Core::ObjectState::Move)
                {
                    part = std::wstring{ Core::ObjectState::Move };
                }
            }
            object->state = part;
            m_dragPart = part;
        }
        else
        {
            if (tool.empty())
            {
                return false;
            }
            Core::ObjectPtr created = Core::CreateObject(tool, Core::Settings::Current());
            if (!created)
            {
                return false;
            }
            Deselect();
            created->state = std::wstring{ Core::ObjectState::Creating };
            object = &m_document->Add(std::move(created));
            m_dragPart = std::wstring{ Core::ObjectState::Creating };
        }
        SetHovered(std::nullopt);
        m_dragId = object->id;
        m_downPoint = image;
        m_downRect = object->rect;
        m_movedEnough = false;
        object->MouseDown(image, modifiers);
        return true;
    }

    void ObjectHandlers::MouseDragged(Core::Point image, Modifiers modifiers, Core::Framer const& framer, double imageDpr)
    {
        auto* object = Find(m_dragId);
        if (!object)
        {
            return;
        }
        double const slop = kClickSlop * imageDpr;
        if (std::abs(image.x - m_downPoint.x) >= slop || std::abs(image.y - m_downPoint.y) >= slop)
        {
            m_movedEnough = true;
        }
        object->MouseDragged(image, modifiers);
        m_guides.clear();
        // Snapping while moving, off with Ctrl or Shift held.
        auto const mode = static_cast<Core::SnappingMode>(Core::Settings::Current().SnappingMode());
        if (m_dragPart == Core::ObjectState::Move && mode != Core::SnappingMode::Off && !HasFlag(modifiers, Modifiers::Ctrl) &&
            !HasFlag(modifiers, Modifiers::Shift))
        {
            std::vector<Core::Rect> candidates{ m_document->Source() };
            for (auto const& other : m_document->Objects())
            {
                if (other->id == object->id || other->state == Core::ObjectState::Creating)
                {
                    continue;
                }
                if (mode == Core::SnappingMode::SimilarObjects && other->ClassName() != object->ClassName())
                {
                    continue;
                }
                candidates.push_back(other->rect.Standardized());
            }
            auto const snap = Core::Snap(object->rect.Standardized(), candidates, Core::SnapThreshold(framer.dpr, framer.zoom));
            if (snap.snappedX || snap.snappedY)
            {
                object->MoveBy(snap.dx, snap.dy);
                m_guides = snap.guides;
            }
        }
    }

    bool ObjectHandlers::MouseUp(Core::Point image, Modifiers modifiers)
    {
        auto* object = Find(m_dragId);
        std::wstring const part = m_dragPart;
        m_dragId.reset();
        m_dragPart.clear();
        m_guides.clear();
        if (!object)
        {
            return false;
        }
        if (part == Core::ObjectState::Creating)
        {
            object->MouseUp(image, modifiers);
            if (object->IsEmpty())
            {
                m_document->Remove(object->id);
                if (m_host.objectsChanged)
                {
                    m_host.objectsChanged();
                }
                return true;
            }
            std::int64_t const id = object->id;
            Select(id);
            if (m_host.recordUndo)
            {
                m_host.recordUndo(L"");
            }
        }
        else
        {
            if (m_movedEnough)
            {
                object->MouseUp(image, modifiers);
            }
            else
            {
                // A click: put the object back exactly where it was.
                object->rect = m_downRect;
            }
            object->state = std::wstring{ Core::ObjectState::Selected };
            if (m_movedEnough && m_host.recordUndo)
            {
                m_host.recordUndo(L"");
            }
        }
        if (m_host.objectsChanged)
        {
            m_host.objectsChanged();
        }
        return true;
    }

    bool ObjectHandlers::CancelCreation()
    {
        auto* object = Find(m_dragId);
        if (!object || m_dragPart != Core::ObjectState::Creating)
        {
            return false;
        }
        m_document->Remove(object->id);
        m_dragId.reset();
        m_dragPart.clear();
        m_guides.clear();
        if (m_host.objectsChanged)
        {
            m_host.objectsChanged();
        }
        return true;
    }

    bool ObjectHandlers::KeyDown(std::uint32_t key, Modifiers modifiers, double imageDpr, bool realPixels)
    {
        auto* object = Active();
        if (!object)
        {
            return false;
        }
        bool const ctrl = HasFlag(modifiers, Modifiers::Ctrl);
        if (key == VK_TAB && !ctrl)
        {
            SelectNext();
            return true;
        }
        if ((key == VK_DELETE || key == VK_BACK) && !ctrl)
        {
            DeleteActive();
            return true;
        }
        if (ctrl && key == 'D')
        {
            DuplicateActive();
            return true;
        }
        if (ctrl && key == 'C')
        {
            CopyActive();
            return true;
        }
        if (ctrl && key == 'V' && !m_copied.empty())
        {
            return PasteCopied();
        }
        if (ctrl && (key == VK_OEM_6 || key == VK_OEM_4))
        {
            int const next = std::clamp(object->size + (key == VK_OEM_6 ? 1 : -1), 0, 9);
            SetActiveProperty(L"size", std::to_wstring(next));
            return true;
        }
        Core::KeyResult const result = object->KeyDown({ key, modifiers }, { imageDpr, realPixels });
        if (result == Core::KeyResult::Deselect)
        {
            Deselect();
            return true;
        }
        if (result == Core::KeyResult::Handled)
        {
            if (m_host.recordUndoDebounced)
            {
                m_host.recordUndoDebounced();
            }
            if (m_host.objectsChanged)
            {
                m_host.objectsChanged();
            }
            return true;
        }
        return false;
    }

    void ObjectHandlers::DeleteActive()
    {
        if (!m_active || !m_document)
        {
            return;
        }
        m_document->Remove(*m_active);
        m_active.reset();
        if (m_host.activeChanged)
        {
            m_host.activeChanged();
        }
        if (m_host.recordUndo)
        {
            m_host.recordUndo(L"");
        }
        if (m_host.objectsChanged)
        {
            m_host.objectsChanged();
        }
    }

    void ObjectHandlers::DuplicateActive()
    {
        auto* object = Active();
        if (!object)
        {
            return;
        }
        m_copied = Core::DumpWithType(*object).Serialize();
        PasteCopied();
    }

    void ObjectHandlers::CopyActive()
    {
        auto* object = Active();
        if (!object)
        {
            return;
        }
        m_copied = Core::DumpWithType(*object).Serialize();
        try
        {
            winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
            package.SetText(m_copied);
            winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        }
        catch (...)
        {
        }
    }

    bool ObjectHandlers::PasteCopied()
    {
        if (m_copied.empty() || !m_document)
        {
            return false;
        }
        auto const json = Core::JsonValue::Parse(m_copied);
        if (!json)
        {
            return false;
        }
        Core::ObjectPtr object = Core::LoadObject(*json);
        if (!object)
        {
            return false;
        }
        // Offset so the copy is visible next to the source object.
        object->MoveBy(20, 20);
        object->state = std::wstring{ Core::ObjectState::Selected };
        std::int64_t const id = m_document->Add(std::move(object)).id;
        m_copied = Core::DumpWithType(*m_document->Find(id)).Serialize();
        Select(id);
        if (m_host.recordUndo)
        {
            m_host.recordUndo(L"");
        }
        if (m_host.objectsChanged)
        {
            m_host.objectsChanged();
        }
        return true;
    }

    void ObjectHandlers::SelectNext()
    {
        if (!m_document || m_document->Objects().empty())
        {
            return;
        }
        auto const& objects = m_document->Objects();
        std::ptrdiff_t index = m_active ? m_document->IndexOf(*m_active) : -1;
        index = (index + 1) % static_cast<std::ptrdiff_t>(objects.size());
        Select(objects[static_cast<std::size_t>(index)]->id);
    }

    void ObjectHandlers::SetActiveProperty(std::wstring_view key, std::wstring_view value)
    {
        auto* object = Active();
        if (!object)
        {
            return;
        }
        object->SetProperty(key, value);
        // The next object of this class starts with the same value ("<Class>: <prop>").
        if (key == L"size")
        {
            Core::Settings::Current().WriteInt(object->PreferenceKey(L"size"), object->size);
        }
        else if (key == L"color")
        {
            Core::Settings::Current().DefaultColor(std::wstring{ value });
        }
        if (m_host.recordUndoDebounced)
        {
            m_host.recordUndoDebounced();
        }
        if (m_host.objectsChanged)
        {
            m_host.objectsChanged();
        }
    }

    void ObjectHandlers::Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Render::Context const& context)
    {
        for (auto const& guide : m_guides)
        {
            Render::DrawGuide(session, context.ToCanvas(guide.from), context.ToCanvas(guide.to), context.rasterScale);
        }
        auto* object = Active();
        if (!object || object->state == Core::ObjectState::Creating)
        {
            return;
        }
        // A selected object shows only its knobs, with no selection outline. The black/white α 0.4
        // layer (lplaceholder) is the highlighter tool's brush preview, shown only while the highlighter
        // tool is active.
        for (auto const& handle : object->HandlesAt(context.zoom, context.dpr))
        {
            if (handle.shown)
            {
                Render::DrawKnob(session, context.ToCanvas(handle.position), handle.name == m_dragPart);
            }
        }
    }
}
