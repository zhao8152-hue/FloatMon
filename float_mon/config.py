"""
全局配置常量 — 尺寸、颜色、时序、字体路径
"""

# ==================== 尺寸 ====================
BALL_R = 58                          # 悬浮球半径
BALL_D = BALL_R * 2                  # 悬浮球直径
SCALE = 3                            # PIL渲染超采样倍率
PANEL_W, PANEL_H = 250, 340          # 详情面板尺寸

# ==================== 时序 ====================
REFRESH_MS = 1000                    # 数据刷新间隔(ms)
SNAP_DIST = 30                       # 贴边吸附阈值(px)
FADE_STEPS = 12                      # 淡入淡出步数
FADE_DELAY = 25                      # 淡入淡出间隔(ms)
HOVER_DELAY = 350                    # 悬停延迟(ms)
POLL_MS_IDLE = 50                    # 空闲轮询间隔(ms)
POLL_MS_DRAG = 16                    # 拖拽轮询间隔(ms)
HIDE_DELAY = 450                     # 面板隐藏延迟(ms)

# ==================== 透明色键 ====================
KEY_COLOR = (1, 2, 3)               # tkinter面板透明色(RGB)

# ==================== 颜色(RGB) ====================
C_BG       = (13, 17, 23)           # 主背景深色
C_BG2      = (22, 27, 34)           # 面板背景
C_RING_BG  = (30, 38, 52)           # 环形轨道底色
C_TEXT     = (201, 209, 217)         # 主文字
C_TEXT2    = (139, 148, 158)         # 次要文字
C_GREEN    = (63, 185, 80)           # 低负载
C_YELLOW   = (210, 153, 34)          # 中负载
C_RED      = (248, 81, 73)           # 高负载
C_CYAN     = (57, 210, 192)          # 网速上行
C_BLUE     = (121, 192, 255)         # 网速下行
C_BORDER   = (48, 54, 61)            # 面板边框

# ==================== 渐变色(热力色阶) ====================
# CPU: 青→紫→红
GRAD_CPU_S = (0, 212, 255)         # 青色 (0%)
GRAD_CPU_M = (120, 80, 255)        # 紫色 (50%)
GRAD_CPU_E = (248, 81, 73)         # 红色 (100%)
# MEM: 绿→黄→红
GRAD_MEM_S = (63, 185, 80)         # 绿色 (0%)
GRAD_MEM_M = (255, 214, 0)         # 黄色 (50%)
GRAD_MEM_E = (248, 81, 73)         # 红色 (100%)

# ==================== 字体路径 ====================
FONT_SERIF = [
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
]
FONT_MONO = [
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/cascadia.ttf",
    "C:/Windows/Fonts/lucon.ttf",
]
