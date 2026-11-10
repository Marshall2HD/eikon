# Eikon architecture

Eikon is a screenshot tool for Windows 11: area, window, fullscreen and scrolling capture, an
annotation editor, a floating preview thumbnail, pinned images, OCR, a pixel ruler and colour picker,
backdrops, and a screenshot Library. It is written in C++20 with C++/WinRT on WinUI 3 (Windows App
SDK), uses Win2D for every canvas and Win32 + Direct2D for the capture overlays, and ships as an MSIX
package. See [building.md](building.md) for how to build and test it.

## Design decisions

1. **Keyboard conventions.** Ctrl is the command modifier and Alt the option modifier inside the app.
   In the editor, Ctrl-drag reverses a new arrow and turns snapping off and Alt-drag smart-adjusts the
   crop, so dragging the image out of the editor is Ctrl+Alt-drag (or the toolbar's drag button).
2. **Global hotkeys:** fullscreen Ctrl+Shift+1, area Ctrl+Shift+2, OCR Ctrl+Alt+Shift+O (Win+Shift+digit
   belongs to the taskbar). A hotkey that another app already holds fails to register silently; the
   Hotkeys pane shows which one failed. The pane's "Reassign Default" section offers taking Print Screen
   from the Snipping Tool.
3. **Area selection always uses Eikon's own selector.** Windows has no system picker that returns a
   rectangle, so `RegionSelector` handles area, window ("Capture Any Window" highlights windows) and
   repeat capture. The `areaCustomGrabber`/`scrollingCustomGrabber` settings are kept for compatibility
   of stored settings but have no effect.
4. **No accounts, telemetry or update check.** Every feature is available. Uploads go only to an
   S3-compatible bucket the user configures. The Library is off until it is enabled
   (`eikon://settings/library/enable`).
5. **URL scheme:** `eikon://` commands (`grab/area`, `grab/fullscreen`, `load/clipboard`, `show`,
   `settings/…`, …) are parsed by `Core/UrlCommand` and routed by `AppController`.
6. **Scale factors:** Eikon uses each monitor's real scale (1.25, 1.5, …) for every device-pixel
   calculation, including the ruler's px/pt readout. Saved image DPI is 96 × scale.
7. **Wheel scrolling:** Windows wheel events scroll by lines, so scrolling capture places each strip by
   its measured shift instead of assuming a fixed step.
8. **Fonts:** Segoe UI Variable for UI text; the "goofy" text style uses a bundled handwriting font.
9. **Clipboard:** images are written as both `PNG` and `CF_DIBV5`, since most Windows apps read a DIB.
10. **Icons and cursors.** Tool and UI icons are [Lucide](https://lucide.dev) icons
    (`third_party/lucide`, ISC), rendered by `tools/generate_icons.py` to fixed sizes and tints; the
    images Lucide has no icon for (zoom and pen cursors, pixel-tool helper sheets, window-background
    previews, Library file badges, the drag-out file) are drawn by the same script. The move, resize
    and pointer cursors are Windows' own (`IDC_SIZEALL`, `IDC_SIZEWE`, …), so they follow the user's
    pointer scheme, size and colour; zoom and pen are drawn at the standard cursor size for the
    display's scale, and Windows enlarges them with the pointer size setting as it does its own.
11. **Cut Out** lifts the selection's subject with Windows AI's `ImageObjectExtractor` where the device
    supports it, otherwise with the bundled U²-Net-p model on the Windows App SDK's ONNX Runtime, and
    as a last resort clears the selection's uniform border colour (`Core/CutOut`, `Platform/SubjectMask`).
12. **Library storage.** The Library index is a JSON-lines file next to the screenshots: one JSON
    object per line. Lines that are not JSON objects are skipped.

## Design tokens ("Candy")

Tokens live in `Themes/Candy.xaml` (brushes, radii, button, icon-button and card styles; WinUI's accent
colours are overridden in `App.xaml`) and in `Platform/Candy.h` (the same values for Direct2D
surfaces). Keep the two in step.

- Soft radial colour fields appear only on the welcome panel and the first-capture card. Working
  surfaces stay neutral, with lilac selection (#ece4ff / accent #7b4bff in light, #3a2a6e / #a98bff in
  dark), ink primary buttons and tooltips, and display-weight headings.
- No capsules: radius 8 for buttons, inputs, tooltips and labels (6 for inner segments), 10–12 for
  tiles, cards and rows, 16 for floating windows and popovers.
- High-contrast themes use system colours and no gradients.
- The capture overlay stays minimal: dimming and a crosshair, no size label or handles.

## Layout

```
Eikon.sln
src/Eikon/
  App.xaml, Main.cpp            entry point, single instance, protocol/launch activation
  AppController.h/.cpp          owns the services and routes every action
  *.idl/*.xaml                  WinRT classes and XAML (must stay in the project root): EditorWindow,
                                SettingsWindow + pages, BackdropPanel, LibraryWindow, controls
  PreviewWindow, PinWindow      the floating thumbnail and pinned images
  Themes/                       Candy.xaml (design tokens), Settings.xaml
  Core/        platform-neutral, unit-tested
    Settings                    every setting key with its default; typed getters write the default on first read
    UrlCommand                  eikon:// parser
    Geometry, Framer            Rect/Point, rounding, editor framing maths
    Document, Undo              the open image: bitmap, crop, objects, backdrop, counter; undo snapshots
    Objects/                    ProtoObject and every annotation class, with JSON Dump/Load
    Color, Contrast             palettes, hex, luminance, colour formats, OKLCH, WCAG 2, APCA
    Ruler                       edge scout for the pixel ruler
    Stitch                      scroll matchers and compositing
    Backdrop, Noise             backdrop config, templates, layout maths, grain and mesh maps
    FileName                    %Y%m%d-%t file naming
    Library, LibraryLayout      Library index lines and gallery layout
    S3                          SigV4 signing and presigning
    CutOut                      mask post-processing and the border-colour fallback for Cut Out
    Snake                       the Snake game on the welcome panel's logo
    Confetti                    the status icon's success puff, simulated
  Platform/    Win32/WinRT integration
    TrayIcon, MessageWindow, HotKeyService, KeyboardHook
    ConfettiWindow              layered click-through window that draws Core/Confetti over the tray icon
    ScreenCapture               monitors, DPI, frozen per-monitor bitmaps (DXGI duplication / BitBlt),
                                window bounds via DWM
    RegionSelector              Win32 layered topmost overlay per monitor, Direct2D
    ScrollCapture, ScrollDriver scrolling capture: SendInput wheel, cursor parking, input stop hook
    FirstCaptureCard            the first area capture's "Where should captures go?" card
    Notifier                    custom notification window + AppNotifications
    Exporter, ImageCodec        WIC encode, clipboard, print, GIF
    FileDrag                    drag-out of the editor and the thumbnail
    Ocr                         Windows.Media.Ocr + ZXing-C++ QR codes
    SubjectMask                 Cut Out's subject mask (Windows AI, then U²-Net-p)
    LibraryIndex                the Library index and the screenshots folder watcher
    Uploader                    WinHTTP PUT using Core/S3
    Secrets                     DPAPI-encrypted dictionary (allowDeeplinks, s3access, s3secret)
  Editor/      the editor canvas (Win2D), owned by EditorWindow
    ShotView                    CanvasControl host: framing, zoom/pan, pointer and key routing
    Render*, Compose            draw image, objects, crop shade, handles, guides, ruler, backdrop;
                                compose the exported image
    CropTool, ObjectHandlers, CtrlHelper, Cursors, DragOut, TextEditOverlay
    PixelTools                  ruler, colour picker, contrast badge
tests/Eikon.Core.Tests/         console test runner for Core (built and run in CI)
tools/generate_assets.py        app icon, MSIX logos, .ico and brand images
tools/generate_icons.py         Lucide icons, cursors and drawings into src/Eikon/Assets
tools/merge_resw.py             merges conflicting Resources.resw edits
third_party/                    ZXing-C++ (QR), Lucide icon sources, U²-Net-p model; licences in each folder
```

## Third-party notices

Everything Eikon ships that it did not write, with its licence (the full text is in each folder):

| Component | Use | Licence | Where |
|---|---|---|---|
| [ZXing-C++](https://github.com/zxing-cpp/zxing-cpp) v3.1.1 | QR codes before OCR | Apache-2.0 (libzueci BSD-3-Clause) | `third_party/zxing-cpp` |
| [Lucide](https://lucide.dev) 1.70.0 | tool and UI icons, cursor glyphs | ISC (Feather-derived icons MIT) | `third_party/lucide` |
| [U²-Net-p](https://github.com/xuebinqin/U-2-Net) (ONNX export by [rembg](https://github.com/danielgatis/rembg), MIT) | Cut Out's subject model, embedded in Eikon.exe | Apache-2.0 | `third_party/u2net` |

Win2D, WebView2, WIL and the Windows App SDK (whose framework package provides the ONNX Runtime that runs
the model) come from NuGet under their own Microsoft licences.

## Contracts

- **Threading:** everything runs on the UI thread (the XAML dispatcher, which also pumps the hidden
  `MessageWindow`). Capture overlays are Win32 windows on the same thread. Background work (OCR,
  encoding, upload, stitching, Cut Out) uses coroutines and returns with `wil::resume_foreground`.
- **Images:** a captured image is `Core::Bitmap` (BGRA8 premultiplied, width, height, dpr, plus the
  screen rect it came from). Win2D wraps it with `CanvasBitmap::CreateFromBytes` when shown.
- **Coordinates:** Core and the objects use image pixels with y **down**. The editor converts between
  view points and device pixels with the window's rasterization scale.
- **Rendering:** `Core::ProtoObject` is the base of every annotation: id, class name, colour, size,
  rect/points, selection, `Dump()`/`Load()` to JSON, hit testing, handles and key handling. Drawing is
  `Editor::Render::Draw(session, object, context)` per class, so Core stays free of Win2D. The canvas and
  the exported image (`Editor::Compose`) use the same drawing code.
- **Actions:** every user action (menu, tray, hotkey, URL, toolbar) is a call on `AppController` with a
  mode string (`"fullscreen"`, `"interactive"`, `"window"`, `"interactive-window"`,
  `"interactive-repeat"`, `"delayed"`) and a source (`"menu"`, `"hotkey"`, `"url"`).
- **Settings:** `Core::Settings::Current()` has one typed property per key and a change event; values
  are stored in `ApplicationData.LocalSettings` under the key names in `Core/Settings.h`. Some key names
  are kept as they are for compatibility, typos included (`libPopoveerSize`).
- **Strings:** user-visible text lives in `Strings/en-US/Resources.resw` and is read with `Strings::Get`.

## Packaging

The app is a single-project MSIX (`src/Eikon/Package.appxmanifest`): a full-trust desktop app with the
`eikon` protocol, a startup task ("Run at Startup") and the Windows App SDK framework as a dependency.
Assets in subfolders of `Assets/` are `Image` items in `Eikon.vcxproj`, so they are packaged. The
U²-Net-p model is embedded in `Eikon.exe` as an `RCDATA` resource (`Eikon.rc`). CI
(`.github/workflows/build.yml`) builds x64 and ARM64 sideload packages signed with a throwaway
certificate and runs the Core tests.

## Test hooks

- **`capture-overlay-visible`** in the package's LocalState folder (`Core::DataFolder()`): while the
  file exists, `RegionSelector` does not set `WDA_EXCLUDEFROMCAPTURE` on its overlays, so a desktop
  screenshot taken with GDI can show the area selector as the user sees it. It is read when an overlay
  window is created. Remove it before a scrolling capture: that capture grabs live frames and relies on
  the exclusion to keep its own frame out of the result.
