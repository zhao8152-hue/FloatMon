"""
PIL 渲染器 — 悬浮球体 + 详情面板
"""

from PIL import Image, ImageDraw, ImageTk

from . import config as cfg
from .utils import load_font, get_color, get_cpu_color, format_speed, format_bytes, draw_right_text, draw_progress_bar


class Renderer:
    """负责将系统数据渲染为 PIL 图像"""

    def __init__(self):
        S = cfg.SCALE
        self._f_main = load_font(cfg.FONT_MONO, 14 * S)
        self._f_mid  = load_font(cfg.FONT_MONO, 10 * S)
        self._f_xs   = load_font(cfg.FONT_SERIF, 8 * S)
        self._f_s    = load_font(cfg.FONT_SERIF, 10 * S)
        self._f_m    = load_font(cfg.FONT_SERIF, 13 * S)
        self._m_s    = load_font(cfg.FONT_MONO, 10 * S)
        self._m_m    = load_font(cfg.FONT_MONO, 12 * S)

    # ---------- 悬浮球体 ----------
    def render_ball(self, data):
        """渲染悬浮球，返回最终尺寸的 RGBA Image"""
        S = cfg.SCALE
        size = cfg.BALL_D * S
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        cx = cy = size // 2
        r = cx - 2 * S

        # 背景渐变
        for i in range(r + 2 * S, 0, -1):
            dist = r + 2 * S - i
            fade = min(1.0, dist / (2 * S)) if dist < 2 * S else 1.0
            draw.ellipse([cx - i, cy - i, cx + i, cy + i],
                         fill=cfg.C_BG + (int(255 * fade),))

        # 核心圆
        cr = int(r * 0.93)
        draw.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], fill=cfg.C_BG + (255,))

        cpu = data.get("cpu", 0)
        mem = data.get("mem_pct", 0)
        up = data.get("upload", 0)
        dn = data.get("download", 0)

        # 三段渐变色阶：起始→中间→结束
        def _grad3(pct, c_s, c_m, c_e):
            t = max(0, min(100, pct)) / 100
            if t < 0.5:
                s = t / 0.5
                c, e = c_s, c_m
            else:
                s = (t - 0.5) / 0.5
                c, e = c_m, c_e
            return tuple(int(c[j] + (e[j] - c[j]) * s) for j in range(3))

        cc = _grad3(cpu, cfg.GRAD_CPU_S, cfg.GRAD_CPU_M, cfg.GRAD_CPU_E)
        mc = _grad3(mem, cfg.GRAD_MEM_S, cfg.GRAD_MEM_M, cfg.GRAD_MEM_E)

        # 渐变弧线：起始色 → 当前使用率对应色（三段渐变）
        def _grad_arc(bbox, start_deg, sweep, width, pct, c_s, c_m, c_e):
            c_end = _grad3(pct, c_s, c_m, c_e)
            steps = max(1, int(abs(sweep)))
            step = sweep / steps
            for i in range(steps):
                t = i / max(steps - 1, 1)
                color = tuple(int(c_s[j] + (c_end[j] - c_s[j]) * t)
                              for j in range(3))
                a1 = start_deg + step * i
                draw.arc(bbox, a1, a1 + step + 0.5, fill=color + (255,), width=width)

        # CPU 外环
        wr = 5 * S
        ri = r - 4 * S
        draw.ellipse([cx - ri, cy - ri, cx + ri, cy + ri],
                      outline=cfg.C_RING_BG + (255,), width=wr)
        if cpu > 0.5:
            _grad_arc([cx - ri, cy - ri, cx + ri, cy + ri],
                      270, min(cpu * 3.6, 359.9), wr, cpu,
                      cfg.GRAD_CPU_S, cfg.GRAD_CPU_M, cfg.GRAD_CPU_E)

        # MEM 内环
        ri2 = r - 13 * S
        wr2 = 4 * S
        draw.ellipse([cx - ri2, cy - ri2, cx + ri2, cy + ri2],
                      outline=cfg.C_RING_BG + (255,), width=wr2)
        if mem > 0.5:
            _grad_arc([cx - ri2, cy - ri2, cx + ri2, cy + ri2],
                      270, min(mem * 3.6, 359.9), wr2, mem,
                      cfg.GRAD_MEM_S, cfg.GRAD_MEM_M, cfg.GRAD_MEM_E)

        # 内容垂直居中
        line_h_label = 12 * S
        line_h_val = 14 * S
        line_h_sub = 12 * S
        total_h = line_h_label + line_h_val + line_h_sub + 6 * S + line_h_sub
        y = cy - total_h // 2

        # "CPU" 标签
        bb = draw.textbbox((0, 0), "CPU", font=self._f_mid)
        draw.text((cx - (bb[2] - bb[0]) // 2, y), "CPU",
                  fill=cfg.C_TEXT2 + (200,), font=self._f_mid)
        y += line_h_label

        # CPU数值
        cpu_txt = f"{cpu:.0f}%"
        bb = draw.textbbox((0, 0), cpu_txt, font=self._f_main)
        draw.text((cx - (bb[2] - bb[0]) // 2, y), cpu_txt,
                  fill=cc + (255,), font=self._f_main)
        y += line_h_val + 4 * S

        # MEM
        mem_txt = f"MEM {mem:.0f}%"
        bb = draw.textbbox((0, 0), mem_txt, font=self._f_mid)
        draw.text((cx - (bb[2] - bb[0]) // 2, y), mem_txt,
                  fill=mc + (220,), font=self._f_mid)
        y += line_h_sub + 2 * S

        # 网速
        up_txt = f"↑{format_speed(up)}"
        dn_txt = f"↓{format_speed(dn)}"
        bu = draw.textbbox((0, 0), up_txt, font=self._f_mid)
        bd = draw.textbbox((0, 0), dn_txt, font=self._f_mid)
        uw, dw = bu[2] - bu[0], bd[2] - bd[0]
        gap_x = 5 * S
        total_w = uw + gap_x + dw
        sx = cx - total_w // 2
        draw.text((sx, y), up_txt, fill=cfg.C_CYAN + (220,), font=self._f_mid)
        draw.text((sx + uw + gap_x, y), dn_txt, fill=cfg.C_BLUE + (220,), font=self._f_mid)

        return img.resize((cfg.BALL_D, cfg.BALL_D), Image.LANCZOS)

    # ---------- 详情面板 ----------
    def render_panel(self, data):
        """渲染详情面板，返回最终尺寸的 RGB Image"""
        S = cfg.SCALE
        w, h = cfg.PANEL_W * S, cfg.PANEL_H * S
        img = Image.new("RGB", (w, h), cfg.KEY_COLOR)
        draw = ImageDraw.Draw(img)

        # 圆角背景
        draw.rounded_rectangle([0, 0, w - 1, h - 1], 10 * S,
                               fill=cfg.C_BG2, outline=cfg.C_BORDER, width=1 * S)
        y = 14 * S
        draw.text((14 * S, y), "System Monitor", fill=cfg.C_TEXT, font=self._f_m)
        y += 32 * S

        # CPU（青→紫→红）
        cpu = data.get("cpu", 0)
        ct = data.get("cpu_temp")
        cc = get_cpu_color(cpu)
        draw.text((14 * S, y), "CPU", fill=cfg.C_TEXT2, font=self._f_s)
        draw_right_text(draw, w, 14 * S, y, f"{cpu:.1f}%", cc, self._m_m)
        y += 18 * S
        draw_progress_bar(draw, 14 * S, y, w - 28 * S, 6 * S, cpu, cc)
        y += 14 * S
        if ct is not None:
            draw_right_text(draw, w, 14 * S, y, f"{ct:.0f}\u00b0C",
                            get_cpu_color(ct), self._m_s)
        y += 22 * S

        # GPU
        gpu = data.get("gpu")
        if gpu:
            gc = get_color(gpu["usage"])
            draw.text((14 * S, y), "GPU", fill=cfg.C_TEXT2, font=self._f_s)
            draw_right_text(draw, w, 14 * S, y, f"{gpu['usage']:.0f}%", gc, self._m_m)
            y += 18 * S
            draw_progress_bar(draw, 14 * S, y, w - 28 * S, 6 * S, gpu["usage"], gc)
            y += 14 * S
            draw.text((14 * S, y), f"Temp {gpu['temp']}\u00b0C",
                      fill=cfg.C_TEXT2, font=self._m_s)
            draw_right_text(draw, w, 14 * S, y,
                            f"VRAM {format_bytes(gpu['mem_used'])}/{format_bytes(gpu['mem_total'])}",
                            cfg.C_TEXT2, self._m_s)
            y += 22 * S
        else:
            draw.text((14 * S, y), "GPU  N/A", fill=(72, 79, 88), font=self._f_s)
            y += 20 * S

        # RAM（绿→黄→红）
        mem = data.get("mem_pct", 0)
        mc = get_color(mem)
        draw.text((14 * S, y), "RAM", fill=cfg.C_TEXT2, font=self._f_s)
        draw_right_text(draw, w, 14 * S, y, f"{mem:.1f}%", mc, self._m_m)
        y += 18 * S
        draw_progress_bar(draw, 14 * S, y, w - 28 * S, 6 * S, mem, mc)
        y += 14 * S
        draw.text((14 * S, y),
                  f"{format_bytes(data.get('mem_used', 0))} / {format_bytes(data.get('mem_total', 0))}",
                  fill=cfg.C_TEXT2, font=self._m_s)
        y += 28 * S

        # NET
        draw.text((14 * S, y), "NET", fill=cfg.C_TEXT2, font=self._f_s)
        y += 18 * S
        draw.text((14 * S, y), f"\u2191 {format_speed(data.get('upload', 0))}/s",
                  fill=cfg.C_CYAN, font=self._m_m)
        draw_right_text(draw, w, 14 * S, y,
                        f"\u2193 {format_speed(data.get('download', 0))}/s",
                        cfg.C_BLUE, self._m_m)
        y += 30 * S

        # DISK
        disks = data.get("disks", {})
        if disks:
            draw.text((14 * S, y), "DISK", fill=cfg.C_TEXT2, font=self._f_s)
            y += 18 * S
            for letter, info in disks.items():
                dc = get_color(info["pct"])
                draw.text((14 * S, y), f"{letter}:", fill=cfg.C_TEXT2, font=self._m_s)
                draw_progress_bar(draw, 34 * S, y + 2 * S, 120 * S, 4 * S,
                                  info["pct"], dc)
                draw_right_text(draw, w, 14 * S, y,
                                f"{info['pct']:.0f}%  {format_bytes(info['used'])}/{format_bytes(info['total'])}",
                                cfg.C_TEXT2, self._m_s)
                y += 20 * S

        return img.resize((cfg.PANEL_W, cfg.PANEL_H), Image.LANCZOS)
