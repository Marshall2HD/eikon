#pragma once

// Drag-out from the editor: CtrlDragOut, the toolbar control, and Ctrl+Alt + drag
// on the canvas drag the image out as a file. The drag is WinUI's own (UIElement::StartDragAsync):
// OLE DoDragDrop started from a WinUI pointer handler never sees the mouse move, while WinUI's drag
// does. DragStarting saves the composed image to the temporary folder (AppController::WriteDragOutFile)
// and puts that file in the DataPackage, Copy only, with the 44-ico-draggable-file drag image.
// The thumbnail and pins are plain Win32 windows and keep OLE (Platform/FileDrag).

namespace Eikon::Editor
{
    // Makes `element` a drag-out source: its DragStarting fills the drag with the saved file.
    void AttachDragOutSource(winrt::Microsoft::UI::Xaml::UIElement const& element);

    // Starts the drag from a source element (AttachDragOutSource) with the pressed pointer at `point`.
    void StartDragOut(winrt::Microsoft::UI::Xaml::UIElement const& element, winrt::Microsoft::UI::Input::PointerPoint const& point);

    // The toolbar control: a click without a drag does nothing; a drag past the system threshold starts it.
    void AttachDragOut(winrt::Microsoft::UI::Xaml::UIElement const& control);
}
