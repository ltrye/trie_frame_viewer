"""Create the FV vector master and derive Windows icon sizes from its geometry.

Run: python scripts/generate_app_icon.py
SVG creation uses only the standard library; ICO/PNG exports require Pillow.
The SVG contains paths, with no fonts, embedded bitmaps, or external resources.
"""

from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "branding"
CANVAS = 512
INSET = 16
RADIUS = 104
BACKGROUND = "#1e2225"
FOREGROUND = "#a8d5c2"
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)

# Both the SVG and Windows exports use these original, font-independent outlines.
GLYPHS = (
    ((84, 148), (216, 148), (216, 192), (128, 192), (128, 234),
     (200, 234), (200, 278), (128, 278), (128, 364), (84, 364)),
    ((232, 148), (279, 148), (330, 310), (381, 148), (428, 148),
     (353, 364), (307, 364)),
)


def outline(points):
    first, *remaining = points
    return f"M{first[0]} {first[1]} " + " ".join(
        f"L{x} {y}" for x, y in remaining
    ) + " Z"


def svg(tile=True):
    title = escape("FV — Trie FrameViewer")
    background = (
        f'  <rect x="{INSET}" y="{INSET}" width="{CANVAS - 2 * INSET}" '
        f'height="{CANVAS - 2 * INSET}" rx="{RADIUS}" fill="{BACKGROUND}"/>\n'
        if tile else ""
    )
    letters = "\n".join(
        f'  <path d="{outline(points)}" fill="{FOREGROUND}"/>'
        for points in GLYPHS
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{CANVAS}" '
        f'height="{CANVAS}" viewBox="0 0 {CANVAS} {CANVAS}" role="img" '
        'aria-labelledby="title description">\n'
        f'  <title id="title">{title}</title>\n'
        '  <desc id="description">Geometric F and V letters in mint green.</desc>\n'
        f'{background}{letters}\n</svg>\n'
    )


def rasterize(size):
    # Raster exports are derived from the vector geometry for Windows compatibility.
    # Supersampling smooths edges at small display sizes; it does not alter the SVG.
    from PIL import Image, ImageDraw

    sample = 4
    scale = size * sample / CANVAS
    image = Image.new("RGBA", (size * sample, size * sample), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (INSET * scale, INSET * scale,
         (CANVAS - INSET) * scale, (CANVAS - INSET) * scale),
        radius=RADIUS * scale,
        fill=BACKGROUND,
    )
    for points in GLYPHS:
        draw.polygon([(x * scale, y * scale) for x, y in points], fill=FOREGROUND)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "fv-icon.svg").write_text(svg(), encoding="utf-8")
    (OUTPUT / "fv-mark.svg").write_text(svg(tile=False), encoding="utf-8")
    images = [rasterize(size) for size in ICON_SIZES]
    images[-1].save(
        OUTPUT / "fv-icon.ico",
        format="ICO",
        sizes=[(size, size) for size in ICON_SIZES],
        append_images=images[:-1],
    )
    rasterize(512).save(OUTPUT / "fv-icon-preview.png")
    print(f"Created SVG masters, Windows ICO, and preview in {OUTPUT}")


if __name__ == "__main__":
    main()
