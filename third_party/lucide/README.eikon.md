# Lucide (vendored icon sources)

Lucida's tool and UI icons are [Lucide](https://lucide.dev) icons. `icons/` holds the SVGs that
`tools/generate_icons.py` renders into `src/Eikon/Assets`, copied unmodified from the npm package.

- Version: **lucide-static 1.70.0** (`npm pack lucide-static@1.70.0`, `package/icons/<name>.svg`).
- Licence: ISC (`LICENSE`, unchanged; it also carries the MIT notice for the icons derived from Feather).
- Rendering: stroke width 1.5 in the 24-unit viewBox with Lucide's round caps and joins, tinted to the
  colour and alpha of the image each one replaced. The counter tool's circle with a "1" is drawn in
  `generate_icons.py` (Lucide has no such icon).

## Updating

1. `npm pack lucide-static@<version>` and unpack it.
2. Copy the SVGs named in `tools/generate_icons.py` from `package/icons/` into `icons/`, and `package/LICENSE`
   over `LICENSE`.
3. `uv run --with resvg-py==0.5.0 --with pillow python tools/generate_icons.py` and look at the result.
