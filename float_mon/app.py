"""
应用逻辑 — 事件轮询、拖拽、悬停、面板、菜单、刷新
"""

import sys
import ctypes
import tkinter as tk
from PIL import Image, ImageTk

from . import config as cfg
from .monitor import SystemMonitor, gpu_shutdown
from .renderer import Renderer
from .win32_layered import (
    LayeredWindow, user32 as _u32,
    POINT, VK_LBUTTON, VK_RBUTTON, HWND_TOPMOST,
)


class FloatingBall:
    """悬浮球主控：协调监控、渲染、窗口和交互"""

    def __init__(self):
        self.monitor = SystemMonitor()
        self.renderer = Renderer()
        self.data = {}
        self._topmost = True
        self._ball_alpha = 1.0

        # 初始位置：右上角
        sw = _u32.GetSystemMetrics(0)
        init_x = sw - cfg.BALL_D - 40
        init_y = 80

        # Win32 分层窗口
        self.ball_win = LayeredWindow(cfg.BALL_D, cfg.BALL_D)
        self.ball_win.show(init_x, init_y)
        self.ball_win.set_topmost(self._topmost)

        # tkinter 隐藏窗口（用于菜单和面板，不参与焦点管理）
        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.geometry("1x1+99999+99999")
        self.root.attributes("-alpha", 0)
        
        # root 窗口始终置顶：它是隐藏的 1x1 像素窗口，不影响视觉，
        # 但必须保持置顶才能让右键菜单正常弹出（否则菜单获取焦点时
        # Windows 会拉低整个程序的 Z 轴层级）
        self.root.attributes("-topmost", True)

        # 面板状态
        self.panel_win = None
        self.panel_label = None
        self.panel_photo = None
        self.panel_visible = False
        self.hovering = False
        self.fade_dir = 0
        self.fade_alpha = 0.0
        self._hover_job = None
        self._fade_job = None
        self._hide_job = None

        # 拖拽状态
        self._dragging = False
        self._drag_sx = 0
        self._drag_sy = 0
        self._drag_wx = 0
        self._drag_wy = 0
        self._drag_moved = False
        self._lbtn_was = False 
        self._rbtn_was = False

        # 菜单状态
        self._menu_open = False

        # 右键菜单
        self.menu = tk.Menu(self.root, tearoff=0)
        self.menu.add_command(label="\u2713 置顶", command=self._toggle_top)
        alpha_menu = tk.Menu(self.menu, tearoff=0)
        for v in [50, 60, 70, 80, 90, 100]:
            alpha_menu.add_command(
                label=f"{v}%",
                command=lambda a=v / 100: self._set_ball_alpha(a),
            )
        self.menu.add_cascade(label="透明度", menu=alpha_menu)
        self.menu.add_separator()
        self.menu.add_command(label="退出", command=self._quit)

        # 启动定时器
        self.root.after(100, self._poll)
        self.root.after(200, self._tick)

    # ==================== 属性 ====================
    def _set_ball_alpha(self, a):
        self._ball_alpha = a

    def _toggle_top(self):
        self._topmost = not self._topmost

        # 只切换悬浮球的置顶状态
        # root 和面板始终置顶（root=隐藏窗口服务菜单，面板=临时悬停窗口不应被遮挡）
        self.ball_win.set_topmost(self._topmost)

        mark = "\u2713 " if self._topmost else "  "
        self.menu.entryconfigure(0, label=f"{mark}置顶")
        self._menu_open = False

    # ==================== 全局轮询 ====================
    def _poll(self):
        """每 16~50ms 轮询鼠标状态，处理拖拽、悬停、右键菜单"""
        try:
            pt = POINT()
            _u32.GetCursorPos(ctypes.byref(pt))

            bx, by = self.ball_win._px, self.ball_win._py
            in_ball = bx <= pt.x <= bx + cfg.BALL_D and by <= pt.y <= by + cfg.BALL_D

            in_panel = False
            if self.panel_win:
                try:
                    px, py = self.panel_win.winfo_x(), self.panel_win.winfo_y()
                    in_panel = px <= pt.x <= px + cfg.PANEL_W and py <= pt.y <= py + cfg.PANEL_H
                except Exception:
                    pass

            lbtn_state = _u32.GetAsyncKeyState(VK_LBUTTON)
            lbtn_down = bool(lbtn_state & 0x8000)
            lbtn_clicked = lbtn_down and not self._lbtn_was
            self._lbtn_was = lbtn_down

            rbtn_state = _u32.GetAsyncKeyState(VK_RBUTTON)
            rbtn_down = bool(rbtn_state & 0x8000)
            rbtn_clicked = rbtn_down and not self._rbtn_was
            self._rbtn_was = rbtn_down

            # 菜单消失：任何位置的左键点击，或者悬浮球外部的右键点击
            if self._menu_open and (lbtn_clicked or (rbtn_clicked and not in_ball)):
                self.root.after(150, self.menu.unpost)
                self._menu_open = False

            # --- 拖拽 ---
            if lbtn_down:
                if in_ball and not self._dragging:
                    self._dragging = True
                    self._drag_moved = False
                    self._drag_sx, self._drag_sy = pt.x, pt.y
                    self._drag_wx, self._drag_wy = self.ball_win._px, self.ball_win._py
                elif self._dragging:
                    nx = self._drag_wx + (pt.x - self._drag_sx)
                    ny = self._drag_wy + (pt.y - self._drag_sy)
                    if abs(nx - self.ball_win._px) > 2 or abs(ny - self.ball_win._py) > 2:
                        self._drag_moved = True
                    self.ball_win.move(nx, ny)
            else:
                if self._dragging:
                    self._dragging = False
                    if self._drag_moved:
                        self._snap()

            # --- 右键菜单 ---
            if rbtn_clicked and in_ball:
                self._menu_open = True
                try:
                    _u32.SetForegroundWindow(self.root.winfo_id())
                except Exception:
                    pass
                self.root.after(0, lambda: self.menu.post(pt.x, pt.y))

            # --- 悬停 ---
            if not self._dragging:
                was = self.hovering
                self.hovering = in_ball or in_panel
                if self.hovering and not was:
                    self._cancel_hover()
                    self._hover_job = self.root.after(cfg.HOVER_DELAY, self._show_panel)
                elif not self.hovering and was:
                    self._cancel_hover()
                    self._hide_job = self.root.after(cfg.HIDE_DELAY, self._try_hide)

        except Exception:
            pass

        interval = cfg.POLL_MS_DRAG if self._dragging else cfg.POLL_MS_IDLE
        self.root.after(interval, self._poll)

    def _snap(self):
        """贴边吸附"""
        sw = _u32.GetSystemMetrics(0)
        sh = _u32.GetSystemMetrics(1)
        x, y = self.ball_win._px, self.ball_win._py
        nx = max(0, min(x, sw - cfg.BALL_D))
        ny = max(0, min(y, sh - cfg.BALL_D))
        # 边缘吸附
        if x < cfg.SNAP_DIST:
            nx = 0
        elif x + cfg.BALL_D > sw - cfg.SNAP_DIST:
            nx = sw - cfg.BALL_D
        if y < cfg.SNAP_DIST:
            ny = 0
        elif y + cfg.BALL_D > sh - cfg.SNAP_DIST:
            ny = sh - cfg.BALL_D
        if nx != x or ny != y:
            self.ball_win.move(nx, ny)

    def _cancel_hover(self):
        if self._hover_job:
            self.root.after_cancel(self._hover_job)
            self._hover_job = None

    def _cancel_hide(self):
        if self._hide_job:
            self.root.after_cancel(self._hide_job)
            self._hide_job = None

    def _try_hide(self):
        if not self.hovering:
            self._start_fade(-1)

    # ==================== 面板 ====================
    def _show_panel(self):
        if self.panel_visible:
            return
        self.panel_visible = True
        key_hex = "#{:02x}{:02x}{:02x}".format(*cfg.KEY_COLOR)

        self.panel_win = tk.Toplevel(self.root)
        self.panel_win.overrideredirect(True)
        self.panel_win.attributes("-topmost", True)  # 面板始终置顶（临时悬停窗口，不应被遮挡）
        self.panel_win.attributes("-transparentcolor", key_hex)
        self.panel_win.attributes("-alpha", 0.0)
        self.panel_win.configure(bg=key_hex)

        self.panel_label = tk.Label(
            self.panel_win, bg=key_hex, bd=0, highlightthickness=0
        )
        self.panel_label.pack()

        bx, by = self.ball_win._px, self.ball_win._py
        sw = _u32.GetSystemMetrics(0)
        if bx + cfg.BALL_D + cfg.PANEL_W + 20 < sw:
            px = bx + cfg.BALL_D + 10
        else:
            px = bx - cfg.PANEL_W - 10
        py = max(10, by - (cfg.PANEL_H - cfg.BALL_D) // 2)
        self.panel_win.geometry(f"{cfg.PANEL_W}x{cfg.PANEL_H}+{px}+{py}")

        self._render_panel()
        self._start_fade(1)

    def _render_panel(self):
        if not self.panel_win:
            return
        img = self.renderer.render_panel(self.data)
        self.panel_photo = ImageTk.PhotoImage(img)
        self.panel_label.configure(image=self.panel_photo)

    def _start_fade(self, direction):
        self._cancel_fade()
        self.fade_dir = direction
        self._fade_tick()

    def _fade_tick(self):
        if self.fade_dir == 0:
            return
        step = 0.92 / cfg.FADE_STEPS
        self.fade_alpha += self.fade_dir * step
        self.fade_alpha = max(0.0, min(0.92, self.fade_alpha))
        if self.panel_win:
            self.panel_win.attributes("-alpha", self.fade_alpha)
        if self.fade_alpha <= 0:
            self.fade_dir = 0
            self._destroy_panel()
            return
        if self.fade_alpha >= 0.92:
            self.fade_dir = 0
            return
        self._fade_job = self.root.after(cfg.FADE_DELAY, self._fade_tick)

    def _cancel_fade(self):
        if self._fade_job:
            self.root.after_cancel(self._fade_job)
            self._fade_job = None

    def _destroy_panel(self):
        if self.panel_win:
            self.panel_win.destroy()
            self.panel_win = self.panel_label = self.panel_photo = None
        self.panel_visible = False
        self.fade_alpha = 0.0

    # ==================== 刷新 ====================
    def _tick(self):
        try:
            self.data = self.monitor.poll()
            img = self.renderer.render_ball(self.data)
            if self._ball_alpha < 1.0:
                r, g, b, a = img.split()
                a = a.point(lambda x: int(x * self._ball_alpha))
                img = Image.merge("RGBA", (r, g, b, a))
            self.ball_win.update(img)
            self.ball_win.pump()
            if self.panel_visible:
                self._render_panel()
        except Exception:
            pass
        self.root.after(cfg.REFRESH_MS, self._tick)

    def _quit(self):
        gpu_shutdown()
        self.ball_win.destroy()
        self.root.destroy()
        sys.exit(0)

    def run(self):
        self.root.mainloop()