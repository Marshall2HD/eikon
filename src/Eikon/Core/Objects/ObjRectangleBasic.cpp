#include "pch.h"
#include "Core/Objects/ObjRectangleBasic.h"

#include "Core/Objects/ObjectSupport.h"
#include "Core/Objects/Random.h"
#include "Core/Settings.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Eikon::Core
{
    using namespace Support;

    namespace
    {
        Path RoundedBox(Rect const& b, double radius)
        {
            Path path;
            double const r = (std::max)(0.0, (std::min)({ radius, b.width / 2, b.height / 2 }));
            double const l = b.Left(), t = b.Top(), rt = b.Right(), bt = b.Bottom();
            path.MoveTo({ l + r, t });
            path.LineTo({ rt - r, t });
            if (r > 0) path.ArcAround({ rt - r, t + r }, r, -kPi / 2, kPi / 2);
            path.LineTo({ rt, bt - r });
            if (r > 0) path.ArcAround({ rt - r, bt - r }, r, 0, kPi / 2);
            path.LineTo({ l + r, bt });
            if (r > 0) path.ArcAround({ l + r, bt - r }, r, kPi / 2, kPi / 2);
            path.LineTo({ l, t + r });
            if (r > 0) path.ArcAround({ l + r, t + r }, r, kPi, kPi / 2);
            path.Close();
            return path;
        }
    }

    std::map<std::wstring, std::shared_ptr<Bitmap const>>& ImageStore()
    {
        static std::map<std::wstring, std::shared_ptr<Bitmap const>> store;
        return store;
    }

    // ---------------- ObjRectangleBasic ----------------

    JsonValue ObjRectangleBasic::Dump() const
    {
        JsonValue json = DumpBase();
        SetBool(json, L"showBackground", showBackground);
        SetBool(json, L"showBorder", showBorder);
        SetNumber(json, L"backgroundAlpha", backgroundAlpha);
        return json;
    }

    bool ObjRectangleBasic::Load(JsonValue const& json)
    {
        auto bg = GetBool(json, L"showBackground"), border = GetBool(json, L"showBorder");
        auto alpha = GetNumber(json, L"backgroundAlpha");
        if (!bg || !border || !alpha || !LoadBase(json))
        {
            return false;
        }
        showBackground = *bg;
        showBorder = *border;
        backgroundAlpha = *alpha;
        state = std::wstring{ ObjectState::Unselected };
        return true;
    }

    std::wstring ObjRectangleBasic::HitTest(Point point, double zoom) const
    {
        if (IsSelected())
        {
            return HitBox(rect, point, zoom);
        }
        return rect.Standardized().Contains(point) ? L"move" : L"";
    }

    KeyResult ObjRectangleBasic::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        Rect const before = rect;
        KeyResult result = BaseKeyDown(key, context);
        if (result == KeyResult::Ignored && !HasFlag(key.modifiers, Modifiers::Ctrl))
        {
            if (key.key == Keys::B)
            {
                if (!showBorder || showBackground) showBorder = !showBorder;
                result = KeyResult::Handled;
            }
            else if (key.key == Keys::F)
            {
                if (!showBackground || showBorder) showBackground = !showBackground;
                result = KeyResult::Handled;
            }
            else if (int const digit = Keys::Digit(key.key); digit >= 0)
            {
                showBackground = true;
                backgroundAlpha = digit == 0 ? 1.0 : digit / 10.0;
                result = KeyResult::Handled;
            }
        }
        if (rect.width <= 0 && rect.height <= 0)
        {
            rect = before;
        }
        return result;
    }

    void ObjRectangleBasic::MouseDown(Point point, Modifiers)
    {
        mouseDown = point;
        mouseDownRect = rect;
        if (IsCreating())
        {
            rect = { std::round(point.x), std::round(point.y), 0, 0 };
        }
    }

    void ObjRectangleBasic::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        if (name == L"fill")
        {
            std::optional<double> parsed;
            try
            {
                std::size_t used = 0;
                double const v = std::stod(std::wstring{ value }, &used);
                if (used == value.size()) parsed = v;
            }
            catch (...)
            {
            }
            backgroundAlpha = parsed ? *parsed / 100.0 : 0.01;
            showBackground = backgroundAlpha > 0;
            showBorder = true;
            return;
        }
        ProtoObject::SetProperty(name, value);
    }

    std::wstring ObjRectangleBasic::Property(std::wstring_view name) const
    {
        if (name == L"fill")
        {
            if (showBackground && backgroundAlpha != 0) return backgroundAlpha == 1.0 ? L"100" : L"40";
            return L"0";
        }
        return ProtoObject::Property(name);
    }

    std::vector<Shape> ObjRectangleBasic::Shapes(double dpr, double) const
    {
        Rect const b = rect.Standardized();
        if (b.width <= 0 || b.height <= 0)
        {
            return {};
        }
        double const minSide = (std::min)(b.width, b.height);
        double const w = kWidths[std::clamp(size, 0, 9)];
        double const stroke = (std::min)(minSide / 10.0, w) * dpr;
        Path const path = RoundedBox(b, (std::min)(minSide / 3.0, 2.0 * w * dpr));
        std::vector<Shape> shapes;
        if (showBackground)
        {
            Shape fill;
            fill.path = path;
            fill.fill = true;
            fill.fillPaint = { color, backgroundAlpha };
            shapes.push_back(std::move(fill));
        }
        if (showBorder || IsHighlighted())
        {
            Shape border;
            border.path = path;
            border.stroke = true;
            border.strokePaint = { DrawColor(), 1.0 };
            border.strokeWidth = IsHighlighted() ? (std::max)(stroke, 2.0 * dpr) : stroke;
            border.join = Shape::Join::Round;
            shapes.push_back(std::move(border));
        }
        return shapes;
    }

    // ---------------- ObjImage ----------------

    void ObjImage::ApplyDefaults(Settings& settings)
    {
        ProtoObject::ApplyDefaults(settings);
        showBorder = false;
        showBackground = false;
        backgroundAlpha = 1.0;
    }

    JsonValue ObjImage::Dump() const
    {
        JsonValue json = ObjRectangleBasic::Dump();
        SetString(json, L"imageHash", imageHash);
        return json;
    }

    bool ObjImage::Load(JsonValue const& json)
    {
        auto hash = GetString(json, L"imageHash");
        if (!hash || !ObjRectangleBasic::Load(json))
        {
            return false;
        }
        imageHash = *hash;
        if (auto image = Image(); image && image->height > 0)
        {
            m_aspect = static_cast<double>(image->width) / image->height;
        }
        return true;
    }

    void ObjImage::Insert(std::shared_ptr<Bitmap const> image, Rect const& visible, Size const& canvas)
    {
        imageHash = Random::Hash(24);
        double const w = image ? image->width : 0;
        double const h = image ? image->height : 0;
        ImageStore()[imageHash] = std::move(image);
        double const cx = visible.x + visible.width / 2;
        double const cy = visible.y + visible.height / 2;
        double x = std::trunc(cx - w / 2.0);
        double y = std::trunc(cy - h / 2.0);
        if (canvas.width <= w) x = std::trunc((std::min)(canvas.width * 0.5, 100.0));
        if (canvas.height <= h) y = std::trunc((std::min)(canvas.height * 0.5, 100.0));
        rect = { x, y, w, h };
        m_aspect = h > 0 ? w / h : 1.0;
        state = std::wstring{ ObjectState::Selected };
    }

    std::shared_ptr<Bitmap const> ObjImage::Image() const
    {
        auto const& store = ImageStore();
        auto const it = store.find(imageHash);
        return it == store.end() ? nullptr : it->second;
    }

    void ObjImage::MouseDragged(Point point, Modifiers modifiers)
    {
        DragBox(point, modifiers);
        // Corner handles keep the aspect ratio (height = width / aspect); edges stretch.
        std::wstring_view const s = state;
        if (m_aspect > 0 && (s == L"tl-resize" || s == L"tr-resize" || s == L"bl-resize" || s == L"br-resize"))
        {
            Rect const down = mouseDownRect.Standardized();
            double const h = rect.width / m_aspect;
            if (s == L"tl-resize" || s == L"tr-resize")
            {
                rect.y = down.Bottom() - h;
            }
            rect.height = h;
        }
    }

    KeyResult ObjImage::KeyDown(KeyEvent const& key, KeyContext const& context)
    {
        if (key.key == Keys::G && !HasFlag(key.modifiers, Modifiers::Ctrl) && state == ObjectState::Selected)
        {
            if (auto const& gif = Environment().saveGif)
            {
                gif(*this);
            }
            return KeyResult::Handled;
        }
        return ObjRectangleBasic::KeyDown(key, context);
    }

    void ObjImage::SetProperty(std::wstring_view name, std::wstring_view value)
    {
        if (name == L"transparency")
        {
            backgroundAlpha = value == L"true" ? 0.5 : 1.0;
            return;
        }
        if (name == L"saveGIF")
        {
            if (auto const& gif = Environment().saveGif)
            {
                gif(*this);
            }
            return;
        }
        ObjRectangleBasic::SetProperty(name, value);
    }

    std::wstring ObjImage::Property(std::wstring_view name) const
    {
        if (name == L"transparency") return backgroundAlpha != 1.0 ? L"true" : L"false";
        return ObjRectangleBasic::Property(name);
    }

    std::vector<Shape> ObjImage::Shapes(double dpr, double) const
    {
        // The pixels are drawn by the renderer; only the hover outline is vector.
        if (!IsHighlighted())
        {
            return {};
        }
        Shape hover;
        Rect const b = rect.Standardized();
        hover.path.MoveTo({ b.Left(), b.Top() });
        hover.path.LineTo({ b.Right(), b.Top() });
        hover.path.LineTo({ b.Right(), b.Bottom() });
        hover.path.LineTo({ b.Left(), b.Bottom() });
        hover.path.Close();
        hover.stroke = true;
        hover.strokePaint = { kHoverBlue, 1.0 };
        hover.strokeWidth = 2.0 * dpr;
        return { hover };
    }
}
