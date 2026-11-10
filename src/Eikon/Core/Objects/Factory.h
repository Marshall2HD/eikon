#pragma once

// Creates annotation objects. The editor asks for a new object when a
// drag starts with a drawing tool selected, and for a restored object when loading an undo state.

#include "Core/Bitmap.h"
#include "Core/Objects/ProtoObject.h"

#include <memory>
#include <string_view>

namespace Eikon::Core
{
    class Settings;

    // `tool` is the editor tool id ("arrow", "text", "rectangle", ...). Returns a
    // new object with ApplyDefaults run, or nullptr when the tool creates no object.
    ObjectPtr CreateObject(std::wstring_view tool, Settings& settings);

    // An ObjImage showing `image` at `rect` (image pixels), as
    // Add Capture inserts it. nullptr for an empty image.
    ObjectPtr CreateImageObject(std::shared_ptr<Bitmap const> image, Rect rect, Settings& settings);

    // A new, default object of class `className` ("ObjArrow", ...), nullptr if unknown.
    ObjectPtr NewObject(std::wstring_view className);

    // Rebuilds an object from DumpWithType's JSON (the "type" member names the class, "object" holds
    // its Dump). nullptr if the class is unknown or the Dump is not that class's shape.
    ObjectPtr LoadObject(JsonValue const& json);

    // {"type": "<ClassName>", "object": <Dump>}: what LoadObject reads back. The Dump is nested rather
    // than merged because ObjArrow's own Dump has a "type" member (the arrow type).
    JsonValue DumpWithType(ProtoObject const& object);
}
