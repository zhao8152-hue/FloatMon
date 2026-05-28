# FloatMon - 桌面悬浮球系统监控工具

实时显示 CPU、内存、GPU、网络和磁盘使用率的桌面悬浮球，Win32 原生分层窗口渲染，圆角透明无锯齿。

## 功能

- **实时监控**：CPU 使用率/温度、内存、GPU（NVIDIA NVML）、网络速度、磁盘使用率
- **悬浮球显示**：圆形球体 + 双环进度条（CPU 外环 / MEM 内环）、渐变色阶
- **悬停面板**：鼠标悬停 350ms 后淡入详情面板，显示完整系统数据
- **拖拽移动**：左键拖拽任意位置，自动贴边吸附
- **置顶切换**：右键菜单切换窗口置顶/非置顶
- **防最小化**：点击"显示桌面"时不会消失
- **透明度调节**：右键菜单 50%-100% 五档透明度

## 系统要求

- Windows 10/11
- Python 3.9+
- NVIDIA GPU（可选，用于 GPU 监控）

## 安装

```bash
pip install -r requirements.txt
```

## 运行

```bash
# 方式 1：顶层入口
python run.py

# 方式 2：包入口
python -m float_mon
```

## 打包为 EXE

```bash
pip install pyinstaller
pyinstaller --onefile --noconsole --name FloatingBall run.py
# 输出：dist/FloatingBall.exe
```

## 操作说明

| 操作 | 说明 |
|------|------|
| 左键拖拽 | 移动悬浮球 |
| 右键点击 | 打开菜单（置顶/透明度/退出） |
| 鼠标悬停 | 显示系统详情面板 |
| 移开鼠标 | 面板淡出消失 |

## 项目结构

```
├── run.py                    # 顶层入口
├── requirements.txt          # Python 依赖
├── float_mon/
│   ├── app.py                # 应用主逻辑
│   ├── config.py             # 配置常量
│   ├── monitor.py            # 系统数据采集
│   ├── renderer.py           # PIL 渲染器
│   ├── utils.py              # 工具函数
│   └── win32_layered.py      # Win32 分层窗口封装
```

## 技术栈

- **Python 3.13**
- **tkinter** — 菜单和面板窗口
- **Win32 API** — `UpdateLayeredWindow` per-pixel alpha 渲染
- **Pillow (PIL)** — 3x 超采样抗锯齿渲染
- **psutil** — 系统资源采集
- **pynvml (nvidia-ml-py)** — NVIDIA GPU 监控

## License

MIT
