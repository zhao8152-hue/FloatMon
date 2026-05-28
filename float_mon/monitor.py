"""
系统数据采集 — CPU、内存、GPU、网络、磁盘
"""

import time
import psutil

try:
    import pynvml as nvml
    nvml.nvmlInit()
    GPU_OK = True
except Exception:
    nvml = None
    GPU_OK = False


class SystemMonitor:
    """系统资源监控器，每次 poll() 返回完整数据快照"""

    def __init__(self):
        self._net = psutil.net_io_counters()
        self._t = time.time()
        self._gpu_handle = None
        if GPU_OK:
            try:
                self._gpu_handle = nvml.nvmlDeviceGetHandleByIndex(0)
            except Exception:
                pass

    def poll(self):
        """采集所有系统指标，返回字典"""
        cpu = psutil.cpu_percent(0)
        mem = psutil.virtual_memory()
        now = time.time()

        # 网络速度（差值计算）
        cn = psutil.net_io_counters()
        dt = max(now - self._t, 0.001)
        up = (cn.bytes_sent - self._net.bytes_sent) / dt
        dn = (cn.bytes_recv - self._net.bytes_recv) / dt
        self._net, self._t = cn, now

        # CPU温度
        cpu_temp = self._read_cpu_temp()

        # GPU信息
        gpu = self._read_gpu()

        # 磁盘信息
        disks = {}
        for letter in "CD":
            try:
                u = psutil.disk_usage(f"{letter}:\\")
                disks[letter] = {"total": u.total, "used": u.used, "pct": u.percent}
            except Exception:
                pass

        return {
            "cpu": cpu, "cpu_temp": cpu_temp,
            "mem_pct": mem.percent, "mem_used": mem.used, "mem_total": mem.total,
            "upload": up, "download": dn,
            "gpu": gpu, "disks": disks,
        }

    def _read_cpu_temp(self):
        try:
            ts = psutil.sensors_temperatures()
            if ts:
                for entries in ts.values():
                    if entries:
                        return entries[0].current
        except Exception:
            pass
        return None

    def _read_gpu(self):
        if not GPU_OK or not self._gpu_handle:
            return None
        try:
            util = nvml.nvmlDeviceGetUtilizationRates(self._gpu_handle)
            temp = nvml.nvmlDeviceGetTemperature(self._gpu_handle, nvml.NVML_TEMPERATURE_GPU)
            mem = nvml.nvmlDeviceGetMemoryInfo(self._gpu_handle)
            return {
                "usage": util.gpu, "temp": temp,
                "mem_used": mem.used, "mem_total": mem.total,
                "mem_pct": mem.used / mem.total * 100 if mem.total else 0,
            }
        except Exception:
            return None


def gpu_shutdown():
    """安全关闭NVML"""
    if GPU_OK:
        try:
            nvml.nvmlShutdown()
        except Exception:
            pass
