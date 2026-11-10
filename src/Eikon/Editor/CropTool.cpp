#include "pch.h"
#include <winrt/Microsoft.Graphics.Canvas.Brushes.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include "Editor/CropTool.h"

#include "Core/EditorTools.h"
#include "Strings.h"

#include <cmath>

namespace Eikon::Editor
{
    namespace
    {
        constexpr double kAnimationSeconds = 0.15;   // path animation
        constexpr std::wstring_view kSelect = L"select";
        constexpr std::wstring_view kUnits = L"units";

        bool IsEdge(std::wstring_view zone)
        {
            return zone == Core::CropZone::Left || zone == Core::CropZone::Right || zone == Core::CropZone::Top || zone == Core::CropZone::Bottom;
        }

        std::wstring EdgeName(std::wstring_view zone)
        {
            if (zone == Core::CropZone::Left) return L"left";
            if (zone == Core::CropZone::Right) return L"right";
            if (zone == Core::CropZone::Top) return L"top";
            return L"bottom";
        }

        // kCAMediaTimingFunctionDefault: cubic-bezier(0.25, 0.1, 0.25, 1).
        double EaseDefault(double t)
        {
            double lo = 0;
            double hi = 1;
            for (int i = 0; i < 30; ++i)
            {
                double const mid = (lo + hi) / 2;
                double const x = 3 * mid * (1 - mid) * (1 - mid) * 0.25 + 3 * mid * mid * (1 - mid) * 0.25 + mid * mid * mid;
                (x < t ? lo : hi) = mid;
            }
            double const s = (lo + hi) / 2;
            return 3 * s * (1 - s) * (1 - s) * 0.1 + 3 * s * s * (1 - s) * 1.0 + s * s * s;
        }
    }

    void CropTool::SetSelection(Core::Rect rect, bool animate)
    {
        rect = rect.Standardized();
        if (animate && m_selection)
        {
            m_animFrom = *m_selection;
            m_animStart = std::chrono::steady_clock::now();
            m_animating = true;
        }
        else
        {
            m_animating = false;
        }
        m_selection = rect;
    }

    void CropTool::Reset() noexcept
    {
        m_selection.reset();
        m_state = L"default";
        m_animating = false;
        m_sizeLabel = {};
    }

    std::wstring_view CropTool::HitTest(Core::Framer const& framer, Core::Point view) const
    {
        if (!m_selection)
        {
            return Core::CropZone::None;
        }
        double const k = framer.dpr;
        if (m_sizeLabel.Width > 0 && view.x >= m_sizeLabel.X * k && view.x <= (m_sizeLabel.X + m_sizeLabel.Width) * k &&
            view.y >= m_sizeLabel.Y * k && view.y <= (m_sizeLabel.Y + m_sizeLabel.Height) * k)
        {
            return kUnits;
        }
        return Core::CropHitZone(framer.ToViewRect(*m_selection), view);
    }

    void CropTool::MouseDown(std::wstring_view zone, Core::Point image, double timeSeconds)
    {
        m_downImage = image;
        m_downTime = timeSeconds;
        m_animating = false;
        if (zone == kUnits)
        {
            m_state = std::wstring{ kUnits };
            return;
        }
        if (zone == Core::CropZone::None || !m_selection)
        {
            m_state = std::wstring{ kSelect };
            m_selection = Core::Rect{ image.x, image.y, 0, 0 };
            m_downRect = *m_selection;
            return;
        }
        m_state = std::wstring{ zone };
        m_downRect = *m_selection;
    }

    void CropTool::MouseDragged(Core::Point image, Core::Rect bounds)
    {
        if (m_state == L"default" || m_state == kUnits)
        {
            return;
        }
        double const dx = image.x - m_downImage.x;
        double const dy = image.y - m_downImage.y;
        if (m_state == kSelect)
        {
            // A new selection follows the cursor, kept inside the image.
            Core::Point const a = m_downImage;
            Core::Point const b{ std::clamp(image.x, bounds.Left(), bounds.Right()), std::clamp(image.y, bounds.Top(), bounds.Bottom()) };
            m_selection = Core::Rect{ a.x, a.y, b.x - a.x, b.y - a.y };
            return;
        }
        Core::Rect next = Core::DragSelection(m_downRect, m_state, dx, dy);
        if (m_state == Core::CropZone::Move)
        {
            next = Core::KeepInside(next, bounds);
        }
        else
        {
            next = next.Standardized().Intersection(bounds);
        }
        m_selection = next;
    }

    CropTool::UpResult CropTool::MouseUp(Core::Point image, double timeSeconds, double dpr, bool realPixels)
    {
        std::wstring const state = m_state;
        m_state = L"default";
        if (state == L"default")
        {
            return UpResult::None;
        }
        double const elapsed = timeSeconds - m_downTime;
        if (std::abs(m_downImage.x - image.x) + std::abs(m_downImage.y - image.y) < 6.0 && IsEdge(state) && elapsed < 0.3)
        {
            m_selection = m_downRect;
            m_adjustEdge = EdgeName(state);
            return UpResult::AdjustEdge;
        }
        if (state == kUnits)
        {
            return UpResult::ToggleUnits;
        }
        if (!m_selection)
        {
            return UpResult::None;
        }
        Core::Rect r = m_selection->Standardized();
        if (state == kSelect && elapsed < 0.1 && r.width * r.width + r.height * r.height < 300.0 * dpr * dpr)
        {
            Reset();
            return UpResult::Cleared;
        }
        r = Core::SnapSelection(r, dpr, realPixels);
        if (r.width == 0 || r.height == 0)
        {
            Reset();
            return UpResult::Cleared;
        }
        m_selection = r;
        return UpResult::Changed;
    }

    bool CropTool::Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Render::Context const& context,
                        winrt::Windows::Foundation::Rect const& area, double imageDpr, bool realPixels, bool hoverUnits,
                        std::wstring_view badgeHint)
    {
        if (!m_selection)
        {
            m_sizeLabel = {};
            return false;
        }
        Core::Rect shown = m_selection->Standardized();
        bool animating = false;
        if (m_animating)
        {
            double const t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_animStart).count() / kAnimationSeconds;
            if (t >= 1)
            {
                m_animating = false;
            }
            else
            {
                double const e = EaseDefault(t);
                Core::Rect const a = m_animFrom.Standardized();
                shown = { a.x + (shown.x - a.x) * e, a.y + (shown.y - a.y) * e, a.width + (shown.width - a.width) * e,
                          a.height + (shown.height - a.height) * e };
                animating = true;
            }
        }
        // The marquee is framed on whole points: Int(minX/dpr) .. Int(maxX/dpr).
        auto rect = context.ToCanvasRect(shown);
        float const left = std::trunc(rect.X);
        float const top = std::trunc(rect.Y);
        rect = { left, top, std::trunc(rect.X + rect.Width) - left, std::trunc(rect.Y + rect.Height) - top };
        Render::MarqueeLabels labels;
        Core::Rect const s = m_selection->Standardized();
        double const unit = realPixels ? 1.0 : imageDpr;
        // "W×H" + U+200A + px/pt, each side Int(|size| / unit).
        labels.size = std::to_wstring(static_cast<long long>(std::abs(s.width) / unit)) + L"\u00D7" +
                      std::to_wstring(static_cast<long long>(std::abs(s.height) / unit)) + L"\u200A" + (realPixels ? L"px" : L"pt");
        labels.hint = !badgeHint.empty() ? std::wstring{ badgeHint } : Strings::Get(hoverUnits ? L"Editor_CropHintUnits" : L"Editor_CropHint");
        m_sizeLabel = Render::DrawMarquee(session, area, rect, labels, context.rasterScale);
        return animating;
    }
}
