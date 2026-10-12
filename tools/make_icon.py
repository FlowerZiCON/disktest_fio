"""生成程序图标 logo.ico（以及给窗口图标用的 logo-window.png）。

两种用法：

1. 把一张已有的图转成多尺寸 ICO（本工程实际用的方式 —— 母版是项目根目录的 logo.ico，
   它只有一帧 128x128，Windows 缩到 16x16 会糊）：
       python tools/make_icon.py resources/logo.ico resources/logo-window.png \
              --from resources/logo-source.ico

   注意：只输出「不大于原图」的尺寸，不做放大，避免小图硬撑出 256 帧变色块。

2. 用脚本内置的矢量画法生成（深色圆角底 + 硬盘 + 红/蓝速度曲线，
   红蓝对应界面里读=红、写=蓝）：
       python tools/make_icon.py resources/logo.ico resources/logo-window.png
"""

import sys

from PIL import Image, ImageDraw

# ---- 内置画法用的配色 ----
BG_TOP = (26, 38, 62)
BG_BOTTOM = (46, 74, 122)
DISK_LIGHT = (226, 232, 240)
DISK_MID = (168, 181, 199)
DISK_DARK = (86, 100, 120)
RED = (226, 46, 46)
BLUE = (46, 96, 226)

ALL_SIZES = (256, 128, 64, 48, 32, 24, 16)


def vertical_gradient(size, top, bottom):
    img = Image.new("RGB", (size, size), top)
    draw = ImageDraw.Draw(img)
    for y in range(size):
        t = y / max(1, size - 1)
        color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        draw.line([(0, y), (size, y)], fill=color)
    return img


def rounded_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return mask


def draw_detailed(size):
    """内置设计的完整版：硬盘 + 两条曲线。"""
    s = size * 4  # 先大后缩，边缘更干净
    base = vertical_gradient(s, BG_TOP, BG_BOTTOM).convert("RGBA")

    plate = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(plate)

    cx, cy = s * 0.5, s * 0.40
    r = s * 0.27
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=DISK_LIGHT)
    d.ellipse([cx - r * 0.78, cy - r * 0.78, cx + r * 0.78, cy + r * 0.78], fill=DISK_MID)
    d.ellipse([cx - r * 0.30, cy - r * 0.30, cx + r * 0.30, cy + r * 0.30], fill=DISK_DARK)
    d.ellipse([cx - r * 0.12, cy - r * 0.12, cx + r * 0.12, cy + r * 0.12], fill=DISK_LIGHT)

    base = Image.alpha_composite(base, plate)

    line = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    dl = ImageDraw.Draw(line)
    x0, x1 = s * 0.16, s * 0.84
    ybase = s * 0.80

    def poly(points, color):
        pts = [(x0 + (x1 - x0) * px, ybase - s * 0.30 * py) for px, py in points]
        dl.line(pts, fill=color, width=max(2, int(s * 0.035)), joint="curve")

    poly([(0.0, 0.30), (0.25, 0.78), (0.5, 0.62), (0.75, 0.88), (1.0, 0.55)], RED)
    poly([(0.0, 0.12), (0.25, 0.40), (0.5, 0.24), (0.75, 0.46), (1.0, 0.30)], BLUE)
    dl.line([(x0, ybase), (x1, ybase)], fill=(255, 255, 255, 110), width=max(1, int(s * 0.014)))

    base = Image.alpha_composite(base, line)
    base.putalpha(rounded_mask(s, int(s * 0.20)))
    return base.resize((size, size), Image.LANCZOS)


def draw_simple(size):
    """内置设计的小尺寸简化版：大硬盘 + 一红一蓝两条粗线。"""
    s = size * 8
    base = vertical_gradient(s, BG_TOP, BG_BOTTOM).convert("RGBA")
    d = ImageDraw.Draw(base)

    cx, cy = s * 0.5, s * 0.38
    r = s * 0.32
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=DISK_LIGHT)
    d.ellipse([cx - r * 0.34, cy - r * 0.34, cx + r * 0.34, cy + r * 0.34], fill=DISK_DARK)

    x0, x1 = s * 0.14, s * 0.86
    d.line([(x0, s * 0.80), (x1, s * 0.80)], fill=RED, width=int(s * 0.13))
    d.line([(x0, s * 0.94), (x1, s * 0.94)], fill=BLUE, width=int(s * 0.13))

    base.putalpha(rounded_mask(s, int(s * 0.18)))
    return base.resize((size, size), Image.LANCZOS)


def generated_frames():
    frames = [draw_detailed(s) for s in (256, 128, 64, 48, 32)]
    frames += [draw_simple(s) for s in (24, 16)]
    return frames


def frames_from_image(path):
    """把一张已有图转成多尺寸；只缩不放。"""
    source = Image.open(path).convert("RGBA")
    side = max(source.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(source, ((side - source.width) // 2, (side - source.height) // 2), source)

    sizes = [s for s in ALL_SIZES if s <= side]
    if not sizes:  # 原图比 16 还小，至少留一帧
        sizes = [side]
    return [canvas.resize((s, s), Image.LANCZOS) for s in sizes]


def main():
    args = sys.argv[1:]
    source = None
    if "--from" in args:
        index = args.index("--from")
        source = args[index + 1]
        del args[index:index + 2]

    out = args[0] if args else "resources/logo.ico"
    png_out = args[1] if len(args) > 1 else None

    frames = frames_from_image(source) if source else generated_frames()

    frames[0].save(out, format="ICO", sizes=[(f.width, f.height) for f in frames])
    origin = f"来自 {source}" if source else "脚本内置画法"
    print(f"wrote {out} ({origin}): " + ", ".join(f"{f.width}x{f.height}" for f in frames))

    if png_out:
        # 窗口图标用 PNG：PNG 解码器编在 QtGui 里，不需要额外插件，
        # 而 .ico 在运行时解码要 imageformats/qico 插件，部署时容易漏。
        frames[0].save(png_out, format="PNG")
        print(f"wrote {png_out}: {frames[0].width}x{frames[0].height}")


if __name__ == "__main__":
    main()
