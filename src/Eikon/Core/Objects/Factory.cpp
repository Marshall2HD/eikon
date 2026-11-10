#include "pch.h"
#include "Core/Objects/Factory.h"

#include "Core/Objects/ObjArrow.h"
#include "Core/Objects/ObjConceal.h"
#include "Core/Objects/ObjCounter.h"
#include "Core/Objects/ObjFreehand.h"
#include "Core/Objects/ObjLine.h"
#include "Core/Objects/ObjOval.h"
#include "Core/Objects/ObjRectangleBasic.h"
#include "Core/Objects/ObjRuler.h"
#include "Core/Objects/ObjText.h"
#include "Core/Settings.h"

namespace Eikon::Core
{
    ObjectPtr NewObject(std::wstring_view name)
    {
        if (name == L"ObjArrow") return std::make_unique<ObjArrow>();
        if (name == L"ObjLine") return std::make_unique<ObjLine>();
        if (name == L"ObjOval") return std::make_unique<ObjOval>();
        if (name == L"ObjRectangle") return std::make_unique<ObjRectangle>();
        if (name == L"ObjSpotlight") return std::make_unique<ObjSpotlight>();
        if (name == L"ObjMagnifier") return std::make_unique<ObjMagnifier>();
        if (name == L"ObjRectangleBasic") return std::make_unique<ObjRectangleBasic>();
        if (name == L"ObjImage") return std::make_unique<ObjImage>();
        if (name == L"ObjText") return std::make_unique<ObjText>();
        if (name == L"ObjCounter") return std::make_unique<ObjCounter>();
        if (name == L"ObjFreehand") return std::make_unique<ObjFreehand>();
        if (name == L"ObjHighlighter") return std::make_unique<ObjHighlighter>();
        if (name == L"ObjConceal") return std::make_unique<ObjConceal>();
        if (name == L"ObjRuler") return std::make_unique<ObjRuler>();
        return nullptr;
    }

    ObjectPtr CreateObject(std::wstring_view tool, Settings& settings)
    {
        // Tool ids; "backdrop" and "append" open panels instead of creating objects.
        std::wstring_view name;
        if (tool == L"arrow") name = L"ObjArrow";
        else if (tool == L"text") name = L"ObjText";
        else if (tool == L"ruler") name = L"ObjRuler";
        else if (tool == L"rectangle") name = L"ObjRectangle";
        else if (tool == L"freehand") name = L"ObjFreehand";
        else if (tool == L"magnifier") name = L"ObjMagnifier";
        else if (tool == L"blur") name = L"ObjConceal";
        else if (tool == L"highlighter") name = L"ObjHighlighter";
        else if (tool == L"spotlight") name = L"ObjSpotlight";
        else if (tool == L"counter") name = L"ObjCounter";
        else if (tool == L"oval") name = L"ObjOval";
        else if (tool == L"line") name = L"ObjLine";
        else if (tool == L"image") name = L"ObjImage";
        else return nullptr;
        ObjectPtr object = NewObject(name);
        if (object)
        {
            object->ApplyDefaults(settings);
        }
        return object;
    }

    ObjectPtr LoadObject(JsonValue const& json)
    {
        auto const* type = json.Find(L"type");
        if (!type || !type->IsString())
        {
            return nullptr;
        }
        auto const* dump = json.Find(L"object");
        ObjectPtr object = NewObject(type->AsString());
        if (!object || !dump || !dump->IsObject() || !object->Load(*dump))
        {
            return nullptr;
        }
        return object;
    }

    ObjectPtr CreateImageObject(std::shared_ptr<Bitmap const> image, Rect rect, Settings& settings)
    {
        if (!image || image->IsEmpty())
        {
            return nullptr;
        }
        auto object = std::make_unique<ObjImage>();
        object->ApplyDefaults(settings);
        // Insert stores the image under a new hash and sets the aspect; Add Capture then places it at
        // its own position instead of Insert's centring.
        object->Insert(std::move(image), rect, { rect.Right() + 1, rect.Bottom() + 1 });
        object->rect = rect;
        return object;
    }

    JsonValue DumpWithType(ProtoObject const& object)
    {
        JsonValue json = JsonValue::Object();
        json.Set(L"type", JsonValue::String(std::wstring{ object.ClassName() }));
        json.Set(L"object", object.Dump());
        return json;
    }
}
