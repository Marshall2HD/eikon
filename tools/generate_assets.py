"""Generates Eikon's MSIX logo assets, the app .ico and the brand images.

Run from the repository root:  uv run --with resvg-py==0.5.0 --with pillow python tools/generate_assets.py

* The app icon is Eikon's Candy mark: a gradient squircle (#FF5A36 -> #B14BFF at 135 degrees under a peach
  highlight top-left and a violet one bottom-right) with Lucide's white "scan" viewfinder and a centre dot,
  rendered with resvg. It is every MSIX logo asset, the .ico, docs/logo.png and the notification logo.
* The brand images (the status icon eikon-22, the tray menu's 16-menu-eikon, the Library's
  lib-tool-28-eikon(-hl) and the notification logo 24-notification-logo(-dark)) are drawn with
  Eikon's mark (brand_images), and so are the status icon's success frame eikon-22-ok (the plate with
  a check cut out) and its capture-in-progress frames eikon-22-animated-1..5 (marching ants around
  the plate's outline) (tray_frames). tools/generate_icons.py draws every other tool and UI image
  (Lucide icons and drawings).
* The empty editor's welcome panel has no background image: its colour fields are XAML brushes
  (EditorWindow.xaml).
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "src" / "Eikon" / "Assets"
S = 1024  # master canvas size


def app_icon_svg(side):
    """The Candy app icon as SVG, `side` px square: a squircle (radius 0.27 of its side)
    filling 0.92 of the canvas, the white Lucide scan corners (stroke 2 in a 24-unit box of 0.54 of the side)
    and a white dot of 0.16 of the side."""
    plate = side * 0.92
    o = (side - plate) / 2
    r = plate * 26 / 96
    scan = (ROOT / "third_party" / "lucide" / "icons" / "scan.svg").read_text()
    paths = "".join(f'<path d="{d}"/>' for d in __import__("re").findall(r'd="([^"]+)"', scan))
    glyph = plate * 52 / 96
    g = (side - glyph) / 2
    dot = plate * 0.16 / 2
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{side}" height="{side}" viewBox="0 0 {side} {side}">
  <defs>
    <linearGradient id="lin" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#ff5a36"/><stop offset="1" stop-color="#b14bff"/></linearGradient>
    <radialGradient id="peach" cx="0.2" cy="0.15" r="0.8" gradientUnits="objectBoundingBox">
      <stop offset="0" stop-color="#ffb08f" stop-opacity="1"/><stop offset="0.6" stop-color="#ffb08f" stop-opacity="0"/></radialGradient>
    <radialGradient id="violet" cx="0.9" cy="0.9" r="0.8" gradientUnits="objectBoundingBox">
      <stop offset="0" stop-color="#9b6bff" stop-opacity="1"/><stop offset="0.6" stop-color="#9b6bff" stop-opacity="0"/></radialGradient>
    <linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.4"/><stop offset="0.04" stop-color="#ffffff" stop-opacity="0"/></linearGradient>
  </defs>
  <rect x="{o}" y="{o}" width="{plate}" height="{plate}" rx="{r}" fill="url(#lin)"/>
  <rect x="{o}" y="{o}" width="{plate}" height="{plate}" rx="{r}" fill="url(#peach)"/>
  <rect x="{o}" y="{o}" width="{plate}" height="{plate}" rx="{r}" fill="url(#violet)"/>
  <rect x="{o}" y="{o}" width="{plate}" height="{plate}" rx="{r}" fill="url(#rim)"/>
  <g transform="translate({g} {g}) scale({glyph / 24})" fill="none" stroke="#ffffff" stroke-width="2"
     stroke-linecap="round" stroke-linejoin="round">{paths}</g>
  <circle cx="{side / 2}" cy="{side / 2}" r="{dot}" fill="#ffffff"/>
</svg>"""


def glyph(size):
    """The app icon, size x size."""
    import io
    import resvg_py
    png = resvg_py.svg_to_bytes(svg_string=app_icon_svg(1024), width=size, height=size)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA")


def app_icon(w, h, scale_fill):
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    side = int(min(w, h) * scale_fill)
    g = glyph(side)
    canvas.paste(g, ((w - side) // 2, (h - side) // 2), g)
    return canvas


def sorted_box(x0, y0, x1, y1):
    return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]


def save_png(name, w, h, scale_fill=0.8):
    app_icon(w, h, scale_fill).save(ASSETS / name)


def mark(size, box, glyph_rgba, plate_rgba=None, radius_ratio=220 / 944, glow=None, bar=None):
    """Eikon's mark drawn into `box` (x0, y0, x1, y1, pixels) of a size x size image.

    With `plate_rgba`, a rounded square in that colour with the viewfinder corners and the dot cut
    out (a template plate); without it, only the corners and the dot in `glyph_rgba`.
    `glow` = (radius, alpha) adds a soft halo under the glyph (the highlighted Library tools).
    `bar` (output pixels) thins the corners and the dot to 2 px outline glyphs like the other UI icons.
    """
    from PIL import ImageFilter
    k = 16  # supersampling
    x0, y0, x1, y1 = (v * k for v in box)
    side = x1 - x0
    shape = Image.new("L", (size * k, size * k), 0)
    d = ImageDraw.Draw(shape)
    # Proportions of glyph(): plate 944, corners 190 in from the plate edge, arms 170, bars 64, dot 92.
    u = side / 944
    inset, arm, width, dot = 190 * u, 170 * u, 64 * u, 92 * u
    if bar is not None:
        dot *= 1.5 * bar * k / width
        width = bar * k
    lo_x, lo_y, hi_x, hi_y = x0 + inset, y0 + inset, x1 - inset, y1 - inset
    for cx, cy, dx, dy in ((lo_x, lo_y, 1, 1), (hi_x, lo_y, -1, 1), (lo_x, hi_y, 1, -1), (hi_x, hi_y, -1, -1)):
        d.rounded_rectangle(sorted_box(cx, cy, cx + dx * arm, cy + dy * width), radius=width / 2, fill=255)
        d.rounded_rectangle(sorted_box(cx, cy, cx + dx * width, cy + dy * arm), radius=width / 2, fill=255)
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    d.ellipse([mx - dot, my - dot, mx + dot, my + dot], fill=255)
    if plate_rgba is not None:
        plate = Image.new("L", (size * k, size * k), 0)
        ImageDraw.Draw(plate).rounded_rectangle([x0, y0, x1, y1], radius=side * radius_ratio, fill=255)
        alpha = Image.composite(Image.new("L", plate.size, 0), plate, shape)
        colour = plate_rgba
    else:
        alpha = shape
        colour = glyph_rgba
    alpha = alpha.resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), colour[:3] + (0,))
    out.putalpha(alpha.point(lambda a: a * colour[3] // 255))
    if glow:
        halo = alpha.filter(ImageFilter.GaussianBlur(glow[0])).point(lambda a: min(255, a * glow[1] // 255))
        base = Image.new("RGBA", (size, size), colour[:3] + (0,))
        base.putalpha(halo)
        base.alpha_composite(out)
        out = base
    return out


def brand_images():
    """Draws the brand images with Eikon's mark."""
    black = (0, 0, 0, 255)

    def glyph_box(x0, y0, x1, y1):
        # The plate-less glyph's corners span the given box.
        pad = (x1 - x0) * (190 / 944) / (1 - 2 * 190 / 944)
        return (x0 - pad, y0 - pad, x1 + pad, y1 + pad)

    images = {
        # Status icon (template, 22 pt @2x): a 32 px plate in a 44 px canvas, glyph cut out.
        "Tray/eikon-22.png": mark(44, (6, 6, 38, 38), black, plate_rgba=black),
        # "Reopen Eikon" tray menu item (template, 16 pt @2x).
        "Menu/16-menu-eikon.png": mark(32, glyph_box(2, 2, 30, 30), black, bar=2.2),
        # Library toolbar "show" tool (28 pt @2x): a template glyph the light Candy toolbar tints.
        "Library/lib-tool-28-eikon.png": mark(56, glyph_box(10, 10, 46, 46), black, bar=2.6),
        # Notification logo: the Candy app mark, drawn at 34 pt (@2x), the same in both themes.
        "Preview/24-notification-logo.png": app_icon(68, 68, 1.0),
        "Preview/24-notification-logo-dark.png": app_icon(68, 68, 1.0),
    }
    for name, image in images.items():
        image.save(ASSETS / name)


def svg_png(svg, size):
    import io
    import resvg_py
    png = resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA")


def tray_frames():
    """The status icon's template frames (44 px, 22 pt @2x), black plus alpha like eikon-22: the success
    frame is eikon-22's 32 px plate with Lucide's check cut out; the five capture frames are a dashed
    outline of the plate whose dashes advance by a fifth of a period per frame, so they loop."""
    head = '<svg xmlns="http://www.w3.org/2000/svg" width="44" height="44" viewBox="0 0 44 44">'
    radius = 32 * 220 / 944
    ok = (head + '<defs><mask id="m"><rect width="44" height="44" fill="white"/>'
          '<path d="M20 6 9 17l-5-5" transform="translate(12 12) scale(0.8333)" fill="none" stroke="black" '
          'stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></mask></defs>'
          f'<rect x="6" y="6" width="32" height="32" rx="{radius:.3f}" fill="black" mask="url(#m)"/></svg>')
    svg_png(ok, 44).save(ASSETS / "Tray" / "eikon-22-ok.png")
    # The dashed outline: a 3.5 px stroke on the centre line of a 30 px rounded square.
    side, r, width = 30.0, 7.0, 3.5
    perimeter = 4 * (side - 2 * r) + 2 * 3.141592653589793 * r
    period = perimeter / 10
    dash = period * 0.6
    for frame in range(1, 6):
        offset = -period * (frame - 1) / 5
        svg = (head + f'<rect x="7" y="7" width="{side}" height="{side}" rx="{r}" fill="none" stroke="black" '
               f'stroke-width="{width}" stroke-dasharray="{dash:.4f} {period - dash:.4f}" '
               f'stroke-dashoffset="{offset:.4f}"/></svg>')
        svg_png(svg, 44).save(ASSETS / "Tray" / f"eikon-22-animated-{frame}.png")


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    for scale in (100, 200):
        f = scale / 100
        suffix = f".scale-{scale}"
        save_png(f"Square150x150Logo{suffix}.png", int(150 * f), int(150 * f), 0.66)
        save_png(f"Square44x44Logo{suffix}.png", int(44 * f), int(44 * f), 1.0)
        save_png(f"Wide310x150Logo{suffix}.png", int(310 * f), int(150 * f), 0.66)
        save_png(f"SplashScreen{suffix}.png", int(620 * f), int(300 * f), 0.5)
        save_png(f"StoreLogo{suffix}.png", int(50 * f), int(50 * f), 1.0)
    for size in (16, 24, 32, 48, 256):
        save_png(f"Square44x44Logo.targetsize-{size}_altform-unplated.png", size, size, 1.0)
    big = glyph(256)
    big.save(ASSETS / "Eikon.ico", sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
    glyph(512).save(ROOT / "docs" / "logo.png")
    brand_images()
    tray_frames()


if __name__ == "__main__":
    main()
