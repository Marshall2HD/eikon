#pragma once

// The backdrop panel, opened from the
// editor toolbar's Backdrop button and anchored to it, and the Backdrop actions it drives.

#include "Core/Document.h"

#include <memory>

namespace Eikon::Editor
{
    // turnOn: the 15 MP gate, then bdrConfig is applied, the backdrop rendered and
    // the panel shown in a flyout under `anchor`.
    void ShowBackdropPanel(winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, std::shared_ptr<Core::Document> document);

    // Render: isOn = 1, the config saved to bdrConfig, repaint
    // (document.backdrop.Changed).
    void RenderBackdrop(Core::Document& document);
    // turnOff: isOn = 0, both radii 0, inset -1; bdrConfig is not written.
    void TurnOffBackdrop(Core::Document& document);
    // loadFile: decodes config.filepath off the UI thread (the
    // desktop wallpaper when it fails or is "wallpaper"), then image mode with the average colour as
    // palette, and renders.
    void LoadBackdropImage(std::shared_ptr<Core::Document> document);
}
