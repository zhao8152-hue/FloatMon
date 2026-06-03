/*
 * FloatMon — 悬浮球系统监控工具 (C++ / Win32 API / GDI+)
 * 
 * 功能：
 *   - 圆形悬浮球：显示CPU%、MEM%、网速(↑↓)，渐变弧线环
 *   - 拖拽移动 + 屏幕边缘吸附
 *   - 右键菜单：置顶切换、透明度调节、退出
 *   - 悬停弹出详情面板：CPU/GPU/RAM/NET/DISK 完整信息
 *   - 面板淡入淡出动画
 * 
 * 编译 (MinGW-w64):
 *   g++ -o FloatMon.exe floatmon.cpp -lgdi32 -lgdiplus -luser32 -lshell32 -lpdh -liphlpapi -lole32 -mwindows -O2 -s -static
 */

/* ================================================================
 * SECTION 1: 头文件 & 宏定义
 * ================================================================ */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601  /* Windows 7+ */
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <olectl.h>     /* PROPID definition needed before gdiplus.h on winlibs */
#include <gdiplus.h>
#include <pdh.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>    /* std::min, std::max */

using std::min;
using std::max;

/* 链接库: 编译时使用 -lgdi32 -lgdiplus -luser32 -lshell32 -lpdh -liphlpapi -lole32 */

using namespace Gdiplus;

/* Premultiplied alpha 查找表: premul[color][alpha] = (color * alpha) / 255 */
static BYTE premul[256][256];
static BOOL premul_inited = FALSE;

static void init_premul(void) {
    if (premul_inited) return;
    for (int c = 0; c < 256; c++)
        for (int a = 0; a < 256; a++)
            premul[c][a] = (BYTE)((c * a) / 255);
    premul_inited = TRUE;
}

/* 将 straight alpha 像素转为 premultiplied alpha */
static inline void premultiply_pixel(BYTE* dst, BYTE b, BYTE g, BYTE r, BYTE a) {
    dst[0] = premul[b][a];
    dst[1] = premul[g][a];
    dst[2] = premul[r][a];
    dst[3] = a;
}

#define BALL_D        116     /* 悬浮球直径(px) */
#define BALL_R        (BALL_D / 2)
#define PANEL_W       250     /* 面板宽 */
#define PANEL_H       380   /* 面板高度，容纳五区块全开 */
#define REFRESH_MS    1000    /* 数据刷新间隔 */
#define SNAP_DIST     30      /* 贴边吸附阈值 */
#define FADE_STEPS    12      /* 淡入淡出步数 */
#define FADE_DELAY    25      /* 淡入淡出间隔(ms) */
#define HOVER_DELAY   350     /* 悬停触发延迟(ms) */
#define POLL_MS_IDLE  50      /* 空闲轮询间隔 */
#define POLL_MS_DRAG  16      /* 拖拽时轮询间隔 */
#define HIDE_DELAY    450     /* 面板隐藏延迟 */

/* 颜色 ARGB */
#define C_BG         0xFF0D1117
#define C_BG2        0xFF161B22
#define C_RING_BG    0xFF1E2634
#define C_TEXT       0xFFC9D1D9
#define C_TEXT2      0xFF8B949E
#define C_GREEN      0xFF3FB950
#define C_YELLOW     0xFFD29922
#define C_RED        0xFFF85149
#define C_CYAN       0xFF39D2C0
#define C_BLUE       0xFF79C0FF
#define C_BORDER     0xFF30363D

/* 渐变: CPU 青→紫→红 */
#define G_CPU_S      0xFF00D4FF
#define G_CPU_M      0xFF7850FF
#define G_CPU_E      0xFFF85149
/* 渐变: MEM 绿→黄→红 */
#define G_MEM_S      0xFF3FB950
#define G_MEM_M      0xFFFFD600
#define G_MEM_E      0xFFF85149

/* 菜单命令ID */
#define IDM_TOGGLE_TOP    1001
#define IDM_ALPHA_50      2001
#define IDM_ALPHA_60      2002
#define IDM_ALPHA_70      2003
#define IDM_ALPHA_80      2004
#define IDM_ALPHA_90      2005
#define IDM_ALPHA_100     2006
#define IDM_EXIT          3001
#define IDM_AUTOSTART     3003
#define IDM_SHOW_CPU      4001
#define IDM_SHOW_GPU      4002
#define IDM_SHOW_RAM      4003
#define IDM_SHOW_NET      4004
#define IDM_SHOW_DISK     4005

/* 自定义消息 */
#define WM_FLOATMON_REFRESH  (WM_USER + 1)
#define WM_FLOATMON_FADE     (WM_USER + 2)

static void update_alpha_checkmarks(void);  /* 前置声明 */

/* 定时器ID */
#define TIMER_POLL      1
#define TIMER_HOVER     2
#define TIMER_HIDE      3
#define TIMER_FADE      4

/* ================================================================
 * SECTION 3: 数据结构
 * ================================================================ */

/* 系统数据快照 */
typedef struct {
    double cpu;
    double cpu_temp;
    double mem_pct;
    ULONGLONG mem_used;
    ULONGLONG mem_total;
    double upload_bps;
    double download_bps;
    /* GPU */
    BOOL gpu_ok;
    double gpu_usage;
    double gpu_temp;
    ULONGLONG gpu_vram_used;
    ULONGLONG gpu_vram_total;
    double gpu_vram_pct;
    /* 磁盘 */
    BOOL disk_c_ok;
    double disk_c_pct;
    ULONGLONG disk_c_used;
    ULONGLONG disk_c_total;
    BOOL disk_d_ok;
    double disk_d_pct;
    ULONGLONG disk_d_used;
    ULONGLONG disk_d_total;
} SysData;

/* 应用程序全局状态 */
typedef struct {
    /* 窗口句柄 */
    HWND hBallWnd;
    HWND hPanelWnd;
    HINSTANCE hInst;

    /* GDI+ token */
    ULONG_PTR gdiToken;

    /* 字体 */
    Font* pFontMain;    /* 14pt mono */
    Font* pFontMid;     /* 10pt mono */
    Font* pFontS;       /* 10pt sans */
    Font* pFontM;       /* 13pt sans */

    /* 悬浮球位置 */
    int ballX, ballY;

    /* 拖拽状态 */
    BOOL dragging;
    int dragStartX, dragStartY;
    int dragWinX, dragWinY;
    BOOL dragMoved;

    /* 鼠标按钮状态 */
    BOOL lbtnWas;
    BOOL rbtnWas;

    /* 悬停状态 */
    BOOL hovering;
    BOOL panelVisible;
    float panelAlpha;
    int fadeDir;            /* 1=fade in, -1=fade out, 0=idle */

    /* 置顶 & 透明度 & 自启 */
    BOOL topmost;
    float ballAlpha;
    BOOL autostart;

    /* 菜单 */
    int menuX, menuY;  /* 记录菜单位置，勾选后重显 */
    HMENU hMenu;
    HMENU hAlphaMenu;
    HMENU hShowMenu;
    BOOL  showCPU, showGPU, showRAM, showNET, showDISK;  /* 详情面板显示开关 */

    /* 系统数据 */
    SysData data;

    /* 网络数据(差值计算用) */
    ULONG64 netInPrev;
    ULONG64 netOutPrev;
    ULONG64 netTimePrev;

    /* PDH (CPU监控) */
    HQUERY hPdhQuery;
    HCOUNTER hPdhCounter;
    BOOL pdhFirst;

    /* 渲染用位图 */
    HBITMAP hBallBmp;
    HDC hBallDC;
    void* pBallBits;

    HBITMAP hPanelBmp;
    HDC hPanelDC;
    void* pPanelBits;
} AppState;

static AppState g;

/* ================================================================
 * SECTION 4: 颜色工具函数
 * ================================================================ */

/* 三段渐变色插值 (value: 0-100, 返回 ARGB 颜色) */
static ARGB grad3_color(double value, ARGB cs, ARGB cm, ARGB ce) {
    double t = value / 100.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    if (t < 0.5) {
        double s = t / 0.5;
        BYTE sr = (cs >> 16) & 0xFF, sg = (cs >> 8) & 0xFF, sb = cs & 0xFF;
        BYTE er = (cm >> 16) & 0xFF, eg = (cm >> 8) & 0xFF, eb = cm & 0xFF;
        int r = (int)(sr + (er - sr) * s);
        int g = (int)(sg + (eg - sg) * s);
        int b = (int)(sb + (eb - sb) * s);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    } else {
        double s = (t - 0.5) / 0.5;
        BYTE sr = (cm >> 16) & 0xFF, sg = (cm >> 8) & 0xFF, sb = cm & 0xFF;
        BYTE er = (ce >> 16) & 0xFF, eg = (ce >> 8) & 0xFF, eb = ce & 0xFF;
        int r = (int)(sr + (er - sr) * s);
        int g = (int)(sg + (eg - sg) * s);
        int b = (int)(sb + (eb - sb) * s);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

/* 默认色阶(绿→黄→红) */
static ARGB get_color(double value) {
    return grad3_color(value, G_MEM_S, G_MEM_M, G_MEM_E);
}

/* CPU专有色阶(青→紫→红) */
static ARGB get_cpu_color(double value) {
    return grad3_color(value, G_CPU_S, G_CPU_M, G_CPU_E);
}

/* ================================================================
 * SECTION 5: 格式化函数
 * ================================================================ */

static void format_speed(double bps, WCHAR* buf, size_t bufSize) {
    if (bps < 1024.0)
        swprintf_s(buf, bufSize, L"%.0fB", bps);
    else if (bps < 1048576.0)
        swprintf_s(buf, bufSize, L"%.0fK", bps / 1024.0);
    else
        swprintf_s(buf, bufSize, L"%.1fM", bps / 1048576.0);
}

static void format_bytes(ULONGLONG b, WCHAR* buf, size_t bufSize) {
    if (b < 1073741824ULL)
        swprintf_s(buf, bufSize, L"%lluMB", b / 1048576ULL);
    else
        swprintf_s(buf, bufSize, L"%.1fGB", b / 1073741824.0);
}

/* ================================================================
 * SECTION 6: 系统监控
 * ================================================================ */

static void monitor_init(void) {
    g.pdhFirst = TRUE;
    g.netTimePrev = 0;

    /* PDH: CPU 使用率 */
    PDH_STATUS status = PdhOpenQueryW(NULL, 0, &g.hPdhQuery);
    if (status == ERROR_SUCCESS) {
        status = PdhAddCounterW(g.hPdhQuery, L"\\Processor(_Total)\\% Processor Time", 0, &g.hPdhCounter);
        if (status == ERROR_SUCCESS) {
            PdhCollectQueryData(g.hPdhQuery);
        }
    }

    /* NVML: GPU (动态加载，遍历已知安装路径) */
    g.data.gpu_ok = FALSE;
    const WCHAR* nvmlPaths[] = {
        L"nvml.dll",
        L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll",
        L"C:\\Windows\\System32\\nvml.dll",
    };
    HMODULE hNVML = NULL;
    for (int np = 0; np < 3; np++) {
        hNVML = LoadLibraryW(nvmlPaths[np]);
        if (hNVML) break;
    }
    if (hNVML) {
        typedef int (*nvmlInit_fn)(void);
        typedef int (*nvmlDeviceGetHandleByIndex_fn)(unsigned int, void**);
        typedef int (*nvmlDeviceGetUtilizationRates_fn)(void*, void*);
        typedef int (*nvmlDeviceGetTemperature_fn)(void*, int, unsigned int*);
        typedef int (*nvmlDeviceGetMemoryInfo_fn)(void*, void*);

        /* 兼容新旧 NVML: 先试 _v2，失败回退原名 */
        nvmlInit_fn pInit = (nvmlInit_fn)GetProcAddress(hNVML, "nvmlInit_v2");
        if (!pInit) pInit = (nvmlInit_fn)GetProcAddress(hNVML, "nvmlInit");
        nvmlDeviceGetHandleByIndex_fn pGetHandle = (nvmlDeviceGetHandleByIndex_fn)GetProcAddress(hNVML, "nvmlDeviceGetHandleByIndex_v2");
        if (!pGetHandle) pGetHandle = (nvmlDeviceGetHandleByIndex_fn)GetProcAddress(hNVML, "nvmlDeviceGetHandleByIndex");
        nvmlDeviceGetUtilizationRates_fn pGetUtil = (nvmlDeviceGetUtilizationRates_fn)GetProcAddress(hNVML, "nvmlDeviceGetUtilizationRates");
        nvmlDeviceGetTemperature_fn pGetTemp = (nvmlDeviceGetTemperature_fn)GetProcAddress(hNVML, "nvmlDeviceGetTemperature");
        nvmlDeviceGetMemoryInfo_fn pGetMem = (nvmlDeviceGetMemoryInfo_fn)GetProcAddress(hNVML, "nvmlDeviceGetMemoryInfo");

        if (pInit && pGetHandle && pGetUtil && pGetTemp && pGetMem) {
            if (pInit() == 0) {
                void* hDevice = NULL;
                if (pGetHandle(0, &hDevice) == 0) {
                    /* 将函数指针存储为窗口属性，供 poll 使用 */
                    SetPropW(g.hBallWnd, L"FM_NVML_DLL", hNVML);
                    SetPropW(g.hBallWnd, L"FM_NVML_DEVICE", hDevice);
                    SetPropW(g.hBallWnd, L"FM_NVML_GETUTIL", (HANDLE)pGetUtil);
                    SetPropW(g.hBallWnd, L"FM_NVML_GETTEMP", (HANDLE)pGetTemp);
                    SetPropW(g.hBallWnd, L"FM_NVML_GETMEM", (HANDLE)pGetMem);
                    g.data.gpu_ok = TRUE;
                    return;
                }
            }
        }
        FreeLibrary(hNVML);
    }
}

static void monitor_poll(void) {
    SysData* d = &g.data;

    /* --- CPU --- */
    if (g.hPdhQuery && g.hPdhCounter) {
        PDH_FMT_COUNTERVALUE cv;
        PdhCollectQueryData(g.hPdhQuery);
        if (PdhGetFormattedCounterValue(g.hPdhCounter, PDH_FMT_DOUBLE, NULL, &cv) == ERROR_SUCCESS) {
            d->cpu = cv.doubleValue;
        }
    } else {
        /* Fallback: 无PDH时估测 (不精确) */
        FILETIME idle, kernel, user;
        static FILETIME prevIdle, prevKernel, prevUser;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            if (prevKernel.dwLowDateTime || prevKernel.dwHighDateTime) {
                ULARGE_INTEGER uIdle, uKernel, uUser, uPrevIdle, uPrevKernel, uPrevUser;
                uIdle.LowPart = idle.dwLowDateTime; uIdle.HighPart = idle.dwHighDateTime;
                uKernel.LowPart = kernel.dwLowDateTime; uKernel.HighPart = kernel.dwHighDateTime;
                uUser.LowPart = user.dwLowDateTime; uUser.HighPart = user.dwHighDateTime;
                uPrevIdle.LowPart = prevIdle.dwLowDateTime; uPrevIdle.HighPart = prevIdle.dwHighDateTime;
                uPrevKernel.LowPart = prevKernel.dwLowDateTime; uPrevKernel.HighPart = prevKernel.dwHighDateTime;
                uPrevUser.LowPart = prevUser.dwLowDateTime; uPrevUser.HighPart = prevUser.dwHighDateTime;

                ULONGLONG idleDiff = uIdle.QuadPart - uPrevIdle.QuadPart;
                ULONGLONG kernelDiff = uKernel.QuadPart - uPrevKernel.QuadPart;
                ULONGLONG userDiff = uUser.QuadPart - uPrevUser.QuadPart;
                ULONGLONG totalDiff = kernelDiff + userDiff;
                d->cpu = totalDiff > 0 ? (1.0 - (double)idleDiff / totalDiff) * 100.0 : 0.0;
            }
            prevIdle = idle; prevKernel = kernel; prevUser = user;
        }
    }
    if (d->cpu < 0.0) d->cpu = 0.0; if (d->cpu > 100.0) d->cpu = 100.0;

    /* --- CPU温度 (Windows API, 可能不可用) --- */
    d->cpu_temp = -1.0; /* N/A */

    /* --- 内存 --- */
    MEMORYSTATUSEX memEx = { sizeof(MEMORYSTATUSEX) };
    if (GlobalMemoryStatusEx(&memEx)) {
        d->mem_pct = (double)memEx.dwMemoryLoad;
        d->mem_used = memEx.ullTotalPhys - memEx.ullAvailPhys;
        d->mem_total = memEx.ullTotalPhys;
    }

    /* --- GPU (NVML 动态加载) --- */
    d->gpu_ok = FALSE;
    void* hDevice = (void*)GetPropW(g.hBallWnd, L"FM_NVML_DEVICE");
    if (hDevice) {
        typedef int (*getUtil_fn)(void*, void*);
        typedef int (*getTemp_fn)(void*, int, unsigned int*);
        typedef int (*getMem_fn)(void*, void*);

        getUtil_fn pGetUtil = (getUtil_fn)GetPropW(g.hBallWnd, L"FM_NVML_GETUTIL");
        getTemp_fn pGetTemp = (getTemp_fn)GetPropW(g.hBallWnd, L"FM_NVML_GETTEMP");
        getMem_fn pGetMem = (getMem_fn)GetPropW(g.hBallWnd, L"FM_NVML_GETMEM");

        if (pGetUtil && pGetTemp && pGetMem) {
            unsigned int util_gpu, util_mem, temp;
            /* nvmlUtilization_t */
            struct { unsigned int gpu; unsigned int memory; } nvmlUtil;
            /* nvmlMemory_t */
            struct { unsigned long long total; unsigned long long free; unsigned long long used; } nvmlMem;

            if (pGetUtil(hDevice, &nvmlUtil) == 0 &&
                pGetTemp(hDevice, 0 /* NVML_TEMPERATURE_GPU */, &temp) == 0 &&
                pGetMem(hDevice, &nvmlMem) == 0) {
                d->gpu_ok = TRUE;
                d->gpu_usage = (double)nvmlUtil.gpu;
                d->gpu_temp = (double)temp;
                d->gpu_vram_used = nvmlMem.used;
                d->gpu_vram_total = nvmlMem.total;
                d->gpu_vram_pct = nvmlMem.total > 0 ? (double)nvmlMem.used / nvmlMem.total * 100.0 : 0.0;
            }
        }
    }

    /* --- 网络速度 --- */
    {
        DWORD dwSize = 0;
        GetIfTable(NULL, &dwSize, FALSE);
        if (dwSize > 0) {
            PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(dwSize);
            if (pIfTable && GetIfTable(pIfTable, &dwSize, FALSE) == NO_ERROR) {
                ULONG64 totalIn = 0, totalOut = 0;
                for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
                    if (pIfTable->table[i].dwOperStatus == IF_OPER_STATUS_OPERATIONAL &&
                        pIfTable->table[i].dwType != MIB_IF_TYPE_LOOPBACK) {
                        totalIn += pIfTable->table[i].dwInOctets;
                        totalOut += pIfTable->table[i].dwOutOctets;
                    }
                }
                ULONG64 now = GetTickCount64();
                if (g.netTimePrev > 0) {
                    double dt = (double)(now - g.netTimePrev) / 1000.0;
                    if (dt > 0.001) {
                        d->upload_bps = (double)(totalOut - g.netOutPrev) / dt;
                        d->download_bps = (double)(totalIn - g.netInPrev) / dt;
                    }
                }
                g.netInPrev = totalIn;
                g.netOutPrev = totalOut;
                g.netTimePrev = now;
            }
            if (pIfTable) free(pIfTable);
        }
    }

    /* --- 磁盘 --- */
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
    d->disk_c_ok = GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFreeBytes);
    if (d->disk_c_ok) {
        d->disk_c_total = totalBytes.QuadPart;
        d->disk_c_used = totalBytes.QuadPart - freeBytes.QuadPart;
        d->disk_c_pct = totalBytes.QuadPart > 0 ? (double)d->disk_c_used / totalBytes.QuadPart * 100.0 : 0.0;
    }
    d->disk_d_ok = GetDiskFreeSpaceExW(L"D:\\", &freeBytes, &totalBytes, &totalFreeBytes);
    if (d->disk_d_ok) {
        d->disk_d_total = totalBytes.QuadPart;
        d->disk_d_used = totalBytes.QuadPart - freeBytes.QuadPart;
        d->disk_d_pct = totalBytes.QuadPart > 0 ? (double)d->disk_d_used / totalBytes.QuadPart * 100.0 : 0.0;
    }
}

static void monitor_cleanup(void) {
    if (g.hPdhQuery) PdhCloseQuery(g.hPdhQuery);
    HMODULE hNVML = (HMODULE)GetPropW(g.hBallWnd, L"FM_NVML_DLL");
    if (hNVML) {
        typedef int (*nvmlShutdown_fn)(void);
        nvmlShutdown_fn pShutdown = (nvmlShutdown_fn)GetProcAddress(hNVML, "nvmlShutdown");
        if (pShutdown) pShutdown();
        FreeLibrary(hNVML);
    }
}

/* ================================================================
 * SECTION 7: GDI+ 渲染
 * ================================================================ */

/* 绘制渐变弧线 */
static void draw_gradient_arc(Graphics* gr, REAL cx, REAL cy, REAL r, REAL arcWidth,
                               REAL startDeg, REAL totalSweep, double pct,
                               ARGB cStart, ARGB cMid, ARGB cEnd) {
    ARGB cActual = grad3_color(pct, cStart, cMid, cEnd);
    int steps = max(1, (int)fabs(totalSweep));
    REAL stepAngle = totalSweep / steps;
    REAL rectX = cx - r, rectY = cy - r, rectW = r * 2, rectH = r * 2;

    for (int i = 0; i < steps; i++) {
        REAL t = steps > 1 ? (REAL)i / (steps - 1) : 0.0f;
        BYTE sr = (cStart >> 16) & 0xFF, sg = (cStart >> 8) & 0xFF, sb = cStart & 0xFF;
        BYTE er = (cActual >> 16) & 0xFF, eg = (cActual >> 8) & 0xFF, eb = cActual & 0xFF;
        int cr = (int)(sr + (er - sr) * t);
        int cg = (int)(sg + (eg - sg) * t);
        int cb = (int)(sb + (eb - sb) * t);
        ARGB color = 0xFF000000 | (cr << 16) | (cg << 8) | cb;

        Pen pen(Color(color), arcWidth);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        gr->DrawArc(&pen, rectX, rectY, rectW, rectH, startDeg + stepAngle * i, stepAngle + 0.5f);
    }
}

/* 渲染悬浮球 — 2x 超采样 → 高质量缩放，与 Python PIL 3x 超采样策略一致 */
static void render_ball(const SysData* d) {
    const REAL SC = 2.0f;
    int hiSize = (int)(BALL_D * SC);   /* 232 */
    int loSize = BALL_D;               /* 116 */

    /* === 2x 超采样渲染 === */
    Bitmap bmpHi(hiSize, hiSize, PixelFormat32bppARGB);
    Graphics gr(&bmpHi);
    gr.SetSmoothingMode(SmoothingModeHighQuality);
    gr.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gr.ScaleTransform(SC, SC);   /* 后续坐标按 1x 写，自动放大到 2x */

    REAL cx = (REAL)loSize / 2, cy = (REAL)loSize / 2;
    REAL outerR = cx - 2.0f;

    /* 背景圆 */
    SolidBrush bgBrush(Color(0xFF, 0x0D, 0x11, 0x17));
    gr.FillEllipse(&bgBrush, cx - outerR, cy - outerR, outerR * 2, outerR * 2);

    /* CPU 外环 */
    REAL ringW1 = 5.0f, ringR1 = outerR - 4.0f;
    Pen ringBgPen(Color(0xFF, 0x1E, 0x26, 0x34), ringW1);
    gr.DrawEllipse(&ringBgPen, cx - ringR1, cy - ringR1, ringR1 * 2, ringR1 * 2);
    if (d->cpu > 0.5)
        draw_gradient_arc(&gr, cx, cy, ringR1, ringW1, 270.0f,
                          (REAL)min(d->cpu * 3.6, 359.9), d->cpu,
                          G_CPU_S, G_CPU_M, G_CPU_E);

    /* MEM 内环 */
    REAL ringW2 = 4.0f, ringR2 = outerR - 13.0f;
    Pen ringBgPen2(Color(0xFF, 0x1E, 0x26, 0x34), ringW2);
    gr.DrawEllipse(&ringBgPen2, cx - ringR2, cy - ringR2, ringR2 * 2, ringR2 * 2);
    if (d->mem_pct > 0.5)
        draw_gradient_arc(&gr, cx, cy, ringR2, ringW2, 270.0f,
                          (REAL)min(d->mem_pct * 3.6, 359.9), d->mem_pct,
                          G_MEM_S, G_MEM_M, G_MEM_E);

    /* 文字 */
    StringFormat fmtTop;
    fmtTop.SetAlignment(StringAlignmentCenter);
    fmtTop.SetLineAlignment(StringAlignmentNear);
    REAL tallH = 20.0f, halfW = 38.0f;

    SolidBrush text2Brush(Color(200, 0x8B, 0x94, 0x9E));
    RectF rcLabel(cx - halfW, 30.0f, halfW * 2, tallH);
    gr.DrawString(L"CPU", -1, g.pFontMid, rcLabel, &fmtTop, &text2Brush);

    WCHAR buf[32];
    swprintf_s(buf, 32, L"%.0f%%", d->cpu);
    ARGB cpuCol = get_cpu_color(d->cpu);
    SolidBrush cpuBrush{Color(cpuCol)};
    RectF rcVal(cx - halfW, 42.0f, halfW * 2, tallH);
    gr.DrawString(buf, -1, g.pFontMain, rcVal, &fmtTop, &cpuBrush);

    swprintf_s(buf, 32, L"MEM %.0f%%", d->mem_pct);
    ARGB memCol = get_color(d->mem_pct);
    SolidBrush memBrush{Color(memCol)};
    RectF rcMem(cx - halfW, 60.0f, halfW * 2, tallH);
    gr.DrawString(buf, -1, g.pFontMid, rcMem, &fmtTop, &memBrush);

    WCHAR upBuf[16], dnBuf[16];
    format_speed(d->upload_bps, upBuf, 16);
    format_speed(d->download_bps, dnBuf, 16);
    WCHAR netBuf[48];
    swprintf_s(netBuf, 48, L"\x2191%s \x2193%s", upBuf, dnBuf);
    RectF rcNet(cx - halfW, 74.0f, halfW * 2, tallH);
    SolidBrush cyanBrush(Color(220, 0x39, 0xD2, 0xC0));
    gr.DrawString(netBuf, -1, g.pFontMid, rcNet, &fmtTop, &cyanBrush);

    /* === 缩放到 1x === */
    gr.ResetTransform();
    gr.Flush();
    Bitmap bmpLo(loSize, loSize, PixelFormat32bppARGB);
    Graphics grLo(&bmpLo);
    grLo.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    grLo.DrawImage(&bmpHi, 0, 0, loSize, loSize);
    grLo.Flush();

    /* 拷贝到 DIB，同时做 straight→premultiplied alpha 转换。
       UpdateLayeredWindow 要求 premultiplied alpha，GDI+ 输出 straight alpha，
       不转换会导致半透明边缘像素过亮（"光辉/光晕"）。 */
    BitmapData bmpData;
    Rect rc(0, 0, loSize, loSize);
    bmpLo.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bmpData);
    BYTE* pDIB = (BYTE*)g.pBallBits;
    BYTE* pSrc = (BYTE*)bmpData.Scan0;
    for (int row = 0; row < loSize; row++) {
        for (int col = 0; col < loSize; col++) {
            int si = row * bmpData.Stride + col * 4;
            int di = row * loSize * 4 + col * 4;
            premultiply_pixel(pDIB + di, pSrc[si], pSrc[si+1], pSrc[si+2], pSrc[si+3]);
        }
    }
    bmpLo.UnlockBits(&bmpData);
}

/* 绘制圆角矩形路径 */
static void add_rounded_rect(GraphicsPath* path, REAL x, REAL y, REAL w, REAL h, REAL r) {
    path->AddArc(x, y, r * 2, r * 2, 180, 90);
    path->AddLine(x + r, y, x + w - r, y);
    path->AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path->AddLine(x + w, y + r, x + w, y + h - r);
    path->AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path->AddLine(x + w - r, y + h, x + r, y + h);
    path->AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path->CloseFigure();
}

/* 绘制进度条 — globalAlpha 控制整体透明度 */
static void draw_progress_bar(Graphics* gr, REAL x, REAL y, REAL w, REAL h, double pct, ARGB color, BYTE globalAlpha) {
    GraphicsPath bgPath;
    add_rounded_rect(&bgPath, x, y, w, h, h / 2);
    SolidBrush bgBrush{Color((C_RING_BG & 0x00FFFFFF) | ((UINT)globalAlpha << 24))};
    gr->FillPath(&bgBrush, &bgPath);

    REAL fw = (REAL)(w * pct / 100.0);
    if (fw > h) {
        GraphicsPath fgPath;
        add_rounded_rect(&fgPath, x, y, fw, h, h / 2);
        SolidBrush fgBrush{Color((color & 0x00FFFFFF) | ((UINT)globalAlpha << 24))};
        gr->FillPath(&fgBrush, &fgPath);
    }
}

/* 渲染详情面板到32位ARGB位图(含alpha通道用于淡入淡出) */
static void render_panel(const SysData* d, BYTE globalAlpha) {
    const REAL SC = 2.0f;
    int hiW = (int)(PANEL_W * SC), hiH = (int)(PANEL_H * SC);
    int loW = PANEL_W, loH = PANEL_H;

    /* === 2x 超采样渲染 === */
    Bitmap bmpHi(hiW, hiH, PixelFormat32bppARGB);
    Graphics gr(&bmpHi);
    gr.SetSmoothingMode(SmoothingModeHighQuality);
    gr.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gr.ScaleTransform(SC, SC);
    gr.Clear(Color(0, 0, 0, 0));

    /* 圆角背景 */
    GraphicsPath bgPath;
    add_rounded_rect(&bgPath, 0.5f, 0.5f, (REAL)loW - 1, (REAL)loH - 1, 10.0f);
    SolidBrush bg2Brush(Color(globalAlpha, 0x16, 0x1B, 0x22));
    gr.FillPath(&bg2Brush, &bgPath);
    Pen borderPen(Color(globalAlpha, 0x30, 0x36, 0x3D), 1.0f);
    gr.DrawPath(&borderPen, &bgPath);

    StringFormat fmtLeft, fmtRight;
    fmtLeft.SetAlignment(StringAlignmentNear);
    fmtLeft.SetLineAlignment(StringAlignmentCenter);
    fmtRight.SetAlignment(StringAlignmentFar);
    fmtRight.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textBrush(Color(globalAlpha, 0xC9, 0xD1, 0xD9));
    SolidBrush text2Brush(Color(globalAlpha, 0x8B, 0x94, 0x9E));

    RectF rcL(14, 14, (REAL)loW - 28, 24);
    gr.DrawString(L"System Monitor", -1, g.pFontM, rcL, &fmtLeft, &textBrush);

    REAL y = 52.0f;
    WCHAR buf[64];
    WCHAR usedBuf[16], totalBuf[16];  /* DISK/RAM 共用 */

    /* --- CPU --- */
    if (g.showCPU) {
        ARGB cc = get_cpu_color(d->cpu);
        gr.DrawString(L"CPU", -1, g.pFontS, RectF(14, y, 100, 18), &fmtLeft, &text2Brush);
        swprintf_s(buf, 64, L"%.1f%%", d->cpu);
        SolidBrush ccBr(Color((cc & 0x00FFFFFF) | ((UINT)globalAlpha << 24)));
        gr.DrawString(buf, -1, g.pFontMain, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &ccBr);
        y += 20.0f;
        draw_progress_bar(&gr, 14, y, (REAL)loW - 28, 6.0f, d->cpu, cc, globalAlpha);
        y += 16.0f;
        if (d->cpu_temp >= 0) {
            swprintf_s(buf, 64, L"%.0f\u00B0C", d->cpu_temp);
            gr.DrawString(buf, -1, g.pFontMid, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &text2Brush);
        }
        y += 24.0f;
    }

    /* --- GPU --- */
    if (g.showGPU) {
        if (d->gpu_ok) {
        ARGB gc = get_color(d->gpu_usage);
        gr.DrawString(L"GPU", -1, g.pFontS, RectF(14, y, 100, 18), &fmtLeft, &text2Brush);
        swprintf_s(buf, 64, L"%.0f%%", d->gpu_usage);
        SolidBrush gcBr(Color((gc & 0x00FFFFFF) | ((UINT)globalAlpha << 24)));
        gr.DrawString(buf, -1, g.pFontMain, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &gcBr);
        y += 20.0f;
        draw_progress_bar(&gr, 14, y, (REAL)loW - 28, 6.0f, d->gpu_usage, gc, globalAlpha);
        y += 16.0f;
        WCHAR vramBuf[64];
        format_bytes(d->gpu_vram_used, buf, 64);
        format_bytes(d->gpu_vram_total, vramBuf, 64);
        WCHAR tempBuf[160];
        swprintf_s(tempBuf, 160, L"Temp %.0f\u00B0C    VRAM %s/%s", d->gpu_temp, buf, vramBuf);
        gr.DrawString(tempBuf, -1, g.pFontMid, RectF(14, y, (REAL)loW - 28, 18), &fmtLeft, &text2Brush);
        y += 24.0f;
    } else {
        gr.DrawString(L"GPU  N/A", -1, g.pFontS, RectF(14, y, (REAL)loW - 28, 18), &fmtLeft, &text2Brush);
        y += 22.0f;
    }
    }  /* g.showGPU */

    /* --- RAM --- */
    if (g.showRAM) {
    ARGB mc = get_color(d->mem_pct);
    gr.DrawString(L"RAM", -1, g.pFontS, RectF(14, y, 100, 18), &fmtLeft, &text2Brush);
    swprintf_s(buf, 64, L"%.1f%%", d->mem_pct);
    SolidBrush mcBr(Color((mc & 0x00FFFFFF) | ((UINT)globalAlpha << 24)));
    gr.DrawString(buf, -1, g.pFontMain, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &mcBr);
    y += 20.0f;
    draw_progress_bar(&gr, 14, y, (REAL)loW - 28, 6.0f, d->mem_pct, mc, globalAlpha);
    y += 16.0f;
    format_bytes(d->mem_used, usedBuf, 16);
    format_bytes(d->mem_total, totalBuf, 16);
    swprintf_s(buf, 64, L"%s / %s", usedBuf, totalBuf);
    gr.DrawString(buf, -1, g.pFontMid, RectF(14, y, (REAL)loW - 28, 18), &fmtLeft, &text2Brush);
    y += 32.0f;
    }  /* g.showRAM */

    /* --- NET --- */
    if (g.showNET) {
    gr.DrawString(L"NET", -1, g.pFontS, RectF(14, y, 100, 18), &fmtLeft, &text2Brush);
    y += 22.0f;
    WCHAR upBuf[16], dnBuf[16];
    format_speed(d->upload_bps, upBuf, 16);
    format_speed(d->download_bps, dnBuf, 16);
    swprintf_s(buf, 64, L"\x2191 %s/s", upBuf);
    SolidBrush cyanBr(Color(((C_CYAN & 0x00FFFFFF) | ((UINT)globalAlpha << 24))));
    gr.DrawString(buf, -1, g.pFontMain, RectF(14, y, 100, 18), &fmtLeft, &cyanBr);
    swprintf_s(buf, 64, L"\x2193 %s/s", dnBuf);
    SolidBrush blueBr(Color(((C_BLUE & 0x00FFFFFF) | ((UINT)globalAlpha << 24))));
    gr.DrawString(buf, -1, g.pFontMain, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &blueBr);
    y += 34.0f;
    }  /* g.showNET */

    /* --- DISK --- */
    if (g.showDISK) {
    gr.DrawString(L"DISK", -1, g.pFontS, RectF(14, y, 100, 18), &fmtLeft, &text2Brush);
    y += 22.0f;
    if (d->disk_c_ok) {
        ARGB dc = get_color(d->disk_c_pct);
        gr.DrawString(L"C:", -1, g.pFontMid, RectF(14, y, 30, 18), &fmtLeft, &text2Brush);
        draw_progress_bar(&gr, 40, y + 6, 100.0f, 4.0f, d->disk_c_pct, dc, globalAlpha);
        format_bytes(d->disk_c_used, usedBuf, 16);
        format_bytes(d->disk_c_total, totalBuf, 16);
        swprintf_s(buf, 64, L"%.0f%%  %s/%s", d->disk_c_pct, usedBuf, totalBuf);
        gr.DrawString(buf, -1, g.pFontMid, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &text2Brush);
        y += 22.0f;
    }
    if (d->disk_d_ok) {
        ARGB dd = get_color(d->disk_d_pct);
        gr.DrawString(L"D:", -1, g.pFontMid, RectF(14, y, 30, 18), &fmtLeft, &text2Brush);
        draw_progress_bar(&gr, 40, y + 6, 100.0f, 4.0f, d->disk_d_pct, dd, globalAlpha);
        format_bytes(d->disk_d_used, usedBuf, 16);
        format_bytes(d->disk_d_total, totalBuf, 16);
        swprintf_s(buf, 64, L"%.0f%%  %s/%s", d->disk_d_pct, usedBuf, totalBuf);
        gr.DrawString(buf, -1, g.pFontMid, RectF(0, y, (REAL)loW - 14, 18), &fmtRight, &text2Brush);
    }
    }  /* g.showDISK */

    /* === 缩放到 1x === */
    gr.ResetTransform();
    gr.Flush();
    Bitmap bmpLo(loW, loH, PixelFormat32bppARGB);
    Graphics grLo(&bmpLo);
    grLo.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    grLo.DrawImage(&bmpHi, 0, 0, loW, loH);
    grLo.Flush();

    /* 拷贝到 DIB + straight→premultiplied alpha 转换 */
    BitmapData bmpData;
    Rect rc(0, 0, loW, loH);
    bmpLo.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bmpData);
    BYTE* pDIB = (BYTE*)g.pPanelBits;
    BYTE* pSrc = (BYTE*)bmpData.Scan0;
    for (int row = 0; row < loH; row++) {
        for (int col = 0; col < loW; col++) {
            int si = row * bmpData.Stride + col * 4;
            int di = row * loW * 4 + col * 4;
            premultiply_pixel(pDIB + di, pSrc[si], pSrc[si+1], pSrc[si+2], pSrc[si+3]);
        }
    }
    bmpLo.UnlockBits(&bmpData);
}

/* ================================================================
 * SECTION 8: 窗口管理
 * ================================================================ */

/* 创建分层窗口的DIB资源 */
static BOOL create_dib(HWND hwnd, int w, int h, HBITMAP* phBmp, HDC* phDC, void** ppBits) {
    HDC hdcScreen = GetDC(NULL);
    *phDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;    /* 负值 = top-down DIB */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    *phBmp = CreateDIBSection(*phDC, &bi, DIB_RGB_COLORS, ppBits, NULL, 0);
    SelectObject(*phDC, *phBmp);
    ReleaseDC(NULL, hdcScreen);
    return *phBmp != NULL;
}

/* 使用UpdateLayeredWindow更新分层窗口 — sourceAlpha 控制合成透明度(0-255) */
static void update_layered(HWND hwnd, HDC hdcMem, int w, int h, int x, int y, BYTE sourceAlpha = 255) {
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, sourceAlpha, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0};
    POINT ptDst = {x, y};
    SIZE sz = {w, h};
    UpdateLayeredWindow(hwnd, NULL, &ptDst, &sz, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);
}

/* 球体透明度对应的 SourceConstantAlpha */
#define BALL_SA  ((BYTE)(g.ballAlpha * 255))

/* 显示/移动悬浮球 */
static void show_ball(int x, int y) {
    g.ballX = x; g.ballY = y;
    HWND insertAfter = g.topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(g.hBallWnd, insertAfter, x, y, BALL_D, BALL_D, SWP_NOACTIVATE);
    ShowWindow(g.hBallWnd, SW_SHOWNOACTIVATE);
    update_layered(g.hBallWnd, g.hBallDC, BALL_D, BALL_D, x, y, BALL_SA);
}

static void move_ball(int x, int y) {
    g.ballX = x; g.ballY = y;
    update_layered(g.hBallWnd, g.hBallDC, BALL_D, BALL_D, x, y, BALL_SA);
}

/* 贴边吸附 */
static void snap_ball(void) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int nx = max(0, min(g.ballX, sw - BALL_D));
    int ny = max(0, min(g.ballY, sh - BALL_D));
    if (g.ballX < SNAP_DIST) nx = 0;
    else if (g.ballX + BALL_D > sw - SNAP_DIST) nx = sw - BALL_D;
    if (g.ballY < SNAP_DIST) ny = 0;
    else if (g.ballY + BALL_D > sh - SNAP_DIST) ny = sh - BALL_D;
    if (nx != g.ballX || ny != g.ballY) move_ball(nx, ny);
}

/* 检查/设置开机自启（注册表 HKCU Run） */
static BOOL check_autostart(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = MAX_PATH;
        WCHAR val[MAX_PATH];
        BOOL found = (RegQueryValueExW(hKey, L"FloatMon", NULL, NULL, (BYTE*)val, &size) == ERROR_SUCCESS);
        RegCloseKey(hKey);
        return found;
    }
    return FALSE;
}

/* 保存/加载 位置和设置到 HKCU\Software\FloatMon */
static void save_settings(void) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\FloatMon", 0, NULL,
                        0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD v = (DWORD)g.ballX; RegSetValueExW(hKey, L"BallX", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        v = (DWORD)g.ballY; RegSetValueExW(hKey, L"BallY", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        v = g.topmost ? 1 : 0; RegSetValueExW(hKey, L"Topmost", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        v = (DWORD)(g.ballAlpha * 100.0f + 0.5f); RegSetValueExW(hKey, L"Alpha", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

static void load_settings(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\FloatMon", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD v, size = sizeof(v);
        if (RegQueryValueExW(hKey, L"BallX", NULL, NULL, (BYTE*)&v, &size) == ERROR_SUCCESS) g.ballX = (int)v;
        size = sizeof(v);
        if (RegQueryValueExW(hKey, L"BallY", NULL, NULL, (BYTE*)&v, &size) == ERROR_SUCCESS) g.ballY = (int)v;
        size = sizeof(v);
        if (RegQueryValueExW(hKey, L"Topmost", NULL, NULL, (BYTE*)&v, &size) == ERROR_SUCCESS) g.topmost = v != 0;
        size = sizeof(v);
        if (RegQueryValueExW(hKey, L"Alpha", NULL, NULL, (BYTE*)&v, &size) == ERROR_SUCCESS && v <= 100)
            g.ballAlpha = v / 100.0f;
        RegCloseKey(hKey);
    }
}

static void toggle_autostart(void) {
    g.autostart = !g.autostart;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        if (g.autostart) {
            WCHAR path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, L"FloatMon", 0, REG_SZ, (BYTE*)path,
                           (DWORD)((wcslen(path) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"FloatMon");
        }
        RegCloseKey(hKey);
    }
    CheckMenuItem(g.hMenu, IDM_AUTOSTART, g.autostart ? MF_CHECKED : MF_UNCHECKED);
}

/* 切换置顶 */
static void toggle_topmost(void) {
    g.topmost = !g.topmost;
    HWND insertAfter = g.topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(g.hBallWnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    CheckMenuItem(g.hMenu, IDM_TOGGLE_TOP, g.topmost ? MF_CHECKED : MF_UNCHECKED);
}

/* 设置悬浮球透明度 — 立即刷新视图 */
static void set_ball_alpha(float a) {
    g.ballAlpha = a;
    update_layered(g.hBallWnd, g.hBallDC, BALL_D, BALL_D, g.ballX, g.ballY, BALL_SA);
}

/* 面板窗口过程 */
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;  /* 鼠标事件穿透到悬浮球检测 */
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 创建面板窗口 */
static void create_panel(void) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"FloatMonPanel";
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    g.hPanelWnd = CreateWindowExW(exStyle, L"FloatMonPanel", L"", WS_POPUP,
                                   0, 0, PANEL_W, PANEL_H, NULL, NULL, g.hInst, NULL);
    create_dib(g.hPanelWnd, PANEL_W, PANEL_H, &g.hPanelBmp, &g.hPanelDC, &g.pPanelBits);

    /* 面板始终可见，初始化全透明帧 */
    memset(g.pPanelBits, 0, PANEL_W * PANEL_H * 4);
    update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, -PANEL_W, 0);
    ShowWindow(g.hPanelWnd, SW_SHOWNOACTIVATE);
}

/* 计算面板位置(悬浮球左侧或右侧) */
static void calc_panel_pos(int* px, int* py) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    if (g.ballX + BALL_D + PANEL_W + 20 < sw) {
        *px = g.ballX + BALL_D + 10;
    } else {
        *px = g.ballX - PANEL_W - 10;
    }
    *py = max(10, g.ballY - (PANEL_H - BALL_D) / 2);
}

/* 显示面板 — 内容缓存在全不透明度，BLENDFUNCTION.SourceConstantAlpha 控制淡入 */
static void show_panel(void) {
    if (g.panelVisible) return;
    g.panelVisible = TRUE;

    int px, py;
    calc_panel_pos(&px, &py);

    /* 内容只渲染一次（全不透明度），后续 fade 通过 SourceConstantAlpha 控制 */
    render_panel(&g.data, 255);
    update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, 0);

    g.fadeDir = 1;
    g.panelAlpha = 0.0f;
    SetTimer(g.hBallWnd, TIMER_FADE, FADE_DELAY, NULL);
}

static void start_hide_panel(void) {
    if (!g.panelVisible || g.fadeDir != 0) return;

    /* 淡出开始前先渲染一帧全不透明内容作为淡出的基础 */
    int px, py;
    calc_panel_pos(&px, &py);
    render_panel(&g.data, 255);
    update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, 235);

    g.panelAlpha = 0.0f;  /* reset counter */
    g.fadeDir = -1;
    SetTimer(g.hBallWnd, TIMER_FADE, FADE_DELAY, NULL);
}

static void destroy_panel(void) {
    g.panelVisible = FALSE;
    g.panelAlpha = 0.0f;
    g.fadeDir = 0;
}

/* 淡入淡出步进 — g.panelAlpha 始终 0→1，fadeDir 决定是淡入还是淡出 */
static void fade_tick(void) {
    if (g.fadeDir == 0) return;

    float step = 1.0f / FADE_STEPS;
    g.panelAlpha += step;

    if (g.panelAlpha >= 1.0f) {
        KillTimer(g.hBallWnd, TIMER_FADE);
        if (g.fadeDir > 0) {
            /* 淡入完成 */
            g.fadeDir = 0;
            int px, py; calc_panel_pos(&px, &py);
            update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, 235);
        } else {
            /* 淡出完成 */
            int px, py; calc_panel_pos(&px, &py);
            update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, 0);
            destroy_panel();
        }
        return;
    }

    /* t³ 缓入/缓出曲线，SourceConstantAlpha 在合成阶段控制透明度 */
    float t = g.panelAlpha;
    BYTE sa;
    if (g.fadeDir > 0)        sa = (BYTE)(t * t * t * 235);
    else                       { float inv = 1.0f - t; sa = (BYTE)(inv * inv * inv * 235); }

    int px, py; calc_panel_pos(&px, &py);
    update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, sa);
}

/* 刷新所有内容 */
static void refresh_all(void) {
    monitor_poll();
    render_ball(&g.data);
    update_layered(g.hBallWnd, g.hBallDC, BALL_D, BALL_D, g.ballX, g.ballY, BALL_SA);
    if (g.panelVisible && g.fadeDir == 0) {
        int px, py;
        calc_panel_pos(&px, &py);
        render_panel(&g.data, 255);  /* 内容始终全不透明度，SourceAlpha 单独控透明度 */
        update_layered(g.hPanelWnd, g.hPanelDC, PANEL_W, PANEL_H, px, py, 235);
    }
}

/* ================================================================
 * SECTION 9: 菜单
 * ================================================================ */

static void build_menu(void) {
    g.hMenu = CreatePopupMenu();
    g.hAlphaMenu = CreatePopupMenu();

    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_50, L"50%");
    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_60, L"60%");
    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_70, L"70%");
    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_80, L"80%");
    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_90, L"90%");
    AppendMenuW(g.hAlphaMenu, MF_STRING, IDM_ALPHA_100, L"100%");

    AppendMenuW(g.hMenu, MF_STRING | (g.autostart ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(g.hMenu, MF_STRING | MF_CHECKED, IDM_TOGGLE_TOP, L"置顶");
    AppendMenuW(g.hMenu, MF_POPUP, (UINT_PTR)g.hAlphaMenu, L"透明度");
    AppendMenuW(g.hMenu, MF_SEPARATOR, 0, NULL);

    /* 详情显示子菜单 */
    g.hShowMenu = CreatePopupMenu();
    AppendMenuW(g.hShowMenu, MF_STRING | MF_CHECKED, IDM_SHOW_CPU,  L"CPU");
    AppendMenuW(g.hShowMenu, MF_STRING | MF_CHECKED, IDM_SHOW_GPU,  L"GPU");
    AppendMenuW(g.hShowMenu, MF_STRING | MF_CHECKED, IDM_SHOW_RAM,  L"RAM");
    AppendMenuW(g.hShowMenu, MF_STRING | MF_CHECKED, IDM_SHOW_NET,  L"NET");
    AppendMenuW(g.hShowMenu, MF_STRING | MF_CHECKED, IDM_SHOW_DISK, L"DISK");
    AppendMenuW(g.hMenu, MF_POPUP, (UINT_PTR)g.hShowMenu, L"详情显示");

    AppendMenuW(g.hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g.hMenu, MF_STRING, IDM_EXIT, L"退出");

    update_alpha_checkmarks();  /* 初始勾选 100% */
}

/* 更新透明度菜单的勾选状态 */
static void update_alpha_checkmarks(void) {
    UINT ids[] = {IDM_ALPHA_50, IDM_ALPHA_60, IDM_ALPHA_70, IDM_ALPHA_80, IDM_ALPHA_90, IDM_ALPHA_100};
    float alphas[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};
    for (int i = 0; i < 6; i++) {
        CheckMenuItem(g.hAlphaMenu, ids[i],
            (fabs(g.ballAlpha - alphas[i]) < 0.01f) ? MF_CHECKED : MF_UNCHECKED);
    }
}

static void show_context_menu(int x, int y) {
    g.menuX = x; g.menuY = y;
    update_alpha_checkmarks();
    SetForegroundWindow(g.hBallWnd);

    for (;;) {
        int cmd = TrackPopupMenu(g.hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                                 x, y, 0, g.hBallWnd, NULL);
        if (cmd == 0) break;

        /* 子菜单项：处理切换后异步重开子菜单 */
        BOOL handled = FALSE;
        switch (cmd) {
        case IDM_SHOW_CPU:  g.showCPU  = !g.showCPU;  CheckMenuItem(g.hShowMenu, IDM_SHOW_CPU,  g.showCPU  ? MF_CHECKED : MF_UNCHECKED); handled = TRUE; break;
        case IDM_SHOW_GPU:  g.showGPU  = !g.showGPU;  CheckMenuItem(g.hShowMenu, IDM_SHOW_GPU,  g.showGPU  ? MF_CHECKED : MF_UNCHECKED); handled = TRUE; break;
        case IDM_SHOW_RAM:  g.showRAM  = !g.showRAM;  CheckMenuItem(g.hShowMenu, IDM_SHOW_RAM,  g.showRAM  ? MF_CHECKED : MF_UNCHECKED); handled = TRUE; break;
        case IDM_SHOW_NET:  g.showNET  = !g.showNET;  CheckMenuItem(g.hShowMenu, IDM_SHOW_NET,  g.showNET  ? MF_CHECKED : MF_UNCHECKED); handled = TRUE; break;
        case IDM_SHOW_DISK: g.showDISK = !g.showDISK; CheckMenuItem(g.hShowMenu, IDM_SHOW_DISK, g.showDISK ? MF_CHECKED : MF_UNCHECKED); handled = TRUE; break;
        }
        if (handled) {
            PostMessageW(g.hBallWnd, WM_USER+80, x, y);
            continue;
        }

        SendMessageW(g.hBallWnd, WM_COMMAND, cmd, 0);
        break;
    }
}

/* ================================================================
 * SECTION 10: 主窗口过程
 * ================================================================ */

static LRESULT CALLBACK BallWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hBallWnd = hwnd;  /* WinMain 里 CreateWindowEx 返回前 WM_CREATE 就触发了，必须提前设置 */

        /* 创建DIB资源 */
        create_dib(hwnd, BALL_D, BALL_D, &g.hBallBmp, &g.hBallDC, &g.pBallBits);

        /* 初始化字体 */
        {
            GdiplusStartupInput gdiSI;
            GdiplusStartup(&g.gdiToken, &gdiSI, NULL);

            g.pFontMain = new Font(L"Consolas", 14.0f, FontStyleRegular, UnitPixel);
            g.pFontMid  = new Font(L"Consolas", 10.0f, FontStyleRegular, UnitPixel);
            g.pFontS    = new Font(L"Segoe UI", 10.0f, FontStyleRegular, UnitPixel);
            g.pFontM    = new Font(L"Segoe UI", 13.0f, FontStyleRegular, UnitPixel);
        }

        /* 初始化监控 */
        monitor_init();
        refresh_all();

        /* 启动定时器 */
        SetTimer(hwnd, TIMER_POLL, POLL_MS_IDLE, NULL);
        SetTimer(hwnd, WM_FLOATMON_REFRESH, REFRESH_MS, NULL);
        break;

    case WM_TIMER:
        switch (wp) {
        case TIMER_POLL: {
            /* 轮询鼠标状态 */
            POINT pt;
            GetCursorPos(&pt);

            BOOL inBall = pt.x >= g.ballX && pt.x <= g.ballX + BALL_D &&
                          pt.y >= g.ballY && pt.y <= g.ballY + BALL_D;

            /* 检测面板区域 */
            BOOL inPanel = FALSE;
            if (g.panelVisible) {
                RECT rc;
                if (GetWindowRect(g.hPanelWnd, &rc)) {
                    inPanel = PtInRect(&rc, pt);
                }
            }

            /* 鼠标按钮状态 */
            BOOL lbtnDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            BOOL lbtnClicked = lbtnDown && !g.lbtnWas;
            g.lbtnWas = lbtnDown;
            BOOL rbtnDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            BOOL rbtnClicked = rbtnDown && !g.rbtnWas;
            g.rbtnWas = rbtnDown;

            /* --- 拖拽 --- */
            if (lbtnDown) {
                if (inBall && !g.dragging) {
                    g.dragging = TRUE;
                    g.dragMoved = FALSE;
                    g.dragStartX = pt.x;
                    g.dragStartY = pt.y;
                    g.dragWinX = g.ballX;
                    g.dragWinY = g.ballY;
                    /* 拖拽开始：暂停数据刷新，淡出面板 */
                    KillTimer(hwnd, TIMER_HOVER);
                    KillTimer(hwnd, WM_FLOATMON_REFRESH);  /* 暂停每秒渲染 */
                    start_hide_panel();
                } else if (g.dragging) {
                    int nx = g.dragWinX + (pt.x - g.dragStartX);
                    int ny = g.dragWinY + (pt.y - g.dragStartY);
                    if (abs(nx - g.ballX) > 2 || abs(ny - g.ballY) > 2)
                        g.dragMoved = TRUE;
                    move_ball(nx, ny);
                }
            } else {
                if (g.dragging) {
                    g.dragging = FALSE;
                    if (g.dragMoved) snap_ball();
                    /* 恢复空闲轮询速率 + 恢复数据刷新 */
                    SetTimer(hwnd, TIMER_POLL, POLL_MS_IDLE, NULL);
                    SetTimer(hwnd, WM_FLOATMON_REFRESH, REFRESH_MS, NULL);
                    /* 拖拽结束：如果鼠标还在球上，重新启动悬停检测 */
                    if (inBall) {
                        KillTimer(hwnd, TIMER_HIDE);
                        KillTimer(hwnd, TIMER_HOVER);
                        SetTimer(hwnd, TIMER_HOVER, HOVER_DELAY, NULL);
                    }
                }
            }
            /* 拖拽时使用更快的轮询速率 */
            if (g.dragging) {
                SetTimer(hwnd, TIMER_POLL, POLL_MS_DRAG, NULL);
            }

            /* --- 右键菜单 --- */
            if (rbtnClicked && inBall) {
                KillTimer(hwnd, TIMER_HOVER);
                KillTimer(hwnd, TIMER_HIDE);
                show_context_menu(pt.x, pt.y);
            }

            /* --- 悬停检测 --- */
            if (!g.dragging) {
                BOOL was = g.hovering;
                g.hovering = inBall || inPanel;
                if (g.hovering && !was) {
                    KillTimer(hwnd, TIMER_HIDE);
                    SetTimer(hwnd, TIMER_HOVER, HOVER_DELAY, NULL);
                } else if (!g.hovering && was) {
                    KillTimer(hwnd, TIMER_HOVER);
                    SetTimer(hwnd, TIMER_HIDE, HIDE_DELAY, NULL);
                }
            }
            break;
        }
        case TIMER_HOVER:
            KillTimer(hwnd, TIMER_HOVER);
            show_panel();
            break;
        case TIMER_HIDE:
            KillTimer(hwnd, TIMER_HIDE);
            if (!g.hovering) start_hide_panel();
            break;
        case TIMER_FADE:
            fade_tick();
            break;
        case WM_FLOATMON_REFRESH:
            if (!g.dragging) refresh_all();  /* 拖拽期间不刷新 */
            break;
        case WM_USER+80:
            /* 主菜单 modal 循环内异步打开详情子菜单,连续勾选不关闭 */
            for (;;) {
                int c = TrackPopupMenu(g.hShowMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                       (int)wp + 160, (int)lp + 75, 0, hwnd, NULL);
                if (c == 0) break;
                switch (c) {
                case IDM_SHOW_CPU:  g.showCPU  = !g.showCPU;  CheckMenuItem(g.hShowMenu, IDM_SHOW_CPU,  g.showCPU  ? MF_CHECKED : MF_UNCHECKED); break;
                case IDM_SHOW_GPU:  g.showGPU  = !g.showGPU;  CheckMenuItem(g.hShowMenu, IDM_SHOW_GPU,  g.showGPU  ? MF_CHECKED : MF_UNCHECKED); break;
                case IDM_SHOW_RAM:  g.showRAM  = !g.showRAM;  CheckMenuItem(g.hShowMenu, IDM_SHOW_RAM,  g.showRAM  ? MF_CHECKED : MF_UNCHECKED); break;
                case IDM_SHOW_NET:  g.showNET  = !g.showNET;  CheckMenuItem(g.hShowMenu, IDM_SHOW_NET,  g.showNET  ? MF_CHECKED : MF_UNCHECKED); break;
                case IDM_SHOW_DISK: g.showDISK = !g.showDISK; CheckMenuItem(g.hShowMenu, IDM_SHOW_DISK, g.showDISK ? MF_CHECKED : MF_UNCHECKED); break;
                }
                PostMessageW(hwnd, WM_USER+80, wp, lp);
            }
            break;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_AUTOSTART:
            toggle_autostart();
            break;
        case IDM_TOGGLE_TOP:
            toggle_topmost();
            break;
        case IDM_ALPHA_50:  set_ball_alpha(0.5f); update_alpha_checkmarks(); break;
        case IDM_ALPHA_60:  set_ball_alpha(0.6f); update_alpha_checkmarks(); break;
        case IDM_ALPHA_70:  set_ball_alpha(0.7f); update_alpha_checkmarks(); break;
        case IDM_ALPHA_80:  set_ball_alpha(0.8f); update_alpha_checkmarks(); break;
        case IDM_ALPHA_90:  set_ball_alpha(0.9f); update_alpha_checkmarks(); break;
        case IDM_ALPHA_100: set_ball_alpha(1.0f); update_alpha_checkmarks(); break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DISPLAYCHANGE:
        /* 屏幕分辨率变化时重新吸附 */
        snap_ball();
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_POLL);
        KillTimer(hwnd, TIMER_HOVER);
        KillTimer(hwnd, TIMER_HIDE);
        KillTimer(hwnd, TIMER_FADE);
        KillTimer(hwnd, WM_FLOATMON_REFRESH);

        monitor_cleanup();

        /* 保存位置和设置 */
        save_settings();

        /* 清理GDI+ */
        delete g.pFontMain;
        delete g.pFontMid;
        delete g.pFontS;
        delete g.pFontM;
        GdiplusShutdown(g.gdiToken);

        /* 清理DIB */
        if (g.hBallBmp) DeleteObject(g.hBallBmp);
        if (g.hBallDC) DeleteDC(g.hBallDC);
        if (g.hPanelBmp) DeleteObject(g.hPanelBmp);
        if (g.hPanelDC) DeleteDC(g.hPanelDC);

        /* 清理面板窗口 */
        if (g.hPanelWnd) DestroyWindow(g.hPanelWnd);

        /* 清理菜单 */
        if (g.hAlphaMenu) DestroyMenu(g.hAlphaMenu);
        if (g.hShowMenu) DestroyMenu(g.hShowMenu);
        if (g.hMenu) DestroyMenu(g.hMenu);

        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================
 * SECTION 11: WinMain 入口
 * ================================================================ */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    /* 防止多开 */
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"FloatMon_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS && hMutex) {
        MessageBoxW(NULL, L"FloatMon 已在运行中", L"悬浮球", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 0;
    }

    /* DPI 感知 — 尝试 Win10+ PerMonitorV2，失败则回退 Vista 方式 */
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            typedef BOOL (WINAPI *SetDPIFn)(HANDLE);
            SetDPIFn fn = (SetDPIFn)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (fn)
                fn((HANDLE)(INT_PTR)-4);  /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            else
                SetProcessDPIAware();
        }
    }

    memset(&g, 0, sizeof(g));
    g.hInst = hInstance;
    g.topmost = TRUE;
    g.ballAlpha = 1.0f;
    g.showCPU = g.showGPU = g.showRAM = g.showNET = g.showDISK = TRUE;
    g.autostart = check_autostart();
    load_settings();  /* 恢复上次保存的位置/置顶/透明度 */

    init_premul();  /* 预计算 premultiplied alpha 表 */

    /* 注册悬浮球窗口类 */
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = BallWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"FloatMonBall";
    RegisterClassExW(&wc);

    /* 创建悬浮球窗口 */
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    g.hBallWnd = CreateWindowExW(exStyle, L"FloatMonBall", L"FloatMon", WS_POPUP,
                                  0, 0, BALL_D, BALL_D, NULL, NULL, hInstance, NULL);

    /* 初始位置: 上次保存位置，首次运行用右上角 */
    if (g.ballX == 0 && g.ballY == 0) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        g.ballX = sw - BALL_D - 40;
        g.ballY = 80;
    }
    show_ball(g.ballX, g.ballY);

    /* 构建菜单 */
    build_menu();

    /* 创建面板窗口(隐藏) */
    create_panel();

    /* 消息循环 */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return (int)msg.wParam;
}
