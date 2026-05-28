"""
Win32 分层窗口封装 — per-pixel alpha 透明渲染
置顶切换：销毁窗口→重建（WS_EX_TOPMOST 必须在创建时确定，运行时改不了）
"""
import ctypes, ctypes.wintypes as wintypes

_user32 = ctypes.windll.user32
_gdi32 = ctypes.windll.gdi32
_kernel32 = ctypes.windll.kernel32
user32 = _user32

WS_EX_LAYERED    = 0x00080000
WS_EX_TOPMOST    = 0x00000008
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_NOACTIVATE = 0x08000000
ULW_ALPHA = 0x00000002

HWND_TOPMOST   = -1
HWND_NOTOPMOST = -2
SWP_NOMOVE     = 0x0002
SWP_NOSIZE     = 0x0001
SWP_NOACTIVATE = 0x0010
SWP_NOZORDER   = 0x0400

WM_CLOSE  = 0x0010
VK_LBUTTON = 0x01
VK_RBUTTON = 0x02


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
        ("biHeight", ctypes.c_long), ("biPlanes", ctypes.c_ushort),
        ("biBitCount", ctypes.c_ushort), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]

class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]

class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

class SIZE(ctypes.Structure):
    _fields_ = [("cx", ctypes.c_long), ("cy", ctypes.c_long)]

class BLENDFUNCTION(ctypes.Structure):
    _fields_ = [
        ("BlendOp", ctypes.c_ubyte), ("BlendFlags", ctypes.c_ubyte),
        ("SourceConstantAlpha", ctypes.c_ubyte), ("AlphaFormat", ctypes.c_ubyte),
    ]

WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_long, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)

class WNDCLASS(ctypes.Structure):
    _fields_ = [
        ("style", wintypes.UINT), ("lpfnWndProc", WNDPROC),
        ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
        ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HANDLE),
        ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HANDLE),
        ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR),
    ]

class MSG(ctypes.Structure):
    _fields_ = [
        ("hwnd", wintypes.HWND), ("message", wintypes.UINT),
        ("wParam", wintypes.WPARAM), ("lParam", wintypes.LPARAM),
        ("time", wintypes.DWORD), ("pt", POINT),
    ]


class LayeredWindow:

    def __init__(self, width, height, title="LayeredWnd"):
        self.w, self.h = width, height
        self._title = title
        self._hinst = _kernel32.GetModuleHandleW(None)
        self._class_name = title + "_cls"
        self._topmost = True
        self._px, self._py = 0, 0
        self._class_registered = False
        self._create()

    def _register_class(self):
        """注册窗口类（仅首次）"""
        if self._class_registered:
            return
        self._wndproc_ref = WNDPROC(self._wndproc)
        wc = WNDCLASS()
        wc.lpfnWndProc = self._wndproc_ref
        wc.hInstance = self._hinst
        wc.lpszClassName = self._class_name
        wc.hCursor = _user32.LoadCursorW(None, 32512)
        _user32.RegisterClassW(ctypes.byref(wc))
        self._class_registered = True

    def _create_window(self):
        """创建窗口 + GDI 资源"""
        ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
        if self._topmost:
            ex |= WS_EX_TOPMOST
        self.hwnd = _user32.CreateWindowExW(
            ex, self._class_name, self._title, 0,
            0, 0, self.w, self.h,
            None, None, self._hinst, None,
        )
        self._hdc_screen = _user32.GetDC(None)
        self._hdc_mem = _gdi32.CreateCompatibleDC(self._hdc_screen)
        bi = BITMAPINFO()
        bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bi.bmiHeader.biWidth = self.w
        bi.bmiHeader.biHeight = -self.h
        bi.bmiHeader.biPlanes = 1
        bi.bmiHeader.biBitCount = 32
        bi.bmiHeader.biCompression = 0
        self._pvBits = ctypes.c_void_p()
        self._hbmp = _gdi32.CreateDIBSection(
            self._hdc_mem, ctypes.byref(bi), 0,
            ctypes.byref(self._pvBits), None, 0,
        )
        self._old_bmp = _gdi32.SelectObject(self._hdc_mem, self._hbmp)

    def _create(self):
        self._register_class()
        self._create_window()

    def _release_window(self):
        """仅释放 GDI + 销毁窗口（不注销窗口类）"""
        _gdi32.SelectObject(self._hdc_mem, self._old_bmp)
        _gdi32.DeleteObject(self._hbmp)
        _gdi32.DeleteDC(self._hdc_mem)
        _user32.ReleaseDC(None, self._hdc_screen)
        _user32.DestroyWindow(self.hwnd)

    def _wndproc(self, hwnd, msg, wp, lp):
        if msg == WM_CLOSE:
            _user32.DestroyWindow(hwnd)
            return 0
        # 阻止最小化：显示桌面 / Win+D 会导致窗口被最小化后消失
        if msg == 0x0112:  # WM_SYSCOMMAND
            if (wp & 0xFFF0) == 0xF020:  # SC_MINIMIZE
                return 0
        return _user32.DefWindowProcW(hwnd, msg, wp, lp)

    # ---------- 渲染 ----------

    def update(self, img):
        raw = img.tobytes("raw", "BGRA")
        ctypes.memmove(self._pvBits, raw, len(raw))
        src, dst = POINT(0, 0), POINT(self._px, self._py)
        sz, bf = SIZE(self.w, self.h), BLENDFUNCTION(0, 0, 255, 1)
        _user32.UpdateLayeredWindow(
            self.hwnd, None, ctypes.byref(dst),
            ctypes.byref(sz), self._hdc_mem, ctypes.byref(src),
            0, ctypes.byref(bf), ULW_ALPHA,
        )

    # ---------- 显示 / 移动 ----------

    def show(self, x, y):
        self._px, self._py = x, y
        flag = HWND_TOPMOST if self._topmost else HWND_NOTOPMOST
        _user32.SetWindowPos(self.hwnd, flag, x, y, self.w, self.h,
                             SWP_NOACTIVATE)
        _user32.ShowWindow(self.hwnd, 5)

    def move(self, x, y):
        self._px, self._py = x, y
        _user32.SetWindowPos(self.hwnd, None, x, y, self.w, self.h,
                             SWP_NOACTIVATE | SWP_NOZORDER)

    # ---------- 置顶切换 ----------

    def set_topmost(self, top):
        if self._topmost == top:
            return
        x, y = self._px, self._py
        self._release_window()       # 释放 GDI + 销毁窗口
        self._topmost = top
        self._create()               # 重建窗口（窗口类已注册，直接建）
        self.show(x, y)              # 定位并显示

    # ---------- 消息泵 / 清理 ----------

    def pump(self):
        msg = MSG()
        while _user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            _user32.TranslateMessage(ctypes.byref(msg))
            _user32.DispatchMessageW(ctypes.byref(msg))

    def destroy(self):
        self._release_window()
        _user32.UnregisterClassW(self._class_name, self._hinst)
