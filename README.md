# FloatMon — 桌面悬浮球系统监控工具

纯 C++ / Win32 API / GDI+ 实现的轻量级桌面悬浮球，实时显示系统资源占用情况。独立单文件 .exe，无需运行时。

## 功能

| 功能 | 说明 |
|------|------|
| **悬浮球** | 圆形球体显示 CPU 使用率、内存占用、网络实时速度（↑↓），渐变弧线环 |
| **详情面板** | 鼠标悬停 350ms 后淡入，显示 CPU / GPU / RAM / NET / DISK 完整数据 |
| **拖拽** | 左键拖拽任意位置，松手自动贴边吸附，拖拽时面板自动隐藏 |
| **右键菜单** | 置顶切换、透明度 50%-100% 调节、详情区块自定义开关、开机自启 |
| **淡入淡出** | 面板 cubic 缓动淡入淡出动画，视觉流畅 |
| **位置记忆** | 退出自动保存位置和设置，重启恢复 |
| **开机自启** | 一键注册/取消系统启动项 |
| **自定义图标** | 黑洞+吸积盘风格图标 |

## 系统要求

- Windows 7+ (64-bit)
- NVIDIA GPU（可选，用于 GPU 温度/显存监控）

## 使用下载

从 [Releases](https://github.com/zhao8152-hue/FloatMon/releases) 下载 `FloatMon.exe`，双击运行即可。绿色免安装。

## 编译

需要 MinGW-w64 64-bit：

```bash
# 进入项目目录
cd FloatMon

# 编译资源文件
windres resource.rc resource.o

# 编译（静态链接，单文件 Exe）
g++ -o FloatMon.exe floatmon.cpp resource.o \
    -lgdi32 -lgdiplus -luser32 -lshell32 -lpdh -liphlpapi -lole32 -ladvapi32 \
    -mwindows -O2 -s -static -static-libgcc -static-libstdc++ -std=c++17
```

或双击 `build.bat` 一键编译。

## 操作说明

| 操作 | 说明 |
|------|------|
| 左键拖拽 | 移动悬浮球，松手贴边 |
| 右键点击 | 打开设置菜单 |
| 鼠标悬停 | 显示系统详情面板 |
| 移开鼠标 | 面板淡出消失 |

## 技术栈

| 技术 | 用途 |
|------|------|
| **C++17 / Win32** | 主框架，分层窗口渲染 |
| **GDI+** | 圆角抗锯齿绘制，2x 超采样 + Straight→Premultiplied Alpha 转换 |
| **PDH** | CPU 使用率采集 |
| **NVML** | NVIDIA GPU 温度/使用率/显存 |
| **IPHLPAPI** | 网络实时速度 |
| **Registry** | 开机自启 / 位置记忆 |

## 项目结构

```
├── FloatMon/
│   ├── FloatMon.exe        # 独立可执行文件
│   ├── floatmon.cpp        # 完整源码 (C++17)
│   ├── icon.ico            # 应用图标
│   ├── resource.rc         # 资源脚本
│   └── make_icon.py        # 图标生成脚本
├── build.bat               # 一键编译
└── README.md
```

## License

MIT
