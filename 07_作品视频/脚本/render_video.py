#!/usr/bin/env python3
"""渲染完整作品视频：钩子 → 标题 → 工业实景 → 设计背景 → 原理 → 整机与外设 → 外壳内部 → 机盖 → 整机成品 → 主载板 → 接口 → 前面板 → AX7010 → 系统架构 → PL 硬件设计 → 上电实测 → 实测结果 → 结尾。

在 WSL 中运行（用到 ffmpeg、Pillow、numpy 和 Windows 字体）：
    python3 render_video.py                          # 输出 ../输出/见微_作品视频.mp4
    python3 render_video.py --stills 12 40 --still-dir 目录   # 只导出静帧检查版式
默认使用 ../素材/实拍/钩子_电机铝片散斑.mp4 作开场，--hook 可换成别的片段。
"""
import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
import wave
from functools import lru_cache
from multiprocessing import Pool
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps

W, H, FPS = 1920, 1080, 30
PROJ = Path(__file__).resolve().parents[1]
REPO = PROJ.parent
PHOTOS = PROJ / "素材" / "实拍"
HOOK_CLIP = PHOTOS / "钩子_电机铝片散斑.mp4"
HOOK_STILL = PHOTOS / "钩子_静帧.png"
PCB_MAIN = REPO / "01_硬件工程/主载板PCB_工程预览.webp"
PCB_PANEL = REPO / "01_硬件工程/前面板PCB_工程预览.webp"
FONT_DIRS = [Path("/mnt/c/Windows/Fonts"), Path("C:/Windows/Fonts")]

# 版式：左对齐 160 px 页边；图解放在实验台浅灰底上，实物照片满幅；黄色只标"当前/自研/测点"，红色只标 AX7010
PAPER = (235, 237, 240)
INK = (25, 27, 30)
INK2 = (82, 87, 95)
INK3 = (138, 143, 151)
RULE = (194, 198, 204)
WHITE = (250, 250, 249)
PANEL = (20, 21, 23)
GRID = (62, 64, 69)
HUD = (170, 175, 182)
YELLOW = (242, 183, 5)
RED = (216, 38, 30)
MARGIN = 160

HOOK_FRAMES = []
HOOK_DUR = 4.6
# 测量结果取自 05_验证资料/板端实测/record.json（2026-11-04 02:05 板端测量记录）
HOOK_RESULT = (("振动频率", "19.95", "Hz"), ("振幅峰峰值", "0.209", "mm"))
LIVE_DATA = (("视觉主频", "19.95", "Hz", "相机 45 fps 采集"), ("麦克风核对", "19.93", "Hz", "与视觉相差 0.02 Hz"),
             ("位移峰峰值", "0.209", "mm", "按试验标定换算"), ("采集", "452", "帧", "丢帧 0，跟踪失败 0"))
# 12 个测点的信号与停机基线之比（gate_ratio），门限为 3 倍
LIVE_GATES = (81.1, 95.0, 67.3, 59.7, 75.6, 62.3, 60.7, 36.3, 65.0, 67.9, 54.1, 37.3)

# 钩子静帧中散斑纸四角（1280×720 像素坐标）
STICKER = ((392, 225), (1000, 258), (992, 475), (358, 400))

# 工业设备实景：机器上的压力表随设备轻微抖动，Pexels 视频 40481917（Pexels 许可，可免费商用、无需署名），无原声
INDUSTRY_CLIP = PROJ / "素材" / "外部" / "工业设备_压力表振动.mp4"
INDUSTRY_FRAMES = []
INDUSTRY_DUR = 9.0  # 原片 7.4 s，渲染时按场景实际时长放慢

# 上电实测：同一次测量的三段录像，统一按全景原片时间 F 取帧（素材内时间 = F − 偏移）。
# 屏幕原片比全景早 1.0 s（报警灯 37.2 / 38.2 s），APP 原片比全景晚 3.4 s（开始测量 31.8 / 28.4 s）；
# 三段都截成对应全景原片 2.5–70 s。全景原片事件：23.7 s 基线完成语音，25.7 s 电机启动，38.2 s 报警灯亮，41.2 s 报警语音。
LIVE = PROJ / "素材" / "实测"
LIVE_CLIPS = {"full": (LIVE / "全景_测量过程.mp4", 2.5), "screen": (LIVE / "屏幕_测量过程.mp4", 2.5),
              "app": (LIVE / "APP_测量过程.mp4", 2.5)}
LIVE_FRAMES = {}

# 旁白：百度语音度怀安，由 synth_voice.py 按字幕合成；字幕显示到该句旁白结束
VOICE_DIR = PROJ / "素材" / "配音"
VOICE_LOG = VOICE_DIR / "配音记录.json"
SPEECH_REPL = (("Hz", "赫兹"), ("mm", "毫米"), ("“", ""), ("”", ""))


def speech_of(cue):
    """字幕 (起, 止, 文本[, 旁白]) 对应的旁白文本；给出第 4 项时以其为准，None 表示不配音。"""
    if len(cue) > 3:
        return cue[3]
    s = cue[2]
    if s.startswith("设备语音"):
        return None
    for a, b in SPEECH_REPL:
        s = s.replace(a, b)
    return s


@lru_cache(maxsize=None)
def voice_log():
    if not VOICE_LOG.exists():
        return {}
    return {e["text"]: e for e in json.loads(VOICE_LOG.read_text(encoding="utf-8"))["clips"]}


def narration_lines():
    return [sp for *_, sc in scenes() for cue in sc if (sp := speech_of(cue))]


# ---------- 缓动 ----------

def clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def prog(t, a, b):
    return clamp((t - a) / (b - a)) if b > a else float(t >= a)


def ease_out(x):
    return 1 - (1 - x) ** 3


def ease_in_out(x):
    return 4 * x ** 3 if x < 0.5 else 1 - (-2 * x + 2) ** 3 / 2


def ease_back(x, s=1.3):
    x -= 1
    return 1 + (s + 1) * x ** 3 + s * x ** 2


def lerp(a, b, p):
    return a + (b - a) * p


# ---------- 字体与文字 ----------

FONT_FILES = {"sans": "NotoSansSC-VF.ttf", "serif": "NotoSerifSC-VF.ttf", "num": "bahnschrift.ttf",
              "cond": "bahnschrift.ttf"}
TOP = {"sans": 0.86, "serif": 0.86, "num": 0.72, "cond": 0.72}
MID = {"sans": 0.37, "serif": 0.37, "num": 0.36, "cond": 0.36}
BOT = {"sans": 0.12, "serif": 0.12, "num": 0.0, "cond": 0.0}


@lru_cache(maxsize=None)
def font(kind, size, weight):
    for d in FONT_DIRS:
        p = d / FONT_FILES[kind]
        if p.exists():
            f = ImageFont.truetype(str(p), size)
            f.set_variation_by_axes([weight] if kind in ("sans", "serif") else [weight, 100 if kind == "num" else 87.5])
            return f
    raise FileNotFoundError(FONT_FILES[kind])


def resolve(s, kind):
    """Bahnschrift 没有中文字形，含中文的字符串改用 Noto Sans SC。"""
    if kind in ("num", "cond") and any("\u2e80" <= ch <= "\u9fff" or "\uff00" <= ch <= "\uffef" for ch in s):
        return "sans"
    return kind


class Sprite:
    __slots__ = ("im", "ox", "oy", "w")

    def __init__(self, im, ox, oy, w):
        self.im, self.ox, self.oy, self.w = im, ox, oy, w


@lru_cache(maxsize=2048)
def text_sprite(s, kind, size, weight, color, tracking):
    f = font(kind, size, weight)
    asc, desc = f.getmetrics()
    pad = int(size * 0.3) + 2
    widths = [f.getlength(ch) for ch in s] if tracking else None
    width = sum(widths) + tracking * (len(s) - 1) if tracking else f.getlength(s)
    im = Image.new("RGBA", (int(math.ceil(width)) + 2 * pad, asc + desc + 2 * pad), color[:3] + (0,))
    d = ImageDraw.Draw(im)
    fill = color[:3] + (255,)
    if tracking:
        x = pad
        for ch, cw in zip(s, widths):
            d.text((x, pad + asc), ch, font=f, fill=fill, anchor="ls")
            x += cw + tracking
    else:
        d.text((pad, pad + asc), s, font=f, fill=fill, anchor="ls")
    return Sprite(im, pad, pad + asc, width)


def text(dst, s, x, y, size=32, weight=400, kind="sans", color=INK, alpha=1.0, anchor="ls", tracking=0):
    kind = resolve(s, kind)
    sp = text_sprite(s, kind, size, weight, color, tracking)
    ax = {"l": 0.0, "m": sp.w / 2, "r": sp.w}[anchor[0]]
    ay = {"t": -TOP[kind] * size, "m": -MID[kind] * size, "s": 0.0, "b": BOT[kind] * size}[anchor[1]]
    paste(dst, sp.im, x - ax - sp.ox, y - ay - sp.oy, alpha)
    return sp.w


def text_w(s, size=32, weight=400, kind="sans", tracking=0):
    return text_sprite(s, resolve(s, kind), size, weight, INK, tracking).w


def rise(dst, s, x, y, size, weight, kind="sans", color=INK, p=1.0, tracking=0, alpha=1.0):
    """文字从基线下方的遮罩里升起。"""
    if p <= 0 or alpha <= 0:
        return
    kind = resolve(s, kind)
    sp = text_sprite(s, kind, size, weight, color, tracking)
    top = y - TOP[kind] * size - size * 0.16
    win_h = int(y + 0.2 * size + 6 - top)
    layer = Image.new("RGBA", (sp.im.width, win_h), color[:3] + (0,))
    paste(layer, sp.im, 0, (y - top) + (1 - p) * win_h - sp.oy)
    paste(dst, layer, x - sp.ox, top, clamp(p * 1.4) * alpha)


def rot_text(dst, s, cx, cy, size, weight=400, kind="sans", color=INK, alpha=1.0, angle=90):
    im = text_sprite(s, resolve(s, kind), size, weight, color, 0).im.rotate(angle, expand=True, resample=Image.BICUBIC)
    paste(dst, im, cx - im.width / 2, cy - im.height / 2, alpha)


# ---------- 合成与图形 ----------

def paste(dst, im, x, y, alpha=1.0):
    if alpha <= 0.004:
        return
    x, y = int(round(x)), int(round(y))
    w, h = im.size
    if x >= dst.width or y >= dst.height or x + w <= 0 or y + h <= 0:
        return
    if x < 0 or y < 0:
        im = im.crop((max(0, -x), max(0, -y), w, h))
        x, y = max(x, 0), max(y, 0)
    if alpha < 0.996:
        im = im.copy()
        im.putalpha(im.getchannel("A").point(lambda v: int(v * alpha + 0.5)))
    dst.alpha_composite(im, (x, y))


@lru_cache(maxsize=512)
def box_img(w, h, fill=None, outline=None, width=0, r=0):
    ss = 3 if r else 1
    big = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    if r:
        d.rounded_rectangle((0, 0, w * ss - 1, h * ss - 1), radius=r * ss, fill=fill, outline=outline, width=width * ss)
        return big.resize((w, h), Image.LANCZOS)
    d.rectangle((0, 0, w - 1, h - 1), fill=fill, outline=outline, width=width)
    return big


def fill_rect(dst, box, color, alpha=1.0):
    x0, y0, x1, y1 = [int(round(v)) for v in box]
    if x1 > x0 and y1 > y0 and alpha > 0.004:
        paste(dst, Image.new("RGBA", (x1 - x0, y1 - y0), color[:3] + (int(255 * alpha),)), x0, y0)


@lru_cache(maxsize=256)
def circle_img(d2, fill=None, outline=None, width=0):
    r = d2 / 2
    d = int(math.ceil(2 * r + width + 4))
    ss = 4
    big = Image.new("RGBA", (d * ss, d * ss), (0, 0, 0, 0))
    c = d * ss / 2
    ImageDraw.Draw(big).ellipse((c - r * ss, c - r * ss, c + r * ss, c + r * ss), fill=fill, outline=outline,
                                width=int(width * ss))
    return big.resize((d, d), Image.LANCZOS)


def circle(dst, cx, cy, r, fill=None, outline=None, width=0, alpha=1.0):
    if r > 0.3:
        im = circle_img(round(r * 4) / 2, fill, outline, width)
        paste(dst, im, cx - im.width / 2, cy - im.height / 2, alpha)


def polyline(dst, pts, color, width=2, alpha=1.0, aa=False):
    if alpha <= 0.004 or len(pts) < 2:
        return
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    pad = width + 3
    x0, y0 = int(min(xs)) - pad, int(min(ys)) - pad
    x1, y1 = int(max(xs)) + pad + 1, int(max(ys)) + pad + 1
    ss = 3 if aa else 1
    layer = Image.new("RGBA", ((x1 - x0) * ss, (y1 - y0) * ss), color[:3] + (0,))
    a = alpha * (color[3] / 255 if len(color) > 3 else 1.0)
    pl = [((x - x0) * ss, (y - y0) * ss) for x, y in pts] if aa else [(round(x) - x0, round(y) - y0) for x, y in pts]
    ImageDraw.Draw(layer).line(pl, fill=color[:3] + (int(255 * a),), width=width * ss, joint="curve")
    if aa:
        layer = layer.resize((x1 - x0, y1 - y0), Image.LANCZOS)
    paste(dst, layer, x0, y0)


def partial(pts, f):
    if f >= 1:
        return list(pts)
    segs = list(zip(pts, pts[1:]))
    lens = [math.dist(a, b) for a, b in segs]
    goal = sum(lens) * f
    out = [pts[0]]
    for (a, b), seg in zip(segs, lens):
        if goal >= seg:
            out.append(b)
            goal -= seg
        else:
            r = goal / seg if seg else 0
            out.append((a[0] + (b[0] - a[0]) * r, a[1] + (b[1] - a[1]) * r))
            break
    return out


def point_on(pts, f):
    return partial(pts, clamp(f))[-1]


def dashed(dst, pts, color, width=2, dash=10, gap=7, alpha=1.0, aa=False):
    for a, b in zip(pts, pts[1:]):
        length = math.dist(a, b)
        s = 0.0
        while s < length:
            e = min(s + dash, length)
            p = (a[0] + (b[0] - a[0]) * s / length, a[1] + (b[1] - a[1]) * s / length)
            q = (a[0] + (b[0] - a[0]) * e / length, a[1] + (b[1] - a[1]) * e / length)
            polyline(dst, [p, q], color, width, alpha, aa)
            s += dash + gap


def dashed_box(dst, box, color, width=2, alpha=1.0, dash=10, gap=7):
    x0, y0, x1, y1 = box
    dashed(dst, [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], color, width, dash, gap, alpha)


def arrow(dst, a, b, color=INK, width=2, alpha=1.0, head=9, both=False):
    polyline(dst, [a, b], color + (255,), width, alpha)
    for p, q in ((a, b), (b, a)) if both else ((a, b),):
        ang = math.atan2(q[1] - p[1], q[0] - p[0])
        tip = q
        l = (tip[0] - head * math.cos(ang - 0.42), tip[1] - head * math.sin(ang - 0.42))
        r = (tip[0] - head * math.cos(ang + 0.42), tip[1] - head * math.sin(ang + 0.42))
        x0, y0 = int(min(tip[0], l[0], r[0])) - 2, int(min(tip[1], l[1], r[1])) - 2
        x1, y1 = int(max(tip[0], l[0], r[0])) + 3, int(max(tip[1], l[1], r[1])) + 3
        ss = 4
        layer = Image.new("RGBA", ((x1 - x0) * ss, (y1 - y0) * ss), color + (0,))
        ImageDraw.Draw(layer).polygon([((x - x0) * ss, (y - y0) * ss) for x, y in (tip, l, r)], fill=color + (255,))
        paste(dst, layer.resize((x1 - x0, y1 - y0), Image.LANCZOS), x0, y0, alpha)


@lru_cache(maxsize=None)
def bg_canvas():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.sqrt(((xx - W * 0.28) / W) ** 2 + ((yy - H * 0.15) / H) ** 2)
    base = np.array(PAPER, np.float32) + np.clip(1 - r * 1.15, 0, 1)[..., None] * 7
    arr = base + np.random.default_rng(3).normal(0, 1.2, (H, W, 1))
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def shadowed(im, blur=24, offset=16, alpha=0.26, pad=64):
    w, h = im.size
    out = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    a = Image.new("L", out.size, 0)
    a.paste(int(255 * alpha), (pad + 6, pad + offset, pad + w - 6, pad + h + offset // 2))
    sh = Image.new("RGBA", out.size, (12, 14, 18, 0))
    sh.putalpha(a.filter(ImageFilter.GaussianBlur(blur)))
    out.alpha_composite(sh)
    out.alpha_composite(im.convert("RGBA"), (pad, pad))
    return out, pad


# ---------- 素材 ----------

@lru_cache(maxsize=None)
def photo(name):
    p = Path(name) if Path(name).is_absolute() else PHOTOS / name
    im = ImageOps.exif_transpose(Image.open(p)).convert("RGB")
    im = ImageEnhance.Contrast(im).enhance(1.05)
    im = ImageEnhance.Color(im).enhance(0.97)
    return im.filter(ImageFilter.UnsharpMask(radius=1.4, percent=55, threshold=2))


def view(im, cx, cy, cw):
    cw = min(cw, im.width, im.height * W / H)
    ch = cw * H / W
    x0 = clamp(cx - cw / 2, 0, im.width - cw)
    y0 = clamp(cy - ch / 2, 0, im.height - ch)
    return im.resize((W, H), Image.BICUBIC, box=(x0, y0, x0 + cw, y0 + ch)), (x0, y0, W / cw)


def to_screen(pt, xf):
    x0, y0, s = xf
    return (pt[0] - x0) * s, (pt[1] - y0) * s


@lru_cache(maxsize=None)
def rounded_mask(w, h, r):
    return box_img(w, h, fill=(255, 255, 255, 255), r=r).getchannel("A")


@lru_cache(maxsize=None)
def thumb(name, box, d):
    im = photo(name).crop(box).resize((d, d), Image.LANCZOS).convert("RGBA")
    ImageDraw.Draw(im).rectangle((0, 0, d - 1, d - 1), outline=INK + (255,), width=1)
    return im


def sticker_pts():
    """散斑纸上 3 行 × 4 列测点（板端默认 12 个），返回钩子静帧坐标。"""
    tl, tr, br, bl = STICKER
    out = []
    for v in (0.22, 0.5, 0.78):
        for u in (0.2, 0.4, 0.6, 0.8):
            top = (lerp(tl[0], tr[0], u), lerp(tl[1], tr[1], u))
            bot = (lerp(bl[0], br[0], u), lerp(bl[1], br[1], u))
            out.append((lerp(top[0], bot[0], v), lerp(top[1], bot[1], v)))
    return out


# ---------- 标签与标注 ----------

@lru_cache(maxsize=256)
def strip_img(s, size, weight, kind, bg, fg, padx, pady, border):
    tw = text_w(s, size, weight, kind)
    w, h = int(tw + 2 * padx), int(size * (1.0 if resolve(s, kind) in ("sans", "serif") else 0.78) + 2 * pady)
    im = box_img(w, h, fill=bg + (255,), outline=(border + (255,)) if border else None, width=1 if border else 0).copy()
    text(im, s, padx, h / 2, size=size, weight=weight, kind=kind, color=fg, anchor="lm")
    return im


def strip(dst, s, x, y, size=24, weight=500, kind="sans", bg=WHITE, fg=INK, pad=(14, 7), anchor="lt",
          alpha=1.0, reveal=1.0, border=None):
    im = strip_img(s, size, weight, kind, bg, fg, pad[0], pad[1], border)
    w, h = im.size
    x0 = x - {"l": 0, "m": w / 2, "r": w}[anchor[0]]
    y0 = y - {"t": 0, "m": h / 2, "b": h}[anchor[1]]
    if reveal < 1:
        rw = int(w * clamp(reveal))
        if rw < 1:
            return w, h
        im = im.crop((0, 0, rw, h))
    paste(dst, im, x0, y0, alpha)
    return w, h


@lru_cache(maxsize=128)
def tag_img(title, sub, accent):
    """accent：None 白底，YELLOW 自研电路板，RED AX7010。"""
    bg = accent or WHITE
    tw = text_w(title, 28, 600)
    sw = text_w(sub, 22, 500, "cond") if sub else 0
    w = int(max(tw, sw) + 36)
    h = 82 if sub else 50
    card = box_img(w, h, fill=bg + (255,)).copy()
    text(card, title, 18, 37 if sub else 35, size=28, weight=600, color=WHITE if accent == RED else INK)
    if sub:
        text(card, sub, 18, 68, size=22, weight=500, kind="cond",
             color=(255, 226, 222) if accent == RED else INK if accent else INK2)
    out, pad = shadowed(card, blur=8, offset=4, alpha=0.24, pad=14)
    return out, pad


def annot(dst, tl, anchor, end, title, sub, side="r", accent=None, alpha=1.0, on_photo=True):
    """引线标注：锚点 → 折线 → 标签，tl 为该标注出现后的时间。"""
    if tl <= 0 or alpha <= 0.004:
        return
    ax, ay = anchor
    pd = ease_back(prog(tl, 0, 0.28))
    pts = [(ax, ay), (ax, end[1]), end]
    pl = partial(pts, ease_in_out(prog(tl, 0.12, 0.45)))
    if on_photo:
        polyline(dst, pl, WHITE + (235,), 5, alpha)
    polyline(dst, pl, INK + (255,), 2, alpha)
    circle(dst, ax, ay, 8 * pd, fill=WHITE + (255,), alpha=alpha)
    circle(dst, ax, ay, 5 * pd, fill=(accent or INK) + (255,), alpha=alpha)
    pc = ease_out(prog(tl, 0.35, 0.7))
    card, pad = tag_img(title, sub, accent)
    cw, ch = card.width - 2 * pad, card.height - 2 * pad
    x = end[0] if side == "r" else end[0] - cw
    paste(dst, card, x - pad + (1 - pc) * (10 if side == "r" else -10), end[1] - ch / 2 - pad, alpha * pc)


def square_mark(dst, cx, cy, s, color, alpha, width=3):
    s = max(3, int(round(s)))
    paste(dst, box_img(s, s, outline=color + (255,), width=width), cx - s / 2, cy - s / 2, alpha)


def spec_rows(dst, t, x, y, rows, t0, width=640):
    for k, (key, val) in enumerate(rows):
        p = ease_out(prog(t, t0 + 0.16 * k, t0 + 0.5 + 0.16 * k))
        yy = y + 66 * k
        polyline(dst, [(x, yy - 42), (x + width * ease_in_out(p), yy - 42)], RULE + (255,), 1)
        text(dst, key, x, yy, size=24, weight=500, color=INK2, alpha=p)
        text(dst, val, x + 128 + (1 - p) * 12, yy, size=29, weight=400, alpha=p)
    p = ease_in_out(prog(t, t0 + 0.16 * len(rows), t0 + 0.5 + 0.16 * len(rows)))
    yy = y + 66 * len(rows)
    polyline(dst, [(x, yy - 42), (x + width * p, yy - 42)], RULE + (255,), 1)


def dimension(dst, a, b, label, alpha, vertical=False):
    if alpha <= 0.004:
        return
    col = INK2 + (255,)
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    g = text_w(label, 22, 500, "cond") / 2 + 12
    if vertical:
        polyline(dst, [a, (mx, my - g)], col, 1, alpha)
        polyline(dst, [(mx, my + g), b], col, 1, alpha)
        rot_text(dst, label, mx, my, 22, 500, "cond", INK2, alpha)
        for p in (a, b):
            polyline(dst, [(p[0] - 9, p[1]), (p[0] + 9, p[1])], col, 1, alpha)
    else:
        polyline(dst, [a, (mx - g, my)], col, 1, alpha)
        polyline(dst, [(mx + g, my), b], col, 1, alpha)
        text(dst, label, mx, my, size=22, weight=500, kind="cond", color=INK2, alpha=alpha, anchor="mm")
        for p in (a, b):
            polyline(dst, [(p[0], p[1] - 9), (p[0], p[1] + 9)], col, 1, alpha)


@lru_cache(maxsize=None)
def scan_line(h):
    arr = np.zeros((h, 7, 4), np.uint8)
    arr[..., :3] = YELLOW
    arr[..., 3] = 255
    arr[:, 0, :3] = INK
    arr[:, 6, :3] = INK
    return Image.fromarray(arr, "RGBA")


def wipe_board(dst, render, real, x, y, p, alpha):
    """从左到右扫描，把 PCB 设计图换成实物照片。"""
    w, h = render.size
    wx = int(w * p)
    if wx < w:
        paste(dst, render.crop((wx, 0, w, h)), x + wx, y, alpha)
    if wx > 0:
        paste(dst, real.crop((0, 0, wx, h)), x, y, alpha)
    if 0 < p < 1:
        paste(dst, scan_line(h), x + wx - 3, y, alpha)


def board_shadow(dst, x, y, w, h, alpha):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sp, pad = shadowed(im, blur=26, offset=18, alpha=0.3, pad=70)
    paste(dst, sp, x - pad, y - pad, alpha)


def side_text(c, t, x, label, title, rows, t0=0.25):
    p = ease_out(prog(t, t0, t0 + 0.4))
    paste(c, box_img(14, 14, fill=YELLOW + (255,)), x, 316, p)
    text(c, label, x + 26, 331, size=24, weight=500, color=INK2, alpha=p)
    rise(c, title, x - 4, 422, 78, 800, p=ease_out(prog(t, t0 + 0.1, t0 + 0.6)))
    spec_rows(c, t, x, 548, rows, t0 + 0.4)


def corner_label(c, name, alpha, mark=YELLOW):
    """右上角的分组名：色块 + 白底标签。"""
    sw, sh = strip_img(name, 30, 700, "sans", WHITE, INK, 18, 9, None).size
    x = W - 72 - sw
    paste(c, box_img(16, 16, fill=mark + (255,)), x - 28, 52 + sh / 2 - 8, alpha)
    strip(c, name, x, 52, size=30, weight=700, pad=(18, 9), alpha=alpha)


# ---------- 场景 ----------

def s_hook(c, t):
    dk = ease_in_out(prog(t, 2.45, 2.95))
    if HOOK_FRAMES:
        frame = Image.open(HOOK_FRAMES[min(int(t * FPS), len(HOOK_FRAMES) - 1)]).convert("RGB")
        if dk > 0.01:
            frame = frame.filter(ImageFilter.GaussianBlur(9 * dk))
        c.paste(frame, (0, 0))
    else:
        text(c, "开场钩子：实拍视频待放入", MARGIN, 300, size=48, weight=700, anchor="lm")
    fill_rect(c, (0, 0, W, H), (8, 10, 14), 0.66 * dk)
    p = ease_out(prog(t, 0.25, 0.85))
    fade = 1 - prog(t, 2.25, 2.55)
    if p < 1:
        rise(c, "它在振动吗？", MARGIN - 8, 930, 132, 900, kind="serif", p=p)
    else:
        text(c, "它在振动吗？", MARGIN - 8, 930, size=132, weight=900, kind="serif", alpha=fade)
    r = ease_out(prog(t, 2.7, 3.05))
    if r <= 0:
        return
    strip(c, "实测结果", MARGIN, 300, size=32, weight=700, bg=YELLOW, pad=(18, 9), reveal=r)
    cnt = ease_out(prog(t, 2.85, 3.55))
    for i, (label, val, unit) in enumerate(HOOK_RESULT):
        x = MARGIN + i * 820
        a = ease_out(prog(t, 2.8 + 0.15 * i, 3.2 + 0.15 * i))
        text(c, label, x, 452, size=42, weight=500, color=(226, 229, 234), alpha=a)
        digits = len(val.split(".")[1]) if "." in val else 0
        vw = text(c, f"{float(val) * cnt:.{digits}f}", x - 6, 660, size=208, weight=700, kind="num", color=WHITE, alpha=a)
        text(c, unit, x + vw + 18, 660, size=76, weight=600, kind="num", color=YELLOW, alpha=a)
    text(c, "数据来自见微板端测量记录", MARGIN, 752, size=26, color=(200, 204, 210), alpha=ease_out(prog(t, 3.3, 3.7)))


@lru_cache(maxsize=None)
def title_photo():
    return photo("整机_低角度亮灯.webp").crop((300, 0, 1494, 1500)).resize((860, 1080), Image.LANCZOS).convert("RGBA")


def s_title(c, t):
    p = ease_out(prog(t, 0.0, 0.8))
    w = int(860 * p)
    if w > 1:
        paste(c, title_photo().crop((860 - w, 0, 860, 1080)), W - w, 0)
    rise(c, "见微", MARGIN - 8, 520, 250, 900, p=ease_out(prog(t, 0.25, 0.9)), tracking=14)
    fill_rect(c, (MARGIN, 588, MARGIN + 92 * ease_in_out(prog(t, 0.8, 1.15)), 601), YELLOW)
    rise(c, "基于 AMD Zynq-7010 的非接触视觉测振仪", MARGIN, 686, 44, 500, p=ease_out(prog(t, 0.9, 1.4)))


TGT_BOX = (60, 40, 1060, 640)
TGT_POS, TGT_W = (MARGIN, 190), 760
CAM_C = (1500, 418)
LENS_DY = 38


def tgt_xy(pt):
    s = TGT_W / (TGT_BOX[2] - TGT_BOX[0])
    return TGT_POS[0] + (pt[0] - TGT_BOX[0]) * s, TGT_POS[1] + (pt[1] - TGT_BOX[1]) * s


@lru_cache(maxsize=None)
def target_print():
    s = TGT_W / (TGT_BOX[2] - TGT_BOX[0])
    im = photo(str(HOOK_STILL)).crop(TGT_BOX)
    im = im.resize((TGT_W, int((TGT_BOX[3] - TGT_BOX[1]) * s)), Image.LANCZOS)
    return shadowed(im)


def camera_icon(dst, cx, cy, p, alpha=1.0):
    """侧视的双目相机，两只镜头上下排列、朝左，与 AN5642 实物方向一致。"""
    if p <= 0:
        return
    x0, y0, x1, y1 = cx - 60, cy - 90, cx + 140, cy + 90
    fill_rect(dst, (x0, y0, x1, y1), PAPER, alpha * clamp(p * 3))
    polyline(dst, partial([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], p), INK + (255,), 3, alpha)
    e = ease_out(prog(p, 0.45, 1.0))
    if e > 0:
        for ly in (cy - LENS_DY, cy + LENS_DY):
            lx = x0 - 70 * e
            fill_rect(dst, (lx, ly - 28, x0, ly + 28), WHITE, alpha)
            polyline(dst, [(x0, ly - 28), (lx, ly - 28), (lx, ly + 28), (x0, ly + 28)], INK + (255,), 3, alpha)
            polyline(dst, [(lx - 6, ly - 20), (lx - 6, ly + 20)], INK + (255,), 5, alpha)
    fill_rect(dst, (x1 - 46, y0 + 16, x1 - 18, y0 + 28), YELLOW, alpha * prog(p, 0.7, 1.0))


def s_industry(c, t):
    """工业设备实景，缓慢推近压力表。"""
    if not INDUSTRY_FRAMES:
        text(c, "工业设备实景：素材待放入", MARGIN, 300, size=48, weight=700, anchor="lm")
        return
    frame = Image.open(INDUSTRY_FRAMES[min(int(t * FPS), len(INDUSTRY_FRAMES) - 1)]).convert("RGB")
    z = 1 + 0.06 * ease_in_out(clamp(t / (INDUSTRY_DUR + 0.4)))
    cw, ch = W / z, H / z
    x0, y0 = (W - cw) * 0.45, (H - ch) * 0.55
    c.paste(frame.resize((W, H), Image.BICUBIC, box=(x0, y0, x0 + cw, y0 + ch)), (0, 0))


# 设计背景：先画逐点贴装、各拉一根线的接触式传感器，再换成一台相机同时测 12 个点
CONTACT = (1, 6, 11)


# 设计背景三段画面与旁白对齐：接触式传感器、激光测振仪单点、相机多点（相机段动画整体后移 BG_CAM_SHIFT）
BG_CONTACT_END, BG_LASER, BG_CAM_SHIFT = 6.0, (6.1, 11.4), 8.1


def s_background(c, t):
    card, pad = target_print()
    a = ease_out(prog(t, 0, 0.6))
    paste(c, card, TGT_POS[0] - pad - 30 * (1 - a), TGT_POS[1] - pad, a)
    pts = [tgt_xy(p) for p in sticker_pts()]
    centre = tgt_xy((686, 345))
    l0, l1 = BG_LASER
    la = ease_out(prog(t, l0, l0 + 0.4)) * (1 - prog(t, l1 - 0.4, l1))
    if la > 0:
        bx, by = CAM_C
        paste(c, box_img(250, 120, fill=INK + (255,), r=14), bx - 110, by - 60, la)
        circle(c, bx - 110, by, 26, fill=(60, 64, 70, 255), alpha=la)
        circle(c, bx - 110, by, 11, fill=RED + (255,), alpha=la)
        bp = ease_in_out(prog(t, l0 + 0.5, l0 + 1.1))
        if bp > 0:
            polyline(c, partial([(bx - 136, by), centre], bp), RED + (255,), 3, la, aa=True)
        if bp >= 1:
            circle(c, centre[0], centre[1], 9, fill=RED + (255,), alpha=la)
        text(c, "激光测振仪", bx + 15, by + 110, size=30, weight=600, alpha=la, anchor="ms")
    ca = 1 - prog(t, BG_CONTACT_END - 0.4, BG_CONTACT_END)
    if ca > 0:
        for k, idx in enumerate(CONTACT):
            x, y = pts[idx]
            cable = [(x, y + 14), (x, 700 + 10 * k), (530 + 30 * k, 772)]
            polyline(c, partial(cable, ease_in_out(prog(t, 0.7 + 0.25 * k, 1.6 + 0.25 * k))), INK2 + (255,), 3,
                     ca, aa=True)
            p = ease_back(prog(t, 0.5 + 0.25 * k, 0.8 + 0.25 * k))
            if p > 0:
                s = 28 * p
                fill_rect(c, (x - s / 2, y - s / 2, x + s / 2, y + s / 2), INK, ca)
                circle(c, x, y, 4 * p, fill=WHITE + (255,), alpha=ca)
        block(c, 500, 772, 120, 44, "采集器", None, 1, ca * ease_out(prog(t, 1.4, 1.8)), 22)
        x, y = pts[CONTACT[0]]
        annot(c, t - 1.5, (x + 14, y - 14), (x + 104, y - 110), "接触式传感器", None, alpha=ca)
    tc = t - BG_CAM_SHIFT
    lp = ease_in_out(prog(tc, 3.9, 4.8))
    if lp > 0:
        for ly in (CAM_C[1] - LENS_DY, CAM_C[1] + LENS_DY):
            dashed(c, partial([(CAM_C[0] - 144, ly), centre], lp), INK + (255,), 2, 12, 8, aa=True)
    camera_icon(c, CAM_C[0], CAM_C[1], ease_in_out(prog(tc, 3.3, 4.2)))
    text(c, "AN5642 双目相机", CAM_C[0] + 40, CAM_C[1] + 150, size=30, weight=600,
         alpha=ease_out(prog(tc, 3.9, 4.4)), anchor="ms")
    for k, (x, y) in enumerate(pts):
        p = ease_back(prog(tc, 4.5 + 0.04 * k, 4.85 + 0.04 * k))
        if p > 0:
            square_mark(c, x, y, 26 * p, YELLOW, clamp(p))
    x = MARGIN
    for k, word in enumerate(("非接触", "多点同测", "早期预警")):
        p = ease_out(prog(tc, 7.3 + 0.35 * k, 7.7 + 0.35 * k))
        w, _ = strip(c, word, x, 800, size=32, weight=600, pad=(20, 10), border=RULE, reveal=p)
        x += w + 16


PR_L = (MARGIN - 40, 200, 1000, 760)
PR_R = (1080, 200, 1800, 760)
CROP = (0, 0, 1016, 640)


@lru_cache(maxsize=None)
def cam_view():
    w, h = PR_L[2] - PR_L[0] - 16, PR_L[3] - PR_L[1] - 16
    return photo(str(HOOK_STILL)).crop(CROP).resize((w, h), Image.LANCZOS).convert("RGBA")


def cam_xy(pt):
    s = (PR_L[2] - PR_L[0] - 16) / (CROP[2] - CROP[0])
    return PR_L[0] + 8 + (pt[0] - CROP[0]) * s, PR_L[1] + 8 + (pt[1] - CROP[1]) * s


def panel_frame(dst, box, alpha=1.0):
    x0, y0, x1, y1 = box
    paste(dst, box_img(x1 - x0, y1 - y0, fill=PANEL + (255,)), x0, y0, alpha)


def graticule(dst, box, alpha):
    x0, y0, x1, y1 = box
    for k in range(11):
        x = lerp(x0, x1, k / 10)
        dashed(dst, [(x, y0), (x, y1)], GRID + (255,), 1, 2, 6, alpha)
    for k in range(9):
        y = lerp(y0, y1, k / 8)
        dashed(dst, [(x0, y), (x1, y)], GRID + (255,), 1, 2, 6, alpha)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    for k in range(51):
        x = lerp(x0, x1, k / 50)
        polyline(dst, [(x, cy - 4), (x, cy + 4)], (92, 95, 101, 255), 1, alpha)
    for k in range(41):
        y = lerp(y0, y1, k / 40)
        polyline(dst, [(cx - 4, y), (cx + 4, y)], (92, 95, 101, 255), 1, alpha)


def s_principle(c, t):
    a = ease_out(prog(t, 0, 0.5))
    text(c, "相机画面", PR_L[0], 178, size=26, weight=600, alpha=a)
    panel_frame(c, PR_L, a)
    paste(c, cam_view(), PR_L[0] + 8, PR_L[1] + 8, a)
    pts = sticker_pts()
    for k, pt in enumerate(pts):
        p = ease_back(prog(t, 0.4 + 0.04 * k, 0.75 + 0.04 * k))
        if p > 0:
            x, y = cam_xy(pt)
            square_mark(c, x, y, 34 * p, YELLOW, clamp(p), width=3 if k != 2 or t < 1.1 else 4)
    refs = ((35, 60), (90, 60), (35, 115), (90, 115))
    pulse = prog(t, 4.0, 5.0)
    for k, pt in enumerate(refs):
        p = ease_back(prog(t, 0.9 + 0.06 * k, 1.25 + 0.06 * k))
        if p > 0:
            x, y = cam_xy(pt)
            square_mark(c, x, y, 34 * p * (1 + 0.18 * math.sin(math.pi * pulse)), WHITE, clamp(p), width=2)
    p = ease_out(prog(t, 0.9, 1.3))
    square_mark(c, PR_L[0] + 12, PR_L[3] + 44, 22, YELLOW, p, width=3)
    text(c, "测点", PR_L[0] + 36, PR_L[3] + 44, size=26, weight=500, color=INK2, alpha=p, anchor="lm")
    square_mark(c, PR_L[0] + 132, PR_L[3] + 44, 22, INK, p, width=2)
    text(c, "参考点", PR_L[0] + 156, PR_L[3] + 44, size=26, weight=500, color=INK2, alpha=p, anchor="lm")
    ra = ease_out(prog(t, 0.2, 0.7))
    panel_frame(c, PR_R, ra)
    heads = (("亚像素跟踪", 0.0, 4.0), ("扣除晃动后的位移", 4.0, 6.1), ("频谱与停机基线", 6.1, 99))
    for label, h0, h1 in heads:
        hp = prog(t, h0 + 0.1, h0 + 0.4) * (1 - prog(t, h1 - 0.15, h1 + 0.05))
        text(c, label, PR_R[0], 178, size=26, weight=600, alpha=hp * ra)
    sel = cam_xy(pts[2])
    lk = ease_in_out(prog(t, 1.15, 1.55)) * (1 - prog(t, 3.8, 4.2))
    if lk > 0:
        path = partial([(sel[0] + 20, sel[1]), (PR_R[0] - 20, sel[1]), (PR_R[0] - 20, 480), (PR_R[0], 480)], lk)
        polyline(c, path, INK + (255,), 4)
        polyline(c, path, YELLOW + (255,), 2)
    ma = prog(t, 1.2, 1.6) * (1 - prog(t, 3.8, 4.2))
    if ma > 0:
        cx, cy = (PR_R[0] + PR_R[2]) / 2, 474
        px, py = int(round(pts[2][0])), int(round(pts[2][1]))
        patch = photo(str(HOOK_STILL)).crop((px - 6, py - 6, px + 6, py + 6)).resize((432, 432), Image.NEAREST)
        g = patch.convert("RGBA")
        gd = ImageDraw.Draw(g)
        for k in range(1, 12):
            gd.line([(k * 36, 0), (k * 36, 432)], fill=(0, 0, 0, 70))
            gd.line([(0, k * 36), (432, k * 36)], fill=(0, 0, 0, 70))
        paste(c, g, cx - 216, cy - 216, ma)
        dashed_box(c, (cx - 144, cy - 144, cx + 144, cy + 144), INK + (255,), 4, ma)
        dashed_box(c, (cx - 144, cy - 144, cx + 144, cy + 144), WHITE + (255,), 2, ma)
        off = 0.3 * 36 * math.sin(2 * math.pi * 1.4 * t)
        polyline(c, [(cx - 144 + off, cy - 144), (cx + 144 + off, cy - 144), (cx + 144 + off, cy + 144),
                     (cx - 144 + off, cy + 144), (cx - 144 + off, cy - 144)], YELLOW + (255,), 3, ma)
        text(c, "位移不足 1 个像素也能分辨", cx, PR_R[3] - 22, size=26, color=WHITE, alpha=ma, anchor="ms")
    gbox = (PR_R[0] + 24, PR_R[1] + 30, PR_R[2] - 24, PR_R[3] - 34)
    wa = prog(t, 4.3, 4.7) * (1 - prog(t, 6.0, 6.4))
    if wa > 0:
        graticule(c, gbox, wa)
        rev =ease_in_out(prog(t, 4.5, 5.9))
        n = 330
        noise = np.random.default_rng(7).normal(0, 0.05, n)
        u = np.linspace(0, 1, n)
        yv = np.sin(2 * math.pi * 6 * u) * (0.92 + 0.08 * np.sin(2 * math.pi * 0.7 * u)) + noise
        m = max(2, int(n * rev))
        cy, amp = (gbox[1] + gbox[3]) / 2, (gbox[3] - gbox[1]) / 8 * 2.4
        pl = [(gbox[0] + (gbox[2] - gbox[0]) * u[i], cy - amp * yv[i]) for i in range(m)]
        polyline(c, pl, YELLOW + (255,), 3, wa, aa=True)
    sa = prog(t, 6.0, 6.4)
    if sa > 0:
        graticule(c, gbox, sa)
        hts = np.random.default_rng(11).uniform(0.15, 0.55, 40)
        for i, v in ((10, 0.7), (11, 1.3), (12, 6.2), (13, 1.1), (14, 0.6), (24, 0.9)):
            hts[i] = v
        div = (gbox[3] - gbox[1]) / 8
        bw = (gbox[2] - gbox[0]) / 40
        ba = ease_out(prog(t, 6.9, 7.4))
        if ba > 0:
            yb = gbox[3] - 1.7 * div
            dashed(c, [(gbox[0], yb), (gbox[0] + (gbox[2] - gbox[0]) * ba, yb)], (225, 228, 233, 255), 2, 10, 6)
            text(c, "报警门限", gbox[2] - 6, yb - 12, size=24, weight=500, color=WHITE, alpha=ba, anchor="rs")
        for i, hv in enumerate(hts):
            p = ease_out(prog(t, 6.1 + i * 0.008, 6.6 + i * 0.008))
            hh = hv * div * p
            if hh >= 1:
                col = YELLOW if i == 12 else (176, 181, 189)
                fill_rect(c, (gbox[0] + i * bw + 3, gbox[3] - hh, gbox[0] + (i + 1) * bw - 3, gbox[3]), col, sa)
        pk = ease_out(prog(t, 6.7, 7.1))
        px = gbox[0] + 12.5 * bw
        strip(c, "主频", px, gbox[3] - 6.2 * div - 12, size=24, weight=700, bg=YELLOW, fg=INK, pad=(10, 5),
              anchor="mb", alpha=pk)


# 整机图标注：分三组讲外设用途；坐标为整机连线_俯拍.jpg 像素，标签终点为初始画面坐标
OV_CORE = ((710, 1111), (900, 720), "AX7010 开发板", "AMD Zynq-7010", "r", RED)
OV_CORE_EDGE = ((468, 1014), (839, 927), (884, 1190), (500, 1283))
OV_KEEP = ((875, 655), (1000, 300), "自研主载板", "外设接口与供电", "r", YELLOW)
OV_GROUPS = (
    ("测量感知", (
        ((450, 1110), (540, 650), "双目相机", "拍摄振动画面", "r", None),
        ((528, 470), (560, 150), "ToF 测距", "测量工作距离", "r", None),
        ((942, 366), (990, 215), "IMU 姿态", "监测相机晃动", "r", None),
        ((1258, 338), (1330, 190), "加速度计", "接触式参考对照", "r", None),
        ((252, 928), (300, 560), "温湿度 SHT40", "记录环境条件", "r", None),
        ((933, 1183), (980, 820), "PDM 麦克风", "声音频率对照", "r", None),
    )),
    ("操作交互", (
        ((1645, 1100), (1540, 700), "7 寸触摸屏", "显示画面与结果", "l", None),
        ((188, 755), (240, 400), "自研前面板", "状态灯与旋钮按键", "r", YELLOW),
        ((117, 1150), (200, 790), "NFC 读卡", "刷卡识别测点", "r", None),
        ((283, 600), (350, 230), "扬声器", "语音播报提示", "r", None),
    )),
    ("输出与辅助", (
        ((1712, 740), (1680, 380), "声光报警灯", "超限亮灯鸣响", "r", None),
        ((1330, 920), (1180, 560), "热敏打印机", "现场打印报告", "l", None),
        ((1112, 362), (980, 210), "十字激光", "投射十字线辅助对准", "l", None),
        ((1265, 445), (1290, 110), "补光灯", "暗处补充照明", "r", None),
        ((1567, 483), (1590, 238), "散热风扇", "机内散热", "r", None),
    )),
)
OV_BEAT = 4.15


def s_overview(c, t):
    im = photo("整机连线_俯拍.jpg")
    p = ease_in_out(clamp(t / (3 * OV_BEAT + 0.5)))
    frame, xf = view(im, 1000 - 14 * p, 787 - 6 * p, 2000 - 40 * p)
    c.paste(frame, (0, 0))
    s0 = W / 2000

    def place(spec, tl, alpha):
        anc, end, title, sub, side, accent = spec
        ax, ay = to_screen(anc, xf)
        bx, by = to_screen(anc, (0, 225, s0))
        annot(c, tl, (ax, ay), (end[0] + ax - bx, end[1] + ay - by), title, sub, side, accent, alpha=alpha)

    # AX7010 全程用红框圈出
    ep = ease_in_out(prog(t, 0.1, 0.8))
    if ep > 0:
        edge = partial([to_screen(pt, xf) for pt in OV_CORE_EDGE + OV_CORE_EDGE[:1]], ep)
        polyline(c, edge, WHITE + (190,), 9, aa=True)
        polyline(c, edge, RED + (255,), 5, aa=True)
    place(OV_CORE, t - 0.25, 1.0)
    place(OV_KEEP, t - 0.45, 1.0)
    for g, (name, specs) in enumerate(OV_GROUPS):
        t0 = g * OV_BEAT
        last = g == len(OV_GROUPS) - 1
        fade = 1.0 if last else 1 - prog(t, t0 + OV_BEAT - 0.35, t0 + OV_BEAT)
        if t < t0 or fade <= 0:
            continue
        for k, spec in enumerate(specs):
            place(spec, t - t0 - 0.45 - 0.16 * k, fade)
        corner_label(c, name, ease_out(prog(t, t0 + 0.05, t0 + 0.4)) * fade)


# 外壳内部照片按机盖照片的画框比例裁切；坐标为外壳内部装配.jpg 原图像素，标签终点为画面坐标（左侧右对齐 850，右侧左对齐 1630）
CASE_BOX = (0, 95, 1280, 1686)
CASE_POS, CASE_SCALE = (880, 90), 900 / (CASE_BOX[3] - CASE_BOX[1])
CASE_ANNOTS = (
    ((600, 340), (850, 100), "ToF 测距", "l", None),
    ((287, 395), (850, 180), "补光灯", "l", None),
    ((390, 403), (850, 330), "加速度计", "l", None),
    ((155, 740), (850, 455), "扬声器", "l", None),
    ((175, 1220), (850, 726), "散热风扇", "l", None),
    ((520, 1240), (850, 830), "主载板", "l", YELLOW),
    ((680, 277), (1630, 100), "十字激光", "r", None),
    ((875, 381), (1630, 200), "IMU 姿态", "r", None),
    ((970, 393), (1630, 320), "补光灯", "r", None),
    ((700, 440), (1630, 390), "双目相机", "r", None),
    ((615, 690), (1630, 470), "AX7010 开发板", "r", RED),
    ((1060, 1200), (1630, 715), "热敏打印机", "r", None),
    ((1022, 1424), (1630, 842), "温湿度 SHT40", "r", None),
)


@lru_cache(maxsize=None)
def case_print():
    im = photo("外壳内部装配.jpg").crop(CASE_BOX)
    return shadowed(im.resize((int(im.width * CASE_SCALE), int(im.height * CASE_SCALE)), Image.LANCZOS))


def case_title(c, t):
    rise(c, "整机装配", MARGIN - 4, 300, 78, 800, p=ease_out(prog(t, 0.15, 0.65)))
    text(c, "3D 打印外壳", MARGIN, 358, size=30, weight=500, color=INK2, alpha=ease_out(prog(t, 0.35, 0.8)))


def s_enclosure(c, t):
    card, pad = case_print()
    a = ease_out(prog(t, 0, 0.5))
    paste(c, card, CASE_POS[0] - pad, CASE_POS[1] - pad + (1 - a) * 16, a)
    case_title(c, t)
    for k, (anc, end, title, side, accent) in enumerate(CASE_ANNOTS):
        ax = CASE_POS[0] + (anc[0] - CASE_BOX[0]) * CASE_SCALE
        ay = CASE_POS[1] + (anc[1] - CASE_BOX[1]) * CASE_SCALE
        annot(c, t - 0.5 - 0.15 * k, (ax, ay), end, title, None, side, accent, on_photo=True)


# 机盖照片与外壳内部照片用同一画框，交叉淡化时像盖上机盖；坐标为外壳机盖.webp 原图像素
LID_BOX = (90, 240, 1355, 1813)
LID_SCALE = 900 / (LID_BOX[3] - LID_BOX[1])
LID_ANNOTS = (
    ((315, 440), (850, 204), "三色声光报警灯", "l", None),
    ((342, 822), (850, 423), "NFC 读卡", "l", None),
    ((330, 1080), (850, 545), "前面板", "l", YELLOW),
    ((466, 1185), (850, 655), "麦克风口", "l", None),
    ((840, 1010), (1630, 531), "7 寸触摸屏", "r", None),
)
LID_BUTTONS = (((418, 1478), "风扇"), ((608, 1484), "警报灯"), ((801, 1488), "照明"), ((993, 1492), "电源"))


@lru_cache(maxsize=None)
def lid_print():
    im = photo("外壳机盖.webp").crop(LID_BOX)
    return shadowed(im.resize((int(im.width * LID_SCALE), int(im.height * LID_SCALE)), Image.LANCZOS))


def lid_xy(pt):
    return CASE_POS[0] + (pt[0] - LID_BOX[0]) * LID_SCALE, CASE_POS[1] + (pt[1] - LID_BOX[1]) * LID_SCALE


def s_lid(c, t):
    card, pad = lid_print()
    paste(c, card, CASE_POS[0] - pad, CASE_POS[1] - pad)
    case_title(c, 9.0)
    for k, (anc, end, title, side, accent) in enumerate(LID_ANNOTS):
        annot(c, t - 0.4 - 0.15 * k, lid_xy(anc), end, title, None, side, accent, on_photo=True)
    for k, (pt, name) in enumerate(LID_BUTTONS):
        x, y = lid_xy(pt)
        p = ease_out(prog(t, 1.3 + 0.12 * k, 1.6 + 0.12 * k))
        strip(c, name, x, y + 30 + (1 - p) * 8, size=24, weight=600, pad=(10, 6), anchor="mt", alpha=p)


# 整机正面亮灯照片裁掉右上角路人；坐标为整机_正面亮灯.jpg 原图像素，左侧标签右对齐 650，右侧左对齐 1712
LIT_BOX = (0, 0, 1180, 960)
LIT_POS, LIT_SCALE = (680, 120), 820 / 960
LIT_ANNOTS = (
    ((622, 335), (650, 420), "ToF 测距", "l"),
    ((375, 405), (650, 540), "补光灯", "l"),
    ((580, 838), (650, 860), "十字激光投影", "l"),
    ((910, 130), (1712, 160), "声光报警灯", "r"),
    ((675, 405), (1712, 330), "双目相机", "r"),
    ((882, 437), (1712, 520), "补光灯", "r"),
)


@lru_cache(maxsize=None)
def lit_print():
    im = photo("整机_正面亮灯.jpg").crop(LIT_BOX)
    return shadowed(im.resize((int(im.width * LIT_SCALE), int(im.height * LIT_SCALE)), Image.LANCZOS))


def s_lit(c, t):
    card, pad = lit_print()
    a = ease_out(prog(t, 0, 0.5))
    paste(c, card, LIT_POS[0] - pad, LIT_POS[1] - pad + (1 - a) * 16, a)
    rise(c, "整机成品", MARGIN - 4, 300, 78, 800, p=ease_out(prog(t, 0.15, 0.65)))
    text(c, "补光与十字激光辅助拍摄", MARGIN, 358, size=30, weight=500, color=INK2, alpha=ease_out(prog(t, 0.35, 0.8)))
    for k, (anc, end, title, side) in enumerate(LIT_ANNOTS):
        x = LIT_POS[0] + (anc[0] - LIT_BOX[0]) * LIT_SCALE
        y = LIT_POS[1] + (anc[1] - LIT_BOX[1]) * LIT_SCALE
        annot(c, t - 0.5 - 0.15 * k, (x, y), end, title, None, side, on_photo=True)


CARRIER_RECT = (MARGIN, 160, MARGIN + 740, 900)


@lru_cache(maxsize=None)
def carrier_imgs():
    size = (CARRIER_RECT[2] - CARRIER_RECT[0], CARRIER_RECT[3] - CARRIER_RECT[1])
    render = Image.open(PCB_MAIN).convert("RGB").crop((398, 252, 1368, 1222)).resize(size, Image.LANCZOS)
    real = photo("主载板_正面.jpg").crop((106, 315, 1402, 1602)).resize(size, Image.LANCZOS)
    out = []
    for im in (render, real):
        im = im.convert("RGBA")
        im.putalpha(rounded_mask(size[0], size[1], 30))
        out.append(im)
    return out


def s_carrier(c, t):
    render, real = carrier_imgs()
    a = ease_out(prog(t, 0, 0.5))
    x0, y0, x1, y1 = CARRIER_RECT
    board_shadow(c, x0, y0, x1 - x0, y1 - y0, a)
    wipe_board(c, render, real, x0, y0, ease_in_out(prog(t, 0.75, 1.9)), a)
    da = ease_out(prog(t, 0.3, 0.8))
    dimension(c, (x0, y0 - 36), (x1, y0 - 36), "100 mm", da)
    dimension(c, (x1 + 36, y0), (x1 + 36, y1), "100 mm", da, vertical=True)
    side_text(c, t, 1060, "自研电路板", "主载板",
              (("供电", "12V 输入与多路 DC-DC"), ("监测", "功率监测与实时时钟"), ("协处理", "Arduino Nano ESP32")))


PH9_BOX = (130, 400, 1400, 1650)
PH9_POS = (MARGIN - 40, 140)
PH9_SCALE = 820 / (PH9_BOX[3] - PH9_BOX[1])
# 位号只标在照片上；右侧文字写外设名称与作用
CONN_GROUPS = (
    ("主控互连", "排线连接 AX7010 和 ESP32 协处理器",
     (("J801", (330, 1000), "l"), ("J604 J606", (567, 1242), "b"))),
    ("测量传感器", "测距、姿态、加速度计、温湿度和麦克风",
     (("J701", (750, 565), "t"), ("J702", (1162, 1088), "r"), ("J703", (1162, 1217), "r"),
      ("J704", (279, 671), "l"), ("J706", (258, 1375), "l"))),
    ("执行与报警", "补光灯、十字激光、报警灯和风扇",
     (("J301", (837, 1458), "b"), ("J304", (950, 1458), "b"), ("J302", (608, 1458), "b"))),
    ("交互与存储", "前面板、扬声器、NFC、打印机和语音卡",
     (("J502", (608, 560), "t"), ("J601", (358, 565), "t"), ("J602", (462, 560), "t"),
      ("J401", (375, 1478), "l"), ("J603", (1088, 750), "r"))),
    ("电源", "12V 输入、总开关和两路 5V 输出",
     (("J101", (1130, 1360), "r"), ("J102", (1067, 1458), "b"), ("J501", (875, 620), "t"),
      ("J201", (1033, 620), "t"))),
)


@lru_cache(maxsize=None)
def wired_print():
    bx = PH9_BOX
    size = (int((bx[2] - bx[0]) * PH9_SCALE), int((bx[3] - bx[1]) * PH9_SCALE))
    return shadowed(photo("主载板_外设连接.webp").crop(bx).resize(size, Image.LANCZOS))


def s_connect(c, t):
    card, pad = wired_print()
    a = ease_out(prog(t, 0, 0.5))
    paste(c, card, PH9_POS[0] - pad, PH9_POS[1] - pad + (1 - a) * 14, a)
    lx = 1060
    p = ease_out(prog(t, 0.15, 0.6))
    rise(c, "外设连接", lx - 3, 252, 54, 800, p=p)
    step = 0.78
    t_all = 0.55 + step * len(CONN_GROUPS)
    for k, (name, members, items) in enumerate(CONN_GROUPS):
        tk = 0.55 + step * k
        rev = ease_out(prog(t, tk, tk + 0.3))
        if rev <= 0:
            continue
        active = prog(t, tk, tk + 0.15) * (1 - prog(t, tk + step, tk + step + 0.15))
        y = 362 + 108 * k
        nw = text_w(name, 30, 700)
        if active > 0.01:
            fill_rect(c, (lx - 6, y - 32, lx + nw + 6, y + 7), YELLOW, active * rev)
        text(c, name, lx, y, size=30, weight=700, color=INK if active > 0.5 or t > t_all else INK2, alpha=rev)
        text(c, members, lx, y + 42, size=26, color=INK2, alpha=rev)
        for j, (des, pt, side) in enumerate(items):
            tl = t - tk - 0.05 * j
            if tl <= 0:
                continue
            sx = PH9_POS[0] + (pt[0] - PH9_BOX[0]) * PH9_SCALE
            sy = PH9_POS[1] + (pt[1] - PH9_BOX[1]) * PH9_SCALE
            pd = ease_back(prog(tl, 0, 0.25))
            circle(c, sx, sy, 7.5 * pd, fill=WHITE + (255,))
            circle(c, sx, sy, 5 * pd, fill=(YELLOW if active > 0.5 else INK) + (255,))
            off = {"t": (0, -28, "mb"), "b": (0, 28, "mt"), "l": (-22, 0, "rm"), "r": (22, 0, "lm")}[side]
            strip(c, des, sx + off[0], sy + off[1], size=20, weight=600, kind="cond",
                  bg=YELLOW if active > 0.5 else WHITE, pad=(8, 5), anchor=off[2], alpha=clamp(pd))


PANEL_RECT = (1000, 270, 1780, 791)


@lru_cache(maxsize=None)
def panel_imgs():
    size = (PANEL_RECT[2] - PANEL_RECT[0], PANEL_RECT[3] - PANEL_RECT[1])
    render = Image.open(PCB_PANEL).convert("RGB").crop((210, 272, 1600, 1200)).resize(size, Image.LANCZOS)
    real = photo("前面板.jpg").rotate(90, expand=True).crop((646, 436, 1688, 1120)).resize(size, Image.LANCZOS)
    out = []
    for im in (render, real):
        im = im.convert("RGBA")
        im.putalpha(rounded_mask(size[0], size[1], 58))
        out.append(im)
    return out


def s_panel(c, t):
    render, real = panel_imgs()
    a = ease_out(prog(t, 0, 0.45))
    x0, y0, x1, y1 = PANEL_RECT
    board_shadow(c, x0, y0, x1 - x0, y1 - y0, a)
    wipe_board(c, render, real, x0, y0, ease_in_out(prog(t, 0.55, 1.45)), a)
    dimension(c, (x0, y0 - 36), (x1, y0 - 36), "60 mm", ease_out(prog(t, 0.25, 0.7)))
    dimension(c, (x1 + 36, y0), (x1 + 36, y1), "40 mm", ease_out(prog(t, 0.25, 0.7)), vertical=True)
    side_text(c, t, MARGIN, "自研电路板", "前面板",
              (("指示", "5 颗 RGB 状态灯"), ("输入", "旋转编码器与按键"), ("连接", "排线连接主载板")), t0=0.2)


AX_ANNOTS = (
    ((1170, 842), (1290, 470), "Zynq-7010 芯片", "r"),
    ((380, 830), (470, 470), "AN5642 双目相机", "r"),
    ((1725, 800), (1600, 330), "扩展口连主载板", "l"),
    ((1370, 1192), (1480, 860), "HDMI 接触摸屏", "r"),
    ((1890, 1160), (1760, 760), "PDM 麦克风", "l"),
)


def s_ax7010(c, t):
    im = photo("AX7010_双目相机.jpg")
    if t < 3.2:
        cw, cx, cy = 2000 - 80 * t / 3.2, 1000, 752
    else:
        p = ease_in_out(prog(t, 3.2, 5.1))
        cw, cx, cy = lerp(1920, 640, p), lerp(1000, 1170, p), lerp(752, 842, p)
    frame, xf = view(im, cx, cy, cw)
    c.paste(frame, (0, 0))
    fade = 1 - prog(t, 2.9, 3.3)
    s0 = W / 2000
    for k, (anc, end, title, side) in enumerate(AX_ANNOTS):
        ax, ay = to_screen(anc, xf)
        bx, by = to_screen(anc, (0, 752 - 562.5, s0))
        annot(c, t - (0.3 + 0.24 * k), (ax, ay), (end[0] + ax - bx, end[1] + ay - by), title, None, side, alpha=fade)
    strip(c, "AX7010 开发板", W - 72, 52, size=32, weight=700, bg=RED, fg=WHITE, pad=(20, 10), anchor="rt",
          reveal=ease_out(prog(t, 0.1, 0.5)))


# 与 2026-11-03 代码包一致：双目匹配已移到 PS 软件后端，PL 保留视频、PDM、灯光与打印 UART
ARCH_PL = ("双路相机采集", "HDMI 显示", "PDM 麦克风接收", "灯光与打印接口")
ARCH_PS = ("多点亚像素跟踪", "频谱与基线判定", "双目辅助测距", "界面、记录与报警")
ARCH_ESP = ("NFC 读卡", "SD 语音播报", "Wi-Fi 网关")
ARCH_OUT = ("7 寸触摸屏", "声光报警灯", "热敏打印机")
MOD_Y = (290, 400, 510, 620)
# APP 的 Android、iOS、Windows 版共用一套界面，可局域网直连 AX7010，也可连 ESP32 热点。
# 手机截图取自 iOS 构建验证；电脑截图是同一 web 界面演示模式在 1440×900 窗口下的渲染
APP_PHONE = REPO / "05_验证资料/APP验证/构建验证/ios_home.png"
APP_DESKTOP = PROJ / "素材" / "APP" / "电脑端_总览.png"


@lru_cache(maxsize=None)
def phone_card(w=92):
    shot = Image.open(APP_PHONE).convert("RGB")
    h = round(w * shot.height / shot.width)
    card = box_img(w, h, fill=INK + (255,), r=14).copy()
    scr = shot.resize((w - 6, h - 6), Image.LANCZOS).convert("RGBA")
    scr.putalpha(rounded_mask(w - 6, h - 6, 11))
    card.alpha_composite(scr, (3, 3))
    return card


@lru_cache(maxsize=None)
def monitor_card(sw=256):
    """显示器外框 + 底座；返回图像和屏幕部分的高度。"""
    shot = Image.open(APP_DESKTOP).convert("RGB")
    sh = round(sw * shot.height / shot.width)
    w, h = sw + 12, sh + 12
    out = Image.new("RGBA", (w, h + 20), (0, 0, 0, 0))
    out.alpha_composite(box_img(w, h, fill=INK + (255,), r=6), (0, 0))
    out.alpha_composite(shot.resize((sw, sh), Image.LANCZOS).convert("RGBA"), (6, 6))
    out.alpha_composite(box_img(16, 14, fill=INK2 + (255,)), (w // 2 - 8, h))
    out.alpha_composite(box_img(96, 6, fill=INK2 + (255,)), (w // 2 - 48, h + 14))
    return out, h


def block(c, x, y, w, h, title, sub, state, alpha=1.0, size=26):
    """state：0 弱化，1 正常，2 当前高亮。"""
    fill = YELLOW if state >= 1.5 else WHITE
    edge = INK if state >= 0.5 else RULE
    paste(c, box_img(w, h, fill=fill + (255,), outline=edge + (255,), width=2), x, y, alpha)
    col = INK if state >= 0.5 else INK3
    if sub:
        text(c, title, x + 20, y + 40, size=27, weight=600, color=col, alpha=alpha)
        text(c, sub, x + 20, y + 73, size=22, color=INK2 if state >= 0.5 else INK3, alpha=alpha)
    else:
        text(c, title, x + 20, y + h / 2, size=size, weight=600, color=col, alpha=alpha, anchor="lm")


def token(c, pts, t, alpha, period=0.8, speed=300):
    length = sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))
    dur = max(length / speed, 0.05)
    for k in range(int(max(dur, period) / period) + 1):
        f = ((t - k * period) % max(dur, period)) / dur
        if f <= 1:
            x, y = point_on(pts, f)
            fill_rect(c, (x - 4, y - 4, x + 4, y + 4), INK, alpha * math.sin(math.pi * f) ** 0.4)


def s_arch(c, t):
    intro = ease_out(prog(t, 0, 0.6))

    def win(a, b):
        return prog(t, a, a + 0.25) * (1 - prog(t, b, b + 0.25))

    final = prog(t, 11.5, 11.8)
    on = {"pl": max(win(0.3, 3.7), final), "ps": max(win(3.8, 7.0), final),
          "esp": max(win(7.1, 9.3), final), "app": max(win(9.4, 11.6), final)}
    early = 1 - prog(t, 0.2, 0.45)

    def state(group, t0):
        if t >= 11.5:
            return 1
        lit = on[group] * prog(t, t0, t0 + 0.2)
        return 2 if lit > 0.5 else (1 if on[group] > 0.5 or early > 0.5 else 0)

    def dim(group):
        return intro * (1.0 if on[group] > 0.5 or early > 0.5 or t >= 11.5 else 0.55)

    paste(c, box_img(840, 720, outline=INK + (255,), width=2), 330, 150, intro)
    text(c, "XC7Z010  Zynq-7000 SoC", 352, 190, size=26, weight=600, kind="cond", alpha=intro)
    for group, (x0, x1), label, sub in (("pl", (352, 730), "PL", "可编程逻辑"),
                                        ("ps", (772, 1150), "PS", "双核 ARM 运行 Linux")):
        ga = dim(group)
        dashed_box(c, (x0, 215, x1, 850), INK2 + (255,), 1, ga, 6, 5)
        w0 = text(c, label, x0 + 18, 257, size=28, weight=700, kind="cond", alpha=ga)
        text(c, sub, x0 + 30 + w0, 257, size=22, color=INK2, alpha=ga)
    for k, tt in enumerate(ARCH_PL):
        block(c, 370, MOD_Y[k], 340, 96, tt, None, state("pl", 0.5 + 0.35 * k), intro, 30)
    for k, tt in enumerate(ARCH_PS):
        block(c, 790, MOD_Y[k], 340, 96, tt, None, state("ps", 4.0 + 0.35 * k), intro, 30)
    polyline(c, [(741, 290), (741, 716)], INK + (255,), 2, intro)
    polyline(c, [(761, 290), (761, 716)], INK + (255,), 2, intro)
    rot_text(c, "AXI", 751, 752, 20, 600, "cond", INK2, intro)
    for y in (338, 558):
        arrow(c, (712, y), (739, y), alpha=intro, head=7)
    for y in (448, 668):
        arrow(c, (739, y), (714, y), alpha=intro, head=7)
    for y in (338, 448, 558, 668):
        arrow(c, (763, y), (788, y), alpha=intro, head=7, both=True)
    for name, y, box in (("双目相机", 290, (0, 520, 330, 850)), ("麦克风", 510, (1817, 1100, 1967, 1250))):
        st = 2 if on["pl"] > 0.5 and 0.5 < t < 11.5 else (1 if on["pl"] > 0.5 or early > 0.5 or t >= 11.5 else 0)
        paste(c, box_img(260, 96, fill=WHITE + (255,), outline=(INK if st else RULE) + (255,), width=2), 40, y, intro)
        paste(c, thumb("AX7010_双目相机.jpg", box, 64), 56, y + 16, intro * (1 if st else 0.5))
        text(c, name, 138, y + 48, size=27, weight=600, color=INK if st else INK3, alpha=intro, anchor="lm")
        arrow(c, (300, y + 48), (368, y + 48), alpha=intro, head=8)
    # 右列自上而下：多端 APP、ESP32 协处理器、输出
    aa = dim("app")
    dashed_box(c, (1230, 150, 1870, 470), INK2 + (255,), 1, aa, 6, 5)
    w0 = text(c, "APP", 1252, 200, size=30, weight=700, kind="cond", alpha=aa)
    text(c, "手机与电脑多端", 1266 + w0, 200, size=24, color=INK2, alpha=aa)
    sa = state("app", 9.6)
    (mon, mon_h), ph = monitor_card(), phone_card()
    for im, x, hh, label in ((ph, 1346, ph.height, "手机"), (mon, 1486, mon_h, "电脑")):
        y = 420 - im.height
        paste(c, im, x, y, intro * (1 if sa else 0.45))
        if sa >= 1.5:
            paste(c, box_img(im.width + 12, hh + 12, outline=YELLOW + (255,), width=3), x - 6, y - 6, intro)
        text(c, label, x + im.width / 2, 456, size=24, weight=600, color=INK if sa else INK3, alpha=intro,
             anchor="ms")
    ea = dim("esp")
    dashed_box(c, (1230, 500, 1870, 650), INK2 + (255,), 1, ea, 6, 5)
    paste(c, thumb("主载板_正面.jpg", (320, 700, 540, 920), 44), 1250, 514, ea)
    w0 = text(c, "ESP32", 1306, 548, size=30, weight=700, kind="cond", alpha=ea)
    text(c, "协处理器", 1318 + w0, 548, size=24, color=INK2, alpha=ea)
    for k, name in enumerate(ARCH_ESP):
        block(c, 1250 + 205 * k, 572, 190, 56, name, None, state("esp", 7.3 + 0.3 * k), intro, 24)
    oa = dim("ps")
    dashed_box(c, (1230, 680, 1870, 810), INK2 + (255,), 1, oa, 6, 5)
    text(c, "输出", 1252, 718, size=24, weight=600, color=INK2, alpha=oa)
    for k, name in enumerate(ARCH_OUT):
        block(c, 1250 + 205 * k, 732, 190, 56, name, None, state("ps", 6.0 + 0.2 * k), intro, 24)
    # 连线：PS 经局域网连 APP、经 UART 连 ESP32、驱动输出；ESP32 热点连 APP
    arrow(c, (1154, 300), (1228, 300), alpha=intro, head=8, both=True)
    text(c, "局域网", 1200, 288, size=18, weight=600, color=INK2, alpha=intro, anchor="ms")
    arrow(c, (1154, 600), (1228, 600), alpha=intro, head=8, both=True)
    text(c, "UART", 1200, 588, size=18, weight=600, kind="cond", color=INK2, alpha=intro, anchor="ms")
    arrow(c, (1154, 760), (1228, 760), alpha=intro, head=8)
    dashed(c, [(1755, 482), (1755, 560)], INK + (255,), 2, 7, 5, intro)
    arrow(c, (1755, 488), (1755, 474), alpha=intro, head=8)
    arrow(c, (1755, 554), (1755, 568), alpha=intro, head=8)
    text(c, "Wi-Fi", 1768, 526, size=18, weight=600, kind="cond", color=INK2, alpha=intro, anchor="lm")
    if on["pl"] > 0.05:
        token(c, [(300, 338), (370, 338)], t, on["pl"], 0.5, 160)
        token(c, [(710, 338), (741, 338), (761, 338), (790, 338)], t - 0.2, on["pl"], 0.5, 200)
        token(c, [(710, 558), (751, 558), (751, 448), (790, 448)], t, on["pl"], 0.7, 260)
        token(c, [(300, 558), (370, 558)], t - 0.1, on["pl"], 0.5, 160)
    if on["ps"] > 0.05:
        for y0 in (386, 496, 606):
            token(c, [(960, y0), (960, y0 + 14)], t, on["ps"], 0.3, 40)
        token(c, [(1154, 760), (1228, 760)], t, on["ps"], 0.45, 220)
        token(c, [(790, 668), (751, 668), (710, 668)], t - 0.2, on["ps"], 0.5, 200)
    if on["esp"] > 0.05:
        token(c, [(1154, 600), (1228, 600)], t, on["esp"], 0.45, 220)
    if on["app"] > 0.05:
        token(c, [(1154, 300), (1228, 300)], t, on["app"], 0.45, 220)
        token(c, [(1755, 568), (1755, 474)], t - 0.2, on["app"], 0.45, 160)


PL_VIDEO = ("双目相机输入", "采集与格式转换", "VDMA 读写 DDR", "PS 测振与界面", "视频时序", "HDMI 输出")
PL_CUSTOM = (("PDM 麦克风接收", "2 MHz 采样，带时间戳", True),
             ("灯光控制", "PWM 调光，失联或掉电自动关断", True),
             ("打印串口", "AXI UART16550 硬件流控", False))
# 当前上板位流 printer_uart 的 Vivado 2025.2 实现报告（2026-11-03 代码包 ax7010/vivado/current/reports_original_build）：
# LUT 8,945/17,600，FF 14,420/35,200，BRAM 30.5/60，WNS +0.756 ns
PL_STATS = (("LUT", 50.8, "%", 0.508), ("触发器", 41.0, "%", 0.410), ("BRAM", 50.8, "%", 0.508),
            ("时序裕量", 0.756, "ns", None))


def s_pl(c, t):
    rise(c, "PL 硬件设计", MARGIN - 3, 206, 58, 800, p=ease_out(prog(t, 0.05, 0.55)))
    text(c, "视频通路", MARGIN, 290, size=26, weight=600, color=INK2, alpha=ease_out(prog(t, 0.2, 0.6)))
    nw, gap, ny, nh = 238, 34, 314, 90
    xs = [MARGIN + k * (nw + gap) for k in range(len(PL_VIDEO))]
    head = ease_in_out(prog(t, 0.6, 3.6)) * (len(PL_VIDEO) - 0.001)
    for k, tt in enumerate(PL_VIDEO):
        na = ease_out(prog(t, 0.1 + 0.06 * k, 0.5 + 0.06 * k))
        lit = 2 if 0 < head and int(head) == k and t < 3.65 else 1
        block(c, xs[k], ny, nw, nh, tt, None, lit, na)
        if k < len(PL_VIDEO) - 1:
            arrow(c, (xs[k] + nw + 2, ny + nh / 2), (xs[k + 1] - 2, ny + nh / 2), alpha=na, head=7)
    text(c, "外设与自研逻辑", MARGIN, 458, size=26, weight=600, color=INK2, alpha=ease_out(prog(t, 3.7, 4.1)))
    cw, cg, cy, ch = 520, 20, 480, 90
    for k, (tt, ss, own) in enumerate(PL_CUSTOM):
        p = ease_out(prog(t, 3.8 + 0.18 * k, 4.3 + 0.18 * k))
        x = MARGIN + k * (cw + cg)
        lit = 2 if own and 4.2 < t < 5.6 else 1
        block(c, x, cy + (1 - p) * 12, cw, ch, tt, ss, lit, p)
        if own:
            strip(c, "自研 RTL", x + cw - 12, cy + 14 + (1 - p) * 12, size=21, weight=700, bg=INK, fg=YELLOW,
                  pad=(10, 5), anchor="rt", alpha=p)
    text(c, "资源与时序", MARGIN, 640, size=26, weight=600, color=INK2, alpha=ease_out(prog(t, 4.6, 5.0)))
    cols = [MARGIN + k * 404 for k in range(4)]
    for k in range(1, 4):
        p = ease_in_out(prog(t, 4.7 + 0.08 * k, 5.2 + 0.08 * k))
        polyline(c, [(cols[k] - 30, 672), (cols[k] - 30, 672 + 172 * p)], RULE + (255,), 1)
    cnt = ease_out(prog(t, 4.9, 6.2))
    for k, (label, val, unit, frac) in enumerate(PL_STATS):
        p = ease_out(prog(t, 4.8 + 0.12 * k, 5.3 + 0.12 * k))
        x = cols[k]
        text(c, label, x, 706, size=28, weight=500, color=INK2, alpha=p)
        s = f"{val * cnt:.3f}" if unit == "ns" else f"{val * cnt:.1f}"
        vw = text(c, s, x - 2, 796, size=82, weight=600, kind="num", alpha=p)
        text(c, unit, x + vw + 8, 796, size=30, weight=500, kind="num", color=INK2, alpha=p)
        if frac:
            fill_rect(c, (x, 818, x + 300, 825), RULE, p)
            fill_rect(c, (x, 818, x + 300 * frac * cnt, 825), INK, p)
    fade = prog(t, 7.5, 8.2)
    if fade > 0:
        fill_rect(c, (0, 0, W, H), (0, 0, 0), ease_in_out(fade))


# ---------- 上电实测 ----------

def live(name, F):
    """取全景原片时间 F 时刻的素材帧。"""
    frames = LIVE_FRAMES.get(name)
    if not frames:
        return Image.new("RGB", (1280, 720), PANEL)
    i = int(round((F - LIVE_CLIPS[name][1]) * FPS))
    return Image.open(frames[max(0, min(i, len(frames) - 1))]).convert("RGB")


def lerp_box(a, b, p):
    return tuple(lerp(u, v, p) for u, v in zip(a, b))


@lru_cache(maxsize=24)
def panel_shadow(w, h):
    return shadowed(Image.new("RGBA", (w, h), (0, 0, 0, 0)), blur=22, offset=14, alpha=0.28, pad=60)


def round_mask(w, h, r):
    m = Image.new("L", (w * 2, h * 2), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * 2 - 1, h * 2 - 1), radius=r * 2, fill=255)
    return m.resize((w, h), Image.LANCZOS)


def card(c, im, crop, center, size, angle=0.0, radius=0, alpha=1.0):
    """素材帧的 crop 区域缩放成 size 大小的卡片，带阴影，可绕中心逆时针旋转 angle 度。"""
    w, h = max(2, int(round(size[0]))), max(2, int(round(size[1])))
    img = im.resize((w, h), Image.LANCZOS, box=crop).convert("RGBA")
    if radius:
        img.putalpha(round_mask(w, h, radius))
    sh, pad = panel_shadow(w, h)
    cx, cy = center
    if abs(angle) < 0.5:
        paste(c, sh, cx - w / 2 - pad, cy - h / 2 - pad, alpha)
        paste(c, img, cx - w / 2, cy - h / 2, alpha)
        return
    comp = sh.copy()
    comp.alpha_composite(img, (pad, pad))
    rot = comp.rotate(angle, resample=Image.BICUBIC, expand=True)
    paste(c, rot, cx - rot.width / 2, cy - rot.height / 2, alpha)


# 一镜到底：三路画面共用全景原片时间 F，分段变速，不剪切（素材内时间 = F − 2.5）
LIVE_SEGMENTS = (  # (F 起, F 止, 倍速, 布局, 保留原声)
    (2.6, 6.5, 1.0, "intro", False),
    (6.5, 22.5, 3.0, "baseline", False),
    (22.5, 28.2, 1.0, "voice", True),
    (28.2, 37.6, 2.0, "measure", False),
    (37.6, 45.0, 1.0, "alarm", True),
    (45.0, 52.0, 1.0, "result", False),
    (52.0, 67.4, 3.0, "result", False),
    (67.4, 69.2, 0.5, "receipt", False),
)
LIVE_RAMP = 0.5  # 段间速度在此时长内对称线性过渡，各段起止的 F 不变


def live_times():
    t = [0.0]
    for f0, f1, v, *_ in LIVE_SEGMENTS:
        t.append(t[-1] + (f1 - f0) / v)
    return t


LIVE_T = live_times()
LIVE_DUR = LIVE_T[-1]


def live_seg(t):
    return min(len(LIVE_SEGMENTS) - 1, max(i for i in range(len(LIVE_SEGMENTS)) if LIVE_T[i] <= t))


def live_F(t):
    """场景内时间 t → 全景原片时间 F。"""
    t = clamp(t, 0, LIVE_DUR)
    k, r = live_seg(t), LIVE_RAMP
    f0, _, v = LIVE_SEGMENTS[k][:3]
    F = f0 + v * (t - LIVE_T[k])
    for j in (k, k + 1):
        if 0 < j < len(LIVE_SEGMENTS) and abs(t - LIVE_T[j]) < r / 2:
            va, vb = LIVE_SEGMENTS[j - 1][2], LIVE_SEGMENTS[j][2]
            tau = t - LIVE_T[j] + r / 2
            F = LIVE_SEGMENTS[j][0] - va * r / 2 + va * tau + (vb - va) * tau * tau / (2 * r)
    return F


# 各布局：全景卡高、屏幕卡高、APP 卡高（画面像素）、屏幕放大（0 宽景 / 1 特写）、屏幕卡旋转角、全景裁切
LIVE_LAYOUT = {
    "intro": (585, 205, 640, 0.0, 0.0, (0, 0, 1920, 1080)),
    "baseline": (470, 320, 810, 0.0, 0.0, (300, 0, 1920, 911)),
    "voice": (395, 395, 640, 0.0, 0.0, (600, 20, 1920, 762)),
    "measure": (205, 585, 700, 1.0, 0.0, (600, 20, 1920, 762)),
    "alarm": (585, 205, 700, 0.0, 0.0, (240, 0, 1920, 945)),
    "result": (205, 585, 810, 1.0, 0.0, (600, 20, 1920, 762)),
    "receipt": (120, 570, 560, 1.0, 90.0, (600, 20, 1920, 762)),
}


def live_layout(t):
    names = [s[3] for s in LIVE_SEGMENTS]
    cur = LIVE_LAYOUT[names[0]]
    for j in range(1, len(names)):
        if names[j] == names[j - 1]:
            continue
        pre, post = (2.2, 0.2) if names[j] == "receipt" else (0.35, 0.55)
        p = ease_in_out(prog(t, LIVE_T[j] - pre, LIVE_T[j] + post))
        if p <= 0:
            break
        nxt = LIVE_LAYOUT[names[j]]
        cur = tuple(lerp_box(a, b, p) if isinstance(a, tuple) else lerp(a, b, p) for a, b in zip(cur, nxt))
    return cur


# 屏幕录像中 7 寸屏中心随手持机位漂移（检测深蓝界面得到，素材像素，按 F 插值）
SCREEN_TRACK = ((2.5, 860, 192), (6.5, 840, 212), (12.5, 834, 222), (20.5, 832, 236), (26.5, 836, 248),
                (30.5, 824, 256), (36.5, 836, 244), (42.5, 824, 242), (46.5, 816, 232), (50.5, 812, 250),
                (52.5, 784, 294), (56.5, 774, 290), (62.5, 782, 278), (66.5, 771, 294))
RCPT = (430, 0, 1230, 680)  # 收据在横屏素材中的区域，卡片转 90° 后收据正向
APP_CROP = (0, 110, 852, 1830)  # APP 录屏去掉状态栏
LIVE_TARGET, LIVE_UNIT, LIVE_LAMP = (1015, 655), (1334, 360), (1525, 250)
CLOCK0 = 2 * 3600 + 5 * 60 + 10.6 - 28.4  # 全景 F=28.4 s 对应记录里的开始测量时刻 02:05:10.6


def fit_box(b, w, h):
    """平移裁切框，使其落在画面内。"""
    x0, y0, x1, y1 = b
    dx = -x0 if x0 < 0 else (w - x1 if x1 > w else 0)
    dy = -y0 if y0 < 0 else (h - y1 if y1 > h else 0)
    return x0 + dx, y0 + dy, x1 + dx, y1 + dy


def screen_crop(F, zoom, turn):
    xs = [p[0] for p in SCREEN_TRACK]
    cx = float(np.interp(F, xs, [p[1] for p in SCREEN_TRACK]))
    cy = float(np.interp(F, xs, [p[2] for p in SCREEN_TRACK]))
    wide = fit_box((cx - 320, cy - 174, cx + 440, cy + 254), 1280, 720)
    tight = fit_box((cx - 235, cy - 132, cx + 235, cy + 132), 1280, 720)
    return lerp_box(lerp_box(wide, tight, zoom), RCPT, turn)


def s_live(c, t):
    F = live_F(t)
    hf, hs, ha, zoom, rot, fcrop = live_layout(t)
    scrop = screen_crop(F, zoom, rot / 90)
    wf, ws = hf * 16 / 9, hs * (scrop[2] - scrop[0]) / (scrop[3] - scrop[1])
    rad = math.radians(rot)
    fw = abs(ws * math.cos(rad)) + abs(hs * math.sin(rad))
    fh = abs(ws * math.sin(rad)) + abs(hs * math.cos(rad))
    wa = ha * (APP_CROP[2] - APP_CROP[0]) / (APP_CROP[3] - APP_CROP[1])
    colw = max(wf, fw)
    x0 = (W - colw - 60 - wa) / 2
    top = 540 - (hf + 20 + fh) / 2
    fbox = (x0, top, x0 + wf, top + hf)
    sbox = (x0, top + hf + 20, x0 + fw, top + hf + 20 + fh)
    abox = (x0 + colw + 60, 540 - ha / 2, x0 + colw + 60 + wa, 540 + ha / 2)
    card(c, live("full", F), fcrop, ((fbox[0] + fbox[2]) / 2, (fbox[1] + fbox[3]) / 2), (wf, hf))
    card(c, live("screen", F), scrop, ((sbox[0] + sbox[2]) / 2, (sbox[1] + sbox[3]) / 2), (ws, hs), rot)
    card(c, live("app", F), APP_CROP, ((abox[0] + abox[2]) / 2, 540), (wa, ha), radius=int(ha * 0.035))
    tag = dict(size=22, weight=600, bg=INK, fg=WHITE, pad=(10, 5))
    strip(c, "现场", fbox[0] + 12, fbox[1] + 12, **tag)
    strip(c, "7 寸屏", sbox[0] + 12, sbox[1] + 12, **tag)
    paste(c, box_img(13, 13, fill=YELLOW + (255,)), abox[0], abox[1] - 30)
    text(c, "手机 APP", abox[0] + 22, abox[1] - 23, size=24, weight=600, anchor="lm")
    badge = dict(size=22, weight=700, bg=YELLOW, pad=(10, 5), anchor="rt")
    strip(c, "语音播报", sbox[2] - 12, sbox[1] + 12, alpha=prog(F, 23.6, 23.9) * (1 - prog(F, 26.9, 27.3)), **badge)
    strip(c, "电机启动", fbox[2] - 12, fbox[1] + 12, alpha=prog(F, 25.7, 26.0) * (1 - prog(F, 36.6, 37.4)), **badge)

    def on_full(pt):
        return (fbox[0] + (pt[0] - fcrop[0]) * wf / (fcrop[2] - fcrop[0]),
                fbox[1] + (pt[1] - fcrop[1]) * hf / (fcrop[3] - fcrop[1]))

    fade = 1 - prog(t, LIVE_T[1] - 0.6, LIVE_T[1] - 0.35)
    annot(c, t - 0.5, on_full(LIVE_TARGET), (fbox[0] + 0.36 * wf, fbox[1] + 0.89 * hf), "振动目标", "电机带动散斑铝片",
          "l", alpha=fade)
    annot(c, t - 0.8, on_full(LIVE_UNIT), (fbox[0] + 0.85 * wf, fbox[1] + 0.10 * hf), "见微", None, "r", alpha=fade)
    k_alarm = [s[3] for s in LIVE_SEGMENTS].index("alarm")
    lamp = 1 - prog(t, LIVE_T[k_alarm + 1] - 0.6, LIVE_T[k_alarm + 1] - 0.35)
    annot(c, t - LIVE_T[k_alarm] - 0.7, on_full(LIVE_LAMP), (fbox[0] + 0.58 * wf, fbox[1] + 0.09 * hf), "声光报警",
          None, "l", alpha=lamp)
    # 右上角：现场时间与变速提示
    clk = int(CLOCK0 + F)
    cw, _ = strip(c, f"现场时间 {clk // 3600:02d}:{clk // 60 % 60:02d}:{clk % 60:02d}", W - 72, 52, size=24, weight=600,
                  border=RULE, anchor="rt")
    k = live_seg(t)
    v = LIVE_SEGMENTS[k][2]
    if v != 1:
        a = prog(t, LIVE_T[k], LIVE_T[k] + 0.3) * (1 - prog(t, LIVE_T[k + 1] - 0.3, LIVE_T[k + 1]))
        strip(c, f"{v:g}× {'快放' if v > 1 else '慢放'}", W - 72 - cw - 12, 52, size=24, weight=700, bg=YELLOW,
              anchor="rt", alpha=a)


def s_live_data(c, t):
    cnt = ease_out(prog(t, 0.4, 1.4))
    for k, (label, val, unit, note) in enumerate(LIVE_DATA):
        x, y = MARGIN + (k % 2) * 460, 400 + (k // 2) * 300
        a = ease_out(prog(t, 0.2 + 0.12 * k, 0.6 + 0.12 * k))
        text(c, label, x, y - 118, size=30, weight=500, color=INK2, alpha=a)
        digits = len(val.split(".")[1]) if "." in val else 0
        vw = text(c, f"{float(val) * cnt:.{digits}f}", x - 4, y, size=112, weight=700, kind="num", alpha=a)
        text(c, unit, x + vw + 12, y, size=44, weight=600, kind="num", color=INK2, alpha=a)
        text(c, note, x, y + 50, size=26, color=INK2, alpha=a)
    text(c, "数据来自见微 2026-11-04 02:05 测量记录", MARGIN, 900, size=24, color=INK2,
         alpha=ease_out(prog(t, 1.2, 1.6)))
    # 12 个测点的信号与停机基线之比
    x0, x1, y0, y1, vmax = 1150, 1780, 330, 800, 100.0
    a = ease_out(prog(t, 0.5, 0.9))
    text(c, "各测点信号与停机基线之比", x0, 282, size=30, weight=600, alpha=a)
    polyline(c, [(x0, y1), (x1, y1)], INK + (255,), 2, a)
    for v in (50, 100):
        yy = y1 - (y1 - y0) * v / vmax
        polyline(c, [(x0, yy), (x1, yy)], RULE + (255,), 1, a)
        text(c, str(v), x0 - 14, yy, size=22, color=INK2, alpha=a, anchor="rm")
    pitch = (x1 - x0) / len(LIVE_GATES)
    for k, v in enumerate(LIVE_GATES):
        p = ease_out(prog(t, 0.8 + 0.07 * k, 1.5 + 0.07 * k))
        bx = x0 + k * pitch + pitch * 0.18
        top = y1 - (y1 - y0) * v / vmax * p
        if y1 - top > 1:
            fill_rect(c, (bx, top, bx + pitch * 0.64, y1), YELLOW, a)
        text(c, str(k + 1), bx + pitch * 0.32, y1 + 28, size=20, color=INK2, alpha=a, anchor="mm")
    g = ease_in_out(prog(t, 2.0, 2.6))
    gy = y1 - (y1 - y0) * 3 / vmax
    if g > 0.01:
        dashed(c, [(x0, gy), (x0 + (x1 - x0) * g, gy)], RED + (255,), 2)
    text(c, "3 倍门限", x0 - 14, gy - 2, size=22, weight=600, color=RED, alpha=g, anchor="rm")


SUMMARY = (("非接触多点", "一台相机同时监测 12 个测点"), ("板上实时", "PL 负责采集与外设时序，PS 实时分析"),
           ("现场与远程", "声光报警、打印报告、手机电脑 APP"))


@lru_cache(maxsize=None)
def summary_photo():
    return shadowed(photo("整机_斜俯视.jpg").crop((120, 60, 1280, 930)).resize((720, 540), Image.LANCZOS))


def s_summary(c, t):
    card, pad = summary_photo()
    a = ease_out(prog(t, 0.1, 0.7))
    paste(c, card, 1060 - pad + (1 - a) * 30, 250 - pad, a)
    rise(c, "见微知著", MARGIN - 8, 360, 132, 900, kind="serif", p=ease_out(prog(t, 0.2, 0.9)))
    text(c, "从细微振动，预见设备健康", MARGIN, 440, size=40, weight=500, color=INK2, alpha=ease_out(prog(t, 0.6, 1.1)))
    for k, (head, body) in enumerate(SUMMARY):
        p = ease_out(prog(t, 1.2 + 0.3 * k, 1.7 + 0.3 * k))
        y = 570 + k * 104
        paste(c, box_img(16, 16, fill=YELLOW + (255,)), MARGIN, y - 22, p)
        text(c, head, MARGIN + 32, y, size=36, weight=700, alpha=p)
        text(c, body, MARGIN + 32, y + 42, size=26, color=INK2, alpha=p)


def s_end(c, t):
    p = ease_out(prog(t, 0.0, 0.8))
    w = int(860 * p)
    if w > 1:
        paste(c, title_photo().crop((860 - w, 0, 860, 1080)), W - w, 0)
    rise(c, "见微", MARGIN - 8, 520, 250, 900, p=ease_out(prog(t, 0.25, 0.9)), tracking=14)
    fill_rect(c, (MARGIN, 588, MARGIN + 92 * ease_in_out(prog(t, 0.8, 1.15)), 601), YELLOW)
    rise(c, "非接触  多点同测  现场报警", MARGIN, 686, 44, 500, p=ease_out(prog(t, 0.9, 1.4)))
    text(c, "基于 AMD Zynq-7010", MARGIN, 752, size=32, color=INK2, alpha=ease_out(prog(t, 1.2, 1.7)))
    fade = prog(t, 2.7, 3.5)
    if fade > 0:
        fill_rect(c, (0, 0, W, H), (0, 0, 0), ease_in_out(fade))


def stretched(fn, k):
    """把原有动画节奏放慢 k 倍，配合旁白时长。"""
    def f(c, t):
        fn(c, t / k)
    f.__name__ = fn.__name__
    return f


def live_t(F):
    """全景原片时间 F → 实测场景内时间（live_F 的反函数）。"""
    lo, hi = 0.0, LIVE_DUR
    for _ in range(40):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if live_F(mid) < F else (lo, mid)
    return lo


# ---------- 时间线 ----------

@lru_cache(maxsize=1)
def scenes():
    """(函数, 时长, 转场到下一镜, 章节, 字幕[(起, 止, 文本[, 旁白])，场景内时间])。
    止为 None 时字幕显示到该句旁白结束；文本为空的是只有旁白、不出字幕的句子。"""
    return (
        (s_hook, HOOK_DUR, 0.4, None, [(0.45, None, "", "它在振动吗？")]),
        (s_title, 3.2, 0.45, None, [(0.3, None, "", "见微，非接触视觉测振仪。")]),
        (s_industry, INDUSTRY_DUR, 0.4, "应用背景",
         [(0.2, None, "设备在故障早期，往往只有肉眼难以察觉的微小振动；"),
          (0.0, None, "若不及时处理，故障会进一步加剧，危及人身和财产安全。")]),
        (s_background, 16.0, 0.4, "应用背景",
         [(0.2, None, "接触式传感器要逐点安装布线，测点有限，还会改变轻薄结构的振动；"),
          (BG_LASER[0], None, "激光测振仪精度高，但价格昂贵，一次只能测一个点。"),
          (BG_CAM_SHIFT + 3.3, None, "见微用一台相机，非接触地同时测量 12 个点，"),
          (BG_CAM_SHIFT + 7.3, None, "扣除相机晃动、对照停机基线，在故障萌芽时就发出预警。")]),
        (s_principle, 8.0, 0.4, "测量原理",
         [(0.2, None, "原理是逐帧追踪表面散斑的亚像素位移，"), (0.0, None, "扣除背景晃动后做频谱，并与停机基线比对。")]),
        (s_overview, 3 * OV_BEAT, 0.4, "硬件与外设",
         [(0.2, None, "相机拍摄振动，多种传感器辅助对照和记录；"),
          (OV_BEAT + 0.1, None, "触摸屏、NFC 和扬声器负责人机交互；"),
          (2 * OV_BEAT + 0.1, None, "报警灯和打印机输出结果，激光和补光辅助拍摄。")]),
        (s_enclosure, 4.6, 0.6, "硬件与外设", [(0.3, None, "全部模块都装进了 3D 打印外壳，")]),
        (s_lid, 4.6, 0.4, "硬件与外设", [(0.2, None, "机盖上有屏幕、报警灯、NFC 和四个按键。")]),
        (s_lit, 5.5, 0.4, "硬件与外设", [(0.2, None, "补光灯照亮被测面；十字激光帮助对准，测量时自动关闭。")]),
        (s_carrier, 4.2, 0.4, "硬件与外设", [(0.2, None, "主载板从原理图到 PCB 全部自主设计，")]),
        (s_connect, 4.6, 0.4, "硬件与外设", [(0.1, None, "为十余种外设提供供电、保护和接口；")]),
        (s_panel, 4.0, 0.4, "硬件与外设", [(0.1, None, "前面板集成状态灯、旋钮和按键。")]),
        (s_ax7010, 4.2, 0.6, "硬件与外设", [(0.1, None, "核心是 AMD Zynq 芯片和双目相机。")]),
        (stretched(s_arch, 13.0 / 12.0), 13.0, 0.4, "系统架构",
         [(0.3, None, "PL 负责视频与外设时序，"), (4.0, None, "PS 运行测振算法、界面和报警，"),
          (7.5, None, "ESP32 负责 NFC、语音和联网，"), (10.2, None, "手机电脑 APP 经局域网或热点连接。")]),
        (stretched(s_pl, 9.0 / 8.2), 9.0, 0.5, "PL 设计",
         [(0.3, None, "PL 搭建双路视频与 HDMI 显示通路，"), (0.0, None, "自研麦克风接收和灯光保护逻辑，时序全部收敛。")]),
        (s_live, LIVE_DUR, 0.5, "上电实测",
         [(0.2, None, "电机带动贴有散斑的铝片振动，见微在一旁拍摄。"),
          (4.2, None, "电机停转时，先采集 15 秒停机基线；"),
          (live_t(23.7), live_t(27.2), "设备语音：“停机基线已采集，请开始测量。”"),
          (live_t(27.4), None, "随后开启电机，测量 10 秒，12 个测点同时跟踪；"),
          (live_t(41.2), live_t(44.5), "设备语音：“振动异常，请及时检查设备。”"),
          (live_t(44.7), None, "振动超过 0.02 mm 阈值，声光报警和手机同时响应；"),
          (0.0, None, "屏幕和手机给出主频 19.95 Hz、峰峰值 0.209 mm，"),
          (0.0, None, "热敏打印机当场打印测量报告。")]),
        (s_live_data, 9.0, 0.5, "实测结果",
         [(0.3, None, "麦克风测得的频率与视觉只差 0.02 Hz；"),
          (0.0, None, "12 个测点全部超过停机基线，452 帧无丢帧。")]),
        (s_summary, 7.0, 0.5, "总结",
         [(0.3, None, "见微知著：在故障萌芽时就看见它，"), (0.0, None, "让设备运行更安全，让维护更从容。")]),
        (s_end, 3.5, 0.0, None, []),
    )


def timeline():
    """各场景起点、字幕、章节、总长，以及旁白 [(时间线起点, 音频路径)]。
    旁白不早于给定起点，并接在上一句之后；旁白比画面长时场景随之延长（实测场景时长由素材决定，不延长）。"""
    starts, cues, sections, voices, t, prev = [], [], [], [], 0.0, 0.0
    for fn, dur, xf, sec, sc in scenes():
        starts.append(t)
        end = t + dur
        for cue in sc:
            a, b, s = t + cue[0], cue[1], cue[2]
            sp = speech_of(cue)
            clip = voice_log().get(sp) if sp else None
            if clip:
                a = max(a, prev + 0.15)
                voices.append((a, VOICE_DIR / clip["file"]))
                prev = a + clip["duration"]
                b = prev + 0.25 if b is None else t + b
                if fn is not s_live:
                    end = max(end, prev + 0.35)
            else:
                b = t + b if b is not None else a + 3.0
            if s:
                cues.append((a, b, s))
        if sec:
            if sections and sections[-1][2] == sec:
                sections[-1][1] = end
            else:
                sections.append([t, end, sec])
        t = end
    return starts, cues, sections, t, voices


def draw_scene(k, t):
    c = bg_canvas().copy()
    scenes()[k][0](c, t)
    return c


@lru_cache(maxsize=16)
def chapter_img(name):
    tw = text_w(name, 24, 600)
    w, h = int(tw + 58), 46
    im = box_img(w, h, fill=WHITE + (255,), outline=RULE + (255,), width=1).copy()
    im.alpha_composite(box_img(13, 13, fill=YELLOW + (255,)), (17, 16))
    text(im, name, 40, h / 2, size=24, weight=600, anchor="lm")
    return im


@lru_cache(maxsize=128)
def subtitle_img(s):
    tw = text_w(s, 40, 500)
    w, h = int(tw + 48), 68
    im = box_img(w, h, fill=(14, 16, 20, 168), r=6).copy()
    text(im, s, 24, h / 2, size=40, weight=500, color=WHITE, anchor="lm")
    return im


def overlays(c, T, cues, sections):
    for t0, t1, name in sections:
        if t0 <= T < t1:
            ai = ease_out(prog(T, t0 + 0.2, t0 + 0.6))
            im = chapter_img(name)
            rw = int(im.width * ai)
            if rw > 0:
                paste(c, im.crop((0, 0, rw, im.height)), 72, 52, 1 - prog(T, t1 - 0.35, t1))
    for t0, t1, s in cues:
        a = prog(T, t0 - 0.12, t0 + 0.06) * (1 - prog(T, t1 - 0.06, t1 + 0.12))
        if a > 0:
            im = subtitle_img(s)
            paste(c, im, W / 2 - im.width / 2, 1036 - im.height, a)


TL = None


def render_frame(i):
    global TL
    if TL is None:
        TL = timeline()
    starts, cues, sections, _, _ = TL
    T = i / FPS
    sc = scenes()
    k = max(j for j, s in enumerate(starts) if s <= T + 1e-9)
    c = draw_scene(k, T - starts[k])
    if k > 0 and T < starts[k] + sc[k - 1][2]:
        prev = draw_scene(k - 1, T - starts[k - 1])
        c = Image.blend(prev, c, ease_in_out((T - starts[k]) / sc[k - 1][2]))
    overlays(c, T, cues, sections)
    return c.convert("RGB")


def worker(i):
    return render_frame(i).tobytes()


def extract_frames(path, out_dir, dur, sharpen, speed=1.0):
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    vf = f"setpts=PTS/{speed},fps={FPS},scale={W}:{H}:flags=lanczos:force_original_aspect_ratio=increase,crop={W}:{H}"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", str(path), "-t", f"{dur:.3f}", "-vf",
                    vf + (",unsharp=5:5:0.45" if sharpen else ""), "-q:v", "2", str(Path(out_dir) / "%05d.jpg")],
                   check=True)
    return sorted(str(p) for p in Path(out_dir).glob("*.jpg"))


def extract_live(path, out_dir):
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", str(path), "-vf", f"fps={FPS}", "-q:v", "2",
                    str(Path(out_dir) / "%05d.jpg")], check=True)
    return sorted(str(p) for p in Path(out_dir).glob("*.jpg"))


def live_audio(starts):
    """一镜到底中原速并保留原声的片段 [(时间线起点, 全景原片 F, 时长)]，避开变速过渡区。"""
    t_live = starts[[s[0] for s in scenes()].index(s_live)]
    out, r, n = [], LIVE_RAMP, len(LIVE_SEGMENTS)
    for k, (f0, f1, v, _, keep) in enumerate(LIVE_SEGMENTS):
        if keep and v == 1:
            a, b = f0 + (r / 2 if k > 0 else 0), f1 - (r / 2 if k < n - 1 else 0)
            out.append((t_live + LIVE_T[k] + a - f0, a, b - a))
    return out


def narration_track(voices, total, path):
    """各句旁白按时间线拼成一条单声道 WAV，各句统一到相近响度。"""
    rate, buf = None, None
    for t0, p in voices:
        with wave.open(str(p)) as w:
            r, ch = w.getframerate(), w.getnchannels()
            x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768
        if ch == 2:
            x = x.reshape(-1, 2).mean(axis=1)
        if buf is None:
            rate, buf = r, np.zeros(int((total + 2) * r), np.float32)
        x *= 0.12 / (np.sqrt((x ** 2).mean()) + 1e-9)
        i = int(round(t0 * rate))
        m = min(len(x), len(buf) - i)
        buf[i:i + m] += x[:m]
    if buf is None:
        return False
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((np.clip(buf, -1, 1) * 32767).astype(np.int16).tobytes())
    return True


def audio_graph(total, starts, hook_in, live_in, narr_in):
    """钩子原声和实测原声（基线语音、报警声）作底，旁白响起时自动压低底声。"""
    fmt = "asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo"
    parts, labels = [], []
    if hook_in:
        parts.append(f"[{hook_in}:a]atrim=0:{HOOK_DUR:.3f},{fmt},volume=8dB,afade=t=in:d=0.15,"
                     f"afade=t=out:st={HOOK_DUR - 0.6:.3f}:d=0.6[h]")
        labels.append("[h]")
    for k, (t0, f0, dur) in enumerate(live_audio(starts)):
        parts.append(f"[{live_in}:a]atrim=start={f0 - LIVE_CLIPS['full'][1]:.3f}:duration={dur:.3f},{fmt},volume=16dB,"
                     f"afade=t=in:d=0.08,afade=t=out:st={dur - 0.4:.3f}:d=0.4,adelay={int(t0 * 1000)}:all=1[l{k}]")
        labels.append(f"[l{k}]")
    parts.append("".join(labels) + f"amix=inputs={len(labels)}:normalize=0:dropout_transition=0,"
                 f"apad=whole_dur={total:.3f}[bed]")
    if narr_in:
        parts.append(f"[{narr_in}:a]{fmt},apad=whole_dur={total:.3f},asplit=2[n1][n2]")
        parts.append("[bed][n1]sidechaincompress=threshold=0.02:ratio=6:attack=20:release=400[duck]")
        parts.append(f"[duck][n2]amix=inputs=2:normalize=0:dropout_transition=0,alimiter=limit=0.95,atrim=0:{total:.3f}[a]")
    else:
        parts.append(f"[bed]alimiter=limit=0.95,atrim=0:{total:.3f}[a]")
    return ";".join(parts)


def print_plan():
    """列出各场景实际起止与旁白时间；实测场景标出压到设备原声或超出场景的旁白。"""
    starts, cues, _, total, voices = timeline()
    by_file = {e["file"]: e for e in voice_log().values()}
    bounds = list(zip(starts, starts[1:] + [total]))
    k_live = [s[0] for s in scenes()].index(s_live)
    t_live = starts[k_live]
    device = [(t_live + live_t(a), t_live + live_t(b)) for a, b in ((23.7, 27.2), (41.2, 44.5))]
    for (fn, dur, *_), (a, b) in zip(scenes(), bounds):
        print(f"{a:7.2f}  {fn.__name__:16s} {b - a:5.2f}s（原定 {dur:.2f}s）")
        for v0, p in voices:
            if a <= v0 < b:
                e = by_file[p.name]
                v1 = v0 + e["duration"]
                flag = "".join(" 压到设备原声" for d0, d1 in device if v0 < d1 and v1 > d0)
                flag += " 超出实测场景" if fn is s_live and v1 > b else ""
                print(f"           {v0:7.2f}-{v1:7.2f} {e['text']}{flag}")
    print(f"总长 {total:.1f}s，旁白 {len(voices)} 句")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hook", default=str(HOOK_CLIP), help="开场实拍片段，取前 5 秒")
    ap.add_argument("--stills", nargs="*", type=float, help="只导出这些秒数的静帧")
    ap.add_argument("--still-dir", default=str(PROJ / "输出" / "静帧"))
    ap.add_argument("--out", default=str(PROJ / "输出" / "见微_作品视频.mp4"))
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--plan", action="store_true", help="只列出场景与旁白时间")
    args = ap.parse_args()
    if args.plan:
        print_plan()
        return
    global HOOK_FRAMES, INDUSTRY_FRAMES
    hook = Path(args.hook) if args.hook else None
    tmp = tempfile.mkdtemp(prefix="jianwei_")
    try:
        starts, _, _, total, voices = timeline()
        if hook and hook.exists():
            HOOK_FRAMES = extract_frames(hook, Path(tmp) / "hook", HOOK_DUR, True)
        if INDUSTRY_CLIP.exists():
            # 工业实景按场景实际时长（含转场）放慢
            k = [s[0] for s in scenes()].index(s_industry)
            span = starts[k + 1] - starts[k] + scenes()[k][2]
            clip = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                         str(INDUSTRY_CLIP)], capture_output=True, text=True, check=True).stdout)
            INDUSTRY_FRAMES = extract_frames(INDUSTRY_CLIP, Path(tmp) / "industry", span + 0.1, False,
                                             min(1.0, clip / span))
        for name, (path, _) in LIVE_CLIPS.items():
            LIVE_FRAMES[name] = extract_live(path, Path(tmp) / name)
        if args.stills is not None:
            out = Path(args.still_dir)
            out.mkdir(parents=True, exist_ok=True)
            for s in args.stills:
                render_frame(int(round(s * FPS))).save(out / f"t{s:06.2f}.png")
            print(f"stills -> {out}")
            return
        n = int(round(total * FPS))
        cmd = ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
               "-r", str(FPS), "-i", "-"]
        hook_in = 1 if HOOK_FRAMES else 0
        if hook_in:
            cmd += ["-i", str(hook)]
        cmd += ["-i", str(LIVE_CLIPS["full"][0])]
        narr = Path(tmp) / "narration.wav"
        narr_in = hook_in + 2 if narration_track(voices, n / FPS, narr) else 0
        if narr_in:
            cmd += ["-i", str(narr)]
        graph = ("[0:v]scale=out_color_matrix=bt709:out_range=tv,format=yuv420p[v];"
                 + audio_graph(n / FPS, starts, hook_in, hook_in + 1, narr_in))
        cmd += ["-filter_complex", graph, "-map", "[v]", "-map", "[a]", "-c:a", "aac", "-b:a", "192k", "-ar", "48000"]
        # 先写在 Linux 临时目录，完成后再复制到输出位置；码率上限让成片保持在 100 MB 以内，比赛上限为 300 MB
        out_tmp = str(Path(tmp) / "render.mp4")
        cmd += ["-c:v", "libx264", "-preset", "slow", "-crf", "18", "-maxrate", "9M", "-bufsize", "18M",
                "-profile:v", "high", "-level", "4.1", "-colorspace", "bt709", "-color_primaries", "bt709",
                "-color_trc", "bt709", "-movflags", "+faststart", out_tmp]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        with Pool(args.jobs) as pool:
            for i, buf in enumerate(pool.imap(worker, range(n), chunksize=2)):
                proc.stdin.write(buf)
                if i % 300 == 0:
                    print(f"{i}/{n} frames", flush=True)
        proc.stdin.close()
        if proc.wait():
            sys.exit("ffmpeg failed")
        shutil.copyfile(out_tmp, args.out)
        print(f"{n} frames, {total:.1f}s -> {args.out}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
