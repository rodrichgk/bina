# Renders the Bina app icon (the b-with-waveform mark on a dark tile) at every
# Windows icon size, each drawn separately so small sizes use the simplified mark.
#   python tools/make_icon.py
# Writes src/resources/brand/bina.ico and bina-<size>.png.
import os
from PIL import Image, ImageDraw

TILE = (0x16, 0x18, 0x1F, 255)   # Surface
MOTO = (0xFF, 0xB4, 0x54, 255)   # Accent

# Mark geometry in the logo's own units (same as Butu's mark: stem + bowl)
STEM = (24, 12, 45, 106)          # rounded ends, radius 10.5
BOWL = (66, 70, 34)               # centre x, centre y, radius
BARS_FULL = [(43, 63, 6, 14), (53, 55, 6, 30), (63, 48, 6, 44), (73, 56, 6, 28), (83, 62, 6, 16)]
BARS_SMALL = [(50, 58, 9, 24), (63, 50, 9, 40), (76, 59, 9, 22)]
CENTER = (62, 59)                 # middle of the mark's bounding box


def render(size):
    ss = 8                        # supersampling
    s = size * ss
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=s * 14 / 64, fill=TILE)

    unit = s / 64 * 0.46 * (1.12 if size <= 24 else 1.0)  # small sizes fill the tile a bit more

    def px(x, y):
        return (s / 2 + (x - CENTER[0]) * unit, s / 2 + (y - CENTER[1]) * unit)

    x0, y0 = px(STEM[0], STEM[1])
    x1, y1 = px(STEM[2], STEM[3])
    d.rounded_rectangle((x0, y0, x1, y1), radius=10.5 * unit, fill=MOTO)
    cx, cy = px(BOWL[0], BOWL[1])
    r = BOWL[2] * unit
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=MOTO)

    bars = BARS_FULL if size >= 48 else BARS_SMALL if size >= 24 else []
    for (bx, by, bw, bh) in bars:
        a = px(bx, by)
        b = px(bx + bw, by + bh)
        d.rounded_rectangle((a[0], a[1], b[0], b[1]), radius=bw / 2 * unit, fill=TILE)

    return img.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "src", "resources", "brand")
    os.makedirs(out, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = {size: render(size) for size in sizes}
    for size, img in images.items():
        img.save(os.path.join(out, f"bina-{size}.png"))
    # Each size keeps its own drawing (PIL would otherwise downscale the largest)
    images[256].save(os.path.join(out, "bina.ico"), sizes=[(n, n) for n in sizes],
                     append_images=[images[n] for n in sizes if n != 256])
    print("wrote", out)


if __name__ == "__main__":
    main()
