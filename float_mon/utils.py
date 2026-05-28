"""
无状态工具函数 — 字体加载、格式化、绘图辅助
"""

import os
from PIL import ImageFont

from . import config as cfg


# ==================== 字体 ====================
def load_font(paths, size):
    """尝试加载字体，失败则用默认字体"""
    for p in paths:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


# ==================== 颜色/格式化 ====================
def get_color(value, c_s=None, c_m=None, c_e=None):
    """三段渐变：起始→中间→结束，默认用MEM色阶（绿→黄→红）"""
    if c_s is None:
        c_s, c_m, c_e = cfg.GRAD_MEM_S, cfg.GRAD_MEM_M, cfg.GRAD_MEM_E
    t = max(0, min(100, value)) / 100
    if t < 0.5:
        s = t / 0.5
        c, e = c_s, c_m
    else:
        s = (t - 0.5) / 0.5
        c, e = c_m, c_e
    return tuple(int(c[j] + (e[j] - c[j]) * s) for j in range(3))


def get_cpu_color(value):
    """CPU专用色阶：青→紫→红"""
    return get_color(value, cfg.GRAD_CPU_S, cfg.GRAD_CPU_M, cfg.GRAD_CPU_E)


def format_speed(bps):
    """自适应网速格式化 B/s → KB/s → MB/s"""
    if bps < 1024:
        return f"{bps:.0f}B"
    if bps < 1048576:
        return f"{bps / 1024:.0f}K"
    return f"{bps / 1048576:.1f}M"


def format_bytes(b):
    """自适应字节格式化 MB → GB"""
    if b < 1073741824:
        return f"{b / 1048576:.0f}MB"
    return f"{b / 1073741824:.1f}GB"


# ==================== 绘图辅助 ====================
def draw_right_text(draw, canvas_w, margin, y, text, color, font):
    """右对齐绘制文字"""
    bb = draw.textbbox((0, 0), text, font=font)
    draw.text((canvas_w - margin - (bb[2] - bb[0]), y), text, fill=color, font=font)


def draw_progress_bar(draw, x, y, w, h, percent, color):
    """绘制圆角进度条"""
    draw.rounded_rectangle([x, y, x + w, y + h], h // 2, fill=cfg.C_RING_BG)
    fw = max(0, int(w * percent / 100))
    if fw > h:
        draw.rounded_rectangle([x, y, x + fw, y + h], h // 2, fill=color)
