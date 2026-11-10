"""Renders Lucida's tool and UI icons from Lucide (third_party/lucide) into src/Eikon/Assets, and
draws the images Lucide has no icon for (cursors, helper sheets, window-background previews, file
badges, the drag-out file, the More arrow).

Run from the repository root:  uv run --with resvg-py==0.5.0 --with pillow python tools/generate_icons.py
The outputs are committed. tools/generate_assets.py does not write any of these files. Text in the
drawings uses Segoe UI where installed, else Arial, so outputs differ slightly by machine.

Each PNG keeps the file name and pixel size of the image it replaced, and that image's tint and
alpha (one colour plus alpha, so images the app tints at runtime stay template images):

* Lucide 1.70.0 (lucide-static), ISC: the SVGs used are vendored unmodified in third_party/lucide/icons
  (third_party/lucide/README.eikon.md).
* Stroke width 1.5 in Lucide's 24-unit viewBox, round caps and joins.
* `box` is the size in pixels the 24-unit viewBox is drawn at, centred in the image. 0.9 of the
  image's side matches the old glyphs' size for most images; smaller boxes keep the old glyph size
  where the old image left more room (Library tools, close buttons, the search lens).
* Variants: -dark (dark appearance) and -white (pin toolbar). The Library toolbar and empty-state glyphs are
  template images the app tints (Candy's light toolbar, accent empty states).
  The light-on-dark Preview images keep the old ones' 1 px drop shadow.
* The grip dots (preview-grip, the dragger's grip columns) are filled: at stroke 1.5 Lucide's r = 1
  circles would leave a pinhole in each dot.
"""
import io
import re
from pathlib import Path

import resvg_py
from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "src" / "Eikon" / "Assets"
LUCIDE = ROOT / "third_party" / "lucide" / "icons"
STROKE = 1.5

# Counter tool: a circle with a "1" (Lucide has no such icon).
COUNTER = '<circle cx="12" cy="12" r="10"/><path d="M10.5 9.5 12.5 8v8"/>'

# Tints (r, g, b, a), sampled from the opaque pixels of the images these replace.
EDITOR = (102, 102, 102, 255)        # editor toolbar, selection bar and options bar (#666)
EDITOR_OBJ = (103, 103, 103, 255)    # fill, shape and transparency options
EDITOR_STEP = (39, 41, 42, 255)      # 16-minus / 16-plus
BACKDROP = (65, 65, 65, 255)         # ico-20-randomize / ico-20-revert
MENU = (24, 24, 24, 255)             # tray menu (template)
BLACK = (0, 0, 0, 255)               # Library window, Settings, light thumbnail toolbar
PREVIEW_DARK = (238, 238, 238, 220)  # thumbnail toolbar, dark; pin toolbar (-white)

E20, E16, E16L, M16, L16, P16 = 36, 26.6, 28.8, 28.8, 27, 28.8

# (asset path without .png, Lucide icon, box in px, [(suffix, tint, effect), ...])
LIGHT = [("", EDITOR, None)]
ICONS = [
    # Editor toolbar
    ("Editor/20-copy-lite", "copy", E20, LIGHT), ("Editor/20-save-lite", "save", E20, LIGHT),
    ("Editor/20-pin-lite-sf", "pin", E20, LIGHT), ("Editor/20-upload-lite", "cloud-upload", E20, LIGHT),
    ("Editor/20-arrow-sf", "arrow-up-right", E20, LIGHT), ("Editor/20-text-sf", "type", E20, LIGHT),
    ("Editor/20-measure-lite-sf", "ruler", E20, LIGHT), ("Editor/20-rectangle-sf", "square", E20, LIGHT),
    ("Editor/20-oval-sf", "circle", E20, LIGHT), ("Editor/20-line-sf", "slash", E20, LIGHT),
    ("Editor/20-pen", "pencil", E20, LIGHT), ("Editor/20-highlighter", "highlighter", E20, LIGHT),
    ("Editor/20-blur-lite-sf", "droplet", E20, LIGHT), ("Editor/20-counter-lite-sf", ("custom", COUNTER), E20, LIGHT),
    ("Editor/20-lens-sf", "search", E20, LIGHT), ("Editor/20-spotlight", "focus", E20, LIGHT),
    ("Editor/20-backdrop", "frame", E20, LIGHT), ("Editor/20-image", "image", E20, LIGHT),
    ("Editor/20-appendcapture", "image-plus", E20, LIGHT), ("Editor/20-more", "ellipsis", E20, LIGHT),
    ("Editor/20-back-lite-sf", "chevron-left", E20, LIGHT),
    # Selection bar
    ("Editor/20-adjust-lite", "wand-sparkles", E20, LIGHT), ("Editor/20-resize", "scaling", E20, LIGHT),
    ("Editor/20-ocr-sf", "scan-text", E20, LIGHT), ("Editor/20-selectionsize-lite-sf", "ruler-dimension-line", E20, LIGHT),
    ("Editor/20-erase-lite-sf", "eraser", E20, LIGHT), ("Editor/20-scissors", "scissors", E20, LIGHT),
    ("Editor/20-reset-lite", "circle-x", E20, LIGHT),
    # Options bar: 28 px images; toggle, transparency, gif, minus and plus are 32 px (E16L)
    ("Editor/16-arrow-line", "arrow-right", E16, LIGHT), ("Editor/16-arrow-shape", "arrow-big-right", E16, LIGHT),
    ("Editor/16-arrow-double", "move-horizontal", E16, LIGHT), ("Editor/16-arrow-handdrawn", "spline", E16, LIGHT),
    ("Editor/16-line", "minus", E16, LIGHT), ("Editor/16-dash", "ellipsis", E16, LIGHT),
    ("Editor/16-objfill-transparent", "square", E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-objfill-translucent", ("fill", "square", 0.35), E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-objfill-opaque", ("fill", "square", 1.0), E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-objfill-handdrawn", "signature", E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-shape-rect", "square", E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-shape-oval", "circle", E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-shape-blob", "squircle", E16, [("", EDITOR_OBJ, None)]),
    ("Editor/16-text-sans", "type", E16, LIGHT), ("Editor/16-text-goofy", "pen-tool", E16, LIGHT),
    ("Editor/16-toggle", "chevrons-up-down", E16L, LIGHT),
    ("Editor/16-transparency", "blend", E16L, [("", EDITOR_OBJ, None)]),
    ("Editor/16-gif", "image-play", E16L, LIGHT),
    ("Editor/16-minus", "minus", E16L, [("", EDITOR_STEP, None)]), ("Editor/16-plus", "plus", E16L, [("", EDITOR_STEP, None)]),
    # Backdrop panel
    ("ico-20-randomize", "shuffle", 29, [("", BACKDROP, None)]),
    ("ico-20-revert", "arrow-left-right", 29, [("", BACKDROP, None)]),
    # Tray menu
    ("Menu/16-menu-area", "square-dashed", M16, [("", MENU, None)]),
    ("Menu/16-menu-screen", "monitor", M16, [("", MENU, None)]),
    ("Menu/16-menu-window", "app-window", M16, [("", MENU, None)]),
    ("Menu/16-menu-scrolling", "chevrons-down", M16, [("", MENU, None)]),
    ("Menu/16-menu-scrolling-up", "chevrons-up", M16, [("", MENU, None)]),
    ("Menu/16-menu-ocr", "scan-text", M16, [("", MENU, None)]),
    ("Menu/16-menu-timer", "timer", M16, [("", MENU, None)]),
    ("Menu/16-menu-clipboard", "clipboard-list", M16, [("", MENU, None)]),
    ("Menu/16-menu-file", "file", M16, [("", MENU, None)]),
    ("Menu/16-menu-more", "ellipsis", M16, [("", MENU, None)]),
    # Library window
    # Candy's light toolbar: template glyphs the app tints (ink 2), no highlighted variants.
    *[(f"Library/lib-tool-28-{name}", icon, 38, [("", BLACK, None)])
      for name, icon in (("area", "square-dashed"), ("fullscreen", "monitor"), ("window", "app-window"),
                         ("repeat", "rotate-ccw-square"), ("scrolldown", "chevrons-down"), ("scrollup", "chevrons-up"),
                         ("ocr", "scan-text"), ("clipboard", "clipboard-list"), ("more", "ellipsis"))],
    ("Library/16-lib-layout-large", "rows-2", L16, [("", BLACK, None)]),
    ("Library/16-lib-layout-medium", "layout-grid", L16, [("", BLACK, None)]),
    ("Library/16-lib-layout-small", "grid-3x3", L16, [("", BLACK, None)]),
    ("Library/16-lib-layout-text", "menu", L16, [("", BLACK, None)]),
    ("Library/16-expand", "maximize-2", L16, [("", BLACK, None)]),
    ("Library/16-collapse", "minimize-2", L16, [("", BLACK, None)]),
    ("Library/16-settings", "settings", L16, [("", BLACK, None)]),
    ("Library/20-searchbar-lens", "search", 30, [("", (0, 0, 0, 128), ("offset", 4.5, 2.5))]),
    # Settings sidebar
    ("Prefs/30-pref-general", "settings", 52, [("", BLACK, None)]),
    ("Prefs/30-pref-hotkeys", "keyboard", 52, [("", BLACK, None)]),
    ("Prefs/30-pref-upload", "cloud-upload", 52, [("", BLACK, None)]),
    ("Prefs/30-pref-advanced", "microscope", 52, [("", BLACK, None)]),
    # Thumbnail and pin toolbars, notifications
    *[(f"Preview/ico-16-{name}-light", icon, P16, [("", BLACK, None), ("-dark", PREVIEW_DARK, "shadow")])
      for name, icon in (("copy", "copy"), ("save", "save"), ("pin", "pin"), ("edit", "pencil"), ("ocr", "scan-text"),
                         ("upload", "cloud-upload"))],
    ("Preview/ico-16-copy-white", "copy", P16, [("", PREVIEW_DARK, "shadow")]),
    ("Preview/ico-16-edit-white", "pencil", P16, [("", PREVIEW_DARK, "shadow")]),
    ("Preview/ico-16-close", "x", 24, [("", (0, 0, 0, 153), None), ("-dark", (255, 255, 255, 179), None)]),
    ("Preview/8-close", "x", 20, [("", (91, 0, 0, 204), None)]),
    ("Preview/preview-grip", ("fill", "grip-horizontal", 1.0), 38, [("", (0, 0, 0, 89), None), ("-dark", (255, 255, 255, 102), None)]),
    ("Preview/24-notification-warning", "triangle-alert", 50,
     [("", (40, 53, 64, 255), None), ("-dark", (255, 255, 255, 204), None)]),
]


def inner(spec):
    """The SVG elements of an icon spec, and the fill they need."""
    if isinstance(spec, tuple) and spec[0] == "custom":
        return spec[1], "none"
    if isinstance(spec, tuple) and spec[0] == "fill":
        name, opacity = spec[1], spec[2]
        return inner(name)[0], f'#fff" fill-opacity="{opacity:g}'
    text = (LUCIDE / f"{spec}.svg").read_text()
    body = re.search(r"<svg[^>]*>(.*)</svg>", text, re.S).group(1)
    return re.sub(r"\s+", " ", body).strip(), "none"


def mask(spec, size, box, offset=(0.0, 0.0)):
    """The icon's coverage (L) in a size = (w, h) image, its 24-unit viewBox drawn at `box` px, centred."""
    w, h = size
    body, fill = inner(spec)
    k = box / 24
    x = (w - box) / 2 + offset[0]
    y = (h - box) / 2 + offset[1]
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
           f'<g transform="translate({x:g} {y:g}) scale({k:g})" fill="{fill}" stroke="#fff" stroke-width="{STROKE}" '
           f'stroke-linecap="round" stroke-linejoin="round">{body}</g></svg>')
    png = resvg_py.svg_to_bytes(svg_string=svg)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA").getchannel("A")


def tinted(alpha, tint):
    out = Image.new("RGBA", alpha.size, tint[:3] + (0,))
    out.putalpha(alpha.point(lambda a: a * tint[3] // 255))
    return out


def under(layer, colour, alpha):
    base = Image.new("RGBA", layer.size, colour + (0,))
    base.putalpha(alpha)
    base.alpha_composite(layer)
    return base


def render(spec, size, box, tint, effect):
    if isinstance(effect, tuple) and effect[0] == "offset":
        return tinted(mask(spec, size, box, effect[1:]), tint)
    coverage = mask(spec, size, box)
    out = tinted(coverage, tint)
    if effect == "shadow":
        # Light glyphs over images: a 1 px drop shadow, black α 0.25.
        shadow = ImageChops.offset(coverage, 0, 1).filter(ImageFilter.GaussianBlur(0.6)).point(lambda a: a * 64 // 255)
        out = under(out, (0, 0, 0), shadow)
    return out


def dragger():
    """Editor/20-dragger (64 x 40): file-image between two grip columns at α 0.35, as the old image.
    A template image: the editor shows it as a monochrome BitmapIcon in the toolbar's foreground."""
    size = (64, 40)
    out = tinted(mask("file-image", size, 36), EDITOR)
    dots = Image.new("L", size, 0)
    for dx in (-25, 23):
        dots = ImageChops.lighter(dots, mask(("fill", "grip-vertical", 1.0), size, 34, (dx, 0)))
    return under(out, EDITOR[:3], dots.point(lambda a: a * 90 // 255))


# ---- Redraws of the images Lucide has no icon for (cursors, helper sheets, window-background previews,
# file badges, the drag-out file, the More arrow). Neutral drafts that may be restyled later. ----

FONT = "Segoe UI, Arial, Helvetica, sans-serif"  # Segoe UI where it exists (Windows), else Arial


def lucide_body(name):
    return inner(name)[0]


def svg_image(svg, size):
    """Renders a complete SVG document to an RGBA image of `size`."""
    png = resvg_py.svg_to_bytes(svg_string=svg, width=size[0], height=size[1])
    image = Image.open(io.BytesIO(bytes(png))).convert("RGBA")
    if image.size != size:
        raise SystemExit(f"rendered {image.size}, expected {size}")
    return image


def doc(view_w, view_h, body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{view_w}" height="{view_h}" viewBox="0 0 {view_w} {view_h}">{body}</svg>'


# -- Cursors (Assets/Cursors/*.png; ) --
# Only the two cursors Windows has no shape for; move, the resize arrows and the pointer are the system's
# (Editor/Cursors). A Lucide glyph with a white halo (white stroke 5 under a #111 stroke 2) in a 28-unit
# box around the 24-unit viewBox, and a soft drop shadow (0, 1 unit, blur 0.75 unit, black α 0.35), in
# a 32-unit Windows cursor box. Written as 256 px masters; Editor/Cursors scales them to the standard
# cursor size at the display's scale (Windows applies the pointer size setting) and sets the hotspot, which
# it keeps as constants in box units: keep them in step with the values printed here.
# name, Lucide icon, glyph box in units, hotspot in viewBox units (None: the centre).
CURSORS = [
    ("cursor-zoom", "zoom-in", 20, None),
    ("cursor-pen", "pencil", 23, (2.3, 21.7)),  # the tip
]
CURSOR_BOX = 32  # Windows' cursor box at 100 %
CURSOR_MASTER = 256


def cursor_master(icon, glyph_units, hotspot):
    scale = CURSOR_MASTER / CURSOR_BOX
    origin = (CURSOR_BOX - glyph_units) / 2
    k = glyph_units / 28 * scale  # px per viewBox unit
    ox = oy = origin * scale + 2 * k  # where viewBox (0, 0) lands
    body = lucide_body(icon)
    svg = doc(CURSOR_MASTER, CURSOR_MASTER,
              f'<g transform="translate({ox:g} {oy:g}) scale({k:g})" stroke-linecap="round" stroke-linejoin="round">'
              f'<g stroke="#fff" stroke-width="5" fill="none">{body}</g>'
              f'<g stroke="#111" stroke-width="2" fill="none">{body}</g></g>')
    glyph = svg_image(svg, (CURSOR_MASTER, CURSOR_MASTER))
    shadow = ImageChops.offset(glyph.getchannel("A"), 0, round(scale)).filter(ImageFilter.GaussianBlur(0.75 * scale))
    image = under(glyph, (0, 0, 0), shadow.point(lambda a: a * 89 // 255))
    hx, hy = hotspot if hotspot is not None else (12, 12)
    return image, ((ox + hx * k) / scale, (oy + hy * k) / scale)


def cursors():
    for name, icon, glyph_units, hotspot in CURSORS:
        image, (hx, hy) = cursor_master(icon, glyph_units, hotspot)
        image.save(ASSETS / "Cursors" / f"{name}.png")
        print(f"{name}: hotspot ({hx:.2f}, {hy:.2f}) of {CURSOR_BOX}")
    return len(CURSORS)


# -- Pixel-tool helper sheets (Editor/helper-*). helper-zoom is live XAML. --
# The pointer drawn in the sheets, at its tip.
PTR = ('<path d="M0 0 L0 15 L4 11.5 L7 18 L9.5 17 L6.5 10.5 L11.5 10.5 Z" fill="#111" stroke="#fff" '
       'stroke-width="1.4" stroke-linejoin="round"/>')


def helper_pixel(dark):
    cells = ["#dfeee9", "#c6e4da", "#a9d6c6", "#e8f2ee"]
    if dark:
        cells = ["#2f4a42", "#3b5d52", "#4b7465", "#28403a"]
    grid = "".join(f'<rect x="{12 + c * 11}" y="{6 + r * 11}" width="10" height="10" rx="1.5" fill="{cells[(r * 5 + c) % 4]}"/>'
                   for r in range(5) for c in range(6))
    return (grid + '<rect x="33" y="28" width="10" height="10" rx="1.5" fill="none" stroke="#ff3b1f" stroke-width="1.6"/>'
            f'<g transform="translate(40 34)">{PTR}</g>')


def helper_average(dark):
    stops = ("#2f5f93", "#4f7fb3") if dark else ("#9cc3ea", "#cfe2f6")
    return (f'<defs><linearGradient id="g1"><stop offset="0" stop-color="{stops[0]}"/><stop offset="1" stop-color="{stops[1]}"/></linearGradient></defs>'
            '<rect x="6" y="8" width="78" height="48" rx="6" fill="url(#g1)"/>'
            '<rect x="14" y="20" width="56" height="20" rx="2" fill="#ffffff55" stroke="#fff" stroke-width="1.4" stroke-dasharray="3 2"/>'
            f'<g transform="translate(66 36)">{PTR}</g>')


def helper_text(dark):
    ink, paper, edge = ("#e6e6e6", "#2b2b2b", "#666") if dark else ("#333", "#fff", "#ccc")
    return (f'<text x="10" y="46" font-size="38" font-weight="700" fill="{ink}" font-family="{FONT}">Ab</text>'
            '<rect x="20" y="22" width="9" height="9" rx="1.5" fill="none" stroke="#ff3b1f" stroke-width="1.6"/>'
            f'<circle cx="70" cy="22" r="7" fill="{ink}"/><circle cx="70" cy="44" r="7" fill="{paper}" stroke="{edge}"/>'
            f'<g transform="translate(26 30)">{PTR}</g>')


def helper_ruler(dark):
    return ('<rect x="20" y="20" width="120" height="40" rx="8" fill="#4b8ef0"/>'
            f'<text x="80" y="45" text-anchor="middle" font-size="14" fill="#fff" font-family="{FONT}">Button</text>'
            '<path d="M60 8 V72" stroke="#ff3b1f" stroke-width="1.2"/><path d="M56 20 h8 M56 60 h8" stroke="#ff3b1f" stroke-width="1.2"/>'
            '<rect x="30" y="34" width="28" height="13" rx="3" fill="#ff3b1f"/>'
            f'<text x="44" y="44" text-anchor="middle" font-size="8.5" font-weight="700" fill="#fff" font-family="{FONT}">40px</text>'
            f'<g transform="translate(60 26)">{PTR}</g>')


# -- Window-background previews (Prefs/pref-window-*): the top-right corner of a
# Windows 11 window on each background. --
def window_preview(mode, dark):
    window, line1, line2, label, glyphs, edge = (
        ("#2b2b2b", "#4a4a4a", "#404040", "#bbb", "#bbb", "#ffffff1f") if dark
        else ("#fff", "#d8d8d8", "#e4e4e4", "#555", "#555", "#0000001f"))
    defs = ('<defs><pattern id="ck" width="8" height="8" patternUnits="userSpaceOnUse">'
            f'<rect width="8" height="8" fill="{"#2b2b2b" if dark else "#fff"}"/>'
            f'<rect width="4" height="4" fill="{"#3a3a3a" if dark else "#e6e6e6"}"/>'
            f'<rect x="4" y="4" width="4" height="4" fill="{"#3a3a3a" if dark else "#e6e6e6"}"/></pattern>'
            '<filter id="blur" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="3"/></filter>'
            '<radialGradient id="bloom" cx="70%" cy="80%" r="90%">'
            + ('<stop offset="0" stop-color="#0b3d91"/><stop offset=".5" stop-color="#1d5bb8"/><stop offset="1" stop-color="#0d1b33"/>' if dark
               else '<stop offset="0" stop-color="#0a5bd8"/><stop offset=".5" stop-color="#5aa2f2"/><stop offset="1" stop-color="#c9e2fb"/>')
            + '</radialGradient></defs>')
    fill = {"transparent": "url(#ck)", "trimmed": "#202020" if dark else "#f3f3f3", "wallpaper": "url(#bloom)",
            "solid": "#7a4f3c" if dark else "#ffd9c4"}[mode]
    trimmed = mode == "trimmed"
    shadow = '' if trimmed else f'<rect x="-1" y="13" width="80" height="54" rx="6" fill="#000" opacity="{".4" if dark else ".12"}" filter="url(#blur)"/>'
    buttons = (f'<g stroke="{glyphs}" stroke-width=".9" fill="none"><path d="M55 8.5h4"/><rect x="63" y="6.5" width="4" height="4"/>'
               '<path d="M71 6.5l4 4M75 6.5l-4 4"/></g>')
    return (f'{defs}<rect width="85" height="60" fill="{fill}"/>{shadow}'
            f'<g transform="translate({"5 0" if trimmed else "-4 9"})"><rect width="80" height="54" rx="5" fill="{window}" stroke="{edge}"/>'
            f'<rect x="12" y="22" width="40" height="3" rx="1.5" fill="{line1}"/><rect x="12" y="30" width="56" height="3" rx="1.5" fill="{line2}"/>'
            f'<rect x="12" y="38" width="30" height="3" rx="1.5" fill="{line2}"/>'
            f'<text x="12" y="10.5" font-size="5.5" fill="{label}" font-family="{FONT}">App</text>{buttons}</g>')


# -- Files and small marks --
def file_badge(label, colour, outline="#555"):
    """Library/20-lib-file-*: a Lucide file with a label band (20 pt); -dark: a light outline."""
    return (f'<g fill="none" stroke="{outline}" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round">{lucide_body("file")}'
            f'<rect x="3.5" y="12.5" width="13" height="6" rx="1.5" fill="{colour}" stroke="none"/>'
            f'<text x="10" y="17.3" text-anchor="middle" font-size="4.6" font-weight="700" fill="#fff" stroke="none" '
            f'font-family="{FONT}">{label}</text></g>')


def draggable_file():
    """44-ico-draggable-file: Lucide file-image, #555 on a white page (22 pt)."""
    page, *rest = re.findall(r"<[^>]+/>", lucide_body("file-image"))
    page = page.replace("<path ", '<path fill="#fff" ', 1)
    return (f'<g fill="none" stroke="#555" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round">'
            f'{page}{"".join(rest)}</g>')


def illustrations():
    images = {}
    for name, draw, view in (("helper-color-pixel", helper_pixel, (90, 64)), ("helper-color-average", helper_average, (90, 64)),
                             ("helper-color-text", helper_text, (90, 64)), ("helper-ruler", helper_ruler, (190, 80))):
        for suffix, dark in (("", False), ("-dark", True)):
            images[f"Editor/{name}{suffix}"] = (doc(*view, draw(dark)), (view[0] * 2, view[1] * 2))
    for mode in ("transparent", "trimmed", "wallpaper", "solid"):
        for suffix, dark in (("", False), ("-dark", True)):
            images[f"Prefs/pref-window-{mode}{suffix}"] = (doc(85, 60, window_preview(mode, dark)), (170, 120))
    for suffix, outline in (("", "#555"), ("-dark", "#c4c4c4")):
        images[f"Library/20-lib-file-jpeg{suffix}"] = (doc(24, 24, file_badge("JPG", "#3d9b5f", outline)), (40, 40))
        images[f"Library/20-lib-file-png{suffix}"] = (doc(24, 24, file_badge("PNG", "#e0405e", outline)), (40, 40))
    for path in ("44-ico-draggable-file", "Preview/44-ico-draggable-file"):
        images[path] = (doc(24, 24, draggable_file()), (44, 44))
    for path, (svg, size) in images.items():
        svg_image(svg, size).save(ASSETS / f"{path}.png")
    # The More button's arrow (Editor/20-more-overlay, template): a 12 pt chevron-down at the old
    # glyph's place, the bottom right of the 20 pt image.
    tinted(mask("chevron-down", (40, 40), 24, (14, 14)), EDITOR).save(ASSETS / "Editor" / "20-more-overlay.png")
    # Zoom helper glyphs (Editor/18-zoomhelp-*, template, shown as monochrome BitmapIcons).
    for name, icon in (("mouse", "mouse"), ("hand", "hand")):
        tinted(mask(icon, (36, 36), 36), EDITOR).save(ASSETS / "Editor" / f"18-zoomhelp-{name}.png")
    return len(images) + 3


def pin_dragger():
    """Preview/ico-16-drag-white (56 x 32, the pin toolbar's dragger): file-image between two Lucide
    grip-vertical columns at half the glyph's alpha, light with the Preview images' drop shadow."""
    size = (56, 32)
    glyph = mask("file-image", size, P16)
    dots = Image.new("L", size, 0)
    for dx in (-20, 20):
        dots = ImageChops.lighter(dots, mask(("fill", "grip-vertical", 1.0), size, 28, (dx, 0)))
    coverage = ImageChops.lighter(glyph, dots.point(lambda a: a // 2))
    out = tinted(coverage, PREVIEW_DARK)
    shadow = ImageChops.offset(coverage, 0, 1).filter(ImageFilter.GaussianBlur(0.6)).point(lambda a: a * 64 // 255)
    return under(out, (0, 0, 0), shadow)


# Candy's Library empty states: one 40-pt Lucide glyph each, a template image the app
# tints with the accent. New files, so their size is given here (80 px = 40 pt @2x).
EMPTY_STATES = [("folder", "folder-open"), ("images", "images"), ("notfound", "search-x"), ("pinned", "pin")]


def main():
    count = 0
    for name, icon in EMPTY_STATES:
        render(icon, (80, 80), 76, BLACK, None).save(ASSETS / "Library" / f"40-empty-{name}.png")
        count += 1
    for path, spec, box, variants in ICONS:
        size = Image.open(ASSETS / f"{path}{variants[0][0]}.png").size
        for suffix, tint, effect in variants:
            target = ASSETS / f"{path}{suffix}.png"
            if Image.open(target).size != size:
                raise SystemExit(f"{target}: size differs from {size}")
            render(spec, size, box, tint, effect).save(target)
            count += 1
    dragger().save(ASSETS / "Editor" / "20-dragger.png")
    pin_dragger().save(ASSETS / "Preview" / "ico-16-drag-white.png")
    drawn = illustrations()
    print(f"{count + 2} icons, {drawn} drawings, {cursors()} cursors")


if __name__ == "__main__":
    main()
